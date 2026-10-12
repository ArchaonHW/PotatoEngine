#include "Gameplay/Eval/WinEval.h"
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

SquadTemplate Mk(const char* id, int hp) {
    SquadTemplate t; t.id = id; t.name = id; t.hp = hp;
    t.attack = 10; t.speed = Speed::VeryFast; t.cohesion = 100;
    return t;
}

struct Fixture {
    BattleMap map;
    DoctrineLibrary cards;
    Fixture() {
        map = BattleMap::FromJson(JsonValue::Parse(MAP_DOC).value).value;
        cards = DoctrineLibrary::FromJson(JsonValue::Parse(CARDS_DOC).value)
                    .value;
    }
};

const LedgerEvent* FindType(const BattleResult& r, LedgerEvent::Type t) {
    for (const LedgerEvent& le : r.ledger) {
        if (le.type == t) return &le;
    }
    return nullptr;
}

const SimEvent* FindResultDeclared(const BattleController& bc) {
    for (const SimEvent& e : bc.Events()) {
        if (e.kind == SimEvent::Kind::ResultDeclared) return &e;
    }
    return nullptr;
}

} // namespace

int main() {
    Fixture fx;

    // --- AC1: wipe -> BattleResult with victor, casualties, ledger ---
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("alpha", 200), 0, 0);
        // side 1 fields nothing -> first tick wipes it
        bc.RequestBeat(BattleBeat::Execution);
        bc.Tick();
        Check(bc.Beat() == BattleBeat::Aftermath, "wipe closes battle");
        const BattleResult& r = bc.Outcome();
        Check(r.winnerSide == 0, "wipe: survivor wins");
        Check(r.closeReason == CloseReason::Wipe &&
                  !r.forced && !r.stalemate, "wipe close reason");
        Check(r.destroyed[0] == 0 && r.effective[0] == 1,
              "wipe: no casualties on winner");
        // Ledger: verdict entry last; winner has no routs/casualties.
        Check(!r.ledger.empty() &&
                  r.ledger.back().type == LedgerEvent::Type::Victory &&
                  r.ledger.back().side == 0,
              "ledger ends with Victory for winner");
        const SimEvent* ev = FindResultDeclared(bc);
        Check(ev != nullptr && ev->param == 0 &&
                  ev->aux == static_cast<int>(CloseReason::Wipe),
              "ResultDeclared stamped (wipe)");
    }

    // --- Casualty + Rout entries with squad ids ---
    {
        BattleController bc(2, fx.map, fx.cards);
        SquadTemplate a = Mk("alpha", 200);
        SquadTemplate b = Mk("bravo", 200);
        bc.DeploySquad(a, 0, 0);
        bc.DeploySquad(b, 3, 1);
        bc.RequestBeat(BattleBeat::Execution);
        // Retire the enemy via the cheapest deterministic path —
        // IssueRetreat + the rout timer. (Destroyed/Casualty postings
        // stay unexercisable until a damage action exists — deferred.)
        bc.IssueRetreat(1, 1);
        for (int i = 0; i < ROUT_TICKS + 2 && bc.Tick(); ++i) {}
        Check(bc.Beat() == BattleBeat::Aftermath, "rout closes battle");
        const BattleResult& r = bc.Outcome();
        Check(r.winnerSide == 0 && r.routed[1] == 1, "routed side loses");
        bool sawRout = false;
        for (const LedgerEvent& le : r.ledger) {
            if (le.type == LedgerEvent::Type::Rout && le.side == 1 &&
                le.squadId == "bravo") sawRout = true;
        }
        Check(sawRout, "ledger carries Rout posting with squad id");
        Check(!r.ledger.empty() &&
                  r.ledger.back().type == LedgerEvent::Type::Victory,
              "verdict entry last");
    }

    // --- Concede: forced close, no victor, Draw verdict ---
    {
        BattleController bc(3, fx.map, fx.cards);
        bc.DeploySquad(Mk("alpha", 200), 0, 0);
        bc.DeploySquad(Mk("bravo", 200), 3, 1);
        bc.RequestBeat(BattleBeat::Execution);
        for (int i = 0; i < 5; ++i) bc.Tick();
        bc.RequestBeat(BattleBeat::Aftermath);
        const BattleResult& r = bc.Outcome();
        Check(r.forced && !r.stalemate &&
                  r.closeReason == CloseReason::Concede,
              "concede close reason");
        Check(r.winnerSide == -1, "concede: no victor");
        Check(!r.ledger.empty() &&
                  r.ledger.back().type == LedgerEvent::Type::Draw,
              "concede books a Draw");
        const SimEvent* ev = FindResultDeclared(bc);
        Check(ev != nullptr && ev->aux ==
                  static_cast<int>(CloseReason::Concede),
              "ResultDeclared stamped (concede)");
    }

    // --- Concede reports the field truthfully ---
    // Conceding into an already-empty enemy side still names a victor.
    {
        BattleController bc(41, fx.map, fx.cards);
        bc.DeploySquad(Mk("alpha", 200), 0, 0); // side 1 fields nothing
        bc.RequestBeat(BattleBeat::Execution);
        bc.RequestBeat(BattleBeat::Aftermath); // concede before any tick
        const BattleResult& r = bc.Outcome();
        Check(r.closeReason == CloseReason::Concede && r.forced,
              "pre-tick concede is forced");
        Check(r.winnerSide == 0,
              "concede reports the standing side as victor");
    }
    // A squad mid-Routing at close counts as effective and posts no
    // ledger entry — it is still on the field when the bell rings.
    {
        BattleController bc(42, fx.map, fx.cards);
        bc.DeploySquad(Mk("alpha", 200), 0, 0);
        bc.DeploySquad(Mk("bravo", 200), 3, 1);
        bc.RequestBeat(BattleBeat::Execution);
        bc.IssueRetreat(1, 1);
        bc.Tick(); // bravo is Routing, not yet Routed
        bc.RequestBeat(BattleBeat::Aftermath);
        const BattleResult& r = bc.Outcome();
        Check(r.effective[1] == 1 && r.routed[1] == 0,
              "mid-rout squad counts effective at close");
        Check(FindType(r, LedgerEvent::Type::Rout) == nullptr,
              "mid-rout squad posts no Rout entry");
    }

    // --- Mutual wipe: both sides empty on the same tick -> Wipe+Draw ---
    {
        BattleController bc(33, fx.map, fx.cards); // no deploys at all
        bc.RequestBeat(BattleBeat::Execution);
        bc.Tick();
        Check(bc.Beat() == BattleBeat::Aftermath, "mutual wipe closes");
        const BattleResult& r = bc.Outcome();
        Check(r.closeReason == CloseReason::Wipe && !r.forced &&
                  !r.stalemate && r.winnerSide == -1,
              "mutual wipe -> Wipe + draw");
        Check(r.ledger.size() == 1 &&
                  r.ledger.back().type == LedgerEvent::Type::Draw,
              "mutual wipe books only the Draw");
    }

    // --- Boundary ordering: wipe beats the stalemate timer ---
    {
        EvalConfig cfg;
        cfg.stalemateTicks = 1; // fires on the very first tick
        BattleController bc(34, fx.map, fx.cards, FogConfig{},
                            PlanConfig{}, cfg);
        // No deploys: side-wipe AND stalemate are both true at tick 0 —
        // the wipe check must win (a real resolution beats the timer).
        bc.RequestBeat(BattleBeat::Execution);
        bc.Tick();
        Check(bc.Outcome().closeReason == CloseReason::Wipe,
              "wipe wins over stalemate on the boundary tick");
    }

    // --- AC2: stalemate timer -> draw evaluation ---
    {
        EvalConfig cfg;
        cfg.stalemateTicks = 10; // half a second
        BattleController bc(4, fx.map, fx.cards, FogConfig{},
                            PlanConfig{}, cfg);
        bc.DeploySquad(Mk("alpha", 200), 0, 0);
        bc.DeploySquad(Mk("bravo", 200), 3, 1);
        bc.RequestBeat(BattleBeat::Execution);
        int ticks = 0;
        while (bc.Tick() && ticks < 100) ++ticks;
        Check(ticks == 10, "stalemate fires at the configured tick");
        const BattleResult& r = bc.Outcome();
        Check(r.stalemate && !r.forced &&
                  r.closeReason == CloseReason::Stalemate,
              "stalemate close reason");
        Check(r.winnerSide == -1, "stalemate evaluates to a draw");
        Check(r.effective[0] == 1 && r.effective[1] == 1,
              "stalemate: both sides standing");
        Check(!r.ledger.empty() &&
                  r.ledger.back().type == LedgerEvent::Type::Draw,
              "stalemate books a Draw");
        const SimEvent* ev = FindResultDeclared(bc);
        Check(ev != nullptr && ev->tick == 9 && ev->param == -1 &&
                  ev->aux == static_cast<int>(CloseReason::Stalemate),
              "ResultDeclared stamped with the producing tick");
        // The journal's last two events are BeatChanged->ResultDeclared.
        const auto& evs = bc.Events();
        Check(evs.size() >= 2 &&
                  evs[evs.size() - 2].kind == SimEvent::Kind::BeatChanged &&
                  evs.back().kind == SimEvent::Kind::ResultDeclared,
              "close path emits BeatChanged then ResultDeclared");
    }

    // --- stalemateTicks = 0 disables the timer ---
    {
        EvalConfig cfg;
        cfg.stalemateTicks = 0;
        BattleController bc(5, fx.map, fx.cards, FogConfig{},
                            PlanConfig{}, cfg);
        bc.DeploySquad(Mk("alpha", 200), 0, 0);
        bc.DeploySquad(Mk("bravo", 200), 3, 1);
        bc.RequestBeat(BattleBeat::Execution);
        for (int i = 0; i < 50 && bc.Beat() == BattleBeat::Execution; ++i)
            bc.Tick();
        Check(bc.Beat() == BattleBeat::Execution,
              "stalemate=0 disables the timer");
    }

    // --- EvalConfig::FromJson ---
    {
        EvalConfig d;
        Check(d.stalemateTicks == 6 * 60 * 20,
              "EvalConfig default = 6 min");
        auto parsed = JsonValue::Parse(
            R"({"schema":"potato.balance/1","eval":{"stalemate_ticks":120}})");
        auto c = EvalConfig::FromJson(parsed.value);
        Check(c.ok() && c.value.stalemateTicks == 120,
              "eval.stalemate_ticks parses");
        auto bad = EvalConfig::FromJson(
            JsonValue::Parse(R"({"eval":{"stalemate_ticks":-5}})").value);
        Check(!bad.ok(), "negative stalemate_ticks rejected");
        auto miss = EvalConfig::FromJson(
            JsonValue::Parse(R"({"schema":"potato.balance/1"})").value);
        Check(miss.ok() && miss.value.stalemateTicks == d.stalemateTicks,
              "missing eval section -> defaults");
    }

    // --- Determinism: identical battles identical results ---
    {
        const auto run = [&fx](std::uint64_t seed) {
            BattleController bc(seed, fx.map, fx.cards);
            bc.DeploySquad(Mk("alpha", 200), 0, 0);
            bc.DeploySquad(Mk("bravo", 200), 3, 1);
            bc.RequestBeat(BattleBeat::Execution);
            for (int i = 0; i < 30 && bc.Tick(); ++i) {}
            return bc.Checksum();
        };
        Check(run(9) == run(9), "same seed -> identical checksum");
        Check(run(9) != run(10), "different seed may diverge");
    }

    std::printf(failures ? "EVAL TESTS FAILED: %d\n" : "EVAL TESTS PASS\n",
                failures);
    return failures ? 1 : 0;
}
