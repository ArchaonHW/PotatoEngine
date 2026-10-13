// Epic 7 — 不戰而屈人之兵 (subversion): the IssueSubvert gates,
// defection apply (banner flip + fog remap), queue-order vs Execute,
// the Subversion close reason, wire v7 bounds, record/replay
// round-trip, deed posting (來降者厚賞), chapter/encounter seals,
// and the 嚇阻 deterrence eval.
#include "Campaign/Aftermath/Aftermath.h"
#include "Campaign/Chain/ClaimGate.h"
#include "Campaign/Chapters/ChapterLibrary.h"
#include "Campaign/Chapters/Progression.h"
#include "Campaign/Governance/Accumulators.h"
#include "Campaign/Governance/Deterrence.h"
#include "Campaign/Governance/Victory.h"
#include "Campaign/Ledger/CrossCheck.h"
#include "Campaign/Ledger/DeedBook.h"
#include "Campaign/Ledger/Ledger.h"
#include "Campaign/State/CampaignState.h"
#include "Campaign/World/Encounter.h"
#include "Campaign/World/EncounterSettle.h"
#include "Campaign/World/WorldMap.h"
#include "Campaign/World/WorldState.h"
#include "Gameplay/Command/Intervention.h"
#include "Gameplay/Doctrine/Doctrine.h"
#include "Gameplay/Eval/WinEval.h"
#include "Gameplay/Json/JsonValue.h"
#include "Gameplay/Map/BattleMap.h"
#include "Gameplay/Record/BattleRecorder.h"
#include "Gameplay/Record/ReplayVerifier.h"
#include "Gameplay/Sim/BattleController.h"
#include "Gameplay/Squad/Squad.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

int failures = 0;
void Check(bool cond, const char* name) {
    std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", name);
    if (!cond) ++failures;
}

using namespace Potato::Gameplay;
using Potato::Campaign::Account;
using Potato::Campaign::AftermathRow;
using Potato::Campaign::BookDeeds;
using Potato::Campaign::CampaignState;
using Potato::Campaign::ChapterLibrary;
using Potato::Campaign::ChapterResolution;
using Potato::Campaign::ConcludeChapter;
using Potato::Campaign::DeterrenceMet;
using Potato::Campaign::EncounterDef;
using Potato::Campaign::FoldGovernance;
using Potato::Campaign::InitializeProgress;
using Potato::Campaign::Ledger;
using Potato::Campaign::Posting;
using Potato::Campaign::RecordRootTag;
using Potato::Campaign::ResolutionName;
using Potato::Campaign::ResolveAftermath;
using Potato::Campaign::SettleEncounter;
using Potato::Campaign::WorldMap;
using Potato::Campaign::WorldState;

// Line map: r0 - r1 - r2 - r3.
const char* MAP_DOC = R"json({
    "schema": "potato.map/1", "id": "m", "name": "Line",
    "regions": [{"id": "r0"}, {"id": "r1"}, {"id": "r2"}, {"id": "r3"}],
    "edges": [["r0", "r1"], ["r1", "r2"], ["r2", "r3"]]
})json";

const char* CARDS_DOC = R"json({
    "schema": "potato.doctrine_cards/1",
    "cards": [
        {"id": "card_idle", "cooldown": 5,
         "trigger": {"type": "always"}, "action": {"type": "hold"}}
    ]
})json";

const char* WORLD_DOC = R"json({
    "schema": "potato.world/1",
    "id": "w",
    "nodes": [
        {"id": "kaifeng", "control": "player"},
        {"id": "longmen", "control": "rival"}
    ],
    "routes": [{"a": "kaifeng", "b": "longmen", "days": 2}],
    "start": "kaifeng"
})json";

struct Fx {
    BattleMap map;
    DoctrineLibrary cards;
    Fx()
        : map(BattleMap::FromJson(JsonValue::Parse(MAP_DOC).value).value),
          cards(DoctrineLibrary::FromJson(JsonValue::Parse(CARDS_DOC).value)
                    .value) {}
};

