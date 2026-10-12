// Story 11.1 — mint-claim schema + outbox (potato.mintclaim/1):
// deterministic ids (filename-safe '-' separators), id==recomputed
// binding, per-kind field rules, atomic Emit, per-file-isolated
// Scan, ledger seal bitcast round-trip.
// Story 11.2 — ledger Merkle root: canonical leaf order (entry
// seq, then claim id), domain-tagged FNV-1a fold, set semantics,
// iff-coverage (suspect flag exempt).
// Story 11.3 — replay-gated emission: CrossVerdict::Clean is the
// only path to the outbox; forgery_bust auto-adds on forged
// anchors; refusals are data, never files.
#include "Campaign/Chain/ClaimGate.h"
#include "Campaign/Chain/LedgerMerkle.h"
#include "Campaign/Chain/MintClaim.h"
#include "Campaign/Chain/MintOutbox.h"
#include "Campaign/Ledger/Ledger.h"
#include "Gameplay/Record/BattleRecorder.h"
#include "Gameplay/Json/JsonValue.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <span>
#include <sstream>
#include <vector>

using namespace Potato;
using Potato::Campaign::Account;
using Potato::Campaign::ClaimKind;
using Potato::Campaign::ClaimId;
using Potato::Campaign::CrossVerdict;
using Potato::Campaign::EmitGatedClaims;
using Potato::Campaign::Ledger;
using Potato::Campaign::LedgerMerkleRoot;
using Potato::Campaign::MintClaim;
using Potato::Campaign::MintOutbox;
using Potato::Campaign::Posting;
using Potato::Campaign::RecordRootTag;
using Potato::Campaign::ACH_FORGERY_BUST;
using Potato::Campaign::ACH_FOUR_VOICE;
using Potato::Campaign::ACH_ZERO_COMBAT;
using Potato::Campaign::ParseRootHex;
using Potato::Campaign::RootHex;
using Potato::Gameplay::JsonValue;

namespace fs = std::filesystem;

static int failures = 0;
static void Check(bool cond, const char* name) {
    if (cond) {
        std::printf("[PASS] %s\n", name);
    } else {
        std::printf("[FAIL] %s\n", name);
        ++failures;
    }
}

static void Write(const fs::path& dir, const char* name,
                  const char* text) {
    std::ofstream f(dir / name,
                    std::ios::binary | std::ios::trunc);
    f << text;
}

static std::string ReadAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// A settlement claim with a deliberately >=2^63 ledger tip — the
// wire value must round-trip as an int64 bit-cast (negative JSON).
static MintClaim Settlement() {
    MintClaim c;
    c.kind = ClaimKind::ChapterSettlement;
    c.chapter = 7;
    c.recordRoot = 0x0123abcdef456789ull;
    c.resolution = "governance_victory";
    c.ledgerCount = 412;
    c.ledgerTip = 0xdbdc385a55b95eedull;
    return c;
}

static MintClaim Achievement() {
    MintClaim c;
    c.kind = ClaimKind::Achievement;
    c.chapter = 7;
    c.achievement = "zero_combat";
    c.ledgerCount = 412;
    c.ledgerTip = 0xdbdc385a55b95eedull;
    return c;
}

static bool Rejects(const char* text, const char* errClass) {
    const auto doc = JsonValue::Parse(text);
    if (!doc.ok()) return errClass == std::string("parse");
    const auto r = MintClaim::FromJson(doc.value);
    return !r.ok() && r.error == errClass;
}

