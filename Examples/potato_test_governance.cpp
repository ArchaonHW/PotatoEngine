#include "Gameplay/Governance/GovernanceField.h"
#include "Gameplay/Doctrine/Doctrine.h"
#include "Gameplay/Map/BattleMap.h"
#include "Gameplay/Record/BattleRecorder.h" // EventToJson/FromJson
#include "Gameplay/Sim/BattleController.h"
#include "Gameplay/Squad/Squad.h"
#include "Gameplay/Json/JsonValue.h"

#include <cstdio>
#include <vector>

namespace {

int failures = 0;
void Check(bool cond, const char* name) {
    std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", name);
    if (!cond) ++failures;
}

using namespace Potato::Gameplay;

// Map: r0 - r1(VILLAGE) - r2 - r3 ; branch r1 - r4.
// Only r1 is a village; r4 is a dead end (off-route parking).
const char* MAP_DOC = R"json({
    "schema": "potato.map/1", "id": "g", "name": "Gov",
    "regions": [{"id": "r0"}, {"id": "r1", "strategic": ["village"]},
                {"id": "r2"}, {"id": "r3"}, {"id": "r4"}],
    "edges": [["r0", "r1"], ["r1", "r2"], ["r2", "r3"], ["r1", "r4"]]
})json";

const char* CARDS_DOC = R"json({
    "schema": "potato.doctrine_cards/1",
    "cards": [
        {"id": "hold_a", "cooldown": 5,
         "trigger": {"type": "always"}, "action": {"type": "hold"}},
        {"id": "hold_b", "cooldown": 5,
         "trigger": {"type": "always"}, "action": {"type": "hold"}},
        {"id": "hold_c", "cooldown": 5,
         "trigger": {"type": "always"}, "action": {"type": "hold"}},
        {"id": "scorch", "cooldown": 5,
         "trigger": {"type": "always"}, "action": {"type": "burn"}},
        {"id": "advance2", "cooldown": 5,
         "trigger": {"type": "always"},
         "action": {"type": "move", "param": 2}}
    ]
})json";

SquadTemplate Mk(const char* id) {
    SquadTemplate t; t.id = id; t.name = id; t.hp = 200;
    t.attack = 10; t.speed = Speed::VeryFast; t.cohesion = 100;
    return t;
}

struct Fx {
    BattleMap map;
    DoctrineLibrary cards;
    Fx() {
        map = BattleMap::FromJson(JsonValue::Parse(MAP_DOC).value).value;
        cards = DoctrineLibrary::FromJson(JsonValue::Parse(CARDS_DOC).value)
                    .value;
    }
};

int CountKind(const std::vector<SimEvent>& evs, SimEvent::Kind k) {
    int n = 0;
    for (const SimEvent& e : evs) if (e.kind == k) ++n;
    return n;
}

const SimEvent* FindKind(const std::vector<SimEvent>& evs,
                        SimEvent::Kind k, int nth = 0) {
    for (const SimEvent& e : evs) {
        if (e.kind == k && nth-- == 0) return &e;
    }
    return nullptr;
}

void Ticks(BattleController& bc, int n) {
    for (int i = 0; i < n && bc.Tick(); ++i) {}
}

// Both sides field one squad (auto-wipe guard) parked off the
// contested ground unless the caller repositions them.
void Parked(BattleController& bc, std::size_t s0 = 4,
            std::size_t s1 = 3) {
    bc.DeploySquad(Mk("ally"), s0, 0);
    bc.DeploySquad(Mk("foe"), s1, 1);
}

} // namespace

