#include "Campaign/Governance/Accumulators.h"
#include "Campaign/Ledger/CrossCheck.h"
#include "Campaign/Myth/GodStance.h"
#include "Campaign/Narrative/BattleReport.h"
#include "Campaign/Narrative/Conventions.h"
#include "Campaign/Narrative/Dossier.h"
#include "Campaign/Narrative/IntelLedger.h"
#include "Campaign/Myth/Mandate.h"
#include "Campaign/Myth/MythActions.h"
#include "Campaign/Myth/MythLog.h"
#include "Campaign/Ledger/AuditSpread.h"
#include "Campaign/Ledger/DeedBook.h"
#include "Campaign/Ledger/HistorianReport.h"
#include "Campaign/Ledger/Ledger.h"
#include "Campaign/Narrative/Marginalia.h"
#include "Gameplay/Json/JsonValue.h"
#include "Gameplay/Record/BattleRecorder.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;
void Check(bool cond, const char* name) {
    std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", name);
    if (!cond) ++failures;
}

using namespace Potato::Campaign;
using Potato::Gameplay::JsonValue;

Posting BurnVillage() {
    Posting p;
    p.credit = {Account::Materiel, 40};
    p.debit = {Account::PopularSupport, 15};
    p.memo = "burned village r3";
    p.tags = {"atrocity", "r3"};
    return p;
}

} // namespace

