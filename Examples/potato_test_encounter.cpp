// Story 12.5 — encounter pipeline (potato.encounter/1): world
// triggers marshal a battle into the three-beat BattleController
// and the aftermath writes back to the world — control stakes,
// resolved markers, sealed verdicts, mint claims.
#include "Campaign/Chain/ClaimGate.h"
#include "Campaign/Chain/MintOutbox.h"
#include "Campaign/Characters/Character.h"
#include "Campaign/Ledger/CrossCheck.h"
#include "Campaign/Ledger/Ledger.h"
#include "Campaign/State/CampaignState.h"
#include "Campaign/World/Encounter.h"
#include "Campaign/World/EncounterSettle.h"
#include "Campaign/World/WorldMap.h"
#include "Campaign/World/WorldState.h"
#include "Gameplay/Doctrine/Doctrine.h"
#include "Gameplay/Json/Json.h"
#include "Gameplay/Json/JsonValue.h"
#include "Gameplay/Map/BattleMap.h"
#include "Gameplay/Record/BattleRecorder.h"
#include "Gameplay/Sim/BattleController.h"
#include "Gameplay/Squad/Squad.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

int failures = 0;
void Check(bool cond, const char* name) {
    std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", name);
    if (!cond) ++failures;
}

using Potato::Campaign::AftermathRow;
using Potato::Campaign::CampaignState;
using Potato::Campaign::Character;
using Potato::Campaign::ClaimKind;
using Potato::Campaign::CrossVerdict;
using Potato::Campaign::EmitGatedSettlement;
using Potato::Campaign::EncounterAssembly;
using Potato::Campaign::EncounterDef;
using Potato::Campaign::EncounterLibrary;
using Potato::Campaign::EncounterTrigger;
using Potato::Campaign::Ledger;
using Potato::Campaign::MarshalEncounter;
using Potato::Campaign::MintOutbox;
using Potato::Campaign::PendingEncounters;
using Potato::Campaign::Posting;
using Potato::Campaign::PlayerDeploy;
using Potato::Campaign::RecordRootTag;
using Potato::Campaign::SettleEncounter;
using Potato::Campaign::WorldControl;
using Potato::Campaign::WorldMap;
using Potato::Campaign::WorldState;
using Potato::Gameplay::BattleController;
using Potato::Gameplay::BattleMap;
using Potato::Gameplay::BattleBeat;
using Potato::Gameplay::DoctrineLibrary;
using Potato::Gameplay::JsonValue;
using Potato::Gameplay::SquadSheet;
using Potato::Gameplay::SquadTemplateLibrary;

const char* WORLD_DOC = R"json({
    "schema": "potato.world/1",
    "id": "republic_fall",
    "nodes": [
        {"id": "kaifeng", "control": "player",
         "strategic": ["depot"], "map": "field"},
        {"id": "longmen", "myth": ["shrine"],
         "control": "rival", "map": "field"},
        {"id": "luoyang", "control": "rival"}
    ],
    "routes": [
        {"a": "kaifeng", "b": "longmen", "days": 2},
        {"a": "longmen", "b": "luoyang", "days": 3}
    ],
    "start": "kaifeng"
})json";

const char* MAP_DOC = R"json({
    "schema": "potato.map/1", "id": "field",
    "regions": [{"id": "r0"}, {"id": "r1"}, {"id": "r2"}],
    "edges": [["r0", "r1"], ["r1", "r2"]]
})json";

const char* SQUAD_DOC = R"json({
    "schema": "potato.squad/1",
    "squads": [
        {"id": "militia", "unit": "infantry", "speed": "medium",
         "hp": 60, "attack": 6},
        {"id": "vanguard", "unit": "infantry", "speed": "medium",
         "hp": 100, "attack": 10}
    ]
})json";

