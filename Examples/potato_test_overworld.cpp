// Story 12.9 — Overworld Presentation & Creation UI pins:
//   - WorldProjection: hinted normalize, grid fallback, route
//     edge dedupe, degenerate spans, bad-bounds rejection
//   - RenderOverworld: golden-hash determinism, control rings,
//     POI marks, warband marker, halftone march edge + ETA pips,
//     encounter banners (firing pip, resolved suppression),
//     hearsay sightings with fade floor — reads Sightings() and
//     never the world PRNG
//   - MarchOptions: adjacency-legality, days/supply,
//     affordability, empty-while-marching
//   - RosterList (created excluded) + BuildCharacter wire-gate
//     validation and deterministic commander ids
#include "Game/Render/OverworldView.h"
#include "Game/Render/SpriteAtlas.h"
#include "Game/Render/WorldProjection.h"
#include "Game/Ui/ActionNotice.h"
#include "Game/Ui/CreationForm.h"
#include "Game/Ui/EncounterLaunch.h"
#include "Game/Ui/OrderMenu.h"
#include "Game/Ui/OverworldHud.h"
#include "Campaign/Characters/Character.h"
#include "Campaign/Ledger/Ledger.h"
#include "Campaign/Narrative/Bencao.h"
#include "Campaign/Narrative/BencaoCodex.h"
#include "Campaign/Narrative/IntelLedger.h"
#include "Campaign/Roster/Roster.h"
#include "Campaign/State/CampaignState.h"
#include "Campaign/World/Encounter.h"
#include "Campaign/World/March.h"
#include "Campaign/World/WorldMap.h"
#include "Campaign/World/WorldState.h"
#include "Gameplay/Doctrine/Doctrine.h"
#include "Gameplay/Json/JsonValue.h"
#include "Gameplay/Map/BattleMap.h"
#include "Gameplay/Squad/Squad.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

using Potato::Campaign::Account;
using Potato::Campaign::Character;
using Potato::Campaign::CharacterLibrary;
using Potato::Campaign::EncounterLibrary;
using Potato::Campaign::IssueMarch;
using Potato::Campaign::IssueSighting;
using Potato::Campaign::Ledger;
using Potato::Campaign::Posting;
using Potato::Campaign::RivalPrior;
using Potato::Campaign::WorldMap;
using Potato::Campaign::WorldState;
using Potato::Game::BuildCharacter;
using Potato::Game::CreationForm;
using Potato::Game::Framebuffer;
using Potato::Game::MarchOptions;
using Potato::Game::OverworldInput;
using Potato::Game::RenderOverworld;
using Potato::Game::Rgba;
using Potato::Game::RosterList;
using Potato::Game::SpriteAtlas;
using Potato::Game::WorldProjection;
using Potato::Gameplay::JsonValue;

