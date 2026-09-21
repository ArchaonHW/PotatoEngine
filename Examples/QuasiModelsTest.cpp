// QuasiModelsTest - MathUtils/CollatzHash + JammingModel 的 headless 驗證
//
// 覆蓋：
//   CollatzStoppingTime — 已知值、熔斷、零輸入
//   CollatzField01      — 值域、決定性、seed 分離、重尾分佈
//   CollatzFeature      — 桶域、決定性、退化參數
//   JammingSpeedFactor  — 自由流/阻塞邊界、單調性、NaN、退化參數
//   IsJammed / CrowdDensity — 阻塞判定、圓盤佔有率（含加權）

#include "MathUtils/CollatzHash.h"
#include "MathUtils/JammingModel.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

using namespace Potato;
using namespace Potato::Quasi;

static int g_failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); ++g_failures; } \
    else { printf("  ok: %s\n", msg); } \
} while (0)

static void TestCollatzStoppingTime() {
    printf("-- CollatzStoppingTime --\n");

    // 已知值：6→3→10→5→16→8→4→2→1 共 8 步;27 是經典長軌 111 步
    CHECK(CollatzStoppingTime(1) == 0, "n=1 停滯 0 步");
    CHECK(CollatzStoppingTime(2) == 1, "n=2 停滯 1 步");
    CHECK(CollatzStoppingTime(4) == 2, "n=4 停滯 2 步");
    CHECK(CollatzStoppingTime(6) == 8, "n=6 停滯 8 步");
    CHECK(CollatzStoppingTime(27) == 111, "n=27 停滯 111 步");

    // 熔斷：maxSteps 內未收斂回傳 maxSteps
    CHECK(CollatzStoppingTime(27, 10) == 10, "熔斷回傳 maxSteps");
    CHECK(CollatzStoppingTime(0) == 0, "n=0 視為 0 步");
}

static void TestCollatzField() {
    printf("-- CollatzField01 --\n");

    // 值域 [0,1] + 重尾：多數格低值、少數格高值
    int below = 0, total = 0;
    float maxV = 0.0f;
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const float v = CollatzField01(x, y, 7);
            if (v < 0.0f || v > 1.0f) {
                CHECK(false, "CollatzField01 值域 [0,1]");
                return;
            }
            ++total;
            if (v < 0.5f) ++below;
            if (v > maxV) maxV = v;
        }
    const float fracBelow = static_cast<float>(below) / total;
    printf("    field stats: <0.5 佔 %.3f, max=%.3f\n", fracBelow, maxV);
    CHECK(fracBelow > 0.55f, "重尾：多數格低於中值");
    CHECK(maxV > 0.5f, "稀有高值特徵存在（max>0.5)");

    // 決定性 + seed 分離
    CHECK(CollatzField01(12, 34, 7) == CollatzField01(12, 34, 7),
          "同參數同輸出（可回放）");
    bool differ = false;
    for (int i = 0; i < 64 && !differ; ++i)
        differ = CollatzField01(i, i, 1) != CollatzField01(i, i, 2);
    CHECK(differ, "不同 seed 產生不同場");

    // 退化 scale
    CHECK(CollatzField01(1, 1, 0, 0.0f) == 0.0f, "scale=0 回傳 0");
}

static void TestCollatzFeature() {
    printf("-- CollatzFeature --\n");

    const uint32_t buckets = 8;
    bool seen[8] = {};
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) {
            const uint32_t b = CollatzFeature(x, y, 3, buckets);
            if (b >= buckets) {
                CHECK(false, "CollatzFeature 值域 [0,buckets)");
                return;
            }
            seen[b] = true;
        }
    CHECK(true, "CollatzFeature 值域 [0,buckets) 維持 1024 格");

    int distinct = 0;
    for (bool s : seen) if (s) ++distinct;
    CHECK(distinct >= 3, "分佈不退化（≥3 桶被命中）");

    CHECK(CollatzFeature(5, 5, 3, buckets) == CollatzFeature(5, 5, 3, buckets),
          "CollatzFeature 決定性");
    CHECK(CollatzFeature(5, 5, 3, 0) == 0, "buckets=0 回傳 0");
}

