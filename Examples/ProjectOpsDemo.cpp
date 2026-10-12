// ProjectOpsDemo — 把《企業全維度整體運營》的系統動力學模型
// 套用在 PotatoEngine 專案本身：用 SDModel 模擬引擎開發的
// 存量-流量動態。
//
// 對照原文檔的映射：
//   頂層 治理/法稅 → 專案方向決策、發布閘門（不在模擬範圍）
//   中層 資金/資源/組織/風控/里程 → 功能點、技術債、測試覆蓋率、
//      知識資產、版本里程碑
//   底層 運營/供應鏈/客戶 → 功能交付、程式碼產線、下游遊戲需求
//   回饋環：R「功能越多→使用越多→動能越強」增長環；
//           B「債務拖累→產能下降」「測試缺口→bug 返工」平衡環
//   離散事件：需求變更、引擎 bug 爆擊（機率性）
//   KPI 觸發：技術債指數、覆蓋率門檻、ship readiness 保守模式

#include "Simulation/SystemDynamics.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace Potato::Simulation;

namespace {

const char* Bar(float v, float max, int width = 24) {
    static char buf[64];
    const int n = static_cast<int>(v / max * width + 0.5f);
    int i = 0;
    for (; i < n && i < width; ++i) buf[i] = '#';
    buf[i] = '\0';
    return buf;
}

} // namespace

