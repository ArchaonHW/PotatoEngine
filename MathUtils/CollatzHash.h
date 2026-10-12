#pragma once

#ifndef POTATO_COLLATZHASH_H
#define POTATO_COLLATZHASH_H

#include <cstdint>

namespace Potato {
namespace Quasi {

/**
 * Collatz（3n+1）停滯時間 → 確定性特徵雜湊
 *
 * 數學背景（docs/research/UNSOLVED_MATH_TO_ENGINE.md）：
 *   Collatz 猜想未證——不能保證任意 n 終止於 1，所以
 *   StoppingTime 內建 maxSteps 熔斷（這是誠實性也是防禦）。
 *   工程上取用的是停滯時間函式的三個性質：
 *     - 完全確定（同輸入同輸出 → 可回放、可重現）
 *     - 無可見週期（不像正弦/方格雜湊有條紋）
 *     - 重尾分佈：絕大多數 n 很快收斂，少數拖出長軌跡
 *       → 天然適合撒佈「稀有特徵點」（古林/遺跡/渡口選址）
 *
 * 誠實性：本檔不證明也不依賴猜想成立；熔斷步數內未收斂
 *   的輸入回傳 maxSteps（對雜湊用途只是另一個值）。
 */

namespace detail {
// splitmix64 finalizer：把格點座標+seed 混合成雪崩良好的 64-bit 值
inline uint64_t CollatzMix64(uint64_t v) {
    v ^= v >> 30;
    v *= 0xBF58476D1CE4E5B9ull;
    v ^= v >> 27;
    v *= 0x94D049BB133111EBull;
    v ^= v >> 31;
    return v;
}
} // namespace detail

/**
 * n 的 Collatz 停滯時間（到達 1 的步數）。
 * n==0 視為 0 步；超過 maxSteps 未收斂回傳 maxSteps（熔斷）。
 * 3n+1 溢位時用確定性摺疊（右移+xorshift）繼續——
 * 對雜湊語義無礙，只是另一條軌跡。
 */
inline uint32_t CollatzStoppingTime(uint64_t n, uint32_t maxSteps = 512) {
    uint32_t steps = 0;
    while (n > 1 && steps < maxSteps) {
        if (n & 1ull) {
            n = (n > (0xFFFFFFFFFFFFFFFFull - 1ull) / 3ull)
                    ? (n >> 1) ^ 0x9E3779B97F4A7C15ull  // 溢位摺疊
                    : 3 * n + 1;
        } else {
            n >>= 1;
        }
        ++steps;
    }
    return steps;
}

/**
 * 格點 (x,y)+seed → Collatz 場值 [0,1]
 *
 * 座標先混合到小範圍奇數域（冰雹軌峰值有界、免溢位路徑），
 * 停滯時間以 scale 正規化。分佈重尾：多數格 <0.5，
 * 少數格逼近 1 → 閾值越高特徵越稀有。
 */
inline float CollatzField01(int x, int y, uint64_t seed = 0,
                            float scale = 320.0f) {
    uint64_t n = detail::CollatzMix64(
        static_cast<uint64_t>(static_cast<uint32_t>(x)) * 0x9E3779B1u +
        static_cast<uint64_t>(static_cast<uint32_t>(y)) * 0x85EBCA77u +
        seed * 0x27D4EB2Fu);
    n = 2 + n % 999983u; // 收進小冰雹域：<1e6 的軌峰值遠低於 u64 上限
    const float t = static_cast<float>(CollatzStoppingTime(n));
    if (!(scale > 0.0f)) return 0.0f;
    const float v = t / scale;
    return v > 1.0f ? 1.0f : v;
}

/**
 * 特徵桶：場值等分為 buckets 桶，回傳 [0, buckets)。
 * 可直接當確定性格點雜湊用（選址/分類/查表索引）。
 */
inline uint32_t CollatzFeature(int x, int y, uint64_t seed,
                               uint32_t buckets) {
    if (buckets == 0) return 0;
    const float v = CollatzField01(x, y, seed);
    uint32_t b = static_cast<uint32_t>(v * static_cast<float>(buckets));
    return b >= buckets ? buckets - 1 : b;
}

} // namespace Quasi
} // namespace Potato

#endif // POTATO_COLLATZHASH_H