static int failures = 0;
static void Check(bool cond, const char* what) {
    if (!cond) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

// 4-node hinted world: a-b-c in a row, d below b.
static WorldMap TestWorld() {
    const std::string doc =
        "{\"schema\":\"potato.world/1\",\"id\":\"w\",\"name\":\"W\","
        "\"nodes\":["
        "{\"id\":\"a\",\"terrain\":[\"open\"],\"strategic\":[\"depot\"],"
        "\"control\":\"player\",\"center\":[0,0]},"
        "{\"id\":\"b\",\"terrain\":[\"water\"],\"strategic\":[\"ferry\"],"
        "\"control\":\"rival\",\"center\":[10,0]},"
        "{\"id\":\"c\",\"terrain\":[\"forest\"],\"myth\":[\"shrine\"],"
        "\"center\":[20,0]},"
        "{\"id\":\"d\",\"terrain\":[\"highland\"],\"center\":[10,10]}"
        "],\"routes\":["
        "{\"a\":\"a\",\"b\":\"b\",\"days\":2},"
        "{\"a\":\"b\",\"b\":\"c\",\"days\":3},"
        "{\"a\":\"a\",\"b\":\"d\",\"days\":1}"
        "],\"start\":\"a\"}";
    auto r = WorldMap::FromJson(JsonValue::Parse(doc).value);
    return std::move(r.value);
}

// One node unhinted → grid fallback for the whole map.
static WorldMap UnhintedWorld() {
    const std::string doc =
        "{\"schema\":\"potato.world/1\",\"id\":\"w\",\"name\":\"W\","
        "\"nodes\":["
        "{\"id\":\"a\",\"control\":\"player\"},"
        "{\"id\":\"b\",\"control\":\"rival\"},"
        "{\"id\":\"c\",\"center\":[5,5]}"
        "],\"routes\":[{\"a\":\"a\",\"b\":\"b\",\"days\":1}],"
        "\"start\":\"a\"}";
    auto r = WorldMap::FromJson(JsonValue::Parse(doc).value);
    return std::move(r.value);
}

// 4px tiles covering the ids OverworldView looks up.
static SpriteAtlas TestAtlas() {
    const std::string doc =
        "{\"schema\":\"potato.spriteatlas/1\",\"id\":\"t\",\"tile\":4,"
        "\"palette\":[\"#00000000\",\"#102030ff\",\"#40a0c0ff\","
        "\"#ff8040ff\",\"#20a040ff\",\"#c0c0c0ff\",\"#8040c0ff\"],"
        "\"tiles\":["
        "{\"id\":\"terrain.open\",\"rows\":[\"4444\",\"4444\",\"4444\",\"4444\"]},"
        "{\"id\":\"terrain.water\",\"rows\":[\"2222\",\"2222\",\"2222\",\"2222\"]},"
        "{\"id\":\"terrain.forest\",\"rows\":[\"3355\",\"5555\",\"5555\",\"3355\"]},"
        "{\"id\":\"terrain.highland\",\"rows\":[\"5555\",\"5555\",\"5555\",\"5555\"]},"
        "{\"id\":\"mark.depot\",\"rows\":[\"0330\",\"3333\",\"3333\",\"0330\"]},"
        "{\"id\":\"mark.ferry\",\"rows\":[\"3030\",\"3030\",\"3030\",\"3030\"]},"
        "{\"id\":\"mark.shrine\",\"rows\":[\"0600\",\"0660\",\"0600\",\"0600\"]},"
        "{\"id\":\"warband\",\"rows\":[\"0110\",\"1111\",\"1111\",\"0110\"]},"
        "{\"id\":\"banner\",\"rows\":[\"0033\",\"0333\",\"0030\",\"0030\"]},"
        "{\"id\":\"sighting\",\"rows\":[\"0600\",\"0660\",\"0660\",\"0600\"]}"
        "]}";
    auto r = SpriteAtlas::FromJson(JsonValue::Parse(doc).value);
    return std::move(r.value);
}

namespace fs = std::filesystem;

static fs::path TestDir() {
    const fs::path d =
        fs::temp_directory_path() / "potato_test_overworld";
    fs::create_directories(d);
    return d;
}

static void WriteFile(const fs::path& dir, const char* name,
                      const char* content) {
    std::ofstream out(dir / name, std::ios::binary);
    out << content;
}

int main() {
    // ---------- WorldProjection ----------
    {
        const WorldMap w = TestWorld();
        auto p = WorldProjection::Build(w, 640, 400, 24);
        Check(p.ok(), "projection: hinted build");
        Check(p.value.Hinted(), "projection: hinted flag");
        Check(p.value.Nodes().size() == 4, "projection: 4 nodes");
        // spanX=20,spanY=10 → a at left/top margin, c at right,
        // d at bottom.
        Check(p.value.At(0).x == 24 && p.value.At(0).y == 24,
              "projection: a at margin corner");
        Check(p.value.At(2).x == 616 && p.value.At(2).y == 24,
              "projection: c at right margin");
        Check(p.value.At(3).x == 320 && p.value.At(3).y == 376,
              "projection: d at bottom");
        // Routes a-b, b-c, a-d → 3 deduped edges.
        Check(p.value.Edges().size() == 3,
              "projection: edges deduped");
    }
    {
        const WorldMap w = UnhintedWorld();
        auto p = WorldProjection::Build(w, 300, 300, 10);
        Check(p.ok() && !p.value.Hinted(),
              "projection: unhinted → grid");
        // grid: cols=2, rows=2; node 0 at (80,80), node 2 wraps.
        Check(p.value.At(0).x == 80 && p.value.At(0).y == 80,
              "projection: grid cell center");
        Check(p.value.At(2).x == 80 && p.value.At(2).y == 220,
              "projection: grid wraps rows");
        auto bad = WorldProjection::Build(w, 40, 40, 20);
        Check(!bad.ok(), "projection: margin*2>=w rejects");
    }

    // ---------- RenderOverworld ----------
    const WorldMap w = TestWorld();
    const SpriteAtlas atlas = TestAtlas();
    OverworldInput in;
    in.w = 640; in.h = 400; in.margin = 24; in.nodePx = 20;

    auto proj = WorldProjection::Build(w, in.w, in.h, in.margin);
    const int half = in.nodePx / 2;

    WorldState ws = WorldState::Init(w, 42).value;
    Check(ws.WarbandAt() == "a", "world: warband at start");

    Framebuffer base = RenderOverworld(w, ws, atlas, in).value;
    Framebuffer again = RenderOverworld(w, ws, atlas, in).value;
    Check(base.Hash() == again.Hash(),
          "overworld: rerender byte-identical");

    // Node frames carry faction color: player blue, rival red,
    // neutral green — the corner pixel is the tint itself.
    {
        const auto& pa = proj.value.At(0);
        const Rgba px = base.PixelAt(pa.x - half, pa.y - half);
        Check(px.b > 200 && px.r < 100,
              "overworld: player control frame (blue)");
        const auto& pb = proj.value.At(1);
        const Rgba rx = base.PixelAt(pb.x - half, pb.y - half);
        Check(rx.r > 180 && rx.b < 100,
              "overworld: rival control frame (red)");
        const auto& pc = proj.value.At(2);
        const Rgba nx = base.PixelAt(pc.x - half, pc.y - half);
        Check(nx.g > 100 && nx.r < 100 && nx.b < 100,
              "overworld: neutral control frame (green)");
    }

    // Warband marker sits at node 'a' center — a tile pixel
    // distinct from the open-terrain fill underneath.
    {
        const auto& pa = proj.value.At(0);
        const Rgba px = base.PixelAt(pa.x - 1, pa.y - 1);
        Check(px.g < 80 && px.r < 60,
              "overworld: warband marker drawn");
    }

    // --- march marker: translucent edge + ETA pips at dest ---
    {
        WorldState m = WorldState::Init(w, 42).value;
        Ledger ledger;
        Posting seed;
        seed.credit = {Account::Materiel, 100};
        seed.debit = {Account::ArmyPrestige, 100};
        seed.memo = "war chest";
        Check(ledger.Post(seed).ok(), "overworld: seed funds");
        auto plan = IssueMarch(m, w, ledger, "b");
        Check(plan.ok() && m.Marching(), "overworld: march issued");
        Framebuffer fm =
            RenderOverworld(w, m, atlas, in).value;
        Check(fm.Hash() != base.Hash(),
              "overworld: marching changes frame");
        // Route a→b runs along y=24, x∈24..320.
        const auto& pa = proj.value.At(0);
        const auto& pb = proj.value.At(1);
        int lit = 0;
        for (int x = pa.x + 1; x < pb.x; ++x) {
            const Rgba px = fm.PixelAt(x, pa.y);
            if (px.r > 140) ++lit;
        }
        Check(lit > 200,
              "overworld: march line lit along the route");
        // ETA = day+2 → 2 pips stacked above the dest node.
        const Rgba pip =
            fm.PixelAt(pb.x - 1, pb.y - half - 6);
        Check(pip.r > 240 && pip.g > 230,
              "overworld: ETA pip drawn above dest");
    }

    // --- encounter banners ---
    {
        const fs::path dir = TestDir();
        WriteFile(dir, "enc.json",
                  "{\"schema\":\"potato.encounter/1\","
                  "\"id\":\"b_ambush\",\"node\":\"b\","
                  "\"trigger\":\"arrival\",\"defenders\":["
                  "{\"template\":\"m\",\"region\":0,"
                  "\"deck\":[\"c1\",\"c2\",\"c3\"]}]}");
        EncounterLibrary lib;
        Check(EncounterLibrary::Load(dir, lib).ok &&
                  lib.Size() == 1,
              "overworld: encounter library loads");
        fs::remove(dir / "enc.json");

        OverworldInput ein = in;
        ein.encounters = &lib;
        WorldState es = WorldState::Init(w, 42).value;

        // Banners mark encounters firing NOW (PendingEncounters)
        // — warband at 'a', arrival trigger on 'b' → unfired
        // encounters draw nothing.
        Framebuffer armed =
            RenderOverworld(w, es, atlas, ein).value;
        Check(armed.Hash() == base.Hash(),
              "overworld: unfired encounter draws no banner");

        // Warband on 'b' → arrival trigger fires → banner at the
        // node's bottom-right.
        es.SetWarband("b", w);
        Framebuffer firing =
            RenderOverworld(w, es, atlas, ein).value;
        const auto& pb = proj.value.At(1);
        const Rgba bp = firing.PixelAt(pb.x + half - 1,
                                       pb.y + half - 1);
        Check(bp.r > 150, "overworld: firing banner pixels");

        // Resolved → no longer pending → banner suppressed.
        es.MarkResolved("b_ambush");
        Framebuffer resolved =
            RenderOverworld(w, es, atlas, ein).value;
        const Rgba gone =
            resolved.PixelAt(pb.x + half - 1, pb.y + half - 1);
        Check(!(gone.r > 150 && gone.g < 100),
              "overworld: resolved banner suppressed");
    }

    // --- hearsay sightings: present, then fade toward floor ---
    {
        WorldState s = WorldState::Init(w, 42).value;
        Check(IssueSighting(s, w, "c").ok(),
              "overworld: sighting enqueued");
        s.ResolveBeats(w, 0); // same-day drain → sightings
        Check(s.Sightings().count("c") == 1,
              "overworld: sighting lands");
        Framebuffer f0 =
            RenderOverworld(w, s, atlas, in).value;
        Check(f0.Hash() != base.Hash(),
              "overworld: sighting changes frame");
        const auto& pc = proj.value.At(2);
        // Tile sits left-below the node; the alpha-fade bar runs
        // under the footprint — the bar is the staleness pin.
        const int tx = pc.x - half - 2 + 1;
        const int ty = pc.y + half - atlas.TilePx() + 1;
        const Rgba tilePx = f0.PixelAt(tx, ty);
        Check(tilePx.b > 150, "overworld: sighting tile lit");
        const int bx = pc.x - half + 1;
        const int by = pc.y + half - 3;
        const Rgba fresh = f0.PixelAt(bx, by);
        Check(fresh.b > 200, "overworld: fresh sighting bar lit");
        s.ResolveBeats(w, in.staleDays); // age = staleDays → floor
        Framebuffer fOld =
            RenderOverworld(w, s, atlas, in).value;
        const Rgba aged = fOld.PixelAt(bx, by);
        Check(aged.b < fresh.b - 20,
              "overworld: stale sighting dims");
        Check(aged.b > 40,
              "overworld: stale sighting never blacks out");
    }

    // ---------- MarchOptions ----------
    {
        WorldState s = WorldState::Init(w, 42).value;
        const auto opts = MarchOptions(w, s, 100);
        Check(opts.size() == 2,
              "ordermenu: two legal targets at 'a'");
        // a→b is 2d, a→d is 1d — route-declaration order.
        Check(opts[0].nodeId == "b" && opts[0].days == 2 &&
                  opts[0].supply == 10 && opts[0].affordable,
              "ordermenu: b option days+supply");
        Check(opts[1].nodeId == "d" && opts[1].days == 1,
              "ordermenu: d option");
        const auto poor = MarchOptions(w, s, 7);
        Check(!poor[0].affordable && poor[1].affordable,
              "ordermenu: affordability vs supply");
        Ledger ledger;
        Posting seed;
        seed.credit = {Account::Materiel, 100};
        seed.debit = {Account::ArmyPrestige, 100};
        seed.memo = "war chest";
        Check(ledger.Post(seed).ok(), "ordermenu: seed funds");
        Check(IssueMarch(s, w, ledger, "b").ok(),
              "ordermenu: march issues");
        Check(MarchOptions(w, s, 100).empty(),
              "ordermenu: empty while marching");
    }

    // ---------- RosterList / BuildCharacter ----------
    {
        const fs::path dir = TestDir();
        WriteFile(dir, "gen.json",
                  "{\"schema\":\"potato.character/1\",\"id\":\"lv_bu\","
                  "\"name\":\"\\u5442\\u5e03\",\"origin\":\"\\u4e5d\\u539f\","
                  "\"prior\":\"aggressive\",\"deck_seed\":42}");
        WriteFile(dir, "made.json",
                  "{\"schema\":\"potato.character/1\",\"id\":\"made\","
                  "\"name\":\"x\",\"origin\":\"y\","
                  "\"prior\":\"cunning\",\"deck_seed\":1,"
                  "\"created\":true}");
        CharacterLibrary lib;
        Check(CharacterLibrary::Load(dir, lib).ok &&
                  lib.Size() == 2,
              "creation: character library loads");
        fs::remove(dir / "gen.json");
        fs::remove(dir / "made.json");

        const auto roster = RosterList(lib);
        Check(roster.size() == 1 && roster[0].id == "lv_bu",
              "creation: roster excludes created=true");

        CreationForm form;
        form.name = "tester";
        form.origin = "nowhere";
        form.prior = RivalPrior::Cunning;
        form.deckSeed = 7;
        auto c = BuildCharacter(form);
        Check(c.ok(), "creation: valid form builds");
        Check(c.value.created && c.value.prior == RivalPrior::Cunning,
              "creation: created flag + prior");
        Check(c.value.id.rfind("pc_", 0) == 0,
              "creation: commander id is a pc_ slug");
        Check(BuildCharacter(form).value.id == c.value.id,
              "creation: same form → same id");
        CreationForm bad = form;
        bad.name = "";
        Check(!BuildCharacter(bad).ok(),
              "creation: empty name fails wire gate");
        bad = form;
        bad.name = std::string(200, 'x');
        Check(!BuildCharacter(bad).ok(),
              "creation: oversized name fails wire gate");
    }

    // ---------- 13.4: OverworldHud ----------
    {
        // Top bar reads day + anchor + the ledger folds — never
        // a stored stat.
        WorldState s = WorldState::Init(w, 42).value;
        Ledger ledger;
        Posting seed;
        seed.credit = {Account::Materiel, 100};
        seed.debit = {Account::ArmyPrestige, 100};
        seed.memo = "war chest";
        Check(ledger.Post(seed).ok(), "topbar: seed funds");
        Posting gov;
        gov.credit = {Account::PopularSupport, 20};
        gov.debit = {Account::Materiel, 15};
        gov.memo = "grain relief";
        gov.tags = {"order:+3"};
        Check(ledger.Post(gov).ok(), "topbar: governance post");
        auto bar = Potato::Game::BuildTopBar(w, s, ledger);
        Check(bar.day == 0 && bar.nodeId == "a",
              "topbar: day + anchor");
        Check(bar.nodeName == "a",
              "topbar: unlabeled node falls back to id");
        Check(bar.materiel == 85 && bar.popularSupport == 20 &&
                  bar.order == 3,
              "topbar: balances + governance folds");
        Check(!bar.marching, "topbar: not marching");
        Check(IssueMarch(s, w, ledger, "b").ok(),
              "topbar: march issued");
        bar = Potato::Game::BuildTopBar(w, s, ledger);
        Check(bar.marching && bar.marchDestId == "b" &&
                  bar.marchEta == 2,
              "topbar: in-flight dest + eta");

        // Tab titles — six sections, all named.
        for (std::size_t i = 0;
             i < Potato::Game::kOverworldTabCount; ++i) {
            const char* t = Potato::Game::OverworldTabTitle(
                static_cast<Potato::Game::OverworldTab>(i));
            Check(t != nullptr && t[0] != '\0' && t[0] != '?',
                  "drawer: every tab titled");
        }
        Check(std::string(Potato::Game::OverworldTabTitle(
                  Potato::Game::OverworldTab::March)) == "行軍令",
              "drawer: march tab is 行軍令");
    }

    // --- 遭遇 rows: only encounters firing NOW ---
    {
        const fs::path dir = TestDir();
        WriteFile(dir, "enc.json",
                  "{\"schema\":\"potato.encounter/1\","
                  "\"id\":\"b_ambush\",\"node\":\"b\","
                  "\"trigger\":\"arrival\",\"defenders\":["
                  "{\"template\":\"m\",\"region\":0,"
                  "\"deck\":[\"c1\",\"c2\",\"c3\"]}]}");
        EncounterLibrary lib;
        Check(EncounterLibrary::Load(dir, lib).ok,
              "drawer: encounter library loads");
        fs::remove(dir / "enc.json");
        WorldState s = WorldState::Init(w, 42).value;
        Check(Potato::Game::EncounterRows(s, w, lib).empty(),
              "drawer: no firing encounters at 'a'");
        s.SetWarband("b", w);
        auto rows = Potato::Game::EncounterRows(s, w, lib);
        Check(rows.size() == 1 && rows[0].id == "b_ambush" &&
                  rows[0].trigger ==
                      Potato::Campaign::EncounterTrigger::Arrival,
              "drawer: firing encounter row");
        Check(std::string(Potato::Game::EncTriggerLabel(
                  Potato::Campaign::EncounterTrigger::Poi)) ==
                  "要地",
              "drawer: poi trigger label");
        s.MarkResolved("b_ambush");
        Check(Potato::Game::EncounterRows(s, w, lib).empty(),
              "drawer: resolved encounter drops out");
    }

    // --- 情報 / 名冊 / 帳冊 rows ---
    {
        Potato::Campaign::IntelLedger intel;
        Check(intel.Record("rival_a",
                           Potato::Campaign::IntelKind::RivalTemperament,
                           "其性多疑", 0).ok(),
              "drawer: intel records");
        Check(intel.Resolve(1,
                            Potato::Campaign::IntelVerdict::False)
                  .ok(),
              "drawer: verdict lands");
        auto irows = Potato::Game::IntelRows(intel);
        Check(irows.size() == 1 && irows[0].seq == 1 &&
                  irows[0].verdict ==
                      Potato::Campaign::IntelVerdict::False,
              "drawer: intel row carries verdict");

        Potato::Campaign::CampaignState state;
        Check(Potato::Campaign::Enlist(state, "第一營").ok(),
              "drawer: enlist");
        auto rrows = Potato::Game::SquadRows(state);
        Check(rrows.size() == 1 && rrows[0].name == "第一營" &&
                  !rrows[0].dead,
              "drawer: roster row");
        std::vector<Potato::Campaign::AftermathRow> report = {
            {"第一營", 12, true}};
        Check(Potato::Campaign::ApplyAftermath(state, report)
                  .ok(),
              "drawer: aftermath applies");
        rrows = Potato::Game::SquadRows(state);
        Check(rrows[0].dead && rrows[0].casualties == 12,
              "drawer: memorial row stays visible");

        Ledger ledger;
        for (int i = 0; i < 3; ++i) {
            Posting p;
            p.credit = {Account::Materiel, 10};
            p.debit = {Account::ArmyPrestige, 10};
            p.memo = "entry " + std::to_string(i);
            Check(ledger.Post(p).ok(), "drawer: post");
        }
        auto lrows = Potato::Game::LedgerRows(ledger, 2);
        Check(lrows.size() == 2 && lrows[0].memo == "entry 1" &&
                  lrows[1].memo == "entry 2",
              "drawer: ledger tail is chronological");
        Check(Potato::Game::LedgerRows(ledger, 0).empty(),
              "drawer: zero tail is empty");
    }

    // --- 本草 rows: unlocked in delivery order + pending ---
    {
        const fs::path dir = TestDir();
        WriteFile(dir, "h.json",
                  "{\"schema\":\"potato.bencao/1\",\"id\":\"sanqi\","
                  "\"category\":\"jinshi\",\"name\":\"三七\","
                  "\"nature\":\"甘微苦溫\",\"indications\":\"止血\","
                  "\"source\":\"卷一\","
                  "\"unlock\":{\"kind\":\"ledger_tag\",\"tag\":\"x\"}}");
        Potato::Campaign::BencaoLibrary lib;
        Check(Potato::Campaign::BencaoLibrary::Load(dir, lib).ok &&
                  lib.Size() == 1,
              "drawer: bencao library loads");
        fs::remove(dir / "h.json");
        auto cod = Potato::Campaign::BencaoCodex::FromJson(
            JsonValue::Parse(
                "{\"schema\":\"potato.bencao_state/1\","
                "\"unlocked\":[\"sanqi\"],"
                "\"pending\":[{\"id\":\"ghost\",\"kind\":\"terrain\","
                "\"detail\":\"forest\"}]}").value);
        Check(cod.ok(), "drawer: codex doc parses");
        auto crows = Potato::Game::CodexRows(lib, cod.value);
        Check(crows.size() == 2 && crows[0].id == "sanqi" &&
                  !crows[0].pending && crows[0].name == "三七" &&
                  crows[0].category == "jinshi",
              "drawer: unlocked page named + categorized");
        Check(crows[1].pending && crows[1].id == "ghost" &&
                  crows[1].name == "ghost",
              "drawer: pending page falls back to id");
    }

    // ---------- 13.5: creation screen view-model ----------
    {
        Potato::Gameplay::DoctrineLibrary cards =
            Potato::Gameplay::DoctrineLibrary::FromJson(
                JsonValue::Parse(R"json({
                    "schema": "potato.doctrine_cards/1",
                    "cards": [
                        {"id": "c_hold", "name": "堅守",
                         "trigger": {"type": "always"},
                         "action": {"type": "hold"}},
                        {"id": "c_push", "name": "挺進",
                         "trigger": {"type": "always"},
                         "action": {"type": "move", "param": 0}},
                        {"id": "c_brace",
                         "trigger": {"type": "always"},
                         "action": {"type": "brace", "param": 5}},
                        {"id": "c_burn",
                         "trigger": {"type": "always"},
                         "action": {"type": "burn"}},
                        {"id": "c_rest",
                         "trigger": {"type": "always"},
                         "action": {"type": "hold"}},
                        {"id": "c_wait",
                         "trigger": {"type": "always"},
                         "action": {"type": "hold"}}
                    ]
                })json").value).value;

        // Per-field validation — same bounds, field-attributed.
        CreationForm form;
        form.name = "tester";
        form.origin = "nowhere";
        Check(Potato::Game::ValidateForm(form).empty(),
              "creation: valid form validates clean");
        CreationForm bad = form;
        bad.name = "";
        auto errs = Potato::Game::ValidateForm(bad);
        Check(errs.size() == 1 &&
                  errs[0].field ==
                      Potato::Game::CreationField::Name,
              "creation: empty name flags Name");
        bad = form;
        bad.name = "   ";
        errs = Potato::Game::ValidateForm(bad);
        Check(errs.size() == 1 &&
                  errs[0].message.find("blank") !=
                      std::string::npos,
              "creation: blank name flags blank");
        bad = form;
        bad.origin = std::string(200, 'x');
        errs = Potato::Game::ValidateForm(bad);
        Check(errs.size() == 1 &&
                  errs[0].field ==
                      Potato::Game::CreationField::Origin,
              "creation: oversized origin flags Origin");
        bad = form;
        bad.deck.assign(40, "c");
        errs = Potato::Game::ValidateForm(bad);
        Check(errs.size() == 1 &&
                  errs[0].field ==
                      Potato::Game::CreationField::Deck,
              "creation: oversized deck flags Deck");

        // Gate-reason → field mapping for commit echo.
        Check(Potato::Game::FieldForReason(
                  "missing/empty/out-of-range 'origin'") ==
                  Potato::Game::CreationField::Origin,
              "creation: gate reason maps to field");
        Check(Potato::Game::FieldForReason(
                  "'deck_seed' must be an int") ==
                  Potato::Game::CreationField::DeckSeed,
              "creation: seed reason maps to DeckSeed");
        Check(Potato::Game::FieldForReason(
                  "name and origin are required") ==
                  Potato::Game::CreationField::Name,
              "creation: preflight reason maps to first field");

        // Deterministic roll: same seed → same deck, 3..5
        // distinct, all resolving.
        const auto r1 = Potato::Game::RollDeck(42, cards);
        const auto r2 = Potato::Game::RollDeck(42, cards);
        Check(r1 == r2, "creation: roll is deterministic");
        Check(r1.size() >= 3 && r1.size() <= 5,
              "creation: roll within slot bounds");
        {
            std::set<std::string> uniq(r1.begin(), r1.end());
            Check(uniq.size() == r1.size(),
                  "creation: roll is distinct");
            bool all = true;
            for (const auto& id : r1) {
                if (cards.Find(id) == nullptr) all = false;
            }
            Check(all, "creation: roll resolves in library");
        }
        Potato::Gameplay::DoctrineLibrary empty;
        Check(Potato::Game::RollDeck(1, empty).empty(),
              "creation: empty library → empty roll");

        // Preview: explicit deck verbatim + unknown flags;
        // empty deck previews the seed roll.
        form.deck = {"c_hold", "ghost_card"};
        auto prev = Potato::Game::PreviewDeck(form, cards);
        Check(prev.size() == 2 && prev[0].resolved &&
                  prev[0].name == "堅守" &&
                  !prev[1].resolved &&
                  prev[1].name == "ghost_card",
              "creation: explicit preview flags unknown");
        form.deck.clear();
        form.deckSeed = 42;
        prev = Potato::Game::PreviewDeck(form, cards);
        Check(prev.size() == r1.size() &&
                  prev[0].id == r1[0],
              "creation: empty deck previews the roll");
    }

    // --- two-step march intent ---
    {
        WorldState s = WorldState::Init(w, 42).value;
        Ledger ledger;
        Posting seed;
        seed.credit = {Account::Materiel, 100};
        seed.debit = {Account::ArmyPrestige, 100};
        seed.memo = "war chest";
        Check(ledger.Post(seed).ok(), "intent: seed funds");
        Potato::Game::MarchIntent intent;
        Check(!intent.Pending(), "intent: starts unstaged");
        auto miss = intent.Confirm(s, w, ledger);
        Check(!miss.ok(), "intent: confirm without stage fails");
        Check(!intent.Click(w, s, 100, "zzz"),
              "intent: non-adjacent click rejected");
        Check(intent.Click(w, s, 100, "b"),
              "intent: adjacent node stages");
        Check(intent.Pending() &&
                  intent.Option().nodeId == "b" &&
                  intent.Option().days == 2 &&
                  intent.Option().affordable,
              "intent: staged option carries cost");
        // Re-clicking a different legal node restages.
        Check(intent.Click(w, s, 100, "d") &&
                  intent.Option().nodeId == "d",
              "intent: re-click restages");
        // Clicking out clears.
        Check(!intent.Click(w, s, 100, "zzz") &&
                  !intent.Pending(),
              "intent: click-out clears");
        Check(intent.Click(w, s, 100, "b"), "intent: restage b");
        auto plan = intent.Confirm(s, w, ledger);
        Check(plan.ok() && plan.value.to == "b" &&
                  s.Marching() && !intent.Pending(),
              "intent: confirm issues + clears");
        Check(!intent.Click(w, s, 100, "a"),
              "intent: no staging while marching");
    }

    // --- encounter launch errors stay visible and preflight-only ---
    {
        Potato::Campaign::EncounterDef enc;
        enc.id = "test_encounter";
        enc.node = "a";
        enc.map = "field";
        Potato::Gameplay::DoctrineLibrary cards;
        Potato::Gameplay::SquadTemplateLibrary squads;
        Potato::Game::BattleMapCatalog maps;
        auto launchWs = WorldState::Init(w, 42).value;
        const std::string nodeBefore = launchWs.WarbandAt();
        const std::int64_t dayBefore = launchWs.Day();

        auto missingMap = Potato::Game::PrepareEncounterLaunch(
            enc, w, maps, squads, cards,
            std::span<const Potato::Campaign::PlayerDeploy>{},
            nullptr, 7);
        Potato::Game::ActionNotice notice;
        Potato::Game::ShowActionResult(notice, missingMap, "應戰");
        Check(!missingMap.ok() && notice.visible &&
                  !notice.success &&
                  notice.text.find("找不到戰場地圖「field」") !=
                      std::string::npos,
              "encounter launch: missing map explains rejection");

        const char* mapDoc = R"json({
            "schema":"potato.map/1","id":"field",
            "regions":[{"id":"r0"}],"edges":[]
        })json";
        auto battleMap = Potato::Gameplay::BattleMap::FromJson(
            JsonValue::Parse(mapDoc).value);
        Check(battleMap.ok(), "encounter launch: fixture map loads");
        if (battleMap.ok()) {
            maps.emplace(battleMap.value.Id(),
                         std::move(battleMap.value));
            auto rejectedMarshal =
                Potato::Game::PrepareEncounterLaunch(
                    enc, w, maps, squads, cards,
                    std::span<const Potato::Campaign::PlayerDeploy>{},
                    nullptr, 7);
            Potato::Game::ShowActionResult(
                notice, rejectedMarshal, "應戰");
            Check(!rejectedMarshal.ok() && notice.visible &&
                      !notice.success &&
                      notice.text.find("編隊失敗") !=
                          std::string::npos &&
                      notice.text.find("no player deployment") !=
                          std::string::npos,
                  "encounter launch: marshal rejection explains cause");
        }
        Check(launchWs.WarbandAt() == nodeBefore &&
                  launchWs.Day() == dayBefore,
              "encounter launch: preflight leaves campaign untouched");
    }

    if (failures == 0) {
        std::printf("OVERWORLD TESTS PASS\n");
        return 0;
    }
    std::printf("%d FAILURES\n", failures);
    return 1;
}
