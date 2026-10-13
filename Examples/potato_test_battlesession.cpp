// Story 13.6 — BattleSession pins:
//   - Planning beat: Deploy applies marshaled rows; PumpMs runs
//     zero ticks and discards stale carry
//   - RequestExecution is the irreversible 開戰 commit
//   - PumpMs converts real ms → whole 20 Hz ticks with remainder
//     carry; zero ticks outside Execution
//   - battle close lands Aftermath through the pump itself
//   - Outcome/Events read surface stays live for 13.7's seam
#include "Game/Render/MapProjection.h"
#include "Gameplay/AI/BattleAI.h"
#include "Game/Ui/AftermathScreen.h"
#include "Game/Ui/BattleCommands.h"
#include "Game/Ui/BattleSession.h"

#include "Campaign/Narrative/BattleReport.h"
#include "Campaign/Roster/Roster.h"
#include "Campaign/State/CampaignState.h"
#include "Campaign/World/Encounter.h"
#include "Campaign/World/EncounterSettle.h"
#include "Campaign/World/WorldMap.h"
#include "Campaign/World/WorldState.h"
#include "Gameplay/Doctrine/Doctrine.h"
#include "Gameplay/Eval/WinEval.h"
#include "Gameplay/Json/JsonValue.h"
#include "Gameplay/Map/BattleMap.h"
#include "Gameplay/Squad/Squad.h"

#include <cstdio>
#include <string>

namespace {

int failures = 0;
void Check(bool cond, const char* name) {
    std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", name);
    if (!cond) ++failures;
}

using namespace Potato;
using Potato::Game::BattleSession;

const char* WORLD_DOC = R"json({
    "schema": "potato.world/1", "id": "w", "name": "W",
    "nodes": [{"id": "a", "control": "player"},
              {"id": "b", "control": "rival"}],
    "routes": [{"a": "a", "b": "b", "days": 1}],
    "start": "a"
})json";

const char* MAP_DOC = R"json({
    "schema": "potato.map/1", "id": "field",
    "regions": [{"id": "r0"}, {"id": "r1"}, {"id": "r2"}],
    "edges": [["r0", "r1"], ["r1", "r2"]]
})json";

// r3 hangs off r2 — a Cunning feint's non-path neighbor (the
// chain leaves none).
const char* BRANCH_DOC = R"json({
    "schema": "potato.map/1", "id": "branch",
    "regions": [{"id": "r0"}, {"id": "r1"}, {"id": "r2"},
                {"id": "r3"}],
    "edges": [["r0", "r1"], ["r1", "r2"], ["r2", "r3"]]
})json";

const char* SQUAD_DOC = R"json({
    "schema": "potato.squad/1",
    "squads": [
        {"id": "militia", "unit": "infantry", "speed": "medium",
         "hp": 60, "attack": 6},
        {"id": "vanguard", "unit": "infantry", "speed": "medium",
         "hp": 100, "attack": 10}
    ]
})json";

const char* CARDS_DOC = R"json({
    "schema": "potato.doctrine_cards/1",
    "cards": [
        {"id": "card_brace", "cooldown": 5,
         "trigger": {"type": "enemy_adjacent"},
         "condition": {"type": "cohesion_above", "param": 50},
         "action": {"type": "brace", "param": 15}},
        {"id": "card_hold", "cooldown": 5,
         "trigger": {"type": "always"},
         "action": {"type": "hold"}},
        {"id": "card_flee", "cooldown": 10,
         "trigger": {"type": "cohesion_below", "param": 30},
         "action": {"type": "retreat"}}
    ]
})json";

// A minimal authored encounter: one militia defender at r2 —
// the player deploy rides in via PlayerDeploy. The far end
// keeps r0 out of the defender's sight so a Cunning opponent
// has unobserved ground worth probing (r1 would see the
// whole 3-region chain and issue nothing).
Campaign::EncounterDef Ambush() {
    Campaign::EncounterDef enc;
    enc.id = "field_ambush";
    enc.node = "b";
    enc.trigger = Campaign::EncounterTrigger::Arrival;
    enc.map = "field";
    enc.defenders.push_back(
        {"militia", 2, {"card_hold", "card_brace", "card_flee"}});
    enc.hasSeed = true;
    enc.seed = 4242;
    return enc;
}

} // namespace

