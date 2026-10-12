#pragma once

#ifndef POTATO_GUSTFIELD_H
#define POTATO_GUSTFIELD_H

#include "MathUtils/Vector3.h"
#include "MathUtils/CurlNoise.h"
#include "MathUtils/QuasiRandom.h"

#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace Potato {
namespace Quasi {

/**
 * 間歇性陣風場（turbulence intermittency 的行為級近似）
 *
 * 物理背景（docs/research/UNSOLVED_PHYSICS_TO_ENGINE.md 第三批）：
 *   Navier–Stokes 的未解不只有光滑性——湍流能量耗散集中在
 *   稀疏突發區域（間歇性 / 異常標度指數），平滑場畫不出
 *   「一陣一陣」的風。CurlNoise 給無散度基底流，本類補
 *   乘性調製：強度場 g(p,t) 是對數正態式級聯
 *
 *     χ(p,t) = Σᵢ Aᵢ·sin(kᵢ·p + ωᵢt + φᵢ)，歸一化至單位變異
 *     g = exp(σ·χ̂ − σ²/2)   →   E[g] ≈ 1，重尾突發
 *
 *   多數時空點微風（g<1），少數點爆發陣風（g≫1）——
 *   間歇性的外觀特徵。同一 seed 完全決定，可回放。
 *
 * 誠實性：g·v 不再嚴格無散度（∇·(gv)=∇g·v≠0）——
 *   間歇性調製犧牲精確不變量換突發外觀；要嚴格無散度
 *   請用 TurbulenceField 本體。
 *
 * 用途：雲漂移忽強忽弱、粒子陣風、旗幟/鏡頭的突發擾動。
 */
class GustField {
public:
    // octaves：疊加層數；sigma：間歇強度（0=恆定 1，越大突發越尖）
    explicit GustField(int octaves = 5, uint64_t seed = 0,
                       float baseFrequency = 0.08f, float sigma = 0.7f) {
        Build(octaves, seed, baseFrequency, sigma);
    }

    // 陣風強度 ∈ (0,∞)，集合平均≈1；永不為負
    float Intensity(const Vector3& p, float t = 0.0f) const {
        double chi = 0.0;
        for (const Octave& o : waves) {
            chi += o.amp * std::sin(
                static_cast<double>(o.k.x) * p.x +
                static_cast<double>(o.k.y) * p.y +
                static_cast<double>(o.k.z) * p.z +
                o.omega * t + o.phase);
        }
        return static_cast<float>(
            std::exp(sigma * chi * invNorm - 0.5 * sigma * sigma));
    }

    // 複合取樣：基底流 × 陣風強度（注意：非無散度，見類註解）
    Vector3 Sample(const Vector3& p, float t,
                   const TurbulenceField& flow) const {
        const Vector3 v = flow.Sample(p, t);
        const float g = Intensity(p, t);
        return Vector3(v.x * g, v.y * g, v.z * g);
    }

private:
    struct Octave {
        Vector3 k;      // 波向量（方向=R3 準隨機，長度=頻率）
        float phase;    // 相位
        float omega;    // 時間角頻率
        float amp;      // 振幅（遞減 1/f）
    };

    void Build(int octaves, uint64_t seed, float baseFreq, float s) {
        waves.clear();
        sigma = s;
        std::mt19937_64 rng(seed);
        std::uniform_real_distribution<float> u01(0.0f, 1.0f);

        constexpr double g1 = 0.8191725133961644;  // 1/ψ₃
        constexpr double g2 = 0.6710436067037893;  // 1/ψ₃²

        float freq = baseFreq, amp = 1.0f;
        double ampSq = 0.0;
        for (int i = 0; i < octaves; ++i) {
            Octave o;
            const double u = detail::Frac((i + seed) * g1);
            const double vv = detail::Frac((i + seed) * g2);
            const double z = 2.0 * u - 1.0;
            const double r = std::sqrt(std::max(0.0, 1.0 - z * z));
            const double az = 6.283185307179586 * vv;
            o.k = Vector3(static_cast<float>(r * std::cos(az) * freq),
                          static_cast<float>(r * std::sin(az) * freq),
                          static_cast<float>(z * freq));
            o.amp = amp;
            o.phase = 6.2831853f * u01(rng);
            o.omega = u01(rng) * 0.8f; // 陣風演化比基底流快
            waves.push_back(o);
            ampSq += static_cast<double>(amp) * amp;
            freq *= 2.0f;
            amp *= 0.5f;
        }
        // Σ Aᵢ·sin(·) 的變異數 ≈ ΣAᵢ²/2 → χ̂ 近單位變異
        invNorm = ampSq > 0.0
                      ? static_cast<float>(std::sqrt(2.0 / ampSq))
                      : 0.0f;
    }

    float sigma = 0.7f;
    float invNorm = 1.0f;
    std::vector<Octave> waves;
};

} // namespace Quasi
} // namespace Potato

#endif // POTATO_GUSTFIELD_H
