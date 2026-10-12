#include "Profiler.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <thread>

namespace Potato {

namespace {
std::atomic<Profiler*> g_active{nullptr};

// std::thread::id → uint32（chrome trace 的 tid 欄位）
uint32_t TidHash() {
    return static_cast<uint32_t>(
        std::hash<std::thread::id>{}(std::this_thread::get_id()));
}
} // anonymous namespace

Profiler::Profiler(size_t eventCapacity)
    : eventCapacity(eventCapacity), t0(std::chrono::steady_clock::now()) {
    events.resize(eventCapacity);
}

Profiler::~Profiler() {
    // 若自己正好是全域啟用點,順手清掉,避免懸指標
    Profiler* expected = this;
    g_active.compare_exchange_strong(expected, nullptr);
}

double Profiler::NowUs() const {
    return std::chrono::duration<double, std::micro>(
               std::chrono::steady_clock::now() - t0)
        .count();
}

void Profiler::SetActive(Profiler* p) { g_active.store(p); }
Profiler* Profiler::Current() { return g_active.load(std::memory_order_acquire); }

uint32_t Profiler::ZoneId(const char* name) {
    std::lock_guard<std::mutex> lk(mutex);
    auto it = zoneIds.find(name);
    if (it != zoneIds.end()) return it->second;
    const uint32_t id = static_cast<uint32_t>(zoneNames.size());
    zoneIds.emplace(name, id);
    zoneNames.emplace_back(name);
    stats.emplace_back();
    stats.back().name = name;
    return id;
}

void Profiler::Record(uint32_t zoneId, double startUs, double durUs) {
    std::lock_guard<std::mutex> lk(mutex);
    if (zoneId >= stats.size()) return;

    ZoneStats& s = stats[zoneId];
    ++s.count;
    s.totalUs += durUs;
    s.lastUs = durUs;
    if (s.count == 1 || durUs < s.minUs) s.minUs = durUs;
    if (durUs > s.maxUs) s.maxUs = durUs;

    if (eventCapacity > 0) {
        events[eventHead] = {zoneId, TidHash(), startUs, durUs};
        eventHead = (eventHead + 1) % eventCapacity;
        if (eventCount < eventCapacity) ++eventCount;
    }
}

std::vector<ZoneStats> Profiler::GetStats() const {
    std::lock_guard<std::mutex> lk(mutex);
    std::vector<ZoneStats> out = stats;
    std::sort(out.begin(), out.end(),
              [](const ZoneStats& a, const ZoneStats& b) {
                  return a.totalUs > b.totalUs;
              });
    return out;
}

std::string Profiler::StatsTable() const {
    auto snap = GetStats();
    std::string out;
    char line[256];
    std::snprintf(line, sizeof(line),
                  "%-32s %8s %12s %12s %12s %12s\n",
                  "Zone", "Count", "Total(ms)", "Avg(us)", "Min(us)", "Max(us)");
    out += line;
    for (const auto& s : snap) {
        std::snprintf(line, sizeof(line),
                      "%-32s %8llu %12.3f %12.2f %12.2f %12.2f\n",
                      s.name.c_str(),
                      static_cast<unsigned long long>(s.count),
                      s.totalUs / 1000.0, s.AvgUs(), s.minUs, s.maxUs);
        out += line;
    }
    return out;
}

bool Profiler::DumpChromeTrace(const std::string& path) const {
    std::vector<TraceEvent> snap;
    std::vector<std::string> names;
    {
        std::lock_guard<std::mutex> lk(mutex);
        snap.reserve(eventCount);
        // ring → 時間序（最舊→最新）
        const size_t start = (eventHead + eventCapacity - eventCount) % eventCapacity;
        for (size_t i = 0; i < eventCount; ++i)
            snap.push_back(events[(start + i) % eventCapacity]);
        names = zoneNames;
    }

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;

    f << "{\"traceEvents\":[";
    bool first = true;
    char buf[320];
    for (const auto& e : snap) {
        const char* name =
            (e.zoneId < names.size()) ? names[e.zoneId].c_str() : "?";
        // JSON escape：zone 名來自程式字串字面量,僅處理基本跳脫
        std::string esc;
        for (const char* c = name; *c; ++c) {
            if (*c == '"' || *c == '\\') esc += '\\';
            esc += *c;
        }
        int n = std::snprintf(buf, sizeof(buf),
            "%s{\"name\":\"%s\",\"ph\":\"X\",\"ts\":%.3f,\"dur\":%.3f,"
            "\"pid\":1,\"tid\":%u}",
            first ? "" : ",", esc.c_str(), e.tsUs, e.durUs, e.tidHash);
        if (n > 0) f.write(buf, n);
        first = false;
    }
    f << "]}\n";
    return f.good();
}

void Profiler::Reset() {
    std::lock_guard<std::mutex> lk(mutex);
    for (auto& s : stats) {
        s.count = 0;
        s.totalUs = s.minUs = s.maxUs = s.lastUs = 0.0;
    }
    eventHead = 0;
    eventCount = 0;
}

// ============================================================================
// ScopedProfileZone
// ============================================================================
ScopedProfileZone::ScopedProfileZone(const char* name)
    : profiler(Profiler::Current()), zone(0), startUs(0.0) {
    if (profiler) {
        zone = profiler->ZoneId(name);
        startUs = profiler->NowUs();
    }
}

ScopedProfileZone::~ScopedProfileZone() {
    if (profiler) {
        profiler->Record(zone, startUs, profiler->NowUs() - startUs);
    }
}

} // namespace Potato
