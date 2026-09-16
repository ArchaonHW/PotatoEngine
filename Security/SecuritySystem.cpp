#include "SecuritySystem.h"

#include <chrono>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <cwctype>
#include <fstream>
#include <algorithm>
#include <filesystem>

#ifdef _WIN32
    #include <windows.h>
    #include <psapi.h>
    #include <wintrust.h>
    #include <softpub.h>
    #include <bcrypt.h>
    #include <winternl.h>
    #include <tlhelp32.h>
    #pragma comment(lib, "psapi.lib")
    #pragma comment(lib, "wintrust.lib")
    #pragma comment(lib, "bcrypt.lib")
    #pragma comment(lib, "ntdll.lib")
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

// 字串轉小寫（修正 tolower 的 int->char 轉換警告）
inline std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

#ifdef _WIN32
// 寬字串轉小寫
inline std::wstring ToLowerW(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(),
        [](wchar_t c) { return static_cast<wchar_t>(::towlower(c)); });
    return s;
}

// UTF-8 <-> UTF-16 轉換
inline std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int len = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(len - 1, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), len);
    return w;
}

inline std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int len = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(len - 1, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), len, nullptr, nullptr);
    return s;
}
#endif

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

namespace {

std::string HashStreamToHex(std::istream& file) {
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

#ifdef _WIN32
// 寬路徑版本（fs::path 可正確處理非 ASCII 檔名）
std::string ComputeFileSHA256W(const std::wstring& filePath) {
    std::ifstream file(std::filesystem::path(filePath), std::ios::binary);
    if (!file.is_open()) return "";
    return HashStreamToHex(file);
}
#endif

// HMAC-SHA256（RFC 2104）：用隨機金鑰做記憶體完整性校驗，
// 攻擊者竄改資料後無法重算正確的校驗值（CRC32 則可被任意偽造）
void HmacSha256(const uint8_t* key, size_t keyLen,
                const void* data, size_t dataLen, uint8_t out[32]) {
    uint8_t k[64] = {};
    if (keyLen > 64) {
        // 長金鑰先壓縮
        Sha256Ctx c;
        Sha256Init(c);
        Sha256Update(c, key, keyLen);
        Sha256Final(c, k);
    } else {
        memcpy(k, key, keyLen);
    }
    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; i++) {
        ipad[i] = k[i] ^ 0x36;
        opad[i] = k[i] ^ 0x5c;
    }
    uint8_t inner[32];
    Sha256Ctx c;
    Sha256Init(c);
    Sha256Update(c, ipad, 64);
    Sha256Update(c, static_cast<const uint8_t*>(data), dataLen);
    Sha256Final(c, inner);
    Sha256Init(c);
    Sha256Update(c, opad, 64);
    Sha256Update(c, inner, 32);
    Sha256Final(c, out);
}

} // anonymous namespace

