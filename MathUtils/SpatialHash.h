#pragma once

/**
 * SpatialHash - 泛型 2D 空間雜湊網格（header-only）
 *
 * 用途：RTS 單位鄰近查詢、碰撞粗篩、編隊槽位檢索。
 * 把平面切成 cellSize 方格,物件以點座標入格;
 * 查詢掃覆蓋範圍內的格子再做精確距離/AABB 判定。
 *
 * 泛型 T 為任意值語義型別（單位 id、Entity、指標皆可）。
 * 誠實標註：點物件容器——有體積物件請自行以外接點+擴大半徑處理;
 * 非執行緒安全。
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Potato {

template<typename T>
class SpatialHash {
public:
    using Id = uint32_t;
    static constexpr Id kInvalidId = 0;

    explicit SpatialHash(float cellSize = 8.0f)
        : cellSize_(cellSize > 0.0f ? cellSize : 8.0f) {}

    // 插入點物件，回傳 handle(0 為無效)
    Id Insert(const T& item, float x, float y) {
        const Id id = nextId_++;
        entries_.push_back({item, x, y, true});
        slots_.push_back(id);
        CellOf(x, y).push_back(id);
        idToIndex_[id] = entries_.size() - 1;
        ++aliveCount_;
        return id;
    }

    // 更新物件位置（跨格時搬移）
    bool Move(Id id, float x, float y) {
        auto it = idToIndex_.find(id);
        if (it == idToIndex_.end()) return false;
        Entry& e = entries_[it->second];
        const int64_t oldKey = CellKey(e.x, e.y);
        const int64_t newKey = CellKey(x, y);
        if (oldKey != newKey) {
            auto& cell = cells_[oldKey];
            cell.erase(std::find(cell.begin(), cell.end(), id));
            if (cell.empty()) cells_.erase(oldKey);
            cells_[newKey].push_back(id);
        }
        e.x = x; e.y = y;
        return true;
    }

    bool Remove(Id id) {
        auto it = idToIndex_.find(id);
        if (it == idToIndex_.end()) return false;
        Entry& e = entries_[it->second];
        auto& cell = cells_[CellKey(e.x, e.y)];
        cell.erase(std::find(cell.begin(), cell.end(), id));
        if (cell.empty()) cells_.erase(CellKey(e.x, e.y));
        idToIndex_.erase(it);
        // entries_ 不 swap-erase（idToIndex_ 會全壞）——
        // 標記移除,定期由 Compact() 或 Clear() 回收
        e.alive = false;
        --aliveCount_;
        return true;
    }

    // 半徑查詢：精確圓心距離判定
    template<typename Fn>
    void QueryRadius(float cx, float cy, float radius, Fn&& fn) const {
        const float r2 = radius * radius;
        ForCellsInRect(cx - radius, cy - radius,
                       cx + radius, cy + radius, [&](Id id) {
            const Entry& e = entries_[IndexOf(id)];
            if (!e.alive) return;
            const float dx = e.x - cx, dy = e.y - cy;
            if (dx * dx + dy * dy <= r2) fn(e.item);
        });
    }

    // AABB 查詢
    template<typename Fn>
    void QueryRect(float minX, float minY, float maxX, float maxY,
                   Fn&& fn) const {
        ForCellsInRect(minX, minY, maxX, maxY, [&](Id id) {
            const Entry& e = entries_[IndexOf(id)];
            if (!e.alive) return;
            if (e.x >= minX && e.x <= maxX && e.y >= minY && e.y <= maxY)
                fn(e.item);
        });
    }

    size_t Size() const { return aliveCount_; }
    size_t CellCount() const { return cells_.size(); }

    // 取得物件位置(不存在回 false)
    bool GetPosition(Id id, float& x, float& y) const {
        auto it = idToIndex_.find(id);
        if (it == idToIndex_.end()) return false;
        x = entries_[it->second].x;
        y = entries_[it->second].y;
        return true;
    }

    // 壓縮：移除 dead entries 並重建索引（大量 Remove 後呼叫）
    void Compact() {
        if (aliveCount_ == entries_.size()) return;
        std::vector<Entry> kept;
        std::vector<Id> keptIds;
        kept.reserve(aliveCount_);
        keptIds.reserve(aliveCount_);
        idToIndex_.clear();
        cells_.clear();
        for (size_t i = 0; i < entries_.size(); ++i) {
            if (!entries_[i].alive) continue;
            const Id id = slots_[i];
            idToIndex_[id] = kept.size();
            cells_[CellKey(entries_[i].x, entries_[i].y)].push_back(id);
            kept.push_back(entries_[i]);
            keptIds.push_back(id);
        }
        entries_ = std::move(kept);
        slots_ = std::move(keptIds);
    }

    void Clear() {
        entries_.clear();
        slots_.clear();
        cells_.clear();
        idToIndex_.clear();
        aliveCount_ = 0;
    }

private:
    struct Entry {
        T item;
        float x, y;
        bool alive = true;
    };

    int64_t CellKey(float x, float y) const {
        const int32_t cx = static_cast<int32_t>(std::floor(x / cellSize_));
        const int32_t cy = static_cast<int32_t>(std::floor(y / cellSize_));
        return (static_cast<int64_t>(cx) << 32) |
               static_cast<uint32_t>(cy);
    }

    std::vector<Id>& CellOf(float x, float y) {
        return cells_[CellKey(x, y)];
    }

    size_t IndexOf(Id id) const { return idToIndex_.at(id); }

    template<typename Fn>
    void ForCellsInRect(float minX, float minY, float maxX, float maxY,
                        Fn&& fn) const {
        const int32_t cx0 = static_cast<int32_t>(std::floor(minX / cellSize_));
        const int32_t cy0 = static_cast<int32_t>(std::floor(minY / cellSize_));
        const int32_t cx1 = static_cast<int32_t>(std::floor(maxX / cellSize_));
        const int32_t cy1 = static_cast<int32_t>(std::floor(maxY / cellSize_));
        for (int32_t cy = cy0; cy <= cy1; ++cy) {
            for (int32_t cx = cx0; cx <= cx1; ++cx) {
                const int64_t key =
                    (static_cast<int64_t>(cx) << 32) |
                    static_cast<uint32_t>(cy);
                auto it = cells_.find(key);
                if (it == cells_.end()) continue;
                for (Id id : it->second) fn(id);
            }
        }
    }

    float cellSize_;
    std::vector<Entry> entries_;
    std::vector<Id> slots_;                        // entries_ 同位的 id
    std::unordered_map<int64_t, std::vector<Id>> cells_;
    std::unordered_map<Id, size_t> idToIndex_;
    Id nextId_ = 1;
    size_t aliveCount_ = 0;
};

} // namespace Potato
