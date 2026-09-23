#include "FileSystem/FileWatcher.h"

#include <filesystem>

namespace Potato {

namespace fs = std::filesystem;

namespace {

uint64_t ToTicks(fs::file_time_type t) {
    return static_cast<uint64_t>(t.time_since_epoch().count());
}

} // anonymous namespace

std::string FileWatcher::Normalize(const std::string& path) {
    std::error_code ec;
    fs::path p = fs::absolute(path, ec);
    if (ec) p = fs::path(path);
    return p.lexically_normal().generic_string();
}

bool FileWatcher::StatFile(const std::string& path,
                           uint64_t& mtime, uint64_t& size) {
    std::error_code ec;
    const auto ft = fs::last_write_time(path, ec);
    if (ec) return false;
    const auto sz = fs::file_size(path, ec);
    if (ec) return false;
    mtime = ToTicks(ft);
    size = sz;
    return true;
}

std::unordered_map<std::string, std::pair<uint64_t, uint64_t>>
FileWatcher::ListDir(const std::string& dirPath) {
    std::unordered_map<std::string, std::pair<uint64_t, uint64_t>> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dirPath, ec)) {
        if (!e.is_regular_file(ec)) continue;
        uint64_t mt = 0, sz = 0;
        if (StatFile(e.path().string(), mt, sz)) {
            out[e.path().filename().generic_string()] = {mt, sz};
        }
    }
    return out;
}

bool FileWatcher::Watch(const std::string& path) {
    const std::string key = Normalize(path);
    if (tracked_.count(key)) return false;
    Tracked t;
    t.isDir = false;
    StatFile(path, t.mtime, t.size);  // 不存在也可追蹤（之後 Created）
    tracked_[key] = std::move(t);
    return true;
}

bool FileWatcher::WatchDirectory(const std::string& dirPath) {
    const std::string key = Normalize(dirPath);
    if (tracked_.count(key)) return false;
    std::error_code ec;
    if (!fs::is_directory(dirPath, ec)) return false;
    Tracked t;
    t.isDir = true;
    t.children = ListDir(dirPath);
    tracked_[key] = std::move(t);
    return true;
}

void FileWatcher::Unwatch(const std::string& path) {
    tracked_.erase(Normalize(path));
}

void FileWatcher::UnwatchAll() { tracked_.clear(); }

bool FileWatcher::IsWatching(const std::string& path) const {
    return tracked_.count(Normalize(path)) != 0;
}

std::vector<FileChange> FileWatcher::Poll() {
    std::vector<FileChange> changes;
    for (auto& [key, t] : tracked_) {
        if (!t.isDir) {
            uint64_t mt = 0, sz = 0;
            const bool exists = StatFile(key, mt, sz);
            const bool wasExisting = (t.mtime != 0);
            if (exists && !wasExisting) {
                changes.push_back({key, FileChangeType::Created});
            } else if (!exists && wasExisting) {
                changes.push_back({key, FileChangeType::Deleted});
            } else if (exists && (mt != t.mtime || sz != t.size)) {
                changes.push_back({key, FileChangeType::Modified});
            }
            t.mtime = exists ? mt : 0;
            t.size = exists ? sz : 0;
        } else {
            auto cur = ListDir(key);
            for (const auto& [name, ms] : cur) {
                auto it = t.children.find(name);
                const std::string full = key + "/" + name;
                if (it == t.children.end()) {
                    changes.push_back({full, FileChangeType::Created});
                } else if (it->second != ms) {
                    changes.push_back({full, FileChangeType::Modified});
                }
            }
            for (const auto& [name, ms] : t.children) {
                if (!cur.count(name)) {
                    changes.push_back(
                        {key + "/" + name, FileChangeType::Deleted});
                }
            }
            t.children = std::move(cur);
        }
    }
    return changes;
}

} // namespace Potato
