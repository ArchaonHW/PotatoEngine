/**
 * JobSystemTest - Core/JobSystem 行為契約測試（headless）
 *
 * 覆蓋：
 *   [投遞] Submit N 件 → WaitIdle 後全數執行
 *   [批次] ParallelFor 每索引恰執行一次、總和正確
 *   [粒度] batchSize 自動/手動；count=0 回傳不做事
 *   [等待] help-run：單 worker 池也能完成（呼叫端參與執行）
 *   [巢狀] 工作內再 ParallelFor 不自鎖
 *   [析構] 帶未完成工作解構 → 排空後才 join
 *   [接線] PotatoEngine enableJobSystem → GetJobSystem() 可用
 */

#include "Core/JobSystem.h"
#include "Core/PotatoEngine.h"

#include <atomic>
#include <cstdio>
#include <numeric>
#include <vector>

using namespace Potato;

namespace {

int g_pass = 0;
int g_fail = 0;

void Expect(bool cond, const char* name, const char* detail = "") {
    if (cond) {
        g_pass++;
        printf("  [PASS] %s%s%s\n", name, *detail ? "  -- " : "", detail);
    } else {
        g_fail++;
        printf("  [FAIL] %s%s%s\n", name, *detail ? "  -- " : "", detail);
    }
}

} // anonymous namespace

int main() {
    printf("=== JobSystem 測試 ===\n\n");

    // ---- [投遞] Submit + WaitIdle ----
    printf("-- Submit / WaitIdle --\n");
    {
        JobSystem pool(4);
        Expect(pool.WorkerCount() == 4, "worker 數 = 4");
        Expect(pool.IsRunning(), "pool 運行中");

        std::atomic<int> counter{0};
        constexpr int kJobs = 500;
        for (int i = 0; i < kJobs; ++i)
            pool.Submit([&] { counter.fetch_add(1, std::memory_order_relaxed); });
        pool.WaitIdle();
        Expect(counter.load() == kJobs, "500 件工作全數完成",
               std::to_string(counter.load()).c_str());
        Expect(pool.InFlightCount() == 0, "WaitIdle 後無在途工作");
    }

    // ---- [批次] ParallelFor 正確性 ----
    printf("-- ParallelFor --\n");
    {
        JobSystem pool(0);  // 自動 worker 數
        std::vector<std::atomic<int>> hits(10000);
        pool.ParallelFor(10000, [&](uint32_t i) {
            hits[i].fetch_add(1, std::memory_order_relaxed);
        });
        int64_t sum = 0;
        for (auto& h : hits) sum += h.load();
        Expect(sum == 10000, "每索引恰執行一次",
               std::to_string(sum).c_str());

        // 總和正確（純計算批次）
        std::vector<int> squares(1000, 0);
        pool.ParallelFor(1000, [&](uint32_t i) {
            squares[i] = static_cast<int>(i * i);
        }, /*batchSize=*/16);
        int64_t s = 0;
        for (int v : squares) s += v;
        // Σi², i∈[0,999] = 999·1000·1999/6 = 332833500
        Expect(s == 999LL * 1000LL * (2 * 999 + 1) / 6,
               "batchSize=16 批次結果正確", std::to_string(s).c_str());

        // count=0 → no-op
        std::atomic<int> ran{0};
        pool.ParallelFor(0, [&](uint32_t) { ++ran; });
        Expect(ran.load() == 0, "count=0 不執行");
    }

    // ---- [help-run] 單 worker 也完成 ----
    printf("-- help-run --\n");
    {
        JobSystem pool(1);  // 單工人:等待端若只睡會很慢,help-run 保底
        std::atomic<int> done{0};
        pool.ParallelFor(200, [&](uint32_t) {
            done.fetch_add(1, std::memory_order_relaxed);
        }, 4);
        Expect(done.load() == 200, "單 worker 池 ParallelFor 完成");
    }

    // ---- [巢狀] 工作內再投遞 ----
    printf("-- 巢狀投遞 --\n");
    {
        JobSystem pool(2);
        std::atomic<int> inner{0};
        pool.ParallelFor(8, [&](uint32_t) {
            pool.ParallelFor(25, [&](uint32_t) {
                inner.fetch_add(1, std::memory_order_relaxed);
            });
        });
        Expect(inner.load() == 8 * 25, "巢狀 ParallelFor 全數完成",
               std::to_string(inner.load()).c_str());
    }

    // ---- [析構] 排空才 join ----
    printf("-- 析構排空 --\n");
    {
        std::atomic<int> ran{0};
        {
            JobSystem pool(2);
            for (int i = 0; i < 50; ++i)
                pool.Submit([&] { ++ran; });
            // 不 WaitIdle 直接解構 —— 析構應排空
        }
        Expect(ran.load() == 50, "解構前排空全部工作",
               std::to_string(ran.load()).c_str());
    }

    // ---- [接線] EngineConfig 驅動 ----
    printf("-- EngineConfig 接線 --\n");
    {
        PotatoEngine engine;
        EngineConfig cfg;
        cfg.enableJobSystem = true;
        cfg.workerThreads = 2;
        Expect(engine.Initialize(cfg), "engine.Initialize 成功");
        JobSystem* js = engine.GetJobSystem();
        Expect(js != nullptr && js->WorkerCount() == 2,
               "GetJobSystem() 回 2-worker 池");
        std::atomic<int> n{0};
        js->ParallelFor(100, [&](uint32_t) { ++n; });
        Expect(n.load() == 100, "經引擎池 ParallelFor 完成");
        engine.Shutdown();
        Expect(engine.GetJobSystem() == nullptr,
               "Shutdown 後 GetJobSystem 為空");

        // 停用時不建池
        PotatoEngine engine2;
        EngineConfig cfg2;
        cfg2.enableJobSystem = false;
        engine2.Initialize(cfg2);
        Expect(engine2.GetJobSystem() == nullptr,
               "enableJobSystem=false 不建池");
        engine2.Shutdown();
    }

    printf("\n=== 結果: %d PASS / %d FAIL ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
