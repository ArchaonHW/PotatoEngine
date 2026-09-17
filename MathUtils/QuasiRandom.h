#pragma once

#ifndef POTATO_QUASIRANDOM_H
#define POTATO_QUASIRANDOM_H

#include "MathUtils/Vector2.h"

#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace Potato {
namespace Quasi {

/**
 * 準隨機（低差異）序列與藍噪取樣
 *
 * 數學背景（docs/research/UNSOLVED_MATH_TO_ENGINE.md）：
 *   - 最優 star-discrepancy 常數仍是開放問題；實務上黃金比例族
 *     序列（R2/R3，Roberts 2018）是無表查法中的最佳近似之一
 *   - Poisson-disk 取樣對應受限區域圓盤堆積問題的 O(n) 近似
 *     （Bridson 2007）
 *
 * 用途：疊加態候選格散佈、AA/軟陰影 jitter、合成資料取樣等需要
 * 「均勻但不叢聚」的場合。需要無結構隨機時仍用 mt19937。
 *
 * 注意：低維投影存在確定性結構——需要打亂時由呼叫方加 PRNG
 * jitter 或 random skip（各函式提供 skip 參數）。
 */

namespace detail {
// 取小數部分（{x}）；負值亦回傳 [0,1)
inline double Frac(double v) {
    return v - std::floor(v);
}
} // namespace detail

// ---- 一維基底序列 ----

// van der Corput：第 n 項 = 二進位反轉 n / 2^k
inline double VanDerCorput(uint64_t n, int base = 2) {
    double result = 0.0;
    double denom = 1.0;
    double b = static_cast<double>(base);
    while (n > 0) {
        denom *= b;
        result += static_cast<double>(n % static_cast<uint64_t>(base)) / denom;
        n /= static_cast<uint64_t>(base);
    }
    return result;
}

// Halton：第 n 項在第 dim 維用互質基底（2,3,5,7,11,...）
inline double Halton(uint64_t n, int dim) {
    static const int kPrimes[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29,
                                  31, 37, 41, 43, 47, 53};
    const int base = kPrimes[dim % 16];
    return VanDerCorput(n + 1, base); // +1 跳過 0
}

// ---- 黃金比例族序列（R-sequence）----
// 塑性常數 ψ_d：ψ^(d+1) = ψ + 1 的正根。
// R_d(n) = ( frac(n·α_1), frac(n·α_2), ... )，α_i = 1/ψ_d^i
//
// d=1 即黃金比例 φ=1.618；d=2 塑性常數 ψ₂≈1.324718；
// d=3 ψ₃≈1.220745。低維投影條紋比其他序列少。

// 以 Newton 迭代求 ψ_d 的正根
inline double PlasticConstant(int dim) {
    double psi = 1.5; // 初值 > 1
    for (int i = 0; i < 64; ++i) {
        // f(x) = x^(d+1) - x - 1；Newton: x -= f/f'
        double f = std::pow(psi, dim + 1) - psi - 1.0;
        double fp = (dim + 1) * std::pow(psi, dim) - 1.0;
        double next = psi - f / fp;
        if (std::abs(next - psi) < 1e-15) break;
        psi = next;
    }
    return psi;
}

// R2：回傳第 n 個 2D 準隨機點（座標在 [0,1)²）
inline Vector2 R2(uint64_t n, uint64_t skip = 0) {
    constexpr double kInvPsi2   = 0.7548776662466927;  // 1/ψ₂
    constexpr double kInvPsi2_2 = 0.5698402909980532;  // 1/ψ₂²
    const double i = static_cast<double>(n + skip);
    return Vector2(
        static_cast<float>(detail::Frac(i * kInvPsi2)),
        static_cast<float>(detail::Frac(i * kInvPsi2_2)));
}

// R2 的圓盤版：均勻映射到半徑 radius 的圓內
// （面積均勻：r = sqrt(u)·R，θ = 2π·v）
inline Vector2 R2InDisk(uint64_t n, float radius, uint64_t skip = 0) {
    const Vector2 p = R2(n, skip);
    const float r = radius * std::sqrt(p.x);
    const float theta = 2.0f * 3.14159265358979323846f * p.y;
    return Vector2(r * std::cos(theta), r * std::sin(theta));
}

// ---- Poisson-disk 藍噪取樣（Bridson 2007）----
// 在圓盤（center, radius）內產生 count 個點，任意兩點距離 ≥ minDist。
// 實作為簡化 dart-throwing + 空間雜湊加速；seeded → 可重現。
// 回傳個數可能略少於 count（minDist 過大時飽和），呼叫方檢查 size()。
inline std::vector<Vector2> PoissonDisk(Vector2 center, float radius,
                                        int count, float minDist,
                                        uint64_t seed = 0) {
    std::vector<Vector2> points;
    if (count <= 0 || radius <= 0.0f || minDist <= 0.0f) return points;

    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);

