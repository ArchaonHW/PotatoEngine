// Story 5.1 — Myth infiltration state machine tests: the transition
// table, the battle-scope MythField (checksum + journaled ops +
// InfiltrationChanged events), and potato.myth/1 persistence.

#include "Campaign/Myth/MythState.h"
#include "Gameplay/Doctrine/Doctrine.h"
#include "Gameplay/Json/JsonValue.h"
#include "Gameplay/Map/BattleMap.h"
#include "Gameplay/Myth/Infiltration.h"
#include "Gameplay/Record/BattleRecorder.h"
#include "Gameplay/Record/ReplayVerifier.h"
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
using Potato::Campaign::MythState;

// Line map: r0 - r1(shrine + village) - r2 - r3 — r1 carries both
// layers' marks so burn/shrine asymmetry is exercisable.
const char* MAP_DOC = R"json({
    "schema": "potato.map/1", "id": "m", "name": "Line",
    "regions": [{"id": "r0"},
                {"id": "r1", "strategic": ["village"],
                 "myth": ["shrine"]},
                {"id": "r2"}, {"id": "r3"}],
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
         "trigger": {"type": "always"}, "action": {"type": "hold"}},
        {"id": "scorch", "cooldown": 5,
         "trigger": {"type": "always"}, "action": {"type": "burn"}}
    ]
})json";

struct Fx {
    BattleMap map;
    DoctrineLibrary cards;
    JsonValue md, cd;
    Fx() {
        md = JsonValue::Parse(MAP_DOC).value;
        cd = JsonValue::Parse(CARDS_DOC).value;
        map = BattleMap::FromJson(md).value;
        cards = DoctrineLibrary::FromJson(cd).value;
    }
};

SquadTemplate Mk(const char* id) {
    SquadTemplate t; t.id = id; t.name = id; t.hp = 200;
    t.attack = 10; t.speed = Speed::VeryFast; t.cohesion = 100;
    return t;
}

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

} // namespace

