#include "Gameplay/Squad/BudgetedBuild.h"
#include "Gameplay/Squad/Squad.h"
#include "Gameplay/Json/JsonValue.h"

#include <cstdio>
#include <fstream>
#include <string>

namespace {

int failures = 0;
void Check(bool cond, const char* name) {
    if (cond) {
        std::printf("[PASS] %s\n", name);
    } else {
        std::printf("[FAIL] %s\n", name);
        ++failures;
    }
}

using namespace Potato::Gameplay;

const char* SQUAD_DOC = R"json({
    "schema": "potato.squad/1",
    "squads": [
        {"id": "sq_vanguard", "name": "鋒矢營", "unit": "infantry",
         "hp": 100, "attack": 10, "speed": "medium", "cost": 40},
        {"id": "sq_riders", "unit": "cavalry",
         "hp": 80, "attack": 14, "speed": "fast", "cost": 60},
        {"id": "sq_watch", "unit": "scouts",
         "hp": 40, "attack": 4, "speed": "very_fast", "cohesion": 60, "cost": 30}
    ]
})json";

} // namespace

int main() {
    // --- Template library + instantiate ---
    auto doc = JsonValue::Parse(SQUAD_DOC);
    Check(doc.ok(), "fixture parses");
    auto lib = SquadTemplateLibrary::FromJson(doc.value);
    Check(lib.ok() && lib.value.Count() == 3, "potato.squad/1 library loads");

    const SquadTemplate* vg = lib.value.Find("sq_vanguard");
    Check(vg != nullptr && vg->name == "鋒矢營" && vg->unit == UnitType::Infantry &&
          vg->hp == 100 && vg->attack == 10 && vg->speed == Speed::Medium &&
          vg->cost == 40 && vg->cohesion == 100, "template fields decoded");
    Check(lib.value.Find("nope") == nullptr, "Find miss -> nullptr");

    Squad sq = Squad::Instantiate(*vg, 3);
    Check(sq.id == "sq_vanguard" && sq.name == "鋒矢營" && sq.hp == 100 &&
          sq.maxHp == 100 && sq.cohesion == 100 && sq.regionIndex == 3 &&
          sq.state == SquadState::Holding, "instantiate -> Holding at spawn");

    // --- Speed math (constexpr table) ---
    Check(TicksForEdge(SpeedMilli(Speed::Slow)) == 66, "slow = 66 ticks/edge");
    Check(TicksForEdge(SpeedMilli(Speed::Medium)) == 40, "medium = 40 ticks/edge");
    Check(TicksForEdge(SpeedMilli(Speed::Fast)) == 26, "fast = 26 ticks/edge");
    Check(TicksForEdge(SpeedMilli(Speed::VeryFast)) == 20, "very_fast = 20 ticks/edge");

    // --- Transition table spot-checks ---
    Check(CanTransition(SquadState::Holding, SquadEvent::MoveOrder), "table: Holding->Moving");
    Check(!CanTransition(SquadState::Moving, SquadEvent::MoveOrder), "table: no re-order mid-move");
    Check(CanTransition(SquadState::Moving, SquadEvent::Arrived), "table: Moving->Holding");
    Check(CanTransition(SquadState::Holding, SquadEvent::CohesionBreak) &&
          CanTransition(SquadState::Moving, SquadEvent::CohesionBreak), "table: rout from Holding|Moving");
    Check(!CanTransition(SquadState::Routing, SquadEvent::CohesionBreak), "table: no double-rout");
    Check(CanTransition(SquadState::Routing, SquadEvent::RetreatComplete), "table: Routing->Routed");
    Check(CanTransition(SquadState::Routing, SquadEvent::HpZero), "table: killed while routing");
    Check(!CanTransition(SquadState::Routed, SquadEvent::MoveOrder) &&
          !CanTransition(SquadState::Destroyed, SquadEvent::HpZero), "table: terminal states");

    // --- Movement timing (medium = 40 ticks/edge) ---
    Check(sq.IssueMove(4), "IssueMove from Holding");
    Check(!sq.IssueMove(5), "IssueMove rejected while Moving");
    for (int i = 0; i < 39; ++i) sq.TickMove();
    Check(sq.state == SquadState::Moving && sq.regionIndex == 3 &&
          sq.edgeProgress == 39, "still moving at tick 39");
    sq.TickMove();
    Check(sq.state == SquadState::Holding && sq.regionIndex == 4 &&
          sq.edgeTarget == ~std::size_t{0}, "arrived at tick 40");
    sq.TickMove();
    Check(sq.state == SquadState::Holding && sq.regionIndex == 4,
          "TickMove no-op while Holding");

    // --- Cohesion: rout boundary ---
    {
        Squad s2 = Squad::Instantiate(lib.value.At(1), 0);
        s2.ApplyHit(0, 80); // 100-80 = 20, NOT < 20
        Check(s2.cohesion == 20 && s2.state == SquadState::Holding,
              "cohesion == 20 holds (threshold is < 20)");
        s2.ApplyHit(0, 1); // 19 < 20
        Check(s2.cohesion == 19 && s2.state == SquadState::Routing,
              "cohesion 19 -> Routing");
    }

    // --- Rout cancels an in-flight move ---
    {
        Squad s3 = Squad::Instantiate(lib.value.At(1), 0);
        Check(s3.IssueMove(1), "move issued");
        for (int i = 0; i < 10; ++i) s3.TickMove();
        s3.ApplyHit(0, 90);
        Check(s3.state == SquadState::Routing && s3.regionIndex == 0 &&
              s3.edgeTarget == ~std::size_t{0} && s3.edgeProgress == 0,
              "rout cancels move, squad stays in region");
        s3.TickMove();
        Check(s3.state == SquadState::Routing, "routing squad does not auto-move");
    }

    // --- Destroyed precedence over rout ---
    {
        Squad s4 = Squad::Instantiate(lib.value.At(2), 0);
        s4.ApplyHit(40, 90); // hp 0 AND cohesion < 20
        Check(s4.hp == 0 && s4.state == SquadState::Destroyed,
              "HpZero beats CohesionBreak -> Destroyed");
        Check(!s4.IsEffective(), "Destroyed is not effective");
    }

    // --- Clamps + heal/restore ---
    {
        Squad s5 = Squad::Instantiate(*vg, 0);
        s5.ApplyHit(150, 0);
        Check(s5.hp == 0 && s5.state == SquadState::Destroyed, "hp clamps at 0");
        Squad s6 = Squad::Instantiate(*vg, 0);
        s6.ApplyHit(30, 30);
        s6.Heal(10);
        s6.RestoreCohesion(500);
        Check(s6.hp == 80 && s6.cohesion == 100, "heal/restore clamped");
    }

    // --- Review-hardening edges ---
    {
        Squad s7 = Squad::Instantiate(*vg, 0);
        Check(!s7.IssueMove(0), "IssueMove to self rejected");
        Check(!s7.IssueMove(Squad::NO_REGION), "IssueMove to NO_REGION rejected");
        Check(s7.IssueMove(1), "IssueMove to valid target ok");

        // ApplyHit negative args are caller bugs — clamped, not heals.
        s7.ApplyHit(-50, -50);
        Check(s7.hp == 100 && s7.cohesion == 100 && s7.state == SquadState::Moving,
              "negative losses clamped to 0 (no overheal)");

        // speedMilli corruption cannot crash or teleport.
        s7.speedMilli = 0;
        s7.TickMove();
        Check(s7.state == SquadState::Moving && s7.edgeProgress == 1,
              "speedMilli=0 falls back to base ticks, no crash");
        s7.speedMilli = 1000;
    }
    {
        Squad s8 = Squad::Instantiate(*vg, 0);
        s8.ApplyHit(0, 90); // -> Routing
        s8.RestoreCohesion(100);
        Check(s8.cohesion == 100 && s8.state == SquadState::Routing,
              "rout is sticky: restored cohesion does not un-rout");
        Check(s8.ApplyEvent(SquadEvent::RetreatComplete), "RetreatComplete via ApplyEvent");
        Check(s8.state == SquadState::Routed, "Routing -> Routed");
        s8.ApplyHit(50, 0);
        Check(s8.hp == 100, "ApplyHit no-op on Routed");
        s8.Heal(10);
        s8.RestoreCohesion(10);
        Check(s8.hp == 100 && s8.cohesion == 100, "heal/restore no-op on Routed");
        Check(!s8.IssueMove(1), "IssueMove rejected on Routed");
    }
    {
        Squad s9 = Squad::Instantiate(*vg, 0);
        s9.ApplyHit(100, 0); // -> Destroyed
        Check(!s9.ApplyEvent(SquadEvent::MoveOrder), "ApplyEvent gated on terminal");
        s9.Heal(50);
        Check(s9.hp == 0, "Heal no-op on Destroyed (no zombie)");
    }
    {
        // Hand-built (non-loader) templates get clamped at instantiate.
        SquadTemplate rough;
        rough.id = "rough"; rough.unit = UnitType::Militia;
        rough.hp = 0; rough.attack = -5; rough.cohesion = -10; rough.cost = -1;
        rough.speed = Speed::Slow;
        Squad s10 = Squad::Instantiate(rough, 0);
        Check(s10.maxHp == 1 && s10.hp == 1 && s10.attack == 0 &&
              s10.cohesion == 0 && s10.state == SquadState::Routing,
              "Instantiate clamps hand-built template + broken spawn routs");
    }

    // --- Validation rejects ---
    auto reject = [](const char* json, const char* name) {
        auto d = JsonValue::Parse(json);
        if (!d.ok()) { Check(false, name); return; }
        auto m = SquadTemplateLibrary::FromJson(d.value);
        Check(!m.ok() && !m.error.empty() && !m.reason.empty(), name);
    };
    reject("[]", "non-object root rejected");
    reject(R"({"squads":[]})", "empty squads rejected");
    reject(R"({"squads":[{"id":"a","unit":"infantry","hp":10,"speed":"medium"},
                          {"id":"a","unit":"cavalry","hp":10,"speed":"fast"}]})",
           "duplicate squad id rejected");
    reject(R"({"squads":[{"id":"a","unit":"dragon","hp":10,"speed":"fast"}]})",
           "unknown unit token rejected");
    reject(R"({"squads":[{"id":"a","unit":"infantry","hp":10,"speed":"warp"}]})",
           "unknown speed token rejected");
    reject(R"({"squads":[{"id":"a","unit":"infantry","hp":0,"speed":"fast"}]})",
           "hp < 1 rejected");
    reject(R"({"squads":[{"id":"a","unit":"infantry","speed":"fast"}]})",
           "missing hp rejected");
    reject(R"({"squads":[{"id":"a","hp":10,"speed":"fast"}]})",
           "missing unit rejected");
    reject(R"({"squads":[{"id":"a","unit":"infantry","hp":10}]})",
           "missing speed rejected");
    reject(R"({"squads":[{"id":"a","unit":"infantry","hp":10,"speed":"fast","cohesion":150}]})",
           "cohesion > 100 rejected");
    reject(R"({"schema":"potato.squad/2","squads":[{"id":"a","unit":"infantry","hp":10,"speed":"fast"}]})",
           "schema tag re-checked in FromJson");

    // --- File path via Load ---
    {
        const char* tmp = "potato_test_squad.fixture.tmp.json";
        {
            std::ofstream out(tmp, std::ios::binary);
            Check(out.good(), "fixture file opens");
            out << SQUAD_DOC;
        }
        auto loaded = SquadTemplateLibrary::Load(tmp);
        Check(loaded.ok() && loaded.value.Count() == 3, "Load(path) with schema gate");
        std::remove(tmp);
    }

    // --- Story 9.2: budgeted builds — affordable items
    //     instantiate, every refusal records a reason ---
    {
        // Fixture costs: vanguard 40, riders 60, watch 30.
        auto exact = BuildSquads(lib.value,
                                 {"sq_vanguard", "sq_riders"},
                                 100, 3, 1);
        Check(exact.squads.size() == 2 && exact.skips.empty() &&
                  exact.spent == 100,
              "9.2: exact-budget build instantiates all");

        auto greedy = BuildSquads(
            lib.value,
            {"sq_riders", "sq_vanguard", "sq_watch"}, 90, 0, 0);
        Check(greedy.squads.size() == 2 &&
                  greedy.squads[0].id == "sq_riders" &&
                  greedy.squads[1].id == "sq_watch",
              "9.2: later cheap item fits what remains");
        Check(greedy.skips.size() == 1 &&
                  greedy.skips[0].id == "sq_vanguard" &&
                  greedy.skips[0].reason ==
                      BuildSkipReason::OverBudget,
              "9.2: over-budget item skipped with reason");
        Check(greedy.spent == 90, "9.2: spent accounts exactly");

        auto mix = BuildSquads(
            lib.value,
            {"sq_ghost", "sq_watch", "sq_vanguard"}, 50, 0, 0);
        Check(mix.squads.size() == 1 && mix.skips.size() == 2,
              "9.2: no silent drops — id lands squad or skip");
        Check(mix.skips[0].reason == BuildSkipReason::UnknownId &&
                  mix.skips[1].reason ==
                      BuildSkipReason::OverBudget,
              "9.2: unknown vs over-budget reasons distinct");
        Check(std::string(BuildSkipReasonName(
                  BuildSkipReason::UnknownId)) == "unknown_id" &&
                  std::string(BuildSkipReasonName(
                      BuildSkipReason::OverBudget)) ==
                      "over_budget",
              "9.2: skip reason wire ids pinned");

        auto dup = BuildSquads(lib.value,
                               {"sq_watch", "sq_watch"}, 60, 2,
                               0);
        Check(dup.squads.size() == 2 &&
                  dup.squads[0].id == "sq_watch" &&
                  dup.squads[1].id == "sq_watch#2",
              "9.2: duplicate template ids disambiguated");

        auto broke = BuildSquads(lib.value, {"sq_watch"}, -5, 0,
                                 0);
        Check(broke.squads.empty() && broke.skips.size() == 1 &&
                  broke.spent == 0,
              "9.2: negative budget buys nothing, still logs");

        Check(mix.squads[0].side == 0 &&
                  exact.squads[0].side == 1 &&
                  dup.squads[0].regionIndex == 2,
              "9.2: side + region ride into instantiated squads");
    }

    if (failures == 0) {
        std::puts("SQUAD TESTS PASS");
        return 0;
    }
    std::printf("%d CHECK(S) FAILED\n", failures);
    return 1;
}
