#pragma once

/**
 * FileWatcher - 檔案變更監視（polling 版）
 *
 * 用途：資源 hot-reload、設定檔即時重載、開發期工具鏈。
 *
 * 誠實標註：mtime+size 輪詢實作，非 OS 通知（ReadDirectoryChangesW/
 * inotify)。理由：跨平台一致、無背景執行緒、行為可測——
 * 代價是 Poll() 間隔決定偵測延遲（預設建議 100~500ms)。
 * 目錄監視為非遞迴；遞迴需求由呼叫端自行 Watch 子目錄。
 */

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Potato {

enum class FileChangeType : uint8_t {
    Created,
    Modified,
    Deleted,
};

struct FileChange {
    std::string path;
    FileChangeType type;
};

class FileWatcher {
public:
    // 監視單一檔案；已在追蹤回 false
    bool Watch(const std::string& path);
    // 監視目錄（非遞迴）：追蹤內含檔案的新增/修改/刪除
    bool WatchDirectory(const std::string& dirPath);
    void Unwatch(const std::string& path);
    void UnwatchAll();

    // 輪詢一次，回傳上次 Poll 以來的變更（含首輪後的新增檔）。
    // Watch 當下的狀態為基準線,不產生事件。
    std::vector<FileChange> Poll();

    bool IsWatching(const std::string& path) const;
    size_t WatchedCount() const { return tracked_.size(); }

private:
    struct Tracked {
        bool isDir = false;
        // 檔案:mtime+size;目錄:子檔名 → mtime+size
        uint64_t mtime = 0;
        uint64_t size = 0;
        std::unordered_map<std::string, std::pair<uint64_t, uint64_t>>
            children;
    };

    static bool StatFile(const std::string& path,
                         uint64_t& mtime, uint64_t& size);
    static std::unordered_map<std::string,
                              std::pair<uint64_t, uint64_t>>
        ListDir(const std::string& dirPath);

    // 正規化路徑（絕對化 + 統一分隔符）作為追蹤 key
    static std::string Normalize(const std::string& path);

    std::unordered_map<std::string, Tracked> tracked_;
};

} // namespace Potato
