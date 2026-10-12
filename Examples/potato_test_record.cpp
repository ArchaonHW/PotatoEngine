#include "Gameplay/Record/BattleRecorder.h"
#include "Gameplay/Record/ReplayVerifier.h"
#include "Gameplay/Doctrine/Doctrine.h"
#include "Gameplay/Fog/QuantumFog.h"
#include "Gameplay/Map/BattleMap.h"
#include "Gameplay/Plan/BattlePlan.h"
#include "Gameplay/Sim/BattleController.h"
#include "Gameplay/Squad/Squad.h"
#include "Gameplay/Json/JsonValue.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
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

SquadTemplate Mk(const char* id, int hp, int atk) {
    SquadTemplate t; t.id = id; t.name = id; t.hp = hp;
    t.attack = atk; t.speed = Speed::VeryFast; t.cohesion = 100;
    return t;
}

// A scripted battle: deploys, sheets, arrows (incl. an asymmetric
// side/squad RecordArrow call), intel, and Execution commands
// (redirect + probe + replan) — sealed after a FORCED close.
std::string RunAndRecord(BattleRecorder& rec) {
    auto md = JsonValue::Parse(MAP_DOC).value;
    auto cd = JsonValue::Parse(CARDS_DOC).value;
    BattleMap map = BattleMap::FromJson(md).value;
    DoctrineLibrary cards = DoctrineLibrary::FromJson(cd).value;

    BattleController bc(42, map, cards);
    rec.Bind(42, md, cd);

    SquadTemplate a = Mk("alpha", 200, 15);
    SquadTemplate b = Mk("bravo", 200, 15);
    SquadTemplate c = Mk("charlie", 200, 15);

    bc.DeploySquad(a, 0, 0); rec.RecordDeploy(a, 0, 0);
    bc.DeploySquad(b, 3, 1); rec.RecordDeploy(b, 3, 1);
    bc.DeploySquad(c, 1, 0); rec.RecordDeploy(c, 1, 0); // squad 2, side 0

    const std::vector<std::string> sheet =
        {"card_idle", "card_wait", "card_rest"};
    for (int i = 0; i < 3; ++i) {
        bc.SetSheet(i, SquadSheet::Build(cards, sheet).value);
        rec.RecordSheet(i, sheet);
    }

    bc.DrawArrow(0, 0, {0, 1, 2}); rec.RecordArrow(0, 0, {0, 1, 2});
    // ASYMMETRIC args: side 0 / squad 2 — pins the (side, squad) order.
    bc.DrawArrow(0, 2, {1, 2});    rec.RecordArrow(0, 2, {1, 2});
    bc.SetCloudIntel(0, 1, 2, 70); rec.RecordIntel(0, 1, 2, 70);
    bc.SetCpPool(0, 5);            rec.RecordCpPool(0, 5);

    bc.RequestBeat(BattleBeat::Execution);
    bc.IssueRedirect(0, 2, 2);       // stamped 0, applies tick 0
    bc.Tick();                       // tick 0
    bc.IssueReplan(0, 0, {1, 2, 3}); // stamped 1, applies tick 1
    bc.Tick();                       // tick 1
    bc.IssueProbe(0, 2);             // stamped 2, applies tick 2
    bc.Tick();                       // tick 2
    for (int i = 0; i < 8; ++i) bc.Tick(); // ticks 3..10
    bc.RequestBeat(BattleBeat::Aftermath); // forced close at tick 11
    rec.Seal(bc);

    auto doc = rec.ToJson();
    return doc.ok() ? doc.value.Emit() : std::string{};
}

// Natural-termination battle: side 1 fields nothing — first tick wipes
// it and the battle closes itself (forced=false path).
std::string RunAndRecordWipe(BattleRecorder& rec) {
    auto md = JsonValue::Parse(MAP_DOC).value;
    auto cd = JsonValue::Parse(CARDS_DOC).value;
    BattleMap map = BattleMap::FromJson(md).value;
    DoctrineLibrary cards = DoctrineLibrary::FromJson(cd).value;

    BattleController bc(7, map, cards);
    rec.Bind(7, md, cd);
    SquadTemplate a = Mk("alpha", 200, 15);
    bc.DeploySquad(a, 0, 0); rec.RecordDeploy(a, 0, 0);
    bc.RequestBeat(BattleBeat::Execution);
    bc.Tick(); // tick 0 — side 1 wiped, auto-close
    rec.Seal(bc);
    auto doc = rec.ToJson();
    return doc.ok() ? doc.value.Emit() : std::string{};
}

