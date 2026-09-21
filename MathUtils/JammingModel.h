#pragma once

#ifndef POTATO_JAMMINGMODEL_H
#define POTATO_JAMMINGMODEL_H

#include "MathUtils/Vector2.h"

#include <cmath>
#include <vector>

namespace Potato {
namespace Quasi {

/**
 * 擁擠阻塞模型（jamming transition 的行為級近似）
 *
 * 物理背景（docs/research/UNSOLVED_PHYSICS_TO_ENGINE.md）：
 *   玻璃轉變/阻塞（jamming）——顆粒與人群在高密度下的相變
 *   沒有統一理論；引擎取用的是交通流「基本圖」式近似：
 *   密度低於臨界值自由流，逼近阻塞密度時速度急降——
 *   不模擬個體推擠力，只給密度 → 移速倍率的單調映射。
 *
 * 用途：渡口/狹橋多隊擁擠減速、人群攤位密度效果、
 *   任何「數量密度 → 速度懲罰」的場合。純函式，無狀態，
 *   呼叫端每 tick 以最新位置重算即可。
 *
 * 誠實性：這不是 jamming 物理求解器；臨界/阻塞密度是
 *   可調參數而非理論值。不聲稱解決玻璃轉變問題。
 */
struct JammingParams {
    float criticalDensity = 0.35f; // 佔有率：低於此值完全自由流
    float jamDensity      = 0.85f; // 佔有率：達此值視為完全阻塞
    float minSpeedFactor  = 0.05f; // 阻塞時的殘速倍率（不為 0 避免死鎖）
    float sharpness       = 2.0f;  // 相變陡度：越大越晚降、降越急
    float unitArea        = 0.15f; // 加權版：單位權重的佔地面積（格²）
};

/**
 * 密度 → 移速倍率，值域 [minSpeedFactor, 1.0]
 *
 *   density ≤ critical → 1.0（自由流）
 *   critical < density < jam → 1−(1−minF)·u^sharpness，u∈(0,1)
 *   density ≥ jam → minSpeedFactor（阻塞殘速）
 *
 * u^sharpness 形狀：中段仍接近自由流，接近 jam 才急降——
 * 對應 jamming 的「突然卡死」而非線性變慢。
 */
inline float JammingSpeedFactor(float density,
                                const JammingParams& p = JammingParams{}) {
    if (!(density > p.criticalDensity)) return 1.0f;   // NaN/低密度→自由流
    if (density >= p.jamDensity ||
        !(p.jamDensity > p.criticalDensity)) {         // 退化參數→直接阻塞
        return p.minSpeedFactor;
    }
    const float u = (density - p.criticalDensity) /
                    (p.jamDensity - p.criticalDensity);
    return p.minSpeedFactor +
           (1.0f - p.minSpeedFactor) * (1.0f - std::pow(u, p.sharpness));
}

// 是否進入阻塞態（供觸發/事件判定）
inline bool IsJammed(float density,
                     const JammingParams& p = JammingParams{}) {
    return density >= p.jamDensity;
}

/**
 * 局部佔有率：center 半徑 radius 圓盤內，鄰體總佔地 / 圓盤面積。
 * 每個鄰體視為半徑 bodyRadius 的圓。回傳可 >1（重疊超擠）。
 * 呼叫端若想計入自身，把自身也放進 bodies。
 */
inline float CrowdDensity(const Vector2& center,
                          const std::vector<Vector2>& bodies,
                          float radius, float bodyRadius) {
    if (!(radius > 0.0f) || !(bodyRadius > 0.0f)) return 0.0f;
    const float r2 = radius * radius;
    int count = 0;
    for (const Vector2& b : bodies) {
        const Vector2 d = b - center;
        if (d.LengthSquared() <= r2) ++count;
    }
    return static_cast<float>(count) *
           (bodyRadius * bodyRadius) / r2; // π 相消
}

/**
 * 加權版：bodies[i] 佔地 = weights[i] · unitArea（例如小隊成員數）。
 * weights 長度不足處以 1.0 補。
 */
inline float CrowdDensity(const Vector2& center,
                          const std::vector<Vector2>& bodies,
                          const std::vector<float>& weights,
                          float radius, float unitArea) {
    if (!(radius > 0.0f) || !(unitArea > 0.0f)) return 0.0f;
    const float r2 = radius * radius;
    float occupied = 0.0f;
    for (size_t i = 0; i < bodies.size(); ++i) {
        const Vector2 d = bodies[i] - center;
        if (d.LengthSquared() > r2) continue;
        const float w = (i < weights.size()) ? weights[i] : 1.0f;
        occupied += w * unitArea;
    }
    constexpr float kPi = 3.14159265358979323846f;
    return occupied / (kPi * r2);
}

} // namespace Quasi
} // namespace Potato

#endif // POTATO_JAMMINGMODEL_H
