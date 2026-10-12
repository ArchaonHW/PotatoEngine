#include "Gameplay/Doctrine/Doctrine.h"
#include "Gameplay/Fog/QuantumFog.h"
#include "Gameplay/Map/BattleMap.h"
#include "Gameplay/Squad/Squad.h"
#include "Gameplay/Json/JsonValue.h"
#include "Gameplay/Sim/Sim.h"
#include "Gameplay/Json/Json.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

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

const char* MAP_DOC = R"json({
    "schema": "potato.map/1",
    "id": "m", "name": "Line",
    "regions": [{"id": "r0"}, {"id": "r1"}, {"id": "r2"}],
    "edges": [["r0", "r1"], ["r1", "r2"]]
})json";

const char* CARDS_DOC = R"json({
    "schema": "potato.doctrine_cards/1",
    "cards": [
        {"id": "card_brace", "name": "堅守陣地", "cooldown": 5,
         "trigger": {"type": "enemy_adjacent"},
         "condition": {"type": "cohesion_above", "param": 50},
         "action": {"type": "brace", "param": 15},
         "modifier": {"type": "attack_boost", "param": 2}},
        {"id": "card_flee", "name": "見勢撤退", "cooldown": 10,
         "trigger": {"type": "cohesion_below", "param": 30},
         "action": {"type": "retreat"}},
        {"id": "card_hold", "name": "Hold", "cooldown": 5,
         "trigger": {"type": "always"},
         "condition": {"type": "cp_at_least", "param": 3},
         "action": {"type": "hold"}},
        {"id": "card_push", "name": "Push", "cooldown": 5,
         "trigger": {"type": "always"},
         "action": {"type": "move", "param": 2}},
        {"id": "card_garrison", "name": "Garrison", "cooldown": 5,
         "trigger": {"type": "enemy_in_region"},
         "action": {"type": "hold"}},
        {"id": "card_routwarn", "name": "RoutWarn", "cooldown": 5,
         "trigger": {"type": "always"},
         "condition": {"type": "cohesion_below", "param": 25},
         "action": {"type": "brace", "param": 5}}
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

// Fog where every enemy is perfectly known — preserves pre-1.8 test
// expectations (truth==belief at certainty 100). Stale-intel scenarios
// build their own clouds.
std::array<QuantumFog, 2> TruthfulFog(const BattleMap& map,
                                      const std::vector<Squad>& squads) {
    std::array<QuantumFog, 2> fog{QuantumFog(map, FogConfig{}),
                                  QuantumFog(map, FogConfig{})};
    for (const Squad& s : squads) {
        if (s.regionIndex == Squad::NO_REGION) continue;
        fog[s.side == 1 ? 0 : 1].AddCloud(
            static_cast<int>(s.regionIndex), 100);
    }
    return fog;
}

} // namespace

