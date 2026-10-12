// Story 11.4 — deterministic chain/outbox exporter.
//
// Produces the artifacts the external relayer (Tools/MintRelayer/)
// consumes: a potato.ledger/3 file plus a potato.mintclaim/1
// outbox, all built through the real write paths (Ledger::Post /
// EmitGatedClaims / MintOutbox::Emit). Doubles as the demo driver
// and as the fixture producer for the relayer's test suite —
// output is byte-deterministic, so checked-in fixtures regenerate
// identically.
//
// Usage: potato_export_chain <outdir>
//   writes <outdir>/ledger.json and <outdir>/claims/*.json
#include "Campaign/Chain/ClaimGate.h"
#include "Campaign/Chain/LedgerMerkle.h"
#include "Campaign/Chain/MintClaim.h"
#include "Campaign/Chain/MintOutbox.h"
#include "Campaign/Ledger/Ledger.h"
#include "Gameplay/Json/JsonValue.h"
#include "Gameplay/Record/BattleRecorder.h"
#include "Gameplay/Result.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <span>
#include <vector>

using namespace Potato;
namespace fs = std::filesystem;

namespace {

// tmp→rename atomic write, same discipline as SaveSystem::Save /
// MintOutbox::Emit: a torn write leaves only the .tmp behind.
bool WriteAtomic(const fs::path& dst, const std::string& text) {
    const fs::path tmp = fs::path(dst).concat(".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << text;
        out.flush();
        if (!out) return false;
        out.close();
        if (!out) return false;
    }
    std::error_code ec;
    fs::rename(tmp, dst, ec);
    return !ec;
}

// Self-consistent battle record: payload fields + an integrity
// block whose root is recomputed the way CrossCheck does.
Gameplay::JsonValue MakeRecord(std::uint64_t& root) {
    Gameplay::JsonValue::Object payload;
    payload["battle"] = Gameplay::JsonValue::String("ford_crossing");
    payload["ticks"] = Gameplay::JsonValue::Int(1200);
    Gameplay::JsonValue rec = Gameplay::JsonValue::MakeObject(payload);
    root = Gameplay::BattleRecorder::ComputeRoot(rec);
    Gameplay::JsonValue::Object integ;
    integ["root"] =
        Gameplay::JsonValue::Int(static_cast<std::int64_t>(root));
    payload["integrity"] = Gameplay::JsonValue::MakeObject(
        std::move(integ));
    return Gameplay::JsonValue::MakeObject(std::move(payload));
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr,
                     "usage: potato_export_chain <outdir>\n");
        return 2;
    }
    const fs::path outDir = argv[1];
    const fs::path claimDir = outDir / "claims";
    std::error_code ec;
    fs::create_directories(claimDir, ec);
    if (ec) {
        std::fprintf(stderr, "mkdir failed: %s\n",
                     ec.message().c_str());
        return 1;
    }

    // A small campaign trail: two honest postings, then the chapter
    // seal that anchors the record root (same shape ResolveAftermath
    // writes).
    Campaign::Ledger ledger;
    Campaign::Posting p1;
    p1.credit = {Campaign::Account::Materiel, 40};
    p1.debit = {Campaign::Account::PopularSupport, 15};
    p1.memo = "burned village r3";
    p1.tags = {"atrocity"};
    if (!ledger.Post(p1).ok()) {
        std::fprintf(stderr, "post p1 rejected\n");
        return 1;
    }
    Campaign::Posting p2;
    p2.credit = {Campaign::Account::MartialMerit, 60};
    p2.debit = {Campaign::Account::Materiel, 25};
    p2.memo = "ford crossing won";
    p2.tags = {"chapter:7"};
    if (!ledger.Post(p2).ok()) {
        std::fprintf(stderr, "post p2 rejected\n");
        return 1;
    }

    std::uint64_t root = 0;
    const Gameplay::JsonValue record = MakeRecord(root);
    Campaign::Posting seal;
    seal.credit = {Campaign::Account::Mandate, 1};
    seal.debit = {Campaign::Account::ArmyPrestige, 1};
    seal.memo = "chapter sealed";
    seal.tags = {Campaign::RecordRootTag(root),
                 "resolution:battle_victory", "chapter:7"};
    if (!ledger.Post(seal).ok()) {
        std::fprintf(stderr, "anchor seal rejected\n");
        return 1;
    }

    // The gate does the emitting — the exporter never hand-writes
    // a claim file.
    const Campaign::MintOutbox outbox(claimDir);
    const std::vector<std::string_view> keys = {
        Campaign::ACH_ZERO_COMBAT};
    const auto gate =
        Campaign::EmitGatedClaims(ledger, record, "battle_victory",
                                  7, keys, outbox);
    if (!gate.ok()) {
        std::fprintf(stderr, "gate failed: %s / %s\n",
                     gate.error.c_str(),
                     gate.reason.c_str());
        return 1;
    }
    if (gate.value.verdict != Campaign::CrossVerdict::Clean) {
        std::fprintf(stderr, "gate refused: verdict %d\n",
                     static_cast<int>(gate.value.verdict));
        return 1;
    }

    const auto ledgerDoc = ledger.ToJson();
    if (!ledgerDoc.ok()) {
        std::fprintf(stderr, "ledger emit failed\n");
        return 1;
    }
    const fs::path ledgerPath = outDir / "ledger.json";
    if (!WriteAtomic(ledgerPath, ledgerDoc.value.Emit())) {
        std::fprintf(stderr, "ledger write failed\n");
        return 1;
    }

    // The C++ merkle root over the same (ledger, claims) pair —
    // the relayer's JS port must reproduce this byte-for-byte.
    const auto scan = outbox.Scan();
    if (!scan.ok()) {
        std::fprintf(stderr, "outbox rescan failed\n");
        return 1;
    }
    const auto merkle =
        Campaign::LedgerMerkleRoot(ledger, scan.value.claims);
    if (!merkle.ok()) {
        std::fprintf(stderr, "merkle fold failed\n");
        return 1;
    }
    const fs::path merklePath = outDir / "merkle.json";
    const std::string merkleText =
        "{\"schema\":\"potato.merkle/1\",\"root\":\"" +
        Campaign::RootHex(merkle.value) + "\"}";
    if (!WriteAtomic(merklePath, merkleText)) {
        std::fprintf(stderr, "merkle write failed\n");
        return 1;
    }

    std::printf("wrote %s\n", ledgerPath.string().c_str());
    std::printf("wrote %s\n", merklePath.string().c_str());
    for (const fs::path& f : gate.value.emitted) {
        std::printf("wrote %s\n", f.string().c_str());
    }
    return 0;
}
