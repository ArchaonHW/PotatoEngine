#include "Gameplay/Fog/QuantumFog.h"
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
    "regions": [{"id": "r0"}, {"id": "r1"}, {"id": "r2"}, {"id": "r3"}],
    "edges": [["r0", "r1"], ["r1", "r2"], ["r2", "r3"]]
})json";

const char* CARDS_DOC = R"json({
    "schema": "potato.doctrine_cards/1",
    "cards": [
        {"id": "card_watch", "cooldown": 5,
         "trigger": {"type": "enemy_in_region"},
         "action": {"type": "hold"}},
        {"id": "card_scout", "cooldown": 5,
         "trigger": {"type": "enemy_adjacent"},
         "action": {"type": "hold"}},
        {"id": "card_idle", "cooldown": 5,
         "trigger": {"type": "always"},
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

int CountCardFires(const BattleController& bc, const char* id) {
    int n = 0;
    for (const auto& e : bc.Events())
        if (e.kind == SimEvent::Kind::CardFired && e.cardId == id) ++n;
    return n;
}

const CloudEntity* CloudAt(const QuantumFog& fog, int region) {
    for (const CloudEntity& c : fog.Clouds())
        if (c.believedRegion == region) return &c;
    return nullptr;
}

} // namespace

int main() {
    BattleMap map = LoadMap();
    DoctrineLibrary cards = LoadCards();
    SquadTemplate tmpl = MakeTemplate();
    Check(map.RegionCount() == 4 && cards.Count() == 3, "fixtures load");

    // --- FogConfig: potato.balance/1 ---
    {
        auto d = JsonValue::Parse(R"({"schema":"potato.balance/1",
            "fog":{"decay_per_minute":6,"probe_gain":40,
                   "detect_threshold":70,"initial_intel":80}})");
        auto cfg = FogConfig::FromJson(d.value);
        Check(cfg.ok() && cfg.value.decayPerMinute == 6 &&
              cfg.value.probeGain == 40 && cfg.value.detectThreshold == 70 &&
              cfg.value.initialIntel == 80, "balance/1 fog section loads");
        auto bad = FogConfig::FromJson(JsonValue::Parse(
            R"({"schema":"potato.balance/2","fog":{}})").value);
        Check(!bad.ok(), "wrong schema rejected");
        auto empty = FogConfig::FromJson(JsonValue::Parse("{}").value);
        Check(empty.ok() && empty.value.decayPerMinute == 10,
              "missing fog section -> defaults");
    }

    // --- Unit: decay is exactly 1pt/120 ticks at 10/min ---
    {
        QuantumFog fog(map, FogConfig{});
        fog.Observe(0);                       // field[0] = 100
        const int cid = fog.AddCloud(1, 90);  // cloud cert 90
        for (int i = 0; i < 119; ++i) fog.TickDecay();
        Check(fog.CertaintyAt(0) == 100, "no decay before 120 ticks");
        fog.TickDecay();                      // tick 120
        Check(fog.CertaintyAt(0) == 99, "field decays 1pt/120 ticks");
        bool cloudDecayed = false;
        for (const CloudEntity& c : fog.Clouds())
            if (c.id == cid && c.certainty == 89) cloudDecayed = true;
        Check(cloudDecayed, "cloud certainty decays too");
        for (int i = 0; i < 6000; ++i) fog.TickDecay();
        Check(fog.CertaintyAt(0) >= 0, "decay floors at 0");
    }

    // --- Unit: probe raises field, not clouds; cap 100 ---
    {
        QuantumFog fog(map, FogConfig{});
        const int cid = fog.AddCloud(2, 50);
        fog.Probe(0);
        Check(fog.CertaintyAt(0) == 25, "probe +25 on field");
        fog.Probe(0); fog.Probe(0); fog.Probe(0); fog.Probe(0);
        Check(fog.CertaintyAt(0) == 100, "probe capped at 100");
        Check(fog.Clouds()[0].certainty == 50 &&
              fog.Clouds()[0].believedRegion == 2,
              "probe does not collapse clouds");
        (void)cid;
    }

    // --- Unit: collapse, visible threshold, entangle pair link ---
    {
        QuantumFog fog(map, FogConfig{});
        const int a = fog.AddCloud(0, 30);  // below detectThreshold
        Check(fog.VisibleAt(0) == 0, "cloud under threshold invisible");
        fog.Collapse(a, 2);
        Check(fog.VisibleAt(2) == 1 && fog.VisibleAt(0) == 0,
              "collapse snaps + certainty 100");
        const int b = fog.AddCloud(3, 10);
        fog.Entangle(a, b);
        Check(fog.EntangledWith(a) == b && fog.EntangledWith(b) == a,
              "entangle links both ways");
        fog.RemoveCloud(b);
        Check(fog.EntangledWith(a) == -1, "remove untangles partner");
    }

    // --- Deploy: enemy fog gains a cloud at initial intel ---
    {
        BattleController bc(1, map, cards);
        bc.DeploySquad(tmpl, 0, 0);
        bc.DeploySquad(tmpl, 3, 1);
        Check(bc.Fog(1).Clouds().size() == 1 &&
              bc.Fog(0).Clouds().size() == 1,
              "each side's fog tracks the enemy");
        Check(bc.Fog(0).Clouds()[0].believedRegion == 3 &&
              bc.Fog(0).Clouds()[0].certainty == 60,
              "initial intel: enemy region at certainty 60");
    }

    // --- Auto-observe: adjacent truth collapses cloud each tick ---
    {
        BattleController bc(2, map, cards);
        bc.DeploySquad(tmpl, 0, 0);   // sees r0+r1
        bc.DeploySquad(tmpl, 1, 1);   // enemy truth at r1 — visible
        bc.SetCloudIntel(0, 1, 3, 40); // stale intel: cloud thinks r3
        bc.RequestBeat(BattleBeat::Execution);
        bc.Tick();
        const CloudEntity* c = CloudAt(bc.Fog(0), 1);
        Check(c != nullptr && c->certainty == 100,
              "observed cloud collapses to truth");
    }

    // --- Stale intel: doctrine fires on BELIEF, not truth ---
    {
        BattleController bc(3, map, cards);
        bc.DeploySquad(tmpl, 0, 0);   // player at r0
        bc.DeploySquad(tmpl, 3, 1);   // enemy truth at r3 — invisible
        bc.SetSheet(0, SquadSheet::Build(cards,
            {"card_watch","card_scout","card_idle"}).value);
        // Corrupt intel: cloud claims enemy is standing ON r0.
        bc.SetCloudIntel(0, 1, 0, 100);
        bc.RequestBeat(BattleBeat::Execution);
        bc.Tick();
        Check(CountCardFires(bc, "card_watch") == 1,
              "enemy_in_region fires on stale belief (false positive)");
        // Miss direction: truth at r2, belief at r0, player at r0 can
        // see only r0/r1 — cloud stays stale, VisibleAt(2) == 0 proves
        // doctrine reads no enemy where truth actually stands. (A
        // doctrine-level miss is unreachable: auto-observe covers every
        // region either trigger can check — the squad's own + adjacent.)
        BattleController bc2(4, map, cards);
        bc2.DeploySquad(tmpl, 0, 0);
        bc2.DeploySquad(tmpl, 2, 1); // truth r2 — outside r0's sight ring
        bc2.SetSheet(0, SquadSheet::Build(cards,
            {"card_watch","card_scout","card_idle"}).value);
        bc2.SetCloudIntel(0, 1, 1, 100); // belief misplaced to r1
        bc2.RequestBeat(BattleBeat::Execution);
        bc2.Tick();
        Check(bc2.Fog(0).VisibleAt(2) == 0 &&
              bc2.Fog(0).VisibleAt(1) == 1,
              "unobserved truth stays hidden (belief != truth)");
        Check(CountCardFires(bc2, "card_scout") == 1,
              "enemy_adjacent fires on belief adjacent to squad "
              "(truth r2 is NOT adjacent — pure stale-intel fire)");
    }

    // --- Stale intel decays away ---
    {
        BattleController bc(5, map, cards);
        bc.DeploySquad(tmpl, 0, 0);
        bc.DeploySquad(tmpl, 3, 1);
        bc.SetCloudIntel(0, 1, 2, 61);  // just above threshold
        bc.RequestBeat(BattleBeat::Execution);
        Check(bc.Fog(0).VisibleAt(2) == 1, "stale cloud detected t0");
        // 61 -> after ~1 decay point still >=60; needs 2 pts: 240 ticks
        for (int i = 0; i < 240; ++i) bc.Tick();
        Check(bc.Fog(0).VisibleAt(2) == 0,
              "decayed stale intel falls below detection");
    }

    // --- Probe: budget 3, applies next tick, no collapse ---
    {
        BattleController bc(6, map, cards);
        bc.DeploySquad(tmpl, 0, 0);
        bc.DeploySquad(tmpl, 3, 1);
        bc.RequestBeat(BattleBeat::Execution);
        Check(bc.IssueProbe(0, 2).ok(), "probe accepted");
        Check(bc.IssueProbe(0, 2).ok() && bc.IssueProbe(0, 2).ok(),
              "probes 2-3 accepted");
        Check(!bc.IssueProbe(0, 2).ok(), "4th probe rejected (budget)");
        bc.Tick();
        Check(bc.Fog(0).CertaintyAt(2) == 75, "3 probes applied +25 each");
        const CloudEntity* c = CloudAt(bc.Fog(0), 3);
        Check(c != nullptr && c->certainty != 100,
              "probe never collapses clouds");
    }

    // --- Entangle: collapse propagates to the partner's own truth ---
    {
        BattleController bc(7, map, cards);
        bc.DeploySquad(tmpl, 0, 0);   // side0 @ r0, sees r0+r1
        bc.DeploySquad(tmpl, 1, 1);   // enemy A @ r1 — observed
        bc.DeploySquad(tmpl, 3, 1);   // enemy B @ r3 — NOT observed
        bc.SetCloudIntel(0, 2, 0, 40); // corrupt B's belief to r0 @40
        const int idA = bc.Fog(0).Clouds()[0].id;
        const int idB = bc.Fog(0).Clouds()[1].id;
        bc.RequestBeat(BattleBeat::Execution);
        Check(bc.IssueEntangle(0, idA, idB).ok() &&
              bc.CpPool(0) == 1, "entangle accepted, costs 2");
        bc.Tick(); // entangle applies, then A observed -> collapse
        Check(bc.Fog(0).EntangledWith(idA) == idB,
              "entangle applied at tick start");
        Check(bc.Fog(0).VisibleAt(3) == 1,
              "collapse propagates to unobserved partner's truth");
        // event payload carries both cloud ids (squadIndex=A, param=B)
        bool evtOk = false;
        for (const auto& e : bc.Events())
            if (e.kind == SimEvent::Kind::Intervention &&
                e.aux == static_cast<int>(InterventionKind::Entangle) &&
                e.squadIndex == idA && e.param == idB) evtOk = true;
        Check(evtOk, "entangle event records both cloud ids");
        // re-entangle of an already-linked cloud is rejected
        Check(!bc.IssueEntangle(0, idA, idB).ok(),
              "already-entangled cloud rejected");
    }

    // --- Field certainty: seen regions Observe to 100 ---
    {
        BattleController bc(9, map, cards);
        bc.DeploySquad(tmpl, 0, 0);
        bc.DeploySquad(tmpl, 3, 1);
        bc.RequestBeat(BattleBeat::Execution);
        bc.Tick();
        Check(bc.Fog(0).CertaintyAt(0) == 100 &&
              bc.Fog(0).CertaintyAt(1) == 100 &&
              bc.Fog(0).CertaintyAt(3) == 0,
              "seen regions (own + adjacent) observe to 100");
    }

    // --- SetCloudIntel is Planning-only ---
    {
        BattleController bc(8, map, cards);
        bc.DeploySquad(tmpl, 0, 0);
        bc.DeploySquad(tmpl, 3, 1);
        bc.RequestBeat(BattleBeat::Execution);
        Check(!bc.SetCloudIntel(0, 1, 2, 90),
              "intel hook rejected during Execution");
        Check(!bc.SetCloudIntel(0, 0, 2, 90),
              "cannot brief about own squads (no cloud)");
    }

    if (failures == 0) { std::puts("FOG TESTS PASS"); return 0; }
    std::printf("%d CHECK(S) FAILED\n", failures);
    return 1;
}
