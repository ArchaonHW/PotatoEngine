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
    MemoryTampered          // 受保護記憶體區域被竄改
};

// 違規報告
struct SecurityReport {
    ViolationType type = ViolationType::None;
    std::string details;
};

// 違規事件回呼
using ViolationCallback = std::function<void(const SecurityReport&)>;

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

    // ---- 反除錯 ----
    // 偵測是否有除錯器附加（IsDebuggerPresent / CheckRemoteDebuggerPresent / TracerPid）
    bool CheckDebugger();
    // 計時異常偵測：執行一段已知耗時極短的程式碼，若耗時異常表示可能被單步執行
    bool CheckTimingAnomaly();

    // ---- DLL 注入偵測 ----
    // 將模組名稱加入信任清單（例如 "kernel32.dll"）
    void AddTrustedModule(const std::string& moduleName);
    // 掃描目前已載入的模組，回傳不在信任清單中的模組名稱
    std::vector<std::string> FindUntrustedModules();
    // 檢查是否有未授權模組；發現時觸發回呼
    bool CheckLoadedModules();

    // ---- 完整性校驗 ----
    // 驗證檔案 SHA-256 是否符合預期值
    bool VerifyFileIntegrity(const std::string& filePath, const std::string& expectedSha256Hex);
    // 為記憶體區域建立完整性快照，回傳快照 ID（之後用 VerifyRegion 驗證）
    uint32_t GuardRegion(const void* data, size_t size);
    // 驗證受保護區域是否仍完整
    bool VerifyRegion(uint32_t guardId, const void* data, size_t size);
    void UnguardRegion(uint32_t guardId);

    // ---- 定期監控 ----
    // 啟動背景監控執行緒，每 intervalMs 毫秒執行一次全部檢查
    void StartMonitoring(uint32_t intervalMs = 5000);
    void StopMonitoring();
    bool IsMonitoring() const { return monitoring.load(); }

    // 立即執行全部檢查，回傳是否有違規
    bool RunAllChecks();

private:
    SecurityManager() = default;
    ~SecurityManager();
    SecurityManager(const SecurityManager&) = delete;
    SecurityManager& operator=(const SecurityManager&) = delete;

    void ReportViolation(ViolationType type, const std::string& details);
    void MonitorLoop(uint32_t intervalMs);

    std::atomic<bool> initialized{false};
    std::atomic<bool> monitoring{false};
    std::thread monitorThread;
    ViolationCallback violationCallback;
    std::mutex callbackMutex;

    // 信任模組清單（小寫模組名）
    std::vector<std::string> trustedModules;
    mutable std::mutex trustedMutex;

    // 受保護記憶體區域：guardId -> CRC32
    std::vector<std::pair<uint32_t, uint32_t>> guardedRegions;
    std::mutex guardMutex;
    uint32_t nextGuardId = 1;
};

// 取得違規類型的可讀字串（用於 log）
const char* ViolationTypeToString(ViolationType type);

} // namespace Security
} // namespace Potato
