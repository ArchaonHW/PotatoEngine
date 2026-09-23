/**
 * FileWatcherTest - FileSystem/FileWatcher 測試（headless）
 *
 * 覆蓋：
 *   [單檔] Modified/Created/Deleted 事件
 *   [目錄] 子檔新增/修改/刪除偵測（非遞迴）
 *   [管理] Watch 重複拒絕、Unwatch、IsWatching
 *   [基準] Watch 當下狀態不產生事件
 */

#include "FileSystem/FileWatcher.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

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

void WriteFile(const std::string& path, const std::string& content) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << content;
}

bool HasChange(const std::vector<FileChange>& changes,
               const std::string& suffix, FileChangeType type) {
    for (const auto& c : changes) {
        if (c.type == type &&
            c.path.size() >= suffix.size() &&
            c.path.compare(c.path.size() - suffix.size(),
                           suffix.size(), suffix) == 0)
            return true;
    }
    return false;
}

// mtime 解析度可能很粗（FAT/某些 FS 到秒級）——寫檔後等一拍再 Poll
void Settle() {
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
}

} // anonymous namespace

int main() {
    printf("=== FileWatcher 測試 ===\n\n");

    const fs::path dir = fs::temp_directory_path() / "potato_fw_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    const std::string fileA = (dir / "a.txt").string();
    const std::string fileB = (dir / "b.txt").string();

    WriteFile(fileA, "v1");
    Settle();

    // ---- [單檔] ----
    printf("-- 單檔監視 --\n");
    {
        FileWatcher w;
        Expect(w.Watch(fileA), "Watch 存在檔");
        Expect(!w.Watch(fileA), "重複 Watch 拒絕");
        Expect(w.Poll().empty(), "基準線無事件");

        WriteFile(fileA, "v2-longer-content");
        Settle();
        auto ch = w.Poll();
        Expect(HasChange(ch, "a.txt", FileChangeType::Modified),
               "修改偵測");
        Expect(w.Poll().empty(), "無新變更不重複報");

        fs::remove(fileA);
        Settle();
        Expect(HasChange(w.Poll(), "a.txt", FileChangeType::Deleted),
               "刪除偵測");

        WriteFile(fileA, "reborn");
        Settle();
        Expect(HasChange(w.Poll(), "a.txt", FileChangeType::Created),
               "重建偵測(Created)");

        w.Unwatch(fileA);
        Expect(!w.IsWatching(fileA), "Unwatch 生效");
        WriteFile(fileA, "ignored");
        Settle();
        Expect(w.Poll().empty(), "Unwatch 後不報");
    }

    // ---- [目錄] ----
    printf("-- 目錄監視 --\n");
    {
        FileWatcher w;
        Expect(w.WatchDirectory(dir.string()), "WatchDirectory");
        Expect(w.Poll().empty(), "目錄基準線無事件");

        WriteFile(fileB, "new file");
        Settle();
        Expect(HasChange(w.Poll(), "b.txt", FileChangeType::Created),
               "目錄新檔偵測");

        WriteFile(fileB, "changed content here");
        Settle();
        Expect(HasChange(w.Poll(), "b.txt", FileChangeType::Modified),
               "目錄檔修改偵測");

        fs::remove(fileB);
        Settle();
        Expect(HasChange(w.Poll(), "b.txt", FileChangeType::Deleted),
               "目錄檔刪除偵測");

        Expect(!w.WatchDirectory((dir / "nonexistent").string()),
               "不存在目錄 WatchDirectory 拒絕");
    }

    std::error_code ec2;
    fs::remove_all(dir, ec2);

    printf("\n=== 結果: %d PASS / %d FAIL ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
