#include "Gameplay/Plan/BattlePlan.h"
#include "Gameplay/Doctrine/Doctrine.h"
#include "Gameplay/Fog/QuantumFog.h"
#include "Gameplay/Map/BattleMap.h"
#include "Gameplay/Sim/BattleController.h"
#include "Gameplay/Squad/Squad.h"
#include "Gameplay/Json/JsonValue.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;
void Check(bool cond, const char* name) {
    std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", name);
    if (!cond) ++failures;
}

using namespace Potato::Gameplay;

// Line map: r0 - r1 - r2 - r3
const char* MAP_DOC = R"json({
    "schema": "potato.map/1", "id": "m", "name": "Line",
    "regions": [{"id": "r0"}, {"id": "r1"}, {"id": "r2"}, {"id": "r3"}],
    "edges": [["r0", "r1"], ["r1", "r2"], ["r2", "r3"]]
})json";

const char* CARDS_DOC = R"json({
    "schema": "potato.doctrine_cards/1",
    "cards": [
        {"id": "card_idle", "cooldown": 5,
         "trigger": {"type": "always"}, "action": {"type": "hold"}},
        {"id": "card_wait", "cooldown": 5,
         "trigger": {"type": "always"}, "action": {"type": "hold"}},
        {"id": "card_rest", "cooldown": 5,
         "trigger": {"type": "always"}, "action": {"type": "hold"}}
    ]
})json";

BattleMap LoadMap() {
    auto d = JsonValue::Parse(MAP_DOC);
    return BattleMap::FromJson(d.value).value;
}
DoctrineLibrary LoadCards() {
    auto d = JsonValue::Parse(CARDS_DOC);
    return DoctrineLibrary::FromJson(d.value).value;
}
SquadTemplate MakeTemplate() {
    SquadTemplate t; t.id = "t"; t.unit = UnitType::Infantry; t.hp = 100;
    t.attack = 10;
    t.speed = Speed::VeryFast; // 20 ticks/edge
    return t;
}

} // namespace