SquadTemplate Mk(const char* id, int cohesion = 100) {
    SquadTemplate t;
    t.id = id;
    t.name = id;
    t.hp = 200;
    t.attack = 15;
    t.speed = Speed::VeryFast;
    t.cohesion = cohesion;
    return t;
}

int CountEvents(const BattleController& bc, SimEvent::Kind k) {
    int n = 0;
    for (const SimEvent& e : bc.Events()) {
        if (e.kind == k) ++n;
    }
    return n;
}

bool HasTag(const Ledger& l, const std::string& tag) {
    for (const auto& t : l.Entries().back().tags) {
        if (t == tag) return true;
    }
    return false;
}

// 7.5 — the scripted subversion battle the record/replay checks run.
std::string RunAndRecordSubvert(BattleRecorder& rec) {
    Fx fx;
    auto md = JsonValue::Parse(MAP_DOC).value;
    auto cd = JsonValue::Parse(CARDS_DOC).value;
    BattleController bc(21, fx.map, fx.cards);
    rec.Bind(21, md, cd);
    SquadTemplate a = Mk("ally"), f = Mk("wavering", 30),
                  p = Mk("parked");
    bc.DeploySquad(a, 1, 0); rec.RecordDeploy(a, 1, 0);
    bc.DeploySquad(f, 1, 1); rec.RecordDeploy(f, 1, 1);
    bc.DeploySquad(p, 3, 1); rec.RecordDeploy(p, 3, 1); // keeps side 1
                                                       // alive post-flip
    bc.RequestBeat(BattleBeat::Execution);
    bc.IssueSubvert(0, 1);               // stamped 0, applies tick 0
    bc.Tick();                           // defection lands
    for (int i = 0; i < 4; ++i) bc.Tick();
    bc.RequestBeat(BattleBeat::Aftermath); // concede out (foe parked)
    rec.Seal(bc);
    auto doc = rec.ToJson();
    return doc.ok() ? doc.value.Emit() : std::string{};
}

} // namespace