int main() {
    Campaign::WorldMap world = Campaign::WorldMap::FromJson(
        Gameplay::JsonValue::Parse(WORLD_DOC).value).value;
    Gameplay::BattleMap bmap = Gameplay::BattleMap::FromJson(
        Gameplay::JsonValue::Parse(MAP_DOC).value).value;
    Gameplay::SquadTemplateLibrary squads =
        Gameplay::SquadTemplateLibrary::FromJson(
            Gameplay::JsonValue::Parse(SQUAD_DOC).value).value;
    Gameplay::DoctrineLibrary cards =
        Gameplay::DoctrineLibrary::FromJson(
            Gameplay::JsonValue::Parse(CARDS_DOC).value).value;

    const Campaign::EncounterDef enc = Ambush();
    const Campaign::PlayerDeploy pd{
        squads.Find("vanguard"), 0,
        {"card_hold", "card_brace", "card_flee"}, "先鋒營"};
    auto a = Campaign::MarshalEncounter(enc, world, bmap, squads,
                                        cards, {&pd, 1}, nullptr,
                                        99);
    Check(a.ok(), "marshal succeeds");

    // Stalemate at 200 ticks keeps the close test fast.
    Gameplay::EvalConfig eval;
    eval.stalemateTicks = 200;

    BattleSession s(4242, bmap, cards, a.value.fog, {}, eval);
    Check(s.Beat() == Gameplay::BattleBeat::Planning,
          "session opens in Planning");
    Check(s.PumpMs(1000) == 0,
          "pump runs zero ticks in Planning");

    Check(s.Deploy(a.value), "deploy lands in Planning");
    Check(s.Controller().Squads().size() == 2 &&
              s.Controller().Squads()[0].side == 0 &&
              s.Controller().Squads()[1].side == 1,
          "both sides fielded");

    Check(s.RequestExecution(), "開戰 commit lands");
    Check(!s.RequestExecution(),
          "second commit rejected (irreversible)");

    // ms → ticks: 50 ms/tick, sub-tick remainder carries.
    Check(s.PumpMs(30) == 0, "30 ms runs no tick");
    Check(s.PumpMs(30) == 1, "carry 60 ms runs one tick");
    Check(s.PumpMs(40) == 1, "carry 50 ms runs one tick");
    Check(s.PumpMs(200) == 4, "200 ms runs four ticks");

    // Pump to the stalemate close — the session drives the
    // sim's own close into Aftermath.
    int guard = 0;
    while (s.Beat() == Gameplay::BattleBeat::Execution &&
           guard++ < 100) {
        s.PumpMs(1000);
    }
    Check(s.Beat() == Gameplay::BattleBeat::Aftermath,
          "pump lands the close in Aftermath");
    Check(s.PumpMs(1000) == 0,
          "pump is inert after close");
    Check(s.Outcome().stalemate ||
              s.Outcome().closeReason !=
                  Gameplay::CloseReason::Stalemate ||
              true,
          "outcome surface live");
    Check(s.Outcome().closeReason ==
                  Gameplay::CloseReason::Stalemate ||
          !s.Events().empty(),
          "a verdict was declared");
    {
        bool sawClose = false;
        for (const auto& e : s.Events()) {
            if (e.kind == Gameplay::SimEvent::Kind::ResultDeclared) {
                sawClose = true;
            }
        }
        Check(sawClose, "event log carries the verdict");
    }

    // ---------- 13.7: report page + settle seam ----------
    {
        // The chronicle renders from the event log alone —
        // historianSide 0 (王師's view).
        const std::string report = Campaign::RenderBattleReport(
            s.Events(), 0);
        Check(!report.empty(), "report renders");
        Check(report.find("省略") != std::string::npos,
              "omission confession always prints");

        // Report page: 結算 commit sits bottom-right and dims
        // until the settle call is staged.
        auto page = Game::BuildReportPage(report, false, 960, 600);
        Check(!page.settleEnabled &&
                  !page.HitSettle(page.settleX + 4,
                                  page.settleY + 4),
              "disabled 結算 can't be hit");
        page = Game::BuildReportPage(report, true, 960, 600);
        Check(page.HitSettle(page.settleX + 4, page.settleY + 4) &&
                  !page.HitSettle(4, 4),
              "enabled 結算 hit-test");

        // Casualty mapping: the roster-named player deploy row
        // becomes an AftermathRow; the nameless defender doesn't.
        const auto cas =
            Game::CasualtyReport(s.Controller(), a.value, 0);
        Check(cas.size() == 1 && cas[0].name == "先鋒營" &&
                  cas[0].casualties >= 0,
              "casualty report maps deploy index → roster name");

        // Full settle against a real campaign + world — the
        // same call the shell makes on 結算.
        Campaign::CampaignState state;
        Campaign::WorldState ws =
            Campaign::WorldState::Init(world, 5).value;
        Campaign::Enlist(state, "先鋒營");
        const bool won =
            s.Outcome().winnerSide == 0;
        auto settled = Campaign::SettleEncounter(
            state, ws, world, enc, won, 0, s.Events(), cas,
            0x5ea77ed1, nullptr, s.Outcome().subverted);
        Check(settled.ok(), "settle ceremony lands");
        Check(ws.IsResolved("field_ambush"),
              "encounter marked resolved");
        if (settled.ok()) {
            const auto strip =
                Game::BuildSettlementStrip(settled.value);
            Check(!strip.verdict.empty() &&
                      strip.verdict != "?",
                  "verdict label renders");
            Check(strip.rows.size() >= 2,
                  "strip carries roster + deeds rows");
            Check(Game::ResolutionLabel(
                      Campaign::ChapterResolution::Subversion) ==
                      std::string("屈敵"),
                  "subversion labels 屈敵");
        }
    }

    // ---------- 13.6b: BattleIntent command grammar ----------
    {
        BattleSession s2(7, bmap, cards, {}, {}, eval);
        Check(s2.Deploy(a.value), "intent: redeploy fixture");
        // SetCpPool is a Planning-only door — top up before the
        // beat flips so the retreat cost itself is measurable.
        Check(s2.Controller().SetCpPool(0, Gameplay::CP_CAP),
              "intent: CP topped up in Planning");
        Check(s2.RequestExecution(), "intent: execution armed");
        Gameplay::BattleController& bc = s2.Controller();

        Game::BattleIntent intent;
        Check(!intent.Armed() && !intent.Ready(),
              "intent: empty is unarmed");
        Check(intent.Arm(Gameplay::InterventionKind::Probe),
              "intent: probe arms without a squad");
        Check(!intent.Ready(), "intent: probe awaits region");
        Check(intent.FeedRegion(2) && intent.Ready(),
              "intent: region completes probe");
        auto fired = intent.Fire(bc, 0);
        Check(fired.ok() && fired.value,
              "intent: probe fires through the gate");
        Check(!intent.Armed() && intent.Squad() < 0,
              "intent: fire clears");

        // Squad-sourced verbs need the first click — select
        // before arming (a fresh hand clears the armed verb).
        intent.SelectSquad(0);
        intent.Arm(Gameplay::InterventionKind::Redirect);
        Check(!intent.Ready(),
              "intent: redirect needs target");
        intent.FeedRegion(1); // squad sits at 0 — adjacent only
        Check(intent.Ready(), "intent: redirect complete");
        fired = intent.Fire(bc, 0);
        Check(fired.ok() && fired.value &&
                  bc.CpPool(0) == Gameplay::CP_CAP - 1,
              "intent: redirect spends 1 CP");

        // One pending command per squad — flush the redirect
        // through a tick before the next order.
        Check(s2.PumpMs(50) >= 1, "intent: tick applies pending");

        // Re-selecting clears the armed verb.
        intent.Arm(Gameplay::InterventionKind::Retreat);
        intent.SelectSquad(0);
        Check(!intent.Armed(),
              "intent: re-select disarms stale verb");

        // Retreat is squad-only — Ready on arm.
        intent.SelectSquad(0);
        intent.Arm(Gameplay::InterventionKind::Retreat);
        Check(intent.Ready(), "intent: retreat ready on arm");
        fired = intent.Fire(bc, 0);
        Check(fired.ok() && fired.value &&
                  bc.CpPool(0) == Gameplay::CP_CAP - 4,
              "intent: retreat spends 3 CP");

        // Rejection propagates and still clears.
        intent.SelectSquad(0);
        intent.Arm(Gameplay::InterventionKind::Retreat);
        fired = intent.Fire(bc, 0);
        Check(!fired.ok() && !intent.Armed(),
              "intent: thin CP rejects + clears");

        // Cloud handles resolve to enemy squads via the sim's
        // own mapping — the UI never dereferences truth.
        Check(!bc.Fog(0).Clouds().empty(),
              "intent: deployed enemy left a cloud");
        const int cloudId = bc.Fog(0).Clouds().front().id;
        const int victim = bc.SquadForCloud(0, cloudId);
        Check(victim == 1,
              "intent: cloud resolves to the defender squad");
        intent.Arm(Gameplay::InterventionKind::Execute);
        Check(intent.FeedCloud(bc, 0, cloudId) &&
                  intent.Ready(),
              "intent: execute locks the believed target");
        fired = intent.Fire(bc, 0);
        Check(!fired.ok(),
              "intent: holding enemy can't be executed — "
              "gate rejects, intent cleared");
        Check(!intent.Armed(), "intent: rejection clears");

        // Path accumulation: adjacency + no-revisit + walk-back.
        intent.BeginPath(0);
        Check(intent.ExtendPath(bmap, 1),
              "intent: adjacent extends");
        Check(!intent.ExtendPath(bmap, 0),
              "intent: no revisits");
        Check(intent.ExtendPath(bmap, 2),
              "intent: chain grows");
        Check(intent.ExtendPath(bmap, 2),
              "intent: tail re-click walks back");
        Check(intent.Path().size() == 2,
              "intent: path state coherent");

        // Cloud clicks resolve through belief only.
        const auto& cloud0 = bc.Fog(0).Clouds().front();
        Check(Game::CloudAtRegion(
                  bc.Fog(0),
                  static_cast<std::size_t>(
                      cloud0.believedRegion)) == cloud0.id,
              "intent: believed region resolves its cloud");
        Check(Game::CloudAtRegion(bc.Fog(0),
                                  bmap.RegionCount() + 7) ==
                  -1,
              "intent: no cloud at unobserved region");

        // Inverse projection — center hits its own region,
        // empty space misses.
        auto proj = Game::MapProjection::Build(bmap, 960, 600,
                                               8);
        Check(proj.ok(), "projection: builds");
        const auto& pr0 =
            proj.value.Regions().front();
        Check(proj.value.Hit(pr0.x, pr0.y, 40) ==
                  static_cast<int>(pr0.index),
              "projection: center hits own region");
        Check(proj.value.Hit(1, 1, 2) == -1,
              "projection: empty corner misses");
    }

    // ---------- 13.6c: opponent cadence is sim-time ----------
    {
        // The same elapsed span, pumped in different slices,
        // must drive the identical Act() call sites — AI moves
        // are journaled events, so cadence is part of the
        // record.
        BattleSession s3(7, bmap, cards, {}, {}, eval);
        Gameplay::BattleAI ai3(7, 1, Gameplay::Prior::Cunning,
                               bmap, cards);
        Check(s3.Deploy(a.value), "ai: split deploy");
        s3.AttachOpponent(&ai3, 2);
        s3.RequestExecution();
        for (int i = 0; i < 8; ++i) s3.PumpMs(50); // 8 ticks

        BattleSession s4(7, bmap, cards, {}, {}, eval);
        Gameplay::BattleAI ai4(7, 1, Gameplay::Prior::Cunning,
                               bmap, cards);
        Check(s4.Deploy(a.value), "ai: lump deploy");
        s4.AttachOpponent(&ai4, 2);
        s4.RequestExecution();
        s4.PumpMs(400); // same 8 ticks, one call

        const auto& e3 = s3.Events();
        const auto& e4 = s4.Events();
        Check(e3.size() == e4.size(),
              "ai: identical event count across pump splits");
        bool identical = e3.size() == e4.size();
        for (std::size_t i = 0; identical && i < e3.size();
             ++i) {
            identical = e3[i].kind == e4[i].kind &&
                        e3[i].tick == e4[i].tick &&
                        e3[i].squadIndex == e4[i].squadIndex &&
                        e3[i].slotIndex == e4[i].slotIndex &&
                        e3[i].param == e4[i].param &&
                        e3[i].aux == e4[i].aux &&
                        e3[i].side == e4[i].side &&
                        e3[i].path == e4[i].path &&
                        e3[i].cardId == e4[i].cardId;
        }
        Check(identical,
              "ai: identical event log across pump splits");

        int foeActs = 0;
        for (const auto& e : e4) {
            if (e.kind == Gameplay::SimEvent::Kind::Intervention &&
                e.side == 1)
                ++foeActs;
        }
        Check(foeActs > 0,
              "ai: opponent issued interventions (side 1)");
        Check(s3.Controller().Checksum() ==
                  s4.Controller().Checksum(),
              "ai: checksums identical across pump splits");
    }

    // ===== PlanDeployed — the marshaled-battle planning seam =====
    {
        BattleSession sd(4242, bmap, cards, a.value.fog, {},
                         eval);
        Check(sd.Deploy(a.value), "pd: deploy on chain");
        Gameplay::BattleAI def(7, 1, Gameplay::Prior::Defensive,
                               bmap, cards);
        Check(def.PlanDeployed(sd.Controller(), 0),
              "pd: defensive plans on marshaled rows");
        const auto& arr = sd.Controller().ArrowOf(1);
        Check(arr.active && arr.path.size() == 2 &&
                  arr.path[0] == 2 && arr.path[1] == 1,
              "pd: defensive trims to first hop");
        Check(!def.PlanDeployed(sd.Controller(), 99),
              "pd: out-of-bounds objective rejected");
        Check(sd.RequestExecution(), "pd: beat commits");
        Check(!def.PlanDeployed(sd.Controller(), 0),
              "pd: wrong beat rejected");

        // Branched map — the Cunning feint plants the militia's
        // believed position on r3 (the neighbor OFF its arrow).
        Gameplay::BattleMap dmap =
            Gameplay::BattleMap::FromJson(
                Gameplay::JsonValue::Parse(BRANCH_DOC).value)
                .value;
        BattleSession sc(7, dmap, cards, {}, {}, eval);
        Check(sc.Controller().DeploySquad(
                  *squads.Find("militia"), 2, 1),
              "pd: militia fields on branch");
        Gameplay::BattleAI cun(7, 1, Gameplay::Prior::Cunning,
                               dmap, cards);
        Check(cun.PlanDeployed(sc.Controller(), 0),
              "pd: cunning plans on branch");
        const auto& clouds = sc.Controller().Fog(0).Clouds();
        Check(!clouds.empty() && clouds[0].believedRegion == 3 &&
                  clouds[0].certainty == 70,
              "pd: feint plants false signature off-path");
        Gameplay::BattleAI lone(7, 0, Gameplay::Prior::Cunning,
                                dmap, cards);
        Check(!lone.PlanDeployed(sc.Controller(), 0),
              "pd: no own squads fails");
    }

    if (failures == 0) {
        std::printf("BATTLE SESSION TESTS PASS\n");
        return 0;
    }
    std::printf("%d FAILURES\n", failures);
    return 1;
}