std::string ComputeFileSHA256(const std::string& filePath) {
#ifdef _WIN32
    // UTF-8 -> UTF-16，正確處理非 ASCII 路徑（u8path 在 C++20 已棄用）
    std::ifstream file(std::filesystem::path(Utf8ToWide(filePath)), std::ios::binary);
#else
    std::ifstream file(filePath, std::ios::binary);
#endif
    if (!file.is_open()) {
        return "";
    }
    return HashStreamToHex(file);
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

    // 產生記憶體完整性校驗用的隨機金鑰（每行程不同，攻擊者無法預測/重算 MAC）
#ifdef _WIN32
    if (FAILED(::BCryptGenRandom(nullptr, macKey, sizeof(macKey),
                                 BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
        // 備援：混合多種難以預測的來源
        uint64_t seed = static_cast<uint64_t>(::GetTickCount64()) ^
                        (static_cast<uint64_t>(::GetCurrentProcessId()) << 32) ^
                        reinterpret_cast<uint64_t>(this);
        for (size_t i = 0; i < sizeof(macKey); i++) {
            seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; // xorshift64
            macKey[i] = static_cast<uint8_t>(seed);
        }
    }
#endif

#ifdef _WIN32
    // Windows 系統 DLL 白名單：除了檔名相符，還必須位於系統目錄
    // 且通過 Authenticode 簽章驗證（防止冒名繞過）
    const char* defaultSystem[] = {
        "kernel32.dll", "ntdll.dll", "user32.dll", "gdi32.dll",
        "advapi32.dll", "msvcrt.dll", "ole32.dll", "shell32.dll",
        "ws2_32.dll", "winmm.dll", "opengl32.dll", "glu32.dll",
        "imm32.dll", "version.dll", "shlwapi.dll", "comdlg32.dll",
        "comctl32.dll", "dwmapi.dll", "uxtheme.dll", "kernel.appcore.dll",
        "kernelbase.dll", "msvcp_win.dll", "gdi32full.dll", "win32u.dll",
        "oleaut32.dll", "apphelp.dll", "ntmarta.dll", "cryptbase.dll",
        "cryptsp.dll", "rsaenh.dll", "userenv.dll", "wintypes.dll",
        "msctf.dll", "textinputframework.dll", "coremessaging.dll",
        "twinapi.appcore.dll", "dxcore.dll", "d3d12.dll",
        "wow64.dll", "wow64win.dll", "wow64cpu.dll",
        "bcryptprimitives.dll", "crypt32.dll", "bcrypt.dll",
        "sechost.dll", "rpcrt4.dll", "setupapi.dll", "cfgmgr32.dll",
        "wintrust.dll", "msasn1.dll", "shcore.dll", "combase.dll",
        "windows.storage.dll", "profapi.dll", "powrprof.dll",
        "umpdc.dll", "iphlpapi.dll", "dnsapi.dll", "nsi.dll",
        "mswsock.dll", "napinsp.dll", "pnrpnsp.dll", "nlaapi.dll",
        "winrnr.dll", "wshbth.dll", "rasadhlp.dll", "fwpuclnt.dll",
        "wininet.dll", "urlmon.dll", "iertutil.dll", "sspicli.dll",
        "psapi.dll", "dbghelp.dll", "dxgi.dll", "d3d11.dll",
        "xinput1_4.dll", "xinput9_1_0.dll", "dsound.dll",
        "xaudio2_9.dll", "avrt.dll", "devobj.dll", "clbcatq.dll",
        "imagehlp.dll", "wlanapi.dll", "mfplat.dll",
        "cryptnet.dll", "cabinet.dll", "mssign32.dll", "wshext.dll",
        "gpapi.dll", "fltlib.dll", "dhcpcsvc.dll", "dhcpcsvc6.dll"
    };
    for (const char* name : defaultSystem) {
        systemModules.push_back(name);
    }

    // 執行階段 / 可轉散發 DLL：常見於 app-local 部署（遊戲目錄），
    // 因此僅要求「名稱 + 受信任目錄」，可用 AddTrustedModuleHash 釘選強化
    const char* defaultRuntime[] = {
        "msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll",
        "vcruntime140.dll", "vcruntime140_1.dll", "ucrtbase.dll",
        "concrt140.dll", "vccorlib140.dll", "d3dcompiler_47.dll"
    };
    for (const char* name : defaultRuntime) {
        trustedModules.push_back(name);
    }

    // 預設受信任目錄：System32、SysWOW64、exe 所在目錄
    wchar_t buf[MAX_PATH];
    if (::GetSystemDirectoryW(buf, MAX_PATH)) {
        systemDirs.push_back(WideToUtf8(NormalizeDirW(buf)));
    }
    if (::GetWindowsDirectoryW(buf, MAX_PATH)) {
        systemDirs.push_back(WideToUtf8(NormalizeDirW(
            std::wstring(buf) + L"\\SysWOW64")));
    }
    for (const auto& d : systemDirs) {
        trustedDirs.push_back(d);
    }
    if (::GetModuleFileNameW(nullptr, buf, MAX_PATH)) {
        std::wstring exeDir = NormalizeDirW(buf);
        size_t sep = exeDir.find_last_of(L'\\');
        if (sep != std::wstring::npos) {
            trustedDirs.push_back(WideToUtf8(exeDir.substr(0, sep)));
        }
    }
#elif defined(__linux__)
    // Linux：系統庫目錄
    systemDirs = { "/lib", "/lib64", "/usr/lib", "/usr/lib64", "/usr/local/lib" };
    trustedDirs = systemDirs;
#endif
    return true;
}

void SecurityManager::Shutdown() {
    StopMonitoring();
#ifdef _WIN32
    DisableImageLoadNotify();
#endif
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
    trustedModules.push_back(ToLower(moduleName));
}

void SecurityManager::AddTrustedModuleHash(const std::string& moduleName,
                                           const std::string& sha256Hex) {
    std::lock_guard<std::mutex> lock(trustedMutex);
    std::string name = ToLower(moduleName);
    moduleHashes[name] = ToLower(sha256Hex);
    if (std::find(trustedModules.begin(), trustedModules.end(), name) == trustedModules.end()) {
        trustedModules.push_back(name);
    }
#ifdef _WIN32
    // 釘選改變後快取的驗證結果已失效
    std::lock_guard<std::mutex> vlock(verifyMutex);
    hashVerifyCache.clear();
#endif
}

void SecurityManager::AddTrustedDirectory(const std::string& dirPath) {
    std::string dir = ToLower(dirPath);
#ifdef _WIN32
    std::replace(dir.begin(), dir.end(), '/', '\\');
    while (!dir.empty() && dir.back() == '\\') dir.pop_back();
#else
    while (!dir.empty() && dir.back() == '/') dir.pop_back();
#endif
    if (dir.empty()) return;
    std::lock_guard<std::mutex> lock(trustedMutex);
    trustedDirs.push_back(dir);
}

// 目錄比對：dir 等於 base 或位於 base 之下（Windows 用 '\\'，Unix 用 '/'）
static bool DirMatch(const std::string& dir, const std::string& base) {
    if (dir == base) return true;
    if (dir.size() <= base.size() || dir.compare(0, base.size(), base) != 0) return false;
    char sep = dir[base.size()];
#ifdef _WIN32
    return sep == '\\' || sep == '/';
#else
    return sep == '/';
#endif
}

bool SecurityManager::IsTrustedDir(const std::string& dirLower) const {
    std::lock_guard<std::mutex> lock(trustedMutex);
    for (const auto& d : trustedDirs) {
        if (DirMatch(dirLower, d)) return true;
    }
    return false;
}

bool SecurityManager::IsSystemDir(const std::string& dirLower) const {
    std::lock_guard<std::mutex> lock(trustedMutex);
    for (const auto& d : systemDirs) {
        if (DirMatch(dirLower, d)) return true;
    }
    return false;
}

#ifdef _WIN32
std::wstring SecurityManager::NormalizeDirW(std::wstring dir) {
    dir = ToLowerW(dir);
    std::replace(dir.begin(), dir.end(), L'/', L'\\');
    while (!dir.empty() && dir.back() == L'\\') dir.pop_back();
    return dir;
}

// Authenticode 簽章驗證（WinVerifyTrust）。結果按路徑快取：
// 第一次驗證後寫入快取，後續掃描不再重複呼叫（WinVerifyTrust 本身會載入
// wintrust/crypt32 等模組，耗時數 ms ~ 數十 ms）
bool SecurityManager::VerifyModuleSignature(const std::wstring& fullPath) {
    std::wstring key = ToLowerW(fullPath);
    {
        std::lock_guard<std::mutex> lock(verifyMutex);
        auto it = sigVerifyCache.find(key);
        if (it != sigVerifyCache.end()) return it->second;
    }

    WINTRUST_FILE_INFO fileInfo{};
    fileInfo.cbStruct = sizeof(fileInfo);
    fileInfo.pcwszFilePath = fullPath.c_str();

    WINTRUST_DATA data{};
    data.cbStruct = sizeof(data);
    data.dwUIChoice = WTD_UI_NONE;                       // 不跳 UI
    data.fdwRevocationChecks = WTD_REVOKE_NONE;          // 離線環境不檢查撤銷
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &fileInfo;
    data.dwStateAction = WTD_STATEACTION_IGNORE;
    data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;     // 不上網抓 CRL/AIA

    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    LONG status = ::WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);

    bool ok = (status == ERROR_SUCCESS);
    std::lock_guard<std::mutex> lock(verifyMutex);
    sigVerifyCache[key] = ok;
    return ok;
}

// 雙層判定：
//  - 系統模組名（kernel32、dsound...）-> 必須在系統目錄 + Authenticode 簽章
//  - 一般信任名（AddTrustedModule）   -> 必須在受信任目錄（+ 雜湊釘選若設定）
//  - 其他                              -> 不受信任
bool SecurityManager::IsTrustedModuleWin(const std::wstring& fullPath, std::string& reason) {
    std::wstring lower = NormalizeDirW(fullPath);
    size_t sep = lower.find_last_of(L'\\');
    std::wstring wname = (sep != std::wstring::npos) ? lower.substr(sep + 1) : lower;
    std::string name = WideToUtf8(wname);
    std::string dir  = WideToUtf8(sep != std::wstring::npos ? lower.substr(0, sep) : L"");

    bool isSystemName;
    bool isUserName;
    std::string pinnedHash;
    {
        std::lock_guard<std::mutex> lock(trustedMutex);
        isSystemName = std::find(systemModules.begin(), systemModules.end(), name)
                       != systemModules.end();
        isUserName = std::find(trustedModules.begin(), trustedModules.end(), name)
                     != trustedModules.end();
        auto it = moduleHashes.find(name);
        if (it != moduleHashes.end()) pinnedHash = it->second;
    }

    if (!isSystemName && !isUserName) {
        reason = "name not in whitelist";
        return false;
    }

    if (isSystemName) {
        if (!IsSystemDir(dir)) {
            reason = "system module outside system dir";
            return false;
        }
        if (!VerifyModuleSignature(fullPath)) {
            reason = "invalid/missing Authenticode signature";
            return false;
        }
        return true;
    }

    if (!IsTrustedDir(dir)) {
        reason = "module outside trusted dirs";
        return false;
    }

    // 雜湊釘選（有設定才檢查，結果快取）
    if (!pinnedHash.empty()) {
        {
            std::lock_guard<std::mutex> lock(verifyMutex);
            auto it = hashVerifyCache.find(lower);
            if (it != hashVerifyCache.end()) {
                if (it->second) return true;
                reason = "SHA-256 hash mismatch";
                return false;
            }
        }
        // 寬字串路徑經 filesystem::path 轉換,可處理非 ASCII 檔名（MinGW 相容）
        std::ifstream file(std::filesystem::path(fullPath), std::ios::binary);
        std::string actual = file.is_open() ? HashStreamToHex(file) : "";
        bool ok = !actual.empty() && actual == pinnedHash;
        {
            std::lock_guard<std::mutex> lock(verifyMutex);
            hashVerifyCache[lower] = ok;
        }
        if (!ok) {
            reason = "SHA-256 hash mismatch";
            return false;
        }
    }
    return true;
}
#endif

std::vector<ModuleScanResult> SecurityManager::ScanModules() {
    std::vector<ModuleScanResult> results;

#ifdef _WIN32
    HANDLE hProcess = ::GetCurrentProcess();
    HMODULE modules[1024];
    DWORD needed = 0;

    // 先快照模組路徑（不在持有鎖時呼叫 WinVerifyTrust，避免其載入 DLL 造成遞迴）
    std::vector<std::wstring> paths;
    if (::EnumProcessModules(hProcess, modules, sizeof(modules), &needed)) {
        DWORD count = needed / sizeof(HMODULE);
        for (DWORD i = 0; i < count; i++) {
            wchar_t modulePath[MAX_PATH];
            if (::GetModuleFileNameExW(hProcess, modules[i], modulePath, MAX_PATH)) {
                paths.emplace_back(modulePath);
            }
        }
    }

    for (const auto& p : paths) {
        ModuleScanResult r;
        r.path = WideToUtf8(p);
        size_t sep = r.path.find_last_of("\\/");
        r.name = ToLower(sep != std::string::npos ? r.path.substr(sep + 1) : r.path);
        r.trusted = IsTrustedModuleWin(p, r.reason);
        results.push_back(std::move(r));
    }
#elif defined(__linux__)
    // Linux: 檢查 /proc/self/maps 中的共享庫（名稱 + 受信任目錄 + 雜湊釘選）
    std::ifstream maps("/proc/self/maps");
    std::string line;
    std::vector<std::string> seen;
    while (std::getline(maps, line)) {
        size_t slash = line.find('/');
        size_t pos = line.find_last_of('/');
        if (pos != std::string::npos && line.find(".so") != std::string::npos) {
            std::string fullPath = line.substr(slash);
            fullPath = fullPath.substr(0, fullPath.find(' '));
            std::string lib = line.substr(pos + 1);
            lib = lib.substr(0, lib.find('\n'));
            if (std::find(seen.begin(), seen.end(), lib) != seen.end()) continue;
            seen.push_back(lib);

            ModuleScanResult r;
            r.path = fullPath;
            r.name = ToLower(lib);
            std::string dirLower = ToLower(fullPath.substr(0, fullPath.find_last_of('/')));

            bool nameTrusted;
            std::string pinnedHash;
            {
                std::lock_guard<std::mutex> lock(trustedMutex);
                nameTrusted = std::find(trustedModules.begin(), trustedModules.end(), r.name)
                              != trustedModules.end();
                auto it = moduleHashes.find(r.name);
                if (it != moduleHashes.end()) pinnedHash = it->second;
            }
            if (!nameTrusted) {
                r.reason = "name not in whitelist";
            } else if (!IsTrustedDir(dirLower)) {
                r.reason = "module outside trusted dirs";
            } else if (!pinnedHash.empty() &&
                       ComputeFileSHA256(fullPath) != pinnedHash) {
                r.reason = "SHA-256 hash mismatch";
            } else {
                r.trusted = true;
            }
            results.push_back(std::move(r));
        }
    }
#endif
    return results;
}

std::vector<std::string> SecurityManager::FindUntrustedModules() {
    std::vector<std::string> untrusted;
    for (const auto& r : ScanModules()) {
        if (!r.trusted) untrusted.push_back(r.name);
    }
    return untrusted;
}

bool SecurityManager::CheckLoadedModules() {
    std::string names;
    for (const auto& r : ScanModules()) {
        if (!r.trusted) {
            if (!names.empty()) names += ", ";
            names += r.name + " (" + r.reason + ")";
        }
    }
    if (!names.empty()) {
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
    GuardRecord rec;
    rec.id = nextGuardId++;
    rec.ptr = data;
    rec.size = size;
    HmacSha256(macKey, sizeof(macKey), data, size, rec.mac);
    guardedRegions.push_back(rec);
    return rec.id;
}

bool SecurityManager::VerifyRegion(uint32_t guardId, const void* data, size_t size) {
    std::lock_guard<std::mutex> lock(guardMutex);
    for (const auto& rec : guardedRegions) {
        if (rec.id == guardId) {
            uint8_t current[32];
            HmacSha256(macKey, sizeof(macKey), data, size, current);
            if (memcmp(current, rec.mac, 32) != 0) {
                ReportViolation(ViolationType::MemoryTampered,
                    "Guarded region " + std::to_string(guardId) + " modified");
                return false;
            }
            return true;
        }
    }
    return false; // guardId 不存在
}

bool SecurityManager::VerifyAllGuards() {
    // 複製清單避免在持鎖狀態下做 MAC 運算（可能較慢）與觸發回呼（會取 callbackMutex）
    std::vector<GuardRecord> snapshot;
    {
        std::lock_guard<std::mutex> lock(guardMutex);
        snapshot = guardedRegions;
    }
    bool ok = true;
    for (const auto& rec : snapshot) {
        uint8_t current[32];
        HmacSha256(macKey, sizeof(macKey), rec.ptr, rec.size, current);
        if (memcmp(current, rec.mac, 32) != 0) {
            ReportViolation(ViolationType::MemoryTampered,
                "Guarded region " + std::to_string(rec.id) + " modified (auto-verify)");
            ok = false;
        }
    }
    return ok;
}

void SecurityManager::UnguardRegion(uint32_t guardId) {
    std::lock_guard<std::mutex> lock(guardMutex);
    guardedRegions.erase(
        std::remove_if(guardedRegions.begin(), guardedRegions.end(),
            [guardId](const auto& p) { return p.id == guardId; }),
        guardedRegions.end());
}

// ---- 自身程式碼完整性（.text 區段 HMAC 快照）----

bool SecurityManager::GuardOwnCode() {
#ifdef _WIN32
    HMODULE self = nullptr;
    if (!::GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&Potato::Security::ComputeSHA256),
            &self) || !self) {
        return false;
    }
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(self);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        reinterpret_cast<const uint8_t*>(self) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    // 找第一個可執行區段（通常是 .text）
    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
        if ((sec->Characteristics & IMAGE_SCN_MEM_EXECUTE) &&
            !(sec->Characteristics & IMAGE_SCN_MEM_DISCARDABLE)) {
            ownCodeBase = reinterpret_cast<const uint8_t*>(self) + sec->VirtualAddress;
            ownCodeSize = sec->Misc.VirtualSize;
            HmacSha256(macKey, sizeof(macKey), ownCodeBase, ownCodeSize, ownCodeMac);
            ownCodeArmed = true;
            return true;
        }
    }