    // 空間雜湊：cell 邊長 = minDist/√2 → 每格至多一點
    const float cell = minDist / 1.4142135623730951f;
    const float inv = 1.0f / cell;
    const int gridSide = static_cast<int>(std::ceil(2.0f * radius * inv)) + 2;
    std::vector<int> grid(static_cast<size_t>(gridSide * gridSide), -1);
    const Vector2 origin(center.x - radius, center.y - radius);

    auto gridIndex = [&](const Vector2& p) -> int {
        int gx = static_cast<int>((p.x - origin.x) * inv);
        int gy = static_cast<int>((p.y - origin.y) * inv);
        if (gx < 0 || gy < 0 || gx >= gridSide || gy >= gridSide) return -1;
        return gy * gridSide + gx;
    };

    auto farFromAll = [&](const Vector2& p) -> bool {
        int gx = static_cast<int>((p.x - origin.x) * inv);
        int gy = static_cast<int>((p.y - origin.y) * inv);
        for (int dy = -2; dy <= 2; ++dy) {
            for (int dx = -2; dx <= 2; ++dx) {
                int nx = gx + dx, ny = gy + dy;
                if (nx < 0 || ny < 0 || nx >= gridSide || ny >= gridSide)
                    continue;
                int idx = grid[ny * gridSide + nx];
                if (idx < 0) continue;
                const Vector2 d = points[static_cast<size_t>(idx)] - p;
                if (d.LengthSquared() < minDist * minDist) return false;
            }
        }
        return true;
    };

    const int maxAttempts = count * 30; // 飽和時避免無窮迴圈
    for (int attempt = 0; attempt < maxAttempts &&
         static_cast<int>(points.size()) < count; ++attempt) {
        // 圓內均勻取樣
        const float r = radius * std::sqrt(u01(rng));
        const float theta = 2.0f * 3.14159265358979323846f * u01(rng);
        Vector2 p(center.x + r * std::cos(theta),
                  center.y + r * std::sin(theta));
        if (!farFromAll(p)) continue;
        int gi = gridIndex(p);
        if (gi >= 0) grid[static_cast<size_t>(gi)] =
            static_cast<int>(points.size());
        points.push_back(p);
    }
    return points;
}

// ---- 差異度估算（測試/驗證用）----
// 簡化版 L2 star-discrepancy 代理：比較「每個子區域的計數比例」
// 與面積比例的 RMS 偏差。非嚴格 star-discrepancy，但足以
// 分辨 準隨機序列 vs PRNG 的均勻性差異。
inline double Discrepancy2D(const std::vector<Vector2>& points,
                            int gridDivisions = 16) {
    if (points.empty() || gridDivisions <= 0) return 0.0;
    const int n = gridDivisions;
    const double cellArea = 1.0 / (n * n);
    const double total = static_cast<double>(points.size());
    std::vector<int> counts(static_cast<size_t>(n * n), 0);
    for (const Vector2& p : points) {
        int gx = static_cast<int>(p.x * n);
        int gy = static_cast<int>(p.y * n);
        gx = gx < 0 ? 0 : (gx >= n ? n - 1 : gx);
        gy = gy < 0 ? 0 : (gy >= n ? n - 1 : gy);
        ++counts[static_cast<size_t>(gy * n + gx)];
    }
    double sumSq = 0.0;
    for (int c : counts) {
        const double dev = c / total - cellArea;
        sumSq += dev * dev;
    }
    return std::sqrt(sumSq / (n * n));
}

} // namespace Quasi
} // namespace Potato

#endif // POTATO_QUASIRANDOM_H
