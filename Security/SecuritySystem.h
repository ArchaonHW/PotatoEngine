#pragma once

/**
 * Potato Engine Security System
 * 引擎安全模組：反除錯、完整性校驗、DLL 注入偵測、記憶體防竄改
 *
 * 設計原則：
 * - 自包含：僅依賴標準庫與作業系統 API，不依賴引擎其他模組
 * - 所有檢查皆為「偵測與嚇阻」層級，無法完全阻止有決心的攻擊者
 * - 違規事件透過回呼通知，由遊戲層決定處置方式（記錄、降級、關閉）
 */

#include <string>
#include <vector>
#include <utility>
#include <functional>
#include <cstdint>
#include <cstddef>
#include <atomic>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace Potato {
namespace Security {

// 安全違規類型
enum class ViolationType {
    None,
    DebuggerDetected,       // 偵測到本機除錯器
    RemoteDebuggerDetected, // 偵測到遠端除錯器
    TracerDetected,         // 偵測到 ptrace/追蹤器 (Linux)
    TimingAnomaly,          // 計時異常（可能被單步執行或 patch）
    UntrustedModule,        // 載入了未授權的 DLL/共享庫（注入）
    IntegrityMismatch,      // 檔案完整性校驗失敗（被竄改）
    MemoryTampered,         // 受保護記憶體區域被竄改
    HardwareBreakpoint,     // 偵測到 DR0-DR7 硬體中斷點
    InjectedThread,         // 偵測到起始位址不在任何模組內的執行緒
    CodeTampered,           // 自身 .text 程式碼區段被修改（inline patch/hook）
    HiddenModule,           // 記憶體中有 MEM_IMAGE 區域不在模組清單（手動映射/PEB unlinked）
    SuspiciousMemory,       // 存在可執行的 MEM_PRIVATE 區域（shellcode staging）
    HookDetected,           // IAT entry 指向模組外（import hook）
    ExternalHandle,         // 外部行程持有本行程 handle（Cheat Engine 類工具特徵）
    HeapCorruption,         // Heap 完整性檢查失敗
    ApiHook,                // 關鍵 API 前導碼被 patch（inline hook 攔截系統呼叫）
    ExternalTool            // 偵測到已知作弊/除錯工具行程
};

// 違規報告
struct SecurityReport {
    ViolationType type = ViolationType::None;
    std::string details;
};

// 違規事件回呼
using ViolationCallback = std::function<void(const SecurityReport&)>;

class AuditLedger;  // Security/AuditLedger.h——防竄改稽核帳本

// 模組掃描結果（含不受信任原因，供診斷用）
struct ModuleScanResult {
    std::string name;    // 模組檔名（小寫）
    std::string path;    // 模組完整路徑
    bool trusted = false;
    std::string reason;  // trusted == false 時的原因
};

// ============================================================================
// 密碼學與校驗工具
// ============================================================================

// 計算資料的 SHA-256，回傳 64 字元小寫 hex 字串
std::string ComputeSHA256(const void* data, size_t size);

// 計算檔案的 SHA-256；失敗回傳空字串
std::string ComputeFileSHA256(const std::string& filePath);

// 計算記憶體區域的 CRC32（用於快速記憶體完整性快照）
uint32_t ComputeCRC32(const void* data, size_t size);

// 安全地清除記憶體（保證不被編譯器最佳化移除）
// 用於清除密鑰、token 等敏感資料
void SecureZeroMemory(void* ptr, size_t size);

// 計算 HMAC-SHA256（RFC 2104）訊息鑑別碼，回傳 64 字元小寫 hex。
// 供「攻擊者不知金鑰即無法偽造」的完整性場景（存檔簽章、資料鑑別）
std::string ComputeHMACSHA256(const void* key, size_t keyLen,
                              const void* data, size_t dataLen);

// 產生密碼學安全隨機位元組（Windows BCryptGenRandom / POSIX /dev/urandom；
// 兩者皆失效時退回多來源混合 xorshift——僅為最後備援，非密碼學級）
std::vector<uint8_t> GenerateRandomBytes(size_t len);

// ---- 資料簽章（存檔/設定檔竄改防護）----
// blob 格式：[原始資料 || HMAC-SHA256(key, data) 32B 尾]。
// 金鑰保管責任在呼叫端（例：首啟 GenerateRandomBytes(32) 存於使用者目錄）。
// 驗證使用常數時間比對；blob 短於 32B 或 MAC 不符一律回 false。
std::vector<uint8_t> SignData(const void* key, size_t keyLen,
                              const void* data, size_t dataLen);
// out 非空時驗證通過才寫回原始資料；只驗不取可傳 nullptr
bool VerifySignedData(const void* key, size_t keyLen,
                      const std::vector<uint8_t>& blob,
                      std::vector<uint8_t>* out = nullptr);
// 檔案版：讀 srcPath → 簽章 blob 寫入 destPath
bool SignFile(const std::string& srcPath, const std::string& destPath,
              const void* key, size_t keyLen);
// 驗證簽章檔並取出原始內容（out 可為 nullptr 只驗證）
bool VerifySignedFile(const std::string& filePath,
                      const void* key, size_t keyLen,
                      std::vector<uint8_t>* out = nullptr);

// ============================================================================
// SecurityManager - 安全檢查管理器
// ============================================================================
class SecurityManager {
public:
    static SecurityManager& GetInstance();

