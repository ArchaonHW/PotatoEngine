#pragma once

#ifndef POTATO_PROFILER_H
#define POTATO_PROFILER_H

/**
 * Profiler — 區段計時與追蹤匯出（對應 EngineConfig::enableProfiling）
 *
 * 兩層輸出：
 *   1) 聚合統計：每 zone 的 count/total/avg/min/max（StatsTable/GetStats）
 *   2) 事件環：固定容量 ring buffer 存最近 N 筆事件,
 *      DumpChromeTrace 匯出 chrome://tracing / Perfetto 可開的 JSON
 *
 * 使用（零成本停用）：POTATO_PROFILE_SCOPE("ZoneName") 展開成
 *   ScopedProfileZone——Current() 為 nullptr 時 ctor/dtor 各一次
 *   原子讀 + 分支,測量面近乎免費。
 *
 * 執行緒安全：ZoneId/Record 共用一把短臨界區 mutex；Record 只做
 *   map 更新 + ring push,不配置記憶體。
 *
 * 誠實性：單 mutex 聚合,超高頻 zone（每微秒級）會有可觀 overhead——
 *   設計給每幀數十~數千次的區段（Update/Render/Job批次）。
 */

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Potato {

// 單一 zone 的聚合統計（微秒）
struct ZoneStats {
    std::string name;
    uint64_t count = 0;
    double totalUs = 0.0;
    double minUs = 0.0;
    double maxUs = 0.0;
    double lastUs = 0.0;
    double AvgUs() const { return count > 0 ? totalUs / count : 0.0; }
};

class Profiler {
public:
    // eventCapacity：事件環容量（追蹤匯出用）,預設 65536 筆 ≈ 每筆 32B
    explicit Profiler(size_t eventCapacity = 65536);
    ~Profiler();

    Profiler(const Profiler&) = delete;
    Profiler& operator=(const Profiler&) = delete;

    // ---- 記錄面（由 ScopedProfileZone 呼叫,一般不需直接用）----
    // 名稱駐留回 zone id；同名回同 id（內部 mutex）
    uint32_t ZoneId(const char* name);
    // 記一筆：聚合統計 + 寫入事件環（ts 為相對 Profiler 建立的微秒）
    void Record(uint32_t zoneId, double startUs, double durUs);

    // ---- 查詢面 ----
    // 聚合統計快照,依 totalUs 降冪
    std::vector<ZoneStats> GetStats() const;
    // 可讀表格（固定欄寬,給 log/終端）
    std::string StatsTable() const;
    // 匯出 chrome-tracing JSON（{"traceEvents":[...]},ph="X" 完成事件）
    bool DumpChromeTrace(const std::string& path) const;
    void Reset();

    // ---- 全域啟用點 ----
    // 引擎於 enableProfiling 時 SetActive(this);Shutdown 清回 nullptr。
    // ScopedProfileZone 只碰 Current()——無啟用時 macro 近乎免費。
    static void SetActive(Profiler* p);
    static Profiler* Current();

    // 相對時間戳（微秒,單調時鐘）
    double NowUs() const;

private:
    struct TraceEvent {
        uint32_t zoneId;
        uint32_t tidHash;
        double tsUs;
        double durUs;
    };

    mutable std::mutex mutex;
    std::unordered_map<std::string, uint32_t> zoneIds;
    std::vector<std::string> zoneNames;
    std::vector<ZoneStats> stats;

    std::vector<TraceEvent> events;   // ring buffer
    size_t eventHead = 0;
    size_t eventCount = 0;
    size_t eventCapacity;

    const std::chrono::steady_clock::time_point t0;
};

// RAII 區段計時器：Current()==nullptr 時什麼都不記
class ScopedProfileZone {
public:
    explicit ScopedProfileZone(const char* name);
    ~ScopedProfileZone();

    ScopedProfileZone(const ScopedProfileZone&) = delete;
    ScopedProfileZone& operator=(const ScopedProfileZone&) = delete;

private:
    Profiler* profiler;   // 建構時快照（Current 可能之後被清,安全）
    uint32_t zone;
    double startUs;
};

} // namespace Potato

// ---- 巨集 ----
#define POTATO_PP_CAT_(a, b) a##b
#define POTATO_PP_CAT(a, b) POTATO_PP_CAT_(a, b)
#define POTATO_PROFILE_SCOPE(name) \
    ::Potato::ScopedProfileZone POTATO_PP_CAT(_ppz_, __LINE__)(name)

#endif // POTATO_PROFILER_H