int main() {
    // --- AC1: balanced pair posts atomically; balances fold ---
    {
        Ledger l;
        auto r = l.Post(BurnVillage());
        Check(r.ok() && r.value == 0, "post returns seq 0");
        Check(l.Size() == 1, "entry recorded once");
        Check(l.Balance(Account::Materiel) == 40, "credit folds +40");
        Check(l.Balance(Account::PopularSupport) == -15,
              "debit folds -15");
        Check(l.Balance(Account::Mandate) == 0, "untouched account 0");
        const LedgerEntry& e = l.Entries().front();
        Check(e.credit.account == Account::Materiel &&
                  e.debit.account == Account::PopularSupport &&
                  e.memo == "burned village r3" && e.tags.size() == 2,
              "entry carries both legs + memo + tags");
    }

    // --- AC1: unbalanced postings rejected; chain untouched ---
    {
        Ledger l;
        Posting p = BurnVillage();
        p.credit.amount = 0;
        Check(!l.Post(p).ok(), "zero credit rejected");
        p = BurnVillage();
        p.debit.amount = -5;
        Check(!l.Post(p).ok(), "negative debit rejected");
        p = BurnVillage();
        p.debit.account = Account::Materiel;
        Check(!l.Post(p).ok(), "same-account pair rejected");
        Check(l.Size() == 0 && l.Balance(Account::Materiel) == 0,
              "rejections leave the chain untouched");
    }

    // --- rejection matrix, second half ---
    {
        Ledger l;
        Posting p = BurnVillage();
        p.credit.amount = -1;
        Check(!l.Post(p).ok(), "negative credit rejected");
        p = BurnVillage();
        p.debit.amount = 0;
        Check(!l.Post(p).ok(), "zero debit rejected");
        p = BurnVillage();
        p.credit.account = static_cast<Account>(99);
        Check(!l.Post(p).ok(), "phantom credit account rejected");
        p = BurnVillage();
        p.credit.amount = Ledger::MAX_AMOUNT + 1;
        Check(!l.Post(p).ok(), "amount over MAX_AMOUNT rejected");
        p = BurnVillage();
        p.memo.assign(Ledger::MAX_MEMO_LEN + 1, 'x');
        Check(!l.Post(p).ok(), "oversized memo rejected");
        p = BurnVillage();
        p.tags = {"atrocity", "atrocity"};
        Check(!l.Post(p).ok(), "duplicate tag rejected");
        p = BurnVillage();
        p.tags = {""};
        Check(!l.Post(p).ok(), "empty tag rejected");
        p = BurnVillage();
        p.tags.assign(Ledger::MAX_TAGS + 1, "t");
        Check(!l.Post(p).ok(), "tag count over MAX_TAGS rejected");
        Check(l.Size() == 0, "rejection matrix leaves chain empty");
    }

    // --- MAX leg posts and folds without overflow ---
    {
        Ledger l;
        Posting p = BurnVillage();
        p.credit.amount = Ledger::MAX_AMOUNT;
        p.debit.amount = Ledger::MAX_AMOUNT;
        Check(l.Post(p).ok(), "MAX_AMOUNT leg accepted");
        Check(l.Balance(Account::Materiel) == Ledger::MAX_AMOUNT,
              "max credit folds exactly");
    }

    // --- asymmetric magnitudes are legal by design ---
    {
        Ledger l;
        Posting p = BurnVillage(); // +40 / -15 is the canonical case
        Check(l.Post(p).ok(), "asymmetric pair accepted");
    }

    // --- seq is strictly increasing across appends ---
    {
        Ledger l;
        auto s0 = l.Post(BurnVillage());
        Posting p2 = BurnVillage();
        p2.credit = {Account::MartialMerit, 7};
        p2.debit = {Account::Materiel, 12};
        auto s1 = l.Post(p2);
        Check(s0.ok() && s1.ok() && s1.value == s0.value + 1,
              "seq increments in append order");
        Check(l.Balance(Account::Materiel) == 40 - 12,
              "balances fold across entries");
    }

    // --- potato.ledger/3 round-trip ---
    {
        Ledger l;
        l.Post(BurnVillage());
        Posting p = BurnVillage();
        p.credit = {Account::Mandate, 3};
        p.debit = {Account::ArmyPrestige, 9};
        p.memo = "temple levy";
        p.tags = {};
        l.Post(p);
        auto doc = l.ToJson();
        Check(doc.ok() &&
                  doc.value.FindString("schema") != nullptr &&
                  *doc.value.FindString("schema") ==
                      std::string(Ledger::SCHEMA),
              "ToJson emits potato.ledger/3");
        auto back = Ledger::FromJson(doc.value);
        Check(back.ok() && back.value.Size() == 2 &&
                  back.value.Balance(Account::Materiel) == 40 &&
                  back.value.Balance(Account::PopularSupport) == -15 &&
                  back.value.Balance(Account::Mandate) == 3 &&
                  back.value.Balance(Account::ArmyPrestige) == -9,
              "round-trip preserves entries + balances");
        Check(back.ok() &&
                  back.value.Entries()[0].tags.size() == 2 &&
                  back.value.Entries()[1].memo == "temple levy",
              "round-trip preserves memo + tags");
    }

    // --- serialization via Emit->Parse (the real wire path) ---
    {
        Ledger l;
        l.Post(BurnVillage());
        auto doc = l.ToJson();
        auto parsed = JsonValue::Parse(doc.value.Emit());
        Check(parsed.ok(), "emitted json re-parses");
        auto back = parsed.ok() ? Ledger::FromJson(parsed.value)
                                : Potato::Gameplay::Fail<Ledger>("", "");
        Check(back.ok() && back.value.Balance(Account::Materiel) == 40,
              "emit->parse->load lossless");
    }

    // --- AC (2.2): chain formation ---
    {
        Ledger l;
        l.Post(BurnVillage());
        Posting p = BurnVillage();
        p.credit = {Account::Mandate, 3};
        p.debit = {Account::ArmyPrestige, 9};
        l.Post(p);
        const auto& es = l.Entries();
        Check(es[0].prevHash == Ledger::kGenesisHash,
              "entry 0 chains to genesis");
        Check(es[1].prevHash == es[0].hash && es[1].hash != es[0].hash,
              "entry 1 links to entry 0's hash");
        Check(l.Tip() == es[1].hash, "Tip() is the last entry hash");
        Check(Ledger{}.Tip() == Ledger::kGenesisHash,
              "empty chain tip is genesis");
        Check(l.Verify() == nullptr, "honest chain verifies clean");
    }

    // --- AC (2.2): mutated history fails verify; load rejected ---
    {
        Ledger l;
        l.Post(BurnVillage());
        Posting p = BurnVillage();
        p.credit = {Account::Mandate, 3};
        p.debit = {Account::ArmyPrestige, 9};
        l.Post(p);
        const JsonValue doc = l.ToJson().value;

        const auto mutateEntry = [&doc](std::size_t idx,
                                        const char* key,
                                        JsonValue v) {
            JsonValue::Object o = doc.Members();
            JsonValue::Array arr = o["entries"].Items();
            JsonValue::Object e = arr[idx].Members();
            e[key] = std::move(v);
            arr[idx] = JsonValue::MakeObject(std::move(e));
            o["entries"] = JsonValue::MakeArray(std::move(arr));
            return JsonValue::MakeObject(std::move(o));
        };
        Check(!Ledger::FromJson(mutateEntry(
                  0, "memo", JsonValue::String("altered"))).ok(),
              "mutated memo -> hash mismatch -> reject");
        Check(!Ledger::FromJson(mutateEntry(
                  0, "hash", JsonValue::Int(42))).ok(),
              "forged stored hash -> reject");
        Check(!Ledger::FromJson(mutateEntry(
                  1, "prevHash", JsonValue::Int(42))).ok(),
              "relinked prevHash -> chain break -> reject");

        // Truncate the tail but keep the stored seal.
        {
            JsonValue::Object o = doc.Members();
            JsonValue::Array arr = o["entries"].Items();
            arr.pop_back();
            o["entries"] = JsonValue::MakeArray(std::move(arr));
            Check(!Ledger::FromJson(JsonValue::MakeObject(std::move(o)))
                       .ok(),
                  "truncated tail -> seal mismatch -> reject");
        }
        // Honest round-trip still verifies.
        auto back = Ledger::FromJson(doc);
        Check(back.ok() && back.value.Verify() == nullptr &&
                  back.value.Entries()[1].hash == l.Entries()[1].hash,
              "round-trip preserves + verifies chain");
    }

    // --- FromJson hard gates ---
    {
        auto bad = JsonValue::Parse(R"({"schema":"potato.ledger/1"})");
        Check(bad.ok() && !Ledger::FromJson(bad.value).ok(),
              "old version rejected");
        bad = JsonValue::Parse(R"({"schema":"potato.ledger/2"})");
        Check(bad.ok() && !Ledger::FromJson(bad.value).ok(),
              "/2 rejected (no provenance semantics)");
        bad = JsonValue::Parse(R"({"schema":"potato.ledger/4"})");
        Check(bad.ok() && !Ledger::FromJson(bad.value).ok(),
              "bad version rejected");
        bad = JsonValue::Parse(
            R"({"schema":"potato.ledger/3","seal":0})");
        Check(bad.ok() && !Ledger::FromJson(bad.value).ok(),
              "missing entries rejected");
        bad = JsonValue::Parse(
            R"({"schema":"potato.ledger/3","entries":[]})");
        Check(bad.ok() && !Ledger::FromJson(bad.value).ok(),
              "missing seal rejected");
        bad = JsonValue::Parse(R"([1,2,3])");
        Check(bad.ok() && !Ledger::FromJson(bad.value).ok(),
              "non-object root rejected");

        // Structural gates on a real /2 doc — mutate fields that fail
        // before the chain check runs.
        Ledger l;
        l.Post(BurnVillage());
        const JsonValue doc = l.ToJson().value;
        const auto mutateEntry = [&doc](const char* key, JsonValue v) {
            JsonValue::Object o = doc.Members();
            JsonValue::Array arr = o["entries"].Items();
            JsonValue::Object e = arr[0].Members();
            e[key] = std::move(v);
            arr[0] = JsonValue::MakeObject(std::move(e));
            o["entries"] = JsonValue::MakeArray(std::move(arr));
            return JsonValue::MakeObject(std::move(o));
        };
        Check(!Ledger::FromJson(mutateEntry("seq", JsonValue::Int(1)))
                   .ok(),
              "non-contiguous seq rejected");
        const auto mutateCredit = [&doc](const char* key, JsonValue v) {
            JsonValue::Object o = doc.Members();
            JsonValue::Array arr = o["entries"].Items();
            JsonValue::Object e = arr[0].Members();
            JsonValue::Object leg = e["credit"].Members();
            leg[key] = std::move(v);
            e["credit"] = JsonValue::MakeObject(std::move(leg));
            arr[0] = JsonValue::MakeObject(std::move(e));
            o["entries"] = JsonValue::MakeArray(std::move(arr));
            return JsonValue::MakeObject(std::move(o));
        };
        Check(!Ledger::FromJson(mutateCredit("amount", JsonValue::Int(0)))
                   .ok(),
              "unbalanced persisted entry rejected");
        Check(!Ledger::FromJson(mutateCredit(
                  "account", JsonValue::String("gold")))
                   .ok(),
              "unknown account name rejected");
        Check(!Ledger::FromJson(mutateCredit(
                  "amount", JsonValue::Int(2000000000000LL)))
                   .ok(),
              "persisted over-cap amount rejected");
        Check(!Ledger::FromJson(mutateEntry(
                  "tags", JsonValue::MakeArray(
                              {JsonValue::String("a"),
                               JsonValue::String("a")})))
                   .ok(),
              "persisted duplicate tag rejected");
    }

    // --- account name map is total and round-trips ---
    {
        bool all = true;
        for (int i = 0; i < kAccountCount; ++i) {
            Account a;
            all = all && AccountFromName(
                             AccountName(static_cast<Account>(i)), a) &&
                  static_cast<int>(a) == i;
        }
        Check(all, "all five accounts name round-trip");
        Account a;
        Check(!AccountFromName("min_xin", a),
              "cjk/pinyin variants not silently accepted");
    }

    // --- threat model: recompute is the boundary (2.5 anchors) ---
    {
        Ledger l;
        l.Post(BurnVillage());
        Posting p = BurnVillage();
        p.credit = {Account::Mandate, 3};
        p.debit = {Account::ArmyPrestige, 9};
        l.Post(p);
        const JsonValue doc = l.ToJson().value;

        // Truncate + recompute seal: LOADS. The seal is unkeyed —
        // documented boundary; Story 2.5 anchors the tip externally.
        {
            JsonValue::Object o = doc.Members();
            JsonValue::Array arr = o["entries"].Items();
            arr.pop_back();
            const std::uint64_t h0 = static_cast<std::uint64_t>(
                arr[0]["hash"].AsInt());
            o["entries"] = JsonValue::MakeArray(std::move(arr));
            o["seal"] = JsonValue::Int(static_cast<std::int64_t>(
                Ledger::SealHash(1, h0)));
            Check(Ledger::FromJson(JsonValue::MakeObject(std::move(o)))
                      .ok(),
                  "truncate+reseal loads (recompute-class boundary)");
        }
        // Rehashed rewrite: mutate memo, then recompute the WHOLE
        // chain (every downstream prevHash+hash) and the seal ->
        // LOADS. Same boundary: the seal is unkeyed.
        {
            LedgerEntry e0 = l.Entries()[0];
            e0.memo = "rewritten";
            e0.hash = Ledger::EntryHash(e0);
            LedgerEntry e1 = l.Entries()[1];
            e1.prevHash = e0.hash;
            e1.hash = Ledger::EntryHash(e1);
            JsonValue::Object o = doc.Members();
            JsonValue::Array arr = o["entries"].Items();
            JsonValue::Object f0 = arr[0].Members();
            f0["memo"] = JsonValue::String("rewritten");
            f0["hash"] =
                JsonValue::Int(static_cast<std::int64_t>(e0.hash));
            arr[0] = JsonValue::MakeObject(std::move(f0));
            JsonValue::Object f1 = arr[1].Members();
            f1["prevHash"] =
                JsonValue::Int(static_cast<std::int64_t>(e1.prevHash));
            f1["hash"] =
                JsonValue::Int(static_cast<std::int64_t>(e1.hash));
            arr[1] = JsonValue::MakeObject(std::move(f1));
            o["entries"] = JsonValue::MakeArray(std::move(arr));
            o["seal"] = JsonValue::Int(static_cast<std::int64_t>(
                Ledger::SealHash(2, e1.hash)));
            Check(Ledger::FromJson(JsonValue::MakeObject(std::move(o)))
                      .ok(),
                  "rehashed rewrite loads (anchoring is story 2.5)");
        }
        // Mistyped chain fields rejected at the IsInt gates.
        const auto mutE = [&doc](const char* key, JsonValue v) {
            JsonValue::Object o = doc.Members();
            JsonValue::Array arr = o["entries"].Items();
            JsonValue::Object e = arr[0].Members();
            e[key] = std::move(v);
            arr[0] = JsonValue::MakeObject(std::move(e));
            o["entries"] = JsonValue::MakeArray(std::move(arr));
            return JsonValue::MakeObject(std::move(o));
        };
        Check(!Ledger::FromJson(mutE("hash",
                  JsonValue::String("x"))).ok(),
              "string hash rejected");
        Check(!Ledger::FromJson(mutE("prevHash",
                  JsonValue::Real(1.5))).ok(),
              "real prevHash rejected");
        {
            JsonValue::Object o = doc.Members();
            o["seal"] = JsonValue::Bool(true);
            Check(!Ledger::FromJson(JsonValue::MakeObject(std::move(o)))
                       .ok(),
                  "bool seal rejected");
        }
        // Entry 0's prevHash must be genesis — 0 is not a synonym.
        Check(!Ledger::FromJson(mutE("prevHash",
                  JsonValue::Int(0))).ok(),
              "prevHash 0 != genesis -> reject");

        // Empty ledger round-trips; seal commits to count+genesis.
        {
            const JsonValue empty = Ledger{}.ToJson().value;
            auto back = Ledger::FromJson(empty);
            Check(back.ok() &&
                      back.value.Tip() == Ledger::kGenesisHash,
                  "empty ledger round-trips with genesis seal");
        }
        // Post on a loaded ledger continues the chain.
        {
            auto back = Ledger::FromJson(doc);
            Check(back.ok() &&
                      back.value.Post(BurnVillage()).ok() &&
                      back.value.Entries()[2].prevHash ==
                          l.Entries()[1].hash &&
                      back.value.Verify() == nullptr,
                  "post-on-load continues chain");
        }
    }
    // --- AC (2.3): forgery channel + suspect flags ---
    {
        Ledger l;
        l.Post(BurnVillage());
        const auto fr = l.Forge(BurnVillage());
        Check(fr.ok() && fr.value == 1 &&
                  l.Entries()[1].provenance == Provenance::Forged &&
                  l.Entries()[0].provenance == Provenance::Honest,
              "forge marks provenance");
        Check(l.Verify() == nullptr,
              "forged entry chain-verifies (kept, not corrupt)");
        Check(l.Entries()[1].prevHash == l.Entries()[0].hash,
              "forged entry links through the chain");
        // Forge validates like Post — a forgery pretends to be
        // valid bookkeeping.
        {
            Posting bad = BurnVillage();
            bad.debit = bad.credit;
            Check(!l.Forge(bad).ok() && l.Size() == 2,
                  "forge enforces the same balance invariant");
        }
        // Forged entries still fold — kept data, not hidden.
        Check(l.Balance(Account::Materiel) == 80,
              "forged entries fold into balances");

        // Suspect flag is a mutable judgment overlay.
        Check(!l.Entries()[1].suspect, "suspect defaults false");
        Check(l.SetSuspect(1), "set suspect on forged entry");
        Check(!l.SetSuspect(999), "set suspect out of range");
        Check(l.SuspectEntries().size() == 1 &&
                  l.SuspectEntries()[0] == 1,
              "SuspectEntries lists flagged seqs");
        Check(l.Verify() == nullptr,
              "flag write never breaks the chain");

        // Provenance + suspect both persist.
        auto back = Ledger::FromJson(l.ToJson().value);
        Check(back.ok() &&
                  back.value.Entries()[1].provenance ==
                      Provenance::Forged &&
                  back.value.Entries()[1].suspect &&
                  back.value.SuspectEntries().size() == 1,
              "provenance + suspect round-trip");

        // Flipping provenance in the file is a content mutation —
        // the stored hash no longer matches.
        {
            const JsonValue doc = l.ToJson().value;
            JsonValue::Object o = doc.Members();
            JsonValue::Array arr = o["entries"].Items();
            JsonValue::Object e = arr[1].Members();
            e["provenance"] = JsonValue::String("honest");
            arr[1] = JsonValue::MakeObject(std::move(e));
            o["entries"] = JsonValue::MakeArray(std::move(arr));
            Check(!Ledger::FromJson(JsonValue::MakeObject(
                                       std::move(o)))
                       .ok(),
                  "stripped forged mark -> hash mismatch -> reject");
        }
        // Flipping the suspect flag in the file is hash-transparent:
        // the doc LOADS with the tampered flag — judgment metadata
        // is not a claim (documented surface).
        {
            const JsonValue doc = l.ToJson().value;
            JsonValue::Object o = doc.Members();
            JsonValue::Array arr = o["entries"].Items();
            JsonValue::Object e = arr[1].Members();
            e["suspect"] = JsonValue::Bool(false);
            arr[1] = JsonValue::MakeObject(std::move(e));
            o["entries"] = JsonValue::MakeArray(std::move(arr));
            auto t = Ledger::FromJson(JsonValue::MakeObject(
                std::move(o)));
            Check(t.ok() && !t.value.Entries()[1].suspect,
                  "suspect flag is hash-transparent (loads)");
        }
        // Strict typing on the new fields.
        const auto mutField = [&l](const char* key, JsonValue v) {
            const JsonValue doc = l.ToJson().value;
            JsonValue::Object o = doc.Members();
            JsonValue::Array arr = o["entries"].Items();
            JsonValue::Object e = arr[0].Members();
            e.erase(key);
            if (!v.IsNull()) e[key] = std::move(v);
            arr[0] = JsonValue::MakeObject(std::move(e));
            o["entries"] = JsonValue::MakeArray(std::move(arr));
            return JsonValue::MakeObject(std::move(o));
        };
        Check(!Ledger::FromJson(mutField("provenance",
                  JsonValue{})).ok(),
              "missing provenance rejected");
        Check(!Ledger::FromJson(mutField("provenance",
                  JsonValue::Int(1))).ok(),
              "int provenance rejected");
        Check(!Ledger::FromJson(mutField("provenance",
                  JsonValue::String("planted"))).ok(),
              "unknown provenance rejected");
        Check(!Ledger::FromJson(mutField("suspect",
                  JsonValue::Int(1))).ok(),
              "int suspect rejected");
    }
    // --- 2.3 edge probes (review additions) ---
    {
        // Forge on an empty ledger: first link is genesis.
        Ledger l;
        const auto fr = l.Forge(BurnVillage());
        Check(fr.ok() && l.Entries()[0].prevHash ==
                  Ledger::kGenesisHash &&
                  l.Entries()[0].provenance == Provenance::Forged,
              "forge on empty ledger chains to genesis");
        // An all-forged chain verifies and round-trips.
        l.Forge(BurnVillage());
        Check(l.Verify() == nullptr, "all-forged chain verifies");
        auto back = Ledger::FromJson(l.ToJson().value);
        Check(back.ok() &&
                  back.value.Entries()[0].provenance ==
                      Provenance::Forged &&
                  back.value.Entries()[1].provenance ==
                      Provenance::Forged,
              "all-forged chain round-trips");

        // Suspect on an honest entry: set, clear, persist both ways.
        Check(l.SetSuspect(0) && l.Entries()[0].suspect,
              "honest entry can be flagged (suspicious auditor)");
        Check(l.SetSuspect(0, false) && !l.Entries()[0].suspect,
              "suspect flag clears");
        l.SetSuspect(0);
        back = Ledger::FromJson(l.ToJson().value);
        Check(back.ok() && back.value.Entries()[0].suspect &&
                  back.value.Entries()[0].provenance ==
                      Provenance::Forged,
              "flagged entry round-trips with provenance");

        // Case-variant / whitespace provenance rejected.
        const auto mutProv = [&l](const char* s) {
            const JsonValue doc = l.ToJson().value;
            JsonValue::Object o = doc.Members();
            JsonValue::Array arr = o["entries"].Items();
            JsonValue::Object e = arr[0].Members();
            e["provenance"] = JsonValue::String(s);
            arr[0] = JsonValue::MakeObject(std::move(e));
            o["entries"] = JsonValue::MakeArray(std::move(arr));
            return JsonValue::MakeObject(std::move(o));
        };
        Check(!Ledger::FromJson(mutProv("Forged")).ok(),
              "wrong-case provenance rejected");
        Check(!Ledger::FromJson(mutProv("forged ")).ok(),
              "trailing-space provenance rejected");
        // Missing suspect field rejected (strict typing).
        {
            const JsonValue doc = l.ToJson().value;
            JsonValue::Object o = doc.Members();
            JsonValue::Array arr = o["entries"].Items();
            JsonValue::Object e = arr[0].Members();
            e.erase("suspect");
            arr[0] = JsonValue::MakeObject(std::move(e));
            o["entries"] = JsonValue::MakeArray(std::move(arr));
            Check(!Ledger::FromJson(JsonValue::MakeObject(
                                       std::move(o)))
                       .ok(),
                  "missing suspect rejected");
        }
    }
    // --- AC (2.4): audit-aware report ---
    {
        Ledger l;
        l.Post(BurnVillage());  // seq 0: honest
        l.Forge(BurnVillage()); // seq 1: forged
        l.Post(BurnVillage());  // seq 2: honest
        l.SetSuspect(2);        // seq 2: flagged

        const HistorianReport r = RenderHistorianReport(l);
        Check(r.audit.entries == 3 && r.audit.forged == 1 &&
                  r.audit.suspect == 1,
              "audit segment folds counts");
        Check(r.audit.chainOk && r.audit.tip == l.Tip() &&
                  r.audit.seal == l.Seal(),
              "audit carries chain state");
        Check(r.omissions == 2 && r.includedSeqs.size() == 1 &&
                  r.includedSeqs[0] == 0,
              "forged + suspect omitted, confessedly counted");

        OmissionPolicy p;
        p.omitSuspect = false;
        const HistorianReport rp = RenderHistorianReport(l, p);
        Check(rp.omissions == 1 && rp.includedSeqs.size() == 2,
              "policy: suspect narrated, forged still hidden");
        p.omitForged = false;
        const HistorianReport ra = RenderHistorianReport(l, p);
        Check(ra.omissions == 0 && ra.includedSeqs.size() == 3,
              "policy: nothing omitted confesses zero");

        Check(r.RenderText().find("\xE6\x9C\xAC\xE5\xA0\xB1\xE5\x91\x8A\xE7\x9C\x81\xE7\x95\xA5 2 \xE9\xA0\x85") !=
                  std::string::npos,
              "confession line carries the count");
        const HistorianReport re = RenderHistorianReport(Ledger{});
        Check(re.omissions == 0 &&
                  re.RenderText().find("\xE6\x9C\xAC\xE5\xA0\xB1\xE5\x91\x8A\xE7\x9C\x81\xE7\x95\xA5 0 \xE9\xA0\x85") !=
                      std::string::npos,
              "empty ledger still confesses");
        Check(re.audit.chainOk && re.audit.breakReason == nullptr,
              "empty chain state is honest");

        // Overlap: forged AND suspect — omitted once, counted in both.
        l.SetSuspect(1);
        const HistorianReport ro = RenderHistorianReport(l);
        Check(ro.omissions == 2 && ro.audit.forged == 1 &&
                  ro.audit.suspect == 2,
              "overlap entry omitted once, counted in both audits");
        // Narrate-all policy: confession zero, audit counts persist.
        const HistorianReport rn =
            RenderHistorianReport(l, {false, false});
        Check(rn.omissions == 0 && rn.audit.forged == 1 &&
                  rn.audit.suspect == 2 && rn.includedSeqs.size() == 3,
              "narrate-all keeps audit counts");
        // Partial policy: forged narrated, suspect still hidden.
        const HistorianReport rg =
            RenderHistorianReport(l, {false, true});
        Check(rg.omissions == 2 && rg.includedSeqs.size() == 1 &&
                  rg.includedSeqs[0] == 0,
              "forged-narrated policy hides only suspect");
        // Exact included seqs under default policy.
        Check(r.includedSeqs.size() == 1 && r.includedSeqs[0] == 0,
              "default policy narrates only clean entries");
        // tip/seal present in the text render.
        Check(r.RenderText().find("tip: ") != std::string::npos &&
                  r.RenderText().find("seal: ") != std::string::npos,
              "render carries tip + seal");
    }
    // --- AC (2.5): replay-ledger cross-check ---
    {
        using Potato::Gameplay::BattleRecorder;

        // A minimal but real record doc: payload + integrity.root.
        const auto makeRecord = [](std::int64_t seed) {
            JsonValue::Object p;
            p["schema"] =
                JsonValue::String("potato.battle_record/1");
            p["seed"] = JsonValue::Int(seed);
            const std::uint64_t root = BattleRecorder::ComputeRoot(
                JsonValue::MakeObject(p));
            JsonValue::Object o = std::move(p);
            JsonValue::Object in;
            in["root"] =
                JsonValue::Int(static_cast<std::int64_t>(root));
            o["integrity"] = JsonValue::MakeObject(std::move(in));
            return std::pair{JsonValue::MakeObject(std::move(o)),
                             root};
        };

        // Tag convention round-trips.
        std::uint64_t parsed = 0;
        Check(ParseRecordRootTag(RecordRootTag(0xdeadbeef01ULL),
                                 parsed) &&
                  parsed == 0xdeadbeef01ULL,
              "record_root tag round-trips");
        Check(!ParseRecordRootTag("record_root:xyz", parsed) &&
                  !ParseRecordRootTag("record_:00", parsed),
              "malformed anchor tags ignored");

        // Clean pair: anchor tag binds the record's real root.
        Ledger l;
        const auto [docA, rootA] = makeRecord(42);
        {
            Posting p = BurnVillage();
            p.tags.push_back(RecordRootTag(rootA));
            l.Post(p);
        }
        const CrossCheckResult rc = CrossCheckRecord(l, docA);
        Check(rc.verdict == CrossVerdict::Clean && rc.anchors == 1 &&
                  rc.flagged == 0,
              "consistent pair verifies clean");

        // Tampered record: payload edited post-seal -> recomputed
        // root diverges from the anchor's stored root.
        {
            const auto [docB, rootB] = makeRecord(43);
            JsonValue::Object o = docB.Members();
            o["seed"] = JsonValue::Int(999); // tamper after seal
            const JsonValue tampered =
                JsonValue::MakeObject(std::move(o));
            Posting p = BurnVillage();
            p.tags.push_back(RecordRootTag(rootB));
            l.Post(p); // seq 1 anchors docB's true root
            const CrossCheckResult rm = CrossCheckRecord(l, tampered);
            Check(rm.verdict == CrossVerdict::RecordInconsistent &&
                      rm.flagged == 1 &&
                      l.Entries()[1].suspect,
                  "mismatch flags the covering entry suspect");
            Check(l.SuspectEntries().size() == 1,
                  "suspect flag persists after cross-check");
        }

        // Record uncovered by any anchor: absence, not mismatch.
        {
            const auto [docC, rootC] = makeRecord(7);
            const CrossCheckResult rn = CrossCheckRecord(l, docC);
            Check(rn.verdict == CrossVerdict::NoAnchor &&
                      rn.anchors == 0,
                  "uncovered record -> NoAnchor");
        }
        // Malformed record doc: no integrity root.
        {
            JsonValue::Object o;
            o["schema"] =
                JsonValue::String("potato.battle_record/1");
            const CrossCheckResult rb =
                CrossCheckRecord(l, JsonValue::MakeObject(
                                        std::move(o)));
            Check(rb.verdict == CrossVerdict::BadRecord,
                  "missing integrity -> BadRecord");
        }
        // An anchor claiming a root this record neither declares
        // nor produces belongs to a different record — skipped.
        {
            Ledger l2;
            const auto [docD, rootD] = makeRecord(11);
            Posting p1 = BurnVillage();
            p1.tags.push_back(RecordRootTag(rootD));
            l2.Post(p1); // seq 0: this record's anchor
            Posting p2 = BurnVillage();
            p2.tags.push_back(RecordRootTag(rootD + 1));
            l2.Post(p2); // seq 1: some other record's anchor
            const CrossCheckResult rd = CrossCheckRecord(l2, docD);
            Check(rd.verdict == CrossVerdict::Clean &&
                      rd.anchors == 1 && rd.flagged == 0 &&
                      !l2.Entries()[1].suspect,
                  "foreign-record anchor is skipped");
        }
        // Tampered payload: BOTH anchors claiming the declared root
        // now disagree with what the bytes produce — both flagged.
        {
            Ledger l3;
            const auto [docE, rootE] = makeRecord(13);
            Posting p1 = BurnVillage();
            p1.tags.push_back(RecordRootTag(rootE));
            l3.Post(p1);
            Posting p2 = BurnVillage();
            p2.tags.push_back(RecordRootTag(rootE));
            l3.Post(p2); // second covering entry for same record
            JsonValue::Object o = docE.Members();
            o["seed"] = JsonValue::Int(77); // tamper post-seal
            const JsonValue tampered =
                JsonValue::MakeObject(std::move(o));
            const CrossCheckResult re =
                CrossCheckRecord(l3, tampered);
            Check(re.verdict == CrossVerdict::RecordInconsistent &&
                      re.anchors == 2 && re.flagged == 2 &&
                      l3.Entries()[0].suspect &&
                      l3.Entries()[1].suspect,
                  "every diverging anchor flagged");
        }
    }
    // --- 2.5 review additions ---
    {
        using Potato::Gameplay::BattleRecorder;
        const auto makeRecord = [](std::int64_t seed) {
            JsonValue::Object p;
            p["schema"] =
                JsonValue::String("potato.battle_record/1");
            p["seed"] = JsonValue::Int(seed);
            const std::uint64_t root = BattleRecorder::ComputeRoot(
                JsonValue::MakeObject(p));
            JsonValue::Object o = std::move(p);
            JsonValue::Object in;
            in["root"] =
                JsonValue::Int(static_cast<std::int64_t>(root));
            o["integrity"] = JsonValue::MakeObject(std::move(in));
            return std::pair{JsonValue::MakeObject(std::move(o)),
                             root};
        };

        // integrity.root tampered, payload intact: the anchor is
        // relevant via stored==recomputed; record fails its own
        // seal -> RecordInconsistent, anchor flagged.
        {
            Ledger l;
            const auto [doc, root] = makeRecord(21);
            Posting p = BurnVillage();
            p.tags.push_back(RecordRootTag(root));
            l.Post(p);
            JsonValue::Object o = doc.Members();
            JsonValue::Object in = o["integrity"].Members();
            in["root"] = JsonValue::Int(12345); // corrupt the seal
            o["integrity"] = JsonValue::MakeObject(std::move(in));
            const CrossCheckResult rr =
                CrossCheckRecord(l, JsonValue::MakeObject(
                                        std::move(o)));
            Check(rr.verdict == CrossVerdict::RecordInconsistent &&
                      rr.declaredRoot == 12345 &&
                      rr.recomputedRoot == root &&
                      rr.anchors == 1 && rr.flagged == 1 &&
                      l.Entries()[0].suspect,
                  "corrupt seal -> RecordInconsistent + flag");
        }
        // Self-inconsistent record with NO matching anchor: the
        // broken seal must still surface (not NoAnchor).
        {
            Ledger l;
            const auto [doc, root] = makeRecord(31);
            JsonValue::Object o = doc.Members();
            o["seed"] = JsonValue::Int(99);
            const CrossCheckResult rn =
                CrossCheckRecord(l, JsonValue::MakeObject(
                                        std::move(o)));
            Check(rn.verdict ==
                      CrossVerdict::RecordInconsistent &&
                      rn.anchors == 0 && rn.flagged == 0,
                  "broken-seal record is never NoAnchor");
        }
        // Boundary pin: payload+root BOTH honestly recomputed after
        // tamper -> indistinguishable from an uncovered record ->
        // NoAnchor. OrphanAnchors() finds the orphaned claim.
        {
            Ledger l;
            const auto [doc, root] = makeRecord(41);
            Posting p = BurnVillage();
            p.tags.push_back(RecordRootTag(root));
            l.Post(p);
            JsonValue::Object o = doc.Members();
            o["seed"] = JsonValue::Int(55); // tamper payload
            o.erase("integrity"); // reseal over payload alone
            // attacker honestly reseals:
            JsonValue::Object in;
            in["root"] = JsonValue::Int(static_cast<std::int64_t>(
                BattleRecorder::ComputeRoot(
                    JsonValue::MakeObject(o))));
            o["integrity"] = JsonValue::MakeObject(std::move(in));
            const JsonValue resigned =
                JsonValue::MakeObject(std::move(o));
            const CrossCheckResult ro =
                CrossCheckRecord(l, resigned);
            Check(ro.verdict == CrossVerdict::NoAnchor,
                  "resealed tamper reads as uncovered (boundary)");
            const auto orphans =
                OrphanAnchors(l, {ro.recomputedRoot});
            Check(orphans.size() == 1 && orphans[0] == 0,
                  "orphan anchor surfaces the orphaned claim");
            Check(OrphanAnchors(l, {root}).empty(),
                  "presented root clears its anchor");
        }
        // Forged anchors still cover (claim accuracy is what the
        // chain attests) — but the count is exposed.
        {
            Ledger l;
            const auto [doc, root] = makeRecord(51);
            Posting p = BurnVillage();
            p.tags.push_back(RecordRootTag(root));
            l.Forge(p);
            const CrossCheckResult rf = CrossCheckRecord(l, doc);
            Check(rf.verdict == CrossVerdict::Clean &&
                      rf.forgedAnchors == 1,
                  "forged anchor covers, counted");
        }
        // Idempotency: second check flags nothing new.
        {
            Ledger l;
            const auto [doc, root] = makeRecord(61);
            Posting p = BurnVillage();
            p.tags.push_back(RecordRootTag(root));
            l.Post(p);
            JsonValue::Object o = doc.Members();
            o["seed"] = JsonValue::Int(1);
            const JsonValue t = JsonValue::MakeObject(std::move(o));
            CrossCheckRecord(l, t);
            const CrossCheckResult r2 = CrossCheckRecord(l, t);
            Check(r2.flagged == 0 && l.Entries()[0].suspect,
                  "repeat check does not reflag");
        }
        // Uppercase tag is not canonical.
        std::uint64_t tmp = 0;
        Check(!ParseRecordRootTag("record_root:DEADBEEF00000000",
                                  tmp),
              "uppercase anchor tag rejected");
    }
    // --- AC (4.2): deeds translate; atrocities post tagged ---
    {
        using Potato::Gameplay::SimEvent;
        Ledger l;
        std::vector<SimEvent> evs;
        const auto ev = [&](SimEvent::Kind k, int side, int sq,
                            int param) {
            SimEvent e;
            e.kind = k;
            e.side = side;
            e.squadIndex = sq;
            e.param = param;
            evs.push_back(e);
        };
        ev(SimEvent::Kind::VillageBurned, 0, 0, 3);   // atrocity
        ev(SimEvent::Kind::SquadExecuted, 0, 4, 2);   // atrocity
        ev(SimEvent::Kind::VillageBurned, 1, 1, 0);   // enemy deed
        ev(SimEvent::Kind::CardFired, 0, 0, -1);      // not a deed
        ev(SimEvent::Kind::VillageOccupied, 0, 0, 1);
        ev(SimEvent::Kind::ConvoyRaided, 0, 2, 5);
        const auto n = BookDeeds(l, 0, evs);
        Check(n.ok() && n.value == 4,
              "deeds post; enemy + non-deeds skipped");
        Check(l.Size() == 4, "posting order preserved in chain");
        const LedgerEntry& burn = l.Entries()[0];
        Check(burn.credit.account == Account::Materiel &&
                  burn.credit.amount == 40 &&
                  burn.debit.account == Account::PopularSupport &&
                  burn.debit.amount == 15,
              "burn books +物資40 / -民心15");
        bool burnTagged = false;
        for (const std::string& t : burn.tags) {
            if (t == "atrocity") burnTagged = true;
        }
        Check(burnTagged, "burn carries the atrocity tag");
        const LedgerEntry& exe = l.Entries()[1];
        Check(exe.credit.account == Account::ArmyPrestige &&
                  exe.credit.amount == 5 &&
                  exe.debit.account == Account::PopularSupport &&
                  exe.debit.amount == 10,
              "execution books +軍威5 / -民心10");
        Check(exe.tags.size() == 5 && exe.tags[1] == "victim:4" &&
                  exe.tags[2] == "field:2",
              "execution tags carry victim + place + axes");
        const LedgerEntry& occ = l.Entries()[2];
        Check(occ.credit.account == Account::PopularSupport &&
                  occ.debit.account == Account::Materiel &&
                  occ.tags.size() == 2 && occ.tags[1] == "order:+5",
              "occupation books +民心 / -物資 and feeds 秩序");
        const LedgerEntry& raid = l.Entries()[3];
        Check(raid.tags.size() == 3 && raid.tags[0] == "raid" &&
                  raid.tags[2] == "order:-3",
              "raid tagged raid, not atrocity");
        Check(l.Balance(Account::Materiel) == 40 + 25 - 5 &&
                  l.Balance(Account::PopularSupport) ==
                      -15 - 10 + 10 - 5 &&
                  l.Balance(Account::ArmyPrestige) == 5,
              "deed folds land in balances");
        // Enemy-side deeds don't post even when the fold asks for
        // the player — but they DO post under their own side.
        Ledger le;
        const auto ne = BookDeeds(le, 1, evs);
        Check(ne.ok() && ne.value == 1 && le.Size() == 1,
              "enemy deeds book only under the enemy side");
    }
    // --- AC (4.2): the report acknowledges or visibly omits ---
    {
        using Potato::Gameplay::SimEvent;
        Ledger l;
        SimEvent b;
        b.kind = SimEvent::Kind::VillageBurned;
        b.side = 0; b.param = 3; b.squadIndex = 0;
        const SimEvent evs[1] = {b};
        Check(BookDeeds(l, 0, evs).ok(), "deed booked");
        l.Post(BurnVillage()); // second honest atrocity entry
        Posting plain;
        plain.credit = {Account::Materiel, 1};
        plain.debit = {Account::PopularSupport, 1};
        plain.memo = "routine levy";
        l.Post(plain); // non-atrocity
        const HistorianReport r = RenderHistorianReport(l);
        Check(r.audit.atrocities == 2,
              "audit counts atrocity-tagged entries");
        Check(r.RenderText().find("atrocities: 2") !=
                  std::string::npos,
              "report text acknowledges the count");
        // Omitted atrocity still can't hide: omission confession +
        // audit count both show it.
        l.SetSuspect(0);
        const HistorianReport ro = RenderHistorianReport(l);
        Check(ro.omissions == 1 && ro.audit.atrocities == 2 &&
                  ro.includedSeqs.size() == 2,
              "omitted atrocity still counted in audit");
        const HistorianReport re = RenderHistorianReport(Ledger{});
        Check(re.audit.atrocities == 0, "clean ledger counts zero");
    }
    // --- AC (4.3): accumulators fold; 墮落 ratchets; nothing
    //     writes them ---
    {
        using Potato::Gameplay::SimEvent;
        Ledger l;
        // Populate via the real producer: burn + execute + occupy.
        std::vector<SimEvent> evs;
        const auto ev = [&](SimEvent::Kind k, int side, int sq,
                            int param) {
            SimEvent e;
            e.kind = k; e.side = side; e.squadIndex = sq;
            e.param = param; evs.push_back(e);
        };
        ev(SimEvent::Kind::VillageOccupied, 0, 0, 1);
        ev(SimEvent::Kind::VillageBurned, 0, 0, 3);
        ev(SimEvent::Kind::SquadExecuted, 0, 4, 2);
        Check(BookDeeds(l, 0, evs).ok(), "deeds booked for fold");
        GovernanceAccumulators a = FoldGovernance(l);
        Check(a.popularSupport == 10 - 15 - 10,
              "民心 folds net postings");
        Check(a.order == 5 - 10 - 5, "秩序 folds order:±N tags");
        Check(a.corruption == 15 + 20,
              "墮落 accumulates atrocity contributions");

        // Ratchet: penance drops the running sum, not the peak.
        Posting penance;
        penance.credit = {Account::PopularSupport, 30};
        penance.debit = {Account::Materiel, 30};
        penance.memo = "reparations";
        penance.tags = {"corruption:-25", "order:+8"};
        Check(l.Post(penance).ok(), "penance posts");
        a = FoldGovernance(l);
        Check(a.corruption == 35,
              "墮落 ratchet: peak survives penance");
        Check(a.order == 5 - 10 - 5 + 8,
              "秩序 accepts penance deltas");

        // Forged atrocity still ratchets (suspicion is data).
        Posting forged = BurnVillage();
        forged.tags = {"atrocity", "corruption:+50"};
        Check(l.Forge(forged).ok(), "forged atrocity posts");
        a = FoldGovernance(l);
        Check(a.corruption == 35 - 25 + 50,
              "forged atrocity still ratchets 墮落");
        Check(a.popularSupport == 10 - 15 - 10 + 30 - 15,
              "民心 folds forged postings too");
    }
    // --- AC (4.3): tag grammar edge cases ---
    {
        std::int64_t v = 0;
        Check(ParseGovernanceTag("order:+5", "order", v) && v == 5,
              "signed tag parses");
        Check(ParseGovernanceTag("order:-12", "order", v) &&
                  v == -12,
              "negative tag parses");
        Check(ParseGovernanceTag("order:7", "order", v) && v == 7,
              "bare digits default positive");
        Check(!ParseGovernanceTag("orderly:5", "order", v),
              "axis prefix alone doesn't match");
        Check(!ParseGovernanceTag("order:", "order", v),
              "empty magnitude rejected");
        Check(!ParseGovernanceTag("order:5x", "order", v),
              "trailing garbage rejected");
        Check(!ParseGovernanceTag("order:+", "order", v),
              "lone sign rejected");
        Check(!ParseGovernanceTag(
                  "order:99999999999999999999", "order", v),
              "overflow magnitude rejected");
        Check(!ParseGovernanceTag("order:+5", "corruption", v),
              "wrong axis doesn't fold");
        Check(!ParseGovernanceTag("order:+1001", "order", v),
              "over-cap magnitude rejected");
        Check(ParseGovernanceTag("corruption:+1000", "corruption", v) &&
                  v == 1000,
              "cap boundary accepted");
        Check(!ParseGovernanceTag(":5", "", v),
              "empty axis rejected");
        Check(!ParseGovernanceTag("order:-9223372036854775808",
                                  "order", v),
              "INT64_MIN-magnitude rejected");
        // The fold ignores malformed tags — no exceptions, no UB.
        Ledger l;
        Posting p;
        p.credit = {Account::Materiel, 1};
        p.debit = {Account::PopularSupport, 1};
        p.memo = "x";
        p.tags = {"order:", "order:abc", "order:+99999999999999999999",
                  "corruption:0", "corruption:+3"};
        Check(l.Post(p).ok(), "malformed tags still post (labels)");
        const GovernanceAccumulators a = FoldGovernance(l);
        Check(a.order == 0 && a.corruption == 3,
              "malformed tags fold nothing; valid still counts");
    }
    // --- AC (4.3): poisoning/rail/dedup adversaries ---
    {
        Ledger l;
        const auto post = [&](std::vector<std::string> tags) {
            Posting p;
            p.credit = {Account::Materiel, 1};
            p.debit = {Account::PopularSupport, 1};
            p.memo = "t";
            p.tags = std::move(tags);
            return l.Post(p).ok();
        };
        // Outrun semantics: penance amortizes against the running
        // sum — the peak stands, and new atrocities must push the
        // sum PAST the old peak before the reading moves.
        Check(post({"atrocity", "corruption:+40"}), "atrocity 1");
        Check(post({"corruption:-25"}), "penance posts");
        Check(post({"atrocity", "corruption:+40"}), "atrocity 2");
        GovernanceAccumulators a = FoldGovernance(l);
        Check(a.corruption == 55,
              "penance amortizes: peak 40, sum 55 -> reads 55");
        // Wait — sum after atrocity2 = 40-25+40 = 55 > peak 40:
        // the second atrocity outran the debt.
        Check(a.corruption != 40 && a.corruption != 80,
              "neither naive-max-entry nor naive-sum — the ratchet "
              "is max(running)");
        // A giant penance (bounded by the cap) can still hide fresh
        // atrocities for a while — the debt is real — but the peak
        // is never repaid.
        Check(post({"corruption:-1000"}), "max penance posts");
        Check(post({"atrocity", "corruption:+15"}), "atrocity 3");
        a = FoldGovernance(l);
        Check(a.corruption == 55,
              "deep debt hides new cruelty until outrun; peak kept");
        // Spelling variants dedupe per (axis,value) within an entry;
        // distinct values still stack.
        Check(post({"order:5", "order:+5", "order:05"}),
              "spelling variants post");
        Check(post({"order:5", "order:+10"}),
              "distinct order values post");
        a = FoldGovernance(l);
        Check(a.order == 5 + 5 + 10,
              "variants dedupe per entry; distinct values stack");
        // All-negative history floors at zero — no debt without a deed.
        Ledger l2;
        Posting p2;
        p2.credit = {Account::Materiel, 1};
        p2.debit = {Account::PopularSupport, 1};
        p2.memo = "x";
        p2.tags = {"corruption:-50", "order:-5"};
        Check(l2.Post(p2).ok(), "negative-only entry posts");
        const GovernanceAccumulators a2 = FoldGovernance(l2);
        Check(a2.corruption == 0 && a2.order == -5,
              "corruption floors at 0; order goes negative");
        const GovernanceAccumulators a3 = FoldGovernance(Ledger{});
        Check(a3.popularSupport == 0 && a3.order == 0 &&
                  a3.corruption == 0,
              "empty ledger folds zeros");
    }
    // --- AC (4.4): resolution seals fold into the report,
    // rendered in chronicle voice ---
    {
        Ledger l;
        const auto seal = [&](const char* kind, int ch) {
            Posting p;
            p.credit = {Account::Mandate, 1};
            p.debit = {Account::PopularSupport, 1};
            p.memo = "chapter resolved";
            p.tags = {std::string("resolution:") + kind,
                      "chapter:" + std::to_string(ch)};
            return l.Post(p).ok();
        };
        Check(seal("governance_victory", 0), "gov seal posts");
        Check(seal("battle_victory", 1), "battle seal posts");
        Check(seal("defeat", 2), "defeat seal posts");
        // A forged verdict is read out too — suspicion is data.
        Posting fp;
        fp.credit = {Account::Mandate, 1};
        fp.debit = {Account::PopularSupport, 1};
        fp.memo = "enemy hand";
        fp.tags = {"resolution:governance_victory", "chapter:3"};
        Check(l.Forge(fp).ok(), "forged verdict posts");
        // Lone tags fold nothing — both halves required.
        Posting lp;
        lp.credit = {Account::Mandate, 1};
        lp.debit = {Account::PopularSupport, 1};
        lp.memo = "orphan";
        lp.tags = {"resolution:phantom"};
        Check(l.Post(lp).ok(), "lone resolution tag posts");
        Posting lc;
        lc.credit = {Account::Mandate, 1};
        lc.debit = {Account::PopularSupport, 1};
        lc.memo = "orphan2";
        lc.tags = {"chapter:9"};
        Check(l.Post(lc).ok(), "lone chapter tag posts");

        const HistorianReport r = RenderHistorianReport(l);
        Check(r.resolutions.size() == 4,
              "4.4: four paired seals fold (forged included)");
        Check(r.resolutions[0].chapter == 0 &&
                  r.resolutions[0].kind == "governance_victory",
              "4.4: first resolution note parsed");
        const std::string text = r.RenderText();
        Check(text.find("resolution chapter 0: governance victory") !=
                  std::string::npos &&
                  text.find("\xE4\xBB\xA5\xE6\xB2\xBB\xE7\x82\xBA"
                            "\xE5\x8B\x9D") != std::string::npos,
              "4.4: governance line renders in chronicle voice");
        Check(text.find("resolution chapter 1: battle victory") !=
                  std::string::npos,
              "4.4: battle victory line distinct");
        Check(text.find("resolution chapter 2: defeat") !=
                      std::string::npos &&
                  text.find("rout") == std::string::npos,
              "4.4: defeat line; no battle-rout language");
        Check(text.find("phantom") == std::string::npos &&
                  text.find("chapter 9") == std::string::npos,
              "4.4: orphan tags never render");
        // Forged verdict confessed via the audit counter AND
        // annotated on its own line — doubt is attributable.
        Check(r.audit.forged == 1,
              "4.4: forged verdict counted in audit");
        Check(r.resolutions[3].forged && !r.resolutions[0].forged,
              "4.4: provenance rides the note");
        Check(text.find("resolution chapter 3: governance victory "
                        "\xE2\x80\x94") != std::string::npos &&
                  text.find("(forged)") != std::string::npos,
              "4.4: forged verdict annotated per line");
        // Suspect flag likewise annotates.
        Check(l.SetSuspect(1), "suspect flag set");
        const std::string t2 = RenderHistorianReport(l).RenderText();
        Check(t2.find("battle victory \xE2\x80\x94") !=
                      std::string::npos &&
                  t2.find("(suspect)") != std::string::npos,
              "4.4: suspect verdict annotated per line");

        // Malformed chapter payloads never pair into a verdict.
        Ledger bad;
        const auto badseal = [&](const char* res,
                                 const char* ch) {
            Posting p;
            p.credit = {Account::Mandate, 1};
            p.debit = {Account::PopularSupport, 1};
            p.memo = "x";
            p.tags = {std::string("resolution:") + res,
                      std::string("chapter:") + ch};
            return bad.Post(p).ok();
        };
        Check(badseal("defeat", "3x"), "alnum chapter posts");
        Check(badseal("defeat", "-1"), "signed chapter posts");
        Check(badseal("defeat", ""), "empty chapter posts");
        Check(badseal("defeat", "99999999999"), "11-digit posts");
        Check(badseal("", "4"), "empty resolution posts");
        Check(RenderHistorianReport(bad).resolutions.empty(),
              "4.4: malformed pairs fold nothing");

        // Unknown payloads render sanitized — control bytes can't
        // inject fake report lines.
        Ledger un;
        Posting up;
        up.credit = {Account::Mandate, 1};
        up.debit = {Account::PopularSupport, 1};
        up.memo = "x";
        up.tags = {"resolution:fake\nforged:0", "chapter:5"};
        Check(un.Post(up).ok(), "newline payload posts (tag-level)");
        const std::string ut = RenderHistorianReport(un).RenderText();
        Check(ut.find("fake?forged:0") != std::string::npos &&
                  ut.find("\nforged:0") == std::string::npos,
              "4.4: unknown kind sanitized, no line injection");
    }
    // --- AC (5.3): shrine dedication credits 天命 ---
    {
        using Potato::Gameplay::SimEvent;
        Ledger l;
        std::vector<SimEvent> evs;
        const auto ev = [&](SimEvent::Kind k, int side, int param) {
            SimEvent e;
            e.kind = k;
            e.side = side;
            e.param = param;
            evs.push_back(e);
        };
        ev(SimEvent::Kind::ShrineCaptured, 0, 1); // player shrine
        ev(SimEvent::Kind::ShrineCaptured, 1, 2); // enemy shrine
        const auto n = BookDeeds(l, 0, evs);
        Check(n.ok() && n.value == 1 && l.Size() == 1,
              "5.3: player shrine capture posts once");
        const LedgerEntry& d = l.Entries()[0];
        Check(d.credit.account == Account::Mandate &&
                  d.credit.amount == 10 &&
                  d.debit.account == Account::Materiel &&
                  d.debit.amount == 5,
              "5.3: dedication books +天命10 / -物資5");
        Check(d.tags.size() == 3 && d.tags[0] == "myth" &&
                  d.tags[1] == "field:1" && d.tags[2] == "order:+2",
              "5.3: dedication tagged myth + field + order axis");
        Check(l.Balance(Account::Mandate) == 10,
              "5.3: 天命 balance folds the dedication");
        Check(l.Verify() == nullptr,
              "5.3: myth deed entry chains clean");
    }
    // --- AC (5.3): myth actions debit 天命; insufficiency rejects ---
    {
        Ledger l;
        const auto spend = [&](std::int64_t cost) {
            Posting p;
            p.credit = {Account::PopularSupport, 5}; // awe sink
            p.debit = {Account::Mandate, cost};
            p.memo = "pacify shrine";
            p.tags = {std::string(Ledger::TAG_MYTH)};
            return SpendMandate(l, p);
        };
        Check(!spend(15).ok(),
              "5.3: empty purse cannot buy a myth action");
        Check(l.Size() == 0, "5.3: rejected spend posts nothing");
        // Fund the purse via a dedication deed.
        using Potato::Gameplay::SimEvent;
        SimEvent cap;
        cap.kind = SimEvent::Kind::ShrineCaptured;
        cap.side = 0;
        cap.param = 1;
        const SimEvent evs[1] = {cap};
        Check(BookDeeds(l, 0, evs).ok(), "5.3: dedication funds purse");
        Check(!spend(15).ok(), "5.3: cost above balance rejected");
        Check(l.Size() == 1, "5.3: rejected spend still posts nothing");
        Check(spend(10).ok(),
              "5.3: exact-balance spend lands (boundary)");
        Check(l.Balance(Account::Mandate) == 0 &&
                  l.Balance(Account::PopularSupport) == 5,
              "5.3: spend drains 天命 into the awe sink");
        Check(!spend(1).ok(),
              "5.3: drained purse rejects the next action");
        // Gate hygiene.
        Posting wrong;
        wrong.credit = {Account::Materiel, 1};
        wrong.debit = {Account::Materiel, 1}; // not Mandate
        Check(!SpendMandate(l, wrong).ok(),
              "5.3: non-天命 debit rejected at the gate");
        Check(l.Size() == 2, "5.3: gate rejection posts nothing");
        const auto zero = [&](std::int64_t cost) {
            Posting p;
            p.credit = {Account::Materiel, 1};
            p.debit = {Account::Mandate, cost};
            return SpendMandate(l, p);
        };
        Check(!zero(0).ok() && !zero(-5).ok(),
              "5.3: non-positive cost rejected before posting");
        // Forged grants spend for real — suspicion is an audit
        // overlay, not a balance correction (2.3 contract).
        Posting grant;
        grant.credit = {Account::Mandate, 10};
        grant.debit = {Account::Materiel, 1};
        grant.memo = "miraculous endowment";
        Check(l.Forge(grant).ok(), "5.3: forged grant posts");
        Check(l.Balance(Account::Mandate) == 10,
              "5.3: forged 天命 folds into spendable funds");
        Check(spend(10).ok(),
              "5.3: forged mandate really does spend");
        Check(l.Verify() == nullptr,
              "5.3: forged-funded chain still verifies");
    }
    // --- AC (5.4): catalog prices, SpendMandate pays, log by name ---
    {
        Check(MythActionDefs().size() ==
                  Potato::Gameplay::kMythActionKindCount,
              "5.4: catalog covers every myth action kind");
        const MythActionDef* pac =
            FindMythAction(Potato::Gameplay::MythActionKind::PacifyShrine);
        Check(pac && pac->cost == 15 &&
                  pac->sink == Account::PopularSupport &&
                  pac->sinkAmount == 10 && !pac->needsSquad,
              "5.4: pacify priced 15 天命 -> +10 民心");
        const MythActionDef* pos =
            FindMythAction(Potato::Gameplay::MythActionKind::InvokePossession);
        Check(pos && pos->cost == 20 && pos->needsSquad,
              "5.4: possession priced 20, squad target");
        Ledger l;
        MythLog log;
        using Potato::Gameplay::MythActionKind;
        // Insufficient funds: nothing posts, nothing logs.
        auto r0 = PerformMythAction(l, log, MythActionKind::PacifyShrine,
                                    0, 1, -1);
        Check(!r0.ok() && l.Size() == 0 && log.Size() == 0,
              "5.4: poor purse rejects; no post, no entry");
        // Fund via TWO dedication deeds (each +10; pacify costs 15).
        using Potato::Gameplay::SimEvent;
        SimEvent cap;
        cap.kind = SimEvent::Kind::ShrineCaptured;
        cap.side = 0;
        cap.param = 1;
        SimEvent cap2 = cap;
        cap2.param = 2;
        const SimEvent evs[2] = {cap, cap2};
        Check(BookDeeds(l, 0, evs).ok() && l.Balance(Account::Mandate) == 20,
              "5.4: dedications fund the purse");
        // Wrong target shapes and sides reject before spending.
        Check(!PerformMythAction(l, log, MythActionKind::InvokePossession,
                                 0, 1, -1).ok(),
              "5.4: possession without a squad rejected");
        Check(!PerformMythAction(l, log, MythActionKind::PacifyShrine,
                                 0, 1, 3).ok(),
              "5.4: pacify with a squad target rejected");
        Check(!PerformMythAction(l, log, MythActionKind::PacifyShrine,
                                 2, 1, -1).ok(),
              "5.4: bad side rejected");
        Check(l.Size() == 2 && log.Size() == 0,
              "5.4: rejections never post");
        // Funded action: posts legs, enters the log by name.
        auto r1 = PerformMythAction(l, log, MythActionKind::PacifyShrine,
                                    0, 2, -1);
        Check(r1.ok(), "5.4: funded pacify performs");
        const LedgerEntry& spent = l.Entries()[2];
        Check(spent.debit.account == Account::Mandate &&
                  spent.debit.amount == 15 &&
                  spent.credit.account == Account::PopularSupport &&
                  spent.credit.amount == 10,
              "5.4: spend books -天命15 / +民心10");
        Check(spent.tags.size() == 3 && spent.tags[0] == "myth" &&
                  spent.tags[1] == "action:pacify_shrine" &&
                  spent.tags[2] == "field:2",
              "5.4: spend carries myth+action+field tags");
        Check(log.Size() == 1 && log.Entries()[0].action ==
                                     "pacify_shrine" &&
                  log.Entries()[0].region == 2,
              "5.4: act enters the MythLog by name");
        // 天命 now 5 — a 25-cost ghost army can't be afforded.
        auto r2 = PerformMythAction(l, log,
                                    MythActionKind::RaiseGhostArmy,
                                    0, 2, -1);
        Check(!r2.ok() && l.Size() == 3 && log.Size() == 1,
              "5.4: drained purse blocks the next act");
        Check(l.Verify() == nullptr, "5.4: chain still verifies");
    }
    // --- AC (5.4): MythLog persists, by name ---
    {
        MythLog log;
        Check(log.Record("ghost_army", "陰兵", 0, 1, -1).ok(),
              "5.4: record an entry");
        Check(log.Record("invoke_possession", "降神", 1, 2, 3).ok(),
              "5.4: second entry");
        auto doc = log.ToJson();
        Check(doc.ok(), "5.4: log serializes");
        auto back = MythLog::FromJson(doc.value);
        Check(back.ok() && back.value.Size() == 2 &&
                  back.value.Entries()[1].name ==
                      std::string("降神") &&
                  back.value.Entries()[1].squad == 3,
              "5.4: round-trip preserves names + targets");
        auto bad = Potato::Gameplay::JsonValue::Parse(
            R"({"schema":"potato.mythlog/0","entries":[]})");
        Check(bad.ok() && !MythLog::FromJson(bad.value).ok(),
              "5.4: wrong schema rejected");
        // Non-contiguous seq is rejected — the log is append-ordered.
        auto gap = Potato::Gameplay::JsonValue::Parse(
            R"({"schema":"potato.mythlog/1","entries":[)"
            R"({"seq":1,"action":"x","name":"y","side":0,)"
            R"("region":0,"squad":-1}]})");
        Check(gap.ok() && !MythLog::FromJson(gap.value).ok(),
              "5.4: seq gaps rejected");
    }

    // --- Story 5.5: MythInvasion books as visitation (both legs
    //     reach the player's chronicle regardless of banner) ---
    {
        using Potato::Gameplay::SimEvent;
        const auto inv = [&](int side) {
            SimEvent e;
            e.kind = SimEvent::Kind::MythInvasion;
            e.side = side;
            e.param = 1;
            e.aux = side == -1 ? 1 : 0;
            return e;
        };
        const auto has = [](const LedgerEntry& e,
                            std::string_view tag) {
            return std::find(e.tags.begin(), e.tags.end(), tag) !=
                   e.tags.end();
        };
        // Blessing: the god's host marched for us — bills 天命.
        {
            Ledger l;
            const SimEvent evs[1] = {inv(0)};
            auto r = BookDeeds(l, 0, evs);
            Check(r.ok() && r.value == 1,
                  "5.5: own-banner invasion posts");
            const LedgerEntry& e0 = l.Entries()[0];
            Check(e0.credit.account == Account::ArmyPrestige &&
                      e0.credit.amount == 5 &&
                      e0.debit.account == Account::Mandate &&
                      e0.debit.amount == 5,
                  "5.5: divine aid bills mandate");
            Check(has(e0, Ledger::TAG_MYTH) && has(e0, "invasion") &&
                      has(e0, "field:1"),
                  "5.5: visitation carries myth + field tags");
        }
        // Terror: an enemy-bannered host still lands on our
        // chronicle — visitations bypass the deeds side-gate.
        {
            Ledger l;
            const SimEvent evs[1] = {inv(1)};
            auto r = BookDeeds(l, 0, evs);
            Check(r.ok() && r.value == 1,
                  "5.5: enemy-banner invasion still posts");
            const LedgerEntry& e0 = l.Entries()[0];
            Check(e0.credit.account == Account::ArmyPrestige &&
                      e0.credit.amount == 2 &&
                      e0.debit.account == Account::PopularSupport &&
                      e0.debit.amount == 4,
                  "5.5: terror stiffens ranks, empties hearts");
        }
        // Wild haunting (side -1) books the same terror leg.
        {
            Ledger l;
            const SimEvent evs[1] = {inv(-1)};
            auto r = BookDeeds(l, 0, evs);
            Check(r.ok() && r.value == 1 &&
                      l.Entries()[0].debit.account ==
                          Account::PopularSupport,
                  "5.5: wild haunting books terror");
        }
        // And the side-gate still holds for real deeds — an
        // enemy's arson was never ours to book.
        {
            Ledger l;
            std::vector<SimEvent> evs;
            SimEvent d;
            d.kind = SimEvent::Kind::VillageBurned;
            d.side = 1;
            evs.push_back(d);
            evs.push_back(inv(1));
            auto r = BookDeeds(l, 0, evs);
            Check(r.ok() && r.value == 1 &&
                      l.Entries()[0].memo ==
                          std::string("spirit host terror"),
                  "5.5: enemy arson skipped, visitation booked");
        }
    }

    // --- Story 5.5: LogMythEvents folds invasions by name ---
    {
        using Potato::Gameplay::SimEvent;
        MythLog log;
        std::vector<SimEvent> evs;
        SimEvent inv;
        inv.kind = SimEvent::Kind::MythInvasion;
        inv.side = 1;
        inv.param = 1;
        evs.push_back(inv);
        // A purchased action is already in the log — the fold must
        // not double-book it.
        SimEvent act;
        act.kind = SimEvent::Kind::MythActionInvoked;
        act.side = 0;
        act.param = 1;
        evs.push_back(act);
        auto r = LogMythEvents(log, evs);
        Check(r.ok() && r.value == 1 && log.Size() == 1,
              "5.5: only invasions fold into the log");
        Check(log.Entries()[0].action == std::string("invasion") &&
                  log.Entries()[0].region == 1 &&
                  log.Entries()[0].side == 1,
              "5.5: invasion logged by name, region, banner");
    }

    // --- Story 5.6: folk-register render — hearsay framing,
    //     licensed exaggeration, contradiction without correction ---
    {
        MythLog log;
        // Entry order pins which seq-variant each action renders.
        Check(log.Record("invasion", "神罰", 1, 1, -1).ok() &&
                  log.Record("ghost_army", "陰兵", 0, 1, -1).ok() &&
                  log.Record("pacify_shrine", "安撫", 0, 1, -1).ok() &&
                  log.Record("invoke_possession", "降神", 0, 2, 3)
                      .ok() &&
                  log.Record("invoke_possession", "降神", 0, 2, 4)
                      .ok(),
              "5.6: five entries recorded");
        const std::string folk = RenderMythLog(log);
        // Register check: hearsay markers throughout; no clerk's
        // voice (HistorianReport never says 據說/聽說/有人發誓).
        Check(folk.find("市井傳聞") != std::string::npos &&
                  folk.find("據說") != std::string::npos &&
                  folk.find("聽說") != std::string::npos &&
                  folk.find("有人發誓") != std::string::npos,
              "5.6: folk register markers present");
        // Licensed exaggeration: the ghost host was ONE garrison —
        // the folk telling claims thousands. The ledger knows
        // better; nobody is marked wrong.
        Check(folk.find("數以千計") != std::string::npos,
              "5.6: folk exaggerates the host to thousands");
        // Licensed misattribution: the invasion's host carried the
        // ENEMY banner (side 1) — the folk still claim it for us.
        Check(folk.find("幫咱們") != std::string::npos,
              "5.6: folk misattribute the god's host to home side");
        // Determinism: same log, same telling (seq-keyed variants).
        Check(RenderMythLog(log) == folk,
              "5.6: render is deterministic");
        // Distinct from the clerk's register: a HistorianReport
        // never speaks hearsay.
        {
            Ledger l;
            Posting p;
            p.credit = {Account::Materiel, 10};
            p.debit = {Account::PopularSupport, 5};
            p.memo = "test";
            l.Post(p);
            const std::string report =
                Potato::Campaign::RenderHistorianReport(l)
                    .RenderText();
            Check(report.find("據說") == std::string::npos &&
                      report.find("聽說") == std::string::npos,
                  "5.6: official register carries no hearsay");
        }
        // Unknown actions still enter folklore.
        MythLog odd;
        odd.Record("omen_bird", "異鳥", -1, -1, -1);
        const std::string stray = RenderMythLog(odd);
        Check(stray.find("異鳥") != std::string::npos &&
                  stray.find("怪事") != std::string::npos,
              "5.6: unknown action renders generic folk line");
        // Empty log — even silence is folk-flavored.
        Check(RenderMythLog(MythLog()).find("無傳聞") !=
                  std::string::npos,
              "5.6: empty log renders folk silence");
    }

    // --- Story 5.7: GodStance modulates the 天命 price ---
    {
        using GS = Potato::Gameplay::GodStance;
        using MAK = Potato::Gameplay::MythActionKind;
        Check(EffectiveCost(MAK::PacifyShrine, GS::Neutral) == 15 &&
                  EffectiveCost(MAK::PacifyShrine, GS::Favorable) ==
                      10 &&
                  EffectiveCost(MAK::PacifyShrine, GS::Wrathful) ==
                      25,
              "5.7: stance modulates pacify 15->10/25");
        Check(EffectiveCost(MAK::InvokePossession, GS::Favorable) ==
                  15 &&
                  EffectiveCost(MAK::RaiseGhostArmy, GS::Wrathful) ==
                      35,
              "5.7: discount/surcharge apply across the catalog");
        // Spend-side: a Wrathful region's surcharge makes the base
        // price insufficient — the spend gate sees the REAL cost.
        {
            Ledger l;
            Posting grant;
            grant.credit = {Account::Mandate, 15};
            grant.debit = {Account::Materiel, 15};
            grant.memo = "shrine stipend";
            l.Post(grant);
            MythLog log;
            auto r = PerformMythAction(l, log, MAK::PacifyShrine, 0,
                                       1, -1, GS::Wrathful);
            Check(!r.ok() && log.Size() == 0 &&
                      l.Balance(Account::Mandate) == 15,
                  "5.7: base-price purse can't afford wrathful "
                  "surcharge");
            auto r2 = PerformMythAction(l, log, MAK::PacifyShrine, 0,
                                        1, -1, GS::Neutral);
            Check(r2.ok() && l.Entries()[1].debit.amount == 15,
                  "5.7: neutral ground pays base cost");
        }
        // Favorable discount lands the smaller debit leg.
        {
            Ledger l;
            Posting grant;
            grant.credit = {Account::Mandate, 10};
            grant.debit = {Account::Materiel, 10};
            grant.memo = "stipend";
            l.Post(grant);
            MythLog log;
            auto r = PerformMythAction(l, log, MAK::PacifyShrine, 0,
                                       1, -1, GS::Favorable);
            Check(r.ok() && l.Entries()[1].debit.amount == 10 &&
                      log.Size() == 1,
                  "5.7: favorable ground discounts the debit leg");
        }
        // The narrative surface: stance renders as shrine text,
        // three distinct moods.
        Check(ShrineMoodText(GS::Favorable).find("神悅") !=
                      std::string_view::npos &&
                  ShrineMoodText(GS::Neutral).find("不聞") !=
                      std::string_view::npos &&
                  ShrineMoodText(GS::Wrathful).find("神怒") !=
                      std::string_view::npos,
              "5.7: shrine mood text variants readable");
    }
    // --- Story 6.1: HistorianReport renderer — 史官體 prose ---
    {
        using SE = Potato::Gameplay::SimEvent;
        // Elliptical ladder — the register's only permitted
        // "numbers".
        Check(EllipticalCount(0).empty() &&
                  EllipticalCount(1) == "一二" &&
                  EllipticalCount(2) == "一二" &&
                  EllipticalCount(3) == "數" &&
                  EllipticalCount(9) == "數" &&
                  EllipticalCount(10) == "數十" &&
                  EllipticalCount(49) == "數十" &&
                  EllipticalCount(50) == "百餘" &&
                  EllipticalCount(199) == "百餘" &&
                  EllipticalCount(200) == "不可勝計",
              "6.1: elliptical count bands");

        auto ev = [](SE::Kind k, int side = -1, int aux = 0,
                     int param = -1) {
            SE e;
            e.kind = k;
            e.side = side;
            e.aux = aux;
            e.param = param;
            return e;
        };
        std::vector<SE> evs;
        evs.push_back(ev(SE::Kind::BeatChanged));
        for (int i = 0; i < 5; ++i)
            evs.push_back(ev(SE::Kind::VillageOccupied, 0));
        for (int i = 0; i < 12; ++i)
            evs.push_back(ev(SE::Kind::VillageBurned, 1));
        evs.push_back(ev(SE::Kind::SquadExecuted, 0));
        evs.push_back(ev(SE::Kind::MythActionInvoked, 0, 2));
        evs.push_back(ev(SE::Kind::MythInvasion, -1, 0));
        evs.push_back(ev(SE::Kind::MythInvasion, -1, 2)); // sustain
        evs.push_back(ev(SE::Kind::CardFired, 0));
        // Myth-kind intervention issues are journal noise — the
        // apply event narrates the deed; they must NOT double-count
        // or inflate omissions.
        evs.push_back(ev(SE::Kind::Intervention, 0, 7));
        evs.push_back(ev(SE::Kind::Intervention, 0, 3)); // Probe
        evs.push_back(ev(SE::Kind::ResultDeclared, -1, 0, 0));

        const std::string rep = RenderBattleReport(evs, 0);
        Check(rep.find("史官曰") != std::string::npos,
              "6.1: historian register opens");
        Check(rep.find("邑落來歸者數") != std::string::npos ||
                  rep.find("聚落款附者數") != std::string::npos,
              "6.1: our occupations render with elliptical count");
        Check(rep.find("焚聚落數十") != std::string::npos ||
                  rep.find("廬舍為墟者數十") != std::string::npos,
              "6.1: enemy burns attributed, count elliptical");
        Check(rep.find("降卒不赦，誅者一二") != std::string::npos ||
                  rep.find("俘而弗納，斬者一二") != std::string::npos,
              "6.1: executions narrated as our deed");
        Check(rep.find("發陰兵一二") != std::string::npos ||
                  rep.find("夜召無形之師一二") != std::string::npos,
              "6.1: ghost army invocation narrated");
        Check(rep.find("神罰降者一二") != std::string::npos ||
                  rep.find("降殃者一二") != std::string::npos,
              "6.1: invasion renders as portent");
        Check(rep.find("敵軍盡墨，王師奏捷") != std::string::npos,
              "6.1: wipe victory close line");
        // Omission confession: CardFired + BeatChanged + sustain
        // beat = 3 confessed (myth verb issue is silent, not
        // confessed — its apply narrated the deed).
        Check(rep.find("本報告省略 3 項") != std::string::npos,
              "6.1: omission confession always prints with count");
        // No Arabic tallies outside the confession — the register
        // banishes digits from the prose body.
        {
            std::string body = rep.substr(0, rep.find("本報告省略"));
            bool digit = false;
            for (char c : body) {
                if (c >= '0' && c <= '9') {
                    digit = true;
                    break;
                }
            }
            Check(!digit,
                  "6.1: prose body carries no Arabic numerals");
        }
        // Determinism + clause-pool parity: same list renders
        // identically; a parity-shifted count picks the other
        // clause.
        Check(RenderBattleReport(evs, 0) == rep,
              "6.1: render deterministic");
        {
            std::vector<SE> evs2 = evs;
            evs2.push_back(ev(SE::Kind::VillageOccupied, 0)); // 6
            const std::string rep2 = RenderBattleReport(evs2, 0);
            Check(rep2 != rep &&
                      rep2.find("本報告省略 3 項") !=
                          std::string::npos,
                  "6.1: count parity flips clause variant");
        }
        // Concede/draw/stalemate close lines.
        {
            std::vector<SE> c;
            c.push_back(ev(SE::Kind::ResultDeclared, -1, 1, 0));
            Check(RenderBattleReport(c, 0).find("敵酋請降") !=
                      std::string::npos,
                  "6.1: concede close for our win");
            c.back().param = 1;
            Check(RenderBattleReport(c, 0).find("王師請降") !=
                      std::string::npos,
                  "6.1: concede close for our loss");
            c.back().param = -1;
            c.back().aux = 2;
            Check(RenderBattleReport(c, 0).find("各罷兵去") !=
                      std::string::npos,
                  "6.1: stalemate draw close");
            c.clear();
            Check(RenderBattleReport(c, 0).find("記闕如") !=
                      std::string::npos,
                  "6.1: missing verdict confesses a lacuna");
        }
        // Infiltration direction: aux carries the NEW level — a
        // drop narrates 禳解, a rise narrates portent. Seeded
        // ground needs the carry-in baseline to read truthfully.
        {
            std::vector<SE> m;
            m.push_back(ev(SE::Kind::InfiltrationChanged, -1, 1,
                           3)); // r3: 0→1 rise
            const std::string rm =
                RenderBattleReport(m, 0);
            Check(rm.find("妖氛") != std::string::npos ||
                      rm.find("陰氣所鍾") != std::string::npos,
                  "6.1: rising infiltration narrates as portent");
            // Same event on seeded-level-3 ground is a PACIFY drop.
            std::vector<std::uint8_t> seed = {0, 0, 0, 3};
            const std::string rm2 = RenderBattleReport(m, 0, seed);
            Check(rm2.find("禳解") != std::string::npos ||
                      rm2.find("厲氣漸平") != std::string::npos,
                  "6.1: carry-in baseline reads the drop as "
                  "pacification");
            Check(rm2.find("妖氛") == std::string::npos &&
                      rm2.find("陰氣所鍾") == std::string::npos,
                  "6.1: pacification never misreads as portent");
        }
    }

    // --- Story 6.2: ChapterConventions — 敕命/評斷/懸念 ---
    {
        auto doc = JsonValue::Parse(R"({
            "schema":"potato.narrative/1",
            "chapters":{
                "ch1":{
                    "frontispiece":"爾帥師討逆，毋縱毋暴。",
                    "judgment":{"clean":"敵雖悍，師出有律。",
                                "corrupt":"暴行在冊，雖勝猶辱。"},
                    "cliffhanger":{"blessed":"天命在我，東風將起。",
                                   "barren":"府庫已竭，來日大難。"}},
                "ch2":{
                    "judgment":"敵不可測，慎之。",
                    "cliffhanger":{"default":"烽煙未熄"}}}})");
        Check(doc.ok(), "6.2: narrative doc parses");
        auto convR = ChapterConventions::FromJson(doc.value);
        Check(convR.ok() && convR.value.Size() == 2,
              "6.2: conventions load per-chapter");
        const ChapterConventions& conv = convR.value;

        // Open frame: content frontispiece under the commission
        // header; unknown chapter still gets a generic mandate.
        const std::string open1 = RenderChapterOpen(conv, "ch1");
        Check(open1.find("敕命·ch1") != std::string::npos &&
                  open1.find("毋縱毋暴") != std::string::npos,
              "6.2: frontispiece renders under commission header");
        Check(RenderChapterOpen(conv, "chX").find("毋怠") !=
                  std::string::npos,
              "6.2: unknown chapter still renders a mandate");

        // Close frame: judgment keys on corruption, cliffhanger on
        // mandate balance.
        Ledger clean; // corruption 0 -> Clean; mandate 0 -> Barren
        const std::string closeClean =
            RenderChapterClose(conv, "ch1", clean);
        Check(closeClean.find("敵雖悍") != std::string::npos,
              "6.2: clean ledger reads the clean judgment");
        Check(closeClean.find("府庫已竭") != std::string::npos,
              "6.2: empty mandate reads the barren cliffhanger");
        Check(closeClean.find("且聽下回分解") != std::string::npos,
              "6.2: storyteller hook always closes the chapter");

        Ledger dirty;
        {
            Posting p;
            p.credit = {Account::MartialMerit, 1};
            p.debit = {Account::Materiel, 1};
            p.memo = "atrocity";
            p.tags = {"corruption:+50"};
            dirty.Post(p);
        }
        Check(LedgerMood(dirty) == FrameMood::Corrupt &&
                  RenderChapterClose(conv, "ch1", dirty)
                          .find("暴行在冊") != std::string::npos,
              "6.2: corruption ratchet picks the corrupt verdict");
        Ledger rich;
        {
            Posting p;
            p.credit = {Account::Mandate, 40};
            p.debit = {Account::Materiel, 40};
            p.memo = "shrine tithes";
            rich.Post(p);
        }
        Check(LedgerOmen(rich) == FrameOmen::Blessed &&
                  RenderChapterClose(conv, "ch1", rich)
                          .find("東風將起") != std::string::npos,
              "6.2: mandate surplus picks the blessed hook");

        // Bare-string variant shorthand acts as the "default" key.
        Check(RenderChapterClose(conv, "ch2", clean)
                      .find("敵不可測") != std::string::npos,
              "6.2: string shorthand serves every mood");
        // Missing key falls back: ch1 has no "tainted" judgment —
        // requested -> default -> first entry (map order).
        {
            Ledger tainted;
            Posting p;
            p.credit = {Account::MartialMerit, 1};
            p.debit = {Account::Materiel, 1};
            p.memo = "rough justice";
            p.tags = {"corruption:+20"};
            tainted.Post(p);
            Check(LedgerMood(tainted) == FrameMood::Tainted &&
                      RenderChapterClose(conv, "ch1", tainted)
                              .find("敵雖悍") != std::string::npos,
                  "6.2: missing mood key falls back deterministically");
        }
        // Schema gates: bad version and non-doc reject clean.
        {
            auto bad = JsonValue::Parse(
                R"({"schema":"potato.narrative/2","chapters":{}})");
            Check(bad.ok() &&
                      !ChapterConventions::FromJson(bad.value).ok(),
                  "6.2: wrong schema version rejected");
            auto shape = JsonValue::Parse(
                R"({"schema":"potato.narrative/1","chapters":[]})");
            Check(shape.ok() &&
                      !ChapterConventions::FromJson(shape.value)
                           .ok(),
                  "6.2: bad chapters shape rejected");
        }
        // Canonical round-trip: Emit -> Parse -> FromJson stable.
        {
            const std::string emit = conv.ToJson().Emit();
            auto re = JsonValue::Parse(emit);
            auto conv2 = ChapterConventions::FromJson(re.value);
            Check(re.ok() && conv2.ok() &&
                      conv2.value.Size() == conv.Size() &&
                      conv2.value.ToJson().Emit() == emit,
                  "6.2: ToJson round-trip is canonical");
        }
        // Same ledger -> same frame (deterministic render).
        Check(RenderChapterClose(conv, "ch1", clean) == closeClean,
              "6.2: close frame deterministic");
    }

    // --- Story 6.3: GeneralDossier — hearsay render + revision ---
    {
        GeneralDossier g;
        g.id = "thefox";
        g.prior = RivalPrior::Cunning;
        g.chaptersObserved = 5;
        g.triggers[2] = 4; // EnemyInRegion dominant
        g.triggers[0] = 1;

        const std::string r = RenderDossier(g);
        Check(r.find("聞冊·thefox") != std::string::npos,
              "6.3: dossier names its subject");
        // Three claims -> all three rotating hearsay markers.
        Check(r.find("據聞") != std::string::npos &&
                  r.find("或曰") != std::string::npos &&
                  r.find("傳言") != std::string::npos,
              "6.3: every statement carries a hearsay marker");
        Check(r.find("詭譎") != std::string::npos,
              "6.3: prior renders as temperament rumor");
        Check(r.find("見敵入境必應") != std::string::npos,
              "6.3: dominant trigger renders as habit hearsay");
        Check(r.find("屢聞") != std::string::npos &&
                  r.find("風聞未確") == std::string::npos,
              "6.3: seasoned dossier carries the established tier");
        // No omniscient stats — digits never reach the page.
        {
            bool digit = false;
            for (char c : r) {
                if (c >= '0' && c <= '9') digit = true;
            }
            Check(!digit, "6.3: no omniscient numbers render");
        }
        // Thin dossier — one sighting reads as uncertain rumor.
        GeneralDossier thin;
        thin.id = "newname";
        thin.chaptersObserved = 1;
        const std::string rt = RenderDossier(thin);
        Check(rt.find("風聞未確") != std::string::npos &&
                  rt.find("屢聞") == std::string::npos &&
                  rt.find("其名始聞") != std::string::npos,
              "6.3: thin dossier marked uncertain");
        // Unseen dossier — the name alone, nothing asserted.
        GeneralDossier unseen;
        unseen.id = "ghost";
        Check(DossierClaims(unseen).size() == 1 &&
                  RenderDossier(unseen).find("僅聞其名") !=
                      std::string::npos,
              "6.3: unseen rival renders only the name");
        // Revision seam — proven-wrong claims confess inline.
        const ClaimKind wrong[] = {ClaimKind::Temperament};
        const std::string rr = RenderDossier(g, wrong);
        Check(rr.find("然近事駁之") != std::string::npos &&
                  rr.find("詭譎") != std::string::npos,
              "6.3: proven-wrong claim annotated, not erased");
        Check(RenderDossier(g) == r,
              "6.3: render deterministic");
    }

    // --- Story 6.4: IntelLedger — claim/verdict + feeds ---
    {
        IntelLedger il;
        auto s1 = il.Record("thefox", IntelKind::RivalTemperament,
                            "據聞其人持重", 1);
        auto s2 = il.Record("thefox", IntelKind::RivalHabit,
                            "其斥候謂我軍怯戰", 1);
        auto s3 = il.Record("ford-e3", IntelKind::TerrainIntel,
                            "東道可渡", 1);
        Check(s1.ok() && s2.ok() && s3.ok() && s1.value == 1 &&
                  s2.value == 2 && s3.value == 3,
              "6.4: claims record with dense seqs");
        Check(!il.Record("", IntelKind::TerrainIntel, "x", 1).ok(),
              "6.4: empty subject rejected");
        // Verdict lifecycle — Unresolved -> True|False, terminal.
        Check(il.Resolve(1, IntelVerdict::False).ok(),
              "6.4: claim resolves false");
        Check(!il.Resolve(1, IntelVerdict::True).ok() &&
                  !il.Resolve(1, IntelVerdict::Unresolved).ok() &&
                  !il.Resolve(99, IntelVerdict::True).ok(),
              "6.4: verdicts are terminal, bad seqs fail");
        Check(il.Resolve(2, IntelVerdict::True).ok(),
              "6.4: second claim resolves true");
        // Distortion — resolved-false share; unresolved is
        // uncertain, not discredited.
        Check(il.DistortionFor("thefox") == 50,
              "6.4: distortion = false share of resolved");
        Check(il.DistortionFor("ford-e3") == 0 &&
                  il.DistortionFor("nobody") == 0,
              "6.4: unresolved/unknown subjects have no "
              "distortion");
        // The dossier seam — false rival claims annotate.
        const auto pw = il.ProvenWrongFor("thefox");
        Check(pw.size() == 1 && pw[0] == ClaimKind::Temperament,
              "6.4: false verdict maps to dossier revision");
        // Pending — briefing-time intel still unresolved.
        Check(il.PendingFor("ford-e3").size() == 1 &&
                  il.PendingFor("thefox").empty(),
              "6.4: pending claims listed per subject");
        // RivalDeck feed — distortion erodes counter depth.
        {
            auto cdoc = JsonValue::Parse(
                R"({"cards":[
                    {"id":"counter.always","name":"c",
                     "trigger":{"type":"always"},
                     "action":{"type":"hold"}},
                    {"id":"counter.enemy_in_region","name":"c",
                     "trigger":{"type":"always"},
                     "action":{"type":"hold"}},
                    {"id":"counter.enemy_adjacent","name":"c",
                     "trigger":{"type":"always"},
                     "action":{"type":"hold"}}]})");
            Check(cdoc.ok(), "6.4: counter library parses");
            auto clib = Potato::Gameplay::DoctrineLibrary::
                FromJson(cdoc.value);
            RivalBook book;
            TriggerHistogram h{};
            h[0] = 1;
            h[1] = 2;
            h[2] = 5;
            for (int i = 0; i < 3; ++i) {
                book.RecordChapter("thefox", RivalPrior::Cunning, h);
            }
            const auto full = book.PrepareCounterDeck(
                "thefox", clib.value, 3);
            const auto eroded = book.PrepareCounterDeck(
                "thefox", clib.value, 3, &il);
            Check(full.size() > eroded.size() &&
                      eroded.size() ==
                          static_cast<std::size_t>(3 * 50 / 100),
                  "6.4: distorted intel shallows the counter deck");
        }
        // Persist — potato.intel/1 round-trip keeps verdicts.
        {
            const std::string emit = il.ToJson().Emit();
            auto re = JsonValue::Parse(emit);
            auto il2 = IntelLedger::FromJson(re.value);
            Check(re.ok() && il2.ok() &&
                      il2.value.Size() == 3 &&
                      il2.value.Entry(1)->verdict ==
                          IntelVerdict::False &&
                      il2.value.DistortionFor("thefox") == 50,
                  "6.4: persist round-trip keeps verdicts");
            auto bad = JsonValue::Parse(
                R"({"schema":"potato.intel/2","entries":[]})");
            Check(bad.ok() &&
                      !IntelLedger::FromJson(bad.value).ok(),
                  "6.4: wrong schema rejected");
        }
    }

    // --- Story 12.7: BookDeeds worldNode stamps region:<node>
    //     alongside the map-local field:<n> ---
    {
        using Potato::Gameplay::SimEvent;
        Ledger l;
        SimEvent e;
        e.kind = SimEvent::Kind::VillageBurned;
        e.side = 0;
        e.param = 2; // map-local region 2
        const SimEvent evs[1] = {e};
        const auto n = BookDeeds(l, 0, evs, "longmen");
        Check(n.ok() && n.value == 1, "12.7: worldNode books");
        const LedgerEntry& d = l.Entries()[0];
        const auto has = [](const LedgerEntry& en,
                            std::string_view t) {
            return std::find(en.tags.begin(), en.tags.end(), t) !=
                   en.tags.end();
        };
        Check(has(d, "field:2") && has(d, "region:longmen"),
              "12.7: deed carries field + world region tags");
        // Linear path unchanged — no world node, no region tag.
        Ledger l2;
        const auto n2 = BookDeeds(l2, 0, evs);
        Check(n2.ok() && !has(l2.Entries()[0], "region:longmen") &&
                  has(l2.Entries()[0], "field:2"),
              "12.7: unanchored deeds stay field-only");
        // Oversized node id fails the tag bound.
        const auto tooBig = BookDeeds(
            l2, 0, evs,
            std::string(Ledger::MAX_TAG_LEN, 'x'));
        Check(!tooBig.ok(), "12.7: oversized node id rejected");
    }

    // --- Story 12.7: FoldGovernanceByRegion — per-node folds
    //     from region:<id> tags; field:/untagged excluded ---
    {
        Ledger l;
        const auto post = [&](std::int64_t psCredit,
                              std::int64_t psDebit,
                              std::vector<std::string> tags) {
            Posting p;
            p.credit = {Account::PopularSupport, psCredit};
            p.debit = {Account::PopularSupport, psDebit};
            p.memo = "x";
            p.tags = std::move(tags);
            // Balanced pair: amounts must be >0 and accounts must
            // differ — use Materiel as the counter-leg.
            if (psCredit <= 0) {
                p.credit = {Account::Materiel, 1};
            }
            if (psDebit <= 0) {
                p.debit = {Account::Materiel, 1};
            }
            return l.Post(std::move(p));
        };
        // longmen: +10 民心, order +5 spelled two ways (byte-
        // distinct so Post accepts; value-dedupe folds once),
        // corr:+7.
        Check(post(10, 0,
                   {"region:longmen", "order:+5", "order:05",
                    "corruption:+7"})
                  .ok(),
              "12.7: longmen entry posts");
        // kaifeng: -4 民心 (debit leg), order:-2.
        Check(post(0, 4, {"region:kaifeng", "order:-2"}).ok(),
              "12.7: kaifeng entry posts");
        // Both regions on one entry — folds into each.
        Check(post(3, 0,
                   {"region:longmen", "region:kaifeng"})
                  .ok(),
              "12.7: multi-region entry posts");
        // field: tags are map-local — never world attribution.
        Check(post(99, 0, {"field:2", "order:+9"}).ok(),
              "12.7: field-tagged entry posts");
        // Untagged entries are campaign-scope only.
        Check(post(50, 0, {"order:+1"}).ok(),
              "12.7: untagged entry posts");

        const auto folds = FoldGovernanceByRegion(l);
        Check(folds.size() == 2,
              "12.7: only region:-tagged nodes fold");
        const auto lm = folds.find("longmen");
        const auto kf = folds.find("kaifeng");
        Check(lm != folds.end() && kf != folds.end(),
              "12.7: both world nodes present");
        Check(lm->second.popularSupport == 13 &&
                  lm->second.order == 5 &&
                  lm->second.corruption == 7,
              "12.7: longmen 民心/秩序/墮落 fold");
        Check(kf->second.popularSupport == -4 + 3 &&
                  kf->second.order == -2 &&
                  kf->second.corruption == 0,
              "12.7: kaifeng folds its own entries");
        Check(folds.count("2") == 0,
              "12.7: field:<n> never becomes a world node");
        // Campaign totals still see everything.
        const auto a = FoldGovernance(l);
        Check(a.popularSupport ==
                  l.Balance(Account::PopularSupport),
              "12.7: campaign fold unchanged");
        // std::map ordering: kaifeng < longmen — deterministic
        // id-sorted output.
        Check(folds.begin()->first == "kaifeng",
              "12.7: fold output is id-sorted");
        // The reserved namespaces pin byte-exactly.
        Check(Ledger::TAG_REGION == "region:" &&
                  Ledger::TAG_FIELD == "field:",
              "12.7: tag vocabulary constants pinned");
    }

    // --- Story 6.8: Audit Spread — forged ∪ suspect laid open
    //     with provenance; Scribe testifies only on an
    //     authentic note ---
    {
        using Potato::Campaign::RenderAuditSpread;

        Ledger l;
        Posting clean;
        clean.credit = {Account::Materiel, 10};
        clean.debit = {Account::ArmyPrestige, 10};
        clean.memo = "clean tithe";
        Check(l.Post(clean).ok(), "6.8: clean entry posts"); // seq 0
        Posting forged = BurnVillage();
        Check(l.Forge(forged).ok(), "6.8: forged entry");    // seq 1
        Check(l.Post(clean).ok(), "6.8: entry posts");       // seq 2
        Check(l.SetSuspect(2), "6.8: suspect flag lands");   // seq 2 suspect
        Posting both;
        both.credit = {Account::Mandate, 5};
        both.debit = {Account::MartialMerit, 5};
        both.memo = "enemy hand, questioned";
        Check(l.Forge(both).ok() && l.SetSuspect(3),
              "6.8: forged+suspect entry");                  // seq 3

        // Scribe notes: authentic on seq 1 (testifies),
        // suspect on seq 2 (silent), none on seq 3 (無批).
        auto md = JsonValue::Parse(
            R"json({"schema":"potato.marginalia/1","notes":[
                {"seq":1,"note":"其墨非史官手","suspect":false},
                {"seq":2,"note":"疑筆","suspect":true}
            ]})json");
        auto ms = MarginaliaStore::FromJson(md.value);
        Check(ms.ok() && ms.value.Notes().size() == 2,
              "6.8: marginalia store loads");

        const AuditSpread sp = RenderAuditSpread(l, &ms.value);
        Check(sp.exposed.size() == 3,
              "6.8: forged ∪ suspect expose, clean skipped");
        Check(sp.exposed[0].seq == 1 &&
                  sp.exposed[0].provenance == Provenance::Forged &&
                  !sp.exposed[0].suspect &&
                  sp.exposed[1].seq == 2 &&
                  sp.exposed[1].provenance == Provenance::Honest &&
                  sp.exposed[1].suspect &&
                  sp.exposed[2].seq == 3 &&
                  sp.exposed[2].provenance == Provenance::Forged &&
                  sp.exposed[2].suspect,
              "6.8: rows carry seq + provenance + suspect");
        Check(sp.testimony.size() == 3 &&
                  sp.testimony[0].testifies &&
                  sp.testimony[0].note == "其墨非史官手" &&
                  sp.testimony[1].hasNote &&
                  !sp.testimony[1].testifies &&
                  !sp.testimony[2].hasNote &&
                  !sp.testimony[2].testifies,
              "6.8: testimony follows note authenticity");
        Check(sp.silent == 2, "6.8: silence count");

        const std::string text = sp.RenderText();
        Check(text.find("entry 1 [forged]") != std::string::npos &&
                  text.find("entry 2 [suspect]") !=
                      std::string::npos &&
                  text.find("entry 3 [forged+suspect]") !=
                      std::string::npos,
              "6.8: provenance named per row");
        Check(text.find("批者證曰：其墨非史官手") !=
                  std::string::npos &&
                  text.find("批者默然") != std::string::npos &&
                  text.find("（無批）") != std::string::npos,
              "6.8: testify / silent / absent lines render");
        Check(text.find("silences: 2") != std::string::npos,
              "6.8: silence count renders");
        Check(sp.RenderText() == RenderAuditSpread(l, &ms.value)
                                       .RenderText(),
              "6.8: render deterministic");

        // Null scribe → every row silent; empty ledger → empty.
        Check(RenderAuditSpread(l, nullptr).silent == 3,
              "6.8: no scribe → all silent");
        const AuditSpread empty =
            RenderAuditSpread(Ledger{}, nullptr);
        Check(empty.exposed.empty() && empty.silent == 0,
              "6.8: clean ledger exposes nothing");
    }

    std::printf(failures ? "LEDGER TESTS FAILED: %d\n"
                         : "LEDGER TESTS PASS\n",
                failures);
    return failures ? 1 : 0;
}
