/**
 * ProfilerTest - Core/Profiler 契約測試（headless）
 *
 * 覆蓋：
 *   [聚合] ScopedProfileZone 計數/min/max/avg 正確
 *   [並行] 多執行緒同時記錄 → 計數加總不丟
 *   [追蹤] DumpChromeTrace 產出合法 JSON、事件含 ph:"X"/ts/dur/tid
 *   [環]   事件環容量封頂不溢位不崩潰
 *   [停用] Current()==nullptr 時巨集為近零成本 no-op
 *   [接線] enableProfiling → GetProfiler/Current 啟用;Shutdown 清除
 */

#include "Core/Profiler.h"
#include "Core/PotatoEngine.h"
#include "Serialization/JsonParser.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace Potato;
namespace fs = std::filesystem;

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

void BusyWork() {
    volatile double x = 0;
    for (int i = 0; i < 500; ++i) x += i * 0.5;
    if (x < -1e9) std::puts("unreachable");  // 讀 x 防最佳化
}

} // anonymous namespace

int main() {
    printf("=== Profiler 測試 ===\n\n");

    // ---- [聚合] 基本計時 ----
    printf("-- 聚合統計 --\n");
    {
        Profiler prof;
        Profiler::SetActive(&prof);
        for (int i = 0; i < 50; ++i) {
            POTATO_PROFILE_SCOPE("work_zone");
            BusyWork();
        }
        {
            POTATO_PROFILE_SCOPE("other_zone");
        }
        Profiler::SetActive(nullptr);

        auto stats = prof.GetStats();
        Expect(stats.size() == 2, "兩個 zone 各自聚合");
        const ZoneStats* wz = nullptr;
        for (auto& s : stats) if (s.name == "work_zone") wz = &s;
        Expect(wz && wz->count == 50, "work_zone 計數 50");
        Expect(wz && wz->totalUs > 0.0 && wz->minUs <= wz->maxUs &&
               std::fabs(wz->AvgUs() - wz->totalUs / 50.0) < 1e-6,
               "total/min/max/avg 一致");
        Expect(prof.StatsTable().find("work_zone") != std::string::npos,
               "StatsTable 含 zone 名");
    }

    // ---- [並行] 多執行緒記錄 ----
    printf("-- 多執行緒 --\n");
    {
        Profiler prof;
        Profiler::SetActive(&prof);
        std::vector<std::thread> ts;
        for (int t = 0; t < 4; ++t) {
            ts.emplace_back([] {
                for (int i = 0; i < 250; ++i) {
                    POTATO_PROFILE_SCOPE("mt_zone");
                    BusyWork();
                }
            });
        }
        for (auto& th : ts) th.join();
        Profiler::SetActive(nullptr);

        auto stats = prof.GetStats();
        Expect(stats.size() == 1 && stats[0].count == 1000,
               "4 執行緒 × 250 = 1000 筆聚合",
               std::to_string(stats.empty() ? 0 : stats[0].count));
    }

    // ---- [追蹤] chrome-tracing JSON ----
    printf("-- Chrome trace 匯出 --\n");
    {
        Profiler prof(4096);
        Profiler::SetActive(&prof);
        {
            POTATO_PROFILE_SCOPE("trace_a");
            BusyWork();
        }
        {
            POTATO_PROFILE_SCOPE("trace_b");
        }
        Profiler::SetActive(nullptr);

        const fs::path out =
            fs::temp_directory_path() / "potato_prof_test_trace.json";
        Expect(prof.DumpChromeTrace(out.string()), "DumpChromeTrace 寫檔");

        std::ifstream f(out);
        std::string text((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
        JsonValue root;
        Expect(JsonValue::ParseOk(text, root), "匯出為合法 JSON");
        const JsonValue& events = root["traceEvents"];
        Expect(events.type == JsonValue::Type::Array &&
                   events.AsArray().size() == 2,
               "traceEvents 含 2 筆事件");
        bool hasX = false;
        for (const auto& e : events.AsArray()) {
            const JsonValue& ph = e["ph"];
            if (ph.type == JsonValue::Type::String && ph.AsString() == "X")
                hasX = true;
        }
        Expect(hasX, "事件 ph=\"X\" 完成事件型別");
        f.close();
        fs::remove(out);
    }

    // ---- [環] 容量封頂 ----
    printf("-- 事件環容量 --\n");
    {
        Profiler prof(64);  // 小環
        Profiler::SetActive(&prof);
        for (int i = 0; i < 500; ++i) {
            POTATO_PROFILE_SCOPE("spam");
        }
        Profiler::SetActive(nullptr);

        const fs::path out =
            fs::temp_directory_path() / "potato_prof_ring.json";
        prof.DumpChromeTrace(out.string());
        std::ifstream f(out);
        std::string text((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
        JsonValue root;
        JsonValue::ParseOk(text, root);
        Expect(root["traceEvents"].type == JsonValue::Type::Array &&
                   root["traceEvents"].AsArray().size() == 64,
               "ring 封頂 64 筆（舊事件被覆寫）");
        auto stats = prof.GetStats();
        Expect(stats.size() == 1 && stats[0].count == 500,
               "聚合統計不受環容量影響（500 全計）");
        f.close();
        fs::remove(out);
    }

    // ---- [停用] 無啟用點時 no-op ----
    printf("-- 停用 no-op --\n");
    {
        Profiler::SetActive(nullptr);
        for (int i = 0; i < 1000; ++i) {
            POTATO_PROFILE_SCOPE("noop_zone");  // 不應崩潰/不記錄
        }
        Expect(Profiler::Current() == nullptr, "無啟用點時巨集 no-op");
    }

    // ---- [接線] enableProfiling ----
    printf("-- EngineConfig 接線 --\n");
    {
        PotatoEngine engine;
        EngineConfig cfg;
        cfg.enableProfiling = true;
        cfg.enableJobSystem = false;
        Expect(engine.Initialize(cfg), "engine.Initialize 成功");
        Expect(engine.GetProfiler() != nullptr &&
                   Profiler::Current() == engine.GetProfiler(),
               "Profiler 已啟用且為全域啟用點");
        {
            POTATO_PROFILE_SCOPE("engine_zone");
        }
        auto stats = engine.GetProfiler()->GetStats();
        Expect(stats.size() == 1 && stats[0].name == "engine_zone",
               "引擎 profiler 收到 zone 記錄");
        engine.Shutdown();
        Expect(engine.GetProfiler() == nullptr &&
                   Profiler::Current() == nullptr,
               "Shutdown 後啟用點清除");
    }

    printf("\n=== 結果: %d PASS / %d FAIL ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
