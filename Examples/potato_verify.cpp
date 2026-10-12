// potato_verify — replay-verifier CLI (Story 1.11).
//   potato_verify <record.json>
// Re-runs a potato.battle_record/1 document through the deterministic
// sim and compares bit-exact. Exit 0 on VERIFY OK, 1 on failure.

#include "Gameplay/Record/ReplayVerifier.h"

#include <cstdio>
#include <string>

using namespace Potato::Gameplay;

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: potato_verify <record.json>\n");
        return 1;
    }
    auto result = Replay::VerifyFile(argv[1]);
    if (!result.ok()) {
        std::fprintf(stderr, "VERIFY FAIL: %s (%s)\n",
                     result.reason.c_str(), result.error.c_str());
        return 1;
    }
    const VerifyResult& r = result.value;
    if (r.tampered) {
        std::fprintf(stderr, "VERIFY FAIL: %s\n", r.reason.c_str());
        return 1;
    }
    if (!r.ok) {
        std::fprintf(stderr, "VERIFY FAIL: %s\n", r.reason.c_str());
        return 1;
    }
    if (r.downgrade) {
        std::fprintf(stderr, "WARNING: record stamped by older tool "
                            "version — downgraded verification\n");
    }
    std::printf("VERIFY OK checksum=%llu\n",
                static_cast<unsigned long long>(r.checksum));
    return 0;
}
