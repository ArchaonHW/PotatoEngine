#pragma once

#ifndef POTATO_CURLNOISE_H
#define POTATO_CURLNOISE_H

#include "MathUtils/Vector3.h"
#include "MathUtils/QuasiRandom.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace Potato {
namespace Quasi {

/**
 * 無散度湍流場（curl noise / synthetic turbulence）
 *
 * 物理背景（docs/research/UNSOLVED_PHYSICS_TO_ENGINE.md）：
 *   Navier–Stokes 光滑性是千禧年未解難題，遊戲不求解它；
 *   不可壓縮湍流的關鍵外觀不變量是「無散度」∇·v = 0。
 *   取向量勢 ψ 的速度場 v = ∇×ψ 散度嚴格為零（向量恆等式），
 *   這裡用解析正弦疊加——不是有限差分近似，是精確成立。
 *
 *   ψ_c(p,t) = Σᵢ Aᵢ · sin(kᵢ·p + ωᵢ·t + φᵢ,c)
 *   v = ∇×ψ 由解析微分求出（見 Sample）。
 *
 *   波向量方向取自 R3 準隨機序列（塑性常數），避免週期性條紋；
 *   同一 seed 完全決定，可回放。
 *
 * 用途：戰場霧漂移、粒子環境運動、砲彈風偏、鏡頭微晃。
 * 這是「外觀湍流」，不含壓力-速度耦合，不是流體求解器。
 */
class TurbulenceField {
public:
    // octaves：疊加層數（4–8 視覺已足夠）；seed 決定波向量與相位
    explicit TurbulenceField(int octaves = 6, uint64_t seed = 0,
                             float baseFrequency = 0.15f,
                             float baseAmplitude = 1.0f) {
        Build(octaves, seed, baseFrequency, baseAmplitude);
    }

    // 取樣位置 p（世界座標）在時間 t 的速度；回傳無散度向量場
    Vector3 Sample(const Vector3& p, float t = 0.0f) const {
        Vector3 v(0.0f, 0.0f, 0.0f);
        for (const Octave& o : waves) {
            // v = ∇×ψ；ψ_c = A_c sin(s)，s = k·p + ωt + φ_c
            // ∂ψ_c/∂p_j = A_c cos(s) k_j
            const double s = static_cast<double>(o.k.x) * p.x +
                             static_cast<double>(o.k.y) * p.y +
                             static_cast<double>(o.k.z) * p.z +
                             o.omega * t;
            // 每分量獨立相位 → 三個獨立勢分量
            const double cx = std::cos(s + o.phase.x);
            const double cy = std::cos(s + o.phase.y);
            const double cz = std::cos(s + o.phase.z);
            // curl 分量：
            //   v_x = ∂ψ_z/∂y − ∂ψ_y/∂z = k_y·A_z·cz − k_z·A_y·cy
            //   v_y = ∂ψ_x/∂z − ∂ψ_z/∂x = k_z·A_x·cx − k_x·A_z·cz
            //   v_z = ∂ψ_y/∂x − ∂ψ_x/∂y = k_x·A_y·cy − k_y·A_x·cx
            v.x += static_cast<float>(o.k.y * o.amp.z * cz -
                                      o.k.z * o.amp.y * cy);
            v.y += static_cast<float>(o.k.z * o.amp.x * cx -
                                      o.k.x * o.amp.z * cz);
            v.z += static_cast<float>(o.k.x * o.amp.y * cy -
                                      o.k.y * o.amp.x * cx);
        }
        return v;
    }

    // 散度驗證（測試用）：有限差分估算 ∇·v，應≈0（精度 ~1e-5）
    double EstimateDivergence(const Vector3& p, float t,
                              float eps = 1e-3f) const {
        const Vector3 dx = Sample(p + Vector3(eps, 0, 0), t) -
                           Sample(p - Vector3(eps, 0, 0), t);
        const Vector3 dy = Sample(p + Vector3(0, eps, 0), t) -
                           Sample(p - Vector3(0, eps, 0), t);
        const Vector3 dz = Sample(p + Vector3(0, 0, eps), t) -
                           Sample(p - Vector3(0, 0, eps), t);
        return static_cast<double>(dx.x + dy.y + dz.z) / (2.0 * eps);
    }

private:
    struct Octave {
        Vector3 k;      // 波向量（方向=R3 準隨機，長度=頻率）
        Vector3 amp;    // 各勢分量振幅
        Vector3 phase;  // 各勢分量相位
        float omega;    // 時間角頻率
    };

    void Build(int octaves, uint64_t seed, float baseFreq, float baseAmp) {
        waves.clear();
        std::mt19937_64 rng(seed);
        std::uniform_real_distribution<float> u01(0.0f, 1.0f);

        // R3 的逆塑性常數分量（ψ₃ ≈ 1.2207 的冪次倒數）
        constexpr double g1 = 0.8191725133961644;  // 1/ψ₃
        constexpr double g2 = 0.6710436067037893;  // 1/ψ₃²

        float freq = baseFreq, amp = baseAmp;
        for (int i = 0; i < octaves; ++i) {
            Octave o;
            // 方向：R3 點映射到球面（z = 2u−1, φ = 2πv）
            const double u = detail::Frac((i + seed) * g1);
            const double vv = detail::Frac((i + seed) * g2);
            const double z = 2.0 * u - 1.0;
            const double r = std::sqrt(std::max(0.0, 1.0 - z * z));
            const double az = 6.283185307179586 * vv;
            o.k = Vector3(static_cast<float>(r * std::cos(az) * freq),
                          static_cast<float>(r * std::sin(az) * freq),
                          static_cast<float>(z * freq));
            // 三分量振幅/相位各自獨立（破壞對稱→場更像湍流）
            o.amp = Vector3(amp * u01(rng), amp * u01(rng), amp * u01(rng));
            o.phase = Vector3(6.2831853f * u01(rng),
                              6.2831853f * u01(rng),
                              6.2831853f * u01(rng));
            o.omega = u01(rng) * 0.5f; // 緩慢時間演化
            waves.push_back(o);
            freq *= 2.0f;   // 倍頻
            amp *= 0.5f;    // 振幅遞減（1/f 外觀）
        }
    }

    std::vector<Octave> waves;
};

} // namespace Quasi
} // namespace Potato

#endif // POTATO_CURLNOISE_H
