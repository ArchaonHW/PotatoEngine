#include "Gameplay/Map/BattleMap.h"
#include "Gameplay/Json/JsonValue.h"

#include <cstdio>
#include <fstream>
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

using Potato::Gameplay::BattleMap;
using Potato::Gameplay::JsonValue;
using Potato::Gameplay::IsStrategicPoint;
using Potato::Gameplay::TERRAIN_FOREST;
using Potato::Gameplay::TERRAIN_RIVER;
using Potato::Gameplay::TERRAIN_ROAD;
using Potato::Gameplay::STRATEGIC_DEPOT;
using Potato::Gameplay::STRATEGIC_FERRY;
using Potato::Gameplay::STRATEGIC_VILLAGE;
using Potato::Gameplay::HIST_GRAIN_ROUTE;
using Potato::Gameplay::MYTH_HAUNTED;
using Potato::Gameplay::MYTH_SHRINE;

const char* MAP_DOC = R"json({
    "schema": "potato.map/1",
    "id": "map_hefe",
    "name": "合肥外圍",
    "regions": [
        {"id": "north_ford", "name": "北渡",
         "terrain": ["river", "road"],
         "strategic": ["ferry"],
         "historical": ["grain_route"],
         "myth": ["shrine"],
         "center": [12, 8]},
        {"id": "village_wu", "name": "吳家村",
         "terrain": ["open"],
         "strategic": ["village"]},
        {"id": "pine_wood",
         "terrain": ["forest"],
         "myth": ["haunted"]}
    ],
    "edges": [["north_ford", "village_wu"], ["village_wu", "pine_wood"]]
})json";

bool Contains(const std::vector<std::size_t>& v, std::size_t x) {
    for (std::size_t e : v) if (e == x) return true;
    return false;
}

} // namespace