int main() {
    Fx fx;

    // ===== Story 7.2 — IssueSubvert gates =====
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("ally"), 1, 0);
        bc.DeploySquad(Mk("foe", 30), 1, 1);
        Check(!bc.IssueSubvert(0, 1).ok(),
              "7.2: outside Execution rejects");
        Check(bc.RequestBeat(BattleBeat::Execution), "7.2: to exec");
        Check(!bc.IssueSubvert(0, 0).ok(),
              "7.2: own squad rejects");
        Check(!bc.IssueSubvert(2, 1).ok(), "7.2: bad side rejects");
        Check(!bc.IssueSubvert(0, 9).ok(), "7.2: bad index rejects");
        // Mercy is free — the bounty is a ledger price, not a CP cost.
        Check(CostOf(InterventionKind::Subvert) == 0,
              "7.2: Subvert costs 0 CP");
        auto r = bc.IssueSubvert(0, 1);
        Check(r.ok() && r.value, "7.2: wavering enemy + envoy issues");
        Check(!bc.IssueSubvert(0, 1).ok(),
              "7.2: already-marked target rejects");
    }
    {
        // The wedge only takes cracks: SUBVERT_COHESION is the line.
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("ally"), 1, 0);
        bc.DeploySquad(Mk("steady", SUBVERT_COHESION), 1, 1);
        bc.DeploySquad(Mk("cracked", SUBVERT_COHESION - 1), 1, 1);
        bc.RequestBeat(BattleBeat::Execution);
        Check(!bc.IssueSubvert(0, 1).ok(),
              "7.2: cohesion at the line rejects");
        Check(bc.IssueSubvert(0, 2).ok(),
              "7.2: cohesion below the line issues");
    }
    {
        // No envoy, no parley — and off-field squads can't turn.
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("ally"), 0, 0);
        bc.DeploySquad(Mk("lonely", 30), 3, 1);
        bc.RequestBeat(BattleBeat::Execution);
        Check(!bc.IssueSubvert(0, 1).ok(),
              "7.2: no squad in reach rejects");
        // A routed (already off-field) squad can't be turned either.
        bc.IssueRetreat(1, 1);
        for (int i = 0; i < ROUT_TICKS + 2 && bc.Tick(); ++i) {}
        Check(bc.Squads()[1].state == SquadState::Routed,
              "7.2: retreat plea walked off");
        Check(!bc.IssueSubvert(0, 1).ok(),
              "7.2: off-field target rejects");
    }

    // ===== Story 7.1 — apply: banner flip + SquadDefected + fog =====
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("ally"), 1, 0);
        bc.DeploySquad(Mk("wavering", 30), 1, 1);
        bc.DeploySquad(Mk("parked"), 3, 1); // side 1 stays alive
        bc.RequestBeat(BattleBeat::Execution);
        Check(bc.Fog(0).Clouds().size() == 2 &&
                  bc.Fog(1).Clouds().size() == 1,
              "7.1: pre-flip cloud counts (foes observed / ally seen)");
        Check(bc.IssueSubvert(0, 1).ok(), "7.1: subvert issued");
        bc.Tick();
        const Squad& turned = bc.Squads()[1];
        Check(turned.side == 0 && turned.state == SquadState::Holding,
              "7.1: defector wears our banner, stands down");
        Check(CountEvents(bc, SimEvent::Kind::SquadDefected) == 1,
              "7.1: SquadDefected emitted once");
        for (const SimEvent& e : bc.Events()) {
            if (e.kind == SimEvent::Kind::SquadDefected) {
                Check(e.squadIndex == 1 && e.param == 1 &&
                          e.side == 0,
                      "7.1: deed payload (squad, region, turned-to)");
            }
        }
        // Fog remap: our fog forgets it (own truth now); the betrayed
        // side gains a cloud collapsed at the witnessed spot.
        Check(bc.Fog(0).Clouds().size() == 1,
              "7.1: persuader's fog drops the turned cloud");
        const auto& theirClouds = bc.Fog(1).Clouds();
        Check(theirClouds.size() == 2,
              "7.1: betrayed side gains a cloud");
        bool witnessed = false;
        for (const CloudEntity& c : theirClouds) {
            witnessed |= c.certainty == 100 && c.believedRegion == 1;
        }
        Check(witnessed, "7.1: betrayal witnessed at certainty 100");
    }

    // ===== Queue order — mercy vs the rope, authored order decides =====
    {
        // Subvert-first turns the squad Execute was aimed at.
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("ally"), 1, 0);
        bc.DeploySquad(Mk("pleading"), 1, 1);
        bc.DeploySquad(Mk("parked"), 3, 1);
        bc.RequestBeat(BattleBeat::Execution);
        bc.IssueRetreat(1, 1); // foe files the plea
        bc.Tick();             // Routing
        Check(bc.Squads()[1].state == SquadState::Routing,
              "7.2: plea filed");
        Check(bc.IssueSubvert(0, 1).ok() && bc.IssueExecute(0, 1).ok(),
              "7.2: both marks queue on a Routing victim");
        bc.Tick();
        Check(bc.Squads()[1].side == 0 &&
                  bc.Squads()[1].state == SquadState::Holding &&
                  CountEvents(bc, SimEvent::Kind::SquadDefected) == 1 &&
                  CountEvents(bc, SimEvent::Kind::SquadExecuted) == 0,
              "7.2: Subvert-first turns; the noose finds its own man");
    }
    {
        // Execute-first kills the squad Subvert was aimed at.
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("ally"), 1, 0);
        bc.DeploySquad(Mk("pleading"), 1, 1);
        bc.DeploySquad(Mk("parked"), 3, 1);
        bc.RequestBeat(BattleBeat::Execution);
        bc.IssueRetreat(1, 1);
        bc.Tick();
        Check(bc.IssueExecute(0, 1).ok() && bc.IssueSubvert(0, 1).ok(),
              "7.2: both marks queue (reversed order)");
        bc.Tick();
        Check(bc.Squads()[1].state == SquadState::Destroyed &&
                  CountEvents(bc, SimEvent::Kind::SquadExecuted) == 1 &&
                  CountEvents(bc, SimEvent::Kind::SquadDefected) == 0,
              "7.2: Execute-first kills; the envoy buries a corpse");
    }

    // ===== Story 7.4 — the Subversion close =====
    {
        // The ENTIRE enemy force turns -> CloseReason::Subversion.
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("ally"), 1, 0);
        bc.DeploySquad(Mk("wavering", 30), 1, 1);
        bc.RequestBeat(BattleBeat::Execution);
        bc.IssueSubvert(0, 1);
        Check(bc.Tick(), "7.4: defection tick runs");
        Check(bc.Beat() == BattleBeat::Aftermath,
              "7.4: empty enemy side auto-closes");
        const BattleResult& o = bc.Outcome();
        Check(o.closeReason == CloseReason::Subversion &&
                  o.subverted && o.winnerSide == 0 && !o.forced,
              "7.4: Subversion verdict + subverted flag + winner");
        for (const SimEvent& e : bc.Events()) {
            if (e.kind == SimEvent::Kind::ResultDeclared) {
                Check(e.aux == static_cast<int>(CloseReason::Subversion),
                      "7.4: ResultDeclared carries aux=3 (wire)");
            }
        }
        Check(!bc.Tick(), "7.4: Aftermath is terminal");
    }
    {
        // A turned squad + a routed straggler still bearing the
        // banner -> the wipe reads Wipe, not Subversion.
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("ally"), 1, 0);
        bc.DeploySquad(Mk("wavering", 30), 1, 1);
        bc.DeploySquad(Mk("straggler"), 3, 1);
        bc.RequestBeat(BattleBeat::Execution);
        bc.IssueSubvert(0, 1);
        bc.Tick();
        bc.IssueRetreat(1, 2); // the straggler files out
        bc.Tick();
        Check(bc.Beat() == BattleBeat::Execution,
              "7.4: mixed field does not close yet");
        for (int i = 0; i < ROUT_TICKS + 2 && bc.Tick(); ++i) {}
        const BattleResult& o = bc.Outcome();
        Check(bc.Beat() == BattleBeat::Aftermath &&
                  o.closeReason == CloseReason::Wipe && !o.subverted,
              "7.4: a surviving banner reads Wipe, not Subversion");
    }

    // ===== Story 7.5 — wire v7 bounds + record/replay =====
    {
        SimEvent e;
        auto okKind = [](std::int64_t k, SimEvent& out) {
            const std::string j =
                "{\"aux\":0,\"card\":\"\",\"kind\":" +
                std::to_string(k) +
                ",\"param\":1,\"path\":[],\"side\":0,\"slot\":-1,"
                "\"squad\":1,\"tick\":0}";
            auto p = JsonValue::Parse(j);
            return p.ok() && EventFromJson(p.value, out);
        };
        Check(okKind(13, e) && e.kind == SimEvent::Kind::SquadDefected,
              "7.5: kind 13 (SquadDefected) parses");
        Check(!okKind(14, e), "7.5: kind 14 rejects");
        auto okAux = [](std::int64_t a, SimEvent& out) {
            const std::string j =
                "{\"aux\":" + std::to_string(a) +
                ",\"card\":\"\",\"kind\":2,\"param\":1,\"path\":[],"
                "\"side\":0,\"slot\":-1,\"squad\":1,\"tick\":0}";
            auto p = JsonValue::Parse(j);
            return p.ok() && EventFromJson(p.value, out);
        };
        Check(okAux(10, e), "7.5: aux 10 (Subvert) parses");
        Check(!okAux(11, e), "7.5: aux 11 rejects");
        Check(BattleRecorder::TOOL_VERSION == 7,
              "7.5: tool version bumped to 7");
    }
    {
        BattleRecorder rec;
        const std::string text = RunAndRecordSubvert(rec);
        auto doc = JsonValue::Parse(text);
        Check(doc.ok(), "7.5: subvert record emitted");
        if (doc.ok()) {
            Check(doc.value["toolVersion"].AsInt() == 7,
                  "7.5: record stamped v7");
            bool sawDeed = false, sawCmd = false;
            for (const JsonValue& ev : doc.value["events"].Items()) {
                if (ev["kind"].AsInt() == 13) sawDeed = true;
                if (ev["kind"].AsInt() == 2 && ev["aux"].AsInt() == 10)
                    sawCmd = true;
            }
            Check(sawDeed && sawCmd,
                  "7.5: record carries the Subvert journal + deed");
            auto r = Replay::Verify(doc.value);
            Check(r.ok() && r.value.ok,
                  "7.5: subvert record replays bit-exact");
        }
    }

    // ===== Story 7.3 — deed posting: 來降者厚賞 =====
    {
        Ledger l;
        SimEvent e{SimEvent::Kind::SquadDefected, 3, 2, -1, 1, 0, 0,
                   {}, {}};
        SimEvent hostile{SimEvent::Kind::SquadDefected, 3, 2, -1, 1, 0,
                         1, {}, {}};
        const std::vector<SimEvent> evs{e, hostile};
        auto posted = BookDeeds(l, 0, evs);
        Check(posted.ok() && posted.value == 1,
              "7.3: our side's defection books once (enemy's skipped)");
        Check(l.Balance(Account::ArmyPrestige) == 10 &&
                  l.Balance(Account::Materiel) == -15,
              "7.3: bounty books 物資->軍威");
        Check(HasTag(l, "turned:2") && HasTag(l, "field:1") &&
                  HasTag(l, "order:+3"),
              "7.3: deed carries turned/field/order tags");
    }

    // ===== Story 7.4 — chapter seal: resolution:subversion =====
    {
        namespace fs = std::filesystem;
        const fs::path dir =
            fs::temp_directory_path() / "potato_test_subvert_ch";
        fs::remove_all(dir);
        fs::create_directories(dir);
        const auto wch = [&dir](const char* name, const char* id,
                                int idx) {
            std::ofstream f(dir / name,
                            std::ios::binary | std::ios::trunc);
            f << "{\"schema\":\"potato.chapter/1\",\"id\":\"" << id
              << "\",\"index\":" << idx
              << ",\"title\":\"t\",\"map\":\"m.json\","
                 "\"combat\":true}";
        };
        wch("a.json", "s0", 0);
        wch("b.json", "s1", 1);
        ChapterLibrary lib;
        Check(ChapterLibrary::Load(dir, lib).ok, "7.4: library loads");
        const auto ready = [&lib]() {
            CampaignState st;
            InitializeProgress(st, lib);
            return st;
        };
        const auto fund = [](CampaignState& st, std::int64_t ps,
                             std::int64_t ord) {
            Posting p;
            p.credit = {Account::PopularSupport, ps};
            p.debit = {Account::Materiel, ps};
            p.memo = "rule";
            if (ord != 0) p.tags = {"order:" + std::to_string(ord)};
            return st.GetLedger().Post(p).ok();
        };

        Check(ResolutionName(ChapterResolution::Subversion) ==
                      std::string("subversion"),
              "7.4: canonical spelling matches the MintClaim seam");
        {
            CampaignState st = ready();
            const auto r =
                ConcludeChapter(st, lib, true, 0xabc1, true);
            Check(r.ok() && r.value == ChapterResolution::Subversion,
                  "7.4: subverted field win resolves Subversion");
            const auto& e = st.GetLedger().Entries().back();
            bool res = false, root = false;
            for (const auto& t : e.tags) {
                res |= t == "resolution:subversion";
                root |= t == RecordRootTag(0xabc1);
            }
            Check(res && root,
                  "7.4: seal carries resolution + record-root tags");
            Check(e.credit.account == Account::Mandate &&
                      e.debit.account == Account::ArmyPrestige,
                  "7.4: subversion seal books 軍威->天命");
            Check(st.GetChapter().current == 1,
                  "7.4: chapter advanced past the seal");
        }
        {
            // You cannot negotiate a loss: subverted + lost is Defeat.
            CampaignState st = ready();
            const auto r = ConcludeChapter(st, lib, false, 0, true);
            Check(r.ok() && r.value == ChapterResolution::Defeat,
                  "7.4: subverted defeat stays Defeat");
        }
        {
            // The people's verdict outranks even a bloodless one.
            CampaignState st = ready();
            fund(st, 75, 70);
            const auto r = ConcludeChapter(st, lib, true, 0, true);
            Check(r.ok() &&
                      r.value == ChapterResolution::GovernanceVictory,
                  "7.4: thresholds override subversion");
        }
        {
            // ResolveAftermath passes the flag through to the seal.
            CampaignState st = ready();
            const std::vector<AftermathRow> none;
            const auto s =
                ResolveAftermath(st, lib, true, 0, {}, none,
                                 0x5eed5eed, nullptr, true);
            Check(s.ok() && s.value.resolution ==
                             ChapterResolution::Subversion,
                  "7.4: ResolveAftermath seals subversion");
            Check(HasTag(st.GetLedger(), "resolution:subversion"),
                  "7.4: aftermath seal tagged resolution:subversion");
        }
        fs::remove_all(dir);
    }

    // ===== Story 7.4 — encounter seal shares the leg table =====
    {
        const WorldMap world =
            WorldMap::FromJson(JsonValue::Parse(WORLD_DOC).value).value;
        CampaignState state;
        auto ws = WorldState::Init(world, 5).value;
        EncounterDef enc;
        enc.id = "parley_field";
        enc.node = "longmen";
        enc.hasControlStake = true;
        enc.controlStake = Potato::Campaign::WorldControl::Player;
        const std::vector<AftermathRow> none;
        const std::uint64_t root = 0xd1f3c7;
        const auto s = SettleEncounter(state, ws, world, enc, true, 0,
                                       {}, none, root, nullptr, true);
        Check(s.ok() && s.value.resolution ==
                         ChapterResolution::Subversion,
              "7.4: subverted encounter resolves Subversion");
        Check(HasTag(state.GetLedger(), "resolution:subversion") &&
                  HasTag(state.GetLedger(), "encounter:parley_field") &&
                  HasTag(state.GetLedger(), RecordRootTag(root)),
              "7.4: encounter seal cites verdict/encounter/root");
        Check(ws.IsResolved("parley_field"),
              "7.4: negotiated field still marks resolved");
    }

    // ===== Story 7.6 — zero-combat chapter driver =====
    {
        namespace fs = std::filesystem;
        const fs::path dir =
            fs::temp_directory_path() / "potato_test_zerocombat_ch";
        fs::remove_all(dir);
        fs::create_directories(dir);
        const auto wch = [&dir](const char* name, const char* id,
                                int idx, bool combat) {
            std::ofstream f(dir / name,
                            std::ios::binary | std::ios::trunc);
            f << "{\"schema\":\"potato.chapter/1\",\"id\":\"" << id
              << "\",\"index\":" << idx
              << ",\"title\":\"t\",\"map\":\"m.json\",\"combat\":"
              << (combat ? "true" : "false") << "}";
        };
        wch("a.json", "z0", 0, false); // the parley chapter
        wch("b.json", "z1", 1, true);
        ChapterLibrary lib;
        Check(ChapterLibrary::Load(dir, lib).ok, "7.6: library loads");
        const auto ready = [&lib]() {
            CampaignState st;
            InitializeProgress(st, lib);
            return st;
        };
        const auto stand = [](CampaignState& st, std::int64_t amt) {
            Posting p;
            p.credit = {Account::ArmyPrestige, amt};
            p.debit = {Account::Materiel, amt};
            p.memo = "standing";
            return st.GetLedger().Post(p).ok();
        };
        {
            // Thin standing: the parley hasn't landed — fail-closed,
            // no seal, chapter still in play (NOT a defeat).
            CampaignState st = ready();
            stand(st, 49);
            const auto n = st.GetLedger().Size();
            const auto r = ResolveZeroCombatChapter(st, lib);
            Check(!r.ok() && r.error == "deterrence" &&
                      st.GetLedger().Size() == n &&
                      st.GetChapter().current == 0,
                  "7.6: thin standing stays in play, unsealed");
        }
        {
            // 軍威 enough: the enemy yields the field — the seal
            // reads resolution:subversion, chapter advances.
            CampaignState st = ready();
            stand(st, 60);
            const auto r = ResolveZeroCombatChapter(st, lib);
            Check(r.ok() && r.value == ChapterResolution::Subversion,
                  "7.6: deterrence resolves zero-combat as Subversion");
            Check(HasTag(st.GetLedger(), "resolution:subversion") &&
                      HasTag(st.GetLedger(), "chapter:0"),
                  "7.6: parley seal carries verdict + chapter tags");
            Check(st.GetChapter().current == 1,
                  "7.6: campaign advanced past the parley");
            // ...and the next chapter is a field chapter — the
            // negotiated path refuses it now.
            stand(st, 60);
            const auto r2 = ResolveZeroCombatChapter(st, lib);
            Check(!r2.ok() && r2.error == "chapter",
                  "7.6: combat chapter refuses the parley path");
        }
        {
            // Governance thresholds still outrank the bloodless win.
            CampaignState st = ready();
            stand(st, 60);
            Posting p;
            p.credit = {Account::PopularSupport, 75};
            p.debit = {Account::Materiel, 75};
            p.memo = "rule";
            p.tags = {"order:65"};
            st.GetLedger().Post(p);
            const auto r = ResolveZeroCombatChapter(st, lib);
            Check(r.ok() &&
                      r.value == ChapterResolution::GovernanceVictory,
                  "7.6: thresholds override the negotiated win");
        }
        {
            CampaignState st; // never initialized
            const auto r = ResolveZeroCombatChapter(st, lib);
            Check(!r.ok() && r.error == "state",
                  "7.6: uninitialized state fails closed");
        }
        fs::remove_all(dir);
    }

    // ===== Story 7.6 — the zero_combat claim producer =====
    {
        const auto sub =
            Potato::Campaign::ResolutionAchievements(
                ChapterResolution::Subversion);
        Check(sub.size() == 1 &&
                  sub[0] == Potato::Campaign::ACH_ZERO_COMBAT,
              "7.6: Subversion mints zero_combat");
        Check(Potato::Campaign::ResolutionAchievements(
                  ChapterResolution::BattleVictory)
                      .empty() &&
              Potato::Campaign::ResolutionAchievements(
                  ChapterResolution::GovernanceVictory)
                      .empty() &&
              Potato::Campaign::ResolutionAchievements(
                  ChapterResolution::Defeat)
                      .empty(),
              "7.6: no other resolution mints zero_combat");
    }

    // ===== Story 7.6 — 嚇阻 deterrence eval =====
    {
        Ledger l;
        Check(!DeterrenceMet(l), "7.6: empty ledger deters nothing");
        Posting p;
        p.credit = {Account::ArmyPrestige, 49};
        p.debit = {Account::Materiel, 49};
        p.memo = "standing";
        Check(l.Post(p).ok() && !DeterrenceMet(l),
              "7.6: 49 prestige is short of the line");
        Posting q;
        q.credit = {Account::ArmyPrestige, 1};
        q.debit = {Account::Materiel, 1};
        q.memo = "one more";
        Check(l.Post(q).ok() && DeterrenceMet(l),
              "7.6: 50 prestige meets the deterrence line");
    }
    {
        // A forged reputation still deters — the fold reads balances,
        // suspicion stays data (ledger's own rule for forged entries).
        Ledger l;
        Posting p;
        p.credit = {Account::ArmyPrestige, 60};
        p.debit = {Account::Materiel, 60};
        p.memo = "inflated standing";
        Check(l.Forge(p).ok() && DeterrenceMet(l),
              "7.6: forged prestige folds into deterrence");
    }

    std::printf("%s\n", failures == 0 ? "ALL PASS" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
