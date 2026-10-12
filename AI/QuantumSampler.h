/**
 * QuantumSampler — Born-rule decision sampling for AI agents (Story 13.3)
 *
 * Replaces std::*_distribution draws with actual quantum-circuit
 * measurements from the engine's Quantum module:
 *   - DrawBool     → |0> → Ry(2·asin(√p)) → Measure      (exact Bernoulli)
 *   - DrawWeighted → Qudit SetProbabilities → Measure    (exact categorical)
 *   - DrawInt      → H^⊗k → MeasureAll + reject-sampling (uniform, no bias)
 *
 * DETERMINISM: an injected or seed-backed IRandomSource is the only
 * randomness channel — same seed + same call order gives bit-identical
 * draws. Draws() reports the consumed stream units;
 * QuantumSampler(seed, skips) replays them for save/restore.
 *
 * Degenerate questions (p∈{0,1}, single reachable outcome, n<=1)
 * answer WITHOUT consuming the stream, so replay stays aligned
 * between quantum and classical paths.
 *
 * BOUNDARY: engine/AI-side only. Gameplay/ must never include this
 * (integer determinism); the Campaign/Quantum/QuantumOracle adapter
 * remains the only Gameplay-facing crossing point.
 */

#pragma once

#include "Quantum/RandomSource.h"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace Potato {
namespace AI {

class QuantumSampler {
public:
    // Seeded construction; `skip` replays consumed stream units
    // (from Draws()) for bit-exact resume.
    explicit QuantumSampler(std::uint64_t seed, std::uint64_t skip = 0);
    // Injected source: the sampler owns and forwards to it; nullptr
    // falls back to a deterministic seed=0 stream.
    explicit QuantumSampler(std::unique_ptr<Quantum::IRandomSource> src);
    ~QuantumSampler();

    QuantumSampler(QuantumSampler&&) noexcept;
    QuantumSampler& operator=(QuantumSampler&&) noexcept;
    QuantumSampler(const QuantumSampler&) = delete;
    QuantumSampler& operator=(const QuantumSampler&) = delete;

    // Bernoulli — true with probability p. p<=0 → false, p>=1 → true,
    // both without consuming the stream.
    bool DrawBool(double p);

    // Categorical over weights; returns an index in [0, count).
    // Nonpositive entries are unreachable. Empty/all-nonpositive → -1;
    // single positive entry returns its index without consuming.
    int DrawWeighted(const float* weights, int count);
    int DrawWeighted(const std::vector<float>& weights) {
        return DrawWeighted(weights.data(),
                            static_cast<int>(weights.size()));
    }

    // Story 13.4 — Grover-style amplitude amplification (13.3 dial).
    // Prepares the categorical superposition, then applies `rounds`
    // of oracle-flip + diffusion-about-mean against `marked`, and
    // Measures. rounds=0 is exactly DrawWeighted. Each round rotates
    // the state in the {marked, unmarked} plane by a count-fixed
    // angle 2·asin(1/√count); the marked probability follows
    //   P(r) = sin²(asin(√p_marked) + r·2·asin(1/√count))
    // — it boosts, peaks, then overshoots, so the caller picks
    // rounds as the explore↔exploit dial rather than expecting
    // monotonicity.
    //
    // Degenerate questions follow DrawWeighted (no stream burn).
    // marked outside [0,count) or marked onto a nonpositive weight
    // degrades to a plain weighted draw — nonpositive entries stay
    // unreachable. Costs exactly one stream unit (the final Measure)
    // regardless of rounds.
    int DrawAmplified(const float* weights, int count,
                      int marked, int rounds);
    int DrawAmplified(const std::vector<float>& weights,
                      int marked, int rounds) {
        return DrawAmplified(weights.data(),
                             static_cast<int>(weights.size()),
                             marked, rounds);
    }

    // Story 13.5 — entangled pair draws for coordinated agents.
    // Prepares a Bell pair (H + CNOT on a fresh 2-qubit register)
    // and collapses it once, so both outcomes come out of a single
    // stream unit instead of two independent rolls.
    //
    // DrawCorrelated()     — |Φ⁺⟩ = (|00⟩+|11⟩)/√2 ⇒ both bits equal
    // DrawAntiCorrelated() — |Ψ⁺⟩ = (|01⟩+|10⟩)/√2 ⇒ bits opposite
    //
    // Each bit marginally ~50/50, so two agents sharing one channel
    // stay fair while their decisions are perfectly (anti-)aligned —
    // a quantum channel for allied coordination. Exactly one stream
    // unit per pair.
    std::pair<bool, bool> DrawCorrelated();
    std::pair<bool, bool> DrawAntiCorrelated();

    // Uniform integer in [0, n). n<=1 → 0 without consuming.
    // Reject-sampling over the k-qubit MeasureAll range — no modulo bias.
    int DrawInt(int n);

    // Uniform double in [0, 1) — exactly one stream unit.
    double DrawDouble();

    // Stream units consumed so far; QuantumSampler(seed, Draws())
    // resumes bit-exactly.
    std::uint64_t Draws() const { return draws_; }

private:
    // Q-5-style injection: each draw builds a fresh circuit holding
    // this adapter, which forwards every roll to the sampler's stream
    // (draw order = construction order) and counts consumption.
    class ForwardSource;

    std::unique_ptr<Quantum::IRandomSource> MakeForwarder();
    std::uint64_t Roll64();  // one counted stream unit

    std::unique_ptr<Quantum::IRandomSource> owned_;
    Quantum::IRandomSource* src_ = nullptr;
    std::uint64_t draws_ = 0;
};

} // namespace AI
} // namespace Potato