// Stalemate battle: EvalConfig baked into a bound balance doc — the
// verifier must reconstruct the timer from the record to replay it.
std::string RunAndRecordStalemate(BattleRecorder& rec, bool bindBalance) {
    auto md = JsonValue::Parse(MAP_DOC).value;
    auto cd = JsonValue::Parse(CARDS_DOC).value;
    const char* BAL =
        R"({"schema":"potato.balance/1","eval":{"stalemate_ticks":10}})";
    auto bd = JsonValue::Parse(BAL).value;
    BattleMap map = BattleMap::FromJson(md).value;
    DoctrineLibrary cards = DoctrineLibrary::FromJson(cd).value;
    EvalConfig cfg = EvalConfig::FromJson(bd).value;

    BattleController bc(11, map, cards, FogConfig{}, PlanConfig{}, cfg);
    rec.Bind(11, md, cd);
    if (bindBalance) rec.BindBalance(bd);
    SquadTemplate a = Mk("alpha", 200, 15), b = Mk("bravo", 200, 15);
    bc.DeploySquad(a, 0, 0); rec.RecordDeploy(a, 0, 0);
    bc.DeploySquad(b, 3, 1); rec.RecordDeploy(b, 3, 1);
    bc.RequestBeat(BattleBeat::Execution);
    while (bc.Tick()) {} // stalemate fires at tick 10
    rec.Seal(bc);
    auto doc = rec.ToJson();
    return doc.ok() ? doc.value.Emit() : std::string{};
}

// Governance record: a convoy marches into a raider's camp — the
// "convoy" planning op + ConvoyRaided event (kind 7, the new
// governance vocabulary) must replay bit-exact.
std::string RunAndRecordConvoy(BattleRecorder& rec) {
    auto md = JsonValue::Parse(MAP_DOC).value;
    auto cd = JsonValue::Parse(CARDS_DOC).value;
    BattleMap map = BattleMap::FromJson(md).value;
    DoctrineLibrary cards = DoctrineLibrary::FromJson(cd).value;

    BattleController bc(5, map, cards);
    rec.Bind(5, md, cd);
    SquadTemplate a = Mk("ally", 200, 15), f = Mk("raider", 200, 15);
    bc.DeploySquad(a, 0, 0); rec.RecordDeploy(a, 0, 0);
    bc.DeploySquad(f, 1, 1); rec.RecordDeploy(f, 1, 1);
    bc.SpawnConvoy(0, {0, 1, 2});
    rec.RecordConvoy(0, {0, 1, 2});
    bc.RequestBeat(BattleBeat::Execution);
    for (int i = 0; i < 70 && bc.Tick(); ++i) {} // raided at r1
    bc.RequestBeat(BattleBeat::Aftermath);
    rec.Seal(bc);
    auto doc = rec.ToJson();
    return doc.ok() ? doc.value.Emit() : std::string{};
}

// Execute record: a rout plea refused — the Execute intervention
// (journal op aux=6) and the SquadExecuted deed (kind 8) must replay
// bit-exact.
std::string RunAndRecordExecute(BattleRecorder& rec) {
    auto md = JsonValue::Parse(MAP_DOC).value;
    auto cd = JsonValue::Parse(CARDS_DOC).value;
    BattleMap map = BattleMap::FromJson(md).value;
    DoctrineLibrary cards = DoctrineLibrary::FromJson(cd).value;

    BattleController bc(9, map, cards);
    rec.Bind(9, md, cd);
    SquadTemplate a = Mk("ally", 200, 15), f = Mk("foe", 200, 15);
    bc.DeploySquad(a, 1, 0); rec.RecordDeploy(a, 1, 0);
    bc.DeploySquad(f, 1, 1); rec.RecordDeploy(f, 1, 1);
    bc.RequestBeat(BattleBeat::Execution);
    bc.IssueRetreat(1, 1);  // stamped 0, applies tick 0 -> Routing
    bc.Tick();
    bc.IssueExecute(0, 1);  // stamped 1, applies tick 1 -> Destroyed
    bc.Tick();              // execute lands; wipe auto-closes
    rec.Seal(bc);
    auto doc = rec.ToJson();
    return doc.ok() ? doc.value.Emit() : std::string{};
}

