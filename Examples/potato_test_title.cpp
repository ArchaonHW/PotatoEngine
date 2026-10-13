// Story 13.3 — S0 title screen: the pure BuildTitleLayout widget
// model (menu enables, slot rows, hit rects) and the TitleSlots
// save-scan seam (label reads, corrupt-save honesty).
#include "Game/Ui/TitleScreen.h"

#include "Campaign/Characters/Character.h"
#include "Campaign/Save/SaveSystem.h"
#include "Campaign/State/CampaignState.h"
#include "Campaign/World/WorldMap.h"
#include "Campaign/World/WorldState.h"
#include "Gameplay/Json/JsonValue.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

int failures = 0;
void Check(bool cond, const char* name) {
    std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", name);
    if (!cond) ++failures;
}

using Potato::Campaign::CampaignState;
using Potato::Campaign::Character;
using Potato::Campaign::SaveSystem;
using Potato::Campaign::WorldMap;
using Potato::Campaign::WorldState;
using Potato::Game::BuildTitleLayout;
using Potato::Game::TitleAction;
using Potato::Game::TitleCell;
using Potato::Game::TitleInput;
using Potato::Game::TitleSlotInfo;
using Potato::Game::TitleSlots;
using Potato::Game::TitleWidget;
using Potato::Gameplay::JsonValue;

const char* WORLD_DOC = R"json({
    "schema": "potato.world/1",
    "id": "w",
    "nodes": [{"id": "home", "control": "player"}],
    "routes": [],
    "start": "home"
})json";

int CountKind(const Potato::Game::TitleLayout& l, TitleCell k) {
    int n = 0;
    for (const TitleWidget& w : l.widgets) {
        if (w.kind == k) ++n;
    }
    return n;
}

const TitleWidget* FindAction(const Potato::Game::TitleLayout& l,
                              TitleAction a) {
    for (const TitleWidget& w : l.widgets) {
        if (w.kind == TitleCell::MenuItem &&
            w.aux == static_cast<int>(a)) {
            return &w;
        }
    }
    return nullptr;
}

} // namespace

int main() {
    // ===== Layout skeleton: masthead + four items, slots nested =====
    {
        TitleInput in;
        const auto l = BuildTitleLayout(in);
        Check(CountKind(l, TitleCell::Masthead) == 1 &&
                  CountKind(l, TitleCell::MenuItem) == 4,
              "masthead + four menu items");
        Check(FindAction(l, TitleAction::Continue) != nullptr &&
                  FindAction(l, TitleAction::NewGame) != nullptr &&
                  FindAction(l, TitleAction::Settings) != nullptr &&
                  FindAction(l, TitleAction::Quit) != nullptr,
              "all four actions present");
        Check(!FindAction(l, TitleAction::Continue)->enabled,
              "continue dims with no slot selected");
        Check(FindAction(l, TitleAction::NewGame)->enabled &&
                  FindAction(l, TitleAction::Quit)->enabled,
              "new/quit always enabled");
    }

    // ===== Slots: rows per present slot, continue gates selection =====
    {
        const TitleSlotInfo slots[] = {
            {0, "甲帥 · Day 12", true},
            {3, "乙帥 · Day 4", true},
        };
        TitleInput in;
        in.slots = slots;
        auto l = BuildTitleLayout(in);
        Check(CountKind(l, TitleCell::SlotRow) == 2,
              "one row per present slot");
        Check(!FindAction(l, TitleAction::Continue)->enabled,
              "nothing selected — continue still dims");
        in.selectedSlot = 3;
        l = BuildTitleLayout(in);
        Check(FindAction(l, TitleAction::Continue)->enabled,
              "selected occupied slot enables continue");
        in.selectedSlot = 2; // not a present slot
        l = BuildTitleLayout(in);
        Check(!FindAction(l, TitleAction::Continue)->enabled,
              "selecting an absent slot dims continue");
        // Slot rows carry their slot index for the caller's pick.
        bool row0 = false, row3 = false;
        for (const TitleWidget& w : l.widgets) {
            if (w.kind == TitleCell::SlotRow && w.aux == 0) row0 = true;
            if (w.kind == TitleCell::SlotRow && w.aux == 3) row3 = true;
        }
        Check(row0 && row3, "rows carry their slot index in aux");
    }

    // ===== Hit test: rects are real, last widget wins =====
    {
        const TitleSlotInfo slots[] = {{0, "s", true}};
        TitleInput in;
        in.slots = slots;
        in.selectedSlot = 0;
        const auto l = BuildTitleLayout(in);
        const TitleWidget* quit = FindAction(l, TitleAction::Quit);
        Check(l.Hit(quit->x + 1, quit->y + 1) == quit,
              "hit inside quit rect finds quit");
        Check(l.Hit(-1, -1) == nullptr && l.Hit(in.w + 1, 0) == nullptr,
              "outside every rect hits nothing");
    }

    // ===== TitleSlots: the save-scan seam =====
    {
        namespace fs = std::filesystem;
        const fs::path dir =
            fs::temp_directory_path() / "potato_test_title_saves";
        fs::remove_all(dir);
        fs::create_directories(dir);
        SaveSystem saves(dir);

        const WorldMap world =
            WorldMap::FromJson(JsonValue::Parse(WORLD_DOC).value)
                .value;
        {
            CampaignState st;
            Character c;
            c.id = "cmdr_a";
            c.name = "甲帥";
            c.origin = "皖"; // required non-empty (wire rule)
            st.SetCharacter(c);
            auto ws = WorldState::Init(world, 7).value;
            ws.ResolveBeats(world, 12); // day 12
            st.SetWorld(std::move(ws));
            Check(saves.Save(0, st).ok(), "slot 0 saves");
        }
        {
            CampaignState st; // no commander, no world
            Check(saves.Save(3, st).ok(), "slot 3 saves");
        }
        // A corrupt slot: a .json the loader will refuse.
        {
            std::ofstream f(SaveSystem::SlotPath(dir, 5),
                            std::ios::binary | std::ios::trunc);
            f << "not json at all {{{";
        }

        const auto slots = TitleSlots(saves);
        Check(slots.size() == 3 &&
                  slots[0].slot == 0 && slots[1].slot == 3 &&
                  slots[2].slot == 5,
              "scan lists every present slot in order");
        Check(slots[0].occupied && slots[0].label == "甲帥 · Day 12",
              "labeled by commander + world day");
        Check(slots[1].occupied && slots[1].label == "—",
              "bare state labels as em-dash");
        Check(slots[2].occupied &&
                  slots[2].label.find("檔案損毀") != std::string::npos,
              "corrupt slot keeps its row, labeled honestly");

        fs::remove_all(dir);
    }

    std::printf("%s\n", failures == 0 ? "ALL PASS" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