static void TestJammingSpeedFactor() {
    printf("-- JammingSpeedFactor --\n");

    const JammingParams p; // crit=0.35 jam=0.85 minF=0.05 sharp=2

    CHECK(JammingSpeedFactor(0.0f, p) == 1.0f, "密度 0 → 自由流");
    CHECK(JammingSpeedFactor(p.criticalDensity, p) == 1.0f,
          "密度=critical → 自由流");
    CHECK(JammingSpeedFactor(p.jamDensity, p) == p.minSpeedFactor,
          "密度=jam → 阻塞殘速");
    CHECK(JammingSpeedFactor(5.0f, p) == p.minSpeedFactor,
          "密度>jam（超擠）→ 阻塞殘速");

    // 中點精確值:u=0.5 → minF+(1-minF)(1-0.5^2)=0.7625
    const float mid = JammingSpeedFactor(0.6f, p);
    CHECK(std::abs(mid - 0.7625f) < 1e-5f, "中點密度 → u^sharpness 精確值");

    // 單調遞減（jamming 的「越擠越慢」核心不變量）
    bool mono = true;
    float prev = 1.0f;
    for (float d = p.criticalDensity; d <= p.jamDensity; d += 0.01f) {
        const float f = JammingSpeedFactor(d, p);
        if (f > prev + 1e-6f) { mono = false; break; }
        prev = f;
    }
    CHECK(mono, "critical→jam 單調不增");

    // 陡度:sharpness 越大越晚降（中段更接近自由流）
    JammingParams steep = p;
    steep.sharpness = 8.0f;
    CHECK(JammingSpeedFactor(0.6f, steep) > mid,
          "sharpness↑ 中段速度更高");

    // NaN 與退化參數
    CHECK(JammingSpeedFactor(
              std::numeric_limits<float>::quiet_NaN(), p) == 1.0f,
          "NaN 密度 → 自由流（防禦）");
    JammingParams bad = p;
    bad.jamDensity = bad.criticalDensity; // 退化:jam≤critical
    CHECK(JammingSpeedFactor(0.5f, bad) == bad.minSpeedFactor,
          "退化參數密度>critical → 直接阻塞");

    CHECK(!IsJammed(p.jamDensity - 1e-4f, p), "IsJammed 邊界下");
    CHECK(IsJammed(p.jamDensity, p), "IsJammed 邊界上");
}

static void TestCrowdDensity() {
    printf("-- CrowdDensity --\n");

    const Vector2 c(0.0f, 0.0f);
    const std::vector<Vector2> empty;
    CHECK(CrowdDensity(c, empty, 10.0f, 1.0f) == 0.0f, "空鄰體 → 0");

    // π 相消:4 體 bodyR=5 於 R=10 圓盤 → 4·25/100 = 1.0
    const std::vector<Vector2> four = {
        Vector2(1.0f, 0.0f), Vector2(-1.0f, 0.0f),
        Vector2(0.0f, 1.0f), Vector2(0.0f, -1.0f)};
    CHECK(std::abs(CrowdDensity(c, four, 10.0f, 5.0f) - 1.0f) < 1e-5f,
          "佔有率公式 π 相消（4·25/100=1.0)");

    // 圓外不計
    const std::vector<Vector2> far = {Vector2(100.0f, 0.0f)};
    CHECK(CrowdDensity(c, far, 10.0f, 5.0f) == 0.0f, "圓外鄰體不計");

    // 退化參數
    CHECK(CrowdDensity(c, four, 0.0f, 5.0f) == 0.0f, "radius=0 → 0");
    CHECK(CrowdDensity(c, four, 10.0f, 0.0f) == 0.0f, "bodyRadius=0 → 0");

    // 加權版:2 體 weights{2,3} unitArea=0.1 於 R=10 → 0.5/(π·100)
    const std::vector<Vector2> two = {Vector2(0.0f, 0.0f), Vector2(1.0f, 0.0f)};
    const std::vector<float> w = {2.0f, 3.0f};
    const float expect = 0.5f / (3.14159265358979323846f * 100.0f);
    CHECK(std::abs(CrowdDensity(c, two, w, 10.0f, 0.1f) - expect) < 1e-7f,
          "加權佔有率精確值");

    // weights 短於 bodies 處以 1.0 補
    const std::vector<float> short_w = {2.0f};
    const float expect2 = (2.0f + 1.0f) * 0.1f / (3.14159265358979323846f * 100.0f);
    CHECK(std::abs(CrowdDensity(c, two, short_w, 10.0f, 0.1f) - expect2) < 1e-7f,
          "weights 不足處以 1.0 補");

    CHECK(CrowdDensity(c, two, w, 0.0f, 0.1f) == 0.0f, "加權 radius=0 → 0");
}

int main() {
    printf("=== QuasiModelsTest ===\n");
    TestCollatzStoppingTime();
    TestCollatzField();
    TestCollatzFeature();
    TestJammingSpeedFactor();
    TestCrowdDensity();

    if (g_failures == 0) {
        printf("=== ALL CHECKS PASSED ===\n");
        return 0;
    }
    printf("=== %d FAILURES ===\n", g_failures);
    return 1;
}
