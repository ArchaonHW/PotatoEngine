// potato_test_json — headless tests for the JsonValue DOM parser and
// the potato.<name>/<ver> schema gate.

#include "Gameplay/Json/Json.h"
#include "Gameplay/Json/JsonValue.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <string>

namespace {

int failures = 0;

void Check(bool cond, const char* name) {
    std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", name);
    if (!cond) ++failures;
}

using Potato::Gameplay::JsonValue;
namespace Json = Potato::Gameplay::Json;

} // namespace

int main() {
    // --- AC1: well-formed potato.test/1 loads with typed access ---
    {
        const char* doc = R"json({
            "schema": "potato.test/1",
            "name": "smoke",
            "count": 42,
            "ratio": 1.5,
            "enabled": true,
            "tags": ["a", "b"],
            "nested": {"x": -7},
            "note": "CJK \u4e2d\u6587 and emoji \uD83D\uDE00",
            "dup": 1,
            "dup": 9
        })json";
        auto r = JsonValue::Parse(doc);
        Check(r.ok(), "well-formed doc parses");
        Check(r.value["schema"].AsString() == std::string("potato.test/1"), "schema string read");
        Check(r.value["count"].AsInt() == 42, "int read");
        Check(r.value["ratio"].AsDouble() > 1.4 && r.value["ratio"].AsDouble() < 1.6, "double read");
        Check(r.value["enabled"].AsBool() == true, "bool read");
        Check(r.value["tags"].Size() == 2 && r.value["tags"].At(1).AsString() == "b", "array access");
        Check(r.value["nested"]["x"].AsInt() == -7, "nested access");
        Check(r.value["note"].AsString().find("中文") != std::string::npos, "\\u escape -> UTF-8 (CJK)");
        Check(r.value["note"].AsString().size() > 20, "surrogate pair decoded");
        Check(r.value["dup"].AsInt() == 9, "duplicate key last-wins");
        Check(r.value["missing"].IsNull() && r.value["missing"].AsInt(77) == 77, "missing key -> null + fallback");
    }

    // --- numeric edge cases ---
    {
        auto r = JsonValue::Parse("{\"min\": -9223372036854775808}");
        Check(r.ok() && r.value["min"].AsInt() ==
              (std::numeric_limits<std::int64_t>::min)(), "INT64_MIN parses");
        Check(!JsonValue::Parse("1e999").ok(), "overflow real rejected");
        Check(!JsonValue::Parse("-1e999").ok(), "negative overflow real rejected");
        auto huge = JsonValue::Parse("99999999999999999999999");
        Check(huge.ok() && huge.value.IsReal(), "huge int literal degrades to Real");
        auto big = JsonValue::Parse("1e40");
        Check(big.ok() && big.value.AsInt(-1) == -1, "AsInt on out-of-range Real -> fallback (no UB)");
        auto boolv = JsonValue::Parse("true");
        Check(boolv.ok() && boolv.value.AsInt(42) == 42, "AsInt does not coerce Bool");
    }

    // --- reject paths must carry error + reason ---
    {
        auto bad = JsonValue::Parse("{");
        Check(!bad.ok() && !bad.error.empty() && !bad.reason.empty(), "reject carries error+reason");
        Check(!JsonValue::Parse("\"\\uD83D\"").ok(), "lone high surrogate rejected");
        Check(!JsonValue::Parse("\"\\uDE00\"").ok(), "lone low surrogate rejected");
        Check(!JsonValue::Parse("\"\\uD83D\\u0041\"").ok(), "bad surrogate pair rejected");
    }

    // --- AC2: malformed JSON rejected with reason ---
    {
        Check(!JsonValue::Parse("{").ok(), "truncated object rejected");
        Check(!JsonValue::Parse("{\"a\":01}").ok(), "leading zero rejected");
        Check(!JsonValue::Parse("{\"a\":1,}").ok(), "trailing comma rejected");
        Check(!JsonValue::Parse("[1 2]").ok(), "missing comma rejected");
        Check(!JsonValue::Parse("\"bad\\x\"").ok(), "bad escape rejected");
        Check(!JsonValue::Parse("{} extra").ok(), "trailing garbage rejected");
        Check(!JsonValue::Parse("nul").ok(), "bad literal rejected");
        Check(!JsonValue::Parse("").ok(), "empty input rejected");
    }

    // --- depth limit ---
    {
        std::string deep;
        for (int i = 0; i < 65; ++i) deep += "[";
        for (int i = 0; i < 65; ++i) deep += "]";
        Check(!JsonValue::Parse(deep).ok(), "depth 65 rejected");
        std::string shallow;
        for (int i = 0; i < 64; ++i) shallow += "[";
        for (int i = 0; i < 64; ++i) shallow += "]";
        Check(JsonValue::Parse(shallow).ok(), "depth 64 accepted");
    }

    // --- Json::Load schema gate (temp file) ---
    {
        const char* path = "potato_test_json_fixture.tmp.json";
        {
            std::ofstream out(path, std::ios::binary);
            out << "\xEF\xBB\xBF{\"schema\":\"potato.test/1\",\"v\":3}";
        }
        auto good = Json::Load(path, "potato.test/1");
        Check(good.ok() && good.value["v"].AsInt() == 3, "Load ok + BOM stripped");

        Check(!Json::Load(path, "potato.test/2").ok(), "wrong version rejected");
        Check(!Json::Load(path, "potato.other/1").ok(), "wrong schema name rejected");
        Check(!Json::Load("nonexistent_file_9x7.tmp", "potato.test/1").ok(), "missing file rejected");
        std::remove(path);
    }
    {
        const char* path = "potato_test_json_badschema.tmp.json";
        {
            std::ofstream out(path, std::ios::binary);
            out << "{\"schema\":123,\"v\":1}";
        }
        Check(!Json::Load(path, "potato.test/1").ok(), "non-string schema rejected");
        std::remove(path);
    }
    {
        const char* path = "potato_test_json_noschema.tmp.json";
        {
            std::ofstream out(path, std::ios::binary);
            out << "{\"v\":1}";
        }
        Check(!Json::Load(path, "potato.test/1").ok(), "missing schema rejected");
        std::remove(path);
    }

    std::printf("%s\n", failures == 0 ? "JSON TESTS PASS" : "JSON TESTS FAIL");
    return failures == 0 ? 0 : 1;
}
