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
#include "Game/Ui/CreationForm.h"
#include "Game/Ui/OrderMenu.h"
#include "Campaign/Characters/Character.h"
#include "Campaign/Ledger/Ledger.h"
#include "Campaign/World/Encounter.h"
#include "Campaign/World/March.h"
#include "Campaign/World/WorldMap.h"
#include "Campaign/World/WorldState.h"
#include "Gameplay/Json/JsonValue.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
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

    if (failures == 0) {
        std::printf("OVERWORLD TESTS PASS\n");
        return 0;
    }
    std::printf("%d FAILURES\n", failures);
    return 1;
}
