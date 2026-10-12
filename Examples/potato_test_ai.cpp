#include "Gameplay/AI/BattleAI.h"
#include "Gameplay/Command/Intervention.h"
#include "Gameplay/Doctrine/Doctrine.h"
#include "Gameplay/Fog/QuantumFog.h"
#include "Gameplay/Json/JsonValue.h"
#include "Gameplay/Map/BattleMap.h"
#include "Gameplay/Sim/BattleController.h"
#include "Gameplay/Squad/Squad.h"

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
        {"id": "card_punch", "cooldown": 5,
         "trigger": {"type": "always"}, "action": {"type": "hold"},
         "modifier": {"type": "attack_boost", "param": 5}},
        {"id": "card_brace", "cooldown": 5,
         "trigger": {"type": "always"},
         "action": {"type": "brace", "param": 10}},
        {"id": "card_ambush", "cooldown": 5,
         "trigger": {"type": "enemy_in_region"},
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
    SquadTemplate t; t.id = "t"; t.hp = 100; t.attack = 10;
    t.speed = Speed::VeryFast; // 20 ticks/edge
    return t;
}
const std::vector<std::string> POOL = {"card_punch", "card_brace",
                                       "card_ambush"};

} // namespace

int main() {
    BattleMap map = LoadMap();
    DoctrineLibrary cards = LoadCards();
    SquadTemplate tmpl = MakeTemplate();
    Check(map.RegionCount() == 4 && cards.Count() == 3,
          "fixtures load");

    // --- ScoreCard pins the prior table ---
    {
        const DoctrineCard& punch = *cards.Find("card_punch");
        const DoctrineCard& brace = *cards.Find("card_brace");
        const DoctrineCard& ambush = *cards.Find("card_ambush");
        Check(BattleAI::ScoreCard(punch, Prior::Aggressive) >
                  BattleAI::ScoreCard(brace, Prior::Aggressive) &&
              BattleAI::ScoreCard(punch, Prior::Aggressive) >
                  BattleAI::ScoreCard(ambush, Prior::Aggressive),
              "aggressive prefers attack_boost");
        Check(BattleAI::ScoreCard(brace, Prior::Defensive) >
                  BattleAI::ScoreCard(punch, Prior::Defensive),
              "defensive prefers brace");
        Check(BattleAI::ScoreCard(ambush, Prior::Cunning) >
                  BattleAI::ScoreCard(punch, Prior::Cunning) &&
              BattleAI::ScoreCard(ambush, Prior::Cunning) >
                  BattleAI::ScoreCard(brace, Prior::Cunning),
              "cunning prefers ambush triggers");
    }

    // --- Aggressive plan: closest region, full arrow ---
    {
        BattleController bc(1, map, cards);
        BattleAI ai(99, 1, Prior::Aggressive, map, cards);
        Check(ai.Plan(bc, {tmpl}, {0, 1}, POOL, 3),
              "aggressive plan accepted");
        Check(bc.Squads().size() == 1 &&
                  bc.Squads()[0].regionIndex == 1,
              "aggressive deploys closest to objective");
        Check(bc.ArrowOf(0).active && bc.ArrowOf(0).path.size() == 3 &&
                  bc.ArrowOf(0).path.back() == 3,
              "aggressive draws full path to objective");
        // Symmetry: stats untouched — attack/cohesion/hp == template.
        Check(bc.Squads()[0].attack == tmpl.attack &&
                  bc.Squads()[0].hp == tmpl.hp &&
                  bc.Squads()[0].cohesion == tmpl.cohesion,
              "no stat cheating (template values verbatim)");
    }

    // --- Defensive plan: furthest region, first-hop arrow only ---
    {
        BattleController bc(2, map, cards);
        BattleAI ai(99, 1, Prior::Defensive, map, cards);
        Check(ai.Plan(bc, {tmpl}, {0, 1}, POOL, 3),
              "defensive plan accepted");
        Check(bc.Squads()[0].regionIndex == 0,
              "defensive deploys furthest from objective");
        Check(bc.ArrowOf(0).path.size() == 2,
              "defensive draws only the first hop");
    }

    // --- Cunning plan: full arrow + feint in the ENEMY's fog ---
    {
        BattleController bc(3, map, cards);
        BattleAI ai(99, 1, Prior::Cunning, map, cards);
        Check(ai.Plan(bc, {tmpl}, {1}, POOL, 3),
              "cunning plan accepted");
        // Enemy (side 0) fog: cloud believed at decoy r0 (neighbor of
        // r1 not on path {1,2,3}), while truth sits at r1.
        const auto& clouds = bc.Fog(0).Clouds();
        Check(clouds.size() == 1 && clouds[0].believedRegion == 0 &&
                  clouds[0].certainty == 70,
              "cunning plants feint: believed r0, truth r1");
        Check(bc.Squads()[0].regionIndex == 1, "truth unmoved");
    }

    // --- Execution: AI redirects toward believed contact ---
    {
        BattleController bc(4, map, cards);
        // AI on side 1 at r1, objective r3; player squad at r2 (in
        // AI's fog once SyncFog observes — deploy grants initialIntel).
        bc.DeploySquad(tmpl, 2, 0);
        BattleAI ai(7, 1, Prior::Aggressive, map, cards);
        Check(ai.Plan(bc, {tmpl}, {1}, POOL, 3), "ai plan");
        bc.RequestBeat(BattleBeat::Execution);
        // AI's fog sees player's cloud at r2 (adjacent to r1) after the
        // exec-start SyncFog collapse... believed, certainty >= thresh.
        Check(bc.Fog(1).VisibleAt(2) > 0, "ai fog sees contact at r2");
        ai.Act(bc); // queues Redirect(1->2) at 1 CP
        Check(bc.CpPool(1) == CP_START - 1, "ai paid CP like a player");
        bc.Tick();
        bool sawRedirect = false;
        for (const SimEvent& e : bc.Events()) {
            if (e.kind == SimEvent::Kind::Intervention &&
                e.squadIndex == 1 &&
                e.aux == static_cast<int>(InterventionKind::Redirect) &&
                e.param == 2)
                sawRedirect = true;
        }
        Check(sawRedirect, "redirect issued + logged as player-format");
        Check(bc.Squads()[1].state == SquadState::Moving &&
                  bc.Squads()[1].edgeTarget == 2,
              "ai squad pushes toward contact");
    }

    // --- CP economy symmetry: empty pool -> no command ---
    {
        BattleController bc(5, map, cards);
        bc.DeploySquad(tmpl, 2, 0); // believed at r2, adjacent to AI r1
        BattleAI ai(7, 1, Prior::Aggressive, map, cards);
        ai.Plan(bc, {tmpl}, {1}, POOL, 3);
        bc.SetCpPool(1, 0); // redirect costs 1, override costs 2
        bc.RequestBeat(BattleBeat::Execution);
        ai.Act(bc);
        bc.Tick();
        Check(bc.CpPool(1) == 0, "pool untouched when AI can't afford");
        bool anyCmd = false;
        for (const SimEvent& e : bc.Events()) {
            if (e.kind == SimEvent::Kind::Intervention &&
                e.squadIndex == 1)
                anyCmd = true;
        }
        Check(!anyCmd, "no phantom command on insufficient CP");
    }

    // --- Determinism: same seed+prior+inputs -> same checksum ---
    {
        auto run = [&](Prior p) {
            BattleController b(42, map, cards);
            b.DeploySquad(tmpl, 2, 0);
            b.DeploySquad(tmpl, 0, 0);
            BattleAI ai(7, 1, p, map, cards);
            ai.Plan(b, {tmpl}, {3}, POOL, 0);
            b.RequestBeat(BattleBeat::Execution);
            for (int i = 0; i < 10; ++i) { ai.Act(b); b.Tick(); }
            return b.Checksum();
        };
        Check(run(Prior::Aggressive) == run(Prior::Aggressive),
              "same prior same seed -> same checksum");
        Check(run(Prior::Aggressive) != run(Prior::Defensive),
              "different priors diverge the battle");
    }

    // --- Defensive Act: low-cohesion retreat (event-pinned) ---
    {
        BattleController bc(10, map, cards);
        SquadTemplate weak = tmpl;
        weak.cohesion = 40; // Holding (<45 threshold) but >20 rout
        bc.DeploySquad(tmpl, 3, 0);
        BattleAI ai(7, 1, Prior::Defensive, map, cards);
        ai.Plan(bc, {weak}, {0}, POOL, 3);
        bc.RequestBeat(BattleBeat::Execution);
        ai.Act(bc); // cohesion 40 < 45 -> IssueRetreat (3 CP)
        Check(bc.CpPool(1) == CP_START - 3, "defensive paid 3 CP");
        bc.Tick();
        bool sawRetreat = false;
        for (const SimEvent& e : bc.Events()) {
            if (e.kind == SimEvent::Kind::Intervention &&
                e.aux == static_cast<int>(InterventionKind::Retreat))
                sawRetreat = true;
        }
        Check(sawRetreat &&
                  bc.Squads()[1].state == SquadState::Routing,
              "defensive retreats a wavering squad");
    }

    // --- Defensive Act: brace override + ranked sheet pinned ---
    {
        BattleController bc(14, map, cards);
        // Enemy believed CO-LOCATED at the AI squad's region -> the
        // defensive policy force-fires its brace slot. The fired card
        // id proves the ranked sheet leads with card_brace.
        bc.DeploySquad(tmpl, 0, 0);
        BattleAI ai(7, 1, Prior::Defensive, map, cards);
        ai.Plan(bc, {tmpl}, {0}, POOL, 3);
        bc.RequestBeat(BattleBeat::Execution);
        Check(bc.Fog(1).VisibleAt(0) > 0, "enemy believed co-located");
        ai.Act(bc);
        bc.Tick(); // override applies -> forceNext -> fires next eval
        bc.Tick();
        bool braceFired = false, overrideLogged = false;
        for (const SimEvent& e : bc.Events()) {
            if (e.kind == SimEvent::Kind::Intervention &&
                e.aux == static_cast<int>(InterventionKind::Override))
                overrideLogged = true;
            if (e.kind == SimEvent::Kind::CardFired &&
                e.squadIndex == 1 && e.cardId == "card_brace")
                braceFired = true;
        }
        Check(overrideLogged, "defensive issues brace override");
        Check(braceFired,
              "brace card fired — ranked sheet composition verified");
    }

    // --- Cunning Act: probes + entangle (id-vs-index regression) ---
    {
        BattleController bc(11, map, cards);
        // Five player squads at r3; two spawn-Routing (cohesion 10)
        // rout off in ~40 ticks and get their clouds PRUNED — leaving
        // sparse ids {2,3,4}. Distinct certainties make the lowest-two
        // selection observable: expect entangle(3,4), never (3,2).
        SquadTemplate routing = tmpl;
        routing.cohesion = 10;
        bc.DeploySquad(routing, 3, 0); // cloud id 0 — will vanish
        bc.DeploySquad(routing, 3, 0); // cloud id 1 — will vanish
        bc.DeploySquad(tmpl, 3, 0);    // id 2, cert -> 90
        bc.DeploySquad(tmpl, 3, 0);    // id 3, cert -> 50 (lowest)
        bc.DeploySquad(tmpl, 3, 0);    // id 4, cert -> 70 (2nd lowest)
        bc.SetCloudIntel(1, 2, 3, 90);
        bc.SetCloudIntel(1, 3, 3, 50);
        bc.SetCloudIntel(1, 4, 3, 70);
        BattleAI ai(7, 1, Prior::Cunning, map, cards);
        // Objective == start region: no arrow, the AI squad holds at
        // r0 and never auto-observes r2/r3 — cloud certs stay planted.
        ai.Plan(bc, {tmpl}, {0}, POOL, 0);
        bc.RequestBeat(BattleBeat::Execution);
        for (int i = 0; i < 45; ++i) bc.Tick(); // routings leave field
        Check(bc.Fog(1).Clouds().size() == 3,
              "clouds pruned to sparse ids 2,3,4");
        ai.Act(bc);
        bc.Tick(); // probes + entangle apply
        bool sawProbe = false, sawEntangle = false;
        for (const SimEvent& e : bc.Events()) {
            if (e.kind == SimEvent::Kind::Intervention) {
                if (e.aux == static_cast<int>(InterventionKind::Probe))
                    sawProbe = true;
                if (e.aux == static_cast<int>(InterventionKind::Entangle))
                    sawEntangle = true;
            }
        }
        Check(sawProbe, "cunning probes low-certainty regions");
        Check(sawEntangle, "cunning entangles weakest clouds");
        Check(bc.Fog(1).EntangledWith(3) == 4 &&
                  bc.Fog(1).EntangledWith(4) == 3,
              "entangle picks ids 3+4 — not positional garbage");
    }

    // --- Cunning Act: replans a squad standing off its arrow ---
    {
        BattleController bc(12, map, cards);
        bc.DeploySquad(tmpl, 3, 0);
        BattleAI ai(7, 1, Prior::Cunning, map, cards);
        ai.Plan(bc, {tmpl}, {1}, POOL, 3); // arrow {1,2,3}
        bc.RequestBeat(BattleBeat::Execution);
        // External command pulls the squad off its arrow (r1 -> r0).
        Check(bc.IssueRedirect(1, 1, 0).ok(), "off-path redirect issued");
        for (int i = 0; i < 21; ++i) bc.Tick(); // arrives r0, Holding
        ai.Act(bc);
        bool sawReplan = false;
        for (const SimEvent& e : bc.Events()) {
            if (e.kind == SimEvent::Kind::Intervention &&
                e.squadIndex == 1 &&
                e.aux == static_cast<int>(InterventionKind::Replan))
                sawReplan = true;
        }
        Check(sawReplan, "cunning replans an off-path squad");
        bc.Tick();
        Check(bc.ArrowOf(1).path.size() >= 2 &&
                  bc.ArrowOf(1).path.front() == 0,
              "replanned arrow starts at the squad's position");
    }

    // --- Act outside Execution is a no-op ---
    {
        BattleController bc(13, map, cards);
        bc.DeploySquad(tmpl, 3, 0);
        BattleAI ai(7, 1, Prior::Aggressive, map, cards);
        ai.Plan(bc, {tmpl}, {1}, POOL, 3);
        const std::size_t ev = bc.Events().size();
        ai.Act(bc); // still Planning
        Check(bc.Events().size() == ev && bc.CpPool(1) == CP_START,
              "Act is a no-op outside Execution");
    }

    // --- Rejects ---
    {
        BattleController bc(6, map, cards);
        BattleAI ai(1, 1, Prior::Aggressive, map, cards);
        Check(!ai.Plan(bc, {}, {0}, POOL, 3), "empty troops rejected");
        Check(!ai.Plan(bc, {tmpl}, {}, POOL, 3), "empty starts rejected");
        Check(!ai.Plan(bc, {tmpl}, {0}, POOL, 9), "OOB objective");
        Check(!ai.Plan(bc, {tmpl}, {9}, POOL, 3), "OOB start region");
        Check(bc.Squads().empty(), "rejected plan leaves no state");
        bc.DeploySquad(tmpl, 0, 0);
        bc.RequestBeat(BattleBeat::Execution);
        Check(!ai.Plan(bc, {tmpl}, {1}, POOL, 3),
              "Plan outside Planning rejected");
    }

    if (failures == 0) { std::puts("AI TESTS PASS"); return 0; }
    std::printf("%d CHECK(S) FAILED\n", failures);
    return 1;
}
