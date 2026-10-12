// Story 13.1 — PotatoQuantum module baseline.
//   - gate truth tables (H·H=I, X=NOT, HZH=X, Ry(π)=X)
//   - CNOT/SWAP entanglement + Bell-state correlated measurement
//   - Measure irreversibility + MeasureAll bounds
//   - seeded + injected-IRandomSource determinism
//   - Qudit: probabilities, phases, measure, diffuse
//   - contract bounds (n<1/n>16 throw, qubit OOB throws)
//
// BOUNDARY: this test links PotatoQuantum directly — an engine
// leaf lib. PotatoGameplay must never link or include it
// (integer-determinism rule); the Campaign adapter (13.2) is
// the only sanctioned crossing point, returning ints.
#include "Quantum/QubitRegister.h"
#include "Quantum/Qudit.h"
#include "Quantum/RandomSource.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

int failures = 0;
void Check(bool cond, const char* name) {
    std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", name);
    if (!cond) ++failures;
}

using namespace Potato::Quantum;

constexpr double kEps = 1e-9;
bool Near(double a, double b) { return std::abs(a - b) < kEps; }

// Scripted source: returns a fixed stream of u64s, then 0s.
// NextDouble() takes the high 53 bits → a u64 of 0 yields 0.0,
// which forces every Born-rule roll to pick outcome "1"
// whenever P(1) > 0. A max u64 (~1.0) forces "0" whenever
// P(1) < 1.
struct ScriptedSource : IRandomSource {
    std::vector<std::uint64_t> stream;
    std::size_t i = 0;
    explicit ScriptedSource(std::vector<std::uint64_t> s)
        : stream(std::move(s)) {}
    std::uint64_t NextU64() override {
        return i < stream.size() ? stream[i++] : 0;
    }
};

} // namespace

