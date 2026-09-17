#pragma once

#include "RandomSource.h"

#include <complex>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace Potato {
namespace Quantum {

/**
 * 量子位元暫存器（態向量模擬器）
 *
 * 以精確線性代數模擬 n 個量子位元：狀態為 2^n 個複數振幅，
 * 閘操作為么正變換，測量依 Born rule 塌縮。
 * 這是「真量子計算」的經典模擬——數學上與真 QPU 等價，
 * 僅在規模上受限（n 位元需 2^n 記憶體，遊戲用途建議 n ≤ 8）。
 *
 * 用途：戰場不確定性建模（疊加態敵情）、真隨機性來源、
 *       干涉效應驅動的組合機制。
 */
class QubitRegister {
public:
    using Amplitude = std::complex<double>;

    // 建立 n 位元暫存器，初始為 |0...0>；seed 供可重現測量
    explicit QubitRegister(int numQubits, uint64_t seed = 0);
    // Q-5：注入隨機源（接管測量擲骰）；nullptr 等價 seed=0
    QubitRegister(int numQubits, std::unique_ptr<IRandomSource> src);

    int NumQubits() const { return numQubits; }
    size_t StateCount() const { return amplitudes.size(); }

    // ---- 單位元閘 ----
    void H(int q);        // Hadamard：製造均勻疊加
    void X(int q);        // Pauli-X（NOT）
    void Y(int q);        // Pauli-Y
    void Z(int q);        // Pauli-Z（相位翻轉）
    void Phase(int q, double theta);  // 相位旋轉 e^{iθ}
    void Ry(int q, double theta);     // Y 軸旋轉（振幅重新分配）

    // ---- 雙位元閘 ----
    void CNOT(int control, int target);  // 受控 NOT（產生糾纏）
    void SWAP(int a, int b);

    // ---- 觀測 ----
    // 位元 q 為 |1> 的機率（Born rule：所有 q=1 基態的 |amp|^2 總和）
    double Probability(int q) const;
    // 基態 index 的機率
    double BasisProbability(size_t index) const;
    const std::vector<Amplitude>& Amplitudes() const { return amplitudes; }

    // ---- 測量（塌縮，不可逆）----
    bool Measure(int q);            // 測單一位元，回傳 0/1，態塌縮
    uint64_t MeasureAll();          // 測全部，回傳位元串
    void CollapseTo(size_t basisIndex);  // 直接塌縮到指定基態（同步外部真相）

    bool IsNormalized(double eps = 1e-9) const;
    std::string ToString() const;   // 形如 "0.71|00> + 0.71|11>"

private:
    int numQubits;
    std::vector<Amplitude> amplitudes;
    std::mt19937_64 rng;
    std::unique_ptr<IRandomSource> source; // Q-5 注入源；空則用 rng

    // 測量擲骰：注入源優先，否則內建 seeded RNG
    double Roll();

    void CheckQubit(int q) const;
    // 對所有滿足 mask 條件的基態套用 2x2 么正矩陣
    void ApplySingle(int q, Amplitude a00, Amplitude a01,
                     Amplitude a10, Amplitude a11);
    void Renormalize();
};

/**
 * 量子真隨機位元產生器：H + Measure 的薄封裝。
 * 產生的是（模擬的）量子隨機性——均勻分佈、不可由種子序列預測之外的偏差。
 */
class QuantumBitSource {
public:
    explicit QuantumBitSource(uint64_t seed = 0);
    // Q-5：注入隨機源（如 EntropyRandomSource）；接管所有擲骰
    explicit QuantumBitSource(std::unique_ptr<IRandomSource> src);
    bool NextBit();                 // 每次 |0>→H→Measure，獨立公平
    int NextInt(int maxExclusive);  // 均勻整數（reject sampling 免 modulo bias）
    double NextDouble();            // [0,1) 均勻

private:
    double Roll();                  // 注入源優先，否則內建 rng
    std::mt19937_64 rng;  // 模擬器測量擲骰用（量子部分仍是 Born rule）
    std::unique_ptr<IRandomSource> source;
};

} // namespace Quantum
} // namespace Potato
