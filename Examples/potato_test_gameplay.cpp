// potato_test_gameplay — headless smoke test for the Gameplay module.
// Contract under test: fixed-seed determinism of the sim kernel
// (same seed -> same checksum; different seeds -> different checksums)
// and that the module links/runs with no GL/Rendering/GUI dependency.

#include "Gameplay/Sim/Sim.h"

#include <cstdint>
#include <cstdio>

namespace {

int failures = 0;

void Check(bool cond, const char* name) {
    std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", name);
    if (!cond) ++failures;
}

Potato::Gameplay::Sim RunTicks(std::uint64_t seed, std::uint64_t ticks) {
    Potato::Gameplay::Sim sim(seed);
    for (std::uint64_t i = 0; i < ticks; ++i) sim.Tick();
    return sim;
}

} // namespace

int main() {
    constexpr std::uint64_t TICKS = 200; // 10 seconds at 20 Hz

    const auto a1 = RunTicks(1234, TICKS);
    const auto a2 = RunTicks(1234, TICKS);
    const auto b  = RunTicks(5678, TICKS);
    const auto z0 = RunTicks(0, TICKS);

    Check(a1.TickCount() == TICKS, "tick count reaches 200");
    Check(a1.Checksum() == a2.Checksum(), "same seed -> same checksum");
    Check(a1.Checksum() != b.Checksum(), "different seed -> different checksum");
    Check(z0.Checksum() != 0, "seed 0 produces non-degenerate checksum");
    Check(z0.Checksum() != a1.Checksum(), "seed 0 distinct from other seeds");
    // Golden pin — locks the PRNG bit stream against refactor drift.
    Check(a1.Checksum() == 8436903512816397795ull, "golden checksum (seed 1234 x 200)");

    std::printf("%s\n", failures == 0 ? "SMOKE TICK PASS" : "SMOKE TICK FAIL");
    return failures == 0 ? 0 : 1;
}