#endif
    return false;
}

bool SecurityManager::VerifyOwnCode() {
#ifdef _WIN32
    if (!ownCodeArmed) {
        return GuardOwnCode(); // 第一次呼叫先建立快照
    }
    uint8_t current[32];
    HmacSha256(macKey, sizeof(macKey), ownCodeBase, ownCodeSize, current);
    if (memcmp(current, ownCodeMac, 32) != 0) {
        ReportViolation(ViolationType::CodeTampered,
            ".text section modified (inline patch/hook detected)");
        return false;
    }
#endif
    return true;
}

// ---- 強化偵測 ----

bool SecurityManager::CheckDebuggerExtended() {
#ifdef _WIN32
    using PFN_NtQIP = NTSTATUS(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    auto NtQIP = reinterpret_cast<PFN_NtQIP>(
        ::GetProcAddress(::GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess"));
    if (!NtQIP) return false;

    HANDLE proc = ::GetCurrentProcess();

    // ProcessDebugPort (7)：非 0 表示有除錯埠
    ULONG_PTR debugPort = 0;
    if (NtQIP(proc, 7, &debugPort, sizeof(debugPort), nullptr) >= 0 && debugPort) {
        ReportViolation(ViolationType::RemoteDebuggerDetected,
            "ProcessDebugPort = " + std::to_string(debugPort));
        return true;
    }

    // ProcessDebugObjectHandle (30)：有 handle 表示被除錯
    HANDLE debugObj = nullptr;
    if (NtQIP(proc, 30, &debugObj, sizeof(debugObj), nullptr) >= 0 && debugObj) {
        ReportViolation(ViolationType::RemoteDebuggerDetected,
            "ProcessDebugObjectHandle present");
        return true;
    }

    // ProcessDebugFlags (31)：0 表示被除錯（NoDebugInherit 被清除）
    ULONG dbgFlags = 1;
    if (NtQIP(proc, 31, &dbgFlags, sizeof(dbgFlags), nullptr) >= 0 && dbgFlags == 0) {
        ReportViolation(ViolationType::DebuggerDetected,
            "ProcessDebugFlags == 0");
        return true;
    }

    // PEB: BeingDebugged（offset +2）與 NtGlobalFlag（x64: +0xBC, x86: +0x68）
    PROCESS_BASIC_INFORMATION pbi{};
    if (NtQIP(proc, 0, &pbi, sizeof(pbi), nullptr) >= 0 && pbi.PebBaseAddress) {
        const uint8_t* peb = reinterpret_cast<const uint8_t*>(pbi.PebBaseAddress);
        if (peb[2]) { // BeingDebugged
            ReportViolation(ViolationType::DebuggerDetected,
                "PEB.BeingDebugged set");
            return true;
        }
#ifdef _WIN64
        ULONG ntGlobal = *reinterpret_cast<const ULONG*>(peb + 0xBC);
#else
        ULONG ntGlobal = *reinterpret_cast<const ULONG*>(peb + 0x68);
#endif
        // FLG_HEAP_ENABLE_TAIL_CHECK|FREE_CHECK|VALIDATE_PARAMETERS
        if (ntGlobal & 0x70) {
            ReportViolation(ViolationType::DebuggerDetected,
                "PEB.NtGlobalFlag heap-debug bits set");
            return true;
        }
    }
#endif
    return false;
}

bool SecurityManager::CheckHardwareBreakpoints() {
#ifdef _WIN32
    DWORD pid = ::GetCurrentProcessId();
    DWORD selfTid = ::GetCurrentThreadId();
    bool found = false;

    HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        THREADENTRY32 te{};
        te.dwSize = sizeof(te);
        for (BOOL ok = ::Thread32First(snap, &te); ok; ok = ::Thread32Next(snap, &te)) {
            if (te.th32OwnerProcessID != pid || te.th32ThreadID == selfTid) continue;
            HANDLE h = ::OpenThread(
                THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
                FALSE, te.th32ThreadID);
            if (!h) continue;
            ::SuspendThread(h);
            CONTEXT ctx{};
            ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (::GetThreadContext(h, &ctx)) {
                if ((ctx.Dr7 & 0xFF) && (ctx.Dr0 || ctx.Dr1 || ctx.Dr2 || ctx.Dr3)) {
                    ReportViolation(ViolationType::HardwareBreakpoint,
                        "tid " + std::to_string(te.th32ThreadID) + " has DR breakpoint set");
                    found = true;
                }
            }
            ::ResumeThread(h);
            ::CloseHandle(h);
        }
        ::CloseHandle(snap);
    }

    // 自身執行緒：GetThreadContext 對執行中的執行緒無效，用 RtlCaptureContext
    CONTEXT selfCtx{};
    selfCtx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    ::RtlCaptureContext(&selfCtx);
    if ((selfCtx.Dr7 & 0xFF) && (selfCtx.Dr0 || selfCtx.Dr1 || selfCtx.Dr2 || selfCtx.Dr3)) {
        ReportViolation(ViolationType::HardwareBreakpoint,
            "current thread has DR breakpoint set");
        found = true;
    }
    return found;
#else
    return false;
#endif
}

#ifdef _WIN32
namespace {
// 收集所有已載入模組的位址範圍
struct ModuleRange { uintptr_t base, end; HMODULE mod; };
std::vector<ModuleRange> CollectModuleRanges() {
    std::vector<ModuleRange> ranges;
    HMODULE modules[1024];
    DWORD needed = 0;
    if (::EnumProcessModules(::GetCurrentProcess(), modules, sizeof(modules), &needed)) {
        DWORD count = needed / sizeof(HMODULE);
        for (DWORD i = 0; i < count; i++) {
            MODULEINFO mi{};
            if (::GetModuleInformation(::GetCurrentProcess(), modules[i], &mi, sizeof(mi))) {
                ranges.push_back({ reinterpret_cast<uintptr_t>(mi.lpBaseOfDll),
                                   reinterpret_cast<uintptr_t>(mi.lpBaseOfDll) + mi.SizeOfImage,
                                   modules[i] });
            }
        }
    }
    return ranges;
}

bool AddrInAnyModule(uintptr_t addr, const std::vector<ModuleRange>& ranges) {
    for (const auto& r : ranges) {
        if (addr >= r.base && addr < r.end) return true;
    }
    return false;
}
} // namespace
#endif

bool SecurityManager::CheckInjectedThreads() {
#ifdef _WIN32
    using PFN_NtQIT = NTSTATUS(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    auto NtQIT = reinterpret_cast<PFN_NtQIT>(
        ::GetProcAddress(::GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread"));
    if (!NtQIT) return false;

    auto ranges = CollectModuleRanges();

    DWORD pid = ::GetCurrentProcessId();
    DWORD selfTid = ::GetCurrentThreadId();
    bool found = false;

    HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    for (BOOL ok = ::Thread32First(snap, &te); ok; ok = ::Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == selfTid) continue;
        HANDLE h = ::OpenThread(THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
        if (!h) continue;
        uintptr_t startAddr = 0;
        // ThreadQuerySetWin32StartAddress = 9
        if (NtQIT(h, 9, &startAddr, sizeof(startAddr), nullptr) >= 0 && startAddr) {
            if (!AddrInAnyModule(startAddr, ranges)) {
                ReportViolation(ViolationType::InjectedThread,
                    "tid " + std::to_string(te.th32ThreadID) +
                    " start address outside all modules (shellcode?)");
                found = true;
            }
        }
        ::CloseHandle(h);
    }
    ::CloseHandle(snap);
    return found;
#else
    return false;
#endif
}

// ---- 隱藏模組 / 可疑記憶體 / IAT hook / 外部 handle / heap ----

bool SecurityManager::CheckHiddenModules() {
#ifdef _WIN32
    // EnumProcessModules 讀 PEB 載入器鏈表 —— 被 unlinked 或手動映射的
    // DLL 不會出現。改掃位址空間的 MEM_IMAGE 區域交叉比對。
    auto ranges = CollectModuleRanges();
    std::vector<uintptr_t> imageBases; // 已知模組 base（= AllocationBase）
    for (const auto& r : ranges) imageBases.push_back(r.base);

    bool found = false;
    uintptr_t addr = 0;
    MEMORY_BASIC_INFORMATION mbi{};
    while (::VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi))) {
        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_IMAGE) {
            uintptr_t abase = reinterpret_cast<uintptr_t>(mbi.AllocationBase);
            if (std::find(imageBases.begin(), imageBases.end(), abase) == imageBases.end()) {
                imageBases.push_back(abase); // 每個映像只回報一次
                ReportViolation(ViolationType::HiddenModule,
                    "MEM_IMAGE region at 0x" +
                    std::to_string(abase) +
                    " not in module list (manual mapped?)");
                found = true;
            }
        }
        uintptr_t next = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (next <= addr) break;
        addr = next;
    }
    return found;
#else
    return false;
#endif
}

bool SecurityManager::CheckExecutablePrivateMemory() {
#ifdef _WIN32
    bool found = false;
    uintptr_t addr = 0;
    MEMORY_BASIC_INFORMATION mbi{};
    uintptr_t lastReportedBase = 0;
    while (::VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi))) {
        bool exec = (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                                    PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE && exec) {
            uintptr_t abase = reinterpret_cast<uintptr_t>(mbi.AllocationBase);
            if (abase != lastReportedBase) {
                lastReportedBase = abase;
                ReportViolation(ViolationType::SuspiciousMemory,
                    "executable MEM_PRIVATE region at 0x" +
                    std::to_string(abase) + " (shellcode staging?)");
                found = true;
            }
        }
        uintptr_t next = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (next <= addr) break;
        addr = next;
    }
    return found;
#else
    return false;
#endif
}

