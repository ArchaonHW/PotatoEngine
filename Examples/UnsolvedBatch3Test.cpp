// UnsolvedBatch3Test - 未解決問題第三批引擎件的 headless 驗證
//
// 覆蓋（UNSOLVED_PHYSICS/MATH_TO_ENGINE 第三批）：
//   GustField    — 間歇性陣風：恆正、E[g]≈1、重尾突發、
//                  時間演化、seeded 決定性、sigma=0 退化
//   GolombRuler  — 最優尺表全異距驗證（n≤12）、貪心構造
//                  性質保持（n>12）、FormationOffsets 槽位

#include "MathUtils/GustField.h"
#include "MathUtils/GolombRuler.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace Potato;
using namespace Potato::Quasi;

static int g_failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); ++g_failures; } \
    else { printf("  ok: %s\n", msg); } \
} while (0)

static void TestGustField() {
    printf("-- GustField 間歇陣風 --\n");

    GustField gust(5, 42, 0.08f, 0.7f);
    TurbulenceField flow(4, 7, 0.15f, 1.0f);

    // 恆正 + 集合平均≈1（對數正態歸一化）
    double sum = 0.0, mx = 0.0;
    int below1 = 0, total = 0;
    for (int i = 0; i < 4000; ++i) {
        const Vector3 p(static_cast<float>(i % 37) * 1.31f,
                        static_cast<float>(i % 23) * 0.97f,
                        static_cast<float>(i % 17) * 1.73f);
        const float g = gust.Intensity(p, i * 0.13f);
        if (!(g > 0.0f)) { CHECK(false, "Intensity 恆正"); return; }
        sum += g;
        mx = mx > g ? mx : g;
        if (g < 1.0f) ++below1;
        ++total;
    }
    const double mean = sum / total;
    printf("    E[g]=%.3f  max=%.2f  g<1 佔 %.3f\n", mean, mx,
           static_cast<double>(below1) / total);
    CHECK(std::fabs(mean - 1.0) < 0.12, "集合平均 E[g]≈1");
    CHECK(mx > 2.0 * mean, "間歇性：存在 ≫均值 的突發陣風");
    CHECK(static_cast<double>(below1) / total > 0.5,
          "間歇性：多數點弱於均值（重尾）");

    // 決定性 + 時間演化 + seed 分離
    const Vector3 probe(1.5f, -2.0f, 3.7f);
    CHECK(gust.Intensity(probe, 1.23f) == gust.Intensity(probe, 1.23f),
          "同參數同輸出（可回放）");
    CHECK(gust.Intensity(probe, 0.0f) != gust.Intensity(probe, 5.0f),
          "陣風隨時間演化");
    GustField gustB(5, 43, 0.08f, 0.7f);
    CHECK(gust.Intensity(probe, 1.0f) != gustB.Intensity(probe, 1.0f),
          "異 seed 場不同");

    // sigma=0 → 恆 1（退化為無間歇）
    GustField flat(5, 42, 0.08f, 0.0f);
    CHECK(flat.Intensity(probe, 2.0f) == 1.0f, "sigma=0 → 恆定 1");

    // 複合 Sample = 基底流 × 強度
    const Vector3 v = gust.Sample(probe, 2.0f, flow);
    const Vector3 fv = flow.Sample(probe, 2.0f);
    const float gi = gust.Intensity(probe, 2.0f);
    CHECK(std::fabs(v.x - fv.x * gi) < 1e-5f &&
          std::fabs(v.z - fv.z * gi) < 1e-5f,
          "Sample = flow × intensity");
}

static void TestGolombRuler() {
    printf("-- GolombRuler 全異距尺 --\n");

    // 最優尺表：每階都通過全異距驗證 + 長度符合已知最優值
    const int kOptLen[] = {0, 1, 3, 6, 11, 17, 25, 34, 44, 55, 72, 85};
    for (int n = 1; n <= 12; ++n) {
        const std::vector<int>& m = OptimalGolombRuler(n);
        CHECK(static_cast<int>(m.size()) == n, "尺表刻度數正確");
        CHECK(IsGolomb(m), "尺表全異距");
        CHECK(m.back() == kOptLen[n - 1], "尺表長度=已知最優");
        if (g_failures) return;
    }
    printf("    n=1..12 最優尺全異距驗證通過\n");

    // GolombMarks：n≤12 回最優尺
    CHECK(GolombMarks(8) == OptimalGolombRuler(8), "n≤12 走最優尺");

    // 貪心：n>12 保持全異距（長度非最優——誠實標註）
    const std::vector<int> g13 = GolombMarks(13);
    CHECK(static_cast<int>(g13.size()) == 13 && IsGolomb(g13),
          "n=13 貪心構造仍全異距");
    CHECK(g13.back() > 85, "貪心尺長 > n=12 最優（非最優性）");
    printf("    n=13 貪心尺長 %d（最優 106）\n", g13.back());

    const std::vector<int> g25 = GolombMarks(25);
    CHECK(static_cast<int>(g25.size()) == 25 && IsGolomb(g25),
          "n=25 貪心構造仍全異距");

    // 退化輸入
    CHECK(GolombMarks(0).empty() && GolombMarks(-3).empty(),
          "n<=0 回空表");
    CHECK(OptimalGolombRuler(99).empty(), "表外 n 回空表");
}

static void TestFormationOffsets() {
    printf("-- FormationOffsets 編隊槽位 --\n");

    const auto slots = FormationOffsets(8, 0.5f);
    CHECK(slots.size() == 8, "n 個槽位");
    CHECK(slots[0].LengthSquared() == 0.0f, "第 0 槽在原點");

    // 半徑沿 Golomb 尺 → 兩兩槽位到原點距離全異
    bool radiiDistinct = true;
    for (size_t i = 0; i < slots.size() && radiiDistinct; ++i)
        for (size_t j = i + 1; j < slots.size(); ++j)
            if (std::fabs(slots[i].Length() - slots[j].Length()) < 1e-5f) {
                radiiDistinct = false;
                break;
            }
    CHECK(radiiDistinct, "槽位半徑全異（去同心環）");

    // 退化輸入
    CHECK(FormationOffsets(0, 1.0f).empty(), "n=0 回空表");
    CHECK(FormationOffsets(4, 0.0f).empty(), "spacing=0 回空表");
}

int main() {
    printf("=== UnsolvedBatch3Test ===\n");
    TestGustField();
    TestGolombRuler();
    TestFormationOffsets();

    if (g_failures == 0) {
        printf("=== ALL CHECKS PASSED ===\n");
        return 0;
    }
    printf("=== %d FAILURES ===\n", g_failures);
    return 1;
}