// Recompute the integrity root over a (tampered) payload so the record
// passes the byte-level seal — forcing the verifier onto the semantic
// replay path. NOTE: ComputeRoot is public FNV — edit detection only.
JsonValue Reseal(JsonValue doc) {
    JsonValue::Object payload = doc.Members();
    payload.erase("integrity");
    const std::uint64_t root =
        BattleRecorder::ComputeRoot(JsonValue::MakeObject(payload));
    JsonValue::Object o = doc.Members();
    JsonValue::Object integrity;
    integrity.emplace("root", JsonValue::Int(
                                  static_cast<std::int64_t>(root)));
    o["integrity"] = JsonValue::MakeObject(std::move(integrity));
    return JsonValue::MakeObject(std::move(o));
}

} // namespace

int main(int argc, char** argv) {
    BattleRecorder rec;
    const std::string text = RunAndRecord(rec);
    // --emit <path>: write the sealed record for manual CLI verification
    // (potato_verify <path>) and the potato_verify_cli ctest.
    if (argc == 3 && std::string(argv[1]) == "--emit") {
        std::ofstream out(argv[2], std::ios::binary | std::ios::trunc);
        out << text;
        std::printf("emitted %s (%zu bytes)\n", argv[2], text.size());
    }
    Check(!text.empty(), "record emitted");
    if (text.empty()) return 1;
    auto doc = JsonValue::Parse(text);
    Check(doc.ok(), "record re-parses");
    if (!doc.ok()) return 1;

    // --- Canonical emit: round-trip byte-stability + type stability ---
    {
        auto re = JsonValue::Parse(text);
        Check(re.ok() && re.value.Emit() == text,
              "emit is canonical: parse(emit(x)) == emit(x)");
        auto unsorted = JsonValue::Parse(R"({"z":1,"a":2})");
        Check(unsorted.ok() && unsorted.value.Emit() == R"({"a":2,"z":1})",
              "emit sorts object keys");
        auto esc = JsonValue::Parse(
            R"({"a":[1,"x\"y\n",-5,true,null],"z":{"k":2.5}})");
        Check(esc.ok() &&
                  esc.value.Emit() ==
                      R"({"a":[1,"x\"y\n",-5,true,null],"z":{"k":2.5}})",
              "emit escapes + preserves values");
        JsonValue ints = JsonValue::MakeArray(
            {JsonValue::Int(0), JsonValue::Int(-1),
             JsonValue::Int(-9223372036854775807LL - 1)});
        Check(ints.Emit() == "[0,-1,-9223372036854775808]",
              "emit int64 min exact");
        // Real stays Real through parse∘emit — integral-looking reals
        // keep a type marker; non-finite can't be JSON at all.
        Check(JsonValue::Real(1.0).Emit() == "1.0", "integral real keeps .0");
        Check(JsonValue::Real(-0.0).Emit() == "-0.0", "-0.0 is a fixpoint");
        auto back = JsonValue::Parse(JsonValue::Real(1.0).Emit());
        Check(back.ok() && back.value.IsReal(), "1.0 reparses as Real");
        Check(JsonValue::Real(std::numeric_limits<double>::quiet_NaN())
                  .Emit() == "null",
              "non-finite real emits null (lossy, valid JSON)");
    }

    // --- Happy path: verify the sealed record ---
    {
        auto r = Replay::Verify(doc.value);
        Check(r.ok() && r.value.ok, "clean record verifies");
        Check(r.ok() && !r.value.tampered && !r.value.downgrade,
              "clean record: no flags");
    }

    // --- Natural-termination (side-wipe) record verifies ---
    {
        BattleRecorder wrec;
        const std::string wtext = RunAndRecordWipe(wrec);
        auto wdoc = JsonValue::Parse(wtext);
        Check(wdoc.ok(), "wipe record emitted");
        auto r = wdoc.ok() ? Replay::Verify(wdoc.value)
                           : Fail<VerifyResult>("t", "parse");
        Check(r.ok() && r.value.ok, "natural-close record verifies");
        Check(wdoc.ok() && !wdoc.value["forced"].AsBool(true),
              "wipe record is not forced");
    }

    // --- Stalemate record: embedded eval config drives replay ---
    {
        BattleRecorder srec;
        const std::string stext = RunAndRecordStalemate(srec, true);
        auto sdoc = JsonValue::Parse(stext);
        Check(sdoc.ok(), "stalemate record emitted");
        auto r = sdoc.ok() ? Replay::Verify(sdoc.value)
                           : Fail<VerifyResult>("t", "parse");
        Check(r.ok() && r.value.ok,
              "stalemate record verifies (eval cfg embedded)");
        Check(sdoc.ok() && sdoc.value["stalemate"].AsBool() &&
                  sdoc.value["closeReason"].AsInt() ==
                      static_cast<int>(CloseReason::Stalemate),
              "stalemate record fields");
        // Same battle without the balance doc cannot verify — the
        // default 6-minute timer never fires inside endTick.
        BattleRecorder urec;
        const std::string utext = RunAndRecordStalemate(urec, false);
        auto udoc = JsonValue::Parse(utext);
        auto r2 = udoc.ok() ? Replay::Verify(udoc.value)
                            : Fail<VerifyResult>("t", "parse");
        Check(r2.ok() && !r2.value.ok,
              "missing eval config -> replay diverges, rejected");
    }

    // --- Governance record: convoy op + raid event verify ---
    {
        BattleRecorder crec;
        const std::string ctext = RunAndRecordConvoy(crec);
        auto cdoc = JsonValue::Parse(ctext);
        Check(cdoc.ok(), "convoy record emitted");
        if (cdoc.ok()) {
            bool sawRaid = false;
            for (const JsonValue& e : cdoc.value["events"].Items()) {
                if (e["kind"].AsInt() ==
                    static_cast<std::int64_t>(SimEvent::Kind::ConvoyRaided)) {
                    sawRaid = true;
                }
            }
            Check(sawRaid, "convoy record carries a ConvoyRaided event");
            auto r = Replay::Verify(cdoc.value);
            Check(r.ok() && r.value.ok,
                  "convoy record verifies (op + new event kind)");
        }
    }

    // --- Refused-surrender record: Execute + SquadExecuted verify ---
    {
        BattleRecorder xrec;
        const std::string xtext = RunAndRecordExecute(xrec);
        auto xdoc = JsonValue::Parse(xtext);
        Check(xdoc.ok(), "execute record emitted");
        if (xdoc.ok()) {
            bool sawDeed = false, sawCmd = false;
            for (const JsonValue& e : xdoc.value["events"].Items()) {
                const std::int64_t k = e["kind"].AsInt();
                if (k == static_cast<std::int64_t>(
                             SimEvent::Kind::SquadExecuted)) {
                    sawDeed = true;
                }
                if (k == static_cast<std::int64_t>(
                             SimEvent::Kind::Intervention) &&
                    e["aux"].AsInt() == 6) {
                    sawCmd = true;
                }
            }
            Check(sawDeed && sawCmd,
                  "record carries the refusal command + deed");
            auto r = Replay::Verify(xdoc.value);
            Check(r.ok() && r.value.ok,
                  "execute record verifies end-to-end");
        }
    }

    // --- Tamper matrix: byte-level edits trip the root ---
    {
        JsonValue::Object t = doc.value.Members();
        t["seed"] = JsonValue::Int(43); // different seed
        JsonValue tampered = JsonValue::MakeObject(t);
        auto r = Replay::Verify(tampered);
        Check(r.ok() && !r.value.ok && r.value.tampered,
              "seed edit -> tampered");
    }
    {
        // Root-consistent tamper: flip the Redirect command's target —
        // replay must catch the semantic divergence.
        JsonValue::Object t = doc.value.Members();
        JsonValue::Array ev = t["events"].Items();
        bool found = false;
        for (JsonValue& e : ev) {
            if (e["aux"].AsInt() == 0) { // the Redirect event
                JsonValue::Object eo = e.Members();
                eo["param"] = JsonValue::Int(3); // r2 -> r3: not adjacent
                e = JsonValue::MakeObject(std::move(eo));
                found = true;
            }
        }
        Check(found, "recorded redirect event found");
        t["events"] = JsonValue::MakeArray(std::move(ev));
        JsonValue forged = Reseal(JsonValue::MakeObject(std::move(t)));
        auto r = Replay::Verify(forged);
        Check(r.ok() && !r.value.ok && !r.value.tampered,
              "redirect-target flip (resealed) -> replay rejects");
    }
    {
        JsonValue::Object t = doc.value.Members();
        t["checksum"] = JsonValue::Int(1);
        auto r = Replay::Verify(Reseal(JsonValue::MakeObject(std::move(t))));
        Check(r.ok() && !r.value.ok,
              "forged checksum -> replay mismatch");
    }
    {
        JsonValue::Object t = doc.value.Members();
        JsonValue::Array ev = t["events"].Items();
        if (!ev.empty()) ev.pop_back(); // drop the closing ResultDeclared
        t["events"] = JsonValue::MakeArray(std::move(ev));
        auto r = Replay::Verify(Reseal(JsonValue::MakeObject(std::move(t))));
        Check(r.ok() && !r.value.ok, "truncated stream -> reject");
    }
    {
        JsonValue::Object t = doc.value.Members();
        t["schema"] = JsonValue::String("potato.battle_record/2");
        auto r = Replay::Verify(Reseal(JsonValue::MakeObject(std::move(t))));
        Check(r.ok() && !r.value.ok, "newer schema -> reject");
    }
    {
        JsonValue::Object t = doc.value.Members();
        t["toolVersion"] = JsonValue::Int(0);
        auto r = Replay::Verify(Reseal(JsonValue::MakeObject(std::move(t))));
        Check(r.ok() && r.value.ok && r.value.downgrade,
              "older toolVersion -> verify + downgrade warning");
    }
    {
        JsonValue::Object t = doc.value.Members();
        t["toolVersion"] = JsonValue::Int(99);
        auto r = Replay::Verify(Reseal(JsonValue::MakeObject(std::move(t))));
        Check(r.ok() && !r.value.ok, "unrecognized toolVersion -> reject");
    }
    {
        JsonValue::Object t = doc.value.Members();
        t.erase("integrity");
        auto r = Replay::Verify(JsonValue::MakeObject(std::move(t)));
        Check(r.ok() && !r.value.ok, "missing integrity -> reject");
    }
    {
        JsonValue::Object t = doc.value.Members();
        t.erase("endTick");
        auto r = Replay::Verify(Reseal(JsonValue::MakeObject(std::move(t))));
        Check(r.ok() && !r.value.ok, "missing endTick -> reject");
    }

    // --- File round-trip: emit -> temp file -> VerifyFile (CLI path) ---
    {
        const auto tmp = std::filesystem::temp_directory_path() /
                         ("potato_rec_" + std::to_string(
                             std::chrono::steady_clock::now()
                                 .time_since_epoch().count()) + ".json");
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            out << text;
            if (!out.good()) {
                Check(false, "temp record file written");
            } else {
                Check(true, "temp record file written");
            }
        }
        auto r = Replay::VerifyFile(tmp.string());
        Check(r.ok() && r.value.ok, "record file verifies via VerifyFile");
        std::error_code ec;
        std::filesystem::remove(tmp, ec);
    }

    // --- Malformed input ---
    {
        auto r = Replay::Verify(JsonValue::Int(5));
        Check(r.ok() && !r.value.ok, "non-object root rejected");
        auto f = Replay::VerifyFile("definitely/missing/file.json");
        Check(!f.ok(), "missing file -> error");
    }

    // --- Recorder lifecycle guards ---
    {
        BattleRecorder bare;
        Check(!bare.ToJson().ok(), "unsealed ToJson fails");
        // Mid-battle Seal is a no-op — records are completed battles.
        auto md = JsonValue::Parse(MAP_DOC).value;
        auto cd = JsonValue::Parse(CARDS_DOC).value;
        BattleMap map = BattleMap::FromJson(md).value;
        DoctrineLibrary cards = DoctrineLibrary::FromJson(cd).value;
        BattleController bc(1, map, cards);
        bare.Bind(1, md, cd);
        bare.Seal(bc); // still Planning — must not seal
        Check(!bare.ToJson().ok(), "Seal outside Aftermath rejected");
        // Rebinding after a seal resets all per-battle state.
        BattleRecorder rec2;
        RunAndRecord(rec2); // sealed
        BattleController fresh(9, map, cards);
        rec2.Bind(9, md, cd); // resets sealed_/events_/inputs_
        rec2.Seal(fresh);     // Planning — no-op
        Check(!rec2.ToJson().ok(), "rebind clears sealed state");
    }

    std::printf(failures ? "RECORD TESTS FAILED: %d\n" : "RECORD TESTS PASS\n",
                failures);
    return failures ? 1 : 0;
}
