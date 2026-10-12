// Story 12.1 — WorldMap schema + WorldLibrary registry
// (potato.world/1): node/POI graph with dual-layer flag marks,
// faction control, march routes with day cost, start anchor;
// per-file-isolated dir-scan registry (ChapterLibrary precedent).
// Story 12.2 — WorldState day-beat resolution (potato.worldstate/1):
// Init seeds control/warband/start, canonical ordered event queue,
// dedicated SplitMix64 stream persisted via wire.
// Story 12.3 — warband movement: march orders post supply to the
// ledger and queue arrival; sightings are hearsay-only markers.
#include "Campaign/Chapters/ChapterLibrary.h"
#include "Campaign/Governance/Accumulators.h"
#include "Campaign/Ledger/DeedBook.h"
#include "Campaign/Ledger/Ledger.h"
#include "Campaign/Myth/MythState.h"
#include "Campaign/World/March.h"
#include "Campaign/World/RegionBind.h"
#include "Campaign/World/WorldMap.h"
#include "Campaign/World/WorldState.h"
#include "Gameplay/Json/JsonValue.h"
#include "Gameplay/Map/BattleMap.h" // flag bit constants

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

int failures = 0;
void Check(bool cond, const char* name) {
    if (cond) {
        std::printf("[PASS] %s\n", name);
    } else {
        std::printf("[FAIL] %s\n", name);
        ++failures;
    }
}

using Potato::Campaign::Account;
using Potato::Campaign::IssueMarch;
using Potato::Campaign::IssueSighting;
using Potato::Campaign::Ledger;
using Potato::Campaign::MarchPlan;
using Potato::Campaign::Posting;
using Potato::Campaign::WorldControl;
using Potato::Campaign::WorldEvent;
using Potato::Campaign::WorldEventKind;
using Potato::Campaign::WorldLibrary;
using Potato::Campaign::WorldMap;
using Potato::Campaign::WorldState;
using Potato::Gameplay::JsonValue;
using Potato::Gameplay::HIST_GRAIN_ROUTE;
using Potato::Gameplay::MYTH_SHRINE;
using Potato::Gameplay::STRATEGIC_DEPOT;
using Potato::Gameplay::TERRAIN_HIGHLAND;
using Potato::Gameplay::TERRAIN_OPEN;
using Potato::Gameplay::TERRAIN_ROAD;

const char* WORLD_DOC = R"json({
    "schema": "potato.world/1",
    "id": "republic_fall",
    "name": "河山殘卷",
    "nodes": [
        {"id": "kaifeng", "name": "開封",
         "terrain": ["road", "open"], "strategic": ["depot"],
         "historical": ["grain_route"],
         "control": "player", "map": "ch01_plain",
         "center": [12, 8]},
        {"id": "longmen", "name": "龍門",
         "terrain": ["highland"], "myth": ["shrine"],
         "control": "neutral"},
        {"id": "luoyang",
         "control": "rival"}
    ],
    "routes": [
        {"a": "kaifeng", "b": "longmen", "days": 2},
        {"a": "longmen", "b": "luoyang", "days": 3}
    ],
    "start": "kaifeng"
})json";

const char* WORLD_DOC2 = R"json({
    "schema": "potato.world/1",
    "id": "countermarch",
    "nodes": [{"id": "x", "control": "rival"}],
    "start": "x"
})json";

bool HasNeighbor(const WorldMap& m, std::size_t from,
                 std::size_t to, std::int64_t days) {
    for (const auto& [idx, d] : m.Neighbors(from)) {
        if (idx == to && d == days) return true;
    }
    return false;
}

void WriteFile(const fs::path& dir, const char* name,
               const char* text) {
    std::ofstream out(dir / name, std::ios::binary);
    out << text;
}

} // namespace

