#pragma once

#include <cstdint>
#include <memory>
#include <random>

namespace Potato {
namespace Quantum {

/**
 * 均勻隨機源縫（Q-5）
 *
 * 測量/位元產生的擲骰後端可注入：
 *   - SeededRandomSource：mt19937_64，同 seed 逐位一致（回放可重現）
 *   - EntropyRandomSource：OS entropy（std::random_device）
 *   - 外部真隨機源（如 HTTP QRNG）實作本介面即可注入——
 *     本專案不附網路依賴，僅留縫。
 *
 * 注入方式：QubitRegister / Qudit / QuantumBitSource 建構子接受
 * std::unique_ptr<IRandomSource>；nullptr 退回各自內建 seeded RNG。
 */
class IRandomSource {
public:
    virtual ~IRandomSource() = default;
    virtual uint64_t NextU64() = 0;

    // [0,1) 均勻——取高 53 位（IEEE-754 mantissa 寬度）
    double NextDouble() {
        return static_cast<double>(NextU64() >> 11) *
               (1.0 / 9007199254740992.0);
    }
};

// 預設後端：seeded mt19937_64（同 seed 序列逐位一致）
class SeededRandomSource : public IRandomSource {
public:
    explicit SeededRandomSource(uint64_t seed) : rng(seed) {}
    uint64_t NextU64() override { return rng(); }

private:
    std::mt19937_64 rng;
};

// OS entropy 後端：random_device 只有 unsigned int，拼兩段成 64-bit
class EntropyRandomSource : public IRandomSource {
public:
    uint64_t NextU64() override {
        return (static_cast<uint64_t>(rd()) << 32) |
               static_cast<uint64_t>(rd());
    }

private:
    std::random_device rd;
};

} // namespace Quantum
} // namespace Potato