int main() {
    // ===== Contract bounds =====
    {
        bool threw = false;
        try { QubitRegister bad0(0, 1); } catch (const std::invalid_argument&) { threw = true; }
        Check(threw, "contract: n=0 rejected");
        threw = false;
        try { QubitRegister bad17(17, 1); } catch (const std::invalid_argument&) { threw = true; }
        Check(threw, "contract: n>16 rejected");
        threw = false;
        try { QubitRegister r(2, 1); r.X(-1); } catch (const std::out_of_range&) { threw = true; }
        Check(threw, "contract: negative qubit index rejected");
        threw = false;
        try { QubitRegister r(2, 1); r.H(2); } catch (const std::out_of_range&) { threw = true; }
        Check(threw, "contract: qubit index >= n rejected");
        threw = false;
        try { QubitRegister r(2, 1); r.CNOT(0, 0); } catch (const std::invalid_argument&) { threw = true; }
        Check(threw, "contract: CNOT control==target rejected");
    }

    // ===== Single-qubit gate truth tables =====
    {
        QubitRegister r(1, 7);
        r.H(0); r.H(0);
        Check(Near(r.BasisProbability(0), 1.0), "gate: H·H = I on |0>");

        QubitRegister x(1, 7);
        x.X(0);
        Check(Near(x.Probability(0), 1.0), "gate: X flips |0> to |1>");

        QubitRegister h(1, 7);
        h.H(0);
        Check(Near(h.Probability(0), 0.5), "gate: H on |0> gives 50/50");

        // H·Z·H = X (phase kick flips the |+> to |->)
        QubitRegister hzh(1, 7);
        hzh.H(0); hzh.Z(0); hzh.H(0);
        Check(Near(hzh.Probability(0), 1.0), "gate: H·Z·H = X");

        // Ry(π) ≈ X: full population transfer
        QubitRegister ry(1, 7);
        ry.Ry(0, 3.14159265358979323846);
        Check(Near(ry.Probability(0), 1.0), "gate: Ry(pi) flips |0>");
    }

    // ===== Two-qubit gates =====
    {
        QubitRegister c(2, 11);
        c.X(0); c.CNOT(0, 1);
        Check(Near(c.Probability(1), 1.0), "CNOT: |10> -> |11>");

        QubitRegister c0(2, 11);
        c0.CNOT(0, 1); // control |0> — target untouched
        Check(Near(c0.Probability(1), 0.0), "CNOT: control 0 leaves target");

        QubitRegister s(2, 11);
        s.X(1);           // |10> (bit1 set)
        s.SWAP(0, 1);     // -> |01> (bit0 set)
        Check(Near(s.Probability(0), 1.0) && Near(s.Probability(1), 0.0),
              "SWAP moves the excitation");
    }

    // ===== Bell state: entangled measurement =====
    {
        bool saw0 = false, saw1 = false, correlated = true;
        for (std::uint64_t seed = 1; seed <= 64; ++seed) {
            QubitRegister b(2, seed);
            b.H(0); b.CNOT(0, 1);
            const bool m0 = b.Measure(0);
            const bool m1 = b.Measure(1);
            if (m0 != m1) correlated = false;
            (m0 ? saw1 : saw0) = true;
        }
        Check(correlated, "bell: Measure(0) and Measure(1) always agree");
        Check(saw0 && saw1, "bell: both outcomes occur across seeds");
    }

    // ===== Measure is irreversible =====
    {
        QubitRegister m(1, 3);
        m.H(0);
        const bool first = m.Measure(0);
        bool stable = true;
        for (int i = 0; i < 8; ++i) stable &= (m.Measure(0) == first);
        Check(stable, "measure: collapse persists on re-measure");
        Check(Near(m.Probability(0), first ? 1.0 : 0.0),
              "measure: post-collapse probability is 0/1");
    }

    // ===== MeasureAll bounds =====
    {
        QubitRegister a(3, 5);
        a.H(0); a.H(1); a.H(2);
        const std::uint64_t v = a.MeasureAll();
        Check(v < (1ull << 3), "MeasureAll: result within 2^n");
        Check(Near(a.BasisProbability(v), 1.0),
              "MeasureAll: collapses to the returned basis");
    }

    // ===== Determinism: same seed -> identical stream =====
    {
        QubitRegister r1(3, 42), r2(3, 42);
        bool identical = true;
        for (int i = 0; i < 32; ++i) {
            r1.H(i % 3); r2.H(i % 3);
            const bool b1 = r1.Measure(i % 3);
            const bool b2 = r2.Measure(i % 3);
            if (b1 != b2) identical = false;
        }
        Check(identical, "seed: identical seeds give identical streams");
    }

    // ===== Injected IRandomSource drives collapse =====
    {
        // zero-stream source -> NextDouble 0 -> roll < p1 -> "1"
        auto zero = std::make_unique<ScriptedSource>(
            std::vector<std::uint64_t>{0});
        QubitRegister z(1, std::move(zero));
        z.H(0);
        Check(z.Measure(0) == true, "inject: roll 0 picks outcome 1");

        auto ones = std::make_unique<ScriptedSource>(
            std::vector<std::uint64_t>{~0ull});
        QubitRegister o(1, std::move(ones));
        o.H(0);
        Check(o.Measure(0) == false, "inject: roll ~1 picks outcome 0");
    }

    // ===== QuantumBitSource =====
    {
        QuantumBitSource q1(99), q2(99);
        bool identical = true;
        for (int i = 0; i < 16; ++i) {
            if (q1.NextBit() != q2.NextBit()) identical = false;
        }
        Check(identical, "bitsource: same seed -> same bit stream");

        QuantumBitSource qi(7);
        bool inRange = true;
        for (int i = 0; i < 64; ++i) {
            const int v = qi.NextInt(5);
            if (v < 0 || v >= 5) inRange = false;
        }
        Check(inRange, "bitsource: NextInt stays in range");
    }

    // ===== Qudit =====
    {
        Qudit d(5, 21);
        Check(d.Dimension() == 5 && d.IsNormalized(),
              "qudit: uniform init normalized");
        for (int i = 0; i < 5; ++i) {
            Check(Near(d.Probability(i), 0.2), "qudit: uniform 1/d");
        }

        d.SetProbabilities({0.0, 0.0, 1.0, 0.0, 0.0});
        Check(Near(d.Probability(2), 1.0), "qudit: SetProbabilities");

        Check(d.SetPhases({0.0, 0.5, 1.0, 1.5, 2.0}) &&
              !d.SetPhases({1.0, 2.0}),
              "qudit: SetPhases arity gate");

        const int m = d.Measure();
        Check(m >= 0 && m < 5, "qudit: Measure returns valid level");
        Check(Near(d.Probability(m), 1.0), "qudit: Measure collapses");

        d.CollapseTo(0);
        d.Diffuse(1.0);
        Check(Near(d.Probability(4), 0.2), "qudit: Diffuse(1) uniform");

        bool threw = false;
        try { Qudit bad(1, 1); } catch (const std::invalid_argument&) { threw = true; }
        Check(threw, "qudit: dimension<2 rejected");
    }

    // ===== Born-rule statistics (sanity, loose bounds) =====
    {
        int ones = 0;
        const int trials = 400;
        for (int i = 0; i < trials; ++i) {
            QubitRegister r(1, 1000 + i);
            r.H(0);
            ones += r.Measure(0) ? 1 : 0;
        }
        // 400 fair flips: P(outside [140,260]) is astronomically small
        Check(ones > 140 && ones < 260,
              "bornrule: H+Measure ~50/50 over 400 trials");
    }

    if (failures == 0) {
        std::printf("QUANTUM TESTS PASS\n");
        return 0;
    }
    std::printf("%d FAILURES\n", failures);
    return 1;
}
