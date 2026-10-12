#include "Gameplay/Command/Intervention.h"
#include "Gameplay/Doctrine/Doctrine.h"
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

const char* MAP_DOC = R"json({
    "schema": "potato.map/1", "id": "m", "name": "Line",
    "regions": [{"id": "r0"}, {"id": "r1"}, {"id": "r2"}],
    "edges": [["r0", "r1"], ["r1", "r2"]]
})json";

const char* CARDS_DOC = R"json({
    "schema": "potato.doctrine_cards/1",
    "cards": [
        {"id": "card_brace", "cooldown": 5,
         "trigger": {"type": "enemy_adjacent"},
         "action": {"type": "brace", "param": 15}},
        {"id": "card_hold", "cooldown": 60,
         "trigger": {"type": "enemy_in_region"},
         "action": {"type": "hold"}},
        {"id": "card_wait", "cooldown": 5,
         "trigger": {"type": "always"},
         "condition": {"type": "cp_at_least", "param": 99},
         "action": {"type": "hold"}}
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
    t.speed = Speed::Medium;
    return t;
}

int CountEvents(const BattleController& bc, SimEvent::Kind k) {
    int n = 0;
    for (const auto& e : bc.Events()) if (e.kind == k) ++n;
    return n;
}

} // namespace

int main() {
    BattleMap map = LoadMap();
    DoctrineLibrary cards = LoadCards();
    SquadTemplate tmpl = MakeTemplate();
    Check(cards.Count() == 3, "fixtures load");

    Check(CostOf(InterventionKind::Redirect) == 1 &&
          CostOf(InterventionKind::Override) == 2 &&
          CostOf(InterventionKind::Retreat) == 3, "cost table 1/2/3");
    Check(CP_START == 3 && CP_CAP == 5 && CP_REGEN_TICKS == 1200,
          "economy constants");

    // --- Economy: start 3, regen +1/1200 ticks, cap 5, per-side ---
    {
        BattleController bc(1, map, cards);
        bc.DeploySquad(tmpl, 0, 0);
        bc.DeploySquad(tmpl, 2, 1);
        Check(bc.CpPool(0) == 3 && bc.CpPool(1) == 3, "start 3 per side");
        bc.RequestBeat(BattleBeat::Execution);
        for (int i = 0; i < CP_REGEN_TICKS; ++i) bc.Tick();
        Check(bc.CpPool(0) == 4 && bc.CpPool(1) == 4, "+1 after 60 s");
        for (int i = 0; i < CP_REGEN_TICKS * 3; ++i) bc.Tick();
        Check(bc.CpPool(0) == CP_CAP && bc.CpPool(1) == CP_CAP,
              "regen caps at 5");
    }

    // --- Redirect: 1 CP, applies next tick ---
    {
        BattleController bc(2, map, cards);
        bc.DeploySquad(tmpl, 0, 0);   // side 0 at region 0
        bc.DeploySquad(tmpl, 2, 1);   // side 1 far away
        bc.RequestBeat(BattleBeat::Execution);
        auto r = bc.IssueRedirect(0, 0, 1); // region 0 -> 1 (adjacent)
        Check(r.ok(), "redirect accepted");
        Check(bc.CpPool(0) == 2, "redirect costs 1 CP");
        Check(bc.Squads()[0].state == SquadState::Holding,
              "not applied until next tick");
        Check(bc.Tick(), "tick applies queued command");
        Check(bc.Squads()[0].state == SquadState::Moving &&
              bc.Squads()[0].edgeTarget == 1,
              "redirect resolves within 1 tick (<3s)");
        Check(CountEvents(bc, SimEvent::Kind::Intervention) == 1,
              "intervention event logged");
        const SimEvent* iev = nullptr;
        for (const auto& e : bc.Events())
            if (e.kind == SimEvent::Kind::Intervention) iev = &e;
        Check(iev != nullptr && iev->squadIndex == 0 && iev->param == 1 &&
              iev->aux == static_cast<int>(InterventionKind::Redirect),
              "intervention event payload (squad/target/kind)");
    }

    // --- Rejections: CP, phase, targets, duplicate commands ---
    {
        BattleController bc(3, map, cards);
        bc.DeploySquad(tmpl, 0, 0);  // sq0 side 0
        bc.DeploySquad(tmpl, 2, 1);  // sq1 side 1
        bc.DeploySquad(tmpl, 0, 0);  // sq2 side 0
        bc.DeploySquad(tmpl, 0, 0);  // sq3 side 0
        auto r0 = bc.IssueRedirect(0, 0, 1);
        Check(!r0.ok() && !r0.reason.empty() && bc.CpPool(0) == 3,
              "Planning rejects, no deduction, reason given");
        bc.RequestBeat(BattleBeat::Execution);
        Check(!bc.IssueRedirect(0, 0, 2).ok(), "non-adjacent rejected");
        Check(!bc.IssueRedirect(0, 0, 99).ok(), "OOB region rejected");
        Check(!bc.IssueRedirect(0, 9, 1).ok(), "OOB squad rejected");
        Check(!bc.IssueRedirect(0, 1, 1).ok(), "enemy squad rejected");
        Check(!bc.IssueRedirect(2, 0, 1).ok(), "bad side rejected");
        Check(bc.IssueRedirect(0, 0, 1).ok(), "redirect #1");   // pool 2
        Check(!bc.IssueRedirect(0, 0, 1).ok(),
              "second pending command on same squad rejected");
        Check(!bc.IssueRetreat(0, 0).ok(),
              "retreat on commanded squad also rejected");
        Check(bc.IssueRedirect(0, 2, 1).ok(),
              "redirect on another squad");                      // pool 1
        auto short_ = bc.IssueRetreat(0, 3); // costs 3, pool is 1
        Check(!short_.ok() && !short_.reason.empty(),
              "insufficient CP rejected with reason");
        Check(bc.CpPool(0) == 1, "pool untouched by rejected commands");
    }

    // --- Routing squads reject all commands (no CP burned) ---
    {
        SquadTemplate weak = tmpl;
        weak.cohesion = 10; // below rout threshold -> spawns Routing
        BattleController bc(7, map, cards);
        bc.DeploySquad(weak, 0, 0);
        bc.DeploySquad(tmpl, 2, 1);
        bc.SetSheet(0, SquadSheet::Build(cards,
            {"card_wait","card_hold","card_brace"}).value);
        bc.SetCpPool(0, 5);
        bc.RequestBeat(BattleBeat::Execution);
        Check(bc.Squads()[0].state == SquadState::Routing,
              "weak squad spawns routing");
        Check(!bc.IssueRedirect(0, 0, 1).ok() && bc.CpPool(0) == 5,
              "routing squad rejects redirect");
        Check(!bc.IssueOverride(0, 0, 0).ok() && bc.CpPool(0) == 5,
              "routing squad rejects override");
        Check(!bc.IssueRetreat(0, 0).ok() && bc.CpPool(0) == 5,
              "routing squad rejects retreat");
    }

    // --- Override: force-fire bypasses trigger + cooldown ---
    {
        BattleController bc(4, map, cards);
        bc.DeploySquad(tmpl, 0, 0);
        bc.DeploySquad(tmpl, 2, 1); // not adjacent — enemy triggers unmet
        bc.SetSheet(0, SquadSheet::Build(cards,
            {"card_hold","card_wait","card_brace"}).value);
        bc.SetCpPool(0, 5);
        bc.RequestBeat(BattleBeat::Execution);
        // card_hold trigger enemy_in_region unmet; override slot 0.
        auto r = bc.IssueOverride(0, 0, 0);
        Check(r.ok() && bc.CpPool(0) == 3, "override accepted, costs 2");
        bc.Tick();
        bool fired = false;
        for (const auto& e : bc.Events())
            if (e.kind == SimEvent::Kind::CardFired && e.slotIndex == 0 &&
                e.cardId == "card_hold") fired = true;
        Check(fired, "override fires card with unmet trigger");
        Check(!bc.IssueOverride(0, 0, 9).ok(), "OOB slot rejected");
    }

    // --- Override also fires through an active cooldown ---
    {
        BattleController bc(8, map, cards);
        bc.DeploySquad(tmpl, 0, 0);
        bc.DeploySquad(tmpl, 1, 1); // adjacent -> enemy_adjacent CAN fire
        bc.SetSheet(0, SquadSheet::Build(cards,
            {"card_brace","card_hold","card_wait"}).value);
        bc.SetCpPool(0, 5);
        bc.RequestBeat(BattleBeat::Execution);
        bc.Tick(); // card_brace fires on the trigger -> cooldown 5
        auto r = bc.IssueOverride(0, 0, 0);
        Check(r.ok() && bc.CpPool(0) == 3,
              "override during cooldown accepted");
        bc.Tick();
        int braceFires = 0;
        for (const auto& e : bc.Events())
            if (e.kind == SimEvent::Kind::CardFired &&
                e.cardId == "card_brace") ++braceFires;
        Check(braceFires == 2, "override fires through active cooldown");
    }

    // --- Retreat: 3 CP, squad routes off-field ---
    {
        BattleController bc(5, map, cards);
        bc.DeploySquad(tmpl, 0, 0);
        bc.DeploySquad(tmpl, 2, 1);
        bc.SetCpPool(0, 5);
        bc.RequestBeat(BattleBeat::Execution);
        auto r = bc.IssueRetreat(0, 0);
        Check(r.ok() && bc.CpPool(0) == 2, "retreat accepted, costs 3");
        bc.Tick();
        Check(bc.Squads()[0].state == SquadState::Routing,
              "retreat routes the squad next tick");
        for (int i = 0; i < ROUT_TICKS && bc.Beat() == BattleBeat::Execution;
             ++i) bc.Tick();
        Check(bc.Squads()[0].state == SquadState::Routed,
              "retreat completes off-field");
        Check(bc.Beat() == BattleBeat::Aftermath,
              "empty side closes battle");
    }

    // --- Per-side isolation: enemy spends its own pool ---
    {
        BattleController bc(6, map, cards);
        bc.DeploySquad(tmpl, 0, 0);
        bc.DeploySquad(tmpl, 1, 1);
        bc.RequestBeat(BattleBeat::Execution);
        Check(bc.IssueRetreat(1, 1).ok(), "enemy retreat accepted");
        Check(bc.CpPool(1) == 0 && bc.CpPool(0) == 3,
              "enemy spends own pool");
    }

    if (failures == 0) { std::puts("COMMAND TESTS PASS"); return 0; }
    std::printf("%d CHECK(S) FAILED\n", failures);
    return 1;
}
