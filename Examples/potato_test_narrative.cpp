// Story 6.5 — NarrativePack registry (potato.narrative/1 in
// assets/narrative/): sorted-dir load, per-file rejection, pack-id
// and chapter-id dedupe, merged conventions view feeding the 6.2
// renderers.
#include "Campaign/Narrative/EndingPage.h"
#include "Campaign/Narrative/Marginalia.h"
#include "Campaign/Narrative/NarrativePack.h"
#include "Campaign/Ledger/Ledger.h"
#include "Gameplay/Json/JsonValue.h"
#include "Gameplay/Myth/Infiltration.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace Potato;
using Potato::Campaign::ChapterConventions;
using Potato::Campaign::Ledger;
using Potato::Campaign::NarrativeLibrary;
using Potato::Campaign::RenderChapterClose;
using Potato::Campaign::RenderChapterOpen;

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

static const char* PACK_A =
    "{\"schema\":\"potato.narrative/1\",\"pack\":\"core\","
    "\"chapters\":{\"ch1\":{\"frontispiece\":\"爾帥師討逆。\","
    "\"judgment\":{\"clean\":\"敵雖悍，師出有律。\"},"
    "\"cliffhanger\":{\"barren\":\"府庫已竭。\"}}}}";

static const char* PACK_B =
    "{\"schema\":\"potato.narrative/1\","
    "\"chapters\":{\"ch2\":{\"judgment\":\"敵不可測，慎之。\"}}}";

