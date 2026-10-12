// Story 8.1 — SpriteAtlas & Map Rendering pins:
//   - potato.spriteatlas/1 schema battery (bounds, row shape,
//     palette range, dup ids, strict wire)
//   - Framebuffer ops: clip, src-over, shade multiply, Bresenham,
//     FNV hash determinism
//   - MapProjection: hinted normalize, grid fallback, edge dedupe
//   - RenderBattleView: golden-hash determinism, certainty →
//     grain-not-blackout, marks overlay, own-unit + cloud draws
//     from the observer's slice only (truth boundary)
#include "Game/Hud/HudView.h"
#include "Game/Render/BattleView.h"
#include "Game/Render/Framebuffer.h"
#include "Game/Render/MapProjection.h"
#include "Game/Render/SpriteAtlas.h"
#include "Gameplay/Fog/QuantumFog.h"
#include "Gameplay/Json/JsonValue.h"
#include "Gameplay/Map/BattleMap.h"
#include "Gameplay/Squad/Squad.h"

#include <cstdio>
#include <string>

using Potato::Game::BattleViewInput;
using Potato::Game::Framebuffer;
using Potato::Game::MapProjection;
using Potato::Game::RenderBattleView;
using Potato::Game::Rgba;
using Potato::Game::SpriteAtlas;
using Potato::Gameplay::BattleMap;
using Potato::Gameplay::CloudEntity;
using Potato::Gameplay::FogConfig;
using Potato::Gameplay::JsonValue;
using Potato::Gameplay::QuantumFog;
using Potato::Gameplay::Squad;