int main(int argc, char** argv) {
    int months = 12;
    unsigned seed = 42;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--months")
            months = std::atoi(argv[++i]);
        else if (std::string(argv[i]) == "--seed")
            seed = static_cast<unsigned>(std::atoi(argv[++i]));
    }

    std::printf("=== PotatoEngine 專案運營系統動力學模擬 ===\n");
    std::printf("期程 %d 個月 / seed %u\n\n", months, seed);

    SDModel m;
    m.SetSeed(seed);

    // ---------- 存量 ----------
    // features：已交付功能點（0..100 為當期範疇的 100%）
    m.AddStock("features", 30.f, 0.f, 100.f);
    // tech_debt：累積債務點（0 起，越高產能越差）
    m.AddStock("tech_debt", 20.f, 0.f, 200.f);
    // coverage：測試覆蓋率 0..1（頭部綠燈率代理）
    m.AddStock("coverage", 0.60f, 0.f, 1.f);
    // knowledge：知識資產（文件/慣例沉澱），0..1
    m.AddStock("knowledge", 0.4f, 0.f, 1.f);

    // ---------- 參數 ----------
    m.SetParam("team_capacity", 10.f);     // 每月總產能點
    m.SetParam("debt_drag", 0.6f);         // 債務對產能的侵蝕係數
    m.SetParam("feature_cost", 1.0f);      // 每功能點成本
    m.SetParam("test_ratio", 0.3f);        // 產能投入測試比例
    m.SetParam("debt_paydown", 0.15f);     // 產能投入償債比例
    m.SetParam("bug_rate", 0.08f);         // 每功能點月產生 bug 債
    m.SetParam("scope_total", 100.f);      // 範疇總功能點

    // ---------- 輔助變數 ----------
    // 可用產能：債務侵蝕（B1 管理稀釋環的專案版）
    m.AddAux("effective_capacity", [](const SDState& s) {
        return s.P("team_capacity") /
               (1.f + s.P("debt_drag") * s.S("tech_debt") / 50.f);
    });
    // 審成品質因子：覆蓋率+知識資產（R2 知識複利環）
    m.AddAux("quality_factor", [](const SDState& s) {
        return 0.5f + 0.3f * s.S("coverage") + 0.2f * s.S("knowledge");
    });
    // 交付速率（剩餘工作量也限上限）
    m.AddAux("dev_rate", [](const SDState& s) {
        const float cap = s.A("effective_capacity") *
                          (1.f - s.P("test_ratio") - s.P("debt_paydown"));
        const float remaining = s.P("scope_total") - s.S("features");
        return std::min(cap * s.A("quality_factor") / s.P("feature_cost"),
                        remaining);
    });

    // ---------- 流量 ----------
    m.AddFlow("features", [](const SDState& s) { return s.A("dev_rate"); });
    // 債務：新功能產生 bug 債 − 償債投入（B 環：債高→償債慢）
    m.AddFlow("tech_debt", [](const SDState& s) {
        const float accrual = s.A("dev_rate") * s.P("bug_rate");
        const float paydown = s.A("effective_capacity") *
                              s.P("debt_paydown") *
                              (s.S("tech_debt") > 0.f ? 1.f : 0.f);
        return accrual - paydown;
    });
    // 覆蓋率：測試投入拉升 − 新功能稀釋
    m.AddFlow("coverage", [](const SDState& s) {
        const float add =
            s.A("effective_capacity") * s.P("test_ratio") * 0.01f;
        const float dilution =
            s.A("dev_rate") / s.P("scope_total") * (1.f - s.S("coverage"));
        return add - dilution;
    });
    // 知識資產：文件沉澱 − 遺忘
    m.AddFlow("knowledge", [](const SDState& s) {
        return 0.02f * s.A("dev_rate") - 0.01f * s.S("knowledge");
    });

    // ---------- 離散與機率事件 ----------
    m.AddEvent(3, "features", -5.f);           // M3 需求變更砍範疇
    m.AddEvent(6, "tech_debt", -10.f);         // M6 債務清理 sprint
    m.AddRandomEvent(2, "tech_debt", 8.f, 0.5f);   // 引擎 bug 爆擊
    m.AddRandomEvent(4, "tech_debt", 8.f, 0.5f);
    m.AddRandomEvent(8, "tech_debt", 8.f, 0.5f);
    m.AddRandomEvent(9, "features", -4.f, 0.4f);   // 下游需求插單返工

    // ---------- KPI 探針 ----------
    m.AddProbe("velocity", [](const SDState& s) { return s.A("dev_rate"); });
    m.AddProbe("debt_ratio", [](const SDState& s) {
        return s.S("tech_debt") / (s.S("features") + 1.f);
    });
    m.AddProbe("ship_ready", [](const SDState& s) { // 保守模式旗標
        return (s.S("features") >= 95.f && s.S("coverage") > 0.75f &&
                s.S("tech_debt") < 30.f) ? 1.f : 0.f;
    });

    // ---------- 跑模擬 ----------
    std::printf("%-4s %-9s %-9s %-9s %-9s %s\n", "月", "功能", "債務",
                "覆蓋率", "知識", "velocity");
    for (int i = 0; i < months; ++i) {
        m.Step();
        const int mo = m.Month();
        std::printf("M%-3d %-9s %-9s %-9s %-9s %.2f\n", mo,
                    Bar(m.Stock("features"), 100.f),
                    Bar(m.Stock("tech_debt"), 100.f),
                    Bar(m.Stock("coverage"), 1.f),
                    Bar(m.Stock("knowledge"), 1.f),
                    m.ProbeHistory("velocity").back());
    }

    // ---------- KPI 觸發報告 ----------
    std::printf("\n--- KPI 觸發時間軸 ---\n");
    const auto debtBreach = m.BreachMonths("debt_ratio", 0.5f);
    if (debtBreach.empty()) {
        std::printf("債務指數 <0.5：全期健康\n");
    } else {
        std::printf("債務指數 >0.5 進入月份：");
        for (int b : debtBreach) std::printf("M%d ", b);
        std::printf("\n");
    }
    const auto ready = m.BreachMonths("ship_ready", 0.5f);
    if (ready.empty()) {
        std::printf("ship_ready：期內未達發布閘門\n");
    } else {
        std::printf("ship_ready 首次達標：M%d\n", ready.front());
    }

    std::printf("\n最終：功能 %.1f/100，債務 %.1f，覆蓋率 %.0f%%\n",
                m.Stock("features"), m.Stock("tech_debt"),
                m.Stock("coverage") * 100.f);
    return 0;
}