int main() {
    // --- ids & codec ---
    {
        const MintClaim s = Settlement();
        Check(ClaimId(s) == "settlement-0123abcdef456789",
              "11.1: settlement id binds record root");
        const MintClaim a = Achievement();
        Check(ClaimId(a) == "achievement-zero_combat-0007",
              "11.1: achievement id binds key+chapter");

        const auto doc = s.ToJson();
        Check(doc.ok(), "11.1: settlement ToJson ok");
        const auto parsed = JsonValue::Parse(doc.value.Emit());
        Check(parsed.ok(), "11.1: emit reparses");
        const auto back = MintClaim::FromJson(parsed.value);
        Check(back.ok() &&
                  back.value.recordRoot == s.recordRoot &&
                  back.value.chapter == s.chapter &&
                  back.value.resolution == s.resolution &&
                  back.value.ledgerCount == s.ledgerCount &&
                  back.value.ledgerTip == s.ledgerTip,
              "11.1: settlement round-trips (tip bitcast)");
        // The wire really did carry a negative int64.
        Check(doc.value.Emit().find("\"tip\":-2604144523890565395") !=
                  std::string::npos,
              "11.1: tip emits as int64 bit-cast");

        const auto adoc = Achievement().ToJson();
        const auto aparsed = JsonValue::Parse(adoc.value.Emit());
        const auto aback = MintClaim::FromJson(aparsed.value);
        Check(aback.ok() &&
                  aback.value.kind == ClaimKind::Achievement &&
                  aback.value.achievement == "zero_combat",
              "11.1: achievement round-trips");

        uint64_t v = 0;
        Check(ParseRootHex("0123abcdef456789", v) &&
                  v == 0x0123abcdef456789ull &&
                  RootHex(v) == "0123abcdef456789",
              "11.1: root hex codec");
    }

    // --- rejection battery ---
    Check(Rejects("{\"schema\":\"potato.mintclaim/2\"}", "schema"),
          "11.1: wrong schema rejected");
    Check(Rejects("{\"schema\":\"potato.mintclaim/1\",\"id\":\"x\","
                  "\"kind\":\"bogus\",\"chapter\":0,"
                  "\"ledger\":{\"count\":0,\"tip\":0}}",
                  "kind"),
          "11.1: unknown kind rejected");
    // Settlement missing record_root.
    Check(Rejects("{\"schema\":\"potato.mintclaim/1\","
                  "\"id\":\"settlement-0123abcdef456789\","
                  "\"kind\":\"chapter_settlement\",\"chapter\":7,"
                  "\"resolution\":\"defeat\","
                  "\"ledger\":{\"count\":1,\"tip\":0}}",
                  "field"),
          "11.1: missing record_root rejected");
    // Settlement with non-hex record_root (right id, bad root).
    Check(Rejects("{\"schema\":\"potato.mintclaim/1\","
                  "\"id\":\"settlement-0123abcdef456789\","
                  "\"kind\":\"chapter_settlement\",\"chapter\":7,"
                  "\"record_root\":\"XYZ\",\"resolution\":\"defeat\","
                  "\"ledger\":{\"count\":1,\"tip\":0}}",
                  "field"),
          "11.1: bad record_root hex rejected");
    // Settlement with unknown resolution.
    Check(Rejects("{\"schema\":\"potato.mintclaim/1\","
                  "\"id\":\"settlement-0123abcdef456789\","
                  "\"kind\":\"chapter_settlement\",\"chapter\":7,"
                  "\"record_root\":\"0123abcdef456789\","
                  "\"resolution\":\"total_war\","
                  "\"ledger\":{\"count\":1,\"tip\":0}}",
                  "field"),
          "11.1: unknown resolution rejected");
    // Achievement missing key.
    Check(Rejects("{\"schema\":\"potato.mintclaim/1\","
                  "\"id\":\"achievement--0007\","
                  "\"kind\":\"achievement\",\"chapter\":7,"
                  "\"ledger\":{\"count\":1,\"tip\":0}}",
                  "field"),
          "11.1: missing achievement rejected");
    // Achievement with ':' in key — filename-illegal charset.
    Check(Rejects("{\"schema\":\"potato.mintclaim/1\","
                  "\"id\":\"achievement-bad:key-0007\","
                  "\"kind\":\"achievement\",\"chapter\":7,"
                  "\"achievement\":\"bad:key\","
                  "\"ledger\":{\"count\":1,\"tip\":0}}",
                  "field"),
          "11.1: ':' in achievement key rejected");
    // Kind-crossed: settlement carrying achievement.
    Check(Rejects("{\"schema\":\"potato.mintclaim/1\","
                  "\"id\":\"settlement-0123abcdef456789\","
                  "\"kind\":\"chapter_settlement\",\"chapter\":7,"
                  "\"record_root\":\"0123abcdef456789\","
                  "\"resolution\":\"defeat\","
                  "\"achievement\":\"zero_combat\","
                  "\"ledger\":{\"count\":1,\"tip\":0}}",
                  "field"),
          "11.1: settlement+achievement field rejected");
    // Kind-crossed: achievement carrying record_root.
    Check(Rejects("{\"schema\":\"potato.mintclaim/1\","
                  "\"id\":\"achievement-zero_combat-0007\","
                  "\"kind\":\"achievement\",\"chapter\":7,"
                  "\"achievement\":\"zero_combat\","
                  "\"record_root\":\"0123abcdef456789\","
                  "\"ledger\":{\"count\":1,\"tip\":0}}",
                  "field"),
          "11.1: achievement+record_root field rejected");
    // Stored id != recomputed content id — the tamper check.
    Check(Rejects("{\"schema\":\"potato.mintclaim/1\","
                  "\"id\":\"settlement-0000000000000000\","
                  "\"kind\":\"chapter_settlement\",\"chapter\":7,"
                  "\"record_root\":\"0123abcdef456789\","
                  "\"resolution\":\"defeat\","
                  "\"ledger\":{\"count\":1,\"tip\":0}}",
                  "id"),
          "11.1: id mismatch rejected");
    // Chapter bounds.
    Check(Rejects("{\"schema\":\"potato.mintclaim/1\","
                  "\"id\":\"settlement-0123abcdef456789\","
                  "\"kind\":\"chapter_settlement\",\"chapter\":99,"
                  "\"record_root\":\"0123abcdef456789\","
                  "\"resolution\":\"defeat\","
                  "\"ledger\":{\"count\":1,\"tip\":0}}",
                  "field"),
          "11.1: chapter out of range rejected");

    // --- outbox emit / scan ---
    const fs::path dir =
        fs::temp_directory_path() / "potato_test_mintclaim";
    fs::remove_all(dir);
    {
        MintOutbox box(dir);
        const auto p = box.Emit(Settlement());
        Check(p.ok() && fs::exists(p.value),
              "11.1: emit commits <id>.json");
        Check(p.value.filename().string() ==
                  "settlement-0123abcdef456789.json",
              "11.1: filename is the claim id");
        const std::string first = ReadAll(p.value);
        const auto p2 = box.Emit(Settlement());
        Check(p2.ok() && ReadAll(p2.value) == first,
              "11.1: re-emit byte-identical (idempotent)");
        Check(!fs::exists(dir / "settlement-0123abcdef456789.tmp"),
              "11.1: no tmp left behind");

        Check(box.Emit(Achievement()).ok(),
              "11.1: achievement emits");
        // One poisoned file in the spool.
        Write(dir, "poison.json",
              "{\"schema\":\"potato.mintclaim/1\",\"id\":\"bogus\"}");
        const auto scan = box.Scan();
        Check(scan.ok(), "11.1: scan ok with poison present");
        Check(scan.value.claims.size() == 2 &&
                  scan.value.rejected.size() == 1,
              "11.1: per-file isolation — poison rejected");
        // Canonical order: id sort — achievement- sorts before
        // settlement-.
        Check(ClaimId(scan.value.claims[0]) ==
                  "achievement-zero_combat-0007" &&
                  ClaimId(scan.value.claims[1]) ==
                      "settlement-0123abcdef456789",
              "11.1: scan returns claims in id order");
        // Scanned claims must themselves re-emit identically.
        const auto reDoc = scan.value.claims[1].ToJson();
        Check(reDoc.ok() && reDoc.value.Emit() == first,
              "11.1: scanned claim re-emits identical doc");
    }
    {
        MintOutbox missing(dir / "no_such_dir");
        const auto scan = missing.Scan();
        Check(!scan.ok() && scan.error == "io",
              "11.1: unreadable dir fails io");
    }
    fs::remove_all(dir);

    // ================= Story 11.2 — ledger Merkle root =================

    // --- empty fold → 0 sentinel ---
    {
        Ledger l;
        const auto r =
            LedgerMerkleRoot(l, std::span<const MintClaim>{});
        Check(r.ok() && r.value == 0,
              "11.2: empty ledger + empty claims = 0");
    }

    // --- determinism + canonical order + set semantics ---
    {
        Ledger l;
        Posting p;
        p.credit = {Account::Materiel, 40};
        p.debit = {Account::PopularSupport, 15};
        p.memo = "burned village r3";
        p.tags = {"atrocity"};
        Check(l.Post(p).ok(), "11.2: posting lands");
        Posting q;
        q.credit = {Account::Mandate, 1};
        q.debit = {Account::ArmyPrestige, 1};
        q.memo = "chapter sealed";
        q.tags = {"resolution:governance_victory", "chapter:7"};
        Check(l.Post(q).ok(), "11.2: seal posts");

        const MintClaim s = Settlement();
        const MintClaim a = Achievement();
        const std::vector<MintClaim> forward = {s, a};
        const std::vector<MintClaim> reverse = {a, s};
        const std::vector<MintClaim> duped = {a, s, a};

        const auto r1 = LedgerMerkleRoot(l, forward);
        const auto r2 = LedgerMerkleRoot(l, reverse);
        const auto r3 = LedgerMerkleRoot(l, duped);
        Check(r1.ok() && r2.ok() && r3.ok(), "11.2: folds ok");
        Check(r1.value == r2.value,
              "11.2: claim order irrelevant (canonical sort)");
        Check(r1.value == r3.value,
              "11.2: exact-duplicate claim dedupes");
        Check(LedgerMerkleRoot(l, forward).value == r1.value,
              "11.2: same inputs → same root (bit-exact)");
        Check(r1.value != 0, "11.2: populated fold non-zero");

        // Ledger-only and claims-only subtrees differ from the
        // joined root.
        Check(LedgerMerkleRoot(l, std::span<const MintClaim>{})
                  .value != r1.value,
              "11.2: claims move the root");
        Ledger empty;
        Check(LedgerMerkleRoot(empty, forward).value != r1.value,
              "11.2: ledger moves the root");

        // Iff-coverage: entry content mutates → root moves.
        Ledger l2 = l;
        Posting p2;
        p2.credit = {Account::Materiel, 41}; // +1 amount
        p2.debit = {Account::PopularSupport, 15};
        p2.memo = "burned village r3";
        p2.tags = {"atrocity"};
        Check(l2.Post(p2).ok(), "11.2: sibling ledger posts");
        Check(LedgerMerkleRoot(l2, forward).value != r1.value,
              "11.2: entry content moves the root");

        // Judgment overlay is NOT covered — flag flip must not
        // move the anchor.
        Check(l.SetSuspect(0), "11.2: suspect flag sets");
        Check(LedgerMerkleRoot(l, forward).value == r1.value,
              "11.2: suspect flag does NOT move the root");

        // Claim leaf binds the full emit — ledgerTip changes move
        // the root even though the id wouldn't.
        MintClaim s2 = s;
        s2.ledgerTip ^= 1;
        const std::vector<MintClaim> tipped = {s2, a};
        Check(ClaimId(s2) == ClaimId(s) &&
                  LedgerMerkleRoot(l, tipped).value != r1.value,
              "11.2: claim leaf covers more than the id");
    }

    // --- odd/even leaf counts both fold ---
    {
        Ledger l; // zero entries
        const MintClaim a = Achievement();
        const auto odd =
            LedgerMerkleRoot(l, std::vector<MintClaim>{a});
        Check(odd.ok() && odd.value != 0,
              "11.2: single-leaf fold non-zero");
        const MintClaim s = Settlement();
        const auto even =
            LedgerMerkleRoot(l, std::vector<MintClaim>{a, s});
        Check(even.ok() && even.value != odd.value,
              "11.2: leaf count moves the root");
    }

    // ============ Story 11.3 — replay-gated emission ============

    // A real, self-consistent record doc: payload + integrity.root
    // recomputed the same way CrossCheck does.
    auto MakeRecord = [](std::uint64_t& root) {
        JsonValue::Object payload;
        payload["battle"] = JsonValue::String("ford_crossing");
        payload["ticks"] = JsonValue::Int(1200);
        JsonValue rec = JsonValue::MakeObject(payload);
        root = Potato::Gameplay::BattleRecorder::ComputeRoot(rec);
        JsonValue::Object integ;
        integ["root"] =
            JsonValue::Int(static_cast<std::int64_t>(root));
        payload["integrity"] =
            JsonValue::MakeObject(std::move(integ));
        return JsonValue::MakeObject(std::move(payload));
    };

    const fs::path gateDir =
        fs::temp_directory_path() / "potato_test_claimgate";
    fs::remove_all(gateDir);

    // --- Clean: settlement + achievements emit ---
    {
        std::uint64_t root = 0;
        const JsonValue rec = MakeRecord(root);
        Ledger l;
        Posting seal;
        seal.credit = {Account::Mandate, 1};
        seal.debit = {Account::ArmyPrestige, 1};
        seal.memo = "chapter sealed";
        seal.tags = {RecordRootTag(root),
                     "resolution:battle_victory", "chapter:7"};
        Check(l.Post(seal).ok(), "11.3: anchor seal posts");

        MintOutbox box(gateDir);
        const std::vector<std::string_view> keys = {
            ACH_ZERO_COMBAT, ACH_FOUR_VOICE};
        const auto r = EmitGatedClaims(l, rec, "battle_victory", 7,
                                       keys, box);
        Check(r.ok() && r.value.verdict == CrossVerdict::Clean,
              "11.3: clean record passes the gate");
        Check(r.value.emitted.size() == 3,
              "11.3: settlement + two achievements emit");
        Check(r.value.emitted[0].filename().string() ==
                  "settlement-" + RootHex(root) + ".json",
              "11.3: settlement file binds recomputed root");
        Check(r.value.emitted[1].filename().string() ==
                  "achievement-four_voice_ending-0007.json" &&
                  r.value.emitted[2].filename().string() ==
                      "achievement-zero_combat-0007.json",
              "11.3: achievements emit in id order");
        Check(r.value.recordRoot == root,
              "11.3: report carries the proved root");
        // The emitted settlement claim binds the live seal.
        const auto scan = box.Scan();
        Check(scan.ok() && scan.value.claims.size() == 3 &&
                  scan.value.claims[0].ledgerTip == l.Tip(),
              "11.3: claims stamp the live ledger seal");
    }

    // --- forgery bust: forged anchor auto-adds the key, dedupe ---
    {
        fs::remove_all(gateDir);
        std::uint64_t root = 0;
        const JsonValue rec = MakeRecord(root);
        Ledger l;
        Posting p;
        p.credit = {Account::Mandate, 1};
        p.debit = {Account::ArmyPrestige, 1};
        p.memo = "planted seal";
        p.tags = {RecordRootTag(root), "chapter:7"};
        Check(l.Forge(p).ok(), "11.3: forged anchor posts");

        MintOutbox box(gateDir);
        // Caller also asserts it — the gate dedupes to one claim.
        const std::vector<std::string_view> keys = {
            ACH_FORGERY_BUST};
        const auto r = EmitGatedClaims(l, rec, "subversion", 7,
                                       keys, box);
        Check(r.ok() && r.value.emitted.size() == 2,
              "11.3: forged anchor still mints (coverage by "
              "fiction) + auto-key dedupes");
        Check(r.value.emitted[1].filename().string() ==
                  "achievement-forgery_bust-0007.json",
              "11.3: forgery_bust auto-added");
    }

    // --- refusals: NoAnchor, RecordInconsistent, BadRecord ---
    {
        fs::remove_all(gateDir);
        std::uint64_t root = 0;
        const JsonValue rec = MakeRecord(root);
        Ledger l; // no anchor posted
        MintOutbox box(gateDir);
        const std::vector<std::string_view> keys = {
            ACH_ZERO_COMBAT};
        const auto r = EmitGatedClaims(l, rec, "battle_victory", 7,
                                       keys, box);
        Check(r.ok() && r.value.verdict == CrossVerdict::NoAnchor &&
                  r.value.emitted.empty(),
              "11.3: unanchored record emits nothing");
        Check(!fs::exists(gateDir) ||
                  fs::is_empty(gateDir),
              "11.3: refusal leaves no artifact");

        // Tampered declared root — self-inconsistent.
        JsonValue::Object tampered = rec.Members();
        JsonValue::Object integ = tampered["integrity"].Members();
        integ["root"] = JsonValue::Int(
            static_cast<std::int64_t>(root ^ 1));
        tampered["integrity"] =
            JsonValue::MakeObject(std::move(integ));
        const JsonValue bad = JsonValue::MakeObject(std::move(tampered));
        const auto r2 = EmitGatedClaims(l, bad, "battle_victory",
                                        7, keys, box);
        Check(r2.ok() &&
                  r2.value.verdict ==
                      CrossVerdict::RecordInconsistent &&
                  r2.value.emitted.empty(),
              "11.3: inconsistent record emits nothing");

        // No integrity block at all.
        const auto empty = JsonValue::Parse("{\"battle\":\"x\"}");
        const auto r3 = EmitGatedClaims(l, empty.value,
                                        "battle_victory", 7, keys,
                                        box);
        Check(r3.ok() &&
                  r3.value.verdict == CrossVerdict::BadRecord &&
                  r3.value.emitted.empty(),
              "11.3: malformed record refuses");
    }
    fs::remove_all(gateDir);

    if (failures == 0) {
        std::printf("ALL PASS\n");
        return 0;
    }
    std::printf("%d FAILURES\n", failures);
    return 1;
}