    bool Initialize();
    void Shutdown();
    bool IsInitialized() const { return initialized.load(); }

    // 設定違規事件回呼（例如：記錄 log、通知伺服器、終止程式）
    void SetViolationCallback(ViolationCallback callback);

    // 設定稽核帳本（nullptr 解除）。設定後每次違規事件除回呼外,
    // 亦以 hash 鏈記錄入帳（category="violation",eventType=違規類型名,
    // payload 存 details 的 SHA-256,原文不落帳）——日誌層級的
    // tamper-evident 揭露：作弊者刪改本地違規紀錄會讓鏈斷裂。
    // 呼叫端持有 ledger 生命週期,並應定期取出 HeadHash 外部錨定。
    void SetAuditLedger(AuditLedger* ledger);

    // ---- 反除錯 ----
    // 偵測是否有除錯器附加（IsDebuggerPresent / CheckRemoteDebuggerPresent / TracerPid）
    bool CheckDebugger();
    // 延伸反除錯（Windows）：ProcessDebugPort / DebugObject / DebugFlags / PEB 旗標
    bool CheckDebuggerExtended();
    // 計時異常偵測：執行一段已知耗時極短的程式碼，若耗時異常表示可能被單步執行
    bool CheckTimingAnomaly();
    // 硬體中斷點偵測：掃描全部執行緒的 DR0-DR3/DR7 除錯暫存器
    bool CheckHardwareBreakpoints();
    // 注入執行緒偵測：執行緒起始位址不在任何已載入模組內（CreateRemoteThread/shellcode）
    bool CheckInjectedThreads();
    // 隱藏模組偵測：掃描 MEM_IMAGE 區域，找出不在模組清單中的映像
    // （手動映射 / 從 PEB 載入器鏈表摘除的 DLL 會被 EnumProcessModules 遺漏，這裡能補抓）
    bool CheckHiddenModules();
    // 可疑記憶體：可執行的 MEM_PRIVATE 區域（PAGE_EXECUTE_* 且非模組）= shellcode staging
    bool CheckExecutablePrivateMemory();
    // IAT hook 偵測：import 表 entry 的解析位址不在任何已載入模組內
    bool CheckIATHooks();
    // 外部 handle 偵測：枚舉系統 handle，回報持有本行程 handle 的其他行程
    // diag 為非 null 時輸出診斷訊息（失敗原因 / 掃描統計）
    bool CheckExternalHandles(std::string* diag = nullptr);
    // Heap 完整性：HeapValidate 檢查主堆
    bool CheckHeapIntegrity();
    // 關鍵 API inline hook 偵測：x64 的 ntdll syscall stub 前導碼恆為
    // 4C 8B D1 B8（mov r10,rcx; mov eax,imm）。被 E9/FF25 等覆寫 = 被 hook，
    // 這是外掛攔截 NtProtectVirtualMemory/NtWriteVirtualMemory 的慣用手法。
    bool CheckCriticalApiHooks();
    // 已知作弊/除錯工具行程掃描：cheatengine、x64dbg、ollydbg、windbg、
    // processhacker、systeminformer、xenos、wemod、ida、ghidra 等
    // （子字串比對映像檔名；完整特徵表見 SecuritySystem.cpp kToolNames）
    bool CheckKnownToolProcesses();
    // 執行緒 RIP 稽核：短暫暫停各執行緒檢查指令指標是否落在已載入模組內。
    // 補 CheckInjectedThreads 的盲點——後者只看起始位址，無法抓到
    // 「合法起點建立、之後跳入 shellcode」的執行緒。
    bool CheckThreadContexts();
    // 外部 handle 持有者白名單（exe 檔名，如 "conhost.exe"）。
    // 位於系統目錄的二進位自動視為合法持有者，不需手動加入。
    void AddTrustedHandleHolder(const std::string& imageName);

