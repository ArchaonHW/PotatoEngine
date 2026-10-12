// Story 13.2 — QuantumOracle campaign adapter.
//   - potato.oracle/1 config: parse, strict keys, seed-required
//   - DrawPermille: degenerate short-circuits, exact Bernoulli,
//     stream untouched on certainties
//   - DrawCategorical: weights honored, forced draws don't burn,
//     empty/all-zero -> -1
//   - determinism: same seed+signals -> same integer stream;
//     StreamState resume is bit-exact
//   - boundary: API surface is int/bool only (by signature)
#include "Campaign/Quantum/QuantumOracle.h"
#include "Gameplay/Json/JsonValue.h"

#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

int failures = 0;
void Check(bool cond, const char* name) {
    std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", name);
    if (!cond) ++failures;
}

using Potato::Campaign::QuantumOracle;
using Potato::Gameplay::JsonValue;

} // namespace

int main() {
    // ===== potato.oracle/1 config =====
    {
        const auto good = JsonValue::Parse(
            "{\"schema\":\"potato.oracle/1\",\"seed\":42}");
        Check(QuantumOracle::FromJson(good.value).ok(),
              "oracle: minimal doc loads");

        const auto withState = JsonValue::Parse(
            "{\"schema\":\"potato.oracle/1\",\"seed\":42,"
            "\"state\":777}");
        const auto o = QuantumOracle::FromJson(withState.value);
        Check(o.ok() && o.value.StreamState() == 777,
              "oracle: state resume counter honored");

        const auto badSchema = JsonValue::Parse(
            "{\"schema\":\"potato.oracle/2\",\"seed\":42}");
        Check(!QuantumOracle::FromJson(badSchema.value).ok(),
              "oracle: wrong schema rejected");

        const auto zeroSeed = JsonValue::Parse(
            "{\"schema\":\"potato.oracle/1\",\"seed\":0}");
        Check(!QuantumOracle::FromJson(zeroSeed.value).ok(),
              "oracle: seed 0 rejected (would be OS entropy)");

        const auto noSeed = JsonValue::Parse(
            "{\"schema\":\"potato.oracle/1\"}");
        Check(!QuantumOracle::FromJson(noSeed.value).ok(),
              "oracle: missing seed rejected");

        const auto extra = JsonValue::Parse(
            "{\"schema\":\"potato.oracle/1\",\"seed\":1,"
            "\"bogus\":9}");
        Check(!QuantumOracle::FromJson(extra.value).ok(),
              "oracle: unknown key rejected");

        const auto badState = JsonValue::Parse(
            "{\"schema\":\"potato.oracle/1\",\"seed\":1,"
            "\"state\":\"x\"}");
        Check(!QuantumOracle::FromJson(badState.value).ok(),
              "oracle: mistyped state rejected");
    }

    // ===== DrawPermille degenerate short-circuits =====
    {
        QuantumOracle o(11);
        const std::uint64_t s0 = o.StreamState();
        Check(!o.DrawPermille(0) && !o.DrawPermille(-5) &&
              o.DrawPermille(1000) && o.DrawPermille(5000),
              "permille: certainties resolve without circuit");
        Check(o.StreamState() == s0,
              "permille: certainties do not burn the stream");
    }

    // ===== DrawPermille is a real draw =====
    {
        QuantumOracle o(13);
        bool sawTrue = false, sawFalse = false;
        const std::uint64_t before = o.StreamState();
        for (int i = 0; i < 64; ++i) {
            (o.DrawPermille(500) ? sawTrue : sawFalse) = true;
        }
        Check(sawTrue && sawFalse, "permille: 500 fires both ways");
        Check(o.StreamState() != before,
              "permille: real draws consume the stream");
    }

    // ===== Determinism: same seed + signals -> same stream =====
    {
        QuantumOracle a(42), b(42);
        bool identical = true;
        for (int i = 0; i < 64; ++i) {
            if (a.DrawPermille((i * 37) % 1001) !=
                b.DrawPermille((i * 37) % 1001)) {
                identical = false;
            }
        }
        Check(identical, "determinism: same seed -> same draws");
    }

    // ===== StreamState resume is bit-exact =====
    {
        QuantumOracle a(77);
        for (int i = 0; i < 10; ++i) a.DrawPermille(500);
        const std::uint64_t mid = a.StreamState();
        QuantumOracle b(77, mid);
        bool identical = true;
        for (int i = 0; i < 20; ++i) {
            if (a.DrawPermille(500) != b.DrawPermille(500)) {
                identical = false;
            }
        }
        Check(identical, "determinism: resume continues bit-exact");
    }

    // ===== DrawCategorical =====
    {
        QuantumOracle o(29);
        const int forced[] = {0, 0, 7, 0};
        const std::uint64_t s0 = o.StreamState();
        Check(o.DrawCategorical(forced, 4) == 2,
              "categorical: single weight wins");
        Check(o.StreamState() == s0,
              "categorical: forced draw does not burn stream");

        const int empty[] = {0, 0, 0};
        Check(o.DrawCategorical(empty, 3) == -1 &&
              o.DrawCategorical(nullptr, 0) == -1,
              "categorical: empty/all-zero -> -1");

        const int two[] = {3, 5};
        bool saw0 = false, saw1 = false;
        for (int i = 0; i < 40; ++i) {
            const int v = o.DrawCategorical(two, 2);
            if (v == 0) saw0 = true;
            if (v == 1) saw1 = true;
        }
        Check(saw0 && saw1, "categorical: both outcomes occur");

        const int zeroWeight[] = {0, 4};
        bool onlyOne = true;
        for (int i = 0; i < 20; ++i) {
            onlyOne &= (o.DrawCategorical(zeroWeight, 2) == 1);
        }
        Check(onlyOne, "categorical: zero weight unreachable");
    }

    // ===== Bias sanity: heavy permille mostly fires =====
    {
        QuantumOracle o(53);
        int fires = 0;
        for (int i = 0; i < 100; ++i) fires += o.DrawPermille(900);
        Check(fires > 75, "permille: 900/1000 fires ~90%");
    }

    // ===== File boundary: Load round-trip via temp dir =====
    {
        namespace fs = std::filesystem;
        const fs::path dir =
            fs::temp_directory_path() / "potato_test_oracle";
        fs::remove_all(dir);
        fs::create_directories(dir);
        {
            std::ofstream f(dir / "o.json",
                            std::ios::binary | std::ios::trunc);
            f << "{\"schema\":\"potato.oracle/1\",\"seed\":5}";
        }
        {
            std::ofstream f(dir / "bad.json",
                            std::ios::binary | std::ios::trunc);
            f << "{\"schema\":\"potato.bencao/1\",\"seed\":5}";
        }
        auto o = QuantumOracle::Load((dir / "o.json").string());
        Check(o.ok() && o.value.StreamState() == 5,
              "file: potato.oracle/1 loads");
        auto bad = QuantumOracle::Load((dir / "bad.json").string());
        Check(!bad.ok(), "file: foreign schema rejected");
        auto missing = QuantumOracle::Load(
            (dir / "missing.json").string());
        Check(!missing.ok(), "file: absent file rejected");
        fs::remove_all(dir);
    }

    if (failures == 0) {
        std::printf("ORACLE TESTS PASS\n");
        return 0;
    }
    std::printf("%d FAILURES\n", failures);
    return 1;
}
