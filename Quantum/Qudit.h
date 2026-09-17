#pragma once

#include "RandomSource.h"

#include <complex>
#include <cstdint>
#include <memory>
#include <random>
#include <vector>

namespace Potato {
namespace Quantum {

/**
 * d 維量子態（qudit）
 *
 * QubitRegister 的推廣：狀態為 d 個複數振幅，|a_i|^2 總和 = 1。
 * 用途：候選數非 2 冪的疊加態建模（如敵軍可能在 5 個格子中的疊加）。
 *
 * 支援：
 *   - Measure()：Born rule 取樣並塌縮到單一基態
 *   - Diffuse(rate)：振幅向均勻分佈漂移（情報時效/退相干）
 *   - ApplyUnitary 之外的輔助：AmplitudeShift 做機率推移
 */
class Qudit {
public:
    using Amplitude = std::complex<double>;

    Qudit(int dimension, uint64_t seed = 0);
    // Q-5：注入隨機源（接管 Measure 擲骰）；nullptr 等價 seed=0
    Qudit(int dimension, std::unique_ptr<IRandomSource> src);

    int Dimension() const { return dim; }

    // 以機率分佈初始化（自動取平方根轉振幅，相位為 0）
    void SetProbabilities(const std::vector<double>& probs);
    // 均勻疊加
    void SetUniform();

    double Probability(int i) const;
    std::vector<double> Probabilities() const;
    const std::vector<Amplitude>& Amplitudes() const { return amplitudes; }

    // Born rule 測量：回傳基態 index 並塌縮
    int Measure();
    void CollapseTo(int i);

    // 退相干：以 rate∈[0,1] 向均勻分佈漂移（rate=1 完全均勻）
    void Diffuse(double rate);

    bool IsNormalized(double eps = 1e-9) const;

private:
    int dim;
    std::vector<Amplitude> amplitudes;
    std::mt19937_64 rng;
    std::unique_ptr<IRandomSource> source; // Q-5 注入源；空則用 rng

    double Roll(); // 注入源優先，否則內建 seeded RNG
};

} // namespace Quantum
} // namespace Potato