int main() {
    Fx fx;

    // --- Occupation: exclusive presence dwells, then latches ---
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("occ"), 1, 0);  // squad 0 on the village
        bc.DeploySquad(Mk("foe"), 3, 1); // parked far
        bc.RequestBeat(BattleBeat::Execution);
        Ticks(bc, OCCUPY_TICKS);
        const SimEvent* ev = FindKind(bc.Events(),
                                      SimEvent::Kind::VillageOccupied);
        Check(ev != nullptr, "village occupied event emitted");
        Check(ev && ev->param == 1 && ev->side == 0 &&
                  ev->squadIndex == 0,
              "occupation carries region + perpetrator side + witness");
        Check(ev && ev->tick == OCCUPY_TICKS - 1,
              "occupation fires exactly at the dwell threshold");
        Ticks(bc, OCCUPY_TICKS);
        Check(CountKind(bc.Events(), SimEvent::Kind::VillageOccupied) == 1,
              "latched occupation does not re-emit");
    }

    // --- Dwell requires exclusivity; recapture re-arms ---
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("occ"), 1, 0);
        bc.DeploySquad(Mk("foe"), 0, 1); // adjacent, can walk in
        bc.RequestBeat(BattleBeat::Execution);
        Ticks(bc, OCCUPY_TICKS);
        Check(CountKind(bc.Events(), SimEvent::Kind::VillageOccupied) == 1,
              "occupation latched before contest");
        // Enemy walks in (adjacent r0 -> r1): latch releases while
        // the region is contested.
        auto r = bc.IssueRedirect(1, 1, 1);
        Check(r.ok() && r.value, "contestor redirect issued");
        Ticks(bc, 20 + OCCUPY_TICKS); // VeryFast edge = 20 ticks
        Check(CountKind(bc.Events(), SimEvent::Kind::VillageOccupied) == 1,
              "contested village emits nothing");
        // Enemy walks back out: side 0 re-dwells and re-captures.
        r = bc.IssueRedirect(1, 1, 0);
        Check(r.ok() && r.value, "contestor withdraw issued");
        Ticks(bc, 20 + OCCUPY_TICKS + 2);
        Check(CountKind(bc.Events(), SimEvent::Kind::VillageOccupied) == 2,
              "recapture re-dwells and re-emits");
    }

    // --- Burning: authored doctrine delta -> VillageBurned ---
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("torch"), 1, 0);
        bc.DeploySquad(Mk("foe"), 3, 1);
        auto sheet = SquadSheet::Build(
            fx.cards, {"scorch", "hold_a", "hold_b"});
        Check(sheet.ok(), "burn sheet builds");
        bc.SetSheet(0, sheet.value);
        bc.RequestBeat(BattleBeat::Execution);
        Ticks(bc, 1);
        const SimEvent* ev = FindKind(bc.Events(),
                                      SimEvent::Kind::VillageBurned);
        Check(ev != nullptr, "village burned event emitted");
        Check(ev && ev->param == 1 && ev->side == 0 &&
                  ev->squadIndex == 0,
              "burn carries region + perpetrator");
        Check(ev && ev->tick == 0, "burn applies at delta-commit tick");
        // Ash holds nothing: no occupation, and the card's second
        // fire (cooldown 5 s = tick 100) can't re-burn.
        Ticks(bc, 2 * OCCUPY_TICKS + 40);
        Check(CountKind(bc.Events(), SimEvent::Kind::VillageOccupied) == 0,
              "burned village never occupies");
        Check(CountKind(bc.Events(), SimEvent::Kind::VillageBurned) == 1,
              "village cannot burn twice");
    }

    // --- Burn card on non-village ground is a silent no-op ---
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("torch"), 0, 0); // plain region
        bc.DeploySquad(Mk("foe"), 3, 1);
        bc.SetSheet(0, SquadSheet::Build(
            fx.cards, {"scorch", "hold_a", "hold_b"}).value);
        bc.RequestBeat(BattleBeat::Execution);
        Ticks(bc, 5);
        Check(CountKind(bc.Events(), SimEvent::Kind::VillageBurned) == 0,
              "burn card off-village emits nothing");
    }

    // --- A squad that already walked can't burn ---
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("torch"), 1, 0);
        bc.DeploySquad(Mk("foe"), 3, 1);
        bc.SetSheet(0, SquadSheet::Build(
            fx.cards, {"scorch", "hold_a", "hold_b"}).value);
        bc.RequestBeat(BattleBeat::Execution);
        auto r = bc.IssueRedirect(0, 0, 2); // applies at tick start
        Check(r.ok() && r.value, "redirect issued");
        Ticks(bc, 3);
        Check(CountKind(bc.Events(), SimEvent::Kind::VillageBurned) == 0,
              "moving squad's burn delta rejected at apply");
    }

    // --- Contested burn still burns (perpetrator recorded) ---
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("torch"), 1, 0);
        bc.DeploySquad(Mk("foe"), 1, 1); // enemy shares the village
        bc.SetSheet(0, SquadSheet::Build(
            fx.cards, {"scorch", "hold_a", "hold_b"}).value);
        bc.RequestBeat(BattleBeat::Execution);
        Ticks(bc, 1);
        const SimEvent* ev = FindKind(bc.Events(),
                                      SimEvent::Kind::VillageBurned);
        Check(ev != nullptr && ev->side == 0,
              "contested burn emits, perpetrator recorded");
    }

    // --- Convoy spawn validation ---
    {
        BattleController bc(1, fx.map, fx.cards);
        Check(!bc.SpawnConvoy(-1, {0, 1}), "convoy bad side rejected");
        Check(!bc.SpawnConvoy(0, {0}), "convoy len<2 rejected");
        Check(!bc.SpawnConvoy(0, {0, 3}), "convoy non-adjacent rejected");
        Check(!bc.SpawnConvoy(0, {0, 9}), "convoy OOB region rejected");
        Check(!bc.SpawnConvoy(0, {0, 1, 0}), "convoy revisit rejected");
        for (int i = 0; i < 4; ++i) {
            Check(bc.SpawnConvoy(0, {0, 1}), "convoy spawn accepted");
        }
        Check(!bc.SpawnConvoy(0, {0, 1}), "MAX_CONVOYS cap enforced");
    }

    // --- Convoy arrival: escorted and unescorted ---
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("esc"), 2, 0);  // waits at destination r2
        bc.DeploySquad(Mk("foe"), 4, 1); // off-route parking
        Check(bc.SpawnConvoy(0, {0, 1, 2}), "convoy spawned");
        bc.RequestBeat(BattleBeat::Execution);
        Ticks(bc, 2 * CONVOY_LEG_TICKS + 2);
        const SimEvent* ev = FindKind(bc.Events(),
                                      SimEvent::Kind::ConvoyArrived);
        Check(ev != nullptr, "convoy arrived event emitted");
        Check(ev && ev->param == 2 && ev->side == 0 && ev->aux == 0,
              "arrival carries destination + owner + convoy index");
        Check(ev && ev->squadIndex == 0,
              "escort witness = friendly squad at destination");
    }
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("ally"), 4, 0);
        bc.DeploySquad(Mk("foe"), 3, 1); // r3 off route {0,1,2}? r3 is
                                       // adjacent to r2 but not on it
        Check(bc.SpawnConvoy(0, {0, 1, 2}), "convoy spawned");
        bc.RequestBeat(BattleBeat::Execution);
        Ticks(bc, 2 * CONVOY_LEG_TICKS + 2);
        const SimEvent* ev = FindKind(bc.Events(),
                                      SimEvent::Kind::ConvoyArrived);
        Check(ev != nullptr && ev->squadIndex == -1,
              "unescorted arrival records no escort");
    }

    // --- Convoy raid mid-route: unguarded enemy presence ---
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("ally"), 4, 0);
        bc.DeploySquad(Mk("raider"), 1, 1); // camps r1, mid-route
        Check(bc.SpawnConvoy(0, {0, 1, 2}), "convoy spawned");
        bc.RequestBeat(BattleBeat::Execution);
        Ticks(bc, 2 * CONVOY_LEG_TICKS + 2);
        const SimEvent* ev = FindKind(bc.Events(),
                                      SimEvent::Kind::ConvoyRaided);
        Check(ev != nullptr, "convoy raided event emitted");
        Check(ev && ev->param == 1 && ev->side == 1 && ev->aux == 0 &&
                  ev->squadIndex == 1,
              "raid carries region + raider side/squad + convoy index");
        Check(CountKind(bc.Events(), SimEvent::Kind::ConvoyArrived) == 0,
              "raided convoy never arrives");
    }

    // --- Presence deters: guard beside the convoy's node ---
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("guard"), 1, 0); // guards r1 the whole time
        bc.DeploySquad(Mk("raider"), 1, 1); // shares r1 — contested
        Check(bc.SpawnConvoy(0, {0, 1, 2}), "convoy spawned");
        bc.RequestBeat(BattleBeat::Execution);
        Ticks(bc, 2 * CONVOY_LEG_TICKS + 2);
        Check(CountKind(bc.Events(), SimEvent::Kind::ConvoyRaided) == 0,
              "guarded convoy is not raided");
        Check(CountKind(bc.Events(), SimEvent::Kind::ConvoyArrived) == 1,
              "guarded convoy arrives");
    }

    // --- Raider camped at the destination takes it at the gate ---
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("ally"), 4, 0);
        bc.DeploySquad(Mk("raider"), 2, 1); // camps the destination
        Check(bc.SpawnConvoy(0, {0, 1, 2}), "convoy spawned");
        bc.RequestBeat(BattleBeat::Execution);
        Ticks(bc, 2 * CONVOY_LEG_TICKS + 2);
        const SimEvent* ev = FindKind(bc.Events(),
                                      SimEvent::Kind::ConvoyRaided);
        Check(ev != nullptr && ev->param == 2,
              "raider at destination raids at the gate");
        Check(CountKind(bc.Events(), SimEvent::Kind::ConvoyArrived) == 0,
              "gate-raided convoy never arrives");
    }

    // --- SpawnConvoy is Planning-only ---
    {
        BattleController bc(1, fx.map, fx.cards);
        Parked(bc);
        bc.RequestBeat(BattleBeat::Execution);
        Check(!bc.SpawnConvoy(0, {0, 1}), "convoy spawn in Execution "
                                        "rejected");
    }

    // --- CardFired precedes its own VillageBurned in the log ---
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("torch"), 1, 0);
        bc.DeploySquad(Mk("foe"), 3, 1);
        bc.SetSheet(0, SquadSheet::Build(
            fx.cards, {"scorch", "hold_a", "hold_b"}).value);
        bc.RequestBeat(BattleBeat::Execution);
        Ticks(bc, 1);
        int firedAt = -1, burnedAt = -1;
        for (std::size_t i = 0; i < bc.Events().size(); ++i) {
            const SimEvent& e = bc.Events()[i];
            if (e.kind == SimEvent::Kind::CardFired &&
                e.cardId == "scorch" && firedAt < 0) {
                firedAt = static_cast<int>(i);
            }
            if (e.kind == SimEvent::Kind::VillageBurned &&
                burnedAt < 0) {
                burnedAt = static_cast<int>(i);
            }
        }
        Check(firedAt >= 0 && burnedAt > firedAt,
              "CardFired precedes VillageBurned in log order");
    }

    // --- Slot order is authored semantics: [move,burn] walks off,
    //     [burn,move] torches then walks ---
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("torch"), 1, 0);
        bc.DeploySquad(Mk("foe"), 3, 1);
        bc.SetSheet(0, SquadSheet::Build(
            fx.cards, {"advance2", "scorch", "hold_a"}).value);
        bc.RequestBeat(BattleBeat::Execution);
        Ticks(bc, 1);
        bool scorchFired = false;
        for (const SimEvent& e : bc.Events()) {
            if (e.kind == SimEvent::Kind::CardFired &&
                e.cardId == "scorch") scorchFired = true;
        }
        Check(scorchFired, "move-first sheet still logs the burn card");
        Check(CountKind(bc.Events(), SimEvent::Kind::VillageBurned) == 0,
              "Move delta earlier in list preempts the arson");
    }
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("torch"), 1, 0);
        bc.DeploySquad(Mk("foe"), 3, 1);
        bc.SetSheet(0, SquadSheet::Build(
            fx.cards, {"scorch", "advance2", "hold_a"}).value);
        bc.RequestBeat(BattleBeat::Execution);
        Ticks(bc, 1);
        Check(CountKind(bc.Events(), SimEvent::Kind::VillageBurned) == 1,
              "burn-first sheet torches before walking");
    }

    // --- Arrow-driving squad burns Holding, then marches ---
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("torch"), 1, 0);
        bc.DeploySquad(Mk("foe"), 3, 1);
        bc.SetSheet(0, SquadSheet::Build(
            fx.cards, {"scorch", "hold_a", "hold_b"}).value);
        Check(bc.DrawArrow(0, 0, {1, 2}), "arrow drawn through village");
        bc.RequestBeat(BattleBeat::Execution);
        Ticks(bc, 1);
        Check(CountKind(bc.Events(), SimEvent::Kind::VillageBurned) == 1,
              "arrow squad burns while still Holding");
        Ticks(bc, 25);
        Check(bc.Squads()[0].regionIndex == 2,
              "arrow squad marched on after the burn");
    }

    // --- Two arsonists, one village, one tick: lowest index wins ---
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("t1"), 1, 0);
        bc.DeploySquad(Mk("t2"), 1, 0);
        bc.DeploySquad(Mk("foe"), 3, 1);
        for (int i = 0; i < 2; ++i) {
            bc.SetSheet(static_cast<std::size_t>(i),
                        SquadSheet::Build(
                            fx.cards, {"scorch", "hold_a", "hold_b"})
                            .value);
        }
        bc.RequestBeat(BattleBeat::Execution);
        Ticks(bc, 1);
        const SimEvent* ev = FindKind(bc.Events(),
                                      SimEvent::Kind::VillageBurned);
        Check(CountKind(bc.Events(), SimEvent::Kind::VillageBurned) == 1 &&
                  ev && ev->squadIndex == 0,
              "double burn same tick: one event, lowest squad index");
    }

    // --- Side-1 convoy raided by a side-0 squad ---
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("raider"), 2, 0); // camps mid-route r2
        bc.DeploySquad(Mk("foe"), 4, 1);
        Check(bc.SpawnConvoy(1, {3, 2, 1}), "side-1 convoy spawned");
        bc.RequestBeat(BattleBeat::Execution);
        Ticks(bc, CONVOY_LEG_TICKS + 2);
        const SimEvent* ev = FindKind(bc.Events(),
                                      SimEvent::Kind::ConvoyRaided);
        Check(ev != nullptr && ev->side == 0 && ev->squadIndex == 0 &&
                  ev->param == 2,
              "side-1 convoy raid records side-0 perpetrator");
    }

    // --- Raider camped at the spawn node raids on tick 0 ---
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("ally"), 4, 0);
        bc.DeploySquad(Mk("raider"), 0, 1); // camps path[0]
        Check(bc.SpawnConvoy(0, {0, 1, 2}), "convoy spawned");
        bc.RequestBeat(BattleBeat::Execution);
        Ticks(bc, 3);
        const SimEvent* ev = FindKind(bc.Events(),
                                      SimEvent::Kind::ConvoyRaided);
        Check(ev != nullptr && ev->tick == 0 && ev->param == 0,
              "hostile spawn node raids the convoy on tick 0");
    }

    // --- A convoy mid-route at battle close reports nothing ---
    {
        BattleController bc(1, fx.map, fx.cards);
        Parked(bc);
        Check(bc.SpawnConvoy(0, {0, 1, 2}), "convoy spawned");
        bc.RequestBeat(BattleBeat::Execution);
        Ticks(bc, 10); // mid-route
        bc.RequestBeat(BattleBeat::Aftermath); // concede
        Check(CountKind(bc.Events(), SimEvent::Kind::ConvoyArrived) == 0 &&
                  CountKind(bc.Events(), SimEvent::Kind::ConvoyRaided) == 0,
              "in-flight convoy at close emits no terminal event");
    }

    // --- Refused rout-surrender: Execute kills a Routing enemy ---
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("ally"), 1, 0); // shares r1 with the foe
        bc.DeploySquad(Mk("foe"), 1, 1);
        bc.RequestBeat(BattleBeat::Execution);
        auto r = bc.IssueRetreat(1, 1); // the plea
        Check(r.ok() && r.value, "rout plea issued");
        Ticks(bc, 1); // applies at tick start -> Routing
        Check(bc.Squads()[1].state == SquadState::Routing,
              "plea pending: foe routing at r1");
        bc.SetCpPool(0, 0); // empty pool — refusal is free
        r = bc.IssueExecute(0, 1);
        Check(r.ok() && r.value, "execute issued at 0 CP");
        Ticks(bc, 1);
        const SimEvent* ev =
            FindKind(bc.Events(), SimEvent::Kind::SquadExecuted);
        Check(ev != nullptr, "refusal deed emitted at apply");
        Check(ev && ev->squadIndex == 1 && ev->side == 0 &&
                  ev->param == 1,
              "deed carries victim + refuser + region");
        Check(bc.Squads()[1].state == SquadState::Destroyed,
              "refused plea is a corpse, not a prisoner");
        Check(bc.Squads()[1].hp == 0,
              "the kill shot zeroes hp (Destroyed => hp==0)");
    }

    // --- Execute gates: beat, side, victim state, reach ---
    {
        BattleController bc(1, fx.map, fx.cards);
        Parked(bc);
        Check(!bc.IssueExecute(0, 1).ok(),
              "execute outside Execution rejected");
        bc.RequestBeat(BattleBeat::Execution);
        Check(!bc.IssueExecute(0, 0).ok(),
              "cannot execute your own squad");
        Check(!bc.IssueExecute(0, 1).ok(),
              "non-routing victim rejected");
        Check(!bc.IssueExecute(2, 1).ok(), "bad side rejected");
        Check(!bc.IssueExecute(0, 9).ok(), "bad index rejected");
    }

    // --- Reach is required; a squad off the region cannot refuse ---
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("ally"), 4, 0); // parked far
        bc.DeploySquad(Mk("foe"), 1, 1);
        bc.RequestBeat(BattleBeat::Execution);
        bc.IssueRetreat(1, 1);
        Ticks(bc, 1);
        Check(!bc.IssueExecute(0, 1).ok(),
              "no squad in reach to refuse");
        // Mercy by passage: the plea ages off to Routed — off-field,
        // and no refusal lands after that.
        Ticks(bc, ROUT_TICKS + 2);
        Check(bc.Squads()[1].state == SquadState::Routed,
              "unrefused plea ages off-field");
        Check(!bc.IssueExecute(0, 1).ok(),
              "routed victim is beyond reach");
        Check(CountKind(bc.Events(), SimEvent::Kind::SquadExecuted) == 0,
              "no deed without a refusal");
    }

    // --- A marching squad has already left: presence requires
    //     Holding ---
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("ally"), 1, 0);
        bc.DeploySquad(Mk("foe"), 1, 1);
        bc.RequestBeat(BattleBeat::Execution);
        bc.IssueRetreat(1, 1);
        auto r = bc.IssueRedirect(0, 0, 0); // ally marches off r1
        Check(r.ok() && r.value, "ally redirect queues");
        Ticks(bc, 1); // retreat + redirect apply; ally now Moving
        Check(bc.Squads()[0].state == SquadState::Moving,
              "ally departed mid-leg");
        Check(!bc.IssueExecute(0, 1).ok(),
              "a killer mid-march cannot refuse");
    }

    // --- One refusal per victim; symmetric for the enemy ---
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("ally"), 1, 0);
        bc.DeploySquad(Mk("foe"), 1, 1);
        bc.RequestBeat(BattleBeat::Execution);
        bc.IssueRetreat(1, 1);
        Ticks(bc, 1);
        auto r = bc.IssueExecute(0, 1);
        Check(r.ok() && r.value, "first refusal queues");
        Check(!bc.IssueExecute(0, 1).ok(),
              "victim already marked rejected");
    }
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("ally"), 1, 0); // ally routs; foe refuses
        bc.DeploySquad(Mk("foe"), 1, 1);
        bc.RequestBeat(BattleBeat::Execution);
        bc.IssueRetreat(0, 0);
        Ticks(bc, 1);
        auto r = bc.IssueExecute(1, 0);
        Check(r.ok() && r.value, "enemy refusal queues");
        Ticks(bc, 1);
        const SimEvent* ev =
            FindKind(bc.Events(), SimEvent::Kind::SquadExecuted);
        Check(ev != nullptr && ev->side == 1 && ev->squadIndex == 0,
              "enemy refusal records side-1 perpetrator");
        Check(bc.Squads()[0].state == SquadState::Destroyed,
              "our routed squad is destroyed");
    }

    // --- Issue event stamps the victim's region for context ---
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("ally"), 1, 0);
        bc.DeploySquad(Mk("foe"), 1, 1);
        bc.RequestBeat(BattleBeat::Execution);
        bc.IssueRetreat(1, 1);
        Ticks(bc, 1);
        bc.IssueExecute(0, 1);
        const SimEvent* iv = nullptr;
        for (const SimEvent& e : bc.Events()) {
            if (e.kind == SimEvent::Kind::Intervention &&
                e.aux == static_cast<int>(InterventionKind::Execute)) {
                iv = &e;
            }
        }
        Check(iv != nullptr && iv->squadIndex == 1 && iv->side == 0 &&
                  iv->param == 1,
              "execute command journal carries victim + region");
    }

    // --- Wire: governance kinds round-trip; out-of-range rejected ---
    {
        bool allOk = true;
        for (int k = static_cast<int>(SimEvent::Kind::VillageOccupied);
             k <= static_cast<int>(
                     SimEvent::Kind::MythInvasion); ++k) {
            SimEvent e;
            e.kind = static_cast<SimEvent::Kind>(k);
            e.tick = 42; e.squadIndex = 3; e.slotIndex = -1;
            e.param = 1; e.aux = 2; e.side = 1;
            SimEvent back;
            if (!EventFromJson(EventToJson(e), back)) allOk = false;
            else if (back.kind != e.kind || back.param != e.param ||
                     back.aux != e.aux || back.side != e.side ||
                     back.squadIndex != e.squadIndex ||
                     back.tick != e.tick) allOk = false;
        }
        Check(allOk, "event kinds 4-12 round-trip on the wire");
        auto bad = JsonValue::Parse(
            R"({"kind":13,"tick":0,"squad":-1,"slot":-1,"param":-1,)"
            R"("aux":0,"side":-1,"path":[],"card":""})");
        SimEvent sink;
        Check(bad.ok() && !EventFromJson(bad.value, sink),
              "event kind 13 rejected on the wire");
        // aux gate: MythGhostArmy(9) is the max legitimate
        // Intervention ordinal; aux=10+ is wire-garbage and must
        // reject.
        auto badAux = JsonValue::Parse(
            R"({"kind":2,"tick":0,"squad":0,"slot":-1,"param":6,)"
            R"("aux":10,"side":0,"path":[],"card":""})");
        Check(badAux.ok() && !EventFromJson(badAux.value, sink),
              "intervention aux beyond MythGhostArmy rejected");
    }

    // --- Determinism: identical runs, identical stream + checksum ---
    {
        const auto run = [&]() {
            BattleController bc(7, fx.map, fx.cards);
            bc.DeploySquad(Mk("occ"), 1, 0);
            bc.DeploySquad(Mk("foe"), 3, 1);
            bc.SpawnConvoy(0, {0, 1, 2});
            bc.RequestBeat(BattleBeat::Execution);
            Ticks(bc, 2 * CONVOY_LEG_TICKS + 2);
            return std::pair{bc.Events().size(), bc.Checksum()};
        };
        auto a = run();
        auto b = run();
        Check(a == b, "governance state is deterministic");
    }

    std::printf(failures ? "GOVERNANCE TESTS FAILED: %d\n"
                         : "GOVERNANCE TESTS PASS\n",
                failures);
    return failures ? 1 : 0;
}
