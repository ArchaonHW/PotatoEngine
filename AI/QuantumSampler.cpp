#include "AI/QuantumSampler.h"

#include "Quantum/QubitRegister.h"
#include "Quantum/Qudit.h"

#include <cmath>

namespace Potato {
namespace AI {

// Owned by the fresh circuit but forwards every roll to the
// sampler's counted stream — nested class, sees Roll64().
class QuantumSampler::ForwardSource final : public Quantum::IRandomSource {
public:
    explicit ForwardSource(QuantumSampler* owner) : owner_(owner) {}
    std::uint64_t NextU64() override { return owner_->Roll64(); }
private:
    QuantumSampler* owner_;
};

QuantumSampler::QuantumSampler(std::uint64_t seed, std::uint64_t skip)
    : owned_(std::make_unique<Quantum::SeededRandomSource>(seed)),
      src_(owned_.get()) {
    while (skip--) { Roll64(); }
}

QuantumSampler::QuantumSampler(std::unique_ptr<Quantum::IRandomSource> src)
    : owned_(src ? std::move(src)
                 : std::unique_ptr<Quantum::IRandomSource>(
                       std::make_unique<Quantum::SeededRandomSource>(0))),
      src_(owned_.get()) {
}

QuantumSampler::~QuantumSampler() = default;
QuantumSampler::QuantumSampler(QuantumSampler&&) noexcept = default;
QuantumSampler& QuantumSampler::operator=(QuantumSampler&&) noexcept = default;

std::unique_ptr<Quantum::IRandomSource> QuantumSampler::MakeForwarder() {
    return std::make_unique<ForwardSource>(this);
}

std::uint64_t QuantumSampler::Roll64() {
    ++draws_;
    return src_->NextU64();
}

double QuantumSampler::DrawDouble() {
    // Same high-53-bit mapping as IRandomSource::NextDouble(), but
    // routed through Roll64() so consumption is counted.
    return static_cast<double>(Roll64() >> 11) *
           (1.0 / 9007199254740992.0);
}

bool QuantumSampler::DrawBool(double p) {
    if (p <= 0.0) return false;  // no stream burn
    if (p >= 1.0) return true;   // no stream burn
    Quantum::QubitRegister r(1, MakeForwarder());
    // P(1) = sin²(θ/2) under Ry — θ = 2·asin(√p) hits p exactly.
    r.Ry(0, 2.0 * std::asin(std::sqrt(p)));
    return r.Measure(0);
}

int QuantumSampler::DrawWeighted(const float* weights, int count) {
    if (!weights || count <= 0) return -1;
    int nonzero = 0;
    int sole = -1;
    for (int i = 0; i < count; ++i) {
        if (weights[i] > 0.0f) { ++nonzero; sole = i; }
    }
    if (nonzero == 0) return -1;   // empty question — no answer
    if (nonzero == 1) return sole; // forced — no stream burn
    // Qudit self-normalizes — feed raw positive mass, not a
    // pre-normalized distribution.
    std::vector<double> probs(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        probs[static_cast<std::size_t>(i)] =
            weights[i] > 0.0f ? static_cast<double>(weights[i]) : 0.0;
    }
    Quantum::Qudit d(count, MakeForwarder());
    d.SetProbabilities(probs);
    return d.Measure();
}

int QuantumSampler::DrawAmplified(const float* weights, int count,
                                  int marked, int rounds) {
    if (!weights || count <= 0) return -1;
    // Marking an unreachable or out-of-range index degrades to a
    // plain weighted draw — the categorical contract holds.
    if (marked < 0 || marked >= count || weights[marked] <= 0.0f ||
        rounds <= 0) {
        return DrawWeighted(weights, count);
    }
    int nonzero = 0;
    int sole = -1;
    for (int i = 0; i < count; ++i) {
        if (weights[i] > 0.0f) { ++nonzero; sole = i; }
    }
    if (nonzero == 0) return -1;
    if (nonzero == 1) return sole;

    std::vector<double> probs(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        probs[static_cast<std::size_t>(i)] =
            weights[i] > 0.0f ? static_cast<double>(weights[i]) : 0.0;
    }
    Quantum::Qudit d(count, MakeForwarder());
    d.SetProbabilities(probs);

    // Grover iteration on the real-amplitude line:
    //   oracle  — flip the marked state's phase (a_marked *= -1)
    //   diffuse — reflect every amplitude about the mean
    //             (a_i → 2·mean(a) - a_i), the unitary D = 2|u><u| - I.
    // Both steps are unitary, so normalization is preserved; the
    // renormalization below only sweeps FP drift.
    std::vector<Quantum::Qudit::Amplitude> amps = d.Amplitudes();
    for (int r = 0; r < rounds; ++r) {
        amps[static_cast<std::size_t>(marked)] = -amps[static_cast<std::size_t>(marked)];
        double mean = 0.0;
        for (const auto& a : amps) mean += a.real();
        mean /= static_cast<double>(count);
        for (auto& a : amps) a = Quantum::Qudit::Amplitude{2.0 * mean - a.real(), 0.0};
    }
    // Zero out unreachable entries: diffusion can leak mass into
    // zero-weight states, but the categorical contract says they
    // never answer. Mask + renormalize before the collapse.
    double norm = 0.0;
    for (int i = 0; i < count; ++i) {
        if (weights[i] <= 0.0f) amps[static_cast<std::size_t>(i)] = {0.0, 0.0};
        norm += std::norm(amps[static_cast<std::size_t>(i)]);
    }
    if (norm <= 0.0) return DrawWeighted(weights, count); // FP collapse guard
    const double inv = 1.0 / std::sqrt(norm);
    for (auto& a : amps) a *= inv;
    d.SetAmplitudes(amps);
    return d.Measure();
}

std::pair<bool, bool> QuantumSampler::DrawCorrelated() {
    // |Φ⁺⟩ = (|00⟩+|11⟩)/√2 — one collapse fixes both bits.
    Quantum::QubitRegister r(2, MakeForwarder());
    r.H(0);
    r.CNOT(0, 1);
    const std::uint64_t v = r.MeasureAll();
    return {(v & 1) != 0, (v & 2) != 0};
}

std::pair<bool, bool> QuantumSampler::DrawAntiCorrelated() {
    // |Ψ⁺⟩ = (|01⟩+|10⟩)/√2 — X(1) before the entangling pair.
    Quantum::QubitRegister r(2, MakeForwarder());
    r.X(1);
    r.H(0);
    r.CNOT(0, 1);
    const std::uint64_t v = r.MeasureAll();
    return {(v & 1) != 0, (v & 2) != 0};
}

int QuantumSampler::DrawInt(int n) {
    if (n <= 1) return 0;  // forced — no stream burn
    int bits = 0;
    while ((1 << bits) < n) ++bits;         // n <= 2^bits
    const std::uint64_t limit =
        (std::uint64_t{1} << bits) - ((std::uint64_t{1} << bits) % n);
    for (;;) {
        Quantum::QubitRegister r(bits, MakeForwarder());
        for (int q = 0; q < bits; ++q) r.H(q);
        const std::uint64_t v = r.MeasureAll();
        if (v < limit) return static_cast<int>(v % n);
    }
}

} // namespace AI
} // namespace Potato
