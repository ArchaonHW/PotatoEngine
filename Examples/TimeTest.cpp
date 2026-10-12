// TimeTest - TimeManager 測試：暫停語義、FPS 用真實時間、DelayedCall、Timer

#include "Time/TimeManager.h"
#include <cstdio>
#include <cmath>
#include <thread>
#include <chrono>

using namespace Potato;

static int g_pass = 0;
static int g_fail = 0;

static void Check(bool ok, const char* name) {
    if (ok) { g_pass++; printf("  [PASS] %s\n", name); }
    else    { g_fail++; printf("  [FAIL] %s\n", name); }
}

int main() {
    printf("=== Time Manager Tests ===\n\n");

    TimeManager tm;
    tm.Initialize();

    // [1] 暫停：deltaTime=0、totalTime 不動，但 FPS 用 rawDeltaTime 繼續統計
    {
        tm.Pause();
        // 跑 ~1.1 秒讓 fpsUpdateTime 跨過 1.0s 閾值
        for (int i = 0; i < 110; ++i) {
            tm.Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        Check(tm.GetDeltaTime() == 0.0f, "暫停時 deltaTime=0");
        Check(tm.GetTotalTime() == 0.0f, "暫停時 totalTime 不增長");
        Check(tm.GetCurrentFPS() > 0,
              "暫停時 FPS 仍反映真實幀率(rawDeltaTime)");
        tm.Resume();
    }

    // [2] 恢復後 deltaTime > 0 且被封頂在 100ms
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        tm.Update();
        float dt = tm.GetDeltaTime();
        Check(dt > 0.0f && dt <= 0.1f, "恢復後 deltaTime 在 (0, 0.1] 範圍");
        Check(tm.GetTotalTime() > 0.0f, "恢復後 totalTime 增長");
    }

    // [3] DelayedCall：到期觸發、未到期不觸發
    {
        bool fired = false;
        tm.DelayedCall(0.05f, [&] { fired = true; });
        tm.Update();
        Check(!fired, "DelayedCall 未到期不觸發");
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        tm.Update();
        Check(fired, "DelayedCall 到期觸發");
    }

    // [4] Timer：Start 後 elapsed 累積
    {
        TimerHandle h = tm.CreateTimer();
        tm.StartTimer(h);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        tm.Update();
        float elapsed = tm.GetElapsedTime(h);
        Check(elapsed > 0.01f, "Timer elapsed 累積");
        tm.DestroyTimer(h);
    }

    tm.Shutdown();

    printf("\n=== 結果 ===\n  PASS: %d\n  FAIL: %d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