int main() {
    const fs::path dir =
        fs::temp_directory_path() / "potato_test_narrative";
    fs::remove_all(dir);
    fs::create_directories(dir);

    // --- Two valid packs merge into one conventions view ---
    Write(dir, "b_pack.json", PACK_B);
    Write(dir, "a_pack.json", PACK_A);
    Write(dir, "not_json.txt", "ignored");
    {
        NarrativeLibrary lib;
        auto r = NarrativeLibrary::Load(dir, lib);
        Check(r.ok && r.rejected.empty(),
              "6.5: clean dir loads with no rejections");
        Check(lib.Packs().size() == 2 &&
                  lib.Packs()[0].id == "core" &&
                  lib.Packs()[1].id == "b_pack",
              "6.5: packs register in sorted-file order");
        Check(lib.Size() == 2 && lib.Find("ch1") != nullptr &&
                  lib.Find("ch2") != nullptr,
              "6.5: merged view finds chapters across packs");
        const std::string open =
            RenderChapterOpen(lib.Conventions(), "ch1");
        Check(open.find("爾帥師討逆") != std::string::npos,
              "6.5: merged conventions render through 6.2 API");
        Ledger clean;
        Check(RenderChapterClose(lib.Conventions(), "ch2", clean)
                      .find("敵不可測") != std::string::npos,
              "6.5: pack-B chapter renders via merged view");
    }

    // --- Per-file rejection: bad-version pack drops, rest load ---
    {
        Write(dir, "badver.json",
              "{\"schema\":\"potato.narrative/2\",\"chapters\":{}}");
        Write(dir, "badshape.json",
              "{\"schema\":\"potato.narrative/1\",\"chapters\":[]}");
        Write(dir, "garbage.json", "not json at all");
        NarrativeLibrary lib;
        auto r = NarrativeLibrary::Load(dir, lib);
        Check(r.ok && lib.Size() == 2,
              "6.5: bad files reject without failing the library");
        Check(r.rejected.size() == 3,
              "6.5: all three bad files reported");
        bool sawSchema = false;
        for (const auto& rej : r.rejected) {
            if (rej.error == "schema") sawSchema = true;
        }
        Check(sawSchema, "6.5: wrong version tagged schema");
    }

    // --- Chapter-id collision across packs rejects the LATER
    //     file (sorted order: a_pack.json before z_dup.json) ---
    {
        Write(dir, "z_dup.json",
              "{\"schema\":\"potato.narrative/1\",\"chapters\":"
              "{\"ch1\":{\"judgment\":\"clash\"}}}");
        NarrativeLibrary lib;
        auto r = NarrativeLibrary::Load(dir, lib);
        Check(r.ok && lib.Size() == 2 &&
                  lib.Find("ch1")->judgment.count("clean") == 1,
              "6.5: duplicate chapter id rejects later file");
        bool sawDup = false;
        for (const auto& rej : r.rejected) {
            if (rej.error == "duplicate") sawDup = true;
        }
        Check(sawDup, "6.5: collision tagged duplicate");
        fs::remove(dir / "z_dup.json");
    }

    // --- Duplicate pack id rejects the later file ---
    {
        Write(dir, "z_dup2.json",
              "{\"schema\":\"potato.narrative/1\",\"pack\":\"core\","
              "\"chapters\":{\"ch9\":{\"judgment\":\"x\"}}}");
        NarrativeLibrary lib;
        auto r = NarrativeLibrary::Load(dir, lib);
        Check(r.ok && lib.Find("ch9") == nullptr,
              "6.5: duplicate pack id rejects later file");
        fs::remove(dir / "z_dup2.json");
    }

    // --- Unreadable dir fails wholesale; empty dir loads empty ---
    {
        NarrativeLibrary lib;
        auto r = NarrativeLibrary::Load(dir / "nonexistent", lib);
        Check(!r.ok && r.error == "io",
              "6.5: missing dir fails wholesale");
        const fs::path empty = dir / "empty";
        fs::create_directories(empty);
        auto r2 = NarrativeLibrary::Load(empty, lib);
        Check(r2.ok && r2.rejected.empty() && lib.Size() == 0,
              "6.5: empty dir loads empty");
    }

    // --- Story 6.6: Scribe marginalia on ledger entries ---
    {
        using Potato::Campaign::AnnotateScribe;
        using Potato::Campaign::Ledger;
        using Potato::Campaign::MarginaliaStore;
        using Potato::Campaign::Posting;
        using Potato::Campaign::RenderMarginalia;
        using Potato::Campaign::ScribeWorthy;

        Ledger l;
        const auto post = [&](const char* memo,
                              std::vector<std::string> tags) {
            Posting p;
            p.credit = {Potato::Campaign::Account::Materiel, 5};
            p.debit = {Potato::Campaign::Account::ArmyPrestige, 5};
            p.memo = memo;
            p.tags = std::move(tags);
            return l.Post(std::move(p)).ok();
        };
        Check(post("supply run", {}), "6.6: plain entry posts");
        Check(post("burned village", {"atrocity"}),
              "6.6: atrocity posts");
        Check(post("march a -> b", {"march"}), "6.6: march posts");
        Check(post("close", {"resolution:victory"}),
              "6.6: resolution posts");

        // ScribeWorthy: tagged entries only.
        Check(!ScribeWorthy(l.Entries()[0]) &&
                  ScribeWorthy(l.Entries()[1]) &&
                  ScribeWorthy(l.Entries()[2]) &&
                  ScribeWorthy(l.Entries()[3]),
              "6.6: worthiness keys on salient tags");

        MarginaliaStore store;
        auto n0 = AnnotateScribe(l, store, 0);
        Check(n0.ok() && n0.value == 0 && store.Notes().empty(),
              "6.6: zero cap annotates nothing");
        auto n1 = AnnotateScribe(l, store, 2);
        Check(n1.ok() && n1.value == 2 &&
                  store.Notes().size() == 2 &&
                  store.Notes()[0].seq == 1 &&
                  store.Notes()[1].seq == 2,
              "6.6: cap-limited pass annotates worthy seqs in "
              "order");
        auto n2 = AnnotateScribe(l, store, 10);
        Check(n2.ok() && n2.value == 1 &&
                  store.Notes().size() == 3,
              "6.6: idempotent — annotated entries never repeat");
        Check(store.Find(0) == nullptr &&
                  store.Find(1) != nullptr &&
                  store.Find(1)->note.size() > 0,
              "6.6: plain entry unannotated, worthy ones noted");
        // Deterministic composition: same ledger, same notes.
        {
            MarginaliaStore s2;
            auto n3 = AnnotateScribe(l, s2, 10);
            Check(n3.ok() && s2.Notes().size() == 3 &&
                      s2.Notes()[0].note == store.Notes()[0].note &&
                      s2.Notes()[1].note == store.Notes()[1].note,
                  "6.6: composition deterministic");
        }

        // Render: note lines + authenticity flag state.
        const std::string base = RenderMarginalia(store, l);
        Check(base.find("批〔1〕") != std::string::npos &&
                  base.find("（疑）") == std::string::npos,
              "6.6: notes render clean when unsuspected");
        Check(store.SetSuspect(2) && !store.SetSuspect(99),
              "6.6: suspect overlay sets only on real notes");
        const std::string flagged = RenderMarginalia(store, l);
        Check(flagged.find("批〔2〕") != std::string::npos &&
                  flagged.find("（疑）") != std::string::npos,
              "6.6: suspect note carries the flag");
        // Underlying-entry suspect taints the note above it.
        Check(l.SetSuspect(1), "6.6: ledger suspect flag sets");
        const std::string tainted = RenderMarginalia(store, l);
        const auto p1 = tainted.find("批〔1〕");
        Check(p1 != std::string::npos &&
                  tainted.find("（疑）", p1) != std::string::npos,
              "6.6: suspect entry taints its note");

        // Wire round-trip.
        auto j = store.ToJson();
        Check(j.ok(), "6.6: store serializes");
        auto doc = Potato::Gameplay::JsonValue::Parse(
            j.value.Emit());
        Check(doc.ok(), "6.6: emitted doc re-parses");
        auto st2 = MarginaliaStore::FromJson(doc.value);
        Check(st2.ok() && st2.value.Notes().size() == 3 &&
                  st2.value.Notes()[1].suspect,
              "6.6: suspect flag survives round-trip");
        // Schema gate + dedupe pins.
        {
            auto bad = Potato::Gameplay::JsonValue::Parse(
                "{\"schema\":\"potato.marginalia/2\",\"notes\":[]}");
            Check(bad.ok() &&
                      !MarginaliaStore::FromJson(bad.value).ok(),
                  "6.6: wrong schema rejected");
            auto dup = Potato::Gameplay::JsonValue::Parse(
                "{\"schema\":\"potato.marginalia/1\",\"notes\":"
                "[{\"seq\":1,\"note\":\"a\"},{\"seq\":1,"
                "\"note\":\"b\",\"suspect\":true}]}");
            auto d2 = MarginaliaStore::FromJson(dup.value);
            Check(d2.ok() && d2.value.Notes().size() == 1 &&
                      d2.value.Notes()[0].note == "a" &&
                      !d2.value.Notes()[0].suspect,
                  "6.6: duplicate seq dedupes first-wins");
            auto beyond = Potato::Gameplay::JsonValue::Parse(
                "{\"schema\":\"potato.marginalia/1\",\"notes\":"
                "[{\"seq\":999,\"note\":\"or\"}]}");
            Check(MarginaliaStore::FromJson(beyond.value).ok(),
                  "6.6: notes beyond ledger head tolerated");
        }
    }

    // --- Story 6.7: document-variant rendering — clause pools
    //     keyed on ledger mood/omen + GodStance ---
    {
        using Potato::Campaign::Account;
        using Potato::Campaign::Posting;
        using Potato::Campaign::RenderDocVariant;
        namespace gs = Potato::Gameplay;

        const fs::path vdir = dir / "v";
        fs::create_directories(vdir);
        Write(vdir, "var.json",
              "{\"schema\":\"potato.narrative/1\",\"pack\":\"v\","
              "\"chapters\":{\"vch\":{\"judgment\":\"判。\"}},"
              "\"variants\":{"
              "\"report\":{\"verdict\":{"
              "\"mood\":{\"clean\":[\"其政清，師出有律。\"],"
              "\"corrupt\":[\"冊多隱飾，史筆難直。\"]},"
              "\"stance\":{\"wrathful\":\"神人共憤。\"}}},"
              "\"dossier\":{\"temper\":{\"omen\":{"
              "\"barren\":[\"庫竭，人心思變。\","
              "\"糧盡，道路以目。\"]}}}}}");

        NarrativeLibrary vlib;
        auto vr = NarrativeLibrary::Load(vdir, vlib);
        Check(vr.ok && vr.rejected.empty() &&
                  vlib.Conventions().Variants("report") !=
                      nullptr &&
                  vlib.Conventions().Variants("dossier") !=
                      nullptr &&
                  vlib.Conventions().Variants("nope") == nullptr,
              "6.7: variant docs parse and register");

        // Ledger fixtures — corruption ratchet keys mood,
        // mandate balance keys omen.
        auto mkLedger = [](std::int64_t corrupt,
                           std::int64_t mandate) {
            Ledger l;
            if (corrupt > 0) {
                Posting p;
                p.credit = {Account::Materiel, 1};
                p.debit = {Account::ArmyPrestige, 1};
                p.memo = "x";
                p.tags = {"corruption:+" +
                          std::to_string(corrupt)};
                l.Post(std::move(p));
            }
            if (mandate > 0) {
                Posting g;
                g.credit = {Account::Mandate, mandate};
                g.debit = {Account::Materiel, mandate};
                g.memo = "g";
                l.Post(std::move(g));
            }
            return l;
        };
        Ledger cleanL = mkLedger(0, 0);
        Ledger dirtyL = mkLedger(50, 0);
        Ledger richL = mkLedger(0, 40); // blessed — uncovered

        // Mood axis: clean vs corrupt select different clauses.
        const std::string vClean = RenderDocVariant(
            vlib.Conventions(), "report", "verdict", cleanL,
            gs::GodStance::Neutral);
        Check(vClean.find("其政清") != std::string::npos &&
                  vClean.find("冊多隱飾") == std::string::npos,
              "6.7: clean ledger picks the clean clause");
        const std::string vDirty = RenderDocVariant(
            vlib.Conventions(), "report", "verdict", dirtyL,
            gs::GodStance::Neutral);
        Check(vDirty.find("冊多隱飾") != std::string::npos,
              "6.7: corrupt ledger picks the corrupt clause");

        // Stance axis: uncovered state stays silent (no
        // first-entry fallback — silence, not the wrong voice).
        Check(vClean.find("神人共憤") == std::string::npos,
              "6.7: neutral stance silent on wrathful pool");
        const std::string vMad = RenderDocVariant(
            vlib.Conventions(), "report", "verdict", dirtyL,
            gs::GodStance::Wrathful);
        Check(vMad.find("冊多隱飾") != std::string::npos &&
                  vMad.find("神人共憤") != std::string::npos &&
                  vMad.find("冊多隱飾") < vMad.find("神人共憤"),
              "6.7: axes compose in mood→stance order");

        // Omen axis on a different doc/slot; pool pick is
        // deterministic and salt-sensitive.
        const std::string d1 = RenderDocVariant(
            vlib.Conventions(), "dossier", "temper", cleanL,
            gs::GodStance::Neutral, 0);
        const std::string d2 = RenderDocVariant(
            vlib.Conventions(), "dossier", "temper", cleanL,
            gs::GodStance::Neutral, 0);
        Check(d1 == d2 && !d1.empty(),
              "6.7: omen pick deterministic");
        Check(d1.find("庫竭") != std::string::npos ||
                  d1.find("糧盡") != std::string::npos,
              "6.7: barren omen picks from the pool");

        // Absent doc / slot / axis → empty, not a crash.
        Check(RenderDocVariant(vlib.Conventions(), "nope",
                               "verdict", cleanL,
                               gs::GodStance::Neutral)
                  .empty() &&
                  RenderDocVariant(vlib.Conventions(), "report",
                                   "nope", cleanL,
                                   gs::GodStance::Neutral)
                      .empty() &&
                  RenderDocVariant(vlib.Conventions(), "dossier",
                                   "temper", richL,
                                   gs::GodStance::Neutral)
                      .empty(),
              "6.7: absent doc/slot/state renders nothing");

        // Bare-string pool shorthand works (stance above).
        // Round-trip: ToJson → re-parse preserves pools.
        auto doc2 = Potato::Gameplay::JsonValue::Parse(
            vlib.Conventions().ToJson().Emit());
        Check(doc2.ok(), "6.7: conventions doc re-parses");
        auto conv2 = ChapterConventions::FromJson(doc2.value);
        Check(conv2.ok() &&
                  conv2.value.Variants("report") != nullptr,
              "6.7: variants survive ToJson round-trip");

        // Rejection surface: bad axis name, empty pool,
        // over-cap docs — each rejects its file only.
        Write(vdir, "badaxis.json",
              "{\"schema\":\"potato.narrative/1\",\"chapters\":{}"
              ",\"variants\":{\"r\":{\"s\":{\"vibes\":{"
              "\"x\":\"y\"}}}}}");
        Write(vdir, "emptypool.json",
              "{\"schema\":\"potato.narrative/1\",\"chapters\":{}"
              ",\"variants\":{\"r\":{\"s\":{\"mood\":{"
              "\"clean\":[]}}}}}");
        NarrativeLibrary vlib2;
        auto vr2 = NarrativeLibrary::Load(vdir, vlib2);
        Check(vr2.ok && vr2.rejected.size() == 2 &&
                  vlib2.Conventions().Variants("report") !=
                      nullptr,
              "6.7: bad variant files reject in isolation");

        // Doc-name collision across packs rejects the LATER
        // file (sorted order: var.json's "report" wins).
        Write(vdir, "zz_dup.json",
              "{\"schema\":\"potato.narrative/1\",\"chapters\":{}"
              ",\"variants\":{\"report\":{\"verdict\":{"
              "\"mood\":{\"clean\":\"late\"}}}}}");
        NarrativeLibrary vlib3;
        auto vr3 = NarrativeLibrary::Load(vdir, vlib3);
        Check(vr3.ok &&
                  RenderDocVariant(vlib3.Conventions(),
                                   "report", "verdict", cleanL,
                                   gs::GodStance::Neutral)
                          .find("其政清") != std::string::npos,
              "6.7: duplicate doc name rejects later pack");
    }

    // --- Story 6.7: document variant rendering ---
    {
        using Potato::Campaign::Account;
        using Potato::Campaign::Ledger;
        using Potato::Campaign::Posting;
        using Potato::Campaign::RenderDocVariant;
        using Potato::Gameplay::GodStance;

        // Earlier rejection fixtures still sit in `dir` — every
        // load below reports the same 3 stale rejects; only new
        // failures matter.
        Write(dir, "d_vars.json",
              "{\"schema\":\"potato.narrative/1\",\"pack\":\"vars\","
              "\"chapters\":{},"
              "\"variants\":{\"report\":{\"verdict\":{"
              "\"mood\":{\"clean\":\"師出有律。\","
              "\"corrupt\":[\"冊多隱飾。\",\"暴名遠播。\"]},"
              "\"omen\":{\"default\":\"天命未卜。\"},"
              "\"stance\":{\"wrathful\":\"神人共憤。\"}},"
              "\"temper\":{\"stance\":{\"favorable\":\"神悅。\","
              "\"default\":\"神不語。\"}}}}}");

        NarrativeLibrary lib;
        auto r = NarrativeLibrary::Load(dir, lib);
        Check(r.ok &&
                  lib.Conventions().Variants("report") != nullptr,
              "6.7: variants section loads and registers");

        // Clean ledger: mood=clean, omen=barren (mandate 0),
        // stance passed in.
        Ledger l;
        const std::string v0 = RenderDocVariant(
            lib.Conventions(), "report", "verdict", l,
            GodStance::Neutral);
        Check(v0.find("師出有律。") != std::string::npos &&
                  v0.find("天命未卜。") != std::string::npos,
              "6.7: state keys select their clauses");
        // Neutral stance has no pool key and no default — a
        // variant clause is color: uncovered states stay silent
        // rather than borrowing the wrong key's voice.
        Check(v0.find("神人共憤") == std::string::npos &&
                  v0.find("天命未卜") != std::string::npos,
              "6.7: uncovered axis key stays silent, default "
              "still falls back");
        const std::string v1 = RenderDocVariant(
            lib.Conventions(), "report", "verdict", l,
            GodStance::Wrathful);
        Check(v1.find("神人共憤。") != std::string::npos &&
                  v1 != v0,
              "6.7: GodStance moves the stance clause");

        // Corruption ratchet flips the mood clause.
        {
            Posting p;
            p.credit = {Account::ArmyPrestige, 5};
            p.debit = {Account::Materiel, 5};
            p.memo = "deed";
            p.tags = {"atrocity", "corruption:+45"};
            Check(l.Post(std::move(p)).ok(),
                  "6.7: corruption posting lands");
        }
        const std::string v2 = RenderDocVariant(
            lib.Conventions(), "report", "verdict", l,
            GodStance::Neutral);
        Check(v2.find("師出有律") == std::string::npos &&
                  (v2.find("冊多隱飾") != std::string::npos ||
                   v2.find("暴名遠播") != std::string::npos),
              "6.7: mood threshold flips the clause");
        Check(RenderDocVariant(lib.Conventions(), "report",
                               "verdict", l,
                               GodStance::Neutral) == v2,
              "6.7: same inputs -> same text");
        // Salted pick: over enough salts the two-clause pool
        // shows both members; each render is a pool clause.
        {
            bool sawA = false, sawB = false;
            for (std::uint64_t s = 0; s < 8; ++s) {
                const std::string vs = RenderDocVariant(
                    lib.Conventions(), "report", "verdict", l,
                    GodStance::Neutral, s);
                if (vs.find("冊多隱飾") != std::string::npos) {
                    sawA = true;
                }
                if (vs.find("暴名遠播") != std::string::npos) {
                    sawB = true;
                }
            }
            Check(sawA && sawB,
                  "6.7: salt reshuffles the pool pick");
        }
        // Multi-slot doc: temper has only a stance axis —
        // absent axes emit nothing.
        const std::string t0 = RenderDocVariant(
            lib.Conventions(), "report", "temper", l,
            GodStance::Favorable);
        Check(t0 == "神悅。\n",
              "6.7: absent axes stay silent in mixed slots");
        Check(RenderDocVariant(lib.Conventions(), "report",
                               "temper", l,
                               GodStance::Neutral) ==
                  "神不語。\n",
              "6.7: default key covers unlisted states");
        // Unknown doc/slot render empty.
        Check(RenderDocVariant(lib.Conventions(), "nope",
                               "verdict", l,
                               GodStance::Neutral)
                  .empty() &&
                  RenderDocVariant(lib.Conventions(), "report",
                                   "nope", l,
                                   GodStance::Neutral)
                      .empty(),
              "6.7: unknown doc/slot render empty");

        // Strict axis vocabulary: an unknown axis rejects the
        // whole pack file, siblings still load.
        Write(dir, "z_badaxis.json",
              "{\"schema\":\"potato.narrative/1\",\"chapters\":{},"
              "\"variants\":{\"bad\":{\"s\":{\"luck\":"
              "{\"x\":\"y\"}}}}}");
        {
            NarrativeLibrary lib2;
            auto r2 = NarrativeLibrary::Load(dir, lib2);
            Check(r2.ok &&
                      lib2.Conventions().Variants("report") !=
                          nullptr,
                  "6.7: bad-axis pack drops, siblings load");
            bool sawRej = false;
            for (const auto& rej : r2.rejected) {
                if (rej.path.filename() == "z_badaxis.json") {
                    sawRej = true;
                }
            }
            Check(sawRej, "6.7: unknown axis rejected");
        }
        fs::remove(dir / "z_badaxis.json");

        // Variant doc names are one namespace — a later pack
        // repeating "report" rejects the file.
        Write(dir, "z_dupvar.json",
              "{\"schema\":\"potato.narrative/1\",\"chapters\":{},"
              "\"variants\":{\"report\":{\"s\":{\"mood\":"
              "{\"clean\":\"x\"}}}}}");
        {
            NarrativeLibrary lib3;
            auto r3 = NarrativeLibrary::Load(dir, lib3);
            bool sawDup = false;
            for (const auto& rej : r3.rejected) {
                if (rej.error == "duplicate") sawDup = true;
            }
            Check(sawDup,
                  "6.7: duplicate doc name rejects later file");
            Check(lib3.Conventions().Variants("report") !=
                      nullptr,
                  "6.7: earlier doc survives the rejection");
        }
        fs::remove(dir / "z_dupvar.json");

        // Wire round-trip: emitted doc re-parses with pools
        // intact (ToJson omits empty sections — variants are
        // present here).
        {
            const auto j =
                Potato::Gameplay::JsonValue::Parse(
                    lib.Conventions().ToJson().Emit());
            Check(j.ok(), "6.7: emitted doc re-parses");
            auto conv2 = ChapterConventions::FromJson(j.value);
            Check(conv2.ok() &&
                      RenderDocVariant(conv2.value, "report",
                                       "verdict", l,
                                       GodStance::Wrathful) ==
                          RenderDocVariant(
                              lib.Conventions(), "report",
                              "verdict", l, GodStance::Wrathful),
                  "6.7: variants survive the wire round-trip");
        }
    }

    // ===== Story 6.9 — ending folio: four voices =====
    {
        using Potato::Campaign::Account;
        using Potato::Campaign::EndingVoice;
        using Potato::Campaign::EndingVoiceFromName;
        using Potato::Campaign::EndingVoiceName;
        using Potato::Campaign::FoldEndingVoice;
        using Potato::Campaign::Posting;
        using Potato::Campaign::Provenance;
        using Potato::Campaign::RenderEndingPage;

        auto post = [](Ledger& l, const char* memo,
                       std::vector<std::string> tags = {}) {
            Posting p;
            p.credit = {Account::Materiel, 10};
            p.debit = {Account::ArmyPrestige, 5};
            p.memo = memo;
            p.tags = std::move(tags);
            return l.Post(std::move(p));
        };

        // Clean chronicle → believed.
        {
            Ledger l;
            post(l, "ordinary march");
            post(l, "relief works");
            Check(FoldEndingVoice(l) == EndingVoice::Believed,
                  "6.9: clean ledger closes believed");
            Check(RenderEndingPage(l).find("信史") !=
                      std::string::npos,
                  "6.9: believed folio renders 信史");
        }

        // Suspect flags, no forgery → doubted.
        {
            Ledger l;
            post(l, "a deed");
            post(l, "another deed");
            Check(l.SetSuspect(0), "6.9: flag sets");
            Check(FoldEndingVoice(l) == EndingVoice::Doubted,
                  "6.9: suspect-only ledger closes doubted");
            Check(RenderEndingPage(l).find("疑筆1條") !=
                      std::string::npos,
                  "6.9: doubted folio confesses the count");
        }

        // Forged row present → forged (beats suspicion too).
        {
            Ledger l;
            post(l, "honest");
            l.SetSuspect(0);
            Posting f;
            f.credit = {Account::Mandate, 9};
            f.debit = {Account::Materiel, 9};
            f.memo = "not the chronicler's hand";
            Check(l.Forge(std::move(f)).ok(),
                  "6.9: forge injects");
            Check(FoldEndingVoice(l) == EndingVoice::Forged,
                  "6.9: forged row outranks suspect");
            Check(RenderEndingPage(l).find("偽筆1條") !=
                      std::string::npos,
                  "6.9: forged folio confesses the count");
        }

        // Abandoned: empty book, or the ratchet drowning the
        // hand. A broken chain is the same verdict (Verify is
        // the first gate — an unreadable book is abandoned
        // whatever it claims).
        {
            Check(FoldEndingVoice(Ledger{}) ==
                      EndingVoice::Abandoned,
                  "6.9: empty ledger closes abandoned");
            Ledger l;
            post(l, "the cruelty that ended it",
                 {"corruption:+150"});
            Check(FoldEndingVoice(l) == EndingVoice::Abandoned,
                  "6.9: corruption at the abandon bound closes "
                  "絕筆");
            Check(RenderEndingPage(l).find("絕筆") !=
                      std::string::npos,
                  "6.9: abandoned folio renders 絕筆");
        }

        // Voice names round-trip; unknown rejects.
        {
            EndingVoice v;
            Check(EndingVoiceFromName("abandoned", v) &&
                      v == EndingVoice::Abandoned,
                  "6.9: voice name parses");
            Check(!EndingVoiceFromName("triumphant", v),
                  "6.9: unknown voice rejects");
            Check(std::string(EndingVoiceName(
                      EndingVoice::Doubted)) == "doubted",
                  "6.9: voice name spells doubted");
        }

        // Determinism: same ledger → same folio bytes.
        {
            Ledger l;
            post(l, "steady hand");
            Check(RenderEndingPage(l) == RenderEndingPage(l),
                  "6.9: render deterministic");
        }
    }

    fs::remove_all(dir);

    std::printf(failures ? "NARRATIVE TESTS FAILED: %d\n"
                         : "NARRATIVE TESTS PASS\n",
                failures);
    return failures ? 1 : 0;
}
