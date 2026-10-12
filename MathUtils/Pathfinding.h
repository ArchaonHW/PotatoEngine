#pragma once

/**
 * Pathfinding - 網格尋路（header-only）
 *
 * GridPathfinder：A* 最短路徑，整數成本（orth=10、diag=14)
 * 全確定性——open list tie-break 明確（f → h → node id),
 * 無浮點、無 unordered_map 迭代順序依賴，跨平台逐位一致,
 * 可放心用於 lockstep。
 *
 * FlowField：從目標反向 Dijkstra 成本場，每格存下坡方向——
 * 大軍同目標移動時 O(1) 查向，比每單位各跑 A* 省。
 *
 * 對角移動防切角：斜走要求兩個正交鄰格都可走。
 */

#include <cstdint>
#include <functional>
#include <queue>
#include <vector>

namespace Potato {

// 8 向鄰居（順序固定：先正交後對角，保決定性）
namespace detail {
constexpr int kDirX[8] = {1, -1, 0, 0, 1, 1, -1, -1};
constexpr int kDirY[8] = {0, 0, 1, -1, 1, -1, 1, -1};
constexpr int kCost[8] = {10, 10, 10, 10, 14, 14, 14, 14};
} // namespace detail

class GridPathfinder {
public:
    GridPathfinder(int width, int height)
        : w_(width), h_(height), walk_(width * height, true) {}

    void SetWalkable(int x, int y, bool v) {
        if (Inside(x, y)) walk_[y * w_ + x] = v;
    }
    bool IsWalkable(int x, int y) const {
        return Inside(x, y) && walk_[y * w_ + x];
    }
    int Width() const { return w_; }
    int Height() const { return h_; }

    // A* 找路；不可達/端點不可走回空 vector。
    // 回傳含起點與終點的格座標序列。
    std::vector<std::pair<int, int>> FindPath(int sx, int sy,
                                              int gx, int gy) const {
        std::vector<std::pair<int, int>> path;
        if (!IsWalkable(sx, sy) || !IsWalkable(gx, gy)) return path;
        if (sx == gx && sy == gy) { path.emplace_back(sx, sy); return path; }

        const int n = w_ * h_;
        const int start = sy * w_ + sx, goal = gy * w_ + gx;
        std::vector<int> g(n, INT32_MAX);
        std::vector<int> parent(n, -1);
        std::vector<uint8_t> closed(n, 0);

        // (f, h, idx) — f 同分比 h（偏好直線近的）,再比 idx 保決定性
        struct Node {
            int f, h, idx;
            bool operator>(const Node& o) const {
                if (f != o.f) return f > o.f;
                if (h != o.h) return h > o.h;
                return idx > o.idx;
            }
        };
        std::priority_queue<Node, std::vector<Node>,
                            std::greater<Node>> open;

        g[start] = 0;
        open.push({Heuristic(sx, sy, gx, gy), Heuristic(sx, sy, gx, gy),
                   start});
        while (!open.empty()) {
            const Node cur = open.top();
            open.pop();
            if (closed[cur.idx]) continue;
            closed[cur.idx] = 1;
            if (cur.idx == goal) break;
            const int cx = cur.idx % w_, cy = cur.idx / w_;
            for (int d = 0; d < 8; ++d) {
                const int nx = cx + detail::kDirX[d];
                const int ny = cy + detail::kDirY[d];
                if (!IsWalkable(nx, ny)) continue;
                if (d >= 4) {
                    // 斜走防切角：兩正交鄰格皆需可走
                    if (!IsWalkable(cx + detail::kDirX[d], cy) ||
                        !IsWalkable(cx, cy + detail::kDirY[d]))
                        continue;
                }
                const int ni = ny * w_ + nx;
                const int ng = g[cur.idx] + detail::kCost[d];
                if (ng < g[ni]) {
                    g[ni] = ng;
                    parent[ni] = cur.idx;
                    const int hh = Heuristic(nx, ny, gx, gy);
                    open.push({ng + hh, hh, ni});
                }
            }
        }
        if (parent[goal] == -1) return path;
        for (int i = goal; i != -1; i = parent[i])
            path.emplace_back(i % w_, i / w_);
        std::reverse(path.begin(), path.end());
        return path;
    }

private:
    bool Inside(int x, int y) const {
        return x >= 0 && y >= 0 && x < w_ && y < h_;
    }
    // octile 啟發（整數）:dx≥dy 時 10dx+4dy
    static int Heuristic(int x0, int y0, int x1, int y1) {
        const int dx = x0 > x1 ? x0 - x1 : x1 - x0;
        const int dy = y0 > y1 ? y0 - y1 : y1 - y0;
        const int hi = dx > dy ? dx : dy, lo = dx > dy ? dy : dx;
        return 10 * (hi - lo) + 14 * lo;
    }