int main() {
    // --- FromJson: full model decode ---
    auto doc = JsonValue::Parse(WORLD_DOC);
    Check(doc.ok(), "fixture parses");
    auto w = WorldMap::FromJson(doc.value);
    Check(w.ok(), "potato.world/1 FromJson loads");
    if (w.ok()) {
        const WorldMap& m = w.value;
        Check(m.Id() == "republic_fall" && m.Name() == "河山殘卷",
              "world id + CJK name");
        Check(m.NodeCount() == 3, "3 nodes");

        const auto& kf = m.NodeAt(0);
        Check(kf.id == "kaifeng" && kf.name == "開封",
              "node id + CJK name");
        Check((kf.terrain & (TERRAIN_ROAD | TERRAIN_OPEN)) ==
              (TERRAIN_ROAD | TERRAIN_OPEN), "terrain bits decoded");
        Check((kf.strategic & STRATEGIC_DEPOT) != 0,
              "strategic depot flag");
        Check((kf.historical & HIST_GRAIN_ROUTE) != 0,
              "historical grain_route mark");
        Check(kf.control == WorldControl::Player,
              "control decodes to Player");
        Check(kf.map == "ch01_plain", "battle-map ref kept");
        Check(kf.hasCenter && kf.centerX == 12 && kf.centerY == 8,
              "optional center decoded");

        const auto& lm = m.NodeAt(1);
        Check((lm.myth & MYTH_SHRINE) != 0,
              "myth shrine mark (dual-layer)");
        Check(lm.control == WorldControl::Neutral, "neutral control");
        Check(!lm.hasCenter, "absent center stays unset");

        Check(m.NodeAt(2).control == WorldControl::Rival,
              "rival control");

        const auto* found = m.FindNode("longmen");
        Check(found != nullptr && (found->myth & MYTH_SHRINE) != 0,
              "FindNode by id");
        Check(m.FindNode("nowhere") == nullptr,
              "FindNode miss -> nullptr");
        Check(m.NodeIndexOf("luoyang") == 2, "NodeIndexOf file order");
        Check(m.NodeIndexOf("nowhere") == WorldMap::NO_NODE,
              "NodeIndexOf miss");

        // Undirected routes carry the day cost both ways, in
        // declaration order.
        Check(HasNeighbor(m, 0, 1, 2), "route kaifeng->longmen 2d");
        Check(HasNeighbor(m, 1, 0, 2), "route longmen->kaifeng 2d");
        Check(HasNeighbor(m, 1, 2, 3) && HasNeighbor(m, 2, 1, 3),
              "second route symmetric");
        Check(m.Neighbors(0).size() == 1 &&
                  m.Neighbors(1).size() == 2 &&
                  m.Neighbors(2).size() == 1,
              "neighbor counts correct");
        const auto& nb = m.Neighbors(1);
        Check(nb[0].first == 0 && nb[1].first == 2,
              "neighbor order = route declaration order");

        Check(m.StartIndex() == 0, "start node resolves");
    }

    // Default-constructed map is a safe empty value.
    {
        WorldMap empty;
        Check(empty.NodeCount() == 0 && empty.FindNode("x") == nullptr &&
                  empty.NodeIndexOf("x") == WorldMap::NO_NODE,
              "default WorldMap is empty-safe");
    }

    // --- Validation rejects ---
    auto reject = [](const char* json, const char* name) {
        auto d = JsonValue::Parse(json);
        if (!d.ok()) { Check(false, name); return; } // fixture typo
        auto m = WorldMap::FromJson(d.value);
        Check(!m.ok() && !m.error.empty() && !m.reason.empty() &&
                  m.value.NodeCount() == 0,
              name); // failure purity: no partial map
    };
    reject("[]", "non-object root rejected");
    reject(R"({"schema":"potato.world/2","id":"w","nodes":[{"id":"a"}],"start":"a"})",
           "FromJson rejects mismatched schema tag");
    reject(R"({"nodes":[{"id":"a"}],"start":"a"})", "missing world id");
    reject(R"({"id":5,"nodes":[{"id":"a"}],"start":"a"})",
           "non-string world id rejected");
    reject(R"({"id":"w","name":7,"nodes":[{"id":"a"}],"start":"a"})",
           "non-string world name rejected");
    reject(R"({"id":"w","nodes":{},"start":"a"})",
           "non-array nodes rejected");
    reject(R"({"id":"w","nodes":[],"start":"a"})",
           "empty nodes rejected");
    reject(R"({"id":"w","nodes":[5],"start":"a"})",
           "non-object node rejected");
    reject(R"({"id":"w","nodes":[{"name":"x"}],"start":"x"})",
           "missing node id rejected");
    reject(R"({"id":"w","nodes":[{"id":5}],"start":"x"})",
           "non-string node id rejected");
    reject(R"({"id":"w","nodes":[{"id":"a"},{"id":"a"}],"start":"a"})",
           "duplicate node id rejected");
    reject(R"({"id":"w","nodes":[{"id":"a","terrain":["lava"]}],"start":"a"})",
           "unknown terrain token rejected");
    reject(R"({"id":"w","nodes":[{"id":"a","strategic":"ferry"}],"start":"a"})",
           "non-array flag field rejected");
    reject(R"({"id":"w","nodes":[{"id":"a","myth":["dragon"]}],"start":"a"})",
           "unknown myth token rejected");
    reject(R"({"id":"w","nodes":[{"id":"a","control":"bandit"}],"start":"a"})",
           "unknown control rejected");
    reject(R"({"id":"w","nodes":[{"id":"a","control":3}],"start":"a"})",
           "non-string control rejected");
    reject(R"({"id":"w","nodes":[{"id":"a","map":[1]}],"start":"a"})",
           "non-string map ref rejected");
    reject(R"({"id":"w","nodes":[{"id":"a","center":[1,2,3]}],"start":"a"})",
           "bad center shape rejected");
    reject(R"({"id":"w","nodes":[{"id":"a"}],"routes":"x","start":"a"})",
           "non-array routes rejected");
    reject(R"({"id":"w","nodes":[{"id":"a"}],"routes":[5],"start":"a"})",
           "non-object route rejected");
    reject(R"({"id":"w","nodes":[{"id":"a"},{"id":"b"}],
            "routes":[{"a":"a","days":1}],"start":"a"})",
           "route missing 'b' rejected");
    reject(R"({"id":"w","nodes":[{"id":"a"},{"id":"b"}],
            "routes":[{"a":"a","b":"b"}],"start":"a"})",
           "route missing 'days' rejected");
    reject(R"({"id":"w","nodes":[{"id":"a"},{"id":"b"}],
            "routes":[{"a":"a","b":"b","days":0}],"start":"a"})",
           "days 0 rejected");
    reject(R"({"id":"w","nodes":[{"id":"a"},{"id":"b"}],
            "routes":[{"a":"a","b":"b","days":99}],"start":"a"})",
           "days over MAX_ROUTE_DAYS rejected");
    reject(R"({"id":"w","nodes":[{"id":"a"},{"id":"b"}],
            "routes":[{"a":"a","b":"b","days":"x"}],"start":"a"})",
           "non-int days rejected");
    reject(R"({"id":"w","nodes":[{"id":"a"}],
            "routes":[{"a":"a","b":"ghost","days":1}],"start":"a"})",
           "route to unknown node rejected");
    reject(R"({"id":"w","nodes":[{"id":"a"}],
            "routes":[{"a":"a","b":"a","days":1}],"start":"a"})",
           "self-route rejected");
    reject(R"({"id":"w","nodes":[{"id":"a"},{"id":"b"}],
            "routes":[{"a":"a","b":"b","days":1},
                      {"a":"b","b":"a","days":2}],"start":"a"})",
           "duplicate route (either direction) rejected");
    reject(R"({"id":"w","nodes":[{"id":"a"}]})", "missing start rejected");
    reject(R"({"id":"w","nodes":[{"id":"a"}],"start":5})",
           "non-string start rejected");
    reject(R"({"id":"w","nodes":[{"id":"a"}],"start":"ghost"})",
           "start naming unknown node rejected");

    // --- Lenient-acceptance pins (deliberate policy) ---
    {
        auto d = JsonValue::Parse(
            R"({"id":"w","nodes":[{"id":"a","future":{"x":1}},
                {"id":"b"}],"routes":null,"start":"a"})");
        auto m = WorldMap::FromJson(d.value);
        Check(m.ok() && m.value.NodeCount() == 2 &&
                  m.value.Neighbors(0).empty(),
              "routes:null + unknown extra fields tolerated");
        auto d2 = JsonValue::Parse(
            R"({"id":"w","nodes":[{"id":"a"},{"id":"b"}],
                "routes":[],"start":"a"})");
        auto m2 = WorldMap::FromJson(d2.value);
        Check(m2.ok() && m2.value.Neighbors(0).empty() &&
                  m2.value.Neighbors(1).empty(),
              "routes:[] accepted (isolated nodes legal)");
        auto d3 = JsonValue::Parse(
            R"({"id":"w","nodes":[{"id":"a","terrain":["road","road"]}],
                "start":"a"})");
        auto m3 = WorldMap::FromJson(d3.value);
        Check(m3.ok(), "duplicate flag token tolerated (idempotent OR)");
        // Absent control defaults to Neutral.
        Check(m3.ok() && m3.value.NodeCount() == 1 &&
                  m3.value.NodeAt(0).control == WorldControl::Neutral,
              "absent control defaults to Neutral");
    }

    // --- File path via Json::Load gate ---
    {
        const char* tmp = "potato_test_world.fixture.tmp.json";
        WriteFile(".", tmp, WORLD_DOC);
        auto loaded = WorldMap::Load(tmp);
        Check(loaded.ok() && loaded.value.NodeCount() == 3,
              "Load(path) with schema gate");
        std::remove(tmp);
    }
    {
        const char* tmp = "potato_test_world.badschema.tmp.json";
        WriteFile(".", tmp,
                  R"({"schema":"potato.world/2","id":"w","nodes":[{"id":"a"}],"start":"a"})");
        auto bad = WorldMap::Load(tmp);
        Check(!bad.ok() && bad.error == "schema",
              "wrong schema version rejected");
        std::remove(tmp);
    }
    {
        auto missing =
            WorldMap::Load("potato_test_world.no_such_file.json");
        Check(!missing.ok() && missing.error == "io",
              "missing file -> io error");
    }

    // --- WorldLibrary dir-scan registry ---
    const fs::path dir =
        fs::temp_directory_path() / "potato_test_world_lib";
    const fs::path dir2 =
        fs::temp_directory_path() / "potato_test_world_dup";
    fs::remove_all(dir);
    fs::remove_all(dir2);
    fs::create_directories(dir);
    fs::create_directories(dir2);
    {
        // Two valid worlds + one bad file + one non-json file.
        WriteFile(dir, "b_world.json", WORLD_DOC);
        WriteFile(dir, "a_world.json", WORLD_DOC2);
        WriteFile(dir, "bad.json",
                  R"({"schema":"potato.world/1","id":"x","nodes":[]})");
        WriteFile(dir, "notes.txt", "not json");
        WorldLibrary lib;
        const auto res = WorldLibrary::Load(dir, lib);
        Check(res.ok, "library loads with a rejected file");
        Check(lib.Size() == 2, "two worlds registered");
        Check(res.rejected.size() == 1 &&
                  res.rejected[0].path.filename() == "bad.json",
              "bad file isolated into rejected[]");
        Check(lib.Find("republic_fall") != nullptr &&
                  lib.Find("countermarch") != nullptr,
              "Find by world id");
        Check(lib.Find("nowhere") == nullptr, "Find miss -> nullptr");
        // Canonical order: world id (countermarch < republic_fall),
        // not filename order.
        Check(lib.Worlds()[0].Id() == "countermarch" &&
                  lib.Worlds()[1].Id() == "republic_fall",
              "worlds sorted by id, not filename");

        // Duplicate world id across files -> second file rejected.
        WriteFile(dir2, "a.json", WORLD_DOC);
        WriteFile(dir2, "b.json", WORLD_DOC);
        WorldLibrary lib2;
        const auto res2 = WorldLibrary::Load(dir2, lib2);
        Check(res2.ok && lib2.Size() == 1 &&
                  res2.rejected.size() == 1 &&
                  res2.rejected[0].error == "duplicate",
              "duplicate world id rejected, first wins");

        // Empty dir -> empty ok; unreadable dir -> io.
        const fs::path empty = dir / "empty";
        fs::create_directories(empty);
        WorldLibrary lib3;
        const auto res3 = WorldLibrary::Load(empty, lib3);
        Check(res3.ok && lib3.Size() == 0, "empty dir -> empty ok");
        WorldLibrary lib4;
        const auto res4 = WorldLibrary::Load(dir / "no_such_dir", lib4);
        Check(!res4.ok && res4.error == "io",
              "unreadable dir -> io error");
    }
    fs::remove_all(dir);
    fs::remove_all(dir2);

    // ================= Story 12.2 — WorldState & beats =================

    const WorldMap& wmap = [&]() -> const WorldMap& {
        static WorldMap m = WorldMap::FromJson(
            JsonValue::Parse(WORLD_DOC).value).value;
        return m;
    }();

    // --- Init: seeds control, warband at start, day 0 ---
    {
        auto s = WorldState::Init(wmap, 42);
        Check(s.ok(), "12.2: Init ok");
        Check(s.value.Day() == 0 &&
                  s.value.WarbandAt() == "kaifeng" &&
                  s.value.WorldId() == "republic_fall",
              "12.2: Init day/warband/world");
        Check(s.value.ControlAt("kaifeng") == WorldControl::Player &&
                  s.value.ControlAt("luoyang") == WorldControl::Rival &&
                  s.value.ControlAt("longmen") == WorldControl::Neutral &&
                  s.value.ControlAt("ghost") == WorldControl::Neutral,
              "12.2: control seeded, misses read Neutral");
        Check(s.value.Pending().empty() &&
                  !s.value.IsResolved("longmen"),
              "12.2: empty queue + resolved set");
        WorldMap empty;
        Check(!WorldState::Init(empty, 1).ok(),
              "12.2: Init on empty map rejects");
    }

    // --- Enqueue validation ---
    {
        auto s = WorldState::Init(wmap, 7).value;
        Check(!s.Enqueue(wmap, WorldEvent{1, 0,
                          WorldEventKind::Resolve, "ghost"}).ok(),
              "12.2: enqueue unknown node rejects");
        Check(!s.Enqueue(wmap, WorldEvent{-1, 0,
                          WorldEventKind::Resolve, "longmen"}).ok(),
              "12.2: enqueue past-day rejects");
        Check(!s.Enqueue(wmap, WorldEvent{1, 0,
                          WorldEventKind::Resolve, ""}).ok(),
              "12.2: enqueue empty node id rejects");
        Check(s.Enqueue(wmap, WorldEvent{2, 0,
                        WorldEventKind::Resolve, "longmen"}).ok() &&
                  s.Pending().size() == 1,
              "12.2: valid enqueue lands");
    }

    // --- Canonical apply order: same-day events order by seq ---
    {
        auto s = WorldState::Init(wmap, 5).value;
        // Insert out of order; (day,seq) must win.
        Check(s.Enqueue(wmap, WorldEvent{2, 9, WorldEventKind::SetControl,
                        "luoyang", WorldControl::Neutral}).ok(),
              "12.2: enqueue A");
        Check(s.Enqueue(wmap, WorldEvent{1, 5, WorldEventKind::SetControl,
                        "kaifeng", WorldControl::Rival}).ok(),
              "12.2: enqueue B");
        Check(s.Enqueue(wmap, WorldEvent{2, 3, WorldEventKind::Resolve,
                        "longmen"}).ok(),
              "12.2: enqueue C");
        Check(s.Pending().size() == 3, "12.2: three pending");
        auto n = s.ResolveBeats(wmap, 2);
        Check(n.ok() && n.value == 3, "12.2: all due events applied");
        Check(s.Day() == 2, "12.2: day advanced");
        Check(s.ControlAt("kaifeng") == WorldControl::Rival,
              "12.2: day-1 event applied first (seq order)");
        Check(s.ControlAt("luoyang") == WorldControl::Neutral,
              "12.2: SetControl to Neutral erases (canonical)");
        Check(s.IsResolved("longmen"), "12.2: Resolve marks node");
        Check(s.Pending().empty(), "12.2: queue drained");
        // Re-resolve is a no-op for an empty queue.
        auto n2 = s.ResolveBeats(wmap, 1);
        Check(n2.ok() && n2.value == 0 && s.Day() == 3,
              "12.2: empty drain advances day only");
    }

    // --- ResolveBeats rejects bad advance ---
    {
        auto s = WorldState::Init(wmap, 5).value;
        Check(!s.ResolveBeats(wmap, -1).ok(),
              "12.2: negative days rejected");
        Check(!s.ResolveBeats(wmap, WorldState::MAX_DAY + 1).ok(),
              "12.2: day overflow rejected");
        Check(s.Day() == 0, "12.2: failed advance leaves day");
    }

    // --- SetWarband validates against the map ---
    {
        auto s = WorldState::Init(wmap, 1).value;
        Check(s.SetWarband("longmen", wmap) &&
                  s.WarbandAt() == "longmen",
              "12.2: warband relocates");
        Check(!s.SetWarband("ghost", wmap) &&
                  s.WarbandAt() == "longmen",
              "12.2: unknown node rejected, warband unmoved");
    }

    // --- Dedicated PRNG: deterministic + state persists ---
    {
        auto a = WorldState::Init(wmap, 77).value;
        auto b = WorldState::Init(wmap, 77).value;
        Check(a.Draw() == b.Draw() && a.Draw() == b.Draw(),
              "12.2: same seed -> same draws");
        auto c = WorldState::Init(wmap, 78).value;
        Check(a.Draw() != c.Draw() || a.Draw() != c.Draw(),
              "12.2: different seed -> different stream");
        // Round-trip preserves the counter.
        a.Draw();
        auto wire = a.ToJson();
        Check(wire.ok(), "12.2: ToJson ok");
        auto rt = WorldState::FromJson(wire.value);
        Check(rt.ok(), "12.2: FromJson ok");
        Check(rt.ok() && rt.value.Draw() == a.Draw(),
              "12.2: rng counter survives round-trip");
    }

    // --- ToJson/FromJson round-trip: full state ---
    {
        auto s = WorldState::Init(wmap, 9).value;
        s.Enqueue(wmap, WorldEvent{3, s.NextSeq(),
                  WorldEventKind::SetControl, "longmen",
                  WorldControl::Player});
        s.Enqueue(wmap, WorldEvent{3, s.NextSeq(),
                  WorldEventKind::Resolve, "kaifeng"});
        s.ResolveBeats(wmap, 3);
        s.Enqueue(wmap, WorldEvent{10, s.NextSeq(),
                  WorldEventKind::Resolve, "luoyang"});
        s.SetWarband("luoyang", wmap);
        auto wire = s.ToJson();
        Check(wire.ok(), "12.2: state serializes");
        auto rt = WorldState::FromJson(wire.value);
        Check(rt.ok(), "12.2: state restores");
        const WorldState& r = rt.value;
        Check(r.Day() == 3 && r.WarbandAt() == "luoyang" &&
                  r.WorldId() == "republic_fall",
              "12.2: day/warband/world survive");
        Check(r.ControlAt("longmen") == WorldControl::Player &&
                  r.IsResolved("kaifeng"),
              "12.2: applied effects survive");
        Check(r.Pending().size() == 1 &&
                  r.Pending()[0].day == 10 &&
                  r.Pending()[0].kind == WorldEventKind::Resolve,
              "12.2: pending queue survives");
        // Emit is canonical — two identical states emit identical bytes.
        Check(s.ToJson().value.Emit() == r.ToJson().value.Emit(),
              "12.2: byte-identical serialization (canonical)");
    }

    // --- Malformed docs reject without partial mutation ---
    auto bad = [](const char* json, const char* name) {
        auto d = JsonValue::Parse(json);
        Check(d.ok(), "12.2: bad fixture parses");
        if (!d.ok()) return;
        Check(!WorldState::FromJson(d.value).ok(), name);
    };
    bad(R"({"schema":"potato.worldstate/2","world":"w","day":0,"warband":"a"})",
        "12.2: wrong schema rejected");
    bad(R"({"schema":"potato.worldstate/1","day":0,"warband":"a"})",
        "12.2: missing world rejected");
    bad(R"({"schema":"potato.worldstate/1","world":"w","warband":"a"})",
        "12.2: missing day rejected");
    bad(R"({"schema":"potato.worldstate/1","world":"w","day":-3,"warband":"a"})",
        "12.2: negative day rejected");
    bad(R"({"schema":"potato.worldstate/1","world":"w","day":0})",
        "12.2: missing warband rejected");
    bad(R"({"schema":"potato.worldstate/1","world":"w","day":0,"warband":"a","control":{"a":"bandit"}})",
        "12.2: bad control spelling rejected");
    bad(R"({"schema":"potato.worldstate/1","world":"w","day":0,"warband":"a","resolved":[42]})",
        "12.2: non-string resolved rejected");
    bad(R"({"schema":"potato.worldstate/1","world":"w","day":0,"warband":"a","queue":[{"day":1,"seq":0,"kind":"nonsense","node":"a"}]})",
        "12.2: unknown event kind rejected");
    bad(R"({"schema":"potato.worldstate/1","world":"w","day":0,"warband":"a","queue":[{"day":1,"seq":0,"kind":"control","node":"a"}]})",
        "12.2: control event missing control rejected");

    // --- Determinism: identical sequences -> identical bytes ---
    {
        auto run = [&wmap]() {
            auto s = WorldState::Init(
                WorldMap::FromJson(
                    JsonValue::Parse(WORLD_DOC).value).value,
                1234).value;
            s.Enqueue(wmap,
                      WorldEvent{1, 0, WorldEventKind::SetControl,
                                 "longmen", WorldControl::Rival});
            s.ResolveBeats(wmap, 1);
            s.Draw();
            s.Enqueue(wmap, WorldEvent{2, 0, WorldEventKind::Resolve,
                                       "kaifeng"});
            s.ResolveBeats(wmap, 2);
            return s.ToJson().value.Emit();
        };
        Check(run() == run(),
              "12.2: identical seed+events -> identical bytes");
    }

    // ================= Story 12.3 — warband & movement =================

    // --- March: order → ledger debit → queue → arrival ---
    {
        Ledger ledger;
        // Seed 物資: credit Materiel / debit ArmyPrestige.
        Posting seed;
        seed.credit = {Account::Materiel, 100};
        seed.debit = {Account::ArmyPrestige, 100};
        seed.memo = "war chest";
        Check(ledger.Post(seed).ok(), "12.3: seed funds");

        auto s = WorldState::Init(wmap, 3).value;
        Check(!s.Marching(), "12.3: idle warband");

        auto plan = IssueMarch(s, wmap, ledger, "longmen");
        Check(plan.ok() && plan.value.from == "kaifeng" &&
                  plan.value.to == "longmen" &&
                  plan.value.days == 2 && plan.value.supply == 10,
              "12.3: march plan");
        Check(ledger.Balance(Account::Materiel) == 90 &&
                  ledger.Balance(Account::ArmyPrestige) == -90,
              "12.3: supply posted (debit Materiel)");
        Check(ledger.Entries().back().tags ==
                      std::vector<std::string>{"march",
                                               "region:longmen"},
              "12.3: march tags book the region");
        Check(s.Marching() && s.MarchDest() == "longmen" &&
                  s.MarchEta() == 2,
              "12.3: in-flight marker set");
        Check(s.Pending().size() == 1 &&
                  s.Pending()[0].kind == WorldEventKind::March &&
                  s.Pending()[0].day == 2 && s.Pending()[0].seq == 0,
              "12.3: march queued at day+days with state seq");

        // Still at kaifeng mid-march; arrives when the beat lands.
        Check(s.WarbandAt() == "kaifeng", "12.3: en route, not there");
        auto n = s.ResolveBeats(wmap, 1);
        Check(n.ok() && s.WarbandAt() == "kaifeng" && s.Marching(),
              "12.3: day 1 — still on the road");
        n = s.ResolveBeats(wmap, 1);
        Check(n.ok() && n.value == 1 && s.WarbandAt() == "longmen" &&
                  !s.Marching(),
              "12.3: arrival applies via the queue");
    }

    // --- Debt marches + atomic rejections ---
    {
        Ledger ledger; // empty — 欠帳行軍：物資可負
        auto s = WorldState::Init(wmap, 3).value;
        Check(IssueMarch(s, wmap, ledger, "longmen").ok() &&
                  s.Marching() &&
                  ledger.Balance(Account::Materiel) == -10,
              "12.3: unfunded march books debt");
        const auto entriesBefore = ledger.Size();
        Check(!IssueMarch(s, wmap, ledger, "kaifeng").ok() &&
                  ledger.Size() == entriesBefore,
              "12.3: second order while marching rejects");
        // Not adjacent: luoyang isn't a kaifeng neighbor.
        auto s2 = WorldState::Init(wmap, 3).value;
        Check(!IssueMarch(s2, wmap, ledger, "luoyang").ok() &&
                  ledger.Balance(Account::Materiel) == -10,
              "12.3: non-adjacent rejects, ledger untouched");
        Check(IssueMarch(s2, wmap, ledger, "longmen").ok(),
              "12.3: first order lands");
    }

    // --- Sightings are hearsay marks ---
    {
        auto s = WorldState::Init(wmap, 1).value;
        Check(IssueSighting(s, wmap, "luoyang").ok() &&
                  IssueSighting(s, wmap, "luoyang").ok() &&
                  IssueSighting(s, wmap, "longmen").ok(),
              "12.3: sightings enqueue");
        Check(!IssueSighting(s, wmap, "ghost").ok(),
              "12.3: sighting unknown node rejects");
        Check(s.Pending().size() == 3 && s.Pending()[0].seq == 0 &&
                  s.Pending()[1].seq == 1 && s.Pending()[2].seq == 2,
              "12.3: sightings draw monotonic state seqs");
        s.ResolveBeats(wmap, 0); // same-day drain
        const auto& sg = s.Sightings();
        Check(sg.size() == 2 && sg.at("luoyang").count == 2 &&
                  sg.at("luoyang").day == 0 &&
                  sg.at("longmen").count == 1,
              "12.3: hearsay counts accumulate per node");
        Check(!s.IsResolved("luoyang"),
              "12.3: sightings never touch resolved truth");
    }

    // --- Edited-out nodes: no teleport, march marker releases ---
    {
        // A second map where longmen doesn't exist.
        const WorldMap shrunk = WorldMap::FromJson(
            JsonValue::Parse(WORLD_DOC2).value).value;
        Ledger ledger;
        Posting seed;
        seed.credit = {Account::Materiel, 100};
        seed.debit = {Account::ArmyPrestige, 100};
        ledger.Post(seed);
        auto s = WorldState::Init(wmap, 3).value;
        IssueMarch(s, wmap, ledger, "longmen");
        IssueSighting(s, wmap, "longmen");
        Check(s.Marching() && s.Pending().size() == 2,
              "12.3: march + sight queued pre-shrink");
        // Resolve past the arrival day against the shrunken map.
        auto n = s.ResolveBeats(shrunk, 5);
        Check(n.ok() && n.value == 0,
              "12.3: vanished-node events skip, none applied");
        Check(s.WarbandAt() == "kaifeng",
              "12.3: warband never teleports to a deleted node");
        Check(!s.Marching(),
              "12.3: aborted march releases the in-flight marker");
        Check(s.Sightings().empty(),
              "12.3: sighting on a deleted node never lands");
        Check(s.Day() == 5, "12.3: beats still advance");
    }

    // --- Determinism: identical order sequence -> identical bytes ---
    {
        auto run = [&wmap]() {
            auto s = WorldState::Init(
                WorldMap::FromJson(
                    JsonValue::Parse(WORLD_DOC).value).value,
                55).value;
            Ledger ledger;
            Posting seed;
            seed.credit = {Account::Materiel, 100};
            seed.debit = {Account::ArmyPrestige, 100};
            ledger.Post(seed);
            IssueMarch(s, wmap, ledger, "longmen");
            IssueSighting(s, wmap, "luoyang");
            s.ResolveBeats(wmap, 1);
            IssueSighting(s, wmap, "kaifeng");
            s.ResolveBeats(wmap, 1); // march lands at day 2
            return s.ToJson().value.Emit();
        };
        Check(run() == run(),
              "12.3: identical order sequence -> identical bytes");
    }

    // --- Round-trip: in-flight march + sightings + seq counter ---
    {
        Ledger ledger;
        Posting seed;
        seed.credit = {Account::Materiel, 100};
        seed.debit = {Account::ArmyPrestige, 100};
        ledger.Post(seed);
        auto s = WorldState::Init(wmap, 3).value;
        IssueMarch(s, wmap, ledger, "longmen");
        IssueSighting(s, wmap, "luoyang");
        s.ResolveBeats(wmap, 0); // sight lands, march in flight

        auto wire = s.ToJson();
        auto rt = WorldState::FromJson(wire.value);
        Check(rt.ok(), "12.3: mid-march state restores");
        const WorldState& r = rt.value;
        Check(r.Marching() && r.MarchDest() == "longmen" &&
                  r.MarchEta() == 2,
              "12.3: in-flight march survives");
        Check(r.Sightings().count("luoyang") == 1 &&
                  r.Sightings().at("luoyang").count == 1,
              "12.3: sightings survive");
        Check(r.Pending().size() == 1 &&
                  r.Pending()[0].kind == WorldEventKind::March,
              "12.3: queued march survives");
        // seq counter never replays an id.
        auto s2 = r;
        auto n = s2.ResolveBeats(wmap, 2);
        Check(n.ok() && s2.WarbandAt() == "longmen",
              "12.3: restored state finishes the march");
        Check(s2.NextSeq() >= 2,
              "12.3: seq counter continues past stored events");
    }

    // --- Malformed 12.3 fields reject ---
    bad(R"({"schema":"potato.worldstate/1","world":"w","day":0,"warband":"a","marching":{"to":"b"}})",
        "12.3: marching missing eta rejected");
    bad(R"({"schema":"potato.worldstate/1","world":"w","day":0,"warband":"a","sightings":{"x":{"day":0,"count":0}}})",
        "12.3: sighting count 0 rejected");
    bad(R"({"schema":"potato.worldstate/1","world":"w","day":0,"warband":"a","queue":[{"day":1,"seq":0,"kind":"teleport","node":"a"}]})",
        "12.3: unknown new kind rejected");
    bad(R"({"schema":"potato.worldstate/1","world":"w","day":0,"warband":"a","sightings":{"x":{"day":-1,"count":2}}})",
        "12.3: sighting negative day rejected");

    // --- Story 12.7: regional governance & myth binding ---
    {
        using Potato::Campaign::BookDeeds;
        using Potato::Campaign::ChapterDef;
        using Potato::Campaign::FoldGovernanceByRegion;
        using Potato::Campaign::IsShrinePoi;
        using Potato::Campaign::MythPlaceKey;
        using Potato::Campaign::MythState;
        using Potato::Campaign::RegionTagFor;
        using Potato::Campaign::SetShrineLevel;
        using Potato::Campaign::ShrineLevel;
        using Potato::Gameplay::SimEvent;

        Ledger l;
        // Untagged entry — campaign totals only.
        {
            Posting p;
            p.credit = {Account::PopularSupport, 50};
            p.debit = {Account::Materiel, 50};
            p.memo = "unsited";
            Check(l.Post(p).ok(), "12.7: untagged posts");
        }
        // Two deeds tagged at longmen.
        {
            Posting p;
            p.credit = {Account::Materiel, 40};
            p.debit = {Account::PopularSupport, 15};
            p.memo = "burned village";
            p.tags = {"atrocity", "field:2",
                      std::string(Ledger::TAG_REGION) + "longmen",
                      "order:-10", "corruption:+15"};
            Check(l.Post(p).ok(), "12.7: longmen atrocity posts");
        }
        {
            Posting p;
            p.credit = {Account::PopularSupport, 10};
            p.debit = {Account::Materiel, 5};
            p.memo = "occupied village";
            p.tags = {std::string(Ledger::TAG_REGION) + "longmen",
                      "order:+5"};
            Check(l.Post(p).ok(), "12.7: longmen order posts");
        }
        // One deed tagged at kaifeng — different region.
        {
            Posting p;
            p.credit = {Account::PopularSupport, 8};
            p.debit = {Account::Materiel, 8};
            p.memo = "escorted convoy";
            p.tags = {std::string(Ledger::TAG_REGION) + "kaifeng",
                      "order:+2"};
            Check(l.Post(p).ok(), "12.7: kaifeng deed posts");
        }

        const auto fold = FoldGovernanceByRegion(l);
        Check(fold.size() == 2 && fold.count("longmen") &&
                  fold.count("kaifeng"),
              "12.7: per-region map keyed by node id");
        const auto& lm = fold.at("longmen");
        // PS: -15 + 10 = -5; order: -10 + 5 = -5; corruption
        // ratchet peaks at 15.
        Check(lm.popularSupport == -5 && lm.order == -5 &&
                  lm.corruption == 15,
              "12.7: longmen folds PS/order/corruption");
        Check(fold.at("kaifeng").popularSupport == 8 &&
                  fold.at("kaifeng").order == 2 &&
                  fold.at("kaifeng").corruption == 0,
              "12.7: kaifeng folds independently");
        Check(fold.count("") == 0 &&
                  fold.find("unsited") == fold.end(),
              "12.7: untagged entries feed no region");

        // BookDeeds worldNode stamping: the battle happened AT
        // longmen — deeds carry both field:<n> and region:<node>.
        {
            Ledger l2;
            SimEvent e;
            e.kind = SimEvent::Kind::VillageBurned;
            e.side = 0;
            e.param = 3;
            const SimEvent evs[] = {e};
            const auto n = BookDeeds(l2, 0, evs, "longmen");
            Check(n.ok() && n.value == 1,
                  "12.7: world-anchored deed books");
            const auto& tags = l2.Entries().back().tags;
            const bool hasField =
                std::find(tags.begin(), tags.end(),
                          "field:3") != tags.end();
            const bool hasRegion =
                std::find(tags.begin(), tags.end(),
                          "region:longmen") != tags.end();
            Check(hasField && hasRegion,
                  "12.7: deed carries field + world region");
            const auto f2 = FoldGovernanceByRegion(l2);
            Check(f2.count("longmen") &&
                      f2.at("longmen").corruption == 15,
                  "12.7: stamped deed folds under the node");
        }

        // Myth binding: place key follows the node when bound.
        ChapterDef unbound;
        unbound.id = "ch01_kaifeng";
        Check(MythPlaceKey(unbound) == "ch01_kaifeng",
              "12.7: unbound chapter keys by id");
        ChapterDef bound;
        bound.id = "ch02_longmen";
        bound.bound = true;
        bound.bind.node = "longmen";
        Check(MythPlaceKey(bound) == "longmen",
              "12.7: bound chapter keys by node");

        // Shrine POI: node myth flag -> sanctity at slot 0.
        Check(IsShrinePoi(wmap.NodeAt(wmap.NodeIndexOf("longmen"))),
              "12.7: longmen reads as shrine POI");
        Check(!IsShrinePoi(wmap.NodeAt(wmap.NodeIndexOf("kaifeng"))),
              "12.7: kaifeng is not a shrine");
        MythState ms;
        Check(SetShrineLevel(ms, "longmen", 2) &&
                  ShrineLevel(ms, "longmen") == 2 &&
                  ShrineLevel(ms, "kaifeng") == 0,
              "12.7: shrine level round-trips slot 0");
        Check(!SetShrineLevel(ms, "longmen", 4),
              "12.7: level >3 rejected");
    }

    // =============== Story 13.6 — quantum oracle seam ===============
    {
        auto a = WorldState::Init(wmap, 42).value;
        auto b = WorldState::Init(wmap, 42).value;
        Check(!a.HasOracle(), "13.6: oracle off by default");
        Check(a.EnableOracle(999).ok() && a.HasOracle(),
              "13.6: EnableOracle engages");
        Check(b.EnableOracle(999).ok(), "13.6: second instance engages");
        Check(!a.EnableOracle(0).ok(), "13.6: oracle seed 0 rejected");

        bool same = true;
        for (int i = 0; i < 60; ++i) {
            if (a.DrawPermille(300) != b.DrawPermille(300)) same = false;
        }
        Check(same, "13.6: same seed → identical oracle draws");

        // Oracle owns a second stream — enabling it leaves the
        // classic Draw() sequence completely untouched.
        auto c = WorldState::Init(wmap, 42).value;
        auto d = WorldState::Init(wmap, 42).value;
        c.EnableOracle(1);
        bool classicSame = true;
        for (int i = 0; i < 10; ++i) {
            if (c.Draw() != d.Draw()) classicSame = false;
        }
        Check(classicSame,
              "13.6: oracle enable leaves classic stream intact");

        // Classical permille path still answers fairly.
        auto e = WorldState::Init(wmap, 42).value;
        int hits = 0;
        for (int i = 0; i < 300; ++i) if (e.DrawPermille(500)) ++hits;
        Check(hits > 110 && hits < 190,
              "13.6: classical permille ~fair");

        // Degenerate draws answer without consuming either lane.
        auto g = WorldState::Init(wmap, 42).value;
        auto h = WorldState::Init(wmap, 42).value;
        g.EnableOracle(88); h.EnableOracle(88);
        const int lone[4] = {0, 0, 7, 0};
        Check(!g.DrawPermille(0) && g.DrawPermille(1000) &&
              g.DrawCategorical(lone, 4) == 2,
              "13.6: degenerate answers in oracle mode");
        Check(g.DrawPermille(500) == h.DrawPermille(500),
              "13.6: degenerate draws consume nothing");
        const int wts[4] = {0, 1, 3, 0};
        bool unreachable = true;
        for (int i = 0; i < 200; ++i) {
            int idx = g.DrawCategorical(wts, 4);
            if (idx == 0 || idx == 3 || idx < 0) unreachable = false;
        }
        Check(unreachable, "13.6: zero-weight entries stay unreachable");

        // Persistence: oracle mode + stream position round-trip.
        auto p = WorldState::Init(wmap, 42).value;
        p.EnableOracle(4242);
        for (int i = 0; i < 5; ++i) p.DrawPermille(500);
        auto wire = p.ToJson();
        Check(wire.ok() && wire.value.Has("oracle"),
              "13.6: oracle persists under 'oracle' key");
        auto rr = WorldState::FromJson(wire.value);
        Check(rr.ok() && rr.value.HasOracle(),
              "13.6: oracle mode survives round-trip");
        Check(rr.ok() &&
                  rr.value.DrawPermille(500) == p.DrawPermille(500),
              "13.6: oracle stream resumes bit-exactly");

        // Absent field ⇒ classical (back-compat).
        auto q = WorldState::Init(wmap, 42).value;
        auto rq = WorldState::FromJson(q.ToJson().value);
        Check(rq.ok() && !rq.value.HasOracle(),
              "13.6: absent oracle field loads classical");
    }

    if (failures == 0) {
        std::puts("WORLD TESTS PASS");
        return 0;
    }
    std::printf("%d CHECK(S) FAILED\n", failures);
    return 1;
}
