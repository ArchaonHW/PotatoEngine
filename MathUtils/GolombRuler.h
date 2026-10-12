#pragma once

#ifndef POTATO_GOLOMBRULER_H
#define POTATO_GOLOMBRULER_H

#include "MathUtils/Vector2.h"

#include <cmath>
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace Potato {
namespace Quasi {

/**
 * Golomb 尺（完美差異尺）——所有 pairwise 距離互異的整數刻度
 *
 * 數學背景（docs/research/UNSOLVED_MATH_TO_ENGINE.md 第三批）：
 *   最優 Golomb 尺（n 個刻度的最短尺）只在 n≤27 被證明；
 *   一般 n 無閉式解，大 n 連最優長度都不知道。
 *   引擎取用：內建已驗證的最優尺表（n≤12），更大的 n 走
 *   貪心構造——「全異距」性質保持，只是長度不再最優。
 *
 * 用途：採樣抽頭去相關（任意兩對抽頭間距不重複 → 無共振
 *   條紋）、編隊槽位沿軸去對稱擺放、延遲線 tap 設計等需要
 *   「每對間距皆唯一」的場合。
 *
 * 誠實性：貪心路徑的尺長非最優（例如 n=13 貪心 = 147 > 最優
 *   106）；呼叫方若在乎緊緻性請自查 Marks(n).back()。
 */

// 已驗證最優尺表（n≤12；長度 0,1,3,6,11,17,25,34,44,55,72,85
// 對應 OEIS A079426）。回傳空表 = n 不在表內（走 GolombMarks 貪心）
inline const std::vector<int>& OptimalGolombRuler(int n) {
    static const std::vector<std::vector<int>> kTable = {
        {0},                                        // n=1
        {0, 1},                                     // n=2
        {0, 1, 3},                                  // n=3
        {0, 1, 4, 6},                               // n=4
        {0, 1, 4, 9, 11},                           // n=5
        {0, 1, 4, 10, 12, 17},                      // n=6
        {0, 1, 4, 10, 18, 23, 25},                  // n=7
        {0, 1, 4, 9, 15, 22, 32, 34},               // n=8
        {0, 1, 5, 12, 25, 27, 35, 41, 44},          // n=9
        {0, 1, 6, 10, 23, 26, 34, 41, 53, 55},      // n=10
        {0, 1, 4, 13, 28, 33, 47, 54, 64, 70, 72},  // n=11
        {0, 2, 6, 24, 29, 40, 43, 55, 68, 75, 76, 85}, // n=12
    };
    static const std::vector<int> kEmpty;
    return (n >= 1 && n <= 12) ? kTable[static_cast<size_t>(n - 1)]
                               : kEmpty;
}

// 驗證：嚴格遞增且所有 pairwise 距離互異
inline bool IsGolomb(const std::vector<int>& marks) {
    for (size_t i = 1; i < marks.size(); ++i)
        if (marks[i] <= marks[i - 1]) return false;
    std::unordered_set<int> diffs;
    for (size_t i = 0; i < marks.size(); ++i)
        for (size_t j = i + 1; j < marks.size(); ++j)
            if (!diffs.insert(marks[j] - marks[i]).second) return false;
    return true;
}

// n 刻度 Golomb 尺：n≤12 給最優尺；n>12 貪心（保持全異距，
// 長度非最優）。n≤0 回空表。
inline std::vector<int> GolombMarks(int n) {
    if (n <= 0) return {};
    const std::vector<int>& opt = OptimalGolombRuler(n);
    if (!opt.empty()) return opt;

    // 貪心：逐步追加最小合法刻度——新刻度 c 合法 ⟺
    // {c - mᵢ} 全異且不與既有差集相交
    std::vector<int> marks{0, 1};
    std::unordered_set<int> diffs{1};
    while (static_cast<int>(marks.size()) < n) {
        for (int c = marks.back() + 1;; ++c) {
            std::vector<int> added;
            added.reserve(marks.size());
            bool ok = true;
            for (int m : marks) {
                const int d = c - m;
                if (diffs.count(d)) { ok = false; break; }
                added.push_back(d);
            }
            // 新差集內部也必須互異（marks 遞增 → c-m 遞減互異，
            // 恆成立；但保留檢查以自證）
            if (ok) {
                for (int d : added) diffs.insert(d);
                marks.push_back(c);
                break;
            }
        }
    }
    return marks;
}

// 編隊槽位：第 i 槽放黃金角 i·137.5°、半徑 marks[i]·spacing 的
// 螺旋位——半徑沿尺取 → 相鄰槽間距全異（去對稱/無同心環）。
// 回傳 n 個 2D 偏移（第 0 槽在原點）。n≤0 回空表。
inline std::vector<Vector2> FormationOffsets(int n, float spacing) {
    std::vector<Vector2> out;
    if (n <= 0 || !(spacing > 0.0f)) return out;
    const std::vector<int> marks = GolombMarks(n);
    const float goldenAngle = 2.39996322972865332f;
    out.reserve(marks.size());
    for (size_t i = 0; i < marks.size(); ++i) {
        const float r = static_cast<float>(marks[i]) * spacing;
        const float a = goldenAngle * static_cast<float>(i);
        out.emplace_back(r * std::cos(a), r * std::sin(a));
    }
    return out;
}

} // namespace Quasi
} // namespace Potato

#endif // POTATO_GOLOMBRULER_H
