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
SquadSheet Sheet3(const DoctrineLibrary& c) {
    return SquadSheet::Build(c, {"card_brace","card_flee","card_hold"}).value;
}

} // namespace

int main() {
    BattleMap map = LoadMap();
    DoctrineLibrary cards = LoadCards();
    SquadTemplate tmpl = MakeTemplate();
    Check(map.RegionCount() == 3 && cards.Count() == 3, "fixtures load");

    // --- Transition table ---
    {
        Check(CanTransition(BattleBeat::Planning, BattleBeat::Execution),
              "P->E allowed");
        Check(CanTransition(BattleBeat::Execution, BattleBeat::Aftermath),
              "E->A allowed");
        Check(!CanTransition(BattleBeat::Planning, BattleBeat::Aftermath),
              "P->A rejected (illegal)");
        Check(!CanTransition(BattleBeat::Aftermath, BattleBeat::Planning) &&
              !CanTransition(BattleBeat::Aftermath, BattleBeat::Execution),
              "Aftermath is terminal");
        Check(!CanTransition(BattleBeat::Execution, BattleBeat::Planning) &&
              !CanTransition(BattleBeat::Planning, BattleBeat::Planning),
              "backwards/self transitions rejected");
    }

    // --- Planning accepts input, never ticks ---
    {
        BattleController bc(42, map, cards);
        Check(bc.Beat() == BattleBeat::Planning, "starts in Planning");
        Check(!bc.Tick(), "Tick no-op in Planning");
        Check(bc.GetSim().TickCount() == 0, "no ticks elapsed in Planning");
        Check(bc.DeploySquad(tmpl, 0, 0) && bc.DeploySquad(tmpl, 1, 1),
              "deploy accepted in Planning");
        Check(!bc.DeploySquad(tmpl, 99, 0), "OOB region deploy rejected");
        Check(!bc.DeploySquad(tmpl, Squad::NO_REGION, 0),
              "NO_REGION deploy rejected");
        Check(!bc.DeploySquad(tmpl, 0, 2), "side outside {0,1} rejected");
        Check(bc.Squads().size() == 2, "squads deployed");
        Check(bc.SetSheet(0, Sheet3(cards)), "SetSheet accepted in Planning");
        Check(!bc.SetSheet(9, Sheet3(cards)), "SetSheet OOB rejected");
        Check(!bc.Tick(), "Planning still never ticks after input");
        Check(!bc.RequestBeat(BattleBeat::Aftermath),
              "P->A transition rejected");
        Check(bc.Beat() == BattleBeat::Planning, "still Planning after reject");
        Check(bc.RequestBeat(BattleBeat::Execution), "P->E accepted");
        Check(bc.Beat() == BattleBeat::Execution, "now in Execution");
        // boundary event recorded
        bool beatEvt = false;
        for (const auto& e : bc.Events())
            if (e.kind == SimEvent::Kind::BeatChanged &&
                e.aux == static_cast<int>(BattleBeat::Execution)) beatEvt = true;
        Check(beatEvt, "BeatChanged logged");
        Check(!bc.DeploySquad(tmpl, 2, 0), "deploy rejected in Execution");
        Check(!bc.SetSheet(0, Sheet3(cards)), "SetSheet rejected in Execution");
    }

    // --- Execution ticks deterministically; auto-Aftermath on wipe ---
    {
        BattleController bc(7, map, cards);
        bc.DeploySquad(tmpl, 0, 0);
        bc.SetSheet(0, Sheet3(cards));
        bc.RequestBeat(BattleBeat::Execution);
        // side 1 has NO squads -> first tick auto-closes the battle
        Check(bc.Tick(), "tick runs in Execution");
        Check(bc.Beat() == BattleBeat::Aftermath,
              "side wipe auto-closes to Aftermath");
        Check(bc.Outcome().winnerSide == 0, "surviving side wins");
        Check(bc.Outcome().elapsedTicks == 1, "outcome records elapsed ticks");
        Check(bc.Outcome().effective[0] == 1 &&
              bc.Outcome().destroyed[0] == 0, "outcome side counts");
        Check(!bc.Tick(), "Aftermath ticks no-op");
        Check(!bc.RequestBeat(BattleBeat::Planning), "A->P rejected");
        Check(bc.GetSim().TickCount() == 1, "tick count frozen in Aftermath");
    }

    // --- Full determinism: seed+sheets -> identical checksum + log ---
    {
        auto runBattle = [&](std::uint64_t seed) {
            BattleController bc(seed, map, cards);
            bc.DeploySquad(tmpl, 0, 0);
            bc.DeploySquad(tmpl, 1, 1);
            bc.SetSheet(0, Sheet3(cards));
            bc.SetSheet(1, Sheet3(cards));
            bc.RequestBeat(BattleBeat::Execution);
            for (int i = 0; i < 200 && bc.Beat() == BattleBeat::Execution; ++i)
                bc.Tick();
            return bc;
        };
        auto a = runBattle(1234), b = runBattle(1234), c = runBattle(9999);
        Check(a.Checksum() == b.Checksum(), "same seed -> same checksum");
        Check(a.Checksum() != c.Checksum(), "diff seed -> diff checksum");
        // different sheets must diverge the checksum (cooldowns folded)
        BattleController d(1234, map, cards);
        d.DeploySquad(tmpl, 0, 0);
        d.DeploySquad(tmpl, 1, 1); // no sheets set — empty doctrine
        d.RequestBeat(BattleBeat::Execution);
        for (int i = 0; i < 200 && d.Beat() == BattleBeat::Execution; ++i)
            d.Tick();
        Check(a.Checksum() != d.Checksum(), "checksum covers sheet state");
        Check(a.Outcome().winnerSide == b.Outcome().winnerSide &&
              a.Outcome().elapsedTicks == b.Outcome().elapsedTicks,
              "same seed -> identical outcome");
        bool sameLog = a.Events().size() == b.Events().size();
        for (std::size_t i = 0; sameLog && i < a.Events().size(); ++i) {
            const auto& x = a.Events()[i]; const auto& y = b.Events()[i];
            sameLog = x.kind == y.kind && x.tick == y.tick &&
                      x.squadIndex == y.squadIndex &&
                      x.slotIndex == y.slotIndex && x.cardId == y.cardId &&
                      x.aux == y.aux && x.param == y.param;
        }
        Check(sameLog, "same seed -> identical event log");
        // doctrine actually executed through the controller
        bool cardFired = false;
        for (const auto& e : a.Events())
            if (e.kind == SimEvent::Kind::CardFired) cardFired = true;
        Check(cardFired, "cards fired through controller tick");
    }

    // --- cpPool feeds doctrine conditions ---
    {
        BattleController bc(3, map, cards);
        bc.DeploySquad(tmpl, 0, 0);
        bc.DeploySquad(tmpl, 1, 1);
        bc.SetSheet(0, SquadSheet::Build(cards,
            {"card_hold","card_hold","card_hold"}).value);
        bc.SetCpPool(0, 0);
        bc.RequestBeat(BattleBeat::Execution);
        Check(bc.CpPool(0) == 0, "cpPool settable in Planning");
        Check(bc.Tick(), "tick with cp 0");
    }

    // --- Manual Aftermath (concede) vs wipe draw ---
    {
        BattleController bc(5, map, cards);
        bc.DeploySquad(tmpl, 0, 0);
        bc.DeploySquad(tmpl, 1, 1);
        bc.RequestBeat(BattleBeat::Execution);
        bc.Tick();
        Check(bc.RequestBeat(BattleBeat::Aftermath), "manual E->A allowed");
        Check(bc.Outcome().forced && bc.Outcome().winnerSide == -1 &&
              bc.Outcome().effective[0] == 1 && bc.Outcome().effective[1] == 1,
              "forced close flagged, both sides intact");
    }

    // --- Routing squads rout off-field; battle still closes ---
    {
        SquadTemplate weak = tmpl;
        weak.cohesion = 10; // < ROUT_THRESHOLD -> spawns Routing
        BattleController bc(6, map, cards);
        bc.DeploySquad(tmpl, 0, 0);
        bc.DeploySquad(weak, 1, 1);
        bc.RequestBeat(BattleBeat::Execution);
        int ticks = 0;
        while (bc.Beat() == BattleBeat::Execution && ticks < 200) {
            bc.Tick(); ++ticks;
        }
        Check(bc.Beat() == BattleBeat::Aftermath,
              "routing enemy completes rout and closes battle");
        Check(bc.Squads()[1].state == SquadState::Routed,
              "routing squad became Routed");
        Check(bc.Outcome().routed[1] == 1 && bc.Outcome().winnerSide == 0,
              "outcome counts the routed enemy");
        Check(ticks == ROUT_TICKS, "rout completed after ROUT_TICKS");
    }

    if (failures == 0) { std::puts("BATTLE TESTS PASS"); return 0; }
    std::printf("%d CHECK(S) FAILED\n", failures);
    return 1;
}
