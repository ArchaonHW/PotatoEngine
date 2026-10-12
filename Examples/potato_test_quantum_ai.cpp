// Story 13.3 — QuantumSampler + RL agent quantum-sampling opt-in.
//   - DrawBool/DrawWeighted/DrawInt determinism (seed ⇒ identical streams)
//   - resume via Draws() skip (bit-exact stream continuation)
//   - degenerate draws answer without consuming the stream
//   - fairness bounds over seeded draws
//   - agent wiring: QLearning / PolicyGradient identical sequences
//     under the same quantum seed; default path untouched
//
// Links ReinforcementLearning (engine leaf) — never PotatoGameplay.
#include "ReinforcementLearning.h"
#include "QuantumSampler.h"
#include "Quantum/RandomSource.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

namespace {

int failures = 0;
void Check(bool cond, const char* name) {
    std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", name);
    if (!cond) ++failures;
}

using namespace Potato::AI;

} // namespace

int main() {
    // ===== Determinism: same seed ⇒ identical stream =====
    {
        QuantumSampler a(42), b(42);
        bool same = true;
        for (int i = 0; i < 200; ++i) {
            if (a.DrawBool(0.37) != b.DrawBool(0.37)) same = false;
            const float w[4] = {0.1f, 0.2f, 0.3f, 0.4f};
            if (a.DrawWeighted(w, 4) != b.DrawWeighted(w, 4)) same = false;
            if (a.DrawInt(7) != b.DrawInt(7)) same = false;
        }
        Check(same, "determinism: seed 42 streams identical (600 draws)");
        Check(a.Draws() == b.Draws(), "determinism: stream positions match");
    }

    // ===== Resume: skip replayed units, continue bit-exactly =====
    {
        QuantumSampler a(7);
        for (int i = 0; i < 25; ++i) a.DrawBool(0.5);
        const std::uint64_t pos = a.Draws();
        QuantumSampler b(7, pos);
        bool same = true;
        for (int i = 0; i < 50; ++i) {
            if (a.DrawInt(10) != b.DrawInt(10)) same = false;
        }
        Check(same, "resume: skipped stream continues bit-exactly");
    }

    // ===== Degenerate draws consume nothing =====
    {
        QuantumSampler s(1);
        const std::uint64_t p0 = s.Draws();
        Check(!s.DrawBool(0.0), "degenerate: DrawBool(0) = false");
        Check(s.DrawBool(1.0), "degenerate: DrawBool(1) = true");
        const float zeros[3] = {0.0f, 0.0f, 0.0f};
        Check(s.DrawWeighted(zeros, 3) == -1, "degenerate: all-zero weights = -1");
        const float lone[4] = {0.0f, 0.0f, 5.0f, 0.0f};
        Check(s.DrawWeighted(lone, 4) == 2, "degenerate: single weight = its index");
        Check(s.DrawInt(1) == 0 && s.DrawInt(0) == 0, "degenerate: DrawInt<=1 = 0");
        Check(s.Draws() == p0, "degenerate: zero stream consumption");
    }

    // ===== Fairness bounds (seeded, astronomically safe windows) =====
    {
        QuantumSampler s(99);
        int heads = 0;
        for (int i = 0; i < 400; ++i) heads += s.DrawBool(0.5) ? 1 : 0;
        Check(heads > 140 && heads < 260, "fairness: DrawBool(0.5) ~50/50");

        QuantumSampler s2(123);
        int counts[4] = {0, 0, 0, 0};
        const float w[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        for (int i = 0; i < 800; ++i) {
            int c = s2.DrawWeighted(w, 4);
            if (c >= 0 && c < 4) ++counts[c];
        }
        Check(counts[0] > 140 && counts[0] < 260 &&
              counts[1] > 140 && counts[1] < 260 &&
              counts[2] > 140 && counts[2] < 260 &&
              counts[3] > 140 && counts[3] < 260,
              "fairness: DrawWeighted uniform over 4 buckets");

        // Skewed weights favor the heavy bucket
        QuantumSampler s3(321);
        int heavy = 0;
        const float hw[2] = {9.0f, 1.0f};
        for (int i = 0; i < 200; ++i) {
            if (s3.DrawWeighted(hw, 2) == 0) ++heavy;
        }
        Check(heavy > 150 && heavy < 200, "fairness: DrawWeighted favors heavy bucket");
    }

    // ===== Agent wiring: QLearning ε-greedy =====
    {
        QLearningAgent a(4, 4), b(4, 4);
        a.SetExplorationRate(1.0f);  // always take the quantum-explore path
        b.SetExplorationRate(1.0f);
        a.EnableQuantumSampling(2024);
        b.EnableQuantumSampling(2024);
        Check(a.HasQuantumSampling() && b.HasQuantumSampling(),
              "agent: EnableQuantumSampling engages");

        bool same = true;
        for (int i = 0; i < 100; ++i) {
            if (a.SelectAction(0, true) != b.SelectAction(0, true)) same = false;
        }
        Check(same, "agent: QL quantum exploration identical across seeds");

        // Different seed ⇒ stream position differs (overwhelmingly
        // likely to diverge somewhere in 100 draws)
        QLearningAgent c(4, 4);
        c.SetExplorationRate(1.0f);
        c.EnableQuantumSampling(999);
        a.DisableQuantumSampling();  // falls back to classical path
        Check(!a.HasQuantumSampling(), "agent: DisableQuantumSampling restores default");
        bool diverged = false;
        for (int i = 0; i < 100; ++i) {
            if (b.SelectAction(0, true) != c.SelectAction(0, true)) diverged = true;
        }
        Check(diverged, "agent: different seed ⇒ different draws");
    }

    // ===== Agent wiring: PolicyGradient categorical =====
    {
        PolicyGradientAgent p1(3, 5), p2(3, 5);
        // Sync weights — ctor Build() is OS-entropy seeded
        p2.Deserialize(p1.Serialize());
        p1.EnableQuantumSampling(555);
        p2.EnableQuantumSampling(555);

        State s{{0.2f, 0.5f, 0.8f}};
        bool same = true, valid = true;
        for (int i = 0; i < 60; ++i) {
            Action a = p1.SelectAction(s, true);
            Action b = p2.SelectAction(s, true);
            if (a.id != b.id) same = false;
            if (a.id < 0 || a.id >= 5) valid = false;
        }
        Check(same, "agent: PG quantum policy draws identical across seeds");
        Check(valid, "agent: PG quantum draws stay in action range");

        // explore=false argmax path must not consult the sampler
        Action g1 = p1.SelectAction(s, false);
        Action g2 = p2.SelectAction(s, false);
        Check(g1.id == g2.id, "agent: PG argmax path deterministic");
    }

    // ===== MultiArmedBandit wiring =====
    {
        MultiArmedBandit m1(6), m2(6);
        m1.EnableQuantumSampling(77);
        m2.EnableQuantumSampling(77);
        bool same = true, inRange = true;
        for (int i = 0; i < 80; ++i) {
            int a = m1.SelectArm(), b = m2.SelectArm();
            if (a != b) same = false;
            if (a < 0 || a >= 6) inRange = false;
        }
        Check(same, "agent: MAB quantum draws identical across seeds");
        Check(inRange, "agent: MAB draws stay in arm range");
    }

    // ===== Story 13.4: amplitude amplification =====
    {
        const float w[4] = {0.1f, 0.3f, 0.3f, 0.3f};

        // Determinism: same seed ⇒ identical amplified streams
        QuantumSampler a(42), b(42);
        bool same = true;
        for (int i = 0; i < 150; ++i) {
            if (a.DrawAmplified(w, 4, 0, 2) != b.DrawAmplified(w, 4, 0, 2))
                same = false;
        }
        Check(same, "amplify: same seed ⇒ identical amplified streams");

        // Grover dynamics: each round rotates by 2·asin(1/√N) = 60°
        // for N=4; marked phase starts at asin(√0.1) ≈ 18.4°, so
        // P(r) = sin²(18.4° + 60°r) ⇒ r=0→0.10  r=1→0.96  r=2→0.44  r=3→0.10
        QuantumSampler s(64);
        int hit[4] = {0, 0, 0, 0};
        for (int r = 0; r < 4; ++r) {
            for (int i = 0; i < 400; ++i) {
                if (s.DrawAmplified(w, 4, 0, r) == 0) ++hit[r];
            }
        }
        Check(hit[0] > 20 && hit[0] < 70, "amplify: rounds=0 ≈ Born-rule share");
        Check(hit[1] > 340, "amplify: rounds=1 saturates marked (≈0.96)");
        Check(hit[2] > 140 && hit[2] < 220, "amplify: rounds=2 overshoots (≈0.44)");
        Check(hit[3] > 20 && hit[3] < 70, "amplify: rounds=3 wraps back (≈0.10)");
        Check(hit[1] > hit[2] && hit[2] > hit[3],
              "amplify: shares follow Grover rotation ordering");
        Check(s.Draws() == 1600, "amplify: exactly one stream unit per draw");

        // Zero-weight entries stay unreachable even under amplification
        QuantumSampler z(11);
        const float zw[3] = {0.0f, 0.5f, 0.5f};
        int zeros = 0;
        for (int i = 0; i < 300; ++i) {
            if (z.DrawAmplified(zw, 3, 1, 3) == 0) ++zeros;
        }
        Check(zeros == 0, "amplify: nonpositive entries stay unreachable");

        // Degenerate marked degrades to a plain weighted draw
        QuantumSampler g(5);
        const float gw[3] = {0.2f, 0.3f, 0.5f};
        const std::uint64_t before = g.Draws();
        int c = g.DrawAmplified(gw, 3, 9, 4);  // marked out of range
        Check(c >= 0 && c < 3 && g.Draws() == before + 1,
              "amplify: bad marked ⇒ plain weighted draw (1 unit)");
        const float lw[3] = {0.0f, 7.0f, 0.0f};
        Check(g.DrawAmplified(lw, 3, 1, 5) == 1 && g.Draws() == before + 1,
              "amplify: single weight answers without consuming");
    }

    // ===== Agent wiring: amplification on policy agents =====
    {
        PolicyGradientAgent p1(3, 5), p2(3, 5);
        p2.Deserialize(p1.Serialize());
        p1.EnableQuantumSampling(888); p1.SetQuantumAmplification(2);
        p2.EnableQuantumSampling(888); p2.SetQuantumAmplification(2);
        State s{{0.2f, 0.5f, 0.8f}};
        bool same = true, valid = true;
        for (int i = 0; i < 60; ++i) {
            Action aa = p1.SelectAction(s, true);
            Action bb = p2.SelectAction(s, true);
            if (aa.id != bb.id) same = false;
            if (aa.id < 0 || aa.id >= 5) valid = false;
        }
        Check(same, "agent: amplified policy draws identical across seeds");
        Check(valid, "agent: amplified draws stay in action range");
    }

    // ===== Story 13.5: entangled pair draws =====
    {
        // Determinism: same seed ⇒ identical pair streams
        QuantumSampler a(7), b(7);
        bool same = true;
        for (int i = 0; i < 100; ++i) {
            if (a.DrawCorrelated() != b.DrawCorrelated()) same = false;
            if (a.DrawAntiCorrelated() != b.DrawAntiCorrelated()) same = false;
        }
        Check(same, "bell: same seed ⇒ identical entangled streams");

        // Correlation invariants: Φ⁺ always equal bits, Ψ⁺ always opposite
        QuantumSampler s(31);
        int equalC = 0, equalA = 0, ones = 0;
        for (int i = 0; i < 300; ++i) {
            auto c = s.DrawCorrelated();
            auto d = s.DrawAntiCorrelated();
            if (c.first == c.second) ++equalC;
            if (d.first != d.second) ++equalA;
            ones += (c.first ? 1 : 0) + (d.first ? 1 : 0);
        }
        Check(equalC == 300, "bell: DrawCorrelated pairs always agree");
        Check(equalA == 300, "bell: DrawAntiCorrelated pairs always oppose");
        Check(ones > 220 && ones < 380, "bell: each bit marginally ~50/50");
        Check(s.Draws() == 600, "bell: one stream unit per entangled pair");
    }

    if (failures == 0) {
        std::printf("QUANTUM AI TESTS PASS\n");
        return 0;
    }
    std::printf("%d FAILURES\n", failures);
    return 1;
}
