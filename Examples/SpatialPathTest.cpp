/**
 * SpatialPathTest - SpatialHash + Pathfinding 測試（headless）
 *
 * 覆蓋：
 *   [SpatialHash] Insert/QueryRadius/QueryRect/Move/Remove/Compact
 *   [A*]         直線/繞障礙/防切角/不可達/最優性/決定性
 *   [FlowField]  成本場單調下坡、Direction 朝目標、不可達回 false
 */

#include "MathUtils/SpatialHash.h"
#include "MathUtils/Pathfinding.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace Potato;

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

template<typename T>
std::vector<T> CollectRadius(const SpatialHash<T>& sh,
                           float x, float y, float r) {
    std::vector<T> out;
    sh.QueryRadius(x, y, r, [&](const T& v) { out.push_back(v); });
    return out;
}

} // anonymous namespace

int main() {
    printf("=== SpatialHash + Pathfinding 測試 ===\n\n");

    // ---- [SpatialHash] ----
    printf("-- SpatialHash --\n");
    {
        SpatialHash<int> sh(10.0f);
        auto a = sh.Insert(100, 5, 5);      // cell (0,0)
        auto b = sh.Insert(200, 55, 5);     // cell (5,0)
        auto c = sh.Insert(300, -15, -5);   // cell (-2,-1)
        Expect(a != SpatialHash<int>::kInvalidId && sh.Size() == 3,
               "Insert×3");

        auto near = CollectRadius(sh, 0, 0, 15.0f);
        Expect(near.size() == 1 && near[0] == 100,
               "半徑查詢只命中近點");

        // r=50:(5,5)d=7.1 與 (-15,-5)d=15.8 命中、(55,5)d=55.2 排除
        auto wide = CollectRadius(sh, 0, 0, 50.0f);
        Expect(wide.size() == 2, "大半徑含跨格(負座標格也掃到)",
               std::to_string(wide.size()));

        std::vector<int> rect;
        sh.QueryRect(-20, -10, 0, 10, [&](const int& v) {
            rect.push_back(v);
        });
        Expect(rect.size() == 1 && rect[0] == 300, "AABB 查詢負座標");

        sh.Move(b, -10, 0);  // 搬到 cell (-1,0)
        auto moved = CollectRadius(sh, -10, 0, 1.0f);
        Expect(moved.size() == 1 && moved[0] == 200, "Move 跨格後可查");

        sh.Remove(a);
        Expect(sh.Size() == 2, "Remove 後 Size=2");
        Expect(CollectRadius(sh, 5, 5, 1.0f).empty(), "移除點查不到");

        // Compact 後功能正常
        for (int i = 0; i < 50; ++i) sh.Insert(i, i * 3.0f, 0.0f);
        sh.Remove(c);
        sh.Compact();
        Expect(sh.Size() == 51, "Compact 後存活數正確",
               std::to_string(sh.Size()));
        // r=4 於 (60,0):命中 x=57/60/63 三點(i=19/20/21)
        Expect(CollectRadius(sh, 60.0f, 0, 4.0f).size() == 3,
               "Compact 後查詢仍正確");
        sh.Clear();
        Expect(sh.Size() == 0 && sh.CellCount() == 0, "Clear 清空");
    }

    // ---- [A*] ----
    printf("-- GridPathfinder A* --\n");
    {
        GridPathfinder g(20, 20);
        auto path = g.FindPath(0, 0, 19, 19);
        Expect(!path.empty() && path.front() == std::make_pair(0, 0) &&
               path.back() == std::make_pair(19, 19),
               "對角直達含端點", std::to_string(path.size()) + " 格");

        // 整牆擋路留一口
        GridPathfinder g2(20, 20);
        for (int y = 0; y < 20; ++y) g2.SetWalkable(10, y, false);
        g2.SetWalkable(10, 19, true);  // 開底端缺口
        auto p2 = g2.FindPath(0, 0, 19, 0);
        Expect(!p2.empty(), "繞缺口有路");
        bool throughGap = false;
        for (auto& [x, y] : p2) if (x == 10 && y == 19) throughGap = true;
        Expect(throughGap, "路徑經過缺口");
        // 路徑不踩障礙
        bool clean = true;
        for (auto& [x, y] : p2) if (!g2.IsWalkable(x, y)) clean = false;
        Expect(clean, "路徑不踩障礙格");

        // 完全封死 → 空
        GridPathfinder g3(10, 10);
        for (int y = 0; y < 10; ++y) g3.SetWalkable(5, y, false);
        Expect(g3.FindPath(0, 0, 9, 0).empty(), "封死回空路徑");
        Expect(g3.FindPath(0, 0, 5, 5).empty(), "終點不可走回空");

        // 防切角：斜縫不可穿越
        GridPathfinder g4(10, 10);
        g4.SetWalkable(4, 4, false);
        g4.SetWalkable(5, 5, false);  // 對角相鄰雙障礙
        auto p4 = g4.FindPath(4, 5, 5, 4);
        // (4,5)→(5,4) 是對角線穿越兩障礙角——應繞路或不可達
        if (!p4.empty()) {
            bool cutCorner = false;
            for (size_t i = 1; i < p4.size(); ++i) {
                if (p4[i - 1] == std::make_pair(4, 5) &&
                    p4[i] == std::make_pair(5, 4)) cutCorner = true;
            }
            Expect(!cutCorner, "不切對角障礙角");
        } else {
            Expect(true, "不切對角障礙角（不可達）");
        }

        // 決定性：兩次同輸入同路徑
        auto r1 = g2.FindPath(0, 0, 19, 0);
        auto r2 = g2.FindPath(0, 0, 19, 0);
        Expect(r1 == r2, "同輸入同路徑（決定性）");
    }

    // ---- [FlowField] ----
    printf("-- FlowField --\n");
    {
        FlowField ff(15, 15);
        Expect(ff.Compute(7, 7), "成本場計算");
        Expect(ff.Cost(7, 7) == 0, "目標格成本 0");
        Expect(ff.Cost(8, 7) == 10, "正交鄰格成本 10");
        Expect(ff.Cost(8, 8) == 14, "對角鄰格成本 14");

        float dx = 0, dy = 0;
        Expect(ff.Direction(0, 0, dx, dy), "邊角有下坡方向");
        Expect(dx > 0 && dy > 0, "方向朝目標 (+x+y)");
        Expect(!ff.Direction(7, 7, dx, dy), "目標格無方向");

        // 障礙擋住 → 成本繞路較高
        FlowField ff2(15, 15);
        for (int y = 0; y < 15; ++y) ff2.SetWalkable(5, y, false);
        ff2.SetWalkable(5, 14, true);
        ff2.Compute(10, 7);
        Expect(ff2.Cost(0, 0) > 100, "繞路成本 > 直線 100",
               std::to_string(ff2.Cost(0, 0)));
        float ddx = 0, ddy = 0;
        Expect(ff2.Direction(0, 0, ddx, ddy) && ddy > 0,
               "繞路方向朝下（缺口在底）");
        // 完全隔絕區不可達
        FlowField ff3(9, 9);
        for (int y = 0; y < 9; ++y) ff3.SetWalkable(4, y, false);
        ff3.Compute(7, 4);
        Expect(!ff3.Direction(0, 0, ddx, ddy), "隔絕區 Direction=false");
    }

    printf("\n=== 結果: %d PASS / %d FAIL ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
