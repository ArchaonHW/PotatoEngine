// Story 10.1 — bencao codex entry registry (potato.bencao/1):
// per-file isolated load, six-field anatomy, unlock manifest
// validation, annotation-key guard, canonical ordering.
// Story 10.2 — unlock engine (potato.bencao_state/1): six-kind
// trigger evaluation over read-only signals, idempotent unlocks,
// pending 補鈔 FIFO, codex state round-trip.
// Story 10.3 — 補鈔 delivery (potato.buchao/1): bounded drain,
// clause-pool 批註 composition, suspect flag, store round-trip.
#include "Campaign/Narrative/BattleReport.h"
#include "Campaign/Narrative/Bencao.h"
#include "Campaign/Narrative/BencaoCodex.h"
#include "Campaign/Narrative/BencaoCite.h"
#include "Campaign/Narrative/Buchao.h"
#include "Campaign/Narrative/Codex.h"
#include "Campaign/Ledger/Ledger.h"
#include "Campaign/Myth/MythLog.h"
#include "Campaign/Myth/MythState.h"
#include "Gameplay/Doctrine/Doctrine.h" // SimEvent
#include "Gameplay/Json/JsonValue.h"
#include "Gameplay/Map/BattleMap.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace Potato;
using Potato::Campaign::Account;
using Potato::Campaign::BencaoCategory;
using Potato::Campaign::BencaoCodex;
using Potato::Campaign::BencaoEntry;
using Potato::Campaign::BencaoLibrary;
using Potato::Campaign::BuchaoStore;
using Potato::Campaign::CodexSignals;
using Potato::Campaign::ComposeMarginalia;
using Potato::Campaign::DeliverBuchao;
using Potato::Campaign::Ledger;
using Potato::Campaign::MythLog;
using Potato::Campaign::MythState;
using Potato::Campaign::PendingPage;
using Potato::Campaign::Posting;
using Potato::Campaign::RenderCodex;
using Potato::Campaign::ResolveBencaoUnlocks;
using Potato::Campaign::TerrainFlagsOf;
using Potato::Campaign::UnlockKind;
using Potato::Gameplay::BattleMap;
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

// A minimal valid entry; `kind`/`extra` let callers vary the
// unlock block without duplicating the whole fixture.
static std::string Entry(const char* id, const char* cat,
                         const char* unlock) {
    std::string s =
        "{\"schema\":\"potato.bencao/1\",\"id\":\"";
    s += id;
    s += "\",\"category\":\"";
    s += cat;
    s += "\",\"name\":\"三七\",\"nature\":\"甘、微苦，溫。\","
         "\"indications\":\"止血散血，定痛。\","
         "\"source\":\"本草綱目·卷十二\",\"unlock\":";
    s += unlock;
    s += "}";
    return s;
}

