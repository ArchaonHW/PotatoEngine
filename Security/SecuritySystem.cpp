#include "SecuritySystem.h"

#include <chrono>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <fstream>
#include <algorithm>

#ifdef _WIN32
    #include <windows.h>
    #include <psapi.h>
    #pragma comment(lib, "psapi.lib")
#elif defined(__linux__)
    #include <fstream>
#elif defined(__APPLE__)
    #include <sys/types.h>
    #include <sys/sysctl.h>
    #include <unistd.h>
#endif

namespace Potato {
namespace Security {

// ============================================================================
// SHA-256 實作（自包含，無外部依賴）
// ============================================================================
namespace {

struct Sha256Ctx {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  buffer[64];
    size_t   bufferLen;
};

const uint32_t kSha256K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

inline uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

void Sha256Transform(Sha256Ctx& ctx, const uint8_t* block) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = (uint32_t(block[i*4]) << 24) | (uint32_t(block[i*4+1]) << 16) |
               (uint32_t(block[i*4+2]) << 8) | uint32_t(block[i*4+3]);
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr(w[i-15], 7) ^ rotr(w[i-15], 18) ^ (w[i-15] >> 3);
        uint32_t s1 = rotr(w[i-2], 17) ^ rotr(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a = ctx.state[0], b = ctx.state[1], c = ctx.state[2], d = ctx.state[3];
    uint32_t e = ctx.state[4], f = ctx.state[5], g = ctx.state[6], h = ctx.state[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + S1 + ch + kSha256K[i] + w[i];
        uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    ctx.state[0] += a; ctx.state[1] += b; ctx.state[2] += c; ctx.state[3] += d;
    ctx.state[4] += e; ctx.state[5] += f; ctx.state[6] += g; ctx.state[7] += h;
}

void Sha256Init(Sha256Ctx& ctx) {
    ctx.state[0] = 0x6a09e667; ctx.state[1] = 0xbb67ae85;
    ctx.state[2] = 0x3c6ef372; ctx.state[3] = 0xa54ff53a;
    ctx.state[4] = 0x510e527f; ctx.state[5] = 0x9b05688c;
    ctx.state[6] = 0x1f83d9ab; ctx.state[7] = 0x5be0cd19;
    ctx.bitlen = 0;
    ctx.bufferLen = 0;
}

void Sha256Update(Sha256Ctx& ctx, const uint8_t* data, size_t len) {
    ctx.bitlen += uint64_t(len) * 8;
    while (len > 0) {
        size_t need = 64 - ctx.bufferLen;
        size_t take = (len < need) ? len : need;
        memcpy(ctx.buffer + ctx.bufferLen, data, take);
        ctx.bufferLen += take;
        data += take;
        len -= take;
        if (ctx.bufferLen == 64) {
            Sha256Transform(ctx, ctx.buffer);
            ctx.bufferLen = 0;
        }
    }
}

void Sha256Final(Sha256Ctx& ctx, uint8_t out[32]) {
    ctx.buffer[ctx.bufferLen++] = 0x80;
    if (ctx.bufferLen > 56) {
        while (ctx.bufferLen < 64) ctx.buffer[ctx.bufferLen++] = 0;
        Sha256Transform(ctx, ctx.buffer);
        ctx.bufferLen = 0;
    }
    while (ctx.bufferLen < 56) ctx.buffer[ctx.bufferLen++] = 0;
    for (int i = 7; i >= 0; i--) {
        ctx.buffer[ctx.bufferLen++] = uint8_t(ctx.bitlen >> (i * 8));
    }
    Sha256Transform(ctx, ctx.buffer);
    for (int i = 0; i < 8; i++) {
        out[i*4]   = uint8_t(ctx.state[i] >> 24);
        out[i*4+1] = uint8_t(ctx.state[i] >> 16);
        out[i*4+2] = uint8_t(ctx.state[i] >> 8);
        out[i*4+3] = uint8_t(ctx.state[i]);
    }
}

} // anonymous namespace

std::string ComputeSHA256(const void* data, size_t size) {
    Sha256Ctx ctx;
    Sha256Init(ctx);
    Sha256Update(ctx, static_cast<const uint8_t*>(data), size);
    uint8_t hash[32];
    Sha256Final(ctx, hash);

    static const char* hexChars = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (int i = 0; i < 32; i++) {
        result += hexChars[hash[i] >> 4];
        result += hexChars[hash[i] & 0x0f];
    }
    return result;
}

std::string ComputeFileSHA256(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return "";
    }
    Sha256Ctx ctx;
    Sha256Init(ctx);
    char buf[8192];
    while (file.read(buf, sizeof(buf)) || file.gcount() > 0) {
        Sha256Update(ctx, reinterpret_cast<const uint8_t*>(buf),
                     static_cast<size_t>(file.gcount()));
    }
    uint8_t hash[32];
    Sha256Final(ctx, hash);