int main() {
    BattleMap map = LoadMap();
    DoctrineLibrary cards = LoadCards();
    SquadTemplate tmpl = MakeTemplate();
    Check(map.RegionCount() == 4, "fixtures load");

    // --- MeanCertainty helper ---
    {
        QuantumFog fog(map, FogConfig{});
        fog.Observe(0); fog.Observe(1);
        Check(MeanCertainty(fog, {0, 1}) == 100, "mean of seen = 100");
        Check(MeanCertainty(fog, {0, 2}) == 50, "mean mixes seen/unseen");
        Check(MeanCertainty(fog, {}) == 0, "empty path mean 0");
    }

    // --- PlanConfig from potato.balance/1 ---
    {
        auto cfg = PlanConfig::FromJson(JsonValue::Parse(
            R"({"schema":"potato.balance/1","plan":{"bonus_percent_cap":50,"replan_cost":3}})").value);
        Check(cfg.ok() && cfg.value.bonusPercentCap == 50 &&
              cfg.value.replanCost == 3, "plan section loads");
        Check(PlanConfig::FromJson(JsonValue::Parse("{}").value).ok(),
              "missing plan section -> defaults");
        Check(!PlanConfig::FromJson(JsonValue::Parse(
            R"({"plan":{"bonus_percent_cap":150}})").value).ok(),
              "cap >100 rejected");
    }

    // --- DrawArrow validation ---
    {
        BattleController bc(1, map, cards);
        bc.DeploySquad(tmpl, 0, 0);
        bc.DeploySquad(tmpl, 3, 1);
        Check(bc.DrawArrow(0, 0, {0, 1, 2}), "valid arrow accepted");
        Check(!bc.DrawArrow(0, 0, {0, 2}), "non-adjacent rejected");
        Check(!bc.DrawArrow(0, 0, {1, 2}), "wrong start region rejected");
        Check(!bc.DrawArrow(0, 1, {3, 2}), "enemy squad rejected");
        Check(!bc.DrawArrow(0, 0, {0}), "degenerate path rejected");
        Check(!bc.DrawArrow(0, 0, {0, 1, 0}), "cycle/revisit rejected");
        Check(bc.DrawArrow(0, 0, {0, 1}), "redraw replaces arrow");
        Check(bc.ArrowOf(0).active && bc.ArrowOf(0).path.size() == 2,
              "replaced arrow stored");
        bc.RequestBeat(BattleBeat::Execution);
        Check(!bc.DrawArrow(0, 0, {0, 1}), "Execution rejects DrawArrow");
    }

    // --- Bonus scales with certainty over covered ground ---
    // Deployment footprint is initial intel: at Execution start a
    // SyncFog pass observes own+adjacent regions before bonuses.
    {
        BattleController bc(2, map, cards);
        bc.DeploySquad(tmpl, 0, 0);   // sees r0,r1
        bc.DeploySquad(tmpl, 3, 1);
        Check(bc.DrawArrow(0, 0, {0, 1, 2}), "arrow drawn");
        const int base = bc.Squads()[0].attack;
        bc.RequestBeat(BattleBeat::Execution);
        // mean(100, 100, 0) = 66 -> grant = 10*66/100 = 6
        Check(bc.Squads()[0].attack == base + 6 &&
              bc.ArrowOf(0).bonusApplied == 6,
              "bonus = mean certainty 66% of attack");
        BattleController bc2(9, map, cards);
        bc2.DeploySquad(tmpl, 0, 0);
        bc2.DeploySquad(tmpl, 3, 1);
        bc2.DrawArrow(0, 0, {0, 1}); // fully observed: mean 100
        bc2.RequestBeat(BattleBeat::Execution);
        Check(bc2.Squads()[0].attack == base + 10,
              "fully-observed path -> 100% bonus");
        // Cap pinning: PlanConfig{cap 50} over fully-observed ground
        // must grant 50%, not 100% (kills the min(mean,cap) mutant).
        PlanConfig capped; capped.bonusPercentCap = 50;
        BattleController bc3(11, map, cards, FogConfig{}, capped);
        bc3.DeploySquad(tmpl, 0, 0);
        bc3.DeploySquad(tmpl, 3, 1);
        bc3.DrawArrow(0, 0, {0, 1});
        bc3.RequestBeat(BattleBeat::Execution);
        Check(bc3.Squads()[0].attack == base + 5 &&
              bc3.ArrowOf(0).bonusApplied == 5,
              "bonus_percent_cap=50 clamps the grant");
    }

    // --- Arrow march: squad auto-walks the path ---
    {
        BattleController bc(3, map, cards);
        bc.DeploySquad(tmpl, 0, 0);
        bc.DeploySquad(tmpl, 3, 1);
        Check(bc.DrawArrow(0, 0, {0, 1, 2}), "arrow drawn");
        bc.RequestBeat(BattleBeat::Execution);
        bc.Tick(); // march orders leg 1: r0->r1
        Check(bc.Squads()[0].state == SquadState::Moving &&
              bc.Squads()[0].edgeTarget == 1,
              "arrow march issues first leg");
        for (int i = 0; i < 20; ++i) bc.Tick(); // 20 ticks/edge vfast
        Check(bc.Squads()[0].regionIndex == 1, "leg 1 complete");
        bc.Tick(); // next leg ordered
        for (int i = 0; i < 21; ++i) bc.Tick();
        Check(bc.Squads()[0].regionIndex == 2,
              "arrow completes the path");
        Check(bc.ArrowOf(0).cursor == 2, "cursor at path end");
    }

    // --- Replan mid-execution: CP cost + real-time certainty ---
    {
        BattleController bc(4, map, cards);
        bc.DeploySquad(tmpl, 0, 0);
        bc.DeploySquad(tmpl, 3, 1);
        bc.DrawArrow(0, 0, {0, 1});
        bc.SetCpPool(0, 5);
        bc.RequestBeat(BattleBeat::Execution);
        bc.Tick(); // march ordered r0->r1; Moving, edgeTarget=1
        const int withBonus = bc.Squads()[0].attack; // 10+10 (full intel)
        Check(withBonus == 20, "exec-start bonus applied");
        // Replan while Moving: anchor = edgeTarget r1 — the new path
        // must CONTAIN r1; cursor seeks it so the march resumes on
        // arrival instead of stalling at cursor=0.
        auto r = bc.IssueReplan(0, 0, {0, 1, 2});
        Check(r.ok() && bc.CpPool(0) == 3, "replan accepted, costs 2");
        Check(!bc.IssueReplan(0, 0, {0, 1}).ok(),
              "second pending replan rejected (dup guard)");
        bc.Tick(); // applies: cursor seeks anchor r1 -> index 1
        // old grant 10 reversed; new grant = 10*66/100 = 6 -> attack 16
        Check(bc.ArrowOf(0).path.size() == 3 &&
              bc.ArrowOf(0).cursor == 1 &&
              bc.Squads()[0].attack == 16 &&
              bc.ArrowOf(0).bonusApplied == 6,
              "replan recomputes, cursor on anchor");
        // March continuation: after arrival at r1 the arrow must keep
        // walking (the mid-leg replan stall is the fixed defect).
        for (int i = 0; i < 21; ++i) bc.Tick(); // arrive r1
        Check(bc.Squads()[0].regionIndex == 1, "mid-leg arrival at r1");
        for (int i = 0; i < 21; ++i) bc.Tick(); // march r1->r2
        Check(bc.Squads()[0].regionIndex == 2,
              "replanned arrow resumes marching after arrival");
    }

    // --- Replan anchor contract + probe-then-replan ---
    {
        BattleController bc(7, map, cards);
        bc.DeploySquad(tmpl, 0, 0);
        bc.DeploySquad(tmpl, 3, 1);
        bc.DrawArrow(0, 0, {0, 1});
        bc.SetCpPool(0, 5);
        bc.RequestBeat(BattleBeat::Execution);
        bc.Tick();
        // Anchor r1 (edgeTarget mid-leg): a path NOT containing r1 is
        // rejected at issue — never becomes a dead arrow.
        Check(!bc.IssueReplan(0, 0, {2, 3}).ok(),
              "replan without the anchor rejected");
        Check(bc.CpPool(0) == 5, "rejected replan keeps CP");
        // Probe-then-replan: raise certainty on the destination leg,
        // THEN replan — grant must reflect the raised field.
        const int attackNow = bc.Squads()[0].attack;
        bc.IssueProbe(0, 2); // certainty r2: 0 -> 25
        bc.Tick();           // probe applies; r2 now 25
        auto r = bc.IssueReplan(0, 0, {0, 1, 2});
        Check(r.ok(), "replan over probed ground accepted");
        bc.Tick();
        // mean(100,100,25) = 75 -> grant = attack(without old bonus)...
        // old bonus 10 reversed: 20->10; grant = 10*75/100 = 7 -> 17
        Check(bc.Squads()[0].attack == 17 &&
              bc.ArrowOf(0).bonusApplied == 7,
              "replan sees probed certainty (mean 75, not 66)");
        Check(attackNow == 20, "fixture sanity");
    }

    // --- Land-ahead cursor resync via Redirect ---
    {
        BattleController bc(8, map, cards);
        bc.DeploySquad(tmpl, 0, 0);
        bc.DeploySquad(tmpl, 3, 1);
        bc.DrawArrow(0, 0, {0, 1, 2, 3});
        bc.SetCpPool(0, 5);
        bc.RequestBeat(BattleBeat::Execution);
        // Redirect onto path[1] while still Holding (applies before
        // MarchArrows in the same tick).
        Check(bc.IssueRedirect(0, 0, 1).ok(), "redirect accepted");
        bc.Tick(); // redirect applies -> Moving r0->r1; march skips
        for (int i = 0; i < 21; ++i) bc.Tick(); // arrive r1
        Check(bc.Squads()[0].regionIndex == 1, "arrived at r1");
        bc.Tick(); // resync: cursor seeks r1 (index 1) -> orders r2
        Check(bc.Squads()[0].state == SquadState::Moving &&
              bc.Squads()[0].edgeTarget == 2 &&
              bc.ArrowOf(0).cursor == 2,
              "cursor resyncs onto the squad's position");
    }

    // --- Stale path / rejects ---
    {
        BattleController bc(6, map, cards);
        bc.DeploySquad(tmpl, 0, 0);
        bc.DeploySquad(tmpl, 3, 1);
        bc.SetCpPool(0, 2);
        bc.RequestBeat(BattleBeat::Execution);
        Check(!bc.IssueReplan(0, 0, {1, 2}).ok(),
              "replan path not containing anchor rejected");
        Check(!bc.IssueReplan(0, 0, {0, 3}).ok(),
              "non-adjacent replan path rejected");
        Check(!bc.IssueReplan(0, 0, {0, 1, 0}).ok(),
              "cycle replan path rejected");
        Check(bc.CpPool(0) == 2, "rejected replans keep CP");
    }

    // --- Planning-phase checksum isolation (arrow fold pinned) ---
    {
        auto planningRun = [&](std::vector<std::size_t> path) {
            BattleController b(42, map, cards);
            b.DeploySquad(tmpl, 0, 0);
            b.DeploySquad(tmpl, 3, 1);
            b.DrawArrow(0, 0, path);
            return b.Checksum(); // Planning: squad state identical
        };
        Check(planningRun({0, 1, 2}) != planningRun({0, 1}),
              "checksum differs on arrow path alone (Planning)");
        Check(planningRun({0, 1, 2}) == planningRun({0, 1, 2}),
              "checksum stable for identical plans");
    }

    // --- Determinism: identical runs produce identical checksums ---
    {
        auto run = [&] {
            BattleController b(42, map, cards);
            b.DeploySquad(tmpl, 0, 0);
            b.DeploySquad(tmpl, 3, 1);
            b.DrawArrow(0, 0, {0, 1, 2});
            b.RequestBeat(BattleBeat::Execution);
            for (int i = 0; i < 30; ++i) b.Tick();
            return b.Checksum();
        };
        Check(run() == run(), "same seed + same plan -> same checksum");
        auto runAlt = [&] {
            BattleController b(42, map, cards);
            b.DeploySquad(tmpl, 0, 0);
            b.DeploySquad(tmpl, 3, 1);
            b.DrawArrow(0, 0, {0, 1}); // different arrow
            b.RequestBeat(BattleBeat::Execution);
            for (int i = 0; i < 30; ++i) b.Tick();
            return b.Checksum();
        };
        Check(run() != runAlt(), "different arrow -> different checksum");
    }

    if (failures == 0) { std::puts("PLAN TESTS PASS"); return 0; }
    std::printf("%d CHECK(S) FAILED\n", failures);
    return 1;
}