    // ---- DLL 注入偵測 ----
    // 將模組名稱加入信任清單（例如 "myplugin.dll"）。
    // 信任條件：檔名相符 + 模組位於受信任目錄（exe 目錄 / 系統目錄 / AddTrustedDirectory 指定的目錄）。
    void AddTrustedModule(const std::string& moduleName);
    // 對信任模組釘選 SHA-256（建議用於隨遊戲散佈、未簽章的自家 DLL）。
    // 釘選後模組除名稱與路徑外，檔案內容雜湊也必須相符，可阻擋冒名替換。
    void AddTrustedModuleHash(const std::string& moduleName, const std::string& sha256Hex);
    // 加入額外的受信任模組目錄（例如外掛目錄）。系統目錄與 exe 目錄預設已信任。
    void AddTrustedDirectory(const std::string& dirPath);
    // 掃描目前已載入的模組，回傳每個模組的信任判定與原因
    std::vector<ModuleScanResult> ScanModules();
    // 掃描目前已載入的模組，回傳不在信任清單中的模組名稱
    std::vector<std::string> FindUntrustedModules();
    // 檢查是否有未授權模組；發現時觸發回呼
    bool CheckLoadedModules();

    // ---- 完整性校驗 ----
    // 驗證檔案 SHA-256 是否符合預期值
    bool VerifyFileIntegrity(const std::string& filePath, const std::string& expectedSha256Hex);
    // 啟動期自身完整性：依 manifest（GenerateIntegrityManifest 產生的
    // SHA-256 白名單）逐檔驗證 baseDir 下的引擎/遊戲檔案。
    // hmacKey 非空時要求 manifest 帶相符簽章（見 IntegrityManifest.h）。
    // 每筆失敗（竄改/消失/不可讀/清單異常）觸發一次 IntegrityMismatch 回呼。
    // 回傳是否全部通過；manifest 無法讀取/解析/簽章不符也回 false。
    bool CheckIntegrityManifest(const std::string& manifestPath,
                                const std::string& baseDir,
                                const void* hmacKey = nullptr,
                                size_t hmacKeyLen = 0);
    // 為記憶體區域建立完整性快照（keyed HMAC-SHA256），回傳快照 ID。
    // 受保護位址會被記錄，監控執行緒會定期自動驗證（VerifyAllGuards）。
    // 注意：data 指標在 UnguardRegion 前必須保持有效。
    uint32_t GuardRegion(const void* data, size_t size);
    // 驗證受保護區域是否仍完整
    bool VerifyRegion(uint32_t guardId, const void* data, size_t size);
    // 重新驗證所有受保護區域（使用 GuardRegion 記錄的位址）；有任一被竄改回傳 false
    bool VerifyAllGuards();
    void UnguardRegion(uint32_t guardId);

    // ---- 自身程式碼完整性 ----
    // 對本模組的 .text 區段建立 HMAC 快照（偵測 inline patch / hook）
    bool GuardOwnCode();
    // 驗證 .text 是否仍與快照相符
    bool VerifyOwnCode();

    // ---- 即時載入通知（消除輪詢空窗）----
    // 註冊 LdrRegisterDllNotification：DLL 載入當下即由背景執行緒驗證，
    // 不再依賴掃描間隔。回傳 false 表示系統不支援。
    bool EnableImageLoadNotify();
    void DisableImageLoadNotify();

    // ---- 定期監控 ----
    // 啟動背景監控執行緒，每 intervalMs 毫秒執行一次全部檢查
    void StartMonitoring(uint32_t intervalMs = 5000);
    void StopMonitoring();
    bool IsMonitoring() const { return monitoring.load(); }
    // 監控心跳：監控執行緒在 maxAgeMs 內有更新即視為存活。
    // 若監控執行緒被凍結/殺死，此函數回傳 false —— 供遊戲主執行緒定期檢查。
    bool IsMonitorAlive(uint32_t maxAgeMs = 15000) const;

    // 立即執行全部檢查，回傳是否有違規
    bool RunAllChecks();

private:
    SecurityManager() = default;
    ~SecurityManager();
    SecurityManager(const SecurityManager&) = delete;
    SecurityManager& operator=(const SecurityManager&) = delete;