    static const char* hexChars = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (int i = 0; i < 32; i++) {
        result += hexChars[hash[i] >> 4];
        result += hexChars[hash[i] & 0x0f];
    }
    return result;
}

// ============================================================================
// CRC32（用於記憶體完整性快照 - 快速、非密碼學用途）
// ============================================================================
uint32_t ComputeCRC32(const void* data, size_t size) {
    static uint32_t table[256];
    static bool tableInit = false;
    if (!tableInit) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int j = 0; j < 8; j++) {
                c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
            }
            table[i] = c;
        }
        tableInit = true;
    }
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; i++) {
        crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

// ============================================================================
// SecureZeroMemory - 保證清除敏感資料不被編譯器最佳化掉
// ============================================================================
void SecureZeroMemory(void* ptr, size_t size) {
    if (!ptr || size == 0) return;
#ifdef _WIN32
    ::SecureZeroMemory(ptr, size);
#else
    volatile uint8_t* p = static_cast<volatile uint8_t*>(ptr);
    while (size--) {
        *p++ = 0;
    }
#endif
}

// ============================================================================
// SecurityManager
// ============================================================================
SecurityManager& SecurityManager::GetInstance() {
    static SecurityManager instance;
    return instance;
}

SecurityManager::~SecurityManager() {
    Shutdown();
}

bool SecurityManager::Initialize() {
    if (initialized.exchange(true)) {
        return true; // 已初始化
    }

#ifdef _WIN32
    // Windows 預設信任模組（系統 DLL 白名單）
    const char* defaultTrusted[] = {
        "kernel32.dll", "ntdll.dll", "user32.dll", "gdi32.dll",
        "advapi32.dll", "msvcrt.dll", "ole32.dll", "shell32.dll",
        "ws2_32.dll", "winmm.dll", "opengl32.dll", "glu32.dll",
        "imm32.dll", "version.dll", "shlwapi.dll", "comdlg32.dll",
        "comctl32.dll", "dwmapi.dll", "uxtheme.dll", "kernel.appcore.dll",
        "msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll",
        "vcruntime140.dll", "vcruntime140_1.dll", "ucrtbase.dll",
        "bcryptprimitives.dll", "crypt32.dll", "bcrypt.dll",
        "sechost.dll", "rpcrt4.dll", "setupapi.dll", "cfgmgr32.dll",
        "wintrust.dll", "msasn1.dll", "shcore.dll", "combase.dll",
        "windows.storage.dll", "profapi.dll", "powrprof.dll",
        "umpdc.dll", "iphlpapi.dll", "dnsapi.dll", "nsi.dll",
        "mswsock.dll", "napinsp.dll", "pnrpnsp.dll", "nlaapi.dll",
        "winrnr.dll", "wshbth.dll", "rasadhlp.dll", "fwpuclnt.dll",
        "wininet.dll", "urlmon.dll", "iertutil.dll", "sspicli.dll",
        "psapi.dll", "dbghelp.dll", "dxgi.dll", "d3d11.dll",
        "d3dcompiler_47.dll", "xinput1_4.dll", "xinput9_1_0.dll",
        "dsound.dll", "xaudio2_9.dll", "avrt.dll", "devobj.dll"
    };
    for (const char* name : defaultTrusted) {
        trustedModules.push_back(name);
    }
#endif
    return true;
}

void SecurityManager::Shutdown() {
    StopMonitoring();
    initialized = false;
}

void SecurityManager::SetViolationCallback(ViolationCallback callback) {
    std::lock_guard<std::mutex> lock(callbackMutex);
    violationCallback = std::move(callback);
}

void SecurityManager::ReportViolation(ViolationType type, const std::string& details) {
    std::lock_guard<std::mutex> lock(callbackMutex);
    if (violationCallback) {
        SecurityReport report;
        report.type = type;
        report.details = details;
        violationCallback(report);
    }
}

// ---- 反除錯 ----

bool SecurityManager::CheckDebugger() {
#ifdef _WIN32
    if (::IsDebuggerPresent()) {
        ReportViolation(ViolationType::DebuggerDetected, "IsDebuggerPresent() == true");
        return true;
    }
    BOOL remoteDebugger = FALSE;
    if (::CheckRemoteDebuggerPresent(::GetCurrentProcess(), &remoteDebugger) && remoteDebugger) {
        ReportViolation(ViolationType::RemoteDebuggerDetected, "CheckRemoteDebuggerPresent() == true");
        return true;
    }
#elif defined(__linux__)
    // Linux: 檢查 /proc/self/status 的 TracerPid
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.compare(0, 9, "TracerPid") == 0) {
            // TracerPid:\t<pid>
            size_t pos = line.find_last_of("\t ");
            if (pos != std::string::npos) {
                int tracerPid = std::atoi(line.c_str() + pos + 1);
                if (tracerPid != 0) {
                    ReportViolation(ViolationType::TracerDetected,
                        "TracerPid = " + std::to_string(tracerPid));
                    return true;
                }
            }
            break;
        }
    }