bool SecurityManager::CheckIATHooks() {
#ifdef _WIN32
    HMODULE self = ::GetModuleHandleW(nullptr); // 主 exe
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(self);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        reinterpret_cast<const uint8_t*>(self) + dos->e_lfanew);

    auto& impDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!impDir.VirtualAddress) return false;

    auto ranges = CollectModuleRanges();
    auto* desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(
        reinterpret_cast<const uint8_t*>(self) + impDir.VirtualAddress);

    bool found = false;
    for (; desc->Name; desc++) {
        const char* dllName = reinterpret_cast<const char*>(
            reinterpret_cast<const uint8_t*>(self) + desc->Name);
        auto* thunk = reinterpret_cast<const IMAGE_THUNK_DATA*>(
            reinterpret_cast<const uint8_t*>(self) + desc->FirstThunk);
        for (; thunk->u1.Function; thunk++) {
            uintptr_t addr = static_cast<uintptr_t>(thunk->u1.Function);
            // forwarded import 會指向其他系統 DLL —— 仍屬模組內，不算 hook
            if (!AddrInAnyModule(addr, ranges)) {
                ReportViolation(ViolationType::HookDetected,
                    std::string("IAT entry in ") + dllName +
                    " points outside all modules: 0x" + std::to_string(addr));
                found = true;
            }
        }
    }
    return found;