const char* CARDS_DOC = R"json({
    "schema": "potato.doctrine_cards/1",
    "cards": [
        {"id": "card_brace", "cooldown": 5,
         "trigger": {"type": "enemy_adjacent"},
         "condition": {"type": "cohesion_above", "param": 50},
         "action": {"type": "brace", "param": 15}},
        {"id": "card_hold", "cooldown": 5,
         "trigger": {"type": "always"},
         "action": {"type": "hold"}},
        {"id": "card_flee", "cooldown": 10,
         "trigger": {"type": "cohesion_below", "param": 30},
         "action": {"type": "retreat"}}
    ]
})json";

const char* ENC_ARRIVAL = R"json({
    "schema": "potato.encounter/1",
    "id": "longmen_ambush",
    "node": "longmen",
    "trigger": "arrival",
    "map": "field",
    "defenders": [
        {"template": "militia", "region": 1,
         "deck": ["card_hold", "card_brace", "card_flee"]}
    ],
    "stakes": {"control": "player"},
    "seed": 4242
})json";

const char* ENC_PROX = R"json({
    "schema": "potato.encounter/1",
    "id": "luoyang_banners",
    "node": "luoyang",
    "trigger": "proximity",
    "defenders": [
        {"template": "militia", "region": 0,
         "deck": ["card_hold", "card_brace", "card_flee"]}
    ]
})json";

const char* ENC_POI = R"json({
    "schema": "potato.encounter/1",
    "id": "depot_raid",
    "node": "kaifeng",
    "trigger": "poi",
    "defenders": [
        {"template": "militia", "region": 2,
         "deck": ["card_hold", "card_brace", "card_flee"]}
    ]
})json";

const char* ENC_GONE = R"json({
    "schema": "potato.encounter/1",
    "id": "ghost_field",
    "node": "nowhere",
    "defenders": [
        {"template": "militia", "region": 0,
         "deck": ["card_hold", "card_brace", "card_flee"]}
    ]
})json";

const char* ENC_FALLBACK = R"json({
    "schema": "potato.encounter/1",
    "id": "node_map_fight",
    "node": "longmen",
    "defenders": [
        {"template": "militia", "region": 1,
         "deck": ["card_hold", "card_brace", "card_flee"]}
    ]
})json";

void WriteFile(const fs::path& dir, const char* name,
               const char* text) {
    std::ofstream out(dir / name, std::ios::binary);
    out << text;
}

WorldMap MakeWorld() {
    return WorldMap::FromJson(JsonValue::Parse(WORLD_DOC).value)
        .value;
}

// A real, self-consistent record doc (11.3 test precedent):
// payload + integrity.root recomputed the same way CrossCheck
// does.
JsonValue MakeRecord(std::uint64_t& root) {
    JsonValue::Object payload;
    payload["battle"] = JsonValue::String("longmen_ambush");
    payload["ticks"] = JsonValue::Int(800);
    JsonValue rec = JsonValue::MakeObject(payload);
    root = Potato::Gameplay::BattleRecorder::ComputeRoot(rec);
    JsonValue::Object integ;
    integ["root"] = JsonValue::Int(static_cast<std::int64_t>(root));
    payload["integrity"] = JsonValue::MakeObject(std::move(integ));
    return JsonValue::MakeObject(std::move(payload));
}

} // namespace

