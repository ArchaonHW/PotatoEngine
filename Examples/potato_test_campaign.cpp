// Story 3.1 — CampaignState facade: potato.campaign/1 round-trip.
// Story 3.2 — SaveSystem: atomic tmp->rename saves, gated loads.
#include "Campaign/Aftermath/Aftermath.h"
#include "Campaign/Chapters/ChapterLibrary.h"
#include "Campaign/Chapters/Progression.h"
#include "Campaign/Governance/Accumulators.h"
#include "Campaign/Governance/Victory.h"
#include "Campaign/Myth/MythLog.h"
#include "Campaign/Rivals/RivalDeck.h"
#include "Campaign/Roster/RefitCamp.h"
#include "Campaign/Roster/Roster.h"
#include "Campaign/Save/SaveSystem.h"
#include "Campaign/State/CampaignState.h"
#include "Campaign/World/WorldMap.h"
#include "Campaign/World/WorldState.h"
#include "Gameplay/Json/JsonValue.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace Potato;
using Potato::Gameplay::JsonValue;
using Potato::Campaign::CampaignState;
using Potato::Campaign::Ledger;
using Potato::Campaign::Posting;
using Potato::Campaign::Leg;
using Potato::Campaign::Account;
using Potato::Campaign::RosterEntry;
using Potato::Campaign::SaveSystem;
using Potato::Campaign::ChapterLibrary;
using Potato::Campaign::AftermathRow;
using Potato::Campaign::ApplyAftermath;
using Potato::Campaign::Enlist;
using Potato::Campaign::BandCosts;
using Potato::Campaign::Deploy;
using Potato::Campaign::Heal;
using Potato::Campaign::Recruit;
using Potato::Campaign::Plunder;
using Potato::Campaign::RivalBook;
using Potato::Campaign::RivalPrior;
using Potato::Campaign::TriggerHistogram;
using Potato::Campaign::kTriggerKindCount;

static int failures = 0;
static void Check(bool cond, const char* name) {
    if (cond) {
        std::printf("[PASS] %s\n", name);
    } else {
        std::printf("[FAIL] %s\n", name);
        ++failures;
    }
}

static Posting Burn() {
    Posting p;
    p.credit = {Account::Materiel, 40};
    p.debit = {Account::PopularSupport, 15};
    p.memo = "burned village";
    return p;
}

static CampaignState SampleState() {
    CampaignState st;
    st.GetChapter().current = 2;
    st.GetChapter().unlocked = {true, true, true, false};
    st.GetChapter().resolved = {true, true, false, false};
    RosterEntry a{"右翼第三隊", 3, 40, false};
    RosterEntry b{"左翼第一隊", 1, 90, true};
    st.GetRoster() = {a, b};
    st.GetLedger().Post(Burn());
    Posting f = Burn();
    f.memo = "forged report";
    const auto r = st.GetLedger().Forge(f);
    st.GetLedger().SetSuspect(r.value);
    return st;
}