#elif defined(__APPLE__)
    // macOS: 檢查 P_TRACED flag
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid() };
    struct kinfo_proc info;
    size_t size = sizeof(info);
    if (sysctl(mib, 4, &info, &size, nullptr, 0) == 0) {
        if (info.kp_proc.p_flag & P_TRACED) {
            ReportViolation(ViolationType::TracerDetected, "P_TRACED flag set");
            return true;
        }
    }
#endif
    return false;
}

bool SecurityManager::CheckTimingAnomaly() {
    // 執行一段固定的簡單運算，正常情況下應在極短時間內完成
    // 若被單步執行或除錯器中斷，耗時會異常拉長
    auto start = std::chrono::steady_clock::now();

    volatile uint32_t x = 0;
    for (int i = 0; i < 10000; i++) {
        x = x * 31 + static_cast<uint32_t>(i);
    }
    (void)x;

    auto end = std::chrono::steady_clock::now();
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    // 正常情況 < 1ms；超過 50ms 視為異常
    if (elapsedMs > 50) {
        ReportViolation(ViolationType::TimingAnomaly,
            "Timing check took " + std::to_string(elapsedMs) + "ms (expected <50ms)");
        return true;
    }
    return false;
}

// ---- DLL 注入偵測 ----

void SecurityManager::AddTrustedModule(const std::string& moduleName) {
    std::lock_guard<std::mutex> lock(trustedMutex);
    std::string lower = moduleName;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    trustedModules.push_back(lower);
}

std::vector<std::string> SecurityManager::FindUntrustedModules() {
    std::vector<std::string> untrusted;

#ifdef _WIN32
    HANDLE hProcess = ::GetCurrentProcess();
    HMODULE modules[1024];
    DWORD needed = 0;

    if (::EnumProcessModules(hProcess, modules, sizeof(modules), &needed)) {
        DWORD count = needed / sizeof(HMODULE);
        for (DWORD i = 0; i < count; i++) {
            char moduleName[MAX_PATH];
            if (::GetModuleFileNameExA(hProcess, modules[i], moduleName, MAX_PATH)) {
                // 取檔名部分並轉小寫
                std::string fullPath(moduleName);
                size_t sep = fullPath.find_last_of("\\/");
                std::string name = (sep != std::string::npos) ? fullPath.substr(sep + 1) : fullPath;
                std::transform(name.begin(), name.end(), name.begin(), ::tolower);

                std::lock_guard<std::mutex> lock(trustedMutex);
                bool trusted = std::find(trustedModules.begin(), trustedModules.end(), name)
                               != trustedModules.end();
                if (!trusted) {
                    untrusted.push_back(name);
                }
            }
        }
    }
#elif defined(__linux__)
    // Linux: 檢查 /proc/self/maps 中的共享庫
    std::ifstream maps("/proc/self/maps");
    std::string line;
    std::vector<std::string> seen;
    while (std::getline(maps, line)) {
        size_t pos = line.find_last_of('/');
        if (pos != std::string::npos && line.find(".so") != std::string::npos) {
            std::string lib = line.substr(pos + 1);
            lib = lib.substr(0, lib.find('\n'));
            if (std::find(seen.begin(), seen.end(), lib) == seen.end()) {
                seen.push_back(lib);
                std::string lower = lib;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                std::lock_guard<std::mutex> lock(trustedMutex);
                if (std::find(trustedModules.begin(), trustedModules.end(), lower) == trustedModules.end()) {
                    untrusted.push_back(lib);
                }
            }
        }
    }
#endif
    return untrusted;
}

