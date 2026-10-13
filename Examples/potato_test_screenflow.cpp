// Story 13.2 — ScreenFlow: the top-level navigation state machine.
// Table-gated narrative moves, the always-legal abandon door, the
// pause overlay flag, and the save gating that keeps I/O off the
// tick path.
#include "Game/Ui/EndingScreen.h"
#include "Game/Ui/ScreenFlow.h"
#include "Game/Ui/PauseOverlay.h"
#include "Game/Ui/SettingsForm.h"
#include "Game/Ui/TitleScreen.h"
#include "Campaign/Ledger/Ledger.h"

#include <cstdio>
#include <filesystem>

namespace {

int failures = 0;
void Check(bool cond, const char* name) {
    std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", name);
    if (!cond) ++failures;
}

using Potato::Game::ScreenFlow;
using Potato::Game::ScreenId;
using Potato::Gameplay::BattleBeat;

constexpr ScreenId kAll[] = {
    ScreenId::Title,     ScreenId::Creation, ScreenId::Overworld,
    ScreenId::Battle,    ScreenId::Settlement,
    ScreenId::Ending,
};

} // namespace

int main() {
    // ===== The table IS the spec — every (from,to) pair pinned =====
    {
        // Expected targets per screen (narrative moves only —
        // AbandonToTitle is the door, not a path).
        const auto legal = [](ScreenId a, ScreenId b) {
            switch (a) {
                case ScreenId::Title:
                    return b == ScreenId::Creation ||
                           b == ScreenId::Overworld;
                case ScreenId::Creation:
                    return b == ScreenId::Overworld ||
                           b == ScreenId::Title;
                case ScreenId::Overworld:
                    return b == ScreenId::Battle ||
                           b == ScreenId::Ending;
                case ScreenId::Battle:
                    return b == ScreenId::Settlement;
                case ScreenId::Settlement:
                    return b == ScreenId::Overworld ||
                           b == ScreenId::Ending;
                case ScreenId::Ending:
                    return b == ScreenId::Title;
            }
            return false;
        };
        bool allOk = true;
        for (ScreenId a : kAll) {
            for (ScreenId b : kAll) {
                if (Potato::Game::CanTransition(a, b) != legal(a, b)) {
                    allOk = false;
                }
            }
        }
        Check(allOk, "transition table matches the flow diagram");
    }

    // ===== Request: legal moves land, illegal moves don't touch =====
    {
        ScreenFlow f;
        Check(f.Screen() == ScreenId::Title, "boots on title");
        Check(!f.Request(ScreenId::Battle),
              "title can't skip to battle");
        Check(f.Screen() == ScreenId::Title,
              "illegal move left the flow untouched");
        Check(f.Request(ScreenId::Creation) &&
                  f.Screen() == ScreenId::Creation,
              "title -> creation");
        Check(f.Request(ScreenId::Overworld) &&
                  f.Screen() == ScreenId::Overworld,
              "creation -> overworld");
        Check(f.Request(ScreenId::Battle) &&
                  f.Screen() == ScreenId::Battle,
              "overworld -> battle");
        Check(f.Request(ScreenId::Settlement) &&
                  f.Screen() == ScreenId::Settlement,
              "battle -> settlement");
        Check(f.Request(ScreenId::Ending) &&
                  f.Screen() == ScreenId::Ending,
              "settlement -> ending (final chapter)");
        Check(f.Request(ScreenId::Title) &&
                  f.Screen() == ScreenId::Title,
              "ending -> title");
        // The other spine: settle back to the map.
        f.Request(ScreenId::Overworld);
        f.Request(ScreenId::Battle);
        f.Request(ScreenId::Settlement);
        Check(f.Request(ScreenId::Overworld),
              "settlement -> overworld (continuation)");
        Check(!f.Request(ScreenId::Ending) == false,
              "overworld -> ending IS legal (complete-on-load)");
    }

    // ===== AbandonToTitle: the door is always open =====
    {
        for (ScreenId s : kAll) {
            if (s == ScreenId::Title) continue;
            ScreenFlow f;
            // Walk to s via legal moves where possible; the rest
            // get there through the spine.
            f.Request(ScreenId::Overworld);
            f.Request(ScreenId::Battle);
            f.Request(ScreenId::Settlement);
            if (s == ScreenId::Ending) f.Request(ScreenId::Ending);
            if (f.Screen() != s) continue;
            f.AbandonToTitle();
            Check(f.Screen() == ScreenId::Title && !f.Paused(),
                  "abandon returns to title from anywhere");
        }
        ScreenFlow f;
        f.SetPaused(true);
        f.AbandonToTitle();
        Check(!f.Paused(), "abandon clears the pause overlay");
    }

    // ===== Pause overlay: flag, not a screen =====
    {
        ScreenFlow f;
        f.Request(ScreenId::Overworld);
        f.SetPaused(true);
        Check(f.Paused() && f.Screen() == ScreenId::Overworld,
              "pause is an overlay — position unchanged");
        f.SetPaused(false);
        f.Request(ScreenId::Battle);
        Check(!f.Paused(), "screen change clears pause");
        f.SetPaused(true);
        Check(f.Request(ScreenId::Settlement),
              "pause doesn't block narrative moves");
        Check(!f.Paused(), "the move resumed presentation");
    }

    // ===== NoteBattleBeat: mirrored only while on S3 =====
    {
        ScreenFlow f;
        f.NoteBattleBeat(BattleBeat::Execution);
        Check(f.BattleBeatIn() == BattleBeat::Planning,
              "beat note ignored off the battle screen");
        f.Request(ScreenId::Overworld);
        f.Request(ScreenId::Battle);
        f.NoteBattleBeat(BattleBeat::Execution);
        Check(f.BattleBeatIn() == BattleBeat::Execution,
              "beat mirrors while on S3");
        f.Request(ScreenId::Settlement);
        Check(f.BattleBeatIn() == BattleBeat::Planning,
              "leaving battle resets the mirrored beat");
    }

    // ===== CanSave: no I/O on the tick path =====
    {
        ScreenFlow f;
        Check(!f.CanSave(), "title can't save");
        f.Request(ScreenId::Creation);
        Check(!f.CanSave(), "creation can't save");
        f.Request(ScreenId::Overworld);
        Check(f.CanSave(), "overworld saves");
        f.SetPaused(true);
        Check(f.CanSave(), "pause doesn't block saving");
        f.SetPaused(false);
        f.Request(ScreenId::Battle);
        Check(f.CanSave(), "battle planning saves");
        f.NoteBattleBeat(BattleBeat::Execution);
        Check(!f.CanSave(), "execution refuses — ticks are live");
        f.NoteBattleBeat(BattleBeat::Aftermath);
        Check(!f.CanSave(), "aftermath refuses");
        f.Request(ScreenId::Settlement);
        Check(!f.CanSave(), "settlement can't save mid-ceremony");
        f.Request(ScreenId::Ending);
        Check(!f.CanSave(), "ending can't save");
    }

    // ===== 13.8: pause overlay widget model =====
    {
        // Save gating flows straight from ScreenFlow::CanSave —
        // the overlay never re-derives the rule.
        ScreenFlow f;
        f.Request(ScreenId::Overworld);

        Potato::Game::TitleSlotInfo slots[2];
        slots[0].slot = 0;
        slots[0].label = "甲帥 · Day 3";
        slots[0].occupied = true;
        slots[1].slot = 3;
        slots[1].label = "乙帥 · Day 9";
        slots[1].occupied = true;

        Potato::Game::PauseInput in;
        in.canSave = f.CanSave();
        in.slots = slots;
        auto lay = Potato::Game::BuildPauseLayout(in);
        Check(lay.widgets.size() == 5 + 2,
              "overlay: actions + one row per present slot");
        const Potato::Game::PauseWidget* save = nullptr;
        int loads = 0;
        for (const auto& w : lay.widgets) {
            if (w.action == Potato::Game::PauseAction::Save) {
                save = &w;
            }
            if (w.action == Potato::Game::PauseAction::LoadSlot) {
                ++loads;
            }
        }
        Check(save != nullptr && save->enabled,
              "overlay: save lit on the overworld");
        Check(loads == 2, "overlay: two load-slot rows");

        // Hit-test: a load row carries its slot in aux.
        const auto* hit =
            lay.Hit(lay.widgets[2].x + 4, lay.widgets[2].y + 4);
        Check(hit != nullptr &&
                  hit->action ==
                      Potato::Game::PauseAction::LoadSlot &&
                  hit->aux == 0,
              "overlay: slot row hit carries slot index");
        Check(lay.Hit(-1, -1) == nullptr,
              "overlay: outside hits nothing");

        // Off the save-legal screens the same layout dims.
        f.Request(ScreenId::Battle);
        f.NoteBattleBeat(BattleBeat::Execution);
        in.canSave = f.CanSave();
        lay = Potato::Game::BuildPauseLayout(in);
        save = nullptr;
        for (const auto& w : lay.widgets) {
            if (w.action == Potato::Game::PauseAction::Save) {
                save = &w;
            }
        }
        Check(save != nullptr && !save->enabled,
              "overlay: execution dims save — dimmed, not hidden");
        in.canSave = false;
        lay = Potato::Game::BuildPauseLayout(in);
        Check(lay.widgets[1].action ==
                      Potato::Game::PauseAction::Save &&
                  !lay.widgets[1].enabled,
              "overlay: title-side save dims");
    }

    // ===== 13.9: ending folio view-model =====
    {
        // Clean book → 信史; the folio is a pure fold of the
        // ledger — tampered books earn their own voice.
        Potato::Campaign::Ledger ledger;
        Potato::Campaign::Posting p;
        p.credit = {Potato::Campaign::Account::Materiel, 10};
        p.debit = {Potato::Campaign::Account::ArmyPrestige, 10};
        p.memo = "entry";
        Check(ledger.Post(p).ok(), "folio: honest post");
        auto page = Potato::Game::BuildEndingPage(ledger, 960, 600);
        Check(page.voice == Potato::Campaign::EndingVoice::Believed &&
                  page.title == "信史" && !page.text.empty(),
              "folio: clean book reads 信史");
        Check(page.HitReturn(page.returnX + 4, page.returnY + 4) &&
                  !page.HitReturn(0, 0),
              "folio: return-to-title hit-test");

        // Doubt — a suspect flag drops the voice to 疑史.
        Check(ledger.SetSuspect(0), "folio: suspect flag");
        page = Potato::Game::BuildEndingPage(ledger, 960, 600);
        Check(page.voice == Potato::Campaign::EndingVoice::Doubted &&
                  page.title == "疑史",
              "folio: suspect book reads 疑史");

        // Forgery outranks doubt — the harsher verdict wins.
        Check(ledger.Forge(p).ok(), "folio: forged post");
        page = Potato::Game::BuildEndingPage(ledger, 960, 600);
        Check(page.voice == Potato::Campaign::EndingVoice::Forged &&
                  page.title == "偽史",
              "folio: forged book reads 偽史");
        Check(std::string(Potato::Game::EndingVoiceTitle(
                  Potato::Campaign::EndingVoice::Abandoned)) ==
                  "絕筆",
              "folio: abandoned title");

        // The folio hands back to S0 — Ending→Title is in the
        // table (already pinned); the page's HitReturn is the
        // widget that issues it.
    }

    // ===== settings: potato.settings/1 =====
    {
        namespace fs = std::filesystem;
        using Potato::Game::GameSettings;
        using Potato::Game::HudDensity;

        GameSettings s;
        Check(s.volume == 80 && s.resolutionIndex == 0 &&
                  s.density == HudDensity::Standard,
              "set: defaults");

        // Cycle/step math — wrap cycles, clamped steps.
        Potato::Game::StepVolume(s, -40);
        Check(s.volume == 0, "set: volume clamps low");
        Potato::Game::StepVolume(s, 30);
        Check(s.volume == 100, "set: volume clamps high");
        Potato::Game::CycleResolution(s, -1);
        Check(s.resolutionIndex ==
                  Potato::Game::kResolutionCount - 1,
              "set: resolution wraps down");
        Potato::Game::CycleDensity(s, 1);
        Check(s.density == HudDensity::Roomy,
              "set: density cycles up");
        Check(Potato::Game::DensityScale(HudDensity::Roomy) >
                  Potato::Game::DensityScale(
                      HudDensity::Standard) &&
                  Potato::Game::DensityScale(
                      HudDensity::Compact) <
                      Potato::Game::DensityScale(
                          HudDensity::Standard),
              "set: density scale ordered");

        // Round-trip through the wire.
        s.volume = 42;
        s.resolutionIndex = 2;
        s.density = HudDensity::Compact;
        auto j = s.ToJson();
        Check(j.ok(), "set: emits");
        auto back = GameSettings::FromJson(j.value);
        Check(back.ok() && back.value.volume == 42 &&
                  back.value.resolutionIndex == 2 &&
                  back.value.density == HudDensity::Compact,
              "set: round-trips");

        // Rejections — bad content mutates nothing.
        Check(!GameSettings::FromJson(
                  Potato::Gameplay::JsonValue::Parse(
                      R"({"schema":"potato.settings/1","volume":101})")
                      .value)
                   .ok(),
              "set: out-of-range volume rejects");
        Check(!GameSettings::FromJson(
                  Potato::Gameplay::JsonValue::Parse(
                      R"({"schema":"potato.settings/1","resolution":"800x600"})")
                      .value)
                   .ok(),
              "set: unknown resolution rejects");
        Check(!GameSettings::FromJson(
                  Potato::Gameplay::JsonValue::Parse(
                      R"({"schema":"potato.settings/2"})")
                      .value)
                   .ok(),
              "set: wrong schema rejects");
        // Absent members keep defaults — forward-compat.
        auto partial = GameSettings::FromJson(
            Potato::Gameplay::JsonValue::Parse(
                R"({"schema":"potato.settings/1","volume":33})")
                .value);
        Check(partial.ok() && partial.value.volume == 33 &&
                  partial.value.resolutionIndex == 0,
              "set: absent members default");

        // File round-trip — missing file defaults; save+load
        // preserves; tmp file must not linger.
        const fs::path p =
            fs::temp_directory_path() / "potato_test_set.json";
        fs::remove(p);
        auto miss = GameSettings::Load(p);
        Check(miss.ok() && miss.value.volume == 80,
              "set: missing file yields defaults");
        Check(s.Save(p).ok(), "set: save writes");
        auto ld = GameSettings::Load(p);
        Check(ld.ok() && ld.value.volume == 42 &&
                  ld.value.density == HudDensity::Compact,
              "set: load restores");
        Check(!fs::exists(p.string() + ".tmp"),
              "set: tmp renamed away");
        fs::remove(p);
    }

    std::printf("%s\n", failures == 0 ? "ALL PASS" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