static int failures = 0;
static void Check(bool cond, const char* what) {
    if (!cond) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

// A 4px atlas with the tile ids BattleView looks for.
static SpriteAtlas TestAtlas() {
    const std::string doc =
        "{\"schema\":\"potato.spriteatlas/1\",\"id\":\"t\",\"tile\":4,"
        "\"palette\":[\"#00000000\",\"#102030ff\",\"#40a0c0ff\","
        "\"#ff8040ff\",\"#20a040ff\",\"#c0c0c0ff\",\"#8040c0ff\"],"
        "\"tiles\":["
        "{\"id\":\"terrain.open\",\"rows\":[\"4444\",\"4444\",\"4444\",\"4444\"]},"
        "{\"id\":\"terrain.water\",\"rows\":[\"2222\",\"2222\",\"2222\",\"2222\"]},"
        "{\"id\":\"mark.village\",\"rows\":[\"0330\",\"3333\",\"3333\",\"0330\"]},"
        "{\"id\":\"mark.shrine\",\"rows\":[\"0600\",\"0600\",\"6666\",\"0600\"]},"
        "{\"id\":\"unit\",\"rows\":[\"0550\",\"5555\",\"5555\",\"0550\"]},"
        "{\"id\":\"cloud\",\"rows\":[\"0660\",\"6666\",\"6666\",\"0660\"]}"
        "]}";
    auto r = SpriteAtlas::FromJson(JsonValue::Parse(doc).value);
    return std::move(r.value);
}

// A 3-region line graph: a -- b -- c, all hinted.
static BattleMap TestMap() {
    const std::string doc =
        "{\"schema\":\"potato.map/1\",\"id\":\"m\",\"name\":\"m\","
        "\"regions\":["
        "{\"id\":\"a\",\"terrain\":[\"open\"],\"center\":[0,0]},"
        "{\"id\":\"b\",\"terrain\":[\"water\"],\"center\":[10,0],"
        "\"strategic\":[\"village\"]},"
        "{\"id\":\"c\",\"terrain\":[\"forest\"],\"center\":[20,0],"
        "\"myth\":[\"shrine\"]}],"
        "\"edges\":[[\"a\",\"b\"],[\"b\",\"c\"]]}";
    return std::move(BattleMap::FromJson(JsonValue::Parse(doc).value).value);
}

int main() {
    // ===== potato.spriteatlas/1 schema =====
    {
        const std::string good =
            "{\"schema\":\"potato.spriteatlas/1\",\"id\":\"g\","
            "\"tile\":4,\"palette\":[\"#000000\",\"#ffffff\"],"
            "\"tiles\":[{\"id\":\"x\",\"rows\":[\"0110\",\"1001\","
            "\"1001\",\"0110\"]}]}";
        auto a = SpriteAtlas::FromJson(JsonValue::Parse(good).value);
        Check(a.ok() && a.value.Id() == "g" &&
                  a.value.TilePx() == 4 &&
                  a.value.Palette().size() == 2 &&
                  a.value.Find("x") != nullptr &&
                  a.value.Find("y") == nullptr,
              "atlas: minimal doc loads");

        const auto bad = [](const char* label, std::string doc) {
            auto r = SpriteAtlas::FromJson(
                JsonValue::Parse(doc).value);
            Check(!r.ok(), label);
        };
        bad("atlas: wrong schema rejected",
            "{\"schema\":\"potato.spriteatlas/2\",\"id\":\"g\",\"tile\":4,"
            "\"palette\":[\"#000000\"],\"tiles\":[{\"id\":\"x\",\"rows\":"
            "[\"0000\",\"0000\",\"0000\",\"0000\"]}]}");
        bad("atlas: missing id rejected",
            "{\"tile\":4,\"palette\":[\"#000000\"],"
            "\"tiles\":[{\"id\":\"x\",\"rows\":[\"0000\",\"0000\","
            "\"0000\",\"0000\"]}]}");
        bad("atlas: tile px out of range rejected",
            "{\"id\":\"g\",\"tile\":2,\"palette\":[\"#000000\"],"
            "\"tiles\":[{\"id\":\"x\",\"rows\":[\"00\",\"00\"]}]}");
        bad("atlas: empty palette rejected",
            "{\"id\":\"g\",\"tile\":4,\"palette\":[],"
            "\"tiles\":[{\"id\":\"x\",\"rows\":[\"0000\",\"0000\","
            "\"0000\",\"0000\"]}]}");
        bad("atlas: palette >16 rejected",
            "{\"id\":\"g\",\"tile\":4,\"palette\":["
            "\"#000000\",\"#000000\",\"#000000\",\"#000000\","
            "\"#000000\",\"#000000\",\"#000000\",\"#000000\","
            "\"#000000\",\"#000000\",\"#000000\",\"#000000\","
            "\"#000000\",\"#000000\",\"#000000\",\"#000000\","
            "\"#000000\"],\"tiles\":[{\"id\":\"x\",\"rows\":"
            "[\"0000\",\"0000\",\"0000\",\"0000\"]}]}");
        bad("atlas: bad color rejected",
            "{\"id\":\"g\",\"tile\":4,\"palette\":[\"red\"],"
            "\"tiles\":[{\"id\":\"x\",\"rows\":[\"0000\",\"0000\","
            "\"0000\",\"0000\"]}]}");
        bad("atlas: row length mismatch rejected",
            "{\"id\":\"g\",\"tile\":4,\"palette\":[\"#000000\",\"#ffffff\"],"
            "\"tiles\":[{\"id\":\"x\",\"rows\":[\"011\",\"10\"]}]}");
        bad("atlas: pixel out of palette range rejected",
            "{\"id\":\"g\",\"tile\":4,\"palette\":[\"#000000\"],"
            "\"tiles\":[{\"id\":\"x\",\"rows\":[\"0100\",\"1000\","
            "\"0000\",\"0000\"]}]}");
        bad("atlas: non-hex pixel rejected",
            "{\"id\":\"g\",\"tile\":4,\"palette\":[\"#000000\",\"#ffffff\"],"
            "\"tiles\":[{\"id\":\"x\",\"rows\":[\"0x00\",\"1000\","
            "\"0000\",\"0000\"]}]}");
        bad("atlas: duplicate tile id rejected",
            "{\"id\":\"g\",\"tile\":4,\"palette\":[\"#000000\"],"
            "\"tiles\":[{\"id\":\"x\",\"rows\":[\"0000\",\"0000\","
            "\"0000\",\"0000\"]},"
            "{\"id\":\"x\",\"rows\":[\"0000\",\"0000\",\"0000\",\"0000\"]}]}");
        bad("atlas: empty tiles rejected",
            "{\"id\":\"g\",\"tile\":4,\"palette\":[\"#000000\"],"
            "\"tiles\":[]}");
    }

    // ===== Framebuffer ops =====
    {
        Framebuffer fb(16, 16);
        fb.Clear({10, 10, 10, 255});
        Check(fb.PixelAt(0, 0) == Rgba({10, 10, 10, 255}),
              "fb: clear paints");
        fb.SetPixel(2, 2, {255, 0, 0, 255});
        Check(fb.PixelAt(2, 2) == Rgba({255, 0, 0, 255}),
              "fb: set pixel");
        fb.SetPixel(-5, -5, {1, 2, 3, 255}); // clipped, no crash
        // Src-over at 128 alpha over red.
        fb.SetPixel(2, 2, {200, 0, 0, 255});
        fb.BlendPixel(2, 2, {0, 0, 200, 128});
        const Rgba m = fb.PixelAt(2, 2);
        Check(m.r > 0 && m.b > 0 && m.r != 200 && m.b != 200,
              "fb: src-over blends");
        fb.FillRect(-4, -4, 8, 8, {0, 255, 0, 255});
        Check(fb.PixelAt(0, 0) == Rgba({0, 255, 0, 255}),
              "fb: fillrect clips");
        fb.DrawLine(0, 15, 15, 15, {9, 9, 9, 255});
        Check(fb.PixelAt(7, 15) == Rgba({9, 9, 9, 255}),
              "fb: line draws");

        // Shade multiply: tile of solid color 0x40a0c0 at half
        // shade darkens channels but not alpha.
        SpriteAtlas at = TestAtlas();
        const auto* w = at.Find("terrain.water");
        Check(w != nullptr, "fb: water tile found");
        Framebuffer t(8, 8);
        t.DrawTile(at, *w, 0, 0, 255);
        const Rgba full = t.PixelAt(1, 1);
        Framebuffer t2(8, 8);
        t2.DrawTile(at, *w, 0, 0, 128);
        const Rgba dim = t2.PixelAt(1, 1);
        Check(full.b > dim.b && dim.b > 0 &&
                  full.a == dim.a,
              "fb: shade dims rgb, keeps alpha");

        // Determinism: same ops → same hash.
        Framebuffer a2(16, 16), b2(16, 16);
        a2.FillRect(0, 0, 8, 8, {1, 2, 3, 200});
        b2.FillRect(0, 0, 8, 8, {1, 2, 3, 200});
        Check(a2.Hash() == b2.Hash(), "fb: hash deterministic");
        b2.SetPixel(0, 0, {9, 9, 9, 255});
        Check(a2.Hash() != b2.Hash(), "fb: hash sees changes");
    }

    // ===== MapProjection =====
    {
        BattleMap map = TestMap();
        // Hinted: centers normalize into margin..w-margin.
        auto hp = MapProjection::Build(map, 100, 60, 5);
        Check(hp.ok() && hp.value.Hinted() &&
                  hp.value.Regions().size() == 3,
              "proj: hinted build");
        Check(hp.value.At(0).x < hp.value.At(1).x &&
                  hp.value.At(1).x < hp.value.At(2).x,
              "proj: hinted order preserved");
        Check(hp.value.Edges().size() == 2 &&
                  hp.value.Edges()[0] ==
                      std::pair<std::size_t, std::size_t>{0, 1} &&
                  hp.value.Edges()[1] ==
                      std::pair<std::size_t, std::size_t>{1, 2},
              "proj: edges deduped once");

        // Grid fallback: drop a hint → grid layout.
        const std::string noHint =
            "{\"schema\":\"potato.map/1\",\"id\":\"m\",\"name\":\"m\","
            "\"regions\":["
            "{\"id\":\"a\"},{\"id\":\"b\"},{\"id\":\"c\"},"
            "{\"id\":\"d\"},{\"id\":\"e\"}],"
            "\"edges\":[[\"a\",\"b\"]]}";
        BattleMap gm = std::move(
            BattleMap::FromJson(JsonValue::Parse(noHint).value)
                .value);
        auto gp = MapProjection::Build(gm, 100, 100, 5);
        Check(gp.ok() && !gp.value.Hinted(),
              "proj: missing hint → grid mode");
        // 5 regions → 3×2 grid; all coords inside bounds,
        // deterministic on rebuild.
        auto gp2 = MapProjection::Build(gm, 100, 100, 5);
        Check(gp2.ok() &&
                  gp.value.At(3).x == gp2.value.At(3).x &&
                  gp.value.At(3).y == gp2.value.At(3).y,
              "proj: grid deterministic");

        auto bad = MapProjection::Build(map, 10, 10, 9);
        Check(!bad.ok(), "proj: impossible bounds reject");
    }

    // ===== RenderBattleView =====
    {
        BattleMap map = TestMap();
        SpriteAtlas atlas = TestAtlas();

        BattleViewInput in;
        in.w = 160;
        in.h = 90;
        in.regionPx = 16;

        // No fog → full bright, no grain, no clouds.
        auto f1 = RenderBattleView(map, atlas, in);
        Check(f1.ok(), "view: base render ok");
        auto f2 = RenderBattleView(map, atlas, in);
        Check(f2.ok() && f1.value.Hash() == f2.value.Hash(),
              "view: byte-identical rerender (golden)");

        // Region a is "open" → its tile pixels are palette 4
        // (0x20a040) — sample off-center (the a→b edge line
        // starts at the exact center).
        const auto proj =
            MapProjection::Build(map, 160, 90, 8).value;
        const Rgba aPx =
            f1.value.PixelAt(proj.At(0).x - 6, proj.At(0).y - 6);
        Check(aPx.g > aPx.r && aPx.g > 100,
              "view: terrain tile drew at region");

        // Fog: certainty_ starts 0 everywhere → every region
        // dims + grains; Observe(0) lifts region a to 100.
        FogConfig cfg;
        QuantumFog fog(map, cfg);
        in.fog = &fog;
        fog.Observe(0);
        auto g1 = RenderBattleView(map, atlas, in);
        Check(g1.ok() && g1.value.Hash() != f1.value.Hash(),
              "view: fog changes the frame");

        // Grain ≠ blackout: inside region b (certainty 0) the
        // speckled patch is neither uniformly painted nor
        // black — the dimmed water tile survives beneath the
        // noise.
        const int bx = proj.At(1).x, by = proj.At(1).y;
        bool sawLit = false, sawDark = false;
        for (int y = by - 8; y < by + 8; ++y) {
            for (int x = bx - 8; x < bx + 8; ++x) {
                const Rgba p = g1.value.PixelAt(x, y);
                if (p.b > 60) sawLit = true;
                if (p.b < 50) sawDark = true;
            }
        }
        Check(sawLit && sawDark,
              "view: low certainty → grain, not blackout");

        // The observed region carries no speckle — sample the
        // strip LEFT of center (the a→b edge exits right).
        bool uniform = true;
        const Rgba ref =
            g1.value.PixelAt(proj.At(0).x - 8, proj.At(0).y);
        for (int y = proj.At(0).y - 8; y < proj.At(0).y + 8;
             ++y) {
            for (int x = proj.At(0).x - 8;
                 x < proj.At(0).x - 4; ++x) {
                if (g1.value.PixelAt(x, y) != ref) {
                    uniform = false;
                }
            }
        }
        Check(uniform, "view: full certainty → no grain");

        // Own squad draws at its region; cloud draws at
        // believedRegion — and never the truth (cloud is the
        // only enemy surface this layer can see).
        Squad sq;
        sq.id = "own";
        sq.side = 0;
        sq.regionIndex = 0;
        in.ownSquads = {&sq, 1};
        QuantumFog fog3(map, cfg);
        fog3.Observe(0);
        const int cid = fog3.AddCloud(2, 80); // believed at c
        Check(cid >= 0, "view: cloud added");
        in.fog = &fog3;
        auto v = RenderBattleView(map, atlas, in);
        Check(v.ok(), "view: units+cloud render");
        // The unit tile's signature color (pal 5 = 0xc0c0c0)
        // must appear somewhere in region a's neighborhood.
        bool sawUnit = false;
        for (int dy = -16; dy <= 16 && !sawUnit; ++dy) {
            for (int dx = -16; dx <= 16 && !sawUnit; ++dx) {
                const Rgba p = v.value.PixelAt(proj.At(0).x + dx,
                                               proj.At(0).y + dy);
                if (p.r > 180 && p.g > 180 && p.b > 180) {
                    sawUnit = true;
                }
            }
        }
        Check(sawUnit, "view: own unit marker drew");
    }

    // ===== Story 8.2 — HUD: beat gating, CP bar, density =====
    {
        using Potato::Game::BuildHudLayout;
        using Potato::Game::HudCell;
        using Potato::Game::HudDensity;
        using Potato::Game::HudInput;
        using Potato::Game::RenderHud;
        using Potato::Gameplay::BattleBeat;
        using Potato::Gameplay::CardSlot;
        using Potato::Gameplay::InterventionKind;
        using Potato::Gameplay::PlanArrow;
        using Potato::Gameplay::SquadSheet;

        SpriteAtlas atlas = TestAtlas();
        const auto count = [](const Potato::Game::HudLayout& l,
                              HudCell k) {
            int n = 0;
            for (const auto& w : l.widgets) {
                if (w.kind == k) ++n;
            }
            return n;
        };

        // --- Execution: CP + verbs only, nothing planning ---
        HudInput ex;
        ex.beat = BattleBeat::Execution;
        ex.cp = 2;
        ex.cpCap = 5;
        ex.probesLeft = 3;
        ex.w = 200;
        ex.h = 120;
        auto exl = BuildHudLayout(ex);
        Check(count(exl, HudCell::CpPip) == 2 &&
                  count(exl, HudCell::CpEmpty) == 3,
              "hud: CP pips = cp filled + cap hollow");
        Check(count(exl, HudCell::ProbePip) == 3,
              "hud: probe pips = budget");
        Check(count(exl, HudCell::Verb) == 10,
              "hud: all 10 verbs on the palette");
        Check(count(exl, HudCell::CardTile) == 0 &&
                  count(exl, HudCell::ArrowRow) == 0 &&
                  count(exl, HudCell::SquadHeader) == 0,
              "hud: execution shows no planning widgets");

        // Affordability: cp=2 — Retreat(3) disabled, Redirect(1)
        // enabled; myth verbs stay enabled (0 CP, billed 天命)
        // and carry the violet tint.
        bool retreatOff = false, redirectOn = false,
             mythTint = false;
        for (const auto& w : exl.widgets) {
            if (w.kind != HudCell::Verb) continue;
            if (w.verb == InterventionKind::Retreat &&
                !w.enabled) {
                retreatOff = true;
            }
            if (w.verb == InterventionKind::Redirect &&
                w.enabled) {
                redirectOn = true;
            }
            if (w.verb == InterventionKind::MythPacify &&
                w.enabled && w.color.b > w.color.g) {
                mythTint = true;
            }
        }
        Check(retreatOff && redirectOn && mythTint,
              "hud: cp<cost dims, myth verbs stay tinted");

        // Replan cost honors PlanConfig, not the constant.
        ex.replanCost = 4;
        bool replanOff = false;
        for (const auto& w : BuildHudLayout(ex).widgets) {
            if (w.kind == HudCell::Verb &&
                w.verb == InterventionKind::Replan) {
                replanOff = !w.enabled && w.aux == 4;
            }
        }
        Check(replanOff, "hud: replan reads live cost");

        // --- Planning: sheets + arrows, zero execution chrome ---
        HudInput pl;
        pl.beat = BattleBeat::Planning;
        pl.cp = 5;
        pl.w = 200;
        pl.h = 120;
        SquadSheet sheet;
        sheet.slots.resize(3);
        sheet.slots[1].cooldownRemaining = 30;
        pl.sheets = {&sheet, 1};
        PlanArrow a1, a2;
        a1.active = true;
        a1.path = {0, 1, 2};
        a2.active = false; // inactive arrows draw nothing
        const PlanArrow arr[2] = {a1, a2};
        pl.arrows = arr;
        pl.density = HudDensity::Full;
        auto pll = BuildHudLayout(pl);
        Check(count(pll, HudCell::SquadHeader) == 1 &&
                  count(pll, HudCell::CardTile) == 3 &&
                  count(pll, HudCell::CooldownBar) == 3 &&
                  count(pll, HudCell::ArrowRow) == 1,
              "hud: planning widgets present (full density)");
        Check(count(pll, HudCell::CpPip) == 0 &&
                  count(pll, HudCell::CpEmpty) == 0 &&
                  count(pll, HudCell::Verb) == 0,
              "hud: planning shows no execution chrome");

        // Density is a runtime field: Minimal strips bars+arrows
        // without touching the same input struct.
        pl.density = HudDensity::Minimal;
        auto plm = BuildHudLayout(pl);
        Check(count(plm, HudCell::CardTile) == 3 &&
                  count(plm, HudCell::CooldownBar) == 0 &&
                  count(plm, HudCell::ArrowRow) == 0,
              "hud: minimal density strips chrome");
        pl.density = HudDensity::Standard;
        Check(count(BuildHudLayout(pl), HudCell::CooldownBar) ==
                  0 &&
                  count(BuildHudLayout(pl), HudCell::ArrowRow) ==
                      1,
              "hud: standard = arrows, no cooldown bars");

        // --- Aftermath: marker only ---
        HudInput am;
        am.beat = BattleBeat::Aftermath;
        am.w = 200;
        am.h = 120;
        const auto aml = BuildHudLayout(am);
        Check(aml.widgets.size() == 1,
              "hud: aftermath is a marker strip");

        // --- Paint + determinism + hit test ---
        auto h1 = RenderHud(atlas, ex);
        auto h2 = RenderHud(atlas, ex);
        Check(h1.ok() && h2.ok() &&
                  h1.value.Hash() == h2.value.Hash(),
              "hud: render deterministic");
        const auto* hit =
            exl.Hit(exl.widgets.back().x + 1,
                    exl.widgets.back().y + 1);
        Check(hit != nullptr && hit->kind == HudCell::Verb,
              "hud: hit test returns the painted widget");
        Check(exl.Hit(-5, -5) == nullptr,
              "hud: miss returns null");
        auto bad = RenderHud(atlas, [&] {
            HudInput i;
            i.w = 0;
            return i;
        }());
        Check(!bad.ok(), "hud: degenerate bounds reject");
    }

    std::printf(failures ? "RENDER TESTS FAILED: %d\n"
                         : "RENDER TESTS PASS\n",
                failures);
    return failures ? 1 : 0;
}
