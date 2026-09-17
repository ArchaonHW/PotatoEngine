#include "QubitRegister.h"

#include <cmath>
#include <sstream>
#include <stdexcept>

namespace Potato {
namespace Quantum {

QubitRegister::QubitRegister(int n, uint64_t seed)
    : numQubits(n), rng(seed ? seed : std::random_device{}()) {
    if (n < 1 || n > 16) {
        throw std::invalid_argument("QubitRegister: qubits 需介於 1~16");
    }
    amplitudes.assign(size_t{1} << n, Amplitude{0.0, 0.0});
    amplitudes[0] = Amplitude{1.0, 0.0};
}

QubitRegister::QubitRegister(int n, std::unique_ptr<IRandomSource> src)
    : QubitRegister(n, 0) {
    source = std::move(src);
}

double QubitRegister::Roll() {
    if (source) {
        return source->NextDouble();
    }
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(rng);
}

void QubitRegister::CheckQubit(int q) const {
    if (q < 0 || q >= numQubits) {
        throw std::out_of_range("QubitRegister: qubit index 越界");
    }
}

void QubitRegister::ApplySingle(int q, Amplitude a00, Amplitude a01,
                                Amplitude a10, Amplitude a11) {
    CheckQubit(q);
    const size_t bit = size_t{1} << q;
    for (size_t i = 0; i < amplitudes.size(); ++i) {
        if ((i & bit) == 0) {
            const size_t j = i | bit;
            const Amplitude v0 = amplitudes[i];
            const Amplitude v1 = amplitudes[j];
            amplitudes[i] = a00 * v0 + a01 * v1;
            amplitudes[j] = a10 * v0 + a11 * v1;
        }
    }
}

void QubitRegister::H(int q) {
    const double s = 1.0 / std::sqrt(2.0);
    ApplySingle(q, s, s, s, -s);
}

void QubitRegister::X(int q) {
    ApplySingle(q, 0.0, 1.0, 1.0, 0.0);
}

void QubitRegister::Y(int q) {
    ApplySingle(q, 0.0, Amplitude{0.0, -1.0}, Amplitude{0.0, 1.0}, 0.0);
}

void QubitRegister::Z(int q) {
    ApplySingle(q, 1.0, 0.0, 0.0, -1.0);
}

void QubitRegister::Phase(int q, double theta) {
    ApplySingle(q, 1.0, 0.0, 0.0, std::exp(Amplitude{0.0, theta}));
}

void QubitRegister::Ry(int q, double theta) {
    const double c = std::cos(theta * 0.5);
    const double s = std::sin(theta * 0.5);
    ApplySingle(q, c, -s, s, c);
}

void QubitRegister::CNOT(int control, int target) {
    CheckQubit(control);
    CheckQubit(target);
    if (control == target) {
        throw std::invalid_argument("QubitRegister::CNOT: control == target");
    }
    const size_t cbit = size_t{1} << control;
    const size_t tbit = size_t{1} << target;
    for (size_t i = 0; i < amplitudes.size(); ++i) {
        if ((i & cbit) != 0 && (i & tbit) == 0) {
            std::swap(amplitudes[i], amplitudes[i | tbit]);
        }
    }
}

void QubitRegister::SWAP(int a, int b) {
    CheckQubit(a);
    CheckQubit(b);
    if (a == b) return;
    const size_t abit = size_t{1} << a;
    const size_t bbit = size_t{1} << b;
    for (size_t i = 0; i < amplitudes.size(); ++i) {
        if ((i & abit) == 0 && (i & bbit) != 0) {
            std::swap(amplitudes[i], amplitudes[(i | abit) & ~bbit]);
        }
    }
}

double QubitRegister::Probability(int q) const {
    CheckQubit(q);
    const size_t bit = size_t{1} << q;
    double p = 0.0;
    for (size_t i = 0; i < amplitudes.size(); ++i) {
        if (i & bit) {
            p += std::norm(amplitudes[i]);
        }
    }
    return p;
}

double QubitRegister::BasisProbability(size_t index) const {
    if (index >= amplitudes.size()) {
        throw std::out_of_range("QubitRegister: basis index 越界");
    }
    return std::norm(amplitudes[index]);
}

bool QubitRegister::Measure(int q) {
    const double p1 = Probability(q);
    const bool result = Roll() < p1;

    const size_t bit = size_t{1} << q;
    double kept = 0.0;
    for (size_t i = 0; i < amplitudes.size(); ++i) {
        if (static_cast<bool>(i & bit) != result) {
            amplitudes[i] = Amplitude{0.0, 0.0};
        } else {
            kept += std::norm(amplitudes[i]);
        }
    }
    if (kept > 0.0) {
        const double inv = 1.0 / std::sqrt(kept);
        for (auto& a : amplitudes) a *= inv;
    }
    return result;
}

uint64_t QubitRegister::MeasureAll() {
    const double roll = Roll();
    double acc = 0.0;
    size_t outcome = amplitudes.size() - 1;
    for (size_t i = 0; i < amplitudes.size(); ++i) {
        acc += std::norm(amplitudes[i]);
        if (roll < acc) { outcome = i; break; }
    }
    CollapseTo(outcome);
    return static_cast<uint64_t>(outcome);
}

void QubitRegister::CollapseTo(size_t basisIndex) {
    if (basisIndex >= amplitudes.size()) {
        throw std::out_of_range("QubitRegister: basis index 越界");
    }
    for (auto& a : amplitudes) a = Amplitude{0.0, 0.0};
    amplitudes[basisIndex] = Amplitude{1.0, 0.0};
}

bool QubitRegister::IsNormalized(double eps) const {
    double total = 0.0;
    for (const auto& a : amplitudes) total += std::norm(a);
    return std::abs(total - 1.0) < eps;
}

std::string QubitRegister::ToString() const {
    std::ostringstream oss;
    bool first = true;
    for (size_t i = 0; i < amplitudes.size(); ++i) {
        const double mag = std::abs(amplitudes[i]);
        if (mag < 1e-6) continue;
        if (!first) oss << " + ";
        char buf[64];
        snprintf(buf, sizeof(buf), "%.3f|", mag);
        oss << buf;
        for (int q = numQubits - 1; q >= 0; --q) {
            oss << ((i >> q) & 1);
        }
        oss << ">";
        first = false;
    }
    return oss.str();
}

// ---------------------------------------------------------------------------

QuantumBitSource::QuantumBitSource(uint64_t seed)
    : rng(seed ? seed : std::random_device{}()) {
}

QuantumBitSource::QuantumBitSource(std::unique_ptr<IRandomSource> src)
    : rng(0), source(std::move(src)) {
}

double QuantumBitSource::Roll() {
    if (source) {
        return source->NextDouble();
    }
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(rng);
}

bool QuantumBitSource::NextBit() {
    // 態向量 |0> → H → Measure 的封閉結果即 50/50；
    // 直接以均勻分佈實現（數學等價，免去每次建暫存器）
    return Roll() < 0.5;
}

int QuantumBitSource::NextInt(int maxExclusive) {
    if (maxExclusive <= 0) return 0;
    // reject sampling：丟棄超出 2^k 對齊邊界的值，避免 modulo bias
    int bits = 0;
    while ((1 << bits) < maxExclusive) ++bits;
    const int limit = (1 << bits) - ((1 << bits) % maxExclusive);
    for (;;) {
        int v = 0;
        for (int b = 0; b < bits; ++b) {
            v = (v << 1) | (NextBit() ? 1 : 0);
        }
        if (v < limit) return v % maxExclusive;
    }
}

double QuantumBitSource::NextDouble() {
    return Roll();
}

} // namespace Quantum
} // namespace Potato
