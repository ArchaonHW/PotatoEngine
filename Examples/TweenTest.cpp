/**
 * TweenTest - MathUtils/Tween.h 插值工具測試（headless）
 *
 * 覆蓋：
 *   [曲線] 各 Ease 端點值 t=0→0、t=1→1;單調性抽樣
 *   [插補] Tween 值推進、delay、Finished、Reset
 *   [轉向] Retarget 位置連續
 *   [決定] 同輸入同輸出（float 純函數）
 */

#include "MathUtils/Tween.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace Potato;

namespace {

int g_pass = 0;
int g_fail = 0;

void Expect(bool cond, const char* name, const std::string& detail = "") {
    if (cond) {
        g_pass++;
        printf("  [PASS] %s%s%s\n", name,
               detail.empty() ? "" : "  -- ", detail.c_str());
    } else {
        g_fail++;
        printf("  [FAIL] %s%s%s\n", name,
               detail.empty() ? "" : "  -- ", detail.c_str());
    }
}

} // anonymous namespace

int main() {
    printf("=== Tween 測試 ===\n\n");

    // ---- [曲線端點] ----
    printf("-- Ease 端點 --\n");
    {
        const Tween::Ease all[] = {
            Tween::Ease::Linear,
            Tween::Ease::QuadIn, Tween::Ease::QuadOut,
            Tween::Ease::QuadInOut,
            Tween::Ease::CubicIn, Tween::Ease::CubicOut,
            Tween::Ease::CubicInOut,
            Tween::Ease::SineIn, Tween::Ease::SineOut,
            Tween::Ease::SineInOut,
            Tween::Ease::ExpoIn, Tween::Ease::ExpoOut,
            Tween::Ease::ExpoInOut,
            Tween::Ease::BackIn, Tween::Ease::BackOut,
            Tween::Ease::BackInOut,
            Tween::Ease::BounceOut, Tween::Ease::ElasticOut,
        };
        bool endpointsOk = true;
        for (auto e : all) {
            const float a = Tween::Evaluate(e, 0.0f);
            const float b = Tween::Evaluate(e, 1.0f);
            if (std::fabs(a) > 1e-4f || std::fabs(b - 1.0f) > 1e-4f)
                endpointsOk = false;
        }
        Expect(endpointsOk, "全部 Ease 端點 0→0、1→1");
        Expect(std::fabs(Tween::Evaluate(Tween::Ease::Linear, 0.5f) -
                         0.5f) < 1e-6f,
               "Linear 中點 0.5");
        // Out 系在中點應比 Linear 快
        Expect(Tween::Evaluate(Tween::Ease::QuadOut, 0.5f) > 0.5f &&
               Tween::Evaluate(Tween::Ease::QuadIn, 0.5f) < 0.5f,
               "QuadIn 慢起/QuadOut 快起");
        // BackOut 過衝
        Expect(Tween::Evaluate(Tween::Ease::BackOut, 0.8f) > 1.0f,
               "BackOut 過衝 >1");
    }

    // ---- [Tween 值推進] ----
    printf("-- Tween 推進 --\n");
    {
        Tween::Tween tw(0.0f, 100.0f, 2.0f, Tween::Ease::Linear);
        Expect(tw.Value() == 0.0f, "初始值=from");
        tw.Update(1.0f);
        Expect(std::fabs(tw.Value() - 50.0f) < 1e-4f, "半程=50");
        Expect(!tw.Finished(), "半程未完");
        tw.Update(1.5f);  // 超過 duration
        Expect(tw.Value() == 100.0f && tw.Finished(), "夾在 to + Finished");
        tw.Reset();
        Expect(tw.Value() == 0.0f && !tw.Started(), "Reset 歸零");
    }
    {
        Tween::Tween tw(0.0f, 10.0f, 1.0f, Tween::Ease::Linear, 0.5f);
        tw.Update(0.3f);
        Expect(tw.Value() == 0.0f && !tw.Started(), "delay 期間不動");
        tw.Update(0.5f);  // elapsed=0.8, progress=0.3
        Expect(std::fabs(tw.Value() - 3.0f) < 1e-4f,
               "delay 後 progress=0.3 → 3.0",
               std::to_string(tw.Value()));
    }
    {
        Tween::Tween tw(0.0f, 10.0f, 1.0f, Tween::Ease::QuadOut);
        tw.Update(0.5f);
        const float mid = tw.Value();
        tw.Retarget(20.0f, 1.0f);
        Expect(std::fabs(tw.Value() - mid) < 1e-4f,
               "Retarget 位置連續");
    }

    // ---- [決定性] ----
    printf("-- 決定性 --\n");
    {
        const float a = Tween::Evaluate(Tween::Ease::ElasticOut, 0.37f);
        const float b = Tween::Evaluate(Tween::Ease::ElasticOut, 0.37f);
        Expect(a == b, "同輸入逐位相同");
    }

    printf("\n=== 結果: %d PASS / %d FAIL ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