int main() {
    // --- FromJson: full model decode ---
    auto doc = JsonValue::Parse(MAP_DOC);
    Check(doc.ok(), "fixture parses");
    auto map = BattleMap::FromJson(doc.value);
    Check(map.ok(), "potato.map/1 FromJson loads");
    if (map.ok()) {
        Check(map.value.Id() == "map_hefe" && map.value.Name() == "合肥外圍",
              "map id + CJK name");
        Check(map.value.RegionCount() == 3, "3 regions");

        const auto& ford = map.value.RegionAt(0);
        Check(ford.id == "north_ford" && ford.name == "北渡", "region id + CJK name");
        Check((ford.terrain & (TERRAIN_RIVER | TERRAIN_ROAD)) ==
              (TERRAIN_RIVER | TERRAIN_ROAD), "terrain bits decoded");
        Check((ford.strategic & STRATEGIC_FERRY) != 0, "strategic ferry flag");
        Check((ford.historical & HIST_GRAIN_ROUTE) != 0, "historical grain_route mark");
        Check((ford.myth & MYTH_SHRINE) != 0, "myth shrine mark (dual-layer)");
        Check(ford.hasCenter && ford.centerX == 12 && ford.centerY == 8,
              "optional center decoded");

        Check(IsStrategicPoint(ford), "ferry region is strategic point");
        Check(IsStrategicPoint(map.value.RegionAt(1)), "village is strategic point");
        Check(!IsStrategicPoint(map.value.RegionAt(2)),
              "haunted wood is not a strategic point");
        Check((map.value.RegionAt(2).myth & MYTH_HAUNTED) != 0, "haunted myth mark");

        const auto* found = map.value.FindRegion("village_wu");
        Check(found != nullptr &&
              (found->strategic & STRATEGIC_VILLAGE) != 0, "FindRegion by id");
        Check(map.value.FindRegion("nowhere") == nullptr, "FindRegion miss -> nullptr");
        Check(map.value.RegionIndexOf("pine_wood") == 2, "RegionIndexOf file order");
        Check(map.value.RegionIndexOf("nowhere") == BattleMap::NO_REGION,
              "RegionIndexOf miss");

        // Undirected adjacency: both directions registered.
        Check(Contains(map.value.Neighbors(0), 1), "edge a -> b registered");
        Check(Contains(map.value.Neighbors(1), 0), "edge b -> a registered");
        Check(Contains(map.value.Neighbors(1), 2) &&
              Contains(map.value.Neighbors(2), 1), "second edge symmetric");
        Check(map.value.Neighbors(0).size() == 1, "neighbor counts correct");
        // Contract pins edge-declaration order, not just membership.
        const auto& nb = map.value.Neighbors(1);
        Check(nb.size() == 2 && nb[0] == 0 && nb[1] == 2,
              "neighbor order = edge declaration order");
    }

    // Default-constructed map is a safe empty value.
    {
        BattleMap empty;
        Check(empty.RegionCount() == 0 && empty.FindRegion("x") == nullptr &&
              empty.RegionIndexOf("x") == BattleMap::NO_REGION,
              "default BattleMap is empty-safe");
    }

    // --- Validation rejects ---
    auto reject = [](const char* json, const char* name) {
        auto d = JsonValue::Parse(json);
        if (!d.ok()) { Check(false, name); return; } // fixture typo, not a pass
        auto m = BattleMap::FromJson(d.value);
        Check(!m.ok() && !m.error.empty() && !m.reason.empty() &&
              m.value.RegionCount() == 0, name); // failure purity: no partial map
    };
    reject("[]", "non-object root rejected");
    reject(R"({"regions":[{"id":"a"}]})", "missing map id rejected");
    reject(R"({"id":5,"regions":[{"id":"a"}]})", "non-string map id rejected");
    reject(R"({"id":"m","name":7,"regions":[{"id":"a"}]})",
           "non-string map name rejected");
    reject(R"({"id":"m","regions":{}})", "non-array regions rejected");
    reject(R"({"id":"m","regions":[5]})", "non-object region rejected");
    reject(R"({"id":"m","regions":[{"id":5}]})", "non-string region id rejected");
    reject(R"({"id":"m","regions":[{"id":"a","name":[1]}]})",
           "non-string region name rejected");
    reject(R"({"schema":"potato.map/2","id":"m","regions":[{"id":"a"}]})",
           "FromJson rejects mismatched schema tag");
    reject(R"({"id":"m","regions":[{"id":"a"}],"edges":"x"})",
           "non-array edges rejected");
    reject(R"({"id":"m","regions":[{"id":"a"}],"edges":[["a"]]})",
           "edge arity rejected");
    reject(R"({"id":"m","regions":[{"id":"a"},{"id":"b"}],"edges":[["a","b"],["a","b"]]})",
           "duplicate edge same direction rejected");
    reject(R"({"id":"m","regions":[]})", "empty regions rejected");
    reject(R"({"id":"m","regions":[{"id":"a"},{"id":"a"}]})",
           "duplicate region id rejected");
    reject(R"({"id":"m","regions":[{"id":"a"}],"edges":[["a","b"]]})",
           "edge to unknown region rejected");
    reject(R"({"id":"m","regions":[{"id":"a"}],"edges":[["a","a"]]})",
           "self-edge rejected");
    reject(R"({"id":"m","regions":[{"id":"a","terrain":["lava"]}]})",
           "unknown terrain token rejected");
    reject(R"({"id":"m","regions":[{"id":"a","strategic":"ferry"}]})",
           "non-array flag field rejected");
    reject(R"({"id":"m","regions":[{"id":"a","myth":["dragon"]}]})",
           "unknown myth token rejected");
    reject(R"({"id":"m","regions":[{"id":"a","center":[1,2,3]}]})",
           "bad center shape rejected");
    reject(R"({"id":"m","regions":[{"id":"a"},{"id":"b"}],"edges":[["a","b"],["b","a"]]})",
           "duplicate edge (either direction) rejected");

    // --- Lenient-acceptance pins (deliberate policy) ---
    {
        auto d = JsonValue::Parse(
            R"({"id":"m","regions":[{"id":"a","future_field":{"x":[1]}},{"id":"b"}],
                "edges":null})");
        auto m = BattleMap::FromJson(d.value);
        Check(m.ok() && m.value.RegionCount() == 2 &&
              m.value.Neighbors(0).empty(),
              "edges:null + unknown extra fields tolerated");
        auto d2 = JsonValue::Parse(
            R"({"id":"m","regions":[{"id":"a"},{"id":"b"}],"edges":[]})");
        auto m2 = BattleMap::FromJson(d2.value);
        Check(m2.ok() && m2.value.Neighbors(0).empty() &&
              m2.value.Neighbors(1).empty(),
              "edges:[] accepted (isolated regions legal)");
        auto d3 = JsonValue::Parse(
            R"({"id":"m","regions":[{"id":"a","terrain":["road","road"]}]})");
        auto m3 = BattleMap::FromJson(d3.value);
        Check(m3.ok(), "duplicate flag token tolerated (idempotent OR)");
    }

    // --- File path via Json::Load gate ---
    {
        const char* tmp = "potato_test_map.fixture.tmp.json";
        {
            std::ofstream out(tmp, std::ios::binary);
            Check(out.good(), "fixture file opens");
            out << MAP_DOC;
        }
        auto loaded = BattleMap::Load(tmp);
        Check(loaded.ok() && loaded.value.RegionCount() == 3,
              "Load(path) with schema gate");
        std::remove(tmp);
    }
    {
        const char* tmp = "potato_test_map.badschema.tmp.json";
        {
            std::ofstream out(tmp, std::ios::binary);
            out << R"({"schema":"potato.map/2","id":"m","regions":[{"id":"a"}]})";
        }
        auto bad = BattleMap::Load(tmp);
        Check(!bad.ok() && bad.error == "schema", "wrong schema version rejected");
        std::remove(tmp);
    }
    {
        auto missing = BattleMap::Load("potato_test_map.no_such_file.json");
        Check(!missing.ok() && missing.error == "io", "missing file -> io error");
    }

    if (failures == 0) {
        std::puts("MAP TESTS PASS");
        return 0;
    }
    std::printf("%d CHECK(S) FAILED\n", failures);
    return 1;
}