#else
    return false;
#endif
}

bool SecurityManager::CheckExternalHandles(std::string* diag) {
#ifdef _WIN32
    auto fail = [&](const char* why) { if (diag) *diag = why; return false; };
    using PFN_NtQSI = NTSTATUS(NTAPI*)(ULONG, PVOID, ULONG, PULONG);
    auto NtQSI = reinterpret_cast<PFN_NtQSI>(
        ::GetProcAddress(::GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation"));
    if (!NtQSI) return fail("NtQuerySystemInformation unavailable");

    // SystemExtendedHandleInformation = 64（PID 為完整 ULONG_PTR；
    // 舊版 class 16 的 UniqueProcessId 只有 USHORT，PID > 65535 會截斷）
    struct HandleEntryEx {
        PVOID object; ULONG_PTR pid; ULONG_PTR handle; ULONG access;
        USHORT creatorBackTrace; USHORT objType; ULONG attrs; ULONG reserved;
    };
    struct HandleInfoEx { ULONG_PTR count; ULONG_PTR reserved; HandleEntryEx entries[1]; };

    ULONG_PTR ownPid = ::GetCurrentProcessId();

    // 先開一個自己的 handle（必須在快照「之前」建立，快照才看得到）
    HANDLE selfHandle = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                      FALSE, static_cast<DWORD>(ownPid));
    if (!selfHandle) return fail("cannot open self handle");

    std::vector<uint8_t> buf(1 << 20);
    ULONG retLen = 0;
    NTSTATUS st;
    while ((st = NtQSI(64, buf.data(), static_cast<ULONG>(buf.size()), &retLen))
           == static_cast<NTSTATUS>(0xC0000004)) { // STATUS_INFO_LENGTH_MISMATCH
        buf.resize(buf.size() * 2);
        if (buf.size() > (64u << 20)) {
            ::CloseHandle(selfHandle);
            return fail("handle table too large");
        }
    }
    if (st < 0) {
        ::CloseHandle(selfHandle);
        return fail("NtQuerySystemInformation failed");
    }

    // 用 selfHandle 反查 Process 物件的 type index
    auto* info = reinterpret_cast<HandleInfoEx*>(buf.data());
    int procType = -1;
    for (ULONG_PTR i = 0; i < info->count; i++) {
        if (info->entries[i].pid == ownPid &&
            info->entries[i].handle == reinterpret_cast<ULONG_PTR>(selfHandle)) {
            procType = info->entries[i].objType;
            break;
        }
    }
    ::CloseHandle(selfHandle);
    if (procType < 0) return fail("process type index not found");

    // 找「別的行程持有 process 型別 handle」→ 嘗試確認目標是不是我們
    bool found = false;
    int candidates = 0, ownerOpenFail = 0, dupFail = 0;
    for (ULONG_PTR i = 0; i < info->count; i++) {
        const auto& e = info->entries[i];
        if (e.objType != procType || e.pid == ownPid ||
            e.pid == 0 || e.pid == 4) continue;
        candidates++;

        HANDLE owner = ::OpenProcess(
            PROCESS_DUP_HANDLE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
            static_cast<DWORD>(e.pid));
        if (!owner) { ownerOpenFail++; continue; } // 無權開對方 → 無法確認目標，略過
        HANDLE dup = nullptr;
        if (::DuplicateHandle(owner,
                reinterpret_cast<HANDLE>(e.handle),
                ::GetCurrentProcess(), &dup, 0, FALSE,
                DUPLICATE_SAME_ACCESS) && dup) {
            DWORD targetPid = ::GetProcessId(dup);
            if (targetPid == ownPid) {
                // 同一持有者只回報一次，避免監控週期洗版
                {
                    std::lock_guard<std::mutex> lock(trustedMutex);
                    if (extHandleSeen.count(e.pid)) { ::CloseHandle(dup); ::CloseHandle(owner); continue; }
                    extHandleSeen.insert(e.pid);
                }
                wchar_t name[MAX_PATH] = {};
                DWORD sz = MAX_PATH;
                ::QueryFullProcessImageNameW(owner, 0, name, &sz);
                ReportViolation(ViolationType::ExternalHandle,
                    "pid " + std::to_string(e.pid) + " (" + WideToUtf8(name) +
                    ") holds a handle to this process");
                found = true;
            }
            ::CloseHandle(dup);
        } else {
            dupFail++;
        }
        ::CloseHandle(owner);
    }
    if (diag) {
        *diag = "candidates=" + std::to_string(candidates) +
                " ownerOpenFail=" + std::to_string(ownerOpenFail) +
                " dupFail=" + std::to_string(dupFail) +
                " found=" + std::to_string(found);
    }
    return found;
#else
    (void)diag;
    return false;
#endif
}

bool SecurityManager::CheckHeapIntegrity() {
#ifdef _WIN32
    if (!::HeapValidate(::GetProcessHeap(), 0, nullptr)) {
        ReportViolation(ViolationType::HeapCorruption, "process heap corrupt");
        return true;
    }
#endif
    return false;
}

bool SecurityManager::IsMonitorAlive(uint32_t maxAgeMs) const {
    if (!monitoring.load()) return false;
    int64_t hb = monitorHeartbeatMs.load();
    if (hb == 0) return false;
    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    return (now - hb) <= static_cast<int64_t>(maxAgeMs);
}

// ---- 即時載入通知（LdrRegisterDllNotification）----
#ifdef _WIN32
namespace {
// ntdll 半文件化 API：DLL 載入/卸載時即時回呼（Vista+）
struct LdrDllNotificationData {
    unsigned long flags;
    const UNICODE_STRING* fullDllName;
    const UNICODE_STRING* baseDllName;
    void* dllBase;
    unsigned long sizeOfImage;
};
constexpr unsigned long kLdrLoaded = 1;
using LdrNotifyFn = void(CALLBACK*)(unsigned long, const LdrDllNotificationData*, void*);
using PFN_LdrRegister = NTSTATUS(NTAPI*)(unsigned long, LdrNotifyFn, void*, void**);
using PFN_LdrUnregister = NTSTATUS(NTAPI*)(void*);
} // namespace

void CALLBACK SecurityManager::LdrNotifyThunk(unsigned long reason,
                                              const void* data, void* ctx) {
    if (reason != kLdrLoaded || !data || !ctx) return;
    auto* nd = static_cast<const LdrDllNotificationData*>(data);
    auto* self = static_cast<SecurityManager*>(ctx);
    if (!nd->fullDllName || !nd->fullDllName->Buffer) return;

    // 注意：此回呼在 loader lock 下執行 —— 只能做無鎖複製 + SetEvent，
    // 絕不能取得 mutex 或呼叫會載入 DLL 的 API（會死鎖）
    long i = self->notifyWrite.fetch_add(1, std::memory_order_relaxed)
             % kNotifyRingSize;
    NotifySlot& slot = self->notifyRing[i];
    if (slot.ready.load(std::memory_order_acquire)) {
        self->notifyDropped.fetch_add(1, std::memory_order_relaxed);
    }
    size_t n = nd->fullDllName->Length / sizeof(wchar_t);
    if (n > 259) n = 259;
    memcpy(slot.path, nd->fullDllName->Buffer, n * sizeof(wchar_t));
    slot.path[n] = 0;
    slot.ready.store(1, std::memory_order_release);
    if (self->notifyWakeEvent) {
        ::SetEvent(self->notifyWakeEvent);
    }
}

void SecurityManager::NotifyWorkerLoop() {
    while (notifyRun.load(std::memory_order_acquire)) {
        ::WaitForSingleObject(notifyWakeEvent, 500); // timeout 作為保險
        for (;;) {
            long w = notifyWrite.load(std::memory_order_acquire);
            if (notifyRead >= w) break;
            NotifySlot& slot = notifyRing[notifyRead % kNotifyRingSize];
            if (!slot.ready.exchange(0, std::memory_order_acq_rel)) break;
            OnImageLoad(slot.path);
            notifyRead++;
        }
    }
}

void SecurityManager::OnImageLoad(const wchar_t* path) {
    std::string reason;
    if (!IsTrustedModuleWin(path, reason)) {
        std::wstring wname = NormalizeDirW(path);
        size_t sep = wname.find_last_of(L'\\');
        std::string name = WideToUtf8(
            sep != std::wstring::npos ? wname.substr(sep + 1) : wname);
        ReportViolation(ViolationType::UntrustedModule,
            "Real-time: " + name + " (" + reason + ")");
    }
}

bool SecurityManager::EnableImageLoadNotify() {
    if (ldrCookie) return true;
    HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return false;
    auto reg = reinterpret_cast<PFN_LdrRegister>(
        ::GetProcAddress(ntdll, "LdrRegisterDllNotification"));
    if (!reg) return false;

    notifyWakeEvent = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!notifyWakeEvent) return false;
    notifyRun.store(true, std::memory_order_release);
    notifyWorker = std::thread(&SecurityManager::NotifyWorkerLoop, this);

    NTSTATUS st = reg(0,
        reinterpret_cast<LdrNotifyFn>(&SecurityManager::LdrNotifyThunk),
        this, &ldrCookie);
    if (st < 0) {
        notifyRun.store(false);
        if (notifyWorker.joinable()) notifyWorker.join();
        ::CloseHandle(notifyWakeEvent);
        notifyWakeEvent = nullptr;
        ldrCookie = nullptr;
        return false;
    }
    return true;
}