    void ReportViolation(ViolationType type, const std::string& details);
    void MonitorLoop(uint32_t intervalMs);

#ifdef _WIN32
    // 判定單一已載入模組是否受信任；reason 輸出不受信任的原因
    bool IsTrustedModuleWin(const std::wstring& fullPath, std::string& reason);
    // Authenticode 簽章驗證（WinVerifyTrust，結果按路徑快取）
    bool VerifyModuleSignature(const std::wstring& fullPath);
    static std::wstring NormalizeDirW(std::wstring dir);
#endif
    // 檢查目錄是否在受信任目錄清單內（含子目錄前綴比對）
    bool IsTrustedDir(const std::string& dirLower) const;
    bool IsSystemDir(const std::string& dirLower) const;

    std::atomic<bool> initialized{false};
    std::atomic<bool> monitoring{false};
    std::thread monitorThread;
    // 監控心跳（steady_clock 毫秒時間戳，MonitorLoop 每週期更新）
    std::atomic<int64_t> monitorHeartbeatMs{0};
    ViolationCallback violationCallback;
    AuditLedger* auditLedger = nullptr;   // 可選：違規事件同時入帳（非擁有）
    std::mutex callbackMutex;

    // 系統模組清單（小寫檔名）：必須位於系統目錄且通過 Authenticode 簽章驗證
    std::vector<std::string> systemModules;
    // 一般信任模組清單（小寫檔名）：必須位於受信任目錄
    std::vector<std::string> trustedModules;
    // 模組 SHA-256 釘選：小寫檔名 -> 預期雜湊
    std::unordered_map<std::string, std::string> moduleHashes;
    // 已回報過的外部 handle 持有者（去重）
    std::unordered_set<uintptr_t> extHandleSeen;
    // 外部 handle 持有者白名單（小寫 exe 檔名）
    std::unordered_set<std::string> trustedHandleHolders;
    // 已回報過的外部工具行程（去重）
    std::unordered_set<uintptr_t> extToolSeen;
    mutable std::mutex trustedMutex;

    // 受信任模組目錄（正規化：小寫、反斜線、無尾分隔符）
    std::vector<std::string> trustedDirs;
    // 系統目錄（system32 / syswow64 或 /lib /usr/lib 等）
    std::vector<std::string> systemDirs;

#ifdef _WIN32
    // 簽章 / 雜湊驗證結果快取（小寫完整路徑 -> 通過與否），避免每次掃描重複驗證
    std::unordered_map<std::wstring, bool> sigVerifyCache;
    std::unordered_map<std::wstring, bool> hashVerifyCache;
    std::mutex verifyMutex;
#endif

    // ---- 記憶體防護（keyed HMAC-SHA256，攻擊者無法重算校驗值）----
    struct GuardRecord {
        uint32_t id;
        const void* ptr;         // 受保護位址（供 VerifyAllGuards 定期驗證）
        size_t size;
        uint8_t mac[32];
    };
    std::vector<GuardRecord> guardedRegions;
    std::mutex guardMutex;
    uint32_t nextGuardId = 1;
    uint8_t macKey[32] = {};      // 隨機金鑰，Initialize 時產生

    // ---- 自身程式碼完整性 ----
    bool ownCodeArmed = false;
    const void* ownCodeBase = nullptr;
    size_t ownCodeSize = 0;
    uint8_t ownCodeMac[32] = {};

#ifdef _WIN32
    // ---- 即時載入通知 ----
    // loader-lock 安全的無鎖 ring：callback 只複製路徑 + SetEvent
    static constexpr int kNotifyRingSize = 64;
    struct NotifySlot { std::atomic<long> ready{0}; wchar_t path[260]; };
    NotifySlot notifyRing[kNotifyRingSize];
    std::atomic<long> notifyWrite{0};
    long notifyRead = 0;
    void* notifyWakeEvent = nullptr;   // HANDLE
    void* ldrCookie = nullptr;          // LdrRegisterDllNotification cookie
    std::thread notifyWorker;
    std::atomic<bool> notifyRun{false};
    std::atomic<uint64_t> notifyDropped{0};

    static void __stdcall LdrNotifyThunk(unsigned long reason, const void* data, void* ctx);
    void NotifyWorkerLoop();
    void OnImageLoad(const wchar_t* path);
#endif
};

// 取得違規類型的可讀字串（用於 log）
const char* ViolationTypeToString(ViolationType type);

} // namespace Security
} // namespace Potato
