// Story 10.6 — Bencao launch content set (assets/bencao/):
// registry-wide invariants — every entry loads, six-field anatomy
// non-empty, source citation present, lang.zh-tw declared, six
// worldview anchors ship with full fields, every unlock trigger is
// reachable through the 10.2 signal surface, and the 10.3 批註
// pass composes a note on every delivered page.
#include "Campaign/Narrative/Bencao.h"
#include "Campaign/Narrative/BencaoCodex.h"
#include "Campaign/Narrative/Buchao.h"
#include "Campaign/Ledger/Ledger.h"
#include "Campaign/Myth/MythLog.h"
#include "Campaign/Myth/MythState.h"
#include "Gameplay/Json/JsonValue.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef POTATO_BENCAO_DIR
#define POTATO_BENCAO_DIR "assets/bencao"
#endif

using namespace Potato;
using Potato::Campaign::Account;
using Potato::Campaign::BencaoCategory;
using Potato::Campaign::BencaoCodex;
using Potato::Campaign::BencaoLibrary;
using Potato::Campaign::BuchaoStore;
using Potato::Campaign::CodexSignals;
using Potato::Campaign::DeliverBuchao;
using Potato::Campaign::Ledger;
using Potato::Campaign::MythLog;
using Potato::Campaign::MythState;
using Potato::Campaign::Posting;
using Potato::Campaign::UnlockKind;
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

static void PostTags(Ledger& l, const char* memo,
                     std::vector<std::string> tags) {
    Posting p;
    p.credit = {Account::Materiel, 1};
    p.debit = {Account::PopularSupport, 1};
    p.memo = memo;
    p.tags = std::move(tags);
    l.Post(p);
}

int main() {
    const fs::path dir = POTATO_BENCAO_DIR;

    BencaoLibrary lib;
    const auto res = BencaoLibrary::Load(dir, lib);
    Check(res.ok, "10.6: launch dir loads");
    Check(res.rejected.empty(), "10.6: zero per-file rejections");
    Check(lib.Size() >= 30, "10.6: launch set is ~30 entries");

    // --- registry-wide field invariants ---
    {
        bool fields = true, source = true, unlock = true;
        for (const auto& e : lib.Entries()) {
            fields = fields && !e.name.empty() && !e.nature.empty() &&
                     !e.indications.empty() && !e.id.empty();
            // Every factual claim carries a 卷/條 citation.
            source = source && !e.source.empty() &&
                     e.source.find("卷") != std::string::npos;
            switch (e.unlockKind) {
            case UnlockKind::Terrain:
            case UnlockKind::LedgerTag:
            case UnlockKind::Governance:
            case UnlockKind::MythState:
                unlock = unlock && !e.unlockParam.empty();
                break;
            case UnlockKind::Corruption:
                unlock = unlock && e.unlockInt > 0;
                break;
            case UnlockKind::ChapterClose:
                break; // -1 = every close; else a chapter index
            }
        }
        Check(fields, "10.6: six-field anatomy non-empty everywhere");
        Check(source, "10.6: every entry carries a 卷 citation");
        Check(unlock, "10.6: every trigger well-formed");
    }

    // --- lang block: zh-tw declared on every authored file ---
    {
        std::vector<fs::path> files;
        for (const auto& de : fs::directory_iterator(dir)) {
            if (de.path().extension() == ".json") {
                files.push_back(de.path());
            }
        }
        std::sort(files.begin(), files.end());
        bool lang = !files.empty();
        for (const auto& f : files) {
            std::ifstream in(f, std::ios::binary);
            std::ostringstream ss;
            ss << in.rdbuf();
            const auto doc = JsonValue::Parse(ss.str());
            lang = lang && doc.ok() &&
                   doc.value["lang"]["zh-tw"].AsBool(false);
        }
        Check(lang, "10.6: every file declares lang.zh-tw");
    }

    // --- the six anchors ship with the fullest anatomy ---
    {
        const char* anchors[] = {"sanqi",  "gancao",  "fuzi",
                                 "lingzhi", "longgu", "yingsu"};
        bool ok = true;
        for (const char* id : anchors) {
            const auto* e = lib.Find(id);
            ok = ok && e && !e->aliases.empty() && !e->origin.empty();
        }
        Check(ok, "10.6: six anchors present with 釋名+集解");
    }

    // --- every trigger is reachable through CodexSignals ---
    {
        Ledger ledger;
        PostTags(ledger, "defeat sealed",
                 {"resolution:defeat", "atrocity", "corruption:+80"});
        PostTags(ledger, "hearts won", {"resolution:governance_victory"});
        PostTags(ledger, "rival turns", {"resolution:subversion"});
        PostTags(ledger, "convoy hit", {"raid"});
        PostTags(ledger, "refit spoils", {"plunder"});

        MythLog mythLog;
        mythLog.Record("pacify_shrine", "安撫", 0, 1, -1);
        mythLog.Record("invoke_possession", "降乩", 0, 1, -1);
        mythLog.Record("ghost_army", "陰兵", 0, 1, -1);
        mythLog.Record("invasion", "神罰", 0, 1, -1);

        MythState myth;
        Check(myth.Set("ch1", 0, 2), "10.6: infiltration state sets");

        CodexSignals sig;
        sig.ledger = &ledger;
        sig.mythLog = &mythLog;
        sig.myth = &myth;
        sig.chapterId = "ch1";
        sig.terrains = {"forest", "river", "water", "highland"};

        BencaoCodex codex;
        // Pinned chapter_close ids in the launch set: 2/3/4/7.
        bool resolved = true;
        for (const std::int64_t idx : {2, 3, 4, 7}) {
            sig.chapterIndex = idx;
            resolved =
                resolved &&
                Potato::Campaign::ResolveBencaoUnlocks(lib, sig,
                                                       codex).ok();
        }
        Check(resolved, "10.6: resolves ok");
        Check(codex.Pending().size() == lib.Size(),
              "10.6: every launch entry's trigger is reachable");

        // The 補鈔 pass composes a 批註 per page — anchors included.
        BuchaoStore store;
        const auto pages = DeliverBuchao(lib, codex, store,
                                         BencaoLibrary::MAX_ENTRIES, 3);
        Check(pages.ok() && pages.value.size() == lib.Size(),
              "10.6: 補鈔 delivers the whole launch set");
        bool notes = pages.ok();
        for (const auto& p : pages.value) {
            notes = notes && !p.note.empty();
        }
        Check(notes, "10.6: every delivered page got a 批註");
        for (const char* id : {"sanqi", "gancao", "fuzi", "lingzhi",
                               "longgu", "yingsu"}) {
            Check(store.Find(id) && !store.Find(id)->note.empty(),
                  "10.6: anchor page annotated");
        }
    }

    if (failures == 0) {
        std::printf("All bencao launch checks passed.\n");
        return 0;
    }
    std::printf("%d check(s) failed.\n", failures);
    return 1;
}
