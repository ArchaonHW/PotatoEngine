/**
 * DownloadGuard 實作——落盤即掃目錄守衛
 */

#include "Security/DownloadGuard.h"
#include "Security/AuditLedger.h"

#include <filesystem>

namespace Potato {
namespace Security {

namespace {

// findings 的 ruleId 串成摘要（入帳 payload 只留其 SHA-256）
std::string FindingIds(const FileScanResult& r) {
    std::string out;
    for (const auto& f : r.findings) {
        if (!out.empty()) out += ',';
        out += f.ruleId;
    }
    return out;
}

} // anonymous namespace

bool DownloadGuard::Watch(const std::string& dirPath, bool recursive_) {
    if (!watcher.WatchDirectory(dirPath)) return false;
    dir = dirPath;
    recursive = recursive_;
    if (recursive) {
        // 既有子目錄立即納入監視（內含檔案為基準線,不即掃——
        // Watch 當下已存在的內容視為呼叫端已接受的現狀）
        std::error_code ec;
        namespace fs = std::filesystem;
        for (fs::recursive_directory_iterator
                 it(dir, fs::directory_options::skip_permission_denied, ec),
                 end;
             !ec && it != end; it.increment(ec)) {
            std::error_code eec;
            if (it->is_directory(eec))
                watcher.WatchDirectory(it->path().string());
        }
    }
    return true;
}

void DownloadGuard::Unwatch() {
    watcher.UnwatchAll();
    dir.clear();
}

bool DownloadGuard::IsWatching() const {
    return !dir.empty() && watcher.IsWatching(dir);
}

void DownloadGuard::SetAutoQuarantine(bool on,
                                      const std::string& quarantineDir_) {
    autoQuarantine = on;
    quarantineDir = quarantineDir_;
}

void DownloadGuard::ScanOne(const std::string& path,
                            std::vector<FileScanResult>& out) {
    FileScanResult r = scanner.ScanFile(path);
    if (callback) callback(r);

    // 非 Clean 入帳：payload 為可讀摘要,帳本只留其 SHA-256
    if (auditLedger && r.verdict != ScanVerdict::Clean) {
        auditLedger->Append("scan", ScanVerdictToString(r.verdict),
                            r.path + "|" + r.sha256 + "|" + FindingIds(r),
                            "download_guard:" + dir);
    }

    if (autoQuarantine && r.verdict == ScanVerdict::Malicious &&
        !quarantineDir.empty()) {
        // 隔離失敗不回頭改判定——檔案仍在原處,由回呼端決定後續
        const bool ok = scanner.QuarantineFile(path, quarantineDir,
                                               &r, nullptr);
        if (auditLedger) {
            auditLedger->Append("quarantine", ok ? "ok" : "failed",
                                r.path + "|" + r.sha256,
                                "download_guard:" + dir);
        }
    }
    out.push_back(std::move(r));
}

void DownloadGuard::DiscoverSubdirs(std::vector<FileScanResult>& out) {
    namespace fs = std::filesystem;
    std::error_code ec;
    for (fs::recursive_directory_iterator
             it(dir, fs::directory_options::skip_permission_denied, ec),
             end;
         !ec && it != end; it.increment(ec)) {
        std::error_code eec;
        if (!it->is_directory(eec)) continue;
        const std::string sub = it->path().string();
        if (watcher.IsWatching(sub)) continue;

        if (!watcher.WatchDirectory(sub)) continue;
        // 新發現的子目錄：其既有檔案不該成為基準線,立即掃一輪
        for (fs::directory_iterator fit(sub, eec), fend;
             !eec && fit != fend; fit.increment(eec)) {
            std::error_code fec;
            if (fit->is_regular_file(fec))
                ScanOne(fit->path().string(), out);
        }
    }
}

std::vector<FileScanResult> DownloadGuard::Poll() {
    std::vector<FileScanResult> out;
    if (recursive) DiscoverSubdirs(out);
    for (const auto& change : watcher.Poll()) {
        if (change.type == FileChangeType::Deleted) continue;
        if (change.type == FileChangeType::Modified && !scanModified) continue;
        ScanOne(change.path, out);
    }
    return out;
}

} // namespace Security
} // namespace Potato
