#pragma once

/**
 * DownloadGuard — 落盤即掃目錄守衛（FileWatcher + ContentScanner）
 *
 * 用途：監視下載/模組/玩家上傳目錄，新檔案落盤或檔案被修改時
 * 自動以 ContentScanner 掃描，並可選擇把 Malicious 檔案直接隔離。
 *
 * 使用模式（遊戲主迴圈或背景執行緒定期呼叫）：
 *   DownloadGuard guard;
 *   guard.SetAutoQuarantine(true, "quarantine/");
 *   guard.Watch("mods/");
 *   // 每幀或每 500ms：
 *   for (const auto& r : guard.Poll()) { ... 上報/記錄 ... }
 *
 * 誠實標註：
 * - 基於 FileWatcher 的 mtime+size 輪詢,非 OS 即時通知——
 *   Poll() 呼叫頻率決定偵測延遲
 * - 分段寫入的大檔案會在 Created 時先掃到不完整內容,
 *   後續 Modified 事件會重掃（SetScanModified 預設開啟即為此）
 */

#include "Security/ContentScanner.h"
#include "FileSystem/FileWatcher.h"

#include <string>
#include <vector>
#include <functional>

namespace Potato {
namespace Security {

class AuditLedger;  // Security/AuditLedger.h——防竄改稽核帳本

class DownloadGuard {
public:
    // 每個被掃描的檔案觸發一次（含 Clean；在隔離動作之前呼叫）
    using ReportCallback = std::function<void(const FileScanResult&)>;

    // 開始監視目錄；目錄不存在或已在監視回 false。
    // recursive=true 時一併監視所有既有子目錄,且 Poll 發現的
    // 新子目錄會自動納入監視（其既有檔案立即掃一輪,不當基準線）。
    bool Watch(const std::string& dirPath, bool recursive = false);
    void Unwatch();
    bool IsWatching() const;
    const std::string& WatchedDir() const { return dir; }

    // Malicious 檔案自動隔離到 quarantineDir（<sha256>.quarantine + 清單）
    // Suspicious 不自動隔離——啟發式有誤報可能,由回呼/遊戲層決定
    void SetAutoQuarantine(bool on, const std::string& quarantineDir = "");
    // Modified 事件是否重掃（預設 true：分段寫入的檔案補掃）
    void SetScanModified(bool on) { scanModified = on; }
    void SetCallback(ReportCallback cb) { callback = std::move(cb); }

    // 設定稽核帳本（nullptr 解除；非擁有,生命週期由呼叫端管理）。
    // 設定後非 Clean 的掃描判定與隔離動作皆以 hash 鏈入帳——
    // 攻擊者刪改本地掃描紀錄會讓鏈斷裂（HeadHash 應外部錨定）。
    void SetAuditLedger(AuditLedger* ledger) { auditLedger = ledger; }

    // 存取內部掃描器——供遊戲層加黑名單/位元組特徵/載入特徵庫
    ContentScanner& Scanner() { return scanner; }
    const ContentScanner& Scanner() const { return scanner; }

    // 掃描自上次 Poll 以來新增/修改的檔案；Deleted 事件忽略。
    // 回傳本輪全部掃描結果（無變更時為空 vector）。
    std::vector<FileScanResult> Poll();

private:
    // 單檔處理鏈：掃描 → 回呼 → 非 Clean 入帳 → Malicious 隔離
    void ScanOne(const std::string& path, std::vector<FileScanResult>& out);
    // recursive 模式：列舉子目錄,新發現的納入監視並即掃既有檔
    void DiscoverSubdirs(std::vector<FileScanResult>& out);

    FileWatcher watcher;
    ContentScanner scanner;
    ReportCallback callback;
    AuditLedger* auditLedger = nullptr;
    std::string dir;
    std::string quarantineDir;
    bool autoQuarantine = false;
    bool scanModified = true;
    bool recursive = false;
};

} // namespace Security
} // namespace Potato