int main() {
    const fs::path dir =
        fs::temp_directory_path() / "potato_test_bencao";
    fs::remove_all(dir);
    fs::create_directories(dir);

    // --- valid load: canonical order, aliases/origin optional ---
    {
        Write(dir, "a.json",
              Entry("sanqi", "shancao",
                    "{\"kind\":\"ledger_tag\",\"tag\":\"first_casualty\"}")
                  .c_str());
        Write(dir, "b.json",
              Entry("fuzi", "ducao",
                    "{\"kind\":\"corruption\",\"at_least\":1}")
                  .c_str());
        Write(dir, "c.json",
              Entry("lingzhi", "jinshi",
                    "{\"kind\":\"myth_state\",\"state\":\"pacify_shrine\"}")
                  .c_str());
        Write(dir, "note.txt", "not json"); // skipped ext

        BencaoLibrary lib;
        const auto res = BencaoLibrary::Load(dir, lib);
        Check(res.ok, "10.1: library load succeeds");
        Check(res.rejected.empty(), "10.1: no rejections");
        Check(lib.Size() == 3, "10.1: valid entries registered");
        // Canonical order: category ordinal first (shancao < ducao
        // < jinshi) regardless of filename.
        Check(lib.Entries()[0].id == "sanqi" &&
                  lib.Entries()[1].id == "fuzi" &&
                  lib.Entries()[2].id == "lingzhi",
              "10.1: canonical category-then-id order");
        Check(lib.Find("sanqi") &&
                  lib.Find("sanqi")->category ==
                      BencaoCategory::Shancao &&
                  lib.Find("sanqi")->unlockKind ==
                      UnlockKind::LedgerTag &&
                  lib.Find("sanqi")->unlockParam == "first_casualty",
              "10.1: Find + manifest fields parse");
        Check(lib.Find("nobody") == nullptr, "10.1: Find misses");
    }

    // --- rejections: schema, fields, kinds, dup id, guard ---
    {
        Write(dir, "bad_schema.json",
              "{\"schema\":\"potato.bencao/2\",\"id\":\"x\"}");
        Write(dir, "no_nature.json",
              "{\"schema\":\"potato.bencao/1\",\"id\":\"nonat\","
              "\"category\":\"shancao\",\"name\":\"x\","
              "\"indications\":\"y\",\"source\":\"z\","
              "\"unlock\":{\"kind\":\"chapter_close\","
              "\"chapter\":-1}}");
        Write(dir, "bad_cat.json",
              Entry("badcat", "yaowu",
                    "{\"kind\":\"chapter_close\",\"chapter\":0}")
                  .c_str());
        Write(dir, "bad_kind.json",
              Entry("badkind", "shancao",
                    "{\"kind\":\"auto\"}")
                  .c_str());
        // corruption without at_least.
        Write(dir, "no_param.json",
              Entry("noparam", "ducao",
                    "{\"kind\":\"corruption\"}")
                  .c_str());
        // 批註 is runtime-bound — files cannot author it.
        Write(dir, "marg.json",
              "{\"schema\":\"potato.bencao/1\",\"id\":\"marg\","
              "\"category\":\"shancao\",\"name\":\"x\","
              "\"nature\":\"y\",\"indications\":\"z\","
              "\"source\":\"s\","
              "\"unlock\":{\"kind\":\"chapter_close\",\"chapter\":-1},"
              "\"marginalia\":{\"x\":\"頁邊字\"}}");
        // Duplicate id — same id as a.json's "sanqi". Must pass
        // field validation to reach the dup check (valid terrain id).
        Write(dir, "zz.json",
              Entry("sanqi", "xicao",
                    "{\"kind\":\"terrain\",\"terrain\":\"river\"}")
                  .c_str());
        // name over MAX_NAME_LEN.
        {
            std::string over =
                "{\"schema\":\"potato.bencao/1\",\"id\":\"over\","
                "\"category\":\"shancao\",\"name\":\"";
            over.append(BencaoLibrary::MAX_NAME_LEN + 1, 'x');
            over += "\",\"nature\":\"y\",\"indications\":\"z\","
                    "\"source\":\"s\","
                    "\"unlock\":{\"kind\":\"chapter_close\","
                    "\"chapter\":-1}}";
            Write(dir, "over.json", over.c_str());
        }
        // Required-field matrix: each absent field must reject.
        Write(dir, "no_id.json",
              "{\"schema\":\"potato.bencao/1\",\"category\":\"shancao\","
              "\"name\":\"x\",\"nature\":\"y\",\"indications\":\"z\","
              "\"source\":\"s\",\"unlock\":{\"kind\":\"chapter_close\","
              "\"chapter\":-1}}");
        Write(dir, "no_name.json",
              "{\"schema\":\"potato.bencao/1\",\"id\":\"nn\","
              "\"category\":\"shancao\",\"nature\":\"y\","
              "\"indications\":\"z\",\"source\":\"s\","
              "\"unlock\":{\"kind\":\"chapter_close\",\"chapter\":-1}}");
        Write(dir, "no_ind.json",
              "{\"schema\":\"potato.bencao/1\",\"id\":\"ni\","
              "\"category\":\"shancao\",\"name\":\"x\",\"nature\":\"y\","
              "\"source\":\"s\",\"unlock\":{\"kind\":\"chapter_close\","
              "\"chapter\":-1}}");
        Write(dir, "no_src.json",
              "{\"schema\":\"potato.bencao/1\",\"id\":\"ns\","
              "\"category\":\"shancao\",\"name\":\"x\",\"nature\":\"y\","
              "\"indications\":\"z\",\"unlock\":{\"kind\":"
              "\"chapter_close\",\"chapter\":-1}}");
        Write(dir, "no_unlock.json",
              "{\"schema\":\"potato.bencao/1\",\"id\":\"nu\","
              "\"category\":\"shancao\",\"name\":\"x\",\"nature\":\"y\","
              "\"indications\":\"z\",\"source\":\"s\"}");
        Write(dir, "no_cat.json",
              "{\"schema\":\"potato.bencao/1\",\"id\":\"nc\","
              "\"name\":\"x\",\"nature\":\"y\",\"indications\":\"z\","
              "\"source\":\"s\",\"unlock\":{\"kind\":\"chapter_close\","
              "\"chapter\":-1}}");
        // lang must not assert zh-tw false.
        Write(dir, "lang_false.json",
              "{\"schema\":\"potato.bencao/1\",\"id\":\"lf\","
              "\"category\":\"shancao\",\"name\":\"x\",\"nature\":\"y\","
              "\"indications\":\"z\",\"source\":\"s\","
              "\"unlock\":{\"kind\":\"chapter_close\",\"chapter\":-1},"
              "\"lang\":{\"zh-tw\":false}}");
        // CJK annotation spellings are reserved too.
        Write(dir, "cjk_marg.json",
              "{\"schema\":\"potato.bencao/1\",\"id\":\"cm\","
              "\"category\":\"shancao\",\"name\":\"x\",\"nature\":\"y\","
              "\"indications\":\"z\",\"source\":\"s\","
              "\"unlock\":{\"kind\":\"chapter_close\",\"chapter\":-1},"
              "\"批註\":{\"x\":\"頁邊字\"}}");
        // aliases/origin malformed shapes.
        Write(dir, "alias_na.json",
              "{\"schema\":\"potato.bencao/1\",\"id\":\"an\","
              "\"category\":\"shancao\",\"name\":\"x\",\"nature\":\"y\","
              "\"indications\":\"z\",\"source\":\"s\",\"aliases\":\"x\","
              "\"unlock\":{\"kind\":\"chapter_close\",\"chapter\":-1}}");
        Write(dir, "alias_ns.json",
              "{\"schema\":\"potato.bencao/1\",\"id\":\"as\","
              "\"category\":\"shancao\",\"name\":\"x\",\"nature\":\"y\","
              "\"indications\":\"z\",\"source\":\"s\",\"aliases\":[1],"
              "\"unlock\":{\"kind\":\"chapter_close\",\"chapter\":-1}}");
        Write(dir, "origin_bad.json",
              "{\"schema\":\"potato.bencao/1\",\"id\":\"ob\","
              "\"category\":\"shancao\",\"name\":\"x\",\"nature\":\"y\","
              "\"indications\":\"z\",\"source\":\"s\",\"origin\":5,"
              "\"unlock\":{\"kind\":\"chapter_close\",\"chapter\":-1}}");
        // Dead/impossible trigger params (strict vocab, review D2).
        Write(dir, "terr_dead.json",
              Entry("td", "shancao",
                    "{\"kind\":\"terrain\",\"terrain\":\"ford\"}")
                  .c_str());
        Write(dir, "myth_dead.json",
              Entry("md", "chongshou",
                    "{\"kind\":\"myth_state\",\"state\":\"shrine_pacified\"}")
                  .c_str());
        Write(dir, "gov_long.json",
              Entry("gl", "gucai",
                    "{\"kind\":\"governance\",\"event\":"
                    "\"012345678901234567890123456789012345678901234567890"
                    "123\"}")
                  .c_str());
        Write(dir, "ch_neg.json",
              Entry("cn", "renbu",
                    "{\"kind\":\"chapter_close\",\"chapter\":-2}")
                  .c_str());
        Write(dir, "ch_over.json",
              Entry("co", "renbu",
                    "{\"kind\":\"chapter_close\",\"chapter\":64}")
                  .c_str());
        Write(dir, "corr_zero.json",
              Entry("cz", "ducao",
                    "{\"kind\":\"corruption\",\"at_least\":0}")
                  .c_str());
        Write(dir, "corr_over.json",
              Entry("cx", "ducao",
                    "{\"kind\":\"corruption\",\"at_least\":1024}")
                  .c_str());

        BencaoLibrary lib;
        const auto res = BencaoLibrary::Load(dir, lib);
        Check(res.ok, "10.1: load succeeds despite rejects");
        Check(lib.Size() == 3,
              "10.1: bad files leave library untouched");
        Check(res.rejected.size() == 26,
              "10.1: schema/field/kind/param/marginalia/dup/bounds/"
              "dead-param each rejected");
        // dup rejection must not consume the id of the good file.
        Check(lib.Find("sanqi") &&
                  lib.Find("sanqi")->category ==
                      BencaoCategory::Shancao,
              "10.1: dup file doesn't overwrite registered id");
    }

    // --- per-kind param pins ---
    {
        const fs::path kdir =
            fs::temp_directory_path() / "potato_test_bencao_kinds";
        fs::remove_all(kdir);
        fs::create_directories(kdir);
        Write(kdir, "t.json",
              Entry("t1", "manshui",
                    "{\"kind\":\"terrain\",\"terrain\":\"river\"}")
                  .c_str());
        Write(kdir, "g.json",
              Entry("g1", "gucai",
                    "{\"kind\":\"governance\",\"event\":\"epidemic\"}")
                  .c_str());
        Write(kdir, "c.json",
              Entry("c1", "renbu",
                    "{\"kind\":\"chapter_close\",\"chapter\":-1}")
                  .c_str());
        // Acceptance side: aliases + origin + lang{zh-tw,en} land;
        // uppercase extension counts as content.
        Write(kdir, "z.json",
              "{\"schema\":\"potato.bencao/1\",\"id\":\"full\","
              "\"category\":\"shancao\",\"name\":\"三七\","
              "\"aliases\":[\"山漆\",\"金不換\"],"
              "\"origin\":\"產於滇粵山地。\",\"nature\":\"甘、微苦，溫。\","
              "\"indications\":\"止血散血，定痛。\","
              "\"source\":\"本草綱目·卷十二\","
              "\"unlock\":{\"kind\":\"chapter_close\",\"chapter\":-1},"
              "\"lang\":{\"zh-tw\":true,\"en\":true}}");
        Write(kdir, "UPPER.JSON",
              Entry("up", "xicao",
                    "{\"kind\":\"chapter_close\",\"chapter\":-1}")
                  .c_str());
        BencaoLibrary lib;
        const auto res = BencaoLibrary::Load(kdir, lib);
        Check(res.ok && lib.Size() == 5 &&
                  res.rejected.empty(),
              "10.1: remaining unlock kinds parse, .JSON counts");
        Check(lib.Find("g1")->unlockKind == UnlockKind::Governance &&
                  lib.Find("g1")->unlockParam == "epidemic" &&
                  lib.Find("c1")->unlockInt == -1,
              "10.1: kind params land");
        Check(lib.Find("full") &&
                  lib.Find("full")->aliases.size() == 2 &&
                  lib.Find("full")->aliases[1] == "金不換" &&
                  lib.Find("full")->origin == "產於滇粵山地。",
              "10.1: aliases/origin/lang accepted + land");
        fs::remove_all(kdir);
    }

    // --- empty dir / unreadable dir ---
    {
        const fs::path empty =
            fs::temp_directory_path() / "potato_test_bencao_empty";
        fs::remove_all(empty);
        fs::create_directories(empty);
        BencaoLibrary lib;
        const auto res = BencaoLibrary::Load(empty, lib);
        Check(res.ok && lib.Size() == 0,
              "10.1: empty dir yields empty library");
        fs::remove_all(empty);

        BencaoLibrary x;
        const auto r = BencaoLibrary::Load(
            dir / "does_not_exist", x);
        Check(!r.ok && r.error == "io",
              "10.1: unreadable dir fails the load");
    }

    // ================= Story 10.2 — unlock engine =================

    const fs::path dir2 =
        fs::temp_directory_path() / "potato_test_bencao_102";
    fs::remove_all(dir2);
    fs::create_directories(dir2);

    // One entry per trigger kind plus a pinned chapter_close.
    Write(dir2, "a.json",
          Entry("t_herb", "shancao",
                "{\"kind\":\"terrain\",\"terrain\":\"forest\"}")
              .c_str());
    Write(dir2, "b.json",
          Entry("l_herb", "xicao",
                "{\"kind\":\"ledger_tag\",\"tag\":\"atrocity\"}")
              .c_str());
    Write(dir2, "c.json",
          Entry("g_herb", "gucai",
                "{\"kind\":\"governance\",\"event\":\"governance_victory\"}")
              .c_str());
    Write(dir2, "d.json",
          Entry("m_herb", "chongshou",
                "{\"kind\":\"myth_state\",\"state\":\"pacify_shrine\"}")
              .c_str());
    Write(dir2, "e.json",
          Entry("m_inf", "chongshou",
                "{\"kind\":\"myth_state\",\"state\":\"infiltrated\"}")
              .c_str());
    Write(dir2, "f.json",
          Entry("c_herb", "ducao",
                "{\"kind\":\"corruption\",\"at_least\":10}")
              .c_str());
    Write(dir2, "g.json",
          Entry("cl_any", "renbu",
                "{\"kind\":\"chapter_close\",\"chapter\":-1}")
              .c_str());
    Write(dir2, "h.json",
          Entry("cl_2", "renbu",
                "{\"kind\":\"chapter_close\",\"chapter\":2}")
              .c_str());
    // A second governance entry keyed to a bare deed tag — the
    // non-seal path through the governance kind.
    Write(dir2, "i.json",
          Entry("g_deed", "gucai",
                "{\"kind\":\"governance\",\"event\":\"epidemic\"}")
              .c_str());

    BencaoLibrary lib2;
    Check(BencaoLibrary::Load(dir2, lib2).ok && lib2.Size() == 9,
          "10.2: trigger library loads");

    // --- null signals: only the "every settle" kind fires ---
    {
        BencaoCodex codex;
        const auto fresh =
            Potato::Campaign::ResolveBencaoUnlocks(lib2, CodexSignals{},
                                                   codex);
        Check(fresh.ok() && fresh.value.size() == 1 &&
                  fresh.value[0] == "cl_any",
              "10.2: null signals fire only chapter_close(any)");
        Check(!codex.IsUnlocked("cl_any") &&
                  codex.Pending().size() == 1,
              "10.2: trigger enqueues pending, book still blank");
    }

    // --- assembled signals: each kind resolves off its own store ---
    {
        Ledger ledger;
        Posting atrocity;
        atrocity.credit = {Account::Materiel, 40};
        atrocity.debit = {Account::PopularSupport, 15};
        atrocity.memo = "burned village r3";
        atrocity.tags = {"atrocity", "corruption:+15"};
        Check(ledger.Post(atrocity).ok(), "10.2: atrocity posts");
        Posting seal;
        seal.credit = {Account::Mandate, 1};
        seal.debit = {Account::ArmyPrestige, 1};
        seal.memo = "chapter sealed";
        seal.tags = {"resolution:governance_victory", "chapter:0"};
        Check(ledger.Post(seal).ok(), "10.2: resolution seal posts");
        // A bare deed tag — no resolution prefix; exercises the
        // governance kind's second matching path.
        Posting deed;
        deed.credit = {Account::PopularSupport, 10};
        deed.debit = {Account::Materiel, 10};
        deed.memo = "relief works";
        deed.tags = {"epidemic"};
        Check(ledger.Post(deed).ok(), "10.2: bare deed tag posts");

        MythLog mythLog;
        Check(mythLog.Record("pacify_shrine", "安撫", 0, 1, -1).ok(),
              "10.2: myth action logs");
        MythState myth;
        Check(myth.Set("ch1", 3, 2), "10.2: infiltration state sets");

        CodexSignals sig;
        sig.ledger = &ledger;
        sig.mythLog = &mythLog;
        sig.myth = &myth;
        sig.chapterId = "ch1";
        sig.chapterIndex = 0; // cl_2 wants 2 — must NOT fire here
        sig.terrains = {"forest"};

        BencaoCodex codex;
        const auto fresh =
            Potato::Campaign::ResolveBencaoUnlocks(lib2, sig, codex);
        Check(fresh.ok(), "10.2: resolve ok");
        // Canonical order = library order (category, then id):
        // t_herb(shancao) < l_herb(xicao) < c_herb(ducao)
        // < g_deed < g_herb (gucai, id order) < m_herb, m_inf
        // (chongshou, id order) < cl_any(renbu). cl_2 is silent at
        // chapter 0.
        Check(fresh.value.size() == 8,
              "10.2: all kinds fire, pinned chapter stays shut");
        Check(fresh.value[0] == "t_herb" && fresh.value[1] == "l_herb" &&
                  fresh.value[2] == "c_herb" &&
                  fresh.value[3] == "g_deed" &&
                  fresh.value[4] == "g_herb" &&
                  fresh.value[5] == "m_herb" &&
                  fresh.value[6] == "m_inf" &&
                  fresh.value[7] == "cl_any",
              "10.2: fresh ids in canonical eval order");

        // Trigger provenance rides the queue — the pending page
        // records WHAT matched.
        const auto& pend = codex.Pending();
        Check(pend.size() == 8 && pend[2].id == "c_herb" &&
                  pend[2].kind == "corruption" &&
                  pend[2].detail == "15" &&
                  pend[4].id == "g_herb" &&
                  pend[4].detail == "resolution:governance_victory" &&
                  pend[6].id == "m_inf" && pend[6].detail == "ch1",
              "10.2: pending carries trigger provenance");

        // Idempotent re-run — same signals unlock nothing twice.
        const auto again =
            Potato::Campaign::ResolveBencaoUnlocks(lib2, sig, codex);
        Check(again.ok() && again.value.empty() &&
                  codex.Pending().size() == 8,
              "10.2: idempotent — no double unlock/enqueue");

        // Pending drains FIFO; chapter 2 settle fires cl_2.
        auto drained = codex.TakePending(3);
        Check(drained.size() == 3 && drained[0].id == "t_herb" &&
                  drained[2].id == "c_herb" &&
                  !drained[0].detail.empty() &&
                  codex.Pending().size() == 5,
              "10.2: TakePending drains FIFO with provenance");
        Check(codex.IsUnlocked("t_herb") &&
                  codex.Unlocked().size() == 3,
              "10.2: 補鈔 delivery writes pages into the book");
        sig.chapterIndex = 2;
        const auto late =
            Potato::Campaign::ResolveBencaoUnlocks(lib2, sig, codex);
        Check(late.ok() && late.value.size() == 1 &&
                  late.value[0] == "cl_2",
              "10.2: pinned chapter_close fires on its chapter");

        // State round-trip: unlocked + remaining pending survive.
        const auto doc = codex.ToJson();
        Check(doc.ok(), "10.2: ToJson ok");
        const auto parsed = JsonValue::Parse(doc.value.Emit());
        Check(parsed.ok(), "10.2: emit reparses");
        const auto back = BencaoCodex::FromJson(parsed.value);
        Check(back.ok() &&
                  back.value.Unlocked() == codex.Unlocked() &&
                  back.value.Pending() == codex.Pending(),
              "10.2: state round-trips losslessly");
    }

    // --- FromJson guards: bad schema, dedupe, pending∩unlocked ---
    {
        const auto bad = JsonValue::Parse(
            "{\"schema\":\"potato.bencao_state/2\",\"unlocked\":[],"
            "\"pending\":[]}");
        Check(bad.ok() && !BencaoCodex::FromJson(bad.value).ok(),
              "10.2: wrong schema rejected");
        const auto dup = JsonValue::Parse(
            "{\"schema\":\"potato.bencao_state/1\","
            "\"unlocked\":[\"a\",\"a\",\"b\"],"
            "\"pending\":[{\"id\":\"b\",\"kind\":\"ledger_tag\","
            "\"detail\":\"x\"},"
            "{\"id\":\"c\",\"kind\":\"terrain\",\"detail\":\"ford\"},"
            "{\"id\":\"c\",\"kind\":\"terrain\",\"detail\":\"ford\"}]}");
        const auto st = BencaoCodex::FromJson(dup.value);
        Check(st.ok() && st.value.Unlocked().size() == 2 &&
                  st.value.Pending().size() == 1 &&
                  st.value.Pending()[0].id == "c",
              "10.2: dupes dedupe, pending∩unlocked drops");
        // Malformed state docs must fail loudly, not load empty.
        Check(!BencaoCodex::FromJson(
                  JsonValue::Parse("{\"schema\":\"potato.bencao_state/1\","
                                   "\"unlocked\":\"x\",\"pending\":[]}")
                      .value)
                  .ok(),
              "10.2: non-array unlocked rejected");
        Check(!BencaoCodex::FromJson(
                  JsonValue::Parse("{\"schema\":\"potato.bencao_state/1\","
                                   "\"unlocked\":[1],\"pending\":[]}")
                      .value)
                  .ok(),
              "10.2: non-string id rejected");
        Check(!BencaoCodex::FromJson(
                  JsonValue::Parse("{\"schema\":\"potato.bencao_state/1\","
                                   "\"unlocked\":[\"\"],\"pending\":[]}")
                      .value)
                  .ok(),
              "10.2: empty id rejected");
        Check(!BencaoCodex::FromJson(
                  JsonValue::Parse("{\"schema\":\"potato.bencao_state/1\","
                                   "\"unlocked\":[],"
                                   "\"pending\":[{\"id\":\"x\"}]}")
                      .value)
                  .ok(),
              "10.2: pending missing fields rejected");
        // Union bound: the two lists share one capacity budget.
        {
            std::string big =
                "{\"schema\":\"potato.bencao_state/1\",\"unlocked\":[";
            for (std::size_t i = 0; i < BencaoCodex::MAX_ENTRIES; ++i) {
                big += "\"id";
                big += std::to_string(i);
                big += "\",";
            }
            big.back() = ']';
            big += ",\"pending\":[{\"id\":\"x\",\"kind\":\"k\","
                   "\"detail\":\"d\"}]}";
            const auto over = JsonValue::Parse(big);
            Check(over.ok() &&
                      !BencaoCodex::FromJson(over.value).ok(),
                  "10.2: union capacity bound rejects");
        }
    }

    // --- TerrainFlagsOf: distinct flag names, flag order ---
    {
        const auto mdoc = JsonValue::Parse(
            "{\"schema\":\"potato.map/1\",\"id\":\"m\",\"name\":\"m\","
            "\"regions\":["
            "{\"id\":\"a\",\"terrain\":[\"forest\",\"river\"]},"
            "{\"id\":\"b\",\"terrain\":[\"highland\"]}]}");
        Check(mdoc.ok(), "10.2: map doc parses");
        const auto map = BattleMap::FromJson(mdoc.value);
        Check(map.ok(), "10.2: map builds");
        const auto flags = Potato::Campaign::TerrainFlagsOf(map.value);
        Check(flags.size() == 3 && flags[0] == "river" &&
                  flags[1] == "forest" && flags[2] == "highland",
              "10.2: TerrainFlagsOf = distinct ids, flag order");
    }

    // ================= Story 10.3 — 補鈔 delivery =================

    // lib2 spans all six unlock kinds — reuse it so each clause
    // pool gets exercised.
    {
        // Seed a codex whose queue holds one page per kind (the
        // resolve pass was pinned in 10.2; seed via state doc).
        const auto st = JsonValue::Parse(
            "{\"schema\":\"potato.bencao_state/1\",\"unlocked\":[],"
            "\"pending\":["
            "{\"id\":\"t_herb\",\"kind\":\"terrain\",\"detail\":\"forest\"},"
            "{\"id\":\"l_herb\",\"kind\":\"ledger_tag\",\"detail\":\"atrocity\"},"
            "{\"id\":\"c_herb\",\"kind\":\"corruption\",\"detail\":\"15\"},"
            "{\"id\":\"g_herb\",\"kind\":\"governance\","
            "\"detail\":\"resolution:governance_victory\"},"
            "{\"id\":\"m_herb\",\"kind\":\"myth_state\","
            "\"detail\":\"pacify_shrine\"},"
            "{\"id\":\"cl_any\",\"kind\":\"chapter_close\",\"detail\":\"0\"}]}");
        Check(st.ok(), "10.3: codex seed doc parses");
        const auto cx = BencaoCodex::FromJson(st.value);
        Check(cx.ok() && cx.value.Pending().size() == 6,
              "10.3: codex seeds with six pending");
        BencaoCodex codex = cx.value;
        BuchaoStore store;

        // Bounded drain: 2 of 6, FIFO, remainder stays pending.
        const auto first = DeliverBuchao(lib2, codex, store, 2, 0);
        Check(first.ok() && first.value.size() == 2 &&
                  first.value[0].entry == "t_herb" &&
                  first.value[1].entry == "l_herb",
              "10.3: bounded drain delivers FIFO head");
        Check(codex.Pending().size() == 4 &&
                  codex.IsUnlocked("t_herb") &&
                  codex.IsUnlocked("l_herb"),
              "10.3: drained pages join the book, rest queued");
        Check(store.Pages().size() == 2 &&
                  !store.Pages()[0].note.empty() &&
                  !store.Pages()[1].note.empty(),
              "10.3: pages land with composed 批註");

        // Drain the rest — every kind's pool produces a note, and
        // the queue is empty when the bound allows.
        const auto rest = DeliverBuchao(lib2, codex, store, 8, 0);
        Check(rest.ok() && rest.value.size() == 4 &&
                  codex.Pending().empty() &&
                  store.Pages().size() == 6,
              "10.3: remaining queue drains under the bound");
        bool allNoted = true;
        for (const auto& p : store.Pages()) {
            if (p.note.empty()) allNoted = false;
        }
        Check(allNoted && store.Find("cl_any") &&
                  store.Find("c_herb"),
              "10.3: every kind's pool composes a note");

        // Idempotent: nothing pending, nothing new delivered.
        const auto again = DeliverBuchao(lib2, codex, store, 8, 0);
        Check(again.ok() && again.value.empty() &&
                  store.Pages().size() == 6,
              "10.3: empty queue delivers nothing");

        // Deterministic composition: same entry + provenance +
        // chapter → same 批註, byte for byte (delivery used
        // chapterIndex 0; the seed's detail rides the hash).
        const BencaoEntry* e = lib2.Find("t_herb");
        const PendingPage tp{"t_herb", "terrain", "forest"};
        Check(e && ComposeMarginalia(*e, tp, 0) ==
                      store.Find("t_herb")->note &&
                  ComposeMarginalia(*e, tp, 0) ==
                      ComposeMarginalia(*e, tp, 0),
              "10.3: composition is deterministic");

        // Suspect flag: judgment overlay, persisted per page.
        Check(store.SetSuspect("l_herb") &&
                  store.Find("l_herb")->suspect,
              "10.3: suspect flag sets");
        Check(!store.SetSuspect("ghost"),
              "10.3: suspect on missing page fails");

        // Store round-trip — order + flags preserved.
        const auto doc = store.ToJson();
        Check(doc.ok(), "10.3: store ToJson ok");
        const auto parsed = JsonValue::Parse(doc.value.Emit());
        const auto back = BuchaoStore::FromJson(parsed.value);
        Check(back.ok() && back.value.Pages().size() == 6 &&
                  back.value.Find("l_herb")->suspect &&
                  back.value.Pages()[0].entry == "t_herb",
              "10.3: store round-trips with suspect flag");
    }

    // --- corrupt queue: pending id absent from the library ---
    {
        const auto st = JsonValue::Parse(
            "{\"schema\":\"potato.bencao_state/1\",\"unlocked\":[],"
            "\"pending\":["
            "{\"id\":\"ghost\",\"kind\":\"terrain\",\"detail\":\"ford\"},"
            "{\"id\":\"t_herb\",\"kind\":\"terrain\",\"detail\":\"forest\"}]}");
        const auto cx = BencaoCodex::FromJson(st.value);
        Check(cx.ok(), "10.3: ghost-pending codex loads");
        BencaoCodex codex = cx.value;
        BuchaoStore store;
        const auto r = DeliverBuchao(lib2, codex, store, 4, 0);
        Check(!r.ok() && r.error == "field",
              "10.3: library-missing pending id fails field");
        Check(codex.Pending().size() == 2 &&
                  store.Pages().empty(),
              "10.3: failed delivery moves nothing");
    }

    // --- FromJson guards: schema, shape, fields, dedupe ---
    {
        const auto bad = JsonValue::Parse(
            "{\"schema\":\"potato.buchao/2\",\"pages\":[]}");
        Check(bad.ok() && !BuchaoStore::FromJson(bad.value).ok(),
              "10.3: wrong schema rejected");
        const auto shape = JsonValue::Parse(
            "{\"schema\":\"potato.buchao/1\",\"pages\":{}}");
        Check(shape.ok() && !BuchaoStore::FromJson(shape.value).ok(),
              "10.3: non-array pages rejected");
        const auto noNote = JsonValue::Parse(
            "{\"schema\":\"potato.buchao/1\",\"pages\":["
            "{\"entry\":\"x\",\"suspect\":false}]}");
        Check(noNote.ok() &&
                  !BuchaoStore::FromJson(noNote.value).ok(),
              "10.3: page without note rejected");
        const auto dup = JsonValue::Parse(
            "{\"schema\":\"potato.buchao/1\",\"pages\":["
            "{\"entry\":\"x\",\"note\":\"a\",\"suspect\":true},"
            "{\"entry\":\"x\",\"note\":\"b\",\"suspect\":false}]}");
        const auto dd = BuchaoStore::FromJson(dup.value);
        Check(dd.ok() && dd.value.Pages().size() == 1 &&
                  dd.value.Pages()[0].note == "a" &&
                  dd.value.Pages()[0].suspect,
              "10.3: duplicate entries dedupe first-wins");
    }

    // --- maxPages == 0 delivers nothing ---
    {
        const auto st = JsonValue::Parse(
            "{\"schema\":\"potato.bencao_state/1\",\"unlocked\":[],"
            "\"pending\":["
            "{\"id\":\"t_herb\",\"kind\":\"terrain\",\"detail\":\"forest\"}]}");
        BencaoCodex codex = BencaoCodex::FromJson(st.value).value;
        BuchaoStore store;
        const auto r = DeliverBuchao(lib2, codex, store, 0, 0);
        Check(r.ok() && r.value.empty() &&
                  codex.Pending().size() == 1,
              "10.3: zero bound delivers nothing");
    }

    // ================= Story 10.4 — codex renderer =================

    // A three-state codex: unlocked / pending / locked.
    const fs::path dir3 = fs::temp_directory_path() / "bc_104_test";
    fs::remove_all(dir3);
    fs::create_directories(dir3);
    Write(dir3, "a.json",
          "{\"schema\":\"potato.bencao/1\",\"id\":\"sanqi\","
          "\"category\":\"shancao\",\"name\":\"三七\","
          "\"aliases\":[\"山漆\",\"金不換\"],"
          "\"origin\":\"生廣西、雲南山峒深處。\","
          "\"nature\":\"甘、微苦，溫。歸肝、胃經。\","
          "\"indications\":\"止血散血，定痛。\","
          "\"source\":\"《本草綱目》卷十二\","
          "\"unlock\":{\"kind\":\"ledger_tag\",\"tag\":\"first_loss\"}}");
    Write(dir3, "b.json",
          Entry("fuzi", "ducao",
                "{\"kind\":\"corruption\",\"at_least\":30}")
              .c_str());
    Write(dir3, "c.json",
          Entry("lingzhi", "jinshi",
                "{\"kind\":\"myth_state\",\"state\":\"pacify_shrine\"}")
              .c_str());
    BencaoLibrary lib3;
    Check(BencaoLibrary::Load(dir3, lib3).ok && lib3.Size() == 3,
          "10.4: fixture library loads");

    const auto cx = JsonValue::Parse(
        "{\"schema\":\"potato.bencao_state/1\",\"unlocked\":[\"sanqi\"],"
        "\"pending\":[{\"id\":\"fuzi\",\"kind\":\"corruption\","
        "\"detail\":\"35\"}]}");
    BencaoCodex codex3 = BencaoCodex::FromJson(cx.value).value;
    BuchaoStore store3;
    {
        // Seed the store's page for the sanqi slot — suspect on:
        // the judgment mark must render.
        const auto seeded = JsonValue::Parse(
            "{\"schema\":\"potato.buchao/1\",\"pages\":[{\"entry\":"
            "\"sanqi\",\"note\":\"第三回，斥候中箭，此末敷之，血止。\","
            "\"suspect\":true}]}");
        store3 = BuchaoStore::FromJson(seeded.value).value;
    }

    const std::string book = RenderCodex(lib3, codex3, &store3);
    Check(book.find("是冊所載，皆前人之驗") != std::string::npos &&
              book.find("勿執紙上之言以試人身") != std::string::npos,
          "10.4: frontispiece verbatim disclaimer");
    Check(book.find("——山草類——") != std::string::npos &&
              book.find("——人部拾遺——") != std::string::npos &&
              book.find("山草類") < book.find("人部拾遺"),
          "10.4: all 8 部類 headers, canonical order");
    Check(book.find("釋名：") != std::string::npos &&
              book.find("性味歸經：") != std::string::npos &&
              book.find("主治：") != std::string::npos &&
              book.find("出處：") != std::string::npos,
          "10.4: unlocked page renders all fields");
    Check(book.find("【諱】補鈔在途") != std::string::npos &&
              book.find("fuzi") == std::string::npos,
          "10.4: pending = sealed slot, name never leaks");
    Check(book.find("【諱】未錄") != std::string::npos &&
              book.find("lingzhi") == std::string::npos,
          "10.4: locked = sealed slot, name never leaks");
    Check(book.find("批註：第三回，斥候中箭") != std::string::npos &&
              book.find("書吏疑其不實") != std::string::npos,
          "10.4: 批註 renders with suspect mark");
    Check(book.find("凡 3 種，已錄 1 種") != std::string::npos &&
              book.find("not medical advice") != std::string::npos,
          "10.4: colophon counts + English disclaimer");
    Check(RenderCodex(lib3, codex3, &store3) == book,
          "10.4: render is deterministic");

    // Empty library: headers + zero counts, no crash.
    BencaoLibrary empty;
    BencaoCodex ecodex;
    const std::string ebook = RenderCodex(empty, ecodex, nullptr);
    Check(ebook.find("——山草類——") != std::string::npos &&
              ebook.find("凡 0 種，已錄 0 種") != std::string::npos,
          "10.4: empty library renders TOC + zero counts");

    // --- Story 10.5: the other voices cite the book ---
    {
        using Potato::Campaign::RenderBattleReport;
        using Potato::Campaign::RenderBencaoColophon;
        using Potato::Campaign::RenderBencaoHearsay;
        using Potato::Campaign::RenderMythLog;
        using Potato::Gameplay::SimEvent;

        const fs::path dir4 =
            fs::temp_directory_path() / "potato_test_bencao_cite";
        fs::remove_all(dir4);
        fs::create_directories(dir4);
        // Two gucai entries (one-per-category pin), one manshui,
        // one jinshi (locked), one chongshou myth_state entry.
        Write(dir4, "a.json",
              Entry("ga", "gucai",
                    "{\"kind\":\"ledger_tag\",\"tag\":\"x\"}")
                  .c_str());
        Write(dir4, "b.json",
              Entry("gb", "gucai",
                    "{\"kind\":\"ledger_tag\",\"tag\":\"y\"}")
                  .c_str());
        Write(dir4, "c.json",
              Entry("m1", "manshui",
                    "{\"kind\":\"ledger_tag\",\"tag\":\"z\"}")
                  .c_str());
        Write(dir4, "d.json",
              Entry("j1", "jinshi",
                    "{\"kind\":\"myth_state\",\"state\":\"ghost_army\"}")
                  .c_str());
        // c1 carries a folk 釋名 — the rumor voice names the
        // alias, never the sealed 正名.
        Write(dir4, "e.json",
              "{\"schema\":\"potato.bencao/1\",\"id\":\"c1\","
              "\"category\":\"chongshou\",\"name\":\"三七\","
              "\"aliases\":[\"鬼蓋\"],"
              "\"nature\":\"甘、微苦，溫。\","
              "\"indications\":\"止血散血，定痛。\","
              "\"source\":\"本草綱目·卷十二\","
              "\"unlock\":{\"kind\":\"myth_state\","
              "\"state\":\"pacify_shrine\"}}");
        BencaoLibrary lib4;
        Check(BencaoLibrary::Load(dir4, lib4).ok &&
                  lib4.Size() == 5,
              "10.5: cite fixture loads");

        auto mkCodex = [](const char* unlockedJson) {
            // pending is a required array on the wire even when
            // empty — omitting it rejects the whole doc.
            const auto j = JsonValue::Parse(
                std::string(
                    "{\"schema\":\"potato.bencao_state/1\","
                    "\"unlocked\":") +
                unlockedJson + ",\"pending\":[]}");
            return BencaoCodex::FromJson(j.value).value;
        };

        // Events: village deed + convoy deed + shrine + execution.
        SimEvent ev[4] = {};
        ev[0].kind = SimEvent::Kind::VillageBurned;
        ev[1].kind = SimEvent::Kind::ConvoyRaided;
        ev[2].kind = SimEvent::Kind::ShrineCaptured;
        ev[3].kind = SimEvent::Kind::SquadExecuted;

        // Unlocked: ga, gb (gucai), m1 (manshui). Locked: j1, c1.
        BencaoCodex cx4 = mkCodex("[\"ga\",\"gb\",\"m1\"]");
        const std::string report = RenderBattleReport(
            ev, /*historianSide=*/0, {}, &cx4, &lib4);
        const auto countOf = [](const std::string& h,
                                std::string_view n) {
            std::size_t c = 0, p = 0;
            while ((p = h.find(n, p)) != std::string::npos) {
                ++c;
                ++p;
            }
            return c;
        };
        Check(countOf(report, "書吏按") == 2,
              "10.5: one colophon per cited category");
        Check(report.find("是役及村落") != std::string::npos &&
                  report.find("是役近河津") != std::string::npos &&
                  report.find("是役近河津") <
                      report.find("是役及村落"),
              "10.5: colophon order follows category ordinal");
        Check(report.find("是役動祠宇") == std::string::npos &&
                  report.find("是役有暴行") == std::string::npos,
              "10.5: categories with no unlocked page stay "
              "silent");
        Check(RenderBattleReport(ev, 0, {}, &cx4, &lib4) ==
                  report,
              "10.5: colophon deterministic");
        // Removable: null args byte-identical to no-arg render.
        Check(RenderBattleReport(ev, 0, {}, nullptr, nullptr) ==
                  RenderBattleReport(ev, 0, {}) &&
                  RenderBattleReport(ev, 0, {}, nullptr,
                                     &lib4) ==
                      RenderBattleReport(ev, 0, {}),
              "10.5: unaided render byte-identical");

        // Shrine deed cites jinshi only once j1 unlocks.
        {
            SimEvent sev[1] = {};
            sev[0].kind = SimEvent::Kind::ShrineCaptured;
            const std::string r0 = RenderBattleReport(
                sev, 0, {}, &cx4, &lib4);
            Check(r0.find("書吏按") == std::string::npos,
                  "10.5: locked jinshi page never named");
            BencaoCodex cxJ = mkCodex("[\"j1\"]");
            const std::string r1 = RenderBattleReport(
                sev, 0, {}, &cxJ, &lib4);
            Check(r1.find("書吏按") != std::string::npos &&
                      r1.find("是役動祠宇") != std::string::npos,
                  "10.5: unlocked jinshi page cited");
        }

        // Helper seam: colophon over the same span matches the
        // renderer's appended clause. Guard the substr — a
        // missing colophon is a FAIL, not a terminate().
        const auto cp = report.find("書吏按");
        Check(cp != std::string::npos &&
                  RenderBencaoColophon(ev, cx4, lib4) ==
                      report.substr(cp),
              "10.5: standalone helper equals appended text");

        // --- hearsay: rumor precedes the catalog, speaks the
        //     folk 釋名, never the sealed 正名. Composed at the
        //     call site — RenderMythLog itself is untouched.
        MythLog mlog;
        Check(mlog.Record("pacify_shrine", "安撫", 0, 1, -1)
                  .ok(),
              "10.5: myth entry recorded");
        const std::string& e0 = mlog.Entries()[0].action;
        (void)e0;
        const std::string hearsay0 = RenderBencaoHearsay(
            mlog.Entries()[0], cx4, lib4);
        Check(hearsay0.find("據說第1里出鬼蓋") !=
                  std::string::npos,
              "10.5: hearsay names the folk alias");
        Check(hearsay0.find("三七") == std::string::npos &&
                  RenderMythLog(mlog).find("三七") ==
                      std::string::npos,
              "10.5: sealed 正名 never leaks to rumor");
        // Once catalogued, rumor goes silent — never the
        // reverse order.
        BencaoCodex cxC = mkCodex("[\"c1\"]");
        const std::string hearsay1 = RenderBencaoHearsay(
            mlog.Entries()[0], cxC, lib4);
        Check(hearsay1.empty(),
              "10.5: catalogued page never hearsays");
        // Seq parity picks the 或云 variant.
        Check(mlog.Record("pacify_shrine", "安撫", 0, 1, -1)
                  .ok(),
              "10.5: second entry recorded");
        Check(RenderBencaoHearsay(mlog.Entries()[1], cx4, lib4)
                  .find("或云第1里有鬼蓋之屬") !=
                  std::string::npos,
              "10.5: odd seq picks the 或云 variant");
        // Aliasless entry hearsays vaguely (靈藥), not by name.
        MythLog mlog2;
        Check(mlog2.Record("ghost_army", "陰兵", 0, 2, -1)
                  .ok(),
              "10.5: ghost_army recorded");
        Check(RenderBencaoHearsay(mlog2.Entries()[0], cx4,
                                  lib4)
                  .find("據說第2里產靈藥") != std::string::npos,
              "10.5: aliasless page hearsays vaguely");
        // Non-matching action carries no hearsay.
        MythLog mlog3;
        Check(mlog3.Record("invasion", "神罰", 0, 3, -1).ok(),
              "10.5: invasion recorded");
        Check(RenderBencaoHearsay(mlog3.Entries()[0], cx4,
                                  lib4)
                  .empty(),
              "10.5: unmatched action no hearsay");
        // Rumor composes beside the log render, not inside it.
        const std::string composed =
            RenderMythLog(mlog) + "\n傳聞拾遺：\n" + hearsay0;
        Check(composed.find("據說第1里的神明息了怒") !=
                  std::string::npos &&
                  composed.find("據說第1里出鬼蓋") !=
                      std::string::npos,
              "10.5: hearsay composes beside the log render");

        fs::remove_all(dir4);
    }

    fs::remove_all(dir3);
    fs::remove_all(dir);
    fs::remove_all(dir2);
    std::printf(failures ? "BENCAO TESTS FAILED: %d\n"
                         : "BENCAO TESTS PASS\n",
                failures);
    return failures ? 1 : 0;
}