int main() {
    const WorldMap world = MakeWorld();
    BattleMap bmap =
        BattleMap::FromJson(JsonValue::Parse(MAP_DOC).value).value;
    SquadTemplateLibrary squads =
        SquadTemplateLibrary::FromJson(
            JsonValue::Parse(SQUAD_DOC).value)
            .value;
    DoctrineLibrary cards =
        DoctrineLibrary::FromJson(JsonValue::Parse(CARDS_DOC).value)
            .value;
    Check(world.NodeCount() == 3, "world fixture loads");
    Check(bmap.RegionCount() == 3, "map fixture loads");
    Check(squads.Count() == 2, "squad fixture loads");
    Check(cards.Count() == 3, "cards fixture loads");

    const fs::path dir =
        fs::temp_directory_path() / "potato_test_encounter";
    fs::remove_all(dir);
    fs::create_directories(dir);

    // --- EncounterLibrary: load + per-file isolation ---
    EncounterLibrary lib;
    {
        WriteFile(dir, "a_arrival.json", ENC_ARRIVAL);
        WriteFile(dir, "b_prox.json", ENC_PROX);
        WriteFile(dir, "c_poi.json", ENC_POI);
        WriteFile(dir, "d_gone.json", ENC_GONE);
        WriteFile(dir, "e_fallback.json", ENC_FALLBACK);
        WriteFile(dir, "f_dup.json", ENC_ARRIVAL); // same id
        WriteFile(dir, "g_bad_top.json",
                  R"({"schema":"potato.encounter/1","id":"x",
                     "node":"longmen","bogus":1,
                     "defenders":[{"template":"militia","region":0,
                     "deck":["card_hold","card_brace",
                             "card_flee"]}]})");
        WriteFile(dir, "h_bad_def.json",
                  R"({"schema":"potato.encounter/1","id":"y",
                     "node":"longmen",
                     "defenders":[{"template":"militia","region":0,
                     "deck":["card_hold","card_brace","card_flee"],
                     "bogus":1}]})");
        WriteFile(dir, "i_bad_trigger.json",
                  R"({"schema":"potato.encounter/1","id":"z",
                     "node":"longmen","trigger":"sneak",
                     "defenders":[{"template":"militia","region":0,
                     "deck":["card_hold","card_brace",
                             "card_flee"]}]})");
        WriteFile(dir, "j_short_deck.json",
                  R"({"schema":"potato.encounter/1","id":"w",
                     "node":"longmen",
                     "defenders":[{"template":"militia","region":0,
                     "deck":["card_hold","card_brace"]}]})");
        WriteFile(dir, "k_bad_stakes.json",
                  R"({"schema":"potato.encounter/1","id":"v",
                     "node":"longmen","stakes":{"control":"gods"},
                     "defenders":[{"template":"militia","region":0,
                     "deck":["card_hold","card_brace",
                             "card_flee"]}]})");
        WriteFile(dir, "l_bad_seed.json",
                  R"({"schema":"potato.encounter/1","id":"u",
                     "node":"longmen","seed":"four",
                     "defenders":[{"template":"militia","region":0,
                     "deck":["card_hold","card_brace",
                             "card_flee"]}]})");
        WriteFile(dir, "m_bad_region.json",
                  R"({"schema":"potato.encounter/1","id":"t",
                     "node":"longmen",
                     "defenders":[{"template":"militia","region":-1,
                     "deck":["card_hold","card_brace",
                             "card_flee"]}]})");
        WriteFile(dir, "n_empty_def.json",
                  R"({"schema":"potato.encounter/1","id":"s",
                     "node":"longmen","defenders":[]})");
        WriteFile(dir, "o_no_node.json",
                  R"({"schema":"potato.encounter/1","id":"r",
                     "defenders":[{"template":"militia","region":0,
                     "deck":["card_hold","card_brace",
                             "card_flee"]}]})");
        auto res = EncounterLibrary::Load(dir, lib);
        Check(res.ok, "library loads");
        Check(lib.Size() == 5, "five encounters accepted");
        Check(res.rejected.size() == 10,
              "ten files rejected (dup + 9 bad)");
        Check(lib.Encounters()[0].id == "depot_raid" &&
                  lib.Encounters()[4].id == "node_map_fight",
              "canonical order is encounter id");
        const EncounterDef* enc =
            lib.Find("longmen_ambush");
        Check(enc != nullptr &&
                  enc->trigger == EncounterTrigger::Arrival &&
                  enc->hasControlStake &&
                  enc->controlStake == WorldControl::Player &&
                  enc->hasSeed && enc->seed == 4242 &&
                  enc->defenders.size() == 1 &&
                  enc->defenders[0].deck.size() == 3,
              "arrival doc decodes fully");
        Check(lib.Find("node_map_fight") != nullptr &&
                  !lib.Find("node_map_fight")->hasSeed &&
                  lib.Find("node_map_fight")->map.empty(),
              "absent trigger/seed/map default");
        Check(lib.Find("ghost_field") != nullptr,
              "vanished-node encounter loads (marshal's check)");
    }

    // --- Library edge: empty dir ok, unreadable fails ---
    {
        EncounterLibrary e2;
        const fs::path empty = dir / "empty";
        fs::create_directories(empty);
        auto r1 = EncounterLibrary::Load(empty, e2);
        Check(r1.ok && e2.Size() == 0 && r1.rejected.empty(),
              "empty dir loads empty");
        EncounterLibrary e3;
        auto r2 = EncounterLibrary::Load(dir / "no_such", e3);
        Check(!r2.ok && r2.error == "io",
              "unreadable dir fails io");
    }

    // --- PendingEncounters: trigger rules ---
    {
        auto ws = WorldState::Init(world, 7).value;
        auto pending = PendingEncounters(ws, world, lib);
        Check(pending.size() == 1 &&
                  pending[0]->id == "depot_raid",
              "start: only poi at warband's flagged node fires");
        Check(ws.SetWarband("longmen", world), "warband moves");
        pending = PendingEncounters(ws, world, lib);
        Check(pending.size() == 3 &&
                  pending[0]->id == "longmen_ambush" &&
                  pending[1]->id == "luoyang_banners" &&
                  pending[2]->id == "node_map_fight",
              "arrival fires at node; proximity reaches next hop");
        Check(ws.MarkResolved("longmen_ambush"),
              "resolved marker lands");
        pending = PendingEncounters(ws, world, lib);
        Check(pending.size() == 2 &&
                  pending[0]->id == "luoyang_banners" &&
                  pending[1]->id == "node_map_fight",
              "resolved encounter leaves the pending set");
    }

    // --- MarshalEncounter: happy path + fallbacks ---
    const EncounterDef& ambush = *lib.Find("longmen_ambush");
    {
        const PlayerDeploy pd{squads.Find("vanguard"), 0,
                              {"card_hold", "card_brace",
                               "card_flee"},
                              "先鋒營"};
        auto a = MarshalEncounter(ambush, world, bmap, squads,
                                  cards, {&pd, 1}, nullptr, 99);
        Check(a.ok(), "marshal succeeds");
        if (a.ok()) {
            Check(a.value.mapId == "field" && a.value.seed == 4242,
                  "enc map ref + authored seed win");
            Check(a.value.rows.size() == 2 &&
                      a.value.rows[0].side == 0 &&
                      a.value.rows[1].side == 1 &&
                      a.value.rows[0].rosterName == "先鋒營",
                  "rows: player order then defender order");
            Check(a.value.fog.probeGain == 25,
                  "no commander -> default fog");
        }
        // Node-map fallback + caller seed + commander bias.
        Character cmdr;
        cmdr.prior = Potato::Campaign::RivalPrior::Aggressive;
        const EncounterDef& fb = *lib.Find("node_map_fight");
        auto a2 = MarshalEncounter(fb, world, bmap, squads, cards,
                                   {&pd, 1}, &cmdr, 77);
        Check(a2.ok() && a2.value.mapId == "field" &&
                  a2.value.seed == 77,
              "node.map fallback + caller seed");
        Check(a2.ok() && a2.value.fog.probeGain == 35 &&
                  a2.value.fog.initialIntel == 50,
              "aggressive prior biases fog (+probe, -intel)");
        // Deploy into a live controller.
        if (a.ok()) {
            BattleController bc(4242, bmap, cards, a.value.fog);
            Check(Potato::Campaign::DeployAssembly(bc, a.value),
                  "deploy lands in Planning");
            Check(bc.Squads().size() == 2 &&
                      bc.Squads()[0].side == 0 &&
                      bc.Squads()[1].side == 1,
                  "both sides fielded");
            Check(bc.RequestBeat(BattleBeat::Execution),
                  "deployed battle can advance");
            Check(!Potato::Campaign::DeployAssembly(bc, a.value),
                  "deploy rejected outside Planning");
        }
    }

    // --- Marshal rejections ---
    {
        const PlayerDeploy pd{squads.Find("vanguard"), 0,
                              {"card_hold", "card_brace",
                               "card_flee"},
                              "x"};
        Check(!MarshalEncounter(*lib.Find("ghost_field"), world,
                                bmap, squads, cards, {&pd, 1},
                                nullptr, 0)
                   .ok(),
              "unknown node rejects");
        Check(!MarshalEncounter(*lib.Find("luoyang_banners"),
                                world, bmap, squads, cards,
                                {&pd, 1}, nullptr, 0)
                   .ok(),
              "node without map + no enc.map rejects");
        Check(!MarshalEncounter(ambush, world, bmap, squads, cards,
                                {}, nullptr, 0)
                   .ok(),
              "empty player deploy rejects");
        const PlayerDeploy badTmpl{nullptr, 0,
                                   {"card_hold", "card_brace",
                                    "card_flee"},
                                   "x"};
        Check(!MarshalEncounter(ambush, world, bmap, squads, cards,
                                {&badTmpl, 1}, nullptr, 0)
                   .ok(),
              "missing player template rejects");
        const PlayerDeploy badRegion{squads.Find("vanguard"), 9,
                                     {"card_hold", "card_brace",
                                      "card_flee"},
                                     "x"};
        Check(!MarshalEncounter(ambush, world, bmap, squads, cards,
                                {&badRegion, 1}, nullptr, 0)
                   .ok(),
              "player region OOB rejects");
        const PlayerDeploy badDeck{squads.Find("vanguard"), 0,
                                   {"card_hold", "card_brace",
                                    "card_nope"},
                                   "x"};
        Check(!MarshalEncounter(ambush, world, bmap, squads, cards,
                                {&badDeck, 1}, nullptr, 0)
                   .ok(),
              "unbuildable deck rejects");
    }

    // --- SettleEncounter: win flips the stake ---
    {
        CampaignState state;
        auto ws = WorldState::Init(world, 5).value;
        const std::uint64_t root = 0x0123abcd;
        const std::vector<AftermathRow> none;
        auto s = SettleEncounter(state, ws, world, ambush, true, 0,
                                 {}, none, root);
        Check(s.ok(), "win settles");
        if (s.ok()) {
            Check(s.value.resolution ==
                      Potato::Campaign::ChapterResolution::
                          BattleVictory,
                  "field victory seals battle_victory");
            Check(s.value.controlFlipped &&
                      ws.ControlAt("longmen") ==
                          WorldControl::Player,
                  "stake flips node control");
            Check(ws.IsResolved("longmen_ambush"),
                  "encounter marked resolved");
        }
        const auto& entries = state.GetLedger().Entries();
        Check(!entries.empty(), "seal posts to ledger");
        if (!entries.empty()) {
            const auto& tags = entries.back().tags;
            auto has = [&](const std::string& t) {
                for (const auto& x : tags) {
                    if (x == t) return true;
                }
                return false;
            };
            Check(has("resolution:battle_victory") &&
                      has("encounter:longmen_ambush") &&
                      has("region:longmen") &&
                      has(RecordRootTag(root)),
                  "seal cites resolution/encounter/region/root");
            bool chapterTag = false;
            for (const auto& x : tags) {
                if (x.rfind("chapter:", 0) == 0) chapterTag = true;
            }
            Check(!chapterTag, "no chapter tag on encounter seal");
        }
        // Same record can't settle twice.
        auto again = SettleEncounter(state, ws, world, ambush,
                                     true, 0, {}, none, root);
        Check(!again.ok(), "settled record root rejects");
    }

    // --- SettleEncounter: defeat converts, no dead end ---
    {
        CampaignState state;
        auto ws = WorldState::Init(world, 5).value;
        Check(ws.SetWarband("luoyang", world), "warband stands");
        const EncounterDef& banners = *lib.Find("luoyang_banners");
        const std::vector<AftermathRow> none;
        auto s = SettleEncounter(state, ws, world, banners, false,
                                 0, {}, none, 0xbeef0001);
        Check(s.ok(), "defeat settles");
        if (s.ok()) {
            Check(s.value.resolution ==
                      Potato::Campaign::ChapterResolution::Defeat,
                  "lost field seals defeat");
            Check(!s.value.controlFlipped &&
                      ws.ControlAt("luoyang") ==
                          WorldControl::Rival,
                  "no stake -> control untouched");
            Check(ws.IsResolved("luoyang_banners"),
                  "defeat still marks resolved (no refire)");
            Check(ws.WarbandAt() == "luoyang",
                  "warband stays put — continuation not reload");
            Check(PendingEncounters(ws, world, lib).empty(),
                  "defeated encounter leaves pending set");
        }
    }

    // --- SettleEncounter preflight rejects ---
    {
        CampaignState state;
        auto ws = WorldState::Init(world, 5).value;
        const std::vector<AftermathRow> none;
        Check(!SettleEncounter(state, ws, world, ambush, true, 0,
                               {}, none, 0)
                   .ok(),
              "record root required");
        Check(!SettleEncounter(state, ws, world, ambush, true, 7,
                               {}, none, 0x1234)
                   .ok(),
              "bad playerSide rejects");
        Check(state.GetLedger().Size() == 0 &&
                  !ws.IsResolved("longmen_ambush"),
              "preflight failures mutate nothing");
    }

    // --- EmitGatedSettlement: Clean record only ---
    {
        const fs::path outDir =
            fs::temp_directory_path() / "potato_test_enc_outbox";
        fs::remove_all(outDir);
        std::uint64_t root = 0;
        const JsonValue rec = MakeRecord(root);
        Ledger l;
        Posting seal;
        seal.credit = {Potato::Campaign::Account::Mandate, 1};
        seal.debit = {Potato::Campaign::Account::ArmyPrestige, 1};
        seal.memo = "encounter sealed";
        seal.tags = {RecordRootTag(root),
                     "resolution:battle_victory",
                     "encounter:longmen_ambush"};
        Check(l.Post(seal).ok(), "anchor seal posts");
        MintOutbox box(outDir);
        auto g = EmitGatedSettlement(l, rec, "battle_victory",
                                     "longmen_ambush", "longmen",
                                     box);
        Check(g.ok() && g.value.verdict == CrossVerdict::Clean &&
                  g.value.emitted.size() == 1,
              "clean record emits one settlement claim");
        const auto scan = box.Scan();
        Check(scan.ok() && scan.value.claims.size() == 1 &&
                  scan.value.claims[0].kind ==
                      ClaimKind::Settlement &&
                  scan.value.claims[0].encounter ==
                      "longmen_ambush" &&
                  scan.value.claims[0].node == "longmen" &&
                  scan.value.claims[0].recordRoot == root,
              "claim round-trips encounter/node/root");
        // Uncovered record refuses — nothing reaches the outbox.
        fs::remove_all(outDir);
        Ledger l2;
        std::uint64_t root2 = 0;
        const JsonValue rec2 = MakeRecord(root2);
        auto g2 = EmitGatedSettlement(l2, rec2, "defeat",
                                      "x", "y", box);
        Check(g2.ok() && g2.value.verdict != CrossVerdict::Clean &&
                  g2.value.emitted.empty(),
              "uncovered record refuses the outbox");
    }

    fs::remove_all(dir);
    if (failures == 0) {
        std::printf("ALL PASS\n");
    } else {
        std::printf("%d FAILURES\n", failures);
    }
    return failures == 0 ? 0 : 1;
}