bool SecurityManager::CheckLoadedModules() {
    auto untrusted = FindUntrustedModules();
    if (!untrusted.empty()) {
        std::string names;
        for (const auto& n : untrusted) {
            if (!names.empty()) names += ", ";
            names += n;
        }
        ReportViolation(ViolationType::UntrustedModule,
            "Untrusted modules loaded: " + names);
        return true;
    }
    return false;
}

// ---- 完整性校驗 ----

bool SecurityManager::VerifyFileIntegrity(const std::string& filePath,
                                          const std::string& expectedSha256Hex) {
    std::string actual = ComputeFileSHA256(filePath);
    if (actual.empty()) {
        ReportViolation(ViolationType::IntegrityMismatch,
            "Cannot read file: " + filePath);
        return false;
    }
    if (actual != expectedSha256Hex) {
        ReportViolation(ViolationType::IntegrityMismatch,
            "File tampered: " + filePath +
            " (expected " + expectedSha256Hex.substr(0, 16) + "...)");
        return false;
    }
    return true;
}

uint32_t SecurityManager::GuardRegion(const void* data, size_t size) {
    std::lock_guard<std::mutex> lock(guardMutex);
    uint32_t id = nextGuardId++;
    guardedRegions.emplace_back(id, ComputeCRC32(data, size));
    return id;
}

bool SecurityManager::VerifyRegion(uint32_t guardId, const void* data, size_t size) {
    std::lock_guard<std::mutex> lock(guardMutex);
    for (const auto& [id, checksum] : guardedRegions) {
        if (id == guardId) {
            uint32_t current = ComputeCRC32(data, size);
            if (current != checksum) {
                ReportViolation(ViolationType::MemoryTampered,
                    "Guarded region " + std::to_string(guardId) + " modified");
                return false;
            }
            return true;
        }
    }
    return false; // guardId 不存在
}

void SecurityManager::UnguardRegion(uint32_t guardId) {
    std::lock_guard<std::mutex> lock(guardMutex);
    guardedRegions.erase(
        std::remove_if(guardedRegions.begin(), guardedRegions.end(),
            [guardId](const auto& p) { return p.first == guardId; }),
        guardedRegions.end());
}

// ---- 定期監控 ----

void SecurityManager::StartMonitoring(uint32_t intervalMs) {
    if (monitoring.exchange(true)) return;
    monitorThread = std::thread(&SecurityManager::MonitorLoop, this, intervalMs);
}

void SecurityManager::StopMonitoring() {
    if (monitoring.exchange(false)) {
        if (monitorThread.joinable()) {
            monitorThread.join();
        }
    }
}

void SecurityManager::MonitorLoop(uint32_t intervalMs) {
    while (monitoring.load()) {
        CheckDebugger();
        CheckTimingAnomaly();
        CheckLoadedModules();

        // 分段睡眠，讓 StopMonitoring 能快速生效
        uint32_t slept = 0;
        while (slept < intervalMs && monitoring.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            slept += 50;
        }
    }
}

bool SecurityManager::RunAllChecks() {
    bool violated = false;
    violated |= CheckDebugger();
    violated |= CheckTimingAnomaly();
    violated |= CheckLoadedModules();
    return violated;
}

const char* ViolationTypeToString(ViolationType type) {
    switch (type) {
        case ViolationType::None:                  return "None";
        case ViolationType::DebuggerDetected:      return "DebuggerDetected";
        case ViolationType::RemoteDebuggerDetected:return "RemoteDebuggerDetected";
        case ViolationType::TracerDetected:        return "TracerDetected";
        case ViolationType::TimingAnomaly:         return "TimingAnomaly";
        case ViolationType::UntrustedModule:       return "UntrustedModule";
        case ViolationType::IntegrityMismatch:     return "IntegrityMismatch";
        case ViolationType::MemoryTampered:        return "MemoryTampered";
    }
    return "Unknown";
}

} // namespace Security
} // namespace Potato