int main() {
    Fx fx;
    using IL = InfiltrationLevel;
    using MK = MythEventKind;

    // --- The table IS the spec: exhaustive 4 x 2 ---
    {
        Check(Transition(IL::None, MK::Incursion) == IL::Whispered,
              "table: 0 incursion -> 1");
        Check(Transition(IL::Whispered, MK::Incursion) == IL::Haunted,
              "table: 1 incursion -> 2");
        Check(Transition(IL::Haunted, MK::Incursion) == IL::Invaded,
              "table: 2 incursion -> 3");
        Check(Transition(IL::Invaded, MK::Incursion) == IL::Invaded,
              "table: 3 incursion saturates");
        Check(Transition(IL::None, MK::Pacification) == IL::None,
              "table: 0 pacification saturates");
        Check(Transition(IL::Whispered, MK::Pacification) == IL::None,
              "table: 1 pacification -> 0");
        Check(Transition(IL::Haunted, MK::Pacification) ==
                  IL::Whispered,
              "table: 2 pacification -> 1");
        Check(Transition(IL::Invaded, MK::Pacification) ==
                  IL::Haunted,
              "table: 3 pacification -> 2");
    }

    // --- Wire gates bound the domains ---
    {
        IL l;
        Check(InfiltrationLevelFromInt(0, l) && l == IL::None,
              "level 0 parses");
        Check(InfiltrationLevelFromInt(3, l) && l == IL::Invaded,
              "level 3 parses");
        Check(!InfiltrationLevelFromInt(-1, l), "level -1 rejected");
        Check(!InfiltrationLevelFromInt(4, l), "level 4 rejected");
        MK k;
        Check(MythEventKindFromInt(0, k) && k == MK::Incursion,
              "kind 0 parses");
        Check(!MythEventKindFromInt(2, k), "kind 2 rejected");
        Check(std::string(InfiltrationLevelName(IL::Haunted)) ==
                  "haunted",
              "level names exist");
    }

    // --- MythField standalone ---
    {
        MythField f;
        f.Init(fx.map);
        Check(f.RegionCount() == 4, "field sizes to the map");
        Check(f.LevelAt(1) == IL::None, "fresh field is quiet");
        auto ev = f.Apply(1, MK::Incursion, 7);
        Check(ev.has_value(), "apply emits on transition");
        Check(ev && ev->kind == SimEvent::Kind::InfiltrationChanged &&
                  ev->param == 1 && ev->aux == 1 && ev->tick == 7 &&
                  ev->squadIndex == -1 && ev->side == -1,
              "event carries region + new level, side-less");
        Check(f.LevelAt(1) == IL::Whispered, "level moved to 1");
        ev = f.Apply(1, MK::Incursion, 8);
        Check(f.LevelAt(1) == IL::Haunted && ev.has_value(),
              "second incursion -> 2");
        Check(!f.Apply(99, MK::Incursion, 9).has_value(),
              "OOB region apply rejected");
        Check(f.LevelAt(99) == IL::None, "OOB level reads quiet");
        Check(!f.Seed(99, IL::Invaded), "OOB seed rejected");
        Check(f.Seed(2, IL::Invaded) &&
                  f.LevelAt(2) == IL::Invaded,
              "seed sets a persisted level directly");
        ev = f.Apply(2, MK::Incursion, 10);
        Check(!ev.has_value() && f.LevelAt(2) == IL::Invaded,
              "saturating apply emits nothing");
    }

    // --- Controller verbs: Planning gate, checksum, events ---
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("ally"), 0, 0);
        bc.DeploySquad(Mk("foe"), 3, 1);
        Check(!bc.SeedInfiltration(9, 1), "seed OOB rejected");
        Check(!bc.SeedInfiltration(1, 4), "seed level 4 rejected");
        Check(!bc.SeedInfiltration(1, -1), "seed level -1 rejected");
        Check(bc.SeedInfiltration(1, 2), "seed level 2 accepted");
        Check(bc.Myth().LevelAt(1) == IL::Haunted, "seed applied");
        Check(bc.ApplyMythEvent(1, MK::Incursion),
              "incursion accepted");
        Check(bc.Myth().LevelAt(1) == IL::Invaded, "level now 3");
        const SimEvent* ev = FindKind(
            bc.Events(), SimEvent::Kind::InfiltrationChanged);
        Check(ev && ev->param == 1 && ev->aux == 3 && ev->tick == 0,
              "planning-phase transition stamps tick 0");
        // Seed emits no event — carry-in is an initial condition.
        Check(CountKind(bc.Events(),
                        SimEvent::Kind::InfiltrationChanged) == 1,
              "seed emits no event");
        bc.RequestBeat(BattleBeat::Execution);
        Check(!bc.SeedInfiltration(2, 1),
              "seed in Execution rejected");
        Check(!bc.ApplyMythEvent(2, MK::Incursion),
              "myth apply in Execution rejected");
    }

    // --- Journal contract: no-op ops reject, never journal ---
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("ally"), 0, 0);
        bc.DeploySquad(Mk("foe"), 3, 1);
        Check(!bc.ApplyMythEvent(0, MK::Pacification),
              "saturating pacification at 0 rejects");
        Check(!bc.SeedInfiltration(0, 0),
              "seed to current level rejects");
        Check(bc.SeedInfiltration(0, 1), "real seed accepts");
        Check(!bc.SeedInfiltration(0, 1),
              "seed to same level rejects");
        Check(CountKind(bc.Events(),
                        SimEvent::Kind::InfiltrationChanged) == 0,
              "rejected ops emit nothing");
        // InfiltrationChanged precedes BeatChanged(Execution) —
        // pin the event-stream shape this introduces.
        Check(bc.ApplyMythEvent(0, MK::Incursion), "apply accepts");
        bc.RequestBeat(BattleBeat::Execution);
        const std::vector<SimEvent>& evs = bc.Events();
        std::size_t firstMyth = evs.size(), firstBeat = evs.size();
        for (std::size_t i = 0; i < evs.size(); ++i) {
            if (evs[i].kind == SimEvent::Kind::InfiltrationChanged &&
                firstMyth == evs.size()) firstMyth = i;
            if (evs[i].kind == SimEvent::Kind::BeatChanged &&
                firstBeat == evs.size()) firstBeat = i;
        }
        Check(firstMyth < firstBeat,
              "myth events precede the Execution BeatChanged");
    }

    // --- Checksum: infiltration is sim state ---
    {
        BattleController a(1, fx.map, fx.cards);
        BattleController b(1, fx.map, fx.cards);
        for (BattleController* p : {&a, &b}) {
            p->DeploySquad(Mk("ally"), 0, 0);
            p->DeploySquad(Mk("foe"), 3, 1);
        }
        Check(a.Checksum() == b.Checksum(),
              "identical battles have identical checksums");
        b.SeedInfiltration(2, 1);
        Check(a.Checksum() != b.Checksum(),
              "infiltration seed folds into the checksum");
    }

    // --- Record -> replay: myth ops verify bit-exact ---
    {
        BattleRecorder rec;
        BattleController bc(5, fx.map, fx.cards);
        rec.Bind(5, fx.md, fx.cd);
        SquadTemplate a = Mk("ally"), f = Mk("foe");
        bc.DeploySquad(a, 0, 0); rec.RecordDeploy(a, 0, 0);
        bc.DeploySquad(f, 3, 1); rec.RecordDeploy(f, 3, 1);
        Check(bc.SeedInfiltration(1, 2), "record: seed carried in");
        rec.RecordMythSeed(1, 2);
        Check(bc.ApplyMythEvent(1, MK::Incursion),
              "record: incursion applied");
        rec.RecordMyth(1, 0);
        bc.RequestBeat(BattleBeat::Execution);
        for (int i = 0; i < 5 && bc.Tick(); ++i) {}
        bc.RequestBeat(BattleBeat::Aftermath);
        rec.Seal(bc);
        auto doc = rec.ToJson();
        Check(doc.ok(), "myth record sealed");
        if (doc.ok()) {
            auto r = Replay::Verify(doc.value);
            Check(r.ok() && r.value.ok,
                  "myth ops replay bit-exact");
            if (r.ok() && !r.value.ok) {
                std::printf("    replay reason: %s\n",
                            r.value.reason.c_str());
            }

            // Forge: inject a myth op into a sealed payload and
            // recompute the root — the replayed op must apply
            // (state changed) or the record rejects.
            JsonValue::Object payload = doc.value.Members();
            payload.erase("integrity");
            JsonValue::Array in = payload["inputs"].Items();
            JsonValue::Object forge;
            // Pacification at level 0 — a saturating no-op. Under
            // the journal contract it now rejects on replay, so a
            // forged no-op op can never ride a valid record.
            forge.emplace("kind", JsonValue::Int(1));
            forge.emplace("op", JsonValue::String("myth"));
            forge.emplace("region", JsonValue::Int(2));
            in.push_back(JsonValue::MakeObject(std::move(forge)));
            payload["inputs"] = JsonValue::MakeArray(std::move(in));
            JsonValue::Object integ;
            integ.emplace("root", JsonValue::Int(
                static_cast<std::int64_t>(
                    BattleRecorder::ComputeRoot(
                        JsonValue::MakeObject(payload)))));
            JsonValue::Object tampered = payload;
            tampered.emplace("integrity",
                             JsonValue::MakeObject(std::move(integ)));
            auto fr = Replay::Verify(
                JsonValue::MakeObject(std::move(tampered)));
            Check(fr.ok() && !fr.value.ok,
                  "forged myth op diverges the replay");
        }
    }

    // --- potato.myth/1 persistence ---
    {
        MythState st;
        Check(st.LevelAt("ch_a", 3) == 0, "absent reads quiet");
        Check(st.Set("ch_a", 3, 2), "set level 2");
        Check(st.LevelAt("ch_a", 3) == 2, "read back 2");
        Check(!st.Set("ch_a", 3, 4), "level 4 rejected");
        Check(!st.Set("ch_a", -1, 1), "region -1 rejected");
        Check(!st.Set("ch_a", 1024, 1), "region cap rejected");
        Check(!st.Set("", 1, 1), "empty chapter id rejected");
        Check(st.Set("ch_a", 3, 0), "0-write erases");
        Check(st.LevelAt("ch_a", 3) == 0, "erased reads quiet");
        Check(st.ChapterCount() == 0,
              "last erase drops the chapter entry (canonical)");
        Check(st.Set("ch_a", 5, 3) && st.Set("ch_b", 0, 1),
              "two chapters hold state");
        auto doc = st.ToJson();
        Check(doc.ok(), "myth doc serializes");
        auto rt = MythState::FromJson(doc.value);
        Check(rt.ok() && rt.value.LevelAt("ch_a", 5) == 3 &&
                  rt.value.LevelAt("ch_b", 0) == 1 &&
                  rt.value.ChapterCount() == 2,
              "round-trip preserves per-chapter levels");
        // Rejections: bad schema, non-object chapters, bad region
        // key, level 0 (non-canonical), level 4, mistyped value.
        auto bad = [](const char* json) {
            return !MythState::FromJson(
                       JsonValue::Parse(json).value)
                       .ok();
        };
        Check(bad(R"({"schema":"potato.myth/2","chapters":{}})"),
              "wrong schema rejected");
        Check(bad(R"({"schema":"potato.myth/1","chapters":[]})"),
              "non-object chapters rejected");
        Check(bad(R"({"schema":"potato.myth/1","chapters":
                      {"c":{"x":1}}})"),
              "non-numeric region key rejected");
        Check(bad(R"({"schema":"potato.myth/1","chapters":
                      {"c":{"1024":1}}})"),
              "region key cap rejected");
        Check(bad(R"({"schema":"potato.myth/1","chapters":
                      {"c":{"3":0}}})"),
              "level 0 rejected (canonical form stores 1..3)");
        Check(bad(R"({"schema":"potato.myth/1","chapters":
                      {"c":{"3":4}}})"),
              "level 4 rejected");
        Check(bad(R"({"schema":"potato.myth/1","chapters":
                      {"c":{"3":"high"}}})"),
              "mistyped level rejected");
        Check(bad(R"({"schema":"potato.myth/1","chapters":
                      {"c":{"03":1}}})"),
              "leading-zero region key rejected (non-canonical)");
    }

    // --- CommitMyth: battle field -> campaign persistence ---
    {
        MythField f;
        f.Init(fx.map);
        f.Apply(1, MK::Incursion, 0);
        f.Apply(1, MK::Incursion, 0); // -> Haunted
        f.Seed(3, IL::Whispered);
        MythState st;
        auto cm = Potato::Campaign::CommitMyth(st, "ch_a", f);
        Check(cm.ok() && cm.value == 2,
              "commit returns nonzero-level count");
        Check(st.LevelAt("ch_a", 1) == 2 && st.LevelAt("ch_a", 3) == 1,
              "commit persists nonzero levels");
        Check(st.LevelAt("ch_a", 0) == 0 && st.LevelAt("ch_a", 2) == 0,
              "quiet regions stay canonical");
        // A rejected commit must leave stored state untouched —
        // ClearChapter runs only after every check passed.
        auto rej = Potato::Campaign::CommitMyth(st, "", f);
        Check(!rej.ok() && st.LevelAt("ch_a", 1) == 2,
              "bad chapter id rejects without clearing state");
        // A second commit replaces wholesale — a stale region can't
        // linger across map revisions.
        MythField g;
        g.Init(fx.map);
        g.Seed(2, IL::Invaded);
        cm = Potato::Campaign::CommitMyth(st, "ch_a", g);
        Check(cm.ok() && st.LevelAt("ch_a", 1) == 0 &&
                  st.LevelAt("ch_a", 2) == 3,
              "re-commit replaces the chapter's region set");
        // Chapter cap: fill the book, then a fresh chapter with
        // nonzero state must reject — an all-quiet commit still
        // clears (no slot needed).
        MythState full;
        for (int i = 0; i < 128; ++i) {
            full.Set("ch_" + std::to_string(i), 0, 1);
        }
        rej = Potato::Campaign::CommitMyth(full, "ch_new", g);
        Check(!rej.ok(), "commit to a full book rejects");
        MythField quiet;
        quiet.Init(fx.map);
        cm = Potato::Campaign::CommitMyth(full, "ch_new", quiet);
        Check(cm.ok() && cm.value == 0,
              "all-quiet commit needs no chapter slot");
    }

    // === Story 5.2 — Shrine Entities ===

    // --- Shrine capture: dwell latch, allegiance flip, stance ---
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("ally"), 1, 0); // on the shrine
        bc.DeploySquad(Mk("foe"), 3, 1); // parked
        Check(bc.Myth().ShrineAt(1) != nullptr &&
                  bc.Myth().ShrineAt(2) == nullptr,
              "shrine indexed by region");
        Check(bc.Myth().StanceAt(1, 0) == GodStance::Neutral &&
                  bc.Myth().StanceAt(1, 1) == GodStance::Neutral,
              "fresh shrine is neutral to both sides");
        bc.RequestBeat(BattleBeat::Execution);
        for (int i = 0; i < SHRINE_DEDICATION_TICKS - 1; ++i) {
            bc.Tick();
        }
        Check(CountKind(bc.Events(), SimEvent::Kind::ShrineCaptured) ==
                  0,
              "no capture before dedication completes");
        bc.Tick(); // the dedicating tick
        const SimEvent* ev =
            FindKind(bc.Events(), SimEvent::Kind::ShrineCaptured);
        Check(ev && ev->param == 1 && ev->side == 0 &&
                  ev->squadIndex == 0,
              "capture carries region + new owner + witness");
        Check(bc.Myth().ShrineAt(1)->owner == 0,
              "allegiance latched to side 0");
        Check(bc.Myth().StanceAt(1, 0) == GodStance::Favorable &&
                  bc.Myth().StanceAt(1, 1) == GodStance::Wrathful,
              "deity favors dedicatee, wrathful to the other");
    }

    // --- Contested shrine never dedicates ---
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("ally"), 1, 0);
        bc.DeploySquad(Mk("foe"), 1, 1); // shares the shrine
        bc.RequestBeat(BattleBeat::Execution);
        for (int i = 0; i < 2 * SHRINE_DEDICATION_TICKS; ++i) {
            bc.Tick();
        }
        Check(CountKind(bc.Events(), SimEvent::Kind::ShrineCaptured) ==
                  0,
              "contested shrine never captures");
        Check(bc.Myth().ShrineAt(1)->owner == -1,
              "unconsecrated stays unconsecrated");
    }

    // --- Recapture: loss frees the latch, new owner flips stance ---
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("ally"), 1, 0);
        bc.DeploySquad(Mk("foe"), 0, 1); // adjacent, can walk in
        bc.RequestBeat(BattleBeat::Execution);
        for (int i = 0; i < SHRINE_DEDICATION_TICKS; ++i) bc.Tick();
        Check(bc.Myth().ShrineAt(1)->owner == 0, "side 0 holds it");
        // Enemy walks in (adjacent r0 -> r1): contested frees latch.
        auto r = bc.IssueRedirect(1, 1, 1);
        Check(r.ok() && r.value, "contestor redirect issued");
        for (int i = 0; i < 20 + SHRINE_DEDICATION_TICKS; ++i) {
            bc.Tick();
        }
        Check(bc.Myth().ShrineAt(1)->owner == -1,
              "contested shrine releases the latch");
        // Ally walks out: enemy dedicates alone -> recapture.
        r = bc.IssueRedirect(0, 0, 2);
        Check(r.ok() && r.value, "ally withdraw issued");
        for (int i = 0; i < 20 + SHRINE_DEDICATION_TICKS + 2; ++i) {
            bc.Tick();
        }
        Check(CountKind(bc.Events(), SimEvent::Kind::ShrineCaptured) ==
                  2,
              "recapture re-dwells and re-emits");
        Check(bc.Myth().ShrineAt(1)->owner == 1 &&
                  bc.Myth().StanceAt(1, 1) == GodStance::Favorable &&
                  bc.Myth().StanceAt(1, 0) == GodStance::Wrathful,
              "recapture flips allegiance and both stances");
    }

    // --- Shrine state folds into the checksum (real divergence) ---
    {
        BattleController a(1, fx.map, fx.cards);
        BattleController b(1, fx.map, fx.cards);
        a.DeploySquad(Mk("ally"), 1, 0); // on the shrine
        b.DeploySquad(Mk("ally"), 0, 0); // parked OFF it
        for (BattleController* p : {&a, &b}) {
            p->DeploySquad(Mk("foe"), 3, 1);
            p->RequestBeat(BattleBeat::Execution);
        }
        for (int i = 0; i < SHRINE_DEDICATION_TICKS + 2; ++i) {
            a.Tick();
            b.Tick();
        }
        Check(a.Checksum() != b.Checksum(),
              "captured vs uncaptured shrine diverges the checksum");
        Check(CountKind(a.Events(), SimEvent::Kind::ShrineCaptured) ==
                  1,
              "shrine capture in deterministic run");
        Check(CountKind(b.Events(), SimEvent::Kind::ShrineCaptured) ==
                  0,
              "diverged run never dedicates");
    }

    // --- The god remembers: stance survives latch release ---
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("ally"), 1, 0);
        bc.DeploySquad(Mk("foe"), 0, 1);
        bc.RequestBeat(BattleBeat::Execution);
        for (int i = 0; i < SHRINE_DEDICATION_TICKS; ++i) bc.Tick();
        Check(bc.Myth().ShrineAt(1)->owner == 0, "captured");
        auto r = bc.IssueRedirect(0, 0, 0); // ally vacates to r0
        Check(r.ok() && r.value, "vacate issued");
        for (int i = 0; i < 30; ++i) bc.Tick(); // ally left, contested
        Check(bc.Myth().ShrineAt(1)->owner == -1,
              "latch released on loss of control");
        Check(bc.Myth().StanceAt(1, 0) == GodStance::Favorable &&
                  bc.Myth().StanceAt(1, 1) == GodStance::Wrathful,
              "stance persists on unconsecrated ground");
    }

    // --- Ash holds no village; the god still takes dedication ---
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("ally"), 0, 0); // parked off r1
        bc.DeploySquad(Mk("foe"), 1, 1);  // holds village+shrine r1
        bc.SetSheet(1, SquadSheet::Build(
            fx.cards, {"scorch", "card_idle", "card_wait"}).value);
        bc.RequestBeat(BattleBeat::Execution);
        bc.Tick(); // burn applies at delta-commit
        Check(CountKind(bc.Events(), SimEvent::Kind::VillageBurned) ==
                  1,
              "village on r1 burned");
        for (int i = 0; i < SHRINE_DEDICATION_TICKS + 2; ++i) {
            bc.Tick();
        }
        Check(CountKind(bc.Events(), SimEvent::Kind::VillageOccupied) ==
                  0,
              "burned village never occupies");
        const SimEvent* cap =
            FindKind(bc.Events(), SimEvent::Kind::ShrineCaptured);
        Check(cap && cap->side == 1,
              "shrine on burned ground still dedicates");
    }

    // --- side-outside-{0,1} squad can never index stance[] (gate) ---
    {
        MythField f;
        f.Init(fx.map);
        std::vector<Squad> squads;
        Squad q = Squad::Instantiate(Mk("ghost"), 1, 2); // side 2!
        squads.push_back(q);
        for (int i = 0; i < SHRINE_DEDICATION_TICKS + 2; ++i) {
            Check(f.Tick(squads, i).empty(),
                  "side-2 presence claims nothing");
        }
        Check(f.ShrineAt(1)->owner == -1,
              "non-domain side cannot capture");
    }

    // --- Record -> replay: a sealed ShrineCaptured verifies ---
    {
        BattleRecorder rec;
        BattleController bc(6, fx.map, fx.cards);
        rec.Bind(6, fx.md, fx.cd);
        SquadTemplate a = Mk("ally"), f = Mk("foe");
        bc.DeploySquad(a, 1, 0); rec.RecordDeploy(a, 1, 0); // shrine
        bc.DeploySquad(f, 3, 1); rec.RecordDeploy(f, 3, 1);
        bc.RequestBeat(BattleBeat::Execution);
        for (int i = 0; i < SHRINE_DEDICATION_TICKS + 5; ++i) {
            bc.Tick();
        }
        Check(CountKind(bc.Events(), SimEvent::Kind::ShrineCaptured) ==
                  1,
              "record: capture event sealed");
        bc.RequestBeat(BattleBeat::Aftermath);
        rec.Seal(bc);
        auto doc = rec.ToJson();
        Check(doc.ok(), "capture record sealed");
        if (doc.ok()) {
            auto r = Replay::Verify(doc.value);
            Check(r.ok() && r.value.ok,
                  "ShrineCaptured record replays bit-exact");
        }
    }

    // ================= Story 5.4: myth actions =================

    // --- Issue gates: beat, domain, target, dedup ---
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("ally"), 0, 0);
        bc.DeploySquad(Mk("foe"), 3, 1);
        // Planning-phase rejection.
        Check(!bc.IssueMythPacify(0, 2).ok(),
              "pacify rejected in Planning");
        bc.RequestBeat(BattleBeat::Execution);
        Check(!bc.IssueMythPacify(2, 2).ok(), "bad side rejected");
        Check(!bc.IssueMythPacify(0, 99).ok(), "OOB region rejected");
        Check(!bc.IssueMythPacify(0, 2).ok(),
              "quiet unconsecrated ground has nothing to pacify");
        Check(!bc.IssueGhostArmy(0, 2).ok(),
              "ghosts need thin veil (level>=1)");
        Check(!bc.IssueMythPossession(0, 1).ok(),
              "a god rides only its own host");
        Check(!bc.IssueMythPossession(0, 9).ok(),
              "OOB squad rejected");
        Check(bc.IssueMythPossession(0, 0).ok(),
              "possession issues on own squad");
        Check(!bc.IssueMythPossession(0, 0).ok(),
              "target already marked (dedup)");
        Check(bc.IssueMythPacify(0, 2).ok() == false, "still quiet");
        // Seed mid-Execution is NOT a verb — infiltration must be
        // seeded in Planning; re-check in the pacify-apply block.
    }

    // --- Pacify: apply emits MythActionInvoked + InfiltrationChanged ---
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("ally"), 0, 0);
        bc.DeploySquad(Mk("foe"), 3, 1);
        Check(bc.SeedInfiltration(2, 2), "haunted region seeded");
        bc.RequestBeat(BattleBeat::Execution);
        Check(bc.IssueMythPacify(0, 2).ok(), "pacify issued");
        Check(!bc.IssueMythPacify(0, 2).ok(),
              "region already marked (dedup)");
        const auto& evs = bc.Events();
        Check(FindKind(evs, SimEvent::Kind::Intervention) != nullptr,
              "pacify journal entry at issue");
        bc.Tick();
        const SimEvent* act =
            FindKind(bc.Events(), SimEvent::Kind::MythActionInvoked);
        Check(act && act->aux ==
                        static_cast<int>(MythActionKind::PacifyShrine) &&
                  act->param == 2 && act->side == 0 &&
                  act->squadIndex == -1,
              "MythActionInvoked carries kind+region+caster");
        const SimEvent* lvl =
            FindKind(bc.Events(), SimEvent::Kind::InfiltrationChanged);
        Check(lvl && lvl->param == 2 && lvl->aux == 1,
              "pacification stepped 2 -> 1");
        Check(bc.Myth().LevelAt(2) == IL::Whispered, "level folded");
    }

    // --- Pacify releases the shrine's latch; the god remembers ---
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("ally"), 1, 0);
        bc.DeploySquad(Mk("foe"), 3, 1);
        bc.RequestBeat(BattleBeat::Execution);
        for (int i = 0; i < SHRINE_DEDICATION_TICKS; ++i) bc.Tick();
        Check(bc.Myth().ShrineAt(1)->owner == 0, "captured");
        Check(bc.IssueMythPacify(0, 1).ok(),
              "pacify on consecrated quiet ground issues");
        bc.Tick();
        Check(bc.Myth().ShrineAt(1)->owner == -1,
              "pacify releases the god's allegiance");
        Check(bc.Myth().StanceAt(1, 0) == GodStance::Favorable,
              "the god still remembers the dedication");
    }

    // --- Possession: flag + buff + event ---
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("ally"), 0, 0);
        bc.DeploySquad(Mk("foe"), 3, 1);
        bc.RequestBeat(BattleBeat::Execution);
        Check(bc.IssueMythPossession(0, 0).ok(), "possession issued");
        bc.Tick();
        const Squad& host = bc.Squads()[0];
        Check(host.possessed, "war-god rides the host");
        Check(host.attack == 12, "possession sharpens attack +2");
        Check(host.cohesion == 100, "cohesion held (already max)");
        const SimEvent* act =
            FindKind(bc.Events(), SimEvent::Kind::MythActionInvoked);
        Check(act && act->aux ==
                        static_cast<int>(
                            MythActionKind::InvokePossession) &&
                  act->squadIndex == 0 && act->side == 0 &&
                  act->param == 0,
              "possession event: aux kind + squad + region");
        // Once per battle per squad.
        Check(!bc.IssueMythPossession(0, 0).ok(),
              "one god per host");
    }

    // --- Ghost army: myth-layer garrison dedicates shrines,
    //     never villages ---
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("ally"), 0, 0);
        bc.DeploySquad(Mk("foe"), 3, 1);
        Check(bc.SeedInfiltration(1, 1), "whispered veil at r1");
        bc.RequestBeat(BattleBeat::Execution);
        Check(bc.IssueGhostArmy(1, 1).ok(), "ghost army rises for side 1");
        bc.Tick();
        Check(bc.Myth().GhostAt(1) == 1, "garrison holds the region");
        const SimEvent* act =
            FindKind(bc.Events(), SimEvent::Kind::MythActionInvoked);
        Check(act && act->aux ==
                        static_cast<int>(
                            MythActionKind::RaiseGhostArmy) &&
                  act->param == 1 && act->side == 1,
              "ghost army event fields");
        for (int i = 0; i < SHRINE_DEDICATION_TICKS + 2; ++i) bc.Tick();
        const SimEvent* cap =
            FindKind(bc.Events(), SimEvent::Kind::ShrineCaptured);
        Check(cap && cap->side == 1 && cap->squadIndex == -1,
              "ghosts alone dedicate the shrine");
        Check(bc.Myth().ShrineAt(1)->owner == 1,
              "shrine latched to the ghost banner");
        Check(CountKind(bc.Events(), SimEvent::Kind::VillageOccupied) ==
                  0,
              "ghosts never hold the historical layer");
        Check(bc.Myth().StanceAt(1, 1) == GodStance::Favorable,
              "god favors the spirit host");
    }

    // --- Ghost + enemy flesh contest the shrine ---
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("ally"), 1, 0); // flesh on the shrine
        bc.DeploySquad(Mk("foe"), 3, 1);
        Check(bc.SeedInfiltration(1, 1), "veil thin");
        bc.RequestBeat(BattleBeat::Execution);
        Check(bc.IssueGhostArmy(1, 1).ok(), "ghosts raised");
        for (int i = 0; i < SHRINE_DEDICATION_TICKS + 2; ++i) bc.Tick();
        Check(CountKind(bc.Events(), SimEvent::Kind::ShrineCaptured) ==
                  0,
              "spirit vs flesh contests the shrine");
        Check(bc.Myth().ShrineAt(1)->owner == -1, "no latch");
    }

    // --- Record -> replay: myth actions ride the command journal ---
    {
        BattleRecorder rec;
        BattleController bc(6, fx.map, fx.cards);
        rec.Bind(6, fx.md, fx.cd);
        SquadTemplate a = Mk("ally"), f = Mk("foe");
        bc.DeploySquad(a, 0, 0); rec.RecordDeploy(a, 0, 0);
        bc.DeploySquad(f, 3, 1); rec.RecordDeploy(f, 3, 1);
        Check(bc.SeedInfiltration(1, 1), "seed r1 whispered");
        rec.RecordMythSeed(1, 1);
        Check(bc.SeedInfiltration(2, 2), "seed r2 haunted");
        rec.RecordMythSeed(2, 2);
        bc.RequestBeat(BattleBeat::Execution);
        Check(bc.IssueMythPossession(0, 0).ok(), "possess ally");
        Check(bc.IssueGhostArmy(0, 1).ok(), "ghosts to r1");
        Check(bc.IssueMythPacify(0, 2).ok(), "pacify r2");
        for (int i = 0; i < SHRINE_DEDICATION_TICKS + 5; ++i) bc.Tick();
        Check(CountKind(bc.Events(),
                        SimEvent::Kind::MythActionInvoked) == 3,
              "three acts invoked");
        bc.RequestBeat(BattleBeat::Aftermath);
        rec.Seal(bc);
        auto doc = rec.ToJson();
        Check(doc.ok(), "myth-action record sealed");
        if (doc.ok()) {
            auto r = Replay::Verify(doc.value);
            Check(r.ok() && r.value.ok,
                  "myth actions replay bit-exact");
        }
    }

    // ================= Story 5.5: invasion events =================

    // --- Occupied level-3 shrine: the god pushes back on the
    //     occupier's banner, immediately on arrival at 3 ---
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("ally"), 1, 0); // holds the shrine
        bc.DeploySquad(Mk("foe"), 3, 1);  // parked
        Check(bc.SeedInfiltration(1, 3), "seeded r1 to Invaded");
        bc.RequestBeat(BattleBeat::Execution);
        bc.Tick(); // invasionTick starts a full period in the past
        const SimEvent* inv =
            FindKind(bc.Events(), SimEvent::Kind::MythInvasion);
        Check(inv && inv->param == 1 && inv->aux == 0 &&
                  inv->side == 1 && inv->squadIndex == -1,
              "pushback host rises for the occupier's enemy");
        Check(bc.Myth().GhostAt(1) == 1, "ghost garrison stands");
        // The rising host contests this very tick — the occupier's
        // dwell can never begin while the god's banner opposes him.
        for (int i = 0; i < SHRINE_DEDICATION_TICKS + 4; ++i) {
            bc.Tick();
        }
        Check(CountKind(bc.Events(), SimEvent::Kind::ShrineCaptured) ==
                  0,
              "invaded shrine never dedicates under pushback");
        Check(bc.Myth().ShrineAt(1)->owner == -1,
              "allegiance stays unlatched");
    }

    // --- Empty/contested ground: wild haunting denies every claim ---
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("ally"), 0, 0); // adjacent, not on it
        bc.DeploySquad(Mk("foe"), 3, 1);
        Check(bc.SeedInfiltration(1, 3), "seeded r1 to Invaded");
        bc.RequestBeat(BattleBeat::Execution);
        bc.Tick();
        const SimEvent* inv =
            FindKind(bc.Events(), SimEvent::Kind::MythInvasion);
        Check(inv && inv->aux == 1 && inv->side == -1,
              "unheld invaded shrine goes wild");
        Check(bc.Myth().GhostAt(1) == GHOST_WILD,
              "wild garrison marker set");
        // No banner can garrison a wild haunting — the myth layer
        // itself holds the ground.
        auto g = bc.IssueGhostArmy(0, 1);
        Check(!g.ok() || !g.value,
              "cannot raise ghosts onto a wild haunting");
    }

    // --- Cadence: sustain beats every INVASION_PERIOD_TICKS ---
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("ally"), 0, 0);
        bc.DeploySquad(Mk("foe"), 3, 1);
        bc.SeedInfiltration(1, 3);
        bc.RequestBeat(BattleBeat::Execution);
        for (int i = 0; i < INVASION_PERIOD_TICKS + 2; ++i) {
            bc.Tick();
        }
        const SimEvent* second = FindKind(
            bc.Events(), SimEvent::Kind::MythInvasion, 1);
        Check(CountKind(bc.Events(), SimEvent::Kind::MythInvasion) ==
                  2,
              "first fire then one cadence sustain");
        Check(second && second->aux == 2 && second->side == -1 &&
                  second->tick == INVASION_PERIOD_TICKS,
              "sustain beat carries the standing host's banner");
    }

    // --- Level-2 shrines never invade ---
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("ally"), 1, 0);
        bc.DeploySquad(Mk("foe"), 3, 1);
        bc.SeedInfiltration(1, 2); // Haunted, not Invaded
        bc.RequestBeat(BattleBeat::Execution);
        for (int i = 0; i < INVASION_PERIOD_TICKS + 4; ++i) {
            bc.Tick();
        }
        Check(CountKind(bc.Events(), SimEvent::Kind::MythInvasion) ==
                  0,
              "haunted (not invaded) shrine stays quiet");
    }

    // --- The counter-play loop: pacify subsides the wild haunting,
    //     dedication reopens ---
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.DeploySquad(Mk("ally"), 1, 0); // on the shrine
        bc.DeploySquad(Mk("foe"), 3, 1);
        bc.SeedInfiltration(1, 3);
        bc.RequestBeat(BattleBeat::Execution);
        bc.Tick(); // pushback host (aux 0, side 1) rises
        Check(bc.Myth().GhostAt(1) == 1,
              "pushback host present before pacify");
        // Pacify drops the level 3->2 — but a SIDE-bannered host is
        // contracted, not wild: it does not dissipate.
        auto p = bc.IssueMythPacify(0, 1);
        Check(p.ok() && p.value, "pacify issued against invasion");
        bc.Tick();
        Check(bc.Myth().LevelAt(1) == IL::Haunted, "level backed off");
        Check(bc.Myth().GhostAt(1) == 1,
              "bannered host does not dissipate on pacify");
    }

    // --- Wild haunting subsides the tick the level drops ---
    {
        MythField f;
        f.Init(fx.map);
        Check(f.Seed(1, IL::Invaded), "field seeded to 3");
        std::vector<Squad> squads; // empty ground
        auto evs = f.Tick(squads, 0);
        Check(evs.size() == 1 &&
                  evs[0].kind == SimEvent::Kind::MythInvasion &&
                  evs[0].aux == 1,
              "wild haunting fires on empty invaded shrine");
        Check(f.GhostAt(1) == GHOST_WILD, "wild marker stands");
        f.Apply(1, MK::Pacification, 1); // -> Haunted
        f.Tick(squads, 1);
        Check(f.GhostAt(1) == -1,
              "wild haunting subsides below Invaded");
    }

    // --- e2e: a sealed record carrying MythInvasion verifies ---
    {
        BattleRecorder rec;
        BattleController bc(6, fx.map, fx.cards);
        rec.Bind(6, fx.md, fx.cd);
        SquadTemplate a = Mk("ally"), f = Mk("foe");
        bc.DeploySquad(a, 1, 0); rec.RecordDeploy(a, 1, 0);
        bc.DeploySquad(f, 3, 1); rec.RecordDeploy(f, 3, 1);
        Check(bc.SeedInfiltration(1, 3), "seeded r1");
        rec.RecordMythSeed(1, 3);
        bc.RequestBeat(BattleBeat::Execution);
        for (int i = 0; i < INVASION_PERIOD_TICKS + 4; ++i) {
            bc.Tick();
        }
        Check(CountKind(bc.Events(), SimEvent::Kind::MythInvasion) >=
                  2,
              "record: rise + sustain sealed");
        bc.RequestBeat(BattleBeat::Aftermath);
        rec.Seal(bc);
        auto doc = rec.ToJson();
        Check(doc.ok(), "invasion record sealed");
        if (doc.ok()) {
            auto r = Replay::Verify(doc.value);
            Check(r.ok() && r.value.ok,
                  "MythInvasion record replays bit-exact");
        }
    }

    // ================= Story 5.7: GodStance gates =================

    // --- A wrathful deity refuses both hosts and dead-lending;
    //     the favored side's verbs pass on the same ground ---
    {
        BattleController bc(1, fx.map, fx.cards);
        bc.SeedInfiltration(1, 1); // Whispered — veil thin enough
        bc.DeploySquad(Mk("ally"), 0, 0); // parked off the shrine
        bc.DeploySquad(Mk("foe"), 1, 1);  // claims it
        bc.RequestBeat(BattleBeat::Execution);
        for (int i = 0; i < SHRINE_DEDICATION_TICKS; ++i) {
            bc.Tick();
        }
        Check(bc.Myth().ShrineAt(1)->owner == 1 &&
                  bc.Myth().StanceAt(1, 0) == GodStance::Wrathful &&
                  bc.Myth().StanceAt(1, 1) == GodStance::Favorable,
              "enemy dedication makes the god wrathful to us");
        auto g = bc.IssueGhostArmy(0, 1);
        Check(!g.ok(), "5.7: wrathful god lends no dead to us");
        auto gf = bc.IssueGhostArmy(1, 1);
        Check(gf.ok() && gf.value,
              "5.7: favored side raises the god's dead");
        // Possession on wrathful ground refuses — walk the ally
        // onto the contested shrine (foe still holds it; stance
        // persists through the latch release).
        auto mv = bc.IssueRedirect(0, 0, 1);
        Check(mv.ok() && mv.value, "ally marches onto the shrine");
        for (int i = 0; i < 60; ++i) bc.Tick();
        Check(bc.Squads()[0].regionIndex == 1,
              "ally stands on the wrathful shrine");
        auto p = bc.IssueMythPossession(0, 0);
        Check(!p.ok(), "5.7: the god refuses a host on his "
                       "wrathful ground");
        // Neutral ground is unaffected — non-shrine regions carry
        // no deity, so the only rejection left is the veil itself.
        Check(bc.Myth().StanceAt(2, 0) == GodStance::Neutral,
              "non-shrine ground is stance-neutral");
        auto g2 = bc.IssueGhostArmy(0, 2);
        Check(!g2.ok() && g2.reason.find("too quiet") !=
                              std::string::npos,
              "5.7: non-shrine rejection is the veil, not the mood");
    }

    std::printf("%s (%d failures)\n",
                failures == 0 ? "ALL PASS" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