    int w_, h_;
    std::vector<uint8_t> walk_;
};

/**
 * FlowField - 目標導向成本場
 * Compute(goalX, goalY) 後 Direction(x,y) 回下坡方向；
 * 不可達格 Direction 回 false。
 */
class FlowField {
public:
    FlowField(int width, int height)
        : w_(width), h_(height), walk_(width * height, true),
          cost_(width * height, INT32_MAX) {}

    void SetWalkable(int x, int y, bool v) {
        if (Inside(x, y)) walk_[y * w_ + x] = v;
    }
    bool IsWalkable(int x, int y) const {
        return Inside(x, y) && walk_[y * w_ + x];
    }

    // 從目標反向 Dijkstra（與 A* 同規則：8 向、防切角、整數成本）
    bool Compute(int gx, int gy) {
        if (!IsWalkable(gx, gy)) return false;
        std::fill(cost_.begin(), cost_.end(), INT32_MAX);
        // 反向場無啟發式——單純 BFS-成本擴散，FIFO+relax 即可
        struct Node {
            int c, idx;
            bool operator>(const Node& o) const {
                return c != o.c ? c > o.c : idx > o.idx;
            }
        };
        std::priority_queue<Node, std::vector<Node>,
                            std::greater<Node>> open;
        const int goal = gy * w_ + gx;
        cost_[goal] = 0;
        open.push({0, goal});
        while (!open.empty()) {
            const Node cur = open.top();
            open.pop();
            if (cur.c > cost_[cur.idx]) continue;
            const int cx = cur.idx % w_, cy = cur.idx / w_;
            for (int d = 0; d < 8; ++d) {
                const int nx = cx + detail::kDirX[d];
                const int ny = cy + detail::kDirY[d];
                if (!IsWalkable(nx, ny)) continue;
                if (d >= 4 &&
                    (!IsWalkable(cx + detail::kDirX[d], cy) ||
                     !IsWalkable(cx, cy + detail::kDirY[d])))
                    continue;
                const int ni = ny * w_ + nx;
                const int nc = cur.c + detail::kCost[d];
                if (nc < cost_[ni]) {
                    cost_[ni] = nc;
                    open.push({nc, ni});
                }
            }
        }
        return true;
    }

    // 成本場值；不可達/不可走回 INT32_MAX
    int Cost(int x, int y) const {
        return Inside(x, y) ? cost_[y * w_ + x] : INT32_MAX;
    }

    // 下坡單位方向（8 向之一）；不可達回 false
    bool Direction(int x, int y, float& dx, float& dy) const {
        if (!Inside(x, y) || cost_[y * w_ + x] == INT32_MAX ||
            cost_[y * w_ + x] == 0) {
            return false;  // 不可達或已在目標
        }
        const int best = BestNeighborCost(x, y);
        // 嚴格下坡：鄰格全不可達或等於/高於本格 → 無方向
        if (best == INT32_MAX || best >= cost_[y * w_ + x]) return false;
        for (int d = 0; d < 8; ++d) {
            const int nx = x + detail::kDirX[d];
            const int ny = y + detail::kDirY[d];
            if (!Inside(nx, ny)) continue;
            if (d >= 4 &&
                (!IsWalkable(x + detail::kDirX[d], y) ||
                 !IsWalkable(x, y + detail::kDirY[d])))
                continue;
            if (cost_[ny * w_ + nx] == best) {
                dx = static_cast<float>(detail::kDirX[d]);
                dy = static_cast<float>(detail::kDirY[d]);
                const float len = (d >= 4) ? 1.41421356f : 1.0f;
                dx /= len; dy /= len;
                return true;
            }
        }
        return false;
    }

private:
    bool Inside(int x, int y) const {
        return x >= 0 && y >= 0 && x < w_ && y < h_;
    }
    int BestNeighborCost(int x, int y) const {
        int best = INT32_MAX;
        for (int d = 0; d < 8; ++d) {
            const int nx = x + detail::kDirX[d];
            const int ny = y + detail::kDirY[d];
            if (!Inside(nx, ny)) continue;
            if (d >= 4 &&
                (!IsWalkable(x + detail::kDirX[d], y) ||
                 !IsWalkable(x, y + detail::kDirY[d])))
                continue;
            best = best < cost_[ny * w_ + nx] ? best : cost_[ny * w_ + nx];
        }
        return best;
    }

    int w_, h_;
    std::vector<uint8_t> walk_;
    std::vector<int> cost_;
};

} // namespace Potato