int main() {
    BattleMap map = LoadMap();
    DoctrineLibrary cards = LoadCards();
    Check(map.RegionCount() == 3 && cards.Count() == 6, "fixtures load");

    // --- Sheet build validation ---
    Check(!SquadSheet::Build(cards, {"a", "b"}).ok(), "sheet <3 rejected");
    Check(!SquadSheet::Build(cards,
              {"card_brace","card_flee","card_hold","card_push","card_brace","card_flee"})
              .ok(), "sheet >5 rejected");
    Check(!SquadSheet::Build(cards,
              {"card_brace","card_flee","card_nope"}).ok(), "unknown card id rejected");
    auto sheetR = SquadSheet::Build(cards, {"card_brace","card_flee","card_hold"});
    Check(sheetR.ok() && sheetR.value.slots.size() == 3, "3-card sheet builds");

    // --- EvalTick: trigger/condition gating ---
    {
        SquadTemplate t; t.id = "t"; t.unit = UnitType::Infantry; t.hp = 100;
        t.speed = Speed::Medium;
        std::vector<Squad> squads{ Squad::Instantiate(t, 0) };
        squads[0].cohesion = 80;
        std::vector<SquadSheet> sheets{ SquadSheet::Build(cards,
            {"card_brace","card_flee","card_hold"}).value };
        Prng rng(42);
        auto fog = TruthfulFog(map, squads);

        // No enemy adjacent, cohesion 80, cp 0: brace trigger fails,
        // flee trigger fails, hold condition (cp>=3) fails.
        auto o0 = Doctrine::EvalTick(map, squads, sheets, cards, rng, std::array<int,2>{0,0}, fog, 0);
        Check(o0.events.empty(), "no fire when trigger/condition unmet");
        // card_hold fires once cp>=3.
        auto o1 = Doctrine::EvalTick(map, squads, sheets, cards, rng, std::array<int,2>{3,3}, fog, 1);
        Check(o1.events.size() == 1 && o1.events[0].cardId == "card_hold" &&
              o1.events[0].squadIndex == 0 && o1.events[0].slotIndex == 2,
              "cp_at_least gates; event records slot");
    }

    // --- Cooldown gates re-fire ---
    {
        std::vector<Squad> squads;
        SquadTemplate t; t.id = "t"; t.unit = UnitType::Infantry; t.hp = 100;
        t.speed = Speed::Medium;
        squads = {Squad::Instantiate(t, 0, 0), Squad::Instantiate(t, 1, 1)};
        std::vector<SquadSheet> sheets{
            SquadSheet::Build(cards, {"card_brace","card_flee","card_hold"}).value,
            SquadSheet::Build(cards, {"card_brace","card_flee","card_hold"}).value };
        Prng rng(7);
        auto fog = TruthfulFog(map, squads);

        auto o0 = Doctrine::EvalTick(map, squads, sheets, cards, rng, std::array<int,2>{5,5}, fog, 0);
        // each side: card_brace (enemy adjacent, cohesion 100>50) +
        // card_hold (cp5>=3) fire; card_flee (cohesion 100 !<30) doesn't.
        Check(o0.events.size() == 4, "both sides' sheets fire (symmetric)");
        int braces = 0, holds = 0;
        for (const auto& e : o0.events) {
            if (e.cardId == "card_brace") ++braces;
            if (e.cardId == "card_hold") ++holds;
        }
        Check(braces == 2 && holds == 2, "exact per-card event counts");
        Check(!o0.deltas.empty(), "brace/modifier produced pending deltas");

        // Cooldown gate: suppression must hold for the full window —
        // deleting the cooldown check must be observable here.
        bool leaked = false;
        for (int i = 1; i <= 99; ++i) {
            auto sup = Doctrine::EvalTick(map, squads, sheets, cards, rng, std::array<int,2>{5,5}, fog, i);
            if (!sup.events.empty()) leaked = true;
        }
        Check(!leaked, "cooldown suppresses re-fire for 99 ticks");
        // cooldown 5s = 100 ticks: slot fired at t0, counts down t1..t99
        // (99 decrements), fires again at t100.
        auto oCd = Doctrine::EvalTick(map, squads, sheets, cards, rng, std::array<int,2>{5,5}, fog, 100);
        bool refired = false;
        for (const auto& e : oCd.events)
            if (e.cardId == "card_brace") refired = true;
        Check(refired, "cooldown ticks down and re-fires at t=100");
    }

    // --- Snapshot isolation: pending deltas invisible same-tick ---
    {
        // enemy at region 1 with 'push' (move to region 2); my squad at
        // region 0 with enemy_adjacent trigger. If deltas applied live,
        // the enemy's move could break adjacency before my trigger evals.
        SquadTemplate t; t.id = "t"; t.unit = UnitType::Infantry; t.hp = 100;
        t.attack = 5;
        t.speed = Speed::Medium;
        std::vector<Squad> squads;
        squads = {Squad::Instantiate(t, 1, 1),   // enemy evaluates FIRST
                  Squad::Instantiate(t, 0, 0)};
        std::vector<SquadSheet> sheets{
            SquadSheet::Build(cards, {"card_push","card_push","card_push"}).value,
            SquadSheet::Build(cards, {"card_brace","card_flee","card_hold"}).value };
        Prng rng(9);
        auto fog = TruthfulFog(map, squads);
        auto o = Doctrine::EvalTick(map, squads, sheets, cards, rng, std::array<int,2>{5,5}, fog, 0);
        Check(squads[0].state == SquadState::Holding &&
              squads[0].regionIndex == 1,
              "live squads untouched during eval (pending deltas only)");
        bool myBraceFired = false;
        for (const auto& e : o.events)
            if (e.squadIndex == 1 && e.cardId == "card_brace") myBraceFired = true;
        Check(myBraceFired, "snapshot isolation: pending move not visible same-tick");
        Doctrine::ApplyDeltas(squads, o.deltas);
        Check(squads[0].state == SquadState::Moving,
              "pending move applied post-eval");
        Check(squads[1].attack == 7, "modifier attack_boost applied post-eval");
    }

    // --- Canonical order: squad idx -> slot idx in event stream ---
    {
        SquadTemplate t; t.id = "t"; t.unit = UnitType::Infantry; t.hp = 100;
        t.speed = Speed::Medium;
        std::vector<Squad> squads{ Squad::Instantiate(t, 0),
                                   Squad::Instantiate(t, 0) };
        std::vector<SquadSheet> sheets{
            SquadSheet::Build(cards, {"card_hold","card_hold","card_hold"}).value,
            SquadSheet::Build(cards, {"card_hold","card_hold","card_hold"}).value };
        Prng rng(1);
        auto fog = TruthfulFog(map, squads);
        auto o = Doctrine::EvalTick(map, squads, sheets, cards, rng, std::array<int,2>{5,5}, fog, 0);
        Check(o.events.size() == 6, "all slots fire");
        bool ordered = true;
        for (std::size_t i = 0; i < o.events.size(); ++i) {
            if (o.events[i].squadIndex != static_cast<int>(i / 3) ||
                o.events[i].slotIndex != static_cast<int>(i % 3))
                ordered = false;
        }
        Check(ordered, "events in (squad,slot) canonical order");
    }

    // --- Determinism: identical seed+sheets -> identical stream ---
    {
        SquadTemplate t; t.id = "t"; t.unit = UnitType::Infantry; t.hp = 100;
        t.speed = Speed::Medium;
        auto run = [&](std::uint64_t seed) {
            std::vector<Squad> squads{ Squad::Instantiate(t, 0, 0),
                                       Squad::Instantiate(t, 1, 1) };
            squads[0].cohesion = 25;
            std::vector<SquadSheet> sheets{
                SquadSheet::Build(cards, {"card_brace","card_flee","card_hold"}).value,
                SquadSheet::Build(cards, {"card_brace","card_flee","card_hold"}).value };
            Prng rng(seed);
            auto fog = TruthfulFog(map, squads);
            std::vector<SimEvent> all;
            for (int i = 0; i < 60; ++i) {
                auto o = Doctrine::EvalTick(map, squads, sheets, cards, rng, std::array<int,2>{5,5}, fog, i);
                Doctrine::ApplyDeltas(squads, o.deltas);
                for (auto& e : o.events) all.push_back(e);
            }
            return all;
        };
        auto a = run(1234), b = run(1234);
        bool same = a.size() == b.size();
        for (std::size_t i = 0; same && i < a.size(); ++i) {
            same = a[i].tick == b[i].tick && a[i].squadIndex == b[i].squadIndex &&
                   a[i].slotIndex == b[i].slotIndex && a[i].cardId == b[i].cardId;
        }
        Check(same && !a.empty(), "identical seeds -> identical event streams");
    }

    // --- Routed/Destroyed squads don't eval; cooldown still ticks ---
    {
        SquadTemplate t; t.id = "t"; t.unit = UnitType::Infantry; t.hp = 100;
        t.speed = Speed::Medium;
        Squad dead = Squad::Instantiate(t, 0);
        dead.ApplyEvent(SquadEvent::HpZero); // -> Destroyed via the table
        std::vector<Squad> squads{dead};
        std::vector<SquadSheet> sheets{
            SquadSheet::Build(cards, {"card_hold","card_hold","card_hold"}).value };
        sheets[0].slots[0].cooldownRemaining = 5;
        Prng rng(2);
        auto fog = TruthfulFog(map, squads);
        auto o = Doctrine::EvalTick(map, squads, sheets, cards, rng, std::array<int,2>{5,5}, fog, 0);
        Check(o.events.empty() && o.deltas.empty(),
              "destroyed squad evaluates nothing");
        Check(sheets[0].slots[0].cooldownRemaining == 4,
              "cooldown still ticks on dead squad's sheet");
    }

    // --- enemy_in_region + cohesion_below condition coverage ---
    {
        SquadTemplate t; t.id = "t"; t.unit = UnitType::Infantry; t.hp = 100;
        t.speed = Speed::Medium;
        // Both squads in region 0 (enemy co-located).
        std::vector<Squad> squads{ Squad::Instantiate(t, 0, 0),
                                   Squad::Instantiate(t, 0, 1) };
        squads[0].cohesion = 20; // below routwarn's 25, above flee's 30? no: 20<30
        std::vector<SquadSheet> sheets{
            SquadSheet::Build(cards, {"card_garrison","card_routwarn","card_hold"}).value,
            SquadSheet::Build(cards, {"card_garrison","card_routwarn","card_hold"}).value };
        Prng rng(11);
        auto fog = TruthfulFog(map, squads);
        auto o = Doctrine::EvalTick(map, squads, sheets, cards, rng, std::array<int,2>{5,5}, fog, 0);
        int garrison = 0, routwarn = 0;
        for (const auto& e : o.events) {
            if (e.cardId == "card_garrison") ++garrison;
            if (e.cardId == "card_routwarn") ++routwarn;
        }
        Check(garrison == 2, "enemy_in_region fires for co-located enemies");
        Check(routwarn == 1, "cohesion_below condition gates (20<25, 100!<25)");

        // NO_REGION squads must NOT see each other as enemies.
        std::vector<Squad> off{ Squad::Instantiate(t, Squad::NO_REGION, 0),
                                Squad::Instantiate(t, Squad::NO_REGION, 1) };
        auto offO = Doctrine::EvalTick(map, off, sheets, cards, rng, std::array<int,2>{5,5}, fog, 0);
        bool garFired = false;
        for (const auto& e : offO.events)
            if (e.cardId == "card_garrison") garFired = true;
        Check(!garFired, "NO_REGION squads never satisfy enemy_in_region");
    }

    // --- move action eval-time gates (bounds + adjacency) ---
    {
        SquadTemplate t; t.id = "t"; t.unit = UnitType::Infantry; t.hp = 100;
        t.speed = Speed::Medium;
        // card_push targets region 2; squad at region 0: 2 is NOT a
        // neighbor of 0 (map is 0-1-2) → delta suppressed.
        std::vector<Squad> squads{ Squad::Instantiate(t, 0, 0) };
        std::vector<SquadSheet> sheets{
            SquadSheet::Build(cards, {"card_push","card_push","card_push"}).value };
        Prng rng(13);
        auto fog = TruthfulFog(map, squads);
        auto o = Doctrine::EvalTick(map, squads, sheets, cards, rng, std::array<int,2>{0,0}, fog, 0);
        bool moveDelta = false;
        for (const auto& d : o.deltas)
            if (d.kind == PendingDelta::Kind::Move) moveDelta = true;
        Check(!moveDelta, "non-adjacent move target suppressed at eval");
        // Squad at region 1: 2 IS adjacent → delta emitted. Fresh sheet —
        // the suppressed fire above still consumed the cooldown (a fired
        // card that no-ops is still a fire, same as Hold).
        squads[0] = Squad::Instantiate(t, 1, 0);
        sheets = {SquadSheet::Build(cards,
            {"card_push","card_push","card_push"}).value};
        auto o2 = Doctrine::EvalTick(map, squads, sheets, cards, rng, std::array<int,2>{0,0}, fog, 0);
        moveDelta = false;
        for (const auto& d : o2.deltas)
            if (d.kind == PendingDelta::Kind::Move && d.region == 2)
                moveDelta = true;
        Check(moveDelta, "adjacent move target emits delta");
        // OOB target: region 99 doesn't exist → suppressed at eval.
        auto dl = DoctrineLibrary::FromJson(JsonValue::Parse(R"({"cards":[
            {"id":"oob","trigger":{"type":"always"},
             "action":{"type":"move","param":99}}]})").value);
        Check(dl.ok(), "oob move card loads (loader can't see the map)");
        auto s3 = SquadSheet::Build(dl.value, {"oob","oob","oob"});
        std::vector<SquadSheet> sh3{ s3.value };
        auto o3 = Doctrine::EvalTick(map, squads, sh3, dl.value, rng,
                                     std::array<int,2>{0,0}, fog, 0);
        moveDelta = false;
        for (const auto& d : o3.deltas)
            if (d.kind == PendingDelta::Kind::Move) moveDelta = true;
        Check(!moveDelta, "out-of-range move target suppressed at eval");
    }

    // --- Loader rejects ---
    auto reject = [](const char* json, const char* name) {
        auto d = JsonValue::Parse(json);
        if (!d.ok()) { Check(false, name); return; }
        auto m = DoctrineLibrary::FromJson(d.value);
        Check(!m.ok() && !m.error.empty() && !m.reason.empty(), name);
    };
    reject(R"({"cards":[]})", "empty cards rejected");
    reject(R"({"cards":[{"id":"c","trigger":{"type":"always"}},
                          {"id":"c","trigger":{"type":"always"}}]})",
           "duplicate card id rejected");
    reject(R"({"cards":[{"id":"c","trigger":{"type":"explode"}}]})",
           "unknown trigger type rejected");
    reject(R"({"cards":[{"id":"c","trigger":{"param":1}}]})",
           "missing clause type rejected");
    reject(R"({"cards":[{"id":"c","cooldown":61}]})", "cooldown >60 rejected");
    reject(R"({"cards":[{"id":"c","cooldown":"fast"}]})", "non-int cooldown rejected");
    reject(R"({"schema":"potato.doctrine_cards/2","cards":[{"id":"c"}]})",
           "schema tag re-checked");
    reject(R"({"cards":[{"id":"c","action":{"type":"move","param":"x"}}]})",
           "non-int param rejected");
    reject(R"({"cards":[{"id":"c","action":{"type":"move","param":-1}}]})",
           "negative move param rejected");
    reject(R"({"cards":[{"id":"c","action":{"type":"brace"}}]})",
           "brace without param rejected");
    reject(R"({"cards":[{"id":"c","trigger":{"type":"cohesion_below"}}]})",
           "cohesion_below without param rejected");
    reject(R"({"cards":[{"id":"c","trigger":{"type":"cohesion_below","param":150}}]})",
           "cohesion param >100 rejected");
    reject(R"({"cards":[{"id":"c","modifier":{"type":"attack_boost","param":99999}}]})",
           "attack_boost param out of range rejected");
    reject(R"({"cards":[{"id":"c","condition":{"type":"cp_at_least","param":-3}}]})",
           "negative cp param rejected");
    reject(R"({"cards":[{"id":"c","trigger":{"type":7}}]})",
           "non-string clause type rejected");

    // --- Story 9.1: launch card pool + declared balance tables ---
    {
        namespace fs = std::filesystem;
        const fs::path repo =
            fs::path(__FILE__).parent_path().parent_path();
        const auto pool = DoctrineLibrary::Load(
            (repo / "assets" / "doctrine" / "core.json").string());
        Check(pool.ok(), "9.1: launch card pool parses");
        if (pool.ok()) {
            Check(pool.value.Count() >= 40 &&
                      pool.value.Count() <= 60,
                  "9.1: pool breadth 40-60 cards");

            const auto bal = Json::Load(
                (repo / "assets" / "balance.json").string(),
                "potato.balance/1");
            Check(bal.ok(), "9.1: balance doc parses");
            auto table = [&](const char* key) {
                std::set<std::int64_t> s;
                if (bal.ok()) {
                    const JsonValue& a = bal.value["cards"][key];
                    if (a.IsArray())
                        for (const auto& v : a.Items())
                            if (v.IsInt()) s.insert(v.AsInt());
                }
                return s;
            };
            const auto cds = table("cooldowns_seconds");
            const auto gates = table("cohesion_gates");
            const auto braces = table("brace_amounts");
            const auto boosts = table("attack_boosts");
            const auto cps = table("cp_gates");
            const auto targets = table("move_targets");
            Check(!cds.empty() && !braces.empty() && !boosts.empty(),
                  "9.1: balance tables declared");

            bool inTables = true;
            bool trigSeen[4] = {}, condSeen[4] = {}, actSeen[5] = {};
            for (std::size_t i = 0; i < pool.value.Count(); ++i) {
                const DoctrineCard& c = pool.value.At(i);
                if (c.cooldownTicks % TICK_RATE_HZ != 0 ||
                    !cds.count(c.cooldownTicks / TICK_RATE_HZ))
                    inTables = false;
                if (c.trigger == TriggerKind::CohesionBelow &&
                    !gates.count(c.triggerParam))
                    inTables = false;
                if ((c.condition == ConditionKind::CohesionAbove ||
                     c.condition == ConditionKind::CohesionBelow) &&
                    !gates.count(c.conditionParam))
                    inTables = false;
                if (c.condition == ConditionKind::CpAtLeast &&
                    !cps.count(c.conditionParam))
                    inTables = false;
                if (c.action == ActionKind::Brace &&
                    !braces.count(c.actionParam))
                    inTables = false;
                if (c.action == ActionKind::Move &&
                    !targets.count(c.actionParam))
                    inTables = false;
                if (c.modifier == ModifierKind::AttackBoost &&
                    !boosts.count(c.modifierParam))
                    inTables = false;
                trigSeen[static_cast<int>(c.trigger)] = true;
                condSeen[static_cast<int>(c.condition)] = true;
                actSeen[static_cast<int>(c.action)] = true;
            }
            Check(inTables,
                  "9.1: every card draws from declared tables");
            for (int i = 0; i < 4; ++i)
                Check(trigSeen[i] && condSeen[i],
                      "9.1: trigger+condition kind coverage");
            for (int i = 0; i < 5; ++i)
                Check(actSeen[i], "9.1: action kind coverage");
        }
    }
    if (failures == 0) {
        std::puts("DOCTRINE TESTS PASS");
        return 0;
    }
    std::printf("%d CHECK(S) FAILED\n", failures);
    return 1;
}