void SecurityManager::DisableImageLoadNotify() {
    if (ldrCookie) {
        auto unreg = reinterpret_cast<PFN_LdrUnregister>(
            ::GetProcAddress(::GetModuleHandleW(L"ntdll.dll"),
                             "LdrUnregisterDllNotification"));
        if (unreg) unreg(ldrCookie);
        ldrCookie = nullptr;
    }
    notifyRun.store(false, std::memory_order_release);
    if (notifyWakeEvent) ::SetEvent(notifyWakeEvent);
    if (notifyWorker.joinable()) notifyWorker.join();
    if (notifyWakeEvent) {
        ::CloseHandle(notifyWakeEvent);
        notifyWakeEvent = nullptr;
    }
}
#else
bool SecurityManager::EnableImageLoadNotify() { return false; }
void SecurityManager::DisableImageLoadNotify() {}
#endif


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
        monitorHeartbeatMs.store(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        CheckDebugger();
        CheckDebuggerExtended();
        CheckTimingAnomaly();
        CheckLoadedModules();
        CheckHiddenModules();
        CheckExecutablePrivateMemory();
        CheckIATHooks();
        CheckExternalHandles();
        CheckInjectedThreads();
        CheckHardwareBreakpoints();
        CheckHeapIntegrity();
        VerifyAllGuards();
        if (ownCodeArmed) VerifyOwnCode();

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
    violated |= CheckDebuggerExtended();
    violated |= CheckTimingAnomaly();
    violated |= CheckLoadedModules();
    violated |= CheckHiddenModules();
    violated |= CheckExecutablePrivateMemory();
    violated |= CheckIATHooks();
    violated |= CheckExternalHandles();
    violated |= CheckInjectedThreads();
    violated |= CheckHardwareBreakpoints();
    violated |= CheckHeapIntegrity();
    violated |= !VerifyAllGuards();
    if (ownCodeArmed) violated |= !VerifyOwnCode();
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
        case ViolationType::HardwareBreakpoint:    return "HardwareBreakpoint";
        case ViolationType::InjectedThread:        return "InjectedThread";
        case ViolationType::CodeTampered:          return "CodeTampered";
        case ViolationType::HiddenModule:          return "HiddenModule";
        case ViolationType::SuspiciousMemory:      return "SuspiciousMemory";
        case ViolationType::HookDetected:          return "HookDetected";
        case ViolationType::ExternalHandle:        return "ExternalHandle";
        case ViolationType::HeapCorruption:        return "HeapCorruption";
    }
    return "Unknown";
}

} // namespace Security
} // namespace Potato