int main() {
    // --- round-trip ---
    {
        const CampaignState st = SampleState();
        auto jr = st.ToJson();
        Check(jr.ok(), "ToJson succeeds");
        const JsonValue doc = jr.value;
        auto back = CampaignState::FromJson(doc);
        Check(back.ok(), "FromJson accepts own emit");
        if (back.ok()) {
            const CampaignState& s2 = back.value;
            Check(s2.GetChapter().current == 2 &&
                      s2.GetChapter().unlocked.size() == 4 &&
                      s2.GetChapter().unlocked[2] &&
                      !s2.GetChapter().resolved[2],
                  "chapter progress round-trips");
            Check(s2.GetRoster().size() == 2 &&
                      s2.GetRoster()[0].name == "右翼第三隊" &&
                      s2.GetRoster()[0].veterancy == 3 &&
                      s2.GetRoster()[0].casualties == 40 &&
                      !s2.GetRoster()[0].dead &&
                      s2.GetRoster()[1].dead,
                  "roster round-trips (incl. CJK name)");
            Check(s2.GetLedger().Size() == 2 &&
                      s2.GetLedger().Verify() == nullptr &&
                      s2.GetLedger().SuspectEntries().size() == 1,
                  "ledger embeds intact (chain + suspect)");
            Check(s2.GetLedger().Balance(Account::Materiel) == 80 &&
                      s2.GetLedger().Balance(Account::PopularSupport) ==
                          -30,
                  "balances round-trip through embed");
        }
    }

    // --- rejections (no partial state escapes) ---
    {
        const CampaignState st = SampleState();
        const JsonValue good = st.ToJson().value;

        auto badSchema = good.Members();
        badSchema["schema"] = JsonValue::String("potato.campaign/1");
        Check(!CampaignState::FromJson(
                  JsonValue::MakeObject(std::move(badSchema)))
                   .ok(),
              "old-format /1 doc rejected by the /2 gate");

        auto badChapter = good.Members();
        badChapter["chapter"] = JsonValue::Int(5);
        Check(!CampaignState::FromJson(
                  JsonValue::MakeObject(std::move(badChapter)))
                   .ok(),
              "non-object chapter rejected");

        auto badCur = good.Members();
        JsonValue::Object ch = badCur["chapter"].Members();
        ch["current"] = JsonValue::Int(-1);
        badCur["chapter"] = JsonValue::MakeObject(std::move(ch));
        Check(!CampaignState::FromJson(
                  JsonValue::MakeObject(std::move(badCur)))
                   .ok(),
              "negative chapter.current rejected");

        auto badRoster = good.Members();
        badRoster["roster"] = JsonValue::String("nope");
        Check(!CampaignState::FromJson(
                  JsonValue::MakeObject(std::move(badRoster)))
                   .ok(),
              "non-array roster rejected");

        // Broken embedded ledger -> whole doc rejected.
        auto badLedger = good.Members();
        JsonValue::Object led = badLedger["ledger"].Members();
        JsonValue::Array ents = led["entries"].Items();
        JsonValue::Object e0 = ents[0].Members();
        e0["memo"] = JsonValue::String("tampered");
        ents[0] = JsonValue::MakeObject(std::move(e0));
        led["entries"] = JsonValue::MakeArray(std::move(ents));
        badLedger["ledger"] = JsonValue::MakeObject(std::move(led));
        Check(!CampaignState::FromJson(
                  JsonValue::MakeObject(std::move(badLedger)))
                   .ok(),
              "tampered embedded ledger rejected");

        // Roster counter bounds.
        auto badVet = good.Members();
        JsonValue::Array ro = badVet["roster"].Items();
        JsonValue::Object r0 = ro[0].Members();
        r0["veterancy"] = JsonValue::Int(-1);
        ro[0] = JsonValue::MakeObject(std::move(r0));
        badVet["roster"] = JsonValue::MakeArray(std::move(ro));
        Check(!CampaignState::FromJson(
                  JsonValue::MakeObject(std::move(badVet)))
                   .ok(),
              "negative veterancy rejected");

        // Duplicate roster names rejected.
        auto dup = good.Members();
        ro = dup["roster"].Items();
        JsonValue::Object r1 = ro[1].Members();
        r1["name"] = ro[0]["name"];
        ro[1] = JsonValue::MakeObject(std::move(r1));
        dup["roster"] = JsonValue::MakeArray(std::move(ro));
        Check(!CampaignState::FromJson(
                  JsonValue::MakeObject(std::move(dup)))
                   .ok(),
              "duplicate roster name rejected");

        // Parallel chapter vectors must agree.
        auto badCh = good.Members();
        ch = badCh["chapter"].Members();
        ch["resolved"] = JsonValue::MakeArray({JsonValue::Bool(true)});
        badCh["chapter"] = JsonValue::MakeObject(std::move(ch));
        Check(!CampaignState::FromJson(
                  JsonValue::MakeObject(std::move(badCh)))
                   .ok(),
              "mismatched chapter vectors rejected");

        // current outside the chapter space.
        auto badIdx = good.Members();
        ch = badIdx["chapter"].Members();
        ch["current"] = JsonValue::Int(9); // > unlocked.size()==4
        badIdx["chapter"] = JsonValue::MakeObject(std::move(ch));
        Check(!CampaignState::FromJson(
                  JsonValue::MakeObject(std::move(badIdx)))
                   .ok(),
              "current beyond chapter space rejected");
    }

    // --- edges ---
    {
        CampaignState empty; // all defaults
        auto jr = empty.ToJson();
        auto back = CampaignState::FromJson(jr.value);
        Check(back.ok() && back.value.GetRoster().empty() &&
                  back.value.GetChapter().current == 0 &&
                  back.value.GetLedger().Size() == 0,
              "empty campaign round-trips");

        // Deterministic emit: serialize twice, byte-identical.
        Check(jr.value.Emit() == empty.ToJson().value.Emit(),
              "emit is deterministic");

        // Full text pipeline: Emit -> Parse -> FromJson.
        auto parsed = JsonValue::Parse(jr.value.Emit());
        Check(parsed.ok() &&
                  CampaignState::FromJson(parsed.value).ok(),
              "emit->parse->load pipeline round-trips");

        // The REAL save path on full state: CJK names and the
        // embedded chain travel Emit->Parse->FromJson.
        auto text = SampleState().ToJson().value.Emit();
        auto full = JsonValue::Parse(text);
        Check(full.ok(), "full state parses");
        auto fullBack = CampaignState::FromJson(full.value);
        Check(fullBack.ok() &&
                  fullBack.value.GetRoster()[0].name ==
                      "右翼第三隊" &&
                  fullBack.value.GetLedger().Verify() == nullptr &&
                  fullBack.value.GetLedger().Size() == 2,
              "full-state text pipeline round-trips");

        // Campaign-complete sentinel: current == vector size.
        CampaignState done;
        done.GetChapter().current = 2;
        done.GetChapter().unlocked = {true, true};
        done.GetChapter().resolved = {true, true};
        Check(CampaignState::FromJson(done.ToJson().value).ok(),
              "campaign-complete sentinel round-trips");
    }

    // ===== Story 3.2 — SaveSystem =====
    {
        namespace fs = std::filesystem;
        const fs::path dir =
            fs::temp_directory_path() / "potato_test_saves";
        fs::remove_all(dir); // clean slate
        SaveSystem saves(dir);

        // Save -> load round-trip through real files.
        {
            const CampaignState st = SampleState();
            const auto w = saves.Save(0, st);
            Check(w.ok() && fs::exists(w.value),
                  "save writes the slot file");
            auto back = saves.Load(0);
            Check(back.ok() &&
                      back.value.GetRoster()[0].name ==
                          "右翼第三隊" &&
                      back.value.GetLedger().Size() == 2 &&
                      back.value.GetLedger().Verify() == nullptr,
                  "save->load round-trips full state");
            Check(saves.ListSlots() == std::vector<int>{0},
                  "ListSlots reports written slot");
        }

        // Overwrite: second save replaces.
        {
            CampaignState st;
            st.GetChapter().current = 1;
            st.GetChapter().unlocked = {true, true};
            st.GetChapter().resolved = {true, false};
            Check(saves.Save(0, st).ok(), "overwrite save writes");
            auto back = saves.Load(0);
            Check(back.ok() &&
                      back.value.GetChapter().current == 1 &&
                      back.value.GetRoster().empty(),
                  "overwrite replaces prior save");
        }

        // Crash simulation: a stale .tmp must not shadow the
        // committed .json.
        {
            const fs::path tmp = SaveSystem::TmpPath(dir, 0);
            std::ofstream junk(tmp, std::ios::binary | std::ios::trunc);
            junk << "{\"schema\":\"potato.campaign/1\",\"chapte";
            junk.close();
            auto back = saves.Load(0);
            Check(back.ok() && back.value.GetChapter().current == 1,
                  "stale .tmp never shadows committed save");
            fs::remove(tmp);
        }

        // Corrupt committed file -> typed reject; next Load still
        // fails (nothing mutated) but a later good Save works.
        {
            const fs::path dst = SaveSystem::SlotPath(dir, 1);
            std::ofstream bad(dst,
                              std::ios::binary | std::ios::trunc);
            bad << "{\"schema\":\"potato.campaign/99\"}";
            bad.close();
            auto r = saves.Load(1);
            Check(!r.ok() && r.error == "schema" && !r.reason.empty(),
                  "bad schema rejected with reason class");
            auto r2 = saves.Load(1);
            Check(!r2.ok(), "rejected file stays rejected");
        }

        // Malformed committed .json -> "parse" class rejection.
        {
            const fs::path dst = SaveSystem::SlotPath(dir, 2);
            std::ofstream bad(dst,
                              std::ios::binary | std::ios::trunc);
            bad << "{\"schema\":\"potato.campaign/1\",\"chapter\":";
            bad.close();
            const auto r = saves.Load(2);
            Check(!r.ok() && r.error == "parse",
                  "torn committed file rejected as parse error");
        }

        // Missing file + slot bounds.
        {
            Check(!saves.Load(5).ok(), "missing slot rejected");
            Check(!saves.Save(-1, SampleState()).ok() &&
                      !saves.Load(-1).ok() &&
                      !saves.Save(8, SampleState()).ok(),
                  "slot bounds enforced before I/O");
        }

        fs::remove_all(dir);
    }

    // ===== Story 3.3 — ChapterLibrary =====
    {
        namespace fs = std::filesystem;
        const fs::path cdir =
            fs::temp_directory_path() / "potato_test_chapters";
        fs::remove_all(cdir);
        fs::create_directories(cdir);
        const auto write = [&cdir](const char* name,
                                   const std::string& text) {
            std::ofstream f(cdir / name,
                            std::ios::binary | std::ios::trunc);
            f << text;
        };
        const auto write_s = [](const fs::path& d,
                                const char* name,
                                const std::string& text) {
            std::ofstream f(d / name,
                            std::ios::binary | std::ios::trunc);
            f << text;
        };
        const auto chapter = [](const char* id, int idx,
                                bool combat = true) {
            return std::string(
                       "{\"schema\":\"potato.chapter/1\","
                       "\"id\":\"") +
                   id + "\",\"index\":" + std::to_string(idx) +
                   ",\"title\":\"t\",\"map\":\"m.json\",\"combat\":" +
                   (combat ? "true" : "false") + "}";
        };

        write("02_second.json", chapter("ch_b", 1));
        write("01_first.json", chapter("ch_a", 0));
        write("03_talk.json", chapter("ch_c", 2, false));
        write("bad_schema.json",
              "{\"schema\":\"potato.chapter/2\",\"id\":\"x\"}");
        write("bad_field.json",
              "{\"schema\":\"potato.chapter/1\",\"id\":\"y\"}");
        write("dup_id.json", chapter("ch_a", 3));
        write("dup_index.json", chapter("ch_d", 0));
        write("not_json.txt",
              "{\"schema\":\"potato.chapter/1\"}"); // skipped ext

        ChapterLibrary lib;
        const auto res = ChapterLibrary::Load(cdir, lib);
        Check(res.ok, "library load succeeds");
        Check(lib.Size() == 3, "valid chapters registered");
        Check(res.rejected.size() == 4,
              "bad schema/field/dup-id/dup-index each rejected");

        // Registered in index order regardless of filename.
        Check(lib.Chapters()[0].id == "ch_a" &&
                  lib.Chapters()[1].id == "ch_b" &&
                  lib.Chapters()[2].id == "ch_c",
              "library ordered by index");
        Check(lib.Find("ch_b") != nullptr &&
                  lib.Find("nope") == nullptr,
              "Find by id works");
        Check(lib.AtIndex(2) != nullptr &&
                  !lib.AtIndex(2)->combat &&
                  lib.AtIndex(5) == nullptr,
              "AtIndex + zero-combat flag");

        // Empty readable dir -> empty library, ok.
        {
            const fs::path empty =
                fs::temp_directory_path() / "potato_test_empty_ch";
            fs::remove_all(empty);
            fs::create_directories(empty);
            ChapterLibrary e;
            const auto r = ChapterLibrary::Load(empty, e);
            Check(r.ok && e.Size() == 0,
                  "empty dir yields empty library");
            fs::remove_all(empty);
        }

        // Missing dir -> io failure.
        {
            ChapterLibrary x;
            const auto r = ChapterLibrary::Load(
                cdir / "does_not_exist", x);
            Check(!r.ok && r.error == "io",
                  "unreadable dir fails the load");
        }

        // Sparse indexes: legal; chapter space is [0, MaxIndex()+1).
        {
            const fs::path sdir =
                fs::temp_directory_path() / "potato_test_sparse_ch";
            fs::remove_all(sdir);
            fs::create_directories(sdir);
            write_s(sdir, "a.json", chapter("s_a", 0));
            write_s(sdir, "b.json", chapter("s_b", 5));
            ChapterLibrary sl;
            const auto r = ChapterLibrary::Load(sdir, sl);
            Check(r.ok && sl.Size() == 2 && sl.MaxIndex() == 5 &&
                      sl.AtIndex(3) == nullptr &&
                      sl.AtIndex(5)->id == "s_b",
                  "sparse indexes: gap is dead space, not error");
            fs::remove_all(sdir);
        }

        fs::remove_all(cdir);
    }

    // ===== Story 3.4 — Chapter progression =====
    {
        namespace fs = std::filesystem;
        const fs::path pdir =
            fs::temp_directory_path() / "potato_test_prog_ch";
        fs::remove_all(pdir);
        fs::create_directories(pdir);
        const auto wch = [](const fs::path& dir, const char* name,
                            const std::string& text) {
            std::ofstream f(dir / name,
                            std::ios::binary | std::ios::trunc);
            f << text;
        };
        const auto chapter = [](const char* id, int idx) {
            return std::string(
                       "{\"schema\":\"potato.chapter/1\","
                       "\"id\":\"") +
                   id + "\",\"index\":" + std::to_string(idx) +
                   ",\"title\":\"t\",\"map\":\"m.json\",\"combat\":true}";
        };

        // Dense run: chapters at 0,1,2.
        wch(pdir, "a.json", chapter("p0", 0));
        wch(pdir, "b.json", chapter("p1", 1));
        wch(pdir, "c.json", chapter("p2", 2));
        ChapterLibrary lib;
        Check(ChapterLibrary::Load(pdir, lib).ok,
              "progression library loads");

        CampaignState st;
        st.GetLedger().Post(Burn());
        st.GetRoster().push_back(
            RosterEntry{"敢死隊", 0, 0, false});
        Check(InitializeProgress(st, lib).ok(),
              "InitializeProgress succeeds");
        const auto& cp0 = st.GetChapter();
        Check(cp0.current == 0 && cp0.unlocked.size() == 3 &&
                  cp0.unlocked[0] && !cp0.unlocked[1],
              "init: first chapter current+unlocked");

        // Advance: resolve ch0 -> unlock ch1.
        Check(ResolveAndAdvance(st, lib).ok(),
              "resolve advances");
        Check(st.GetChapter().resolved[0] &&
                  st.GetChapter().current == 1 &&
                  st.GetChapter().unlocked[1],
              "next chapter unlocked, prior resolved");

        // Carry-forward: roster+ledger are the same state.
        Check(st.GetRoster().size() == 1 &&
                  st.GetLedger().Size() == 1,
              "roster+ledger carry forward untouched");

        // To the end: complete sentinel = space (3).
        Check(ResolveAndAdvance(st, lib).ok() &&
                  ResolveAndAdvance(st, lib).ok(),
              "final chapters resolve");
        Check(st.GetChapter().current == 3,
              "completion sentinel reached");
        Check(!ResolveAndAdvance(st, lib).ok(),
              "advancing a complete campaign rejected");
        Check(st.GetChapter().resolved == std::vector<bool>(
                                              {true, true, true}),
              "all chapters marked resolved");

        // Complete campaign state round-trips (3.1 sentinel fix).
        Check(CampaignState::FromJson(st.ToJson().value).ok(),
              "completed campaign round-trips");

        // Sparse library {0,5}: vectors size 6, dead slots stay
        // locked.
        {
            const fs::path sdir = fs::temp_directory_path() /
                                  "potato_test_prog_sparse";
            fs::remove_all(sdir);
            fs::create_directories(sdir);
            wch(sdir, "a.json", chapter("s0", 0));
            wch(sdir, "b.json", chapter("s5", 5));
            ChapterLibrary sl;
            Check(ChapterLibrary::Load(sdir, sl).ok,
                  "sparse progression library loads");
            CampaignState ss;
            Check(InitializeProgress(ss, sl).ok() &&
                      ss.GetChapter().unlocked.size() == 6 &&
                      ss.GetChapter().current == 0,
                  "sparse init sizes to chapter space");
            Check(ResolveAndAdvance(ss, sl).ok() &&
                      ss.GetChapter().current == 5 &&
                      !ss.GetChapter().unlocked[3] &&
                      ss.GetChapter().unlocked[5],
                  "sparse advance skips dead slots");
            fs::remove_all(sdir);
        }

        // Uninitialized progress vectors can't advance.
        {
            CampaignState bad;
            Check(!ResolveAndAdvance(bad, lib).ok(),
                  "uninitialized progress rejected");
        }

        // Re-init refuses to wipe an in-progress campaign.
        {
            CampaignState mid;
            InitializeProgress(mid, lib);
            ResolveAndAdvance(mid, lib);
            Check(!InitializeProgress(mid, lib).ok(),
                  "re-init on live progress rejected");
        }

        // Dead-slot current: sparse lib {0,5}, vectors sized 6,
        // current=3 (dead) -> rejected. And a chapter that was
        // never unlocked can't resolve.
        {
            const fs::path sdir = fs::temp_directory_path() /
                                  "potato_test_prog_dead";
            fs::remove_all(sdir);
            fs::create_directories(sdir);
            wch(sdir, "a.json", chapter("d0", 0));
            wch(sdir, "b.json", chapter("d5", 5));
            ChapterLibrary dl;
            ChapterLibrary::Load(sdir, dl);
            CampaignState ds;
            InitializeProgress(ds, dl);
            ds.GetChapter().current = 3; // dead slot
            Check(!ResolveAndAdvance(ds, dl).ok(),
                  "dead-slot current rejected");
            ds.GetChapter().current = 5;
            ds.GetChapter().unlocked[5] = false;
            Check(!ResolveAndAdvance(ds, dl).ok(),
                  "locked chapter cannot resolve");
            fs::remove_all(sdir);
        }

        // Empty library can't init.
        {
            ChapterLibrary el;
            CampaignState es;
            Check(!InitializeProgress(es, el).ok(),
                  "empty library init rejected");
        }

        // 64-deep boundary: the complete sentinel at
        // current==MAX_CHAPTERS round-trips; 65 rejects.
        {
            CampaignState full;
            full.GetChapter().unlocked.assign(64, true);
            full.GetChapter().resolved.assign(64, true);
            full.GetChapter().current = 64;
            Check(CampaignState::FromJson(full.ToJson().value).ok(),
                  "64-deep complete sentinel round-trips");
            full.GetChapter().current = 65;
            Check(!CampaignState::FromJson(full.ToJson().value)
                       .ok(),
                  "current beyond MAX_CHAPTERS rejected");
        }

        fs::remove_all(pdir);
    }

    // ===== Story 12.6 — Chapter anchoring (bind/predicate graph)
    {
        namespace fs = std::filesystem;
        using Potato::Campaign::CampaignComplete;
        using Potato::Campaign::ChapterAvailable;
        using Potato::Campaign::ChapterBeat;
        using Potato::Campaign::RefreshAvailability;
        using Potato::Campaign::SetCurrentChapter;
        using Potato::Campaign::WorldControl;
        using Potato::Campaign::WorldMap;
        using Potato::Campaign::WorldState;

        const fs::path bdir =
            fs::temp_directory_path() / "potato_test_bind_ch";
        fs::remove_all(bdir);
        fs::create_directories(bdir);
        const auto wch = [](const fs::path& dir, const char* name,
                            const std::string& text) {
            std::ofstream f(dir / name,
                            std::ios::binary | std::ios::trunc);
            f << text;
        };
        const auto chapter = [](const char* id, int idx,
                                const std::string& bind = "") {
            return std::string(
                       "{\"schema\":\"potato.chapter/1\","
                       "\"id\":\"") +
                   id + "\",\"index\":" + std::to_string(idx) +
                   ",\"title\":\"t\",\"map\":\"m.json\","
                   "\"combat\":true" + bind + "}";
        };

        // --- wire decode + strict-inner-key rejections ---
        wch(bdir, "a.json",
            chapter("a0", 0, ",\"bind\":{\"node\":\"longmen\","
                             "\"control\":\"player\","
                             "\"resolved\":[\"gate\"],"
                             "\"requires\":[\"a0\"],"
                             "\"ledger\":{\"axis\":\"corruption\","
                             "\"at_least\":5},"
                             "\"mandatory\":true,"
                             "\"beat\":\"pivot\"}"));
        wch(bdir, "bad_bind_type.json",
            chapter("bb", 1, ",\"bind\":\"x\""));
        wch(bdir, "bad_key.json",
            chapter("bk", 2, ",\"bind\":{\"nope\":1}"));
        wch(bdir, "bad_ctrl.json",
            chapter("bc", 3, ",\"bind\":{\"control\":\"friend\"}"));
        wch(bdir, "ctrl_no_node.json",
            chapter("cn", 4, ",\"bind\":{\"control\":\"player\"}"));
        wch(bdir, "bad_axis.json",
            chapter("ba", 5, ",\"bind\":{\"ledger\":{\"axis\":"
                             "\"mandate\",\"at_least\":1}}"));
        wch(bdir, "bad_atleast.json",
            chapter("bl", 6, ",\"bind\":{\"ledger\":{\"axis\":"
                             "\"order\",\"at_least\":0}}"));
        wch(bdir, "bad_beat.json",
            chapter("be", 7, ",\"bind\":{\"beat\":\"finale\"}"));
        wch(bdir, "req_dangling.json",
            chapter("rd", 8, ",\"bind\":{\"requires\":[\"ghost\"]}"));
        // Cascade: rc requires rd, which gets pruned.
        wch(bdir, "req_cascade.json",
            chapter("rc", 9, ",\"bind\":{\"requires\":[\"rd\"]}"));
        ChapterLibrary blib;
        const auto bres = ChapterLibrary::Load(bdir, blib);
        Check(bres.ok && blib.Size() == 1,
              "bind library: one valid chapter loads");
        Check(bres.rejected.size() == 9,
              "bind mistypes/unknown keys/dangling+cascade "
              "requires rejected");
        {
            const Potato::Campaign::ChapterDef& d = *blib.Find("a0");
            Check(d.bound && d.bind.node == "longmen" &&
                      d.bind.hasControl &&
                      d.bind.control == WorldControl::Player &&
                      d.bind.resolved.size() == 1 &&
                      d.bind.prereqs.size() == 1 &&
                      d.bind.prereqs[0] == "a0" &&
                      d.bind.hasLedger && d.bind.atLeast == 5 &&
                      d.bind.IsMandatory() &&
                      d.bind.beat == ChapterBeat::Pivot,
                  "bind fields decode");
        }

        // --- predicate evaluation + latch ---
        auto wdoc = JsonValue::Parse(
            "{\"schema\":\"potato.world/1\",\"id\":\"w\","
            "\"nodes\":[{\"id\":\"kaifeng\",\"control\":\"player\"},"
            "{\"id\":\"longmen\",\"control\":\"neutral\"}],"
            "\"routes\":[{\"a\":\"kaifeng\",\"b\":\"longmen\","
            "\"days\":1}],\"start\":\"kaifeng\"}");
        Check(wdoc.ok(), "world fixture parses");
        auto wmap = WorldMap::FromJson(wdoc.value);
        Check(wmap.ok(), "world fixture loads");
        auto ws = WorldState::Init(wmap.value, 7);
        Check(ws.ok() && ws.value.WarbandAt() == "kaifeng",
              "world state inits at start");

        // Bound library: ch0 unbound anchor; ch1 requires ch0 and
        // warband at longmen; ch2 ledger-gated (corruption >= 30),
        // mandatory pivot; ch3 optional side chapter gated on POI
        // resolution; ch4 bound requires+control player@kaifeng.
        const fs::path gdir =
            fs::temp_directory_path() / "potato_test_graph_ch";
        fs::remove_all(gdir);
        fs::create_directories(gdir);
        wch(gdir, "a.json", chapter("g0", 0));
        wch(gdir, "b.json",
            chapter("g1", 1, ",\"bind\":{\"requires\":[\"g0\"],"
                             "\"node\":\"longmen\"}"));
        wch(gdir, "c.json",
            chapter("g2", 2, ",\"bind\":{\"requires\":[\"g0\"],"
                             "\"ledger\":{\"axis\":\"corruption\","
                             "\"at_least\":30},"
                             "\"beat\":\"pivot\"}"));
        wch(gdir, "d.json",
            chapter("g3", 3, ",\"bind\":{\"resolved\":[\"shrine_x\"]}"
                             ));
        wch(gdir, "e.json",
            chapter("g4", 4, ",\"bind\":{\"node\":\"kaifeng\","
                             "\"control\":\"player\","
                             "\"mandatory\":true}"));
        ChapterLibrary glib;
        Check(ChapterLibrary::Load(gdir, glib).ok &&
                  glib.Size() == 5,
              "graph library loads");

        CampaignState st;
        Check(InitializeProgress(st, glib).ok() &&
                  st.GetChapter().current == 0,
              "graph init lands first chapter");
        // Null-world refresh inside init: unbound g0 latches;
        // g4's kaifeng predicates need a live world — not latched.
        Check(st.GetChapter().unlocked[0] &&
                  !st.GetChapter().unlocked[4],
              "node-bound chapter needs live world");
        Check(!ChapterAvailable(*glib.Find("g1"), glib, st,
                                &ws.value, &wmap.value),
              "requires-unmet chapter not available");
        RefreshAvailability(st, glib, &ws.value, &wmap.value);
        Check(st.GetChapter().unlocked[4],
              "world refresh latches control+node bind");
        // Latch is monotone: leave kaifeng, flag stays.
        ws.value.SetWarband("longmen", wmap.value);
        RefreshAvailability(st, glib, &ws.value, &wmap.value);
        Check(st.GetChapter().unlocked[4],
              "latch survives departure");

        // SetCurrentChapter: locked rejected, unlocked accepted.
        Check(!SetCurrentChapter(st, glib, 3),
              "locked chapter cannot be selected");
        Check(SetCurrentChapter(st, glib, 4) &&
                  st.GetChapter().current == 4,
              "shell picks among unlocked");

        // requires gating: g1 needs g0 resolved — resolve g0 via
        // the ceremony path by pointing current back at it.
        Check(SetCurrentChapter(st, glib, 0),
              "re-point at unbound opener");
        Check(ResolveAndAdvance(st, glib).ok(),
              "resolve g0");
        // g1's requires holds now but node predicate needs warband
        // at longmen (warband IS there) — null-world refresh in
        // ResolveAndAdvance can't see it; world refresh can.
        Check(!st.GetChapter().unlocked[1],
              "node bind not latched by null-world refresh");
        RefreshAvailability(st, glib, &ws.value, &wmap.value);
        Check(st.GetChapter().unlocked[1],
              "arrival + requires latch g1");
        Check(!st.GetChapter().unlocked[2],
              "ledger threshold unmet keeps g2 locked");
        Check(!st.GetChapter().unlocked[3],
              "unresolved POI keeps g3 locked");

        // Ledger predicate: corruption:+30 crosses the pivot gate.
        {
            Posting p;
            p.credit = {Account::ArmyPrestige, 1};
            p.debit = {Account::Materiel, 1};
            p.memo = "atrocity";
            p.tags = {"corruption:+30"};
            Check(st.GetLedger().Post(p).ok(),
                  "corruption posting lands");
        }
        RefreshAvailability(st, glib, &ws.value, &wmap.value);
        Check(st.GetChapter().unlocked[2],
              "ledger threshold latches pivot chapter");
        ws.value.MarkResolved("shrine_x");
        RefreshAvailability(st, glib, &ws.value, &wmap.value);
        Check(st.GetChapter().unlocked[3],
              "POI resolution latches side chapter");

        // Out-of-order graph resolution: play g2 before g1.
        Check(SetCurrentChapter(st, glib, 2),
              "graph allows out-of-order pick");
        Check(ResolveAndAdvance(st, glib).ok(),
              "resolve g2 out of index order");
        Check(st.GetChapter().resolved[2] &&
                  !st.GetChapter().resolved[1],
              "g2 resolved while g1 untouched");

        // Mandatory spine: g2 (beat pivot) + g4 (mandatory) are
        // the spine — g1/g3 optional. Resolving g4 completes the
        // campaign even though g1/g3 are open.
        Check(!CampaignComplete(st, glib),
              "open mandatory chapter blocks completion");
        Check(SetCurrentChapter(st, glib, 4) &&
                  ResolveAndAdvance(st, glib).ok(),
              "resolve final mandatory chapter");
        Check(CampaignComplete(st, glib) &&
                  st.GetChapter().current == 5,
              "mandatory spine complete -> sentinel");
        Check(!st.GetChapter().resolved[1] &&
                  !st.GetChapter().resolved[3],
              "optional chapters unresolved at completion");

        fs::remove_all(bdir);
        fs::remove_all(gdir);
    }

    // --- Story 3.5: persistent roster ---
    {
        // Enlist is the roster's write path.
        {
            CampaignState st;
            const auto i0 = Enlist(st, "alpha");
            Check(i0.ok() && i0.value == 0, "enlist returns index");
            Check(st.GetRoster().size() == 1 &&
                      !st.GetRoster()[0].dead,
                  "enlist appends live entry");
            Check(!Enlist(st, "alpha").ok(),
                  "duplicate enlist rejected");
            Check(!Enlist(st, "").ok(),
                  "empty name rejected");
            Check(!Enlist(st, std::string(
                              CampaignState::MAX_NAME_LEN + 1,
                              'x'))
                       .ok(),
                  "oversize name rejected");
            Check(st.GetRoster().size() == 1,
                  "rejected enlists did not mutate");
        }

        // Aftermath: dead stays dead, survivors gain veterancy.
        {
            CampaignState st;
            Enlist(st, "vanguard");
            Enlist(st, "rearguard");
            const auto r = ApplyAftermath(
                st, {{"vanguard", 12, false},
                     {"rearguard", 30, true}});
            Check(r.ok() && r.value.rows == 2 &&
                      r.value.buried == 1 &&
                      r.value.veterans == 1,
                  "aftermath applies");
            const auto& e0 = st.GetRoster()[0];
            const auto& e1 = st.GetRoster()[1];
            Check(e0.veterancy == 1 && e0.casualties == 12 &&
                      !e0.dead,
                  "survivor gains veterancy");
            Check(e1.dead && e1.casualties == 30,
                  "wiped squad persists as dead");

            // Dead stays dead — a second aftermath on a dead
            // squad is a forged report, rejected atomically.
            const auto r2 = ApplyAftermath(
                st, {{"vanguard", 5, false},
                     {"rearguard", 1, false}});
            Check(!r2.ok() &&
                      st.GetRoster()[0].casualties == 12,
                  "dead-squad row rejects whole aftermath");

            // Second battle for the survivor stacks veterancy.
            ApplyAftermath(st, std::vector<AftermathRow>{{"vanguard", 3, false}});
            Check(st.GetRoster()[0].veterancy == 2 &&
                      st.GetRoster()[0].casualties == 15,
                  "survivor stacks veterancy");
        }

        // Atomicity: any bad row rejects the whole report.
        {
            CampaignState st;
            Enlist(st, "a");
            Enlist(st, "b");
            Check(!ApplyAftermath(st, std::vector<AftermathRow>{{"a", 1, false},
                                       {"ghost", 1, false}})
                       .ok(),
                  "unknown name rejected");
            Check(!ApplyAftermath(st, std::vector<AftermathRow>{{"a", 1, false},
                                       {"a", 2, false}})
                       .ok(),
                  "duplicate row rejected");
            Check(!ApplyAftermath(st, std::vector<AftermathRow>{{"a", -1, false}}).ok(),
                  "negative casualties rejected");
            Check(!ApplyAftermath(
                       st, {{"a",
                             CampaignState::MAX_ROSTER_COUNT + 1,
                             false}})
                       .ok(),
                  "unbounded casualties rejected");
            Check(st.GetRoster()[0].casualties == 0 &&
                      st.GetRoster()[0].veterancy == 0,
                  "rejected aftermath left no trace");
            Check(ApplyAftermath(st, {}).ok(),
                  "empty aftermath is a no-op");
        }

        // Round-trip: aftermath state survives the wire, and
        // aftermath applies on a FromJson-loaded roster.
        {
            CampaignState st;
            Enlist(st, "\xE5\x8F\xB3\xE7\xBF\xBC"); // 右翼
            ApplyAftermath(st, std::vector<AftermathRow>{{"\xE5\x8F\xB3\xE7\xBF\xBC", 7,
                                 true}});
            const auto doc = st.ToJson();
            const auto back =
                doc.ok() ? CampaignState::FromJson(doc.value)
                         : Gameplay::Result<CampaignState>{};
            Check(back.ok() && back.value.GetRoster()[0].dead &&
                      back.value.GetRoster()[0].casualties == 7,
                  "aftermath state round-trips");

            // Loaded state continues to take aftermath.
            CampaignState st2;
            Enlist(st2, "f");
            st2 = CampaignState::FromJson(st2.ToJson().value)
                      .value;
            const std::vector<AftermathRow> rows{
                {"f", 2, false}};
            Check(ApplyAftermath(st2, rows).ok() &&
                      st2.GetRoster()[0].veterancy == 1,
                  "aftermath on loaded roster");
        }

        // Boundaries + the mutable-accessor trust boundary.
        {
            CampaignState st;
            // Name exactly at MAX_NAME_LEN is legal.
            Check(Enlist(st, std::string(
                               CampaignState::MAX_NAME_LEN, 'n'))
                       .ok(),
                  "name at MAX_NAME_LEN accepted");
            // Roster fills at 256; 257th rejected.
            while (st.GetRoster().size() <
                   CampaignState::MAX_ROSTER) {
                Enlist(st, "s" +
                               std::to_string(
                                   st.GetRoster().size()));
            }
            Check(st.GetRoster().size() ==
                          CampaignState::MAX_ROSTER &&
                      !Enlist(st, "overflow").ok(),
                  "roster full rejected");
            // Oversized report rejected before per-row checks.
            Check(!ApplyAftermath(
                       st, std::vector<AftermathRow>(
                               st.GetRoster().size() + 1))
                       .ok(),
                  "oversized report rejected");
        }

        {
            CampaignState st;
            Enlist(st, "cap");
            // casualties at exactly MAX is legal.
            Check(ApplyAftermath(
                       st, {{"cap",
                             CampaignState::MAX_ROSTER_COUNT,
                             false}})
                       .ok(),
                  "casualties at bound accepted");
            // One more pushes over the bound -> whole report
            // rejected.
            const std::vector<AftermathRow> over{
                {"cap", 1, false}};
            Check(!ApplyAftermath(st, over).ok() &&
                      st.GetRoster()[0].casualties ==
                          CampaignState::MAX_ROSTER_COUNT,
                  "cumulative overflow rejected atomically");
        }

        {
            CampaignState st;
            Enlist(st, "vet");
            // Live-state bypass: write veterancy at the bound via
            // the mutable accessor (as a corrupt runtime could),
            // then aftermath must refuse the survivor path — but
            // a wipe still lands (wiped rows skip veterancy).
            st.GetRoster()[0].veterancy =
                CampaignState::MAX_ROSTER_COUNT;
            const std::vector<AftermathRow> surv{
                {"vet", 0, false}};
            const std::vector<AftermathRow> wipe{
                {"vet", 0, true}};
            Check(!ApplyAftermath(st, surv).ok(),
                  "veterancy-bound survivor rejected");
            Check(ApplyAftermath(st, wipe).ok() &&
                      st.GetRoster()[0].dead,
                  "wiped row bypasses veterancy check");
        }

        // Corrupted live roster (bypassed setters) can no longer
        // poison a save: ToJson re-validates at the boundary.
        {
            CampaignState st;
            st.GetRoster().push_back(
                RosterEntry{"x", 0, 0, false});
            st.GetRoster().push_back(
                RosterEntry{"x", 0, 0, false}); // dup name
            Check(!st.ToJson().ok(),
                  "ToJson rejects bypassed-invalid roster");
            CampaignState st2;
            st2.GetRoster().push_back(
                RosterEntry{"y", -1, 0, false}); // bad counter
            Check(!st2.ToJson().ok(),
                  "ToJson rejects bypassed-negative counter");
        }
    }

    // --- Story 3.6: RefitCamp ---
    {
        // Band table pins (GDD economy tempo).
        {
            const auto e = BandCosts(1);
            const auto m = BandCosts(5);
            const auto l = BandCosts(10);
            Check(e.heal == 20 && e.recruit == 40 &&
                      e.plunderGain == 30 && e.plunderCost == 10,
                  "early band costs");
            Check(m.heal == 30 && m.recruit == 60 &&
                      m.plunderGain == 45 && m.plunderCost == 15,
                  "mid band costs");
            Check(l.heal == 40 && l.recruit == 80 &&
                      l.plunderGain == 60 && l.plunderCost == 20,
                  "late band costs");
            Check(BandCosts(-1).heal == 40 &&
                      BandCosts(99).heal == 40,
                  "band index clamps");
            Check(BandCosts(2).heal == 20 &&
                      BandCosts(3).heal == 30 &&
                      BandCosts(7).heal == 30 &&
                      BandCosts(8).heal == 40,
                  "band edges partition correctly");
        }

        // Funded refit: deploy/heal/recruit/plunder all land.
        {
            CampaignState st;
            Enlist(st, "iron");
            ApplyAftermath(st, {{"iron", 25, false}});
            Posting grant;
            grant.credit = {Account::Materiel, 100};
            grant.debit = {Account::Mandate, 100};
            grant.memo = "war chest";
            st.GetLedger().Post(grant);
            const std::int64_t mat0 =
                st.GetLedger().Balance(Account::Materiel);
            const std::int64_t prest0 =
                st.GetLedger().Balance(Account::ArmyPrestige);
            const auto entries0 = st.GetLedger().Size();

            const auto dep = Deploy(st, "iron");
            Check(dep.ok() && dep.value == 0,
                  "deploy validates live squad");
            Check(Heal(st, "iron", 1).ok() &&
                      st.GetRoster()[0].casualties == 0,
                  "heal clears casualties");
            Check(st.GetLedger().Balance(Account::Materiel) ==
                          mat0 - 20 &&
                      st.GetLedger().Balance(
                          Account::ArmyPrestige) == prest0 + 20,
                  "heal posts debit/credit pair");

            Check(Recruit(st, "fresh", 1).ok() &&
                      st.GetRoster().size() == 2,
                  "recruit enlists");
            Check(st.GetLedger().Balance(Account::Materiel) ==
                          mat0 - 20 - 40,
                  "recruit posts cost");

            Check(Plunder(st, 1).ok() &&
                      st.GetLedger().Balance(Account::Materiel) ==
                          mat0 - 60 + 30 &&
                      st.GetLedger().Balance(
                          Account::PopularSupport) == -10,
                  "plunder posts asymmetric pair");
            Check(st.GetLedger().Entries().back().tags ==
                          std::vector<std::string>{"plunder"},
                  "plunder carries fold tag");
            Check(st.GetLedger().Size() == entries0 + 3,
                  "refit appends three postings");
        }

        // Overspend: rejected with skip reason, nothing mutates.
        {
            CampaignState st;
            Enlist(st, "poor");
            ApplyAftermath(st, {{"poor", 9, false}});
            const auto size0 = st.GetLedger().Size();
            const auto r = Heal(st, "poor", 1);
            Check(!r.ok() &&
                      r.reason.find("insufficient") !=
                          std::string::npos,
                  "overspend rejected with skip reason");
            Check(st.GetLedger().Size() == size0 &&
                      st.GetRoster()[0].casualties == 9,
                  "overspend leaves ledger+roster untouched");
        }

        // Skips that aren't overspend.
        {
            CampaignState st;
            Enlist(st, "dead");
            ApplyAftermath(st, {{"dead", 5, true}});
            Posting g;
            g.credit = {Account::Materiel, 500};
            g.debit = {Account::Mandate, 500};
            st.GetLedger().Post(g);
            Check(!Deploy(st, "dead").ok(),
                  "dead squad cannot deploy");
            Check(!Heal(st, "dead", 1).ok() &&
                      st.GetRoster()[0].dead,
                  "dead squad cannot heal");
            Check(!Heal(st, "ghost", 1).ok(),
                  "heal unknown squad rejected");
            Check(!Deploy(st, "ghost").ok(),
                  "deploy unknown squad rejected");
            Enlist(st, "fit");
            Check(!Heal(st, "fit", 1).ok(),
                  "full-strength heal skipped");
            Check(!Recruit(st, "dead", 1).ok(),
                  "recruit dup name rejected");
            Check(!Recruit(st, "", 1).ok(),
                  "recruit empty name rejected");
            Check(!Recruit(st,
                           std::string(
                               CampaignState::MAX_NAME_LEN + 1,
                               'x'),
                           1)
                       .ok(),
                  "recruit oversize name rejected");
            Check(st.GetLedger().Size() == 1,
                  "rejected refits post nothing");
        }

        // Afford boundary: exactly cost is enough, cost-1 is not.
        {
            CampaignState st;
            Enlist(st, "edge");
            ApplyAftermath(st, {{"edge", 4, false}});
            Posting g;
            g.credit = {Account::Materiel, 20}; // == early heal
            g.debit = {Account::Mandate, 20};
            st.GetLedger().Post(g);
            Check(Heal(st, "edge", 0).ok() &&
                      st.GetLedger().Balance(
                          Account::Materiel) == 0,
                  "afford at exact boundary");
            ApplyAftermath(st, {{"edge", 2, false}});
            Check(!Heal(st, "edge", 0).ok(),
                  "one short of cost rejected");

            // Recruit at roster-full posts nothing.
            CampaignState full;
            Posting gf;
            gf.credit = {Account::Materiel, 9999};
            gf.debit = {Account::Mandate, 9999};
            full.GetLedger().Post(gf);
            while (full.GetRoster().size() <
                   CampaignState::MAX_ROSTER) {
                Enlist(full, "r" +
                                 std::to_string(
                                     full.GetRoster().size()));
            }
            const auto sz = full.GetLedger().Size();
            Check(!Recruit(full, "one-more", 0).ok() &&
                      full.GetLedger().Size() == sz,
                  "roster-full recruit posts nothing");
        }

        // Plunder works at deficit (negative balances are legal).
        {
            CampaignState st;
            Check(Plunder(st, 9).ok() &&
                      st.GetLedger().Balance(
                          Account::Materiel) == 60 &&
                      st.GetLedger().Balance(
                          Account::PopularSupport) == -20,
                  "plunder at zero/deficit posts");
        }

        // Refit state round-trips through the save wire.
        {
            CampaignState st;
            Enlist(st, "v");
            ApplyAftermath(st, {{"v", 8, false}});
            Posting g;
            g.credit = {Account::Materiel, 100};
            g.debit = {Account::Mandate, 100};
            st.GetLedger().Post(g);
            Heal(st, "v", 0);
            const auto back =
                CampaignState::FromJson(st.ToJson().value);
            Check(back.ok() &&
                      back.value.GetRoster()[0].casualties == 0 &&
                      back.value.GetLedger().Balance(
                          Account::Materiel) == 80,
                  "refit state round-trips");
        }
    }

    // --- Story 3.7: RivalDeck learning ---
    {
        using Potato::Gameplay::DoctrineLibrary;
        using Potato::Gameplay::TriggerKind;

        // Counter library: every counter.<trigger> resolvable
        // except counter.cohesion_below (content gap).
        const auto cdoc = JsonValue::Parse(R"json({"schema":"potato.doctrine_cards/1","cards":[
            {"id":"counter.always","name":"c","trigger":{"type":"always"},"action":{"type":"hold"}},
            {"id":"counter.enemy_in_region","name":"c","trigger":{"type":"always"},"action":{"type":"hold"}},
            {"id":"counter.enemy_adjacent","name":"c","trigger":{"type":"always"},"action":{"type":"hold"}}
        ]})json");
        const auto clib =
            DoctrineLibrary::FromJson(cdoc.value);
        Check(clib.ok() && clib.value.Count() == 3,
              "counter library loads");

        // Below three chapters the hearsay hasn't formed.
        {
            RivalBook book;
            TriggerHistogram u{};
            u[static_cast<size_t>(TriggerKind::Always)] = 9;
            book.RecordChapter("nemesis", RivalPrior::Cunning,
                               u);
            book.RecordChapter("nemesis", RivalPrior::Cunning,
                               u);
            Check(book.PrepareCounterDeck("nemesis",
                                          clib.value, 5)
                      .empty(),
                  "two chapters: no counter-deck yet");
        }

        // Three chapters: counters biased by usage order.
        {
            RivalBook book;
            TriggerHistogram ch{};
            ch[static_cast<size_t>(
                TriggerKind::EnemyInRegion)] = 5;
            ch[static_cast<size_t>(TriggerKind::Always)] = 2;
            book.RecordChapter("nemesis", RivalPrior::Cunning,
                               ch);
            book.RecordChapter("nemesis", RivalPrior::Cunning,
                               ch);
            // Third chapter shifts the habit — adjacent spikes.
            TriggerHistogram ch3{};
            ch3[static_cast<size_t>(
                TriggerKind::EnemyAdjacent)] = 7;
            book.RecordChapter("nemesis", RivalPrior::Cunning,
                               ch3);

            const auto deck = book.PrepareCounterDeck(
                "nemesis", clib.value, 5);
            // Totals: region 10 > adjacent 7 > always 4 >
            // cohesion 0 (unobserved + unresolvable anyway).
            Check(deck.size() == 3 &&
                      deck[0] == "counter.enemy_in_region" &&
                      deck[1] == "counter.enemy_adjacent" &&
                      deck[2] == "counter.always",
                  "counter-deck biased by usage");

            // Depth cap is the difficulty dial.
            Check(book.PrepareCounterDeck("nemesis",
                                          clib.value, 1)
                          .size() == 1,
                  "depth caps counter-deck");
            Check(book.PrepareCounterDeck("nemesis",
                                          clib.value, 0)
                      .empty(),
                  "depth zero yields empty deck");

            // Unknown rival -> empty.
            Check(book.PrepareCounterDeck("stranger",
                                          clib.value, 5)
                      .empty(),
                  "unknown rival yields empty deck");
        }

        // Round-trip + validation rejects.
        {
            RivalBook book;
            TriggerHistogram u{};
            u[static_cast<size_t>(TriggerKind::Always)] = 3;
            u[static_cast<size_t>(
                TriggerKind::CohesionBelow)] = 1;
            book.RecordChapter("wolf", RivalPrior::Defensive,
                               u);
            book.RecordChapter("wolf", RivalPrior::Defensive,
                               u);
            book.RecordChapter("wolf", RivalPrior::Defensive,
                               u);
            const auto doc = book.ToJson();
            const auto back =
                doc.ok()
                    ? RivalBook::FromJson(doc.value)
                    : Gameplay::Result<RivalBook>{};
            Check(back.ok() &&
                      back.value.Find("wolf") != nullptr &&
                      back.value.Find("wolf")
                              ->chaptersObserved == 3,
                  "rival book round-trips");

            // Schema + field validation. Histogram is a
            // name-keyed object — missing keys read as 0,
            // mistyped values reject, unknown keys tolerated.
            const std::string K_T =
                R"json({"always":0,"cohesion_below":0,"enemy_in_region":0,"enemy_adjacent":0})json";
            const auto parseDoc = [](const std::string& s) {
                return JsonValue::Parse(s).value;
            };
            Check(!RivalBook::FromJson(parseDoc(
                       R"({"schema":"potato.rivals/9","generals":[]})"))
                       .ok(),
                  "bad schema rejected");
            Check(!RivalBook::FromJson(parseDoc(
                       R"({"schema":"potato.rivals/1","generals":[{"id":"x","prior":"nope","observed":0,"triggers":)" +
                       K_T + "}]}"))
                       .ok(),
                  "unknown prior rejected");
            Check(!RivalBook::FromJson(parseDoc(
                       R"({"schema":"potato.rivals/1","generals":[{"id":"x","prior":"cunning","observed":0,"triggers":[0,0,0]}]})"))
                       .ok(),
                  "array histogram rejected");
            Check(!RivalBook::FromJson(parseDoc(
                       R"({"schema":"potato.rivals/1","generals":[{"id":"x","prior":"cunning","observed":-1,"triggers":)" +
                       K_T + "}]}"))
                       .ok(),
                  "negative observed rejected");
            Check(!RivalBook::FromJson(parseDoc(
                       R"({"schema":"potato.rivals/1","generals":[{"id":"x","prior":"cunning","observed":0,"triggers":)" +
                       K_T + "},{" +
                       R"("id":"x","prior":"cunning","observed":0,"triggers":)" +
                       K_T + "}]}"))
                       .ok(),
                  "duplicate rival rejected");
            Check(!RivalBook::FromJson(parseDoc(
                       R"({"schema":"potato.rivals/1","generals":[{"id":"x","prior":"cunning","observed":0,"triggers":{"always":"yes"}}]})"))
                       .ok(),
                  "mistyped histogram value rejected");
            Check(!RivalBook::FromJson(parseDoc(
                       R"({"schema":"potato.rivals/1","generals":[{"id":"x","prior":"cunning","observed":0,"triggers":{"always":-1}}]})"))
                       .ok(),
                  "negative usage in file rejected");
            // Missing keys read as zero — forward-compat.
            Check(RivalBook::FromJson(parseDoc(
                       R"({"schema":"potato.rivals/1","generals":[{"id":"x","prior":"cunning","observed":1,"triggers":{"always":5}}]})"))
                       .ok(),
                  "sparse histogram loads as zeros");
        }

        // Deterministic ties + positive-usage unresolvable skip.
        {
            RivalBook book;
            TriggerHistogram u{};
            // Equal counts on region+adjacent -> ordinal order;
            // cohesion_below observed but unresolvable in lib.
            u[static_cast<size_t>(
                TriggerKind::EnemyInRegion)] = 4;
            u[static_cast<size_t>(
                TriggerKind::EnemyAdjacent)] = 4;
            u[static_cast<size_t>(
                TriggerKind::CohesionBelow)] = 9;
            for (int i = 0; i < 3; ++i) {
                book.RecordChapter("tie", RivalPrior::Aggressive,
                                   u);
            }
            const auto deck = book.PrepareCounterDeck(
                "tie", clib.value, 9);
            // cohesion_below highest usage but unresolvable ->
            // skipped; the tied pair orders by ordinal
            // (EnemyInRegion=2 before EnemyAdjacent=3).
            Check(deck.size() == 2 &&
                      deck[0] == "counter.enemy_in_region" &&
                      deck[1] == "counter.enemy_adjacent",
                  "tie breaks by ordinal; unresolvable skipped");
        }

        // All-zero histogram at observed>=3 -> empty deck.
        {
            RivalBook book;
            TriggerHistogram z{};
            for (int i = 0; i < 3; ++i) {
                book.RecordChapter("quiet", RivalPrior::Cunning,
                                   z);
            }
            Check(book.PrepareCounterDeck("quiet", clib.value,
                                          5)
                      .empty(),
                  "observed-but-silent rival: empty deck");
        }

        // Cumulative overflow on an existing dossier rejects
        // atomically — no partial accumulation, no fake chapter.
        {
            RivalBook book;
            TriggerHistogram big{};
            big[static_cast<size_t>(TriggerKind::Always)] =
                RivalBook::MAX_COUNT;
            Check(book.RecordChapter("v", RivalPrior::Cunning,
                                     big)
                      .ok(),
                  "max-count single chapter accepted");
            const auto* dv = book.Find("v");
            Check(dv && dv->chaptersObserved == 1 &&
                      dv->triggers[0] == RivalBook::MAX_COUNT,
                  "at-bound histogram stored");
            Check(!book.RecordChapter("v",
                                      RivalPrior::Aggressive,
                                      big)
                       .ok() &&
                      book.Find("v")->chaptersObserved == 1,
                  "cumulative overflow rejects, no bump");
            // Sticky prior: first sighting wins.
            Check(book.Find("v")->prior == RivalPrior::Cunning,
                  "prior sticky on re-observe");
        }

        // MAX_RIVALS boundary: 64th ok, 65th new rejects,
        // existing rival still updates at a full book.
        {
            RivalBook book;
            TriggerHistogram u{};
            for (int i = 0; i < (int)RivalBook::MAX_RIVALS;
                 ++i) {
                book.RecordChapter("r" + std::to_string(i),
                                   RivalPrior::Aggressive, u);
            }
            Check(book.Count() == RivalBook::MAX_RIVALS &&
                      !book.RecordChapter("r_x",
                                          RivalPrior::Aggressive,
                                          u)
                           .ok(),
                  "rival book cap rejects 65th");
            Check(book.RecordChapter("r0",
                                     RivalPrior::Defensive, u)
                      .ok() &&
                      book.Find("r0")->chaptersObserved == 2,
                  "existing rival updates at full book");
        }

        // RecordChapter returns the dossier's index.
        {
            RivalBook book;
            TriggerHistogram u{};
            const auto i0 = book.RecordChapter(
                "a", RivalPrior::Aggressive, u);
            const auto i1 = book.RecordChapter(
                "b", RivalPrior::Aggressive, u);
            Check(i0.ok() && i1.ok() && i0.value == 0 &&
                      i1.value == 1 &&
                      book.Dossiers()[i0.value].id == "a",
                  "RecordChapter returns index");
        }

        // Bad RecordChapter inputs reject without mutation.
        {
            RivalBook b2;
            TriggerHistogram u{};
            TriggerHistogram bad{};
            bad[0] = -1;
            Check(!b2.RecordChapter("x", RivalPrior::Aggressive,
                                    bad)
                       .ok() &&
                      b2.Count() == 0,
                  "negative usage rejected, no dossier");
            Check(!b2.RecordChapter("", RivalPrior::Aggressive,
                                    u)
                       .ok(),
                  "empty rival id rejected");
        }
    }

    // ===== Story 4.4 — Governance Victory Path =====
    {
        namespace fs = std::filesystem;
        const fs::path vdir =
            fs::temp_directory_path() / "potato_test_victory_ch";
        fs::remove_all(vdir);
        fs::create_directories(vdir);
        const auto wch = [&vdir](const char* name, const char* id,
                                 int idx) {
            std::ofstream f(vdir / name,
                            std::ios::binary | std::ios::trunc);
            f << "{\"schema\":\"potato.chapter/1\",\"id\":\"" << id
              << "\",\"index\":" << idx
              << ",\"title\":\"t\",\"map\":\"m.json\","
                 "\"combat\":true}";
        };
        wch("a.json", "v0", 0);
        wch("b.json", "v1", 1);
        wch("c.json", "v2", 2);
        ChapterLibrary vlib;
        Check(ChapterLibrary::Load(vdir, vlib).ok,
              "4.4: library loads");

        const auto ready = [&vlib]() {
            CampaignState st;
            InitializeProgress(st, vlib);
            return st;
        };
        const auto fund = [](CampaignState& st, std::int64_t ps,
                             std::int64_t ord) {
            Posting p;
            p.credit = {Account::PopularSupport, ps};
            p.debit = {Account::Materiel, ps};
            p.memo = "rule";
            if (ord != 0) {
                p.tags = {"order:" + std::to_string(ord)};
            }
            return st.GetLedger().Post(p).ok();
        };

        using Potato::Campaign::ChapterResolution;
        using Potato::Campaign::ConcludeChapter;
        using Potato::Campaign::FoldGovernance;
        using Potato::Campaign::IsGovernanceVictory;
        using Potato::Campaign::ResolutionName;

        // Threshold predicate: both axes at bound win; one short fails.
        {
            CampaignState st = ready();
            Check(fund(st, 70, 60), "4.4: threshold funding posts");
            const auto a = FoldGovernance(st.GetLedger());
            Check(IsGovernanceVictory(a),
                  "4.4: 70/60 exactly meets thresholds");
            Check(ResolutionName(ChapterResolution::GovernanceVictory) ==
                          std::string("governance_victory") &&
                      ResolutionName(ChapterResolution::BattleVictory) ==
                          std::string("battle_victory") &&
                      ResolutionName(ChapterResolution::Defeat) ==
                          std::string("defeat"),
                  "4.4: resolution names are canonical");
        }
        // Thresholds override upward too: won field + met
        // thresholds still reads governance.
        {
            CampaignState st = ready();
            fund(st, 90, 90);
            const auto r = ConcludeChapter(st, vlib, true);
            Check(r.ok() &&
                      r.value == ChapterResolution::GovernanceVictory,
                  "4.4: won battle + thresholds = governance victory");
        }
        {
            CampaignState st = ready();
            fund(st, 69, 60);
            Check(!IsGovernanceVictory(FoldGovernance(st.GetLedger())),
                  "4.4: 民心 69 short");
            CampaignState s2 = ready();
            fund(s2, 70, 59);
            Check(!IsGovernanceVictory(FoldGovernance(s2.GetLedger())),
                  "4.4: 秩序 59 short");
        }

        // The AC's core: thresholds met at chapter end resolve as
        // governance victory — even when the field was lost.
        {
            CampaignState st = ready();
            fund(st, 75, 80);
            const auto r = ConcludeChapter(st, vlib, false);
            Check(r.ok() &&
                      r.value == ChapterResolution::GovernanceVictory,
                  "4.4: lost battle + thresholds = governance victory");
            const auto& cp = st.GetChapter();
            Check(cp.resolved[0] && cp.current == 1 && cp.unlocked[1],
                  "4.4: chapter sealed-resolved, progression advanced");
            // The verdict is IN the ledger — seal entry carries both
            // tags and the Mandate/民心 token pair.
            const auto& e = st.GetLedger().Entries().back();
            bool hasRes = false, hasCh = false;
            for (const auto& t : e.tags) {
                hasRes |= t == "resolution:governance_victory";
                hasCh |= t == "chapter:0";
            }
            Check(hasRes && hasCh,
                  "4.4: seal entry carries resolution+chapter tags");
            Check(e.credit.account == Account::Mandate &&
                      e.debit.account == Account::PopularSupport,
                  "4.4: governance seal books 民心->天命");
            Check(st.GetLedger().Verify() == nullptr,
                  "4.4: sealed chain still verifies");
            // Seal is real bookkeeping: 民心 dropped the token grain.
            Check(st.GetLedger().Balance(Account::PopularSupport) == 74,
                  "4.4: seal moves exactly one grain");
        }

        // Field won, thresholds unmet -> plain battle victory.
        {
            CampaignState st = ready();
            fund(st, 10, 10);
            const auto r = ConcludeChapter(st, vlib, true);
            Check(r.ok() &&
                      r.value == ChapterResolution::BattleVictory,
                  "4.4: won field without thresholds = battle victory");
            const auto& e = st.GetLedger().Entries().back();
            Check(e.credit.account == Account::ArmyPrestige &&
                      e.debit.account == Account::Materiel,
                  "4.4: battle seal books 物資->軍威");
        }

        // Field lost, thresholds unmet -> defeat (4.5 converts).
        {
            CampaignState st = ready();
            fund(st, 10, 10);
            const auto r = ConcludeChapter(st, vlib, false);
            Check(r.ok() && r.value == ChapterResolution::Defeat,
                  "4.4: lost field without thresholds = defeat");
            const auto& e = st.GetLedger().Entries().back();
            Check(e.credit.account == Account::Materiel &&
                      e.debit.account == Account::ArmyPrestige,
                  "4.4: defeat seal books 軍威->物資 salvage");
        }

        // Fail-closed: nothing posts when the chapter can't close.
        {
            CampaignState st; // never initialized
            fund(st, 90, 90);
            const auto n = st.GetLedger().Size();
            const auto r = ConcludeChapter(st, vlib, true);
            Check(!r.ok() && st.GetLedger().Size() == n,
                  "4.4: uninitialized state rejects, no seal posted");
        }

        // Two chapters closed then sealed-verify still holds; final
        // chapter resolves to the campaign-complete sentinel.
        {
            CampaignState st = ready();
            Check(ConcludeChapter(st, vlib, true).ok() &&
                      ConcludeChapter(st, vlib, true).ok() &&
                      ConcludeChapter(st, vlib, true).ok(),
                  "4.4: three chapters conclude");
            Check(st.GetChapter().current == 3,
                  "4.4: campaign-complete sentinel reached");
            const auto r2 = ConcludeChapter(st, vlib, true);
            Check(!r2.ok() && r2.error == "campaign",
                  "4.4: complete campaign refuses, campaign class");
            std::size_t seals = 0;
            for (const auto& e : st.GetLedger().Entries()) {
                for (const auto& t : e.tags) {
                    if (t.rfind("resolution:", 0) == 0) ++seals;
                }
            }
            Check(seals == 3, "4.4: one seal per closed chapter");
        }

        fs::remove_all(vdir);
    }

    // ===== Story 4.5 — Defeat Conversion =====
    {
        namespace fs = std::filesystem;
        const fs::path adir =
            fs::temp_directory_path() / "potato_test_aftermath_ch";
        fs::remove_all(adir);
        fs::create_directories(adir);
        const auto wch = [&adir](const char* name, const char* id,
                                 int idx) {
            std::ofstream f(adir / name,
                            std::ios::binary | std::ios::trunc);
            f << "{\"schema\":\"potato.chapter/1\",\"id\":\"" << id
              << "\",\"index\":" << idx
              << ",\"title\":\"t\",\"map\":\"m.json\","
                 "\"combat\":true}";
        };
        wch("a.json", "a0", 0);
        wch("b.json", "a1", 1);
        ChapterLibrary alib;
        Check(ChapterLibrary::Load(adir, alib).ok,
              "4.5: library loads");

        using Potato::Gameplay::SimEvent;
        using Potato::Campaign::ChapterResolution;
        using Potato::Campaign::ResolveAftermath;
        using Potato::Campaign::FoldGovernance;

        const auto ready = [&]() {
            CampaignState st;
            InitializeProgress(st, alib);
            Enlist(st, "甲隊");
            Enlist(st, "乙隊");
            return st;
        };
        const auto burn = [](int squad, int region, int side) {
            SimEvent e;
            e.kind = SimEvent::Kind::VillageBurned;
            e.squadIndex = squad;
            e.param = region;
            e.side = side;
            return e;
        };

        // The AC: a lost battle persists casualties, books the loss,
        // and the campaign CONTINUES — no game-over anywhere.
        {
            CampaignState st = ready();
            std::vector<SimEvent> deeds = {burn(0, 3, 0)};
            std::vector<AftermathRow> rows = {
                AftermathRow{"甲隊", 12, true}};
            const auto r = ResolveAftermath(st, alib, false, 0,
                                            deeds, rows, 0xA11CE5);
            Check(r.ok(), "4.5: defeat settlement succeeds");
            Check(r.value.resolution == ChapterResolution::Defeat,
                  "4.5: lost field resolves defeat");
            Check(r.value.roster.buried == 1 &&
                      r.value.roster.veterans == 0 &&
                      st.GetRoster()[0].dead &&
                      st.GetRoster()[0].casualties == 12,
                  "4.5: casualties persist, wiped stays dead");
            Check(r.value.deedsPosted == 1,
                  "4.5: player's arson deed posted");
            Check(r.value.nextChapter == 1 &&
                      st.GetChapter().resolved[0] &&
                      st.GetChapter().unlocked[1],
                  "4.5: campaign continues — chapter advanced");
            // The loss is IN the book: seal + deed + verify clean.
            const auto& entries = st.GetLedger().Entries();
            bool sealed = false, deedSeen = false;
            for (const auto& e : entries) {
                for (const auto& t : e.tags) {
                    sealed |= t == "resolution:defeat";
                    deedSeen |= t == "atrocity";
                }
            }
            Check(sealed && deedSeen &&
                      st.GetLedger().Verify() == nullptr,
                  "4.5: defeat sealed, deed booked, chain verifies");
            // 墮落 ratcheted through the loss — cruelty in defeat
            // still counts.
            Check(FoldGovernance(st.GetLedger()).corruption == 15,
                  "4.5: atrocity ratchets 墮落 through defeat");
        }

        // Win path shares the same flow (single settlement point).
        {
            CampaignState st = ready();
            std::vector<SimEvent> deeds;
            std::vector<AftermathRow> rows = {
                AftermathRow{"甲隊", 3, false}};
            const auto r = ResolveAftermath(st, alib, true, 0,
                                            deeds, rows, 0xB4771E);
            Check(r.ok() &&
                      r.value.resolution ==
                          ChapterResolution::BattleVictory &&
                      r.value.roster.veterans == 1 &&
                      r.value.deedsPosted == 0,
                  "4.5: won field settles as battle victory");
        }

        // Governance thresholds override INSIDE the settlement —
        // deeds post before the fold, so an occupying defender can
        // governance-win a lost battle.
        {
            CampaignState st = ready();
            std::vector<SimEvent> deeds;
            Posting p;
            p.credit = {Account::PopularSupport, 80};
            p.debit = {Account::Materiel, 80};
            p.memo = "rule";
            p.tags = {"order:+70"};
            Check(st.GetLedger().Post(p).ok(),
                  "4.5: threshold funding posts");
            const auto r = ResolveAftermath(st, alib, false, 0,
                                            deeds, {}, 0x60A11);
            Check(r.ok() &&
                      r.value.resolution ==
                          ChapterResolution::GovernanceVictory,
                  "4.5: lost battle + thresholds = governance win");
        }

        // Atomicity: a bad casualty report rejects the whole
        // settlement — no roster death, no deeds, no seal.
        {
            CampaignState st = ready();
            std::vector<SimEvent> deeds = {burn(0, 3, 0)};
            std::vector<AftermathRow> rows = {
                AftermathRow{"不存在的隊", 5, true}};
            const auto n = st.GetLedger().Size();
            const auto r = ResolveAftermath(st, alib, false, 0,
                                            deeds, rows, 0xBAD);
            Check(!r.ok() && st.GetLedger().Size() == n &&
                      !st.GetRoster()[0].dead &&
                      !st.GetChapter().resolved[0],
                  "4.5: bad report rejects whole settlement");
        }

        // 5.5: a settlement carrying a MythInvasion event books the
        // visitation into ledger AND MythLog in one call.
        {
            CampaignState st = ready();
            Potato::Campaign::MythLog log;
            std::vector<SimEvent> deeds;
            SimEvent inv;
            inv.kind = SimEvent::Kind::MythInvasion;
            inv.side = 1; // the god's host marched against us
            inv.param = 1;
            inv.aux = 0;
            deeds.push_back(inv);
            SimEvent act; // already logged at purchase — not refolded
            act.kind = SimEvent::Kind::MythActionInvoked;
            act.side = 0;
            act.param = 1;
            deeds.push_back(act);
            const auto r =
                ResolveAftermath(st, alib, false, 0, deeds,
                                 {AftermathRow{"乙隊", 2, false}},
                                 0x1CADE7, &log);
            Check(r.ok() && r.value.deedsPosted == 1 &&
                      r.value.mythLogged == 1 &&
                      log.Size() == 1 &&
                      log.Entries()[0].action ==
                          std::string("invasion"),
                  "5.5: settlement books visitation to ledger + log");
            const Potato::Campaign::LedgerEntry& e0 =
                st.GetLedger().Entries()[0];
            Check(e0.debit.account == Account::PopularSupport &&
                      e0.debit.amount == 4,
                  "5.5: enemy-banner visitation bills 民心");
        }

        // Preflight pins: invalid side and uninitialized progress
        // both reject BEFORE any mutation — a bad side must not
        // bury the roster (the H1 hole).
        {
            CampaignState st = ready();
            std::vector<AftermathRow> rows = {
                AftermathRow{"甲隊", 1, true}};
            const auto n = st.GetLedger().Size();
            const auto r = ResolveAftermath(st, alib, false, 7,
                                            {}, rows, 0x51DE);
            Check(!r.ok() && st.GetLedger().Size() == n &&
                      !st.GetRoster()[0].dead,
                  "4.5: invalid side rejects pre-mutation");
            CampaignState st2; // uninitialized progress
            const auto r2 = ResolveAftermath(st2, alib, true, 0,
                                             {}, {}, 0x511E);
            Check(!r2.ok() && r2.error == "state",
                  "4.5: uninitialized progress rejects");
            // Retry protection: an already-anchored record root
            // can't settle twice — and the report survives intact.
            CampaignState st3 = ready();
            Check(ResolveAftermath(st3, alib, true, 0, {}, {},
                                   0xD1CE).ok(),
                  "4.5: first settlement lands");
            const auto n3 = st3.GetLedger().Size();
            const auto retry = ResolveAftermath(st3, alib, false, 0,
                                                {}, {}, 0xD1CE);
            Check(!retry.ok() &&
                      retry.reason == "battle already settled" &&
                      st3.GetLedger().Size() == n3,
                  "4.5: same record root can't re-settle");
            CampaignState st4 = ready();
            const auto r0 = ResolveAftermath(st4, alib, true, 0,
                                             {}, {}, 0);
            Check(!r0.ok(), "4.5: zero record root rejected");
        }

        // Final-chapter defeat: settles, resolves, nextChapter is
        // the campaign-complete sentinel — continuation is vacuous,
        // not a game-over screen.
        {
            CampaignState st = ready();
            Check(ResolveAftermath(st, alib, true, 0, {}, {},
                                   0xC4A17E1).ok() &&
                      ResolveAftermath(st, alib, false, 0, {}, {},
                                       0xC4A17E2)
                          .ok(),
                  "4.5: both chapters settle");
            Check(st.GetChapter().current == 2,
                  "4.5: campaign-complete sentinel after final loss");
        }

        // Empty aftermath still settles — a bloodless defeat.
        {
            CampaignState st = ready();
            const auto r = ResolveAftermath(st, alib, false, 0,
                                            {}, {}, 0xE4911);
            Check(r.ok() && r.value.deedsPosted == 0 &&
                      r.value.resolution == ChapterResolution::Defeat,
                  "4.5: empty aftermath still seals defeat");
        }

        fs::remove_all(adir);
    }

    // --- Story 12.8: world + chosen character embed in
    //     potato.campaign/2; /1 docs reject at the gate ---
    {
        using Potato::Campaign::Character;
        using Potato::Campaign::RivalPrior;
        using Potato::Campaign::WorldMap;
        using Potato::Campaign::WorldState;
        namespace fs = std::filesystem;

        auto wdoc = JsonValue::Parse(
            "{\"schema\":\"potato.world/1\",\"id\":\"w\","
            "\"nodes\":[{\"id\":\"kaifeng\",\"control\":\"player\"},"
            "{\"id\":\"longmen\",\"control\":\"neutral\"}],"
            "\"routes\":[{\"a\":\"kaifeng\",\"b\":\"longmen\","
            "\"days\":1}],\"start\":\"kaifeng\"}");
        Check(wdoc.ok(), "12.8: world doc parses");
        auto wmap = WorldMap::FromJson(wdoc.value);
        Check(wmap.ok(), "12.8: world map loads");
        auto ws = WorldState::Init(wmap.value, 42);
        Check(ws.ok(), "12.8: world state inits");
        ws.value.MarkResolved("gate");
        ws.value.Draw(); // advance the rng stream

        CampaignState st = SampleState();
        st.SetWorld(std::move(ws.value));
        Character c;
        c.id = "lv_bu";
        c.name = "\xE5\x91\x82\xE5\xB8\x83"; // 呂布
        c.origin = "\xE4\xB9\x9D\xE5\x8E\x9F"; // 九原
        c.prior = RivalPrior::Aggressive;
        c.deckSeed = 42;
        c.created = true;
        st.SetCharacter(c);
        Check(st.HasWorld() && st.HasCharacter(),
              "12.8: bindings set");

        auto j = st.ToJson();
        Check(j.ok() && j.value.Has("world") &&
                  j.value.Has("character"),
              "12.8: /2 emits both sub-docs");
        const std::string wire = j.value.Emit();
        auto doc = JsonValue::Parse(wire);
        Check(doc.ok(), "12.8: emitted doc re-parses");
        auto st2 = CampaignState::FromJson(doc.value);
        Check(st2.ok() && st2.value.HasWorld() &&
                  st2.value.GetWorld().WorldId() == "w" &&
                  st2.value.GetWorld().WarbandAt() == "kaifeng" &&
                  st2.value.GetWorld().IsResolved("gate"),
              "12.8: world resumes bit-identically");
        Check(st2.value.HasCharacter() &&
                  st2.value.GetCharacter().id == "lv_bu" &&
                  st2.value.GetCharacter().created &&
                  st2.value.GetCharacter().deckSeed == 42,
              "12.8: created commander round-trips");
        auto j2 = st2.value.ToJson();
        Check(j2.ok() && j2.value.Emit() == wire,
              "12.8: re-emit byte-identical");

        // Optional fields: absent stays absent — a worldless
        // campaign is still a legal /2 doc.
        CampaignState bare = SampleState();
        auto bj = bare.ToJson();
        Check(bj.ok() && !bj.value.Has("world") &&
                  !bj.value.Has("character"),
              "12.8: worldless campaign emits no sub-docs");
        auto bareDoc = JsonValue::Parse(bj.value.Emit());
        auto st3 = CampaignState::FromJson(bareDoc.value);
        Check(st3.ok() && !st3.value.HasWorld() &&
                  !st3.value.HasCharacter(),
              "12.8: absent fields load unbound");

        // Present-but-invalid sub-docs reject the envelope.
        {
            auto m = j.value.Members();
            m["world"] = JsonValue::Int(7);
            Check(!CampaignState::FromJson(
                      JsonValue::MakeObject(std::move(m)))
                       .ok(),
                  "12.8: non-object world rejects");
        }
        {
            auto m = j.value.Members();
            m["character"] = JsonValue::String("x");
            Check(!CampaignState::FromJson(
                      JsonValue::MakeObject(std::move(m)))
                       .ok(),
                  "12.8: non-object character rejects");
        }
        {
            auto m = j.value.Members();
            auto w = m["world"].Members();
            w["schema"] =
                JsonValue::String("potato.worldstate/9");
            m["world"] = JsonValue::MakeObject(std::move(w));
            Check(!CampaignState::FromJson(
                      JsonValue::MakeObject(std::move(m)))
                       .ok(),
                  "12.8: wrong world sub-schema rejects");
        }
        {
            auto m = j.value.Members();
            m.erase("world");
            auto cj = m["character"].Members();
            cj.erase("id");
            m["character"] =
                JsonValue::MakeObject(std::move(cj));
            Check(!CampaignState::FromJson(
                      JsonValue::MakeObject(std::move(m)))
                       .ok(),
                  "12.8: malformed character sub-doc rejects");
        }

        // Slot-level round-trip through SaveSystem.
        const fs::path sdir =
            fs::temp_directory_path() / "potato_test_save28";
        fs::remove_all(sdir);
        fs::create_directories(sdir);
        SaveSystem saves(sdir);
        Check(saves.Save(3, st).ok(), "12.8: slot save writes");
        auto loaded = saves.Load(3);
        Check(loaded.ok() && loaded.value.HasWorld() &&
                  loaded.value.GetWorld().IsResolved("gate") &&
                  loaded.value.GetCharacter().id == "lv_bu",
              "12.8: slot load restores world+character");
        fs::remove_all(sdir);
    }

    std::printf(failures ? "CAMPAIGN TESTS FAILED: %d\n"
                         : "CAMPAIGN TESTS PASS\n",
                failures);
    return failures ? 1 : 0;
}
