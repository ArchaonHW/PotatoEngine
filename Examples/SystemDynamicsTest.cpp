// SystemDynamics：存量-流量模擬器測試（全 headless）。
// 驗證：Euler 同步更新（A↔B 轉移守恆）、CLAMP 存量界、
// 參數/輔助變數/事件注入、APrev 一階資訊延遲、
// 以及文件 R1（增長環）+ B3（現金流限制環）混合模型的
// 定性行為——增長後段被現金緩衝煞停。

#include "Simulation/SystemDynamics.h"

#include <cmath>
#include <cstdio>

using namespace Potato::Simulation;

namespace {

int g_pass = 0, g_fail = 0;

void Check(bool cond, const char* name) {
    if (cond) {
        ++g_pass;
        std::printf("[PASS] %s\n", name);
    } else {
        ++g_fail;
        std::printf("[FAIL] %s\n", name);
    }
}

bool Near(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) < eps;
}

} // namespace

int main() {
    std::printf("=== SystemDynamics Test ===\n\n");

    // ---- 基本流量積分 ----
    {
        SDModel m;
        m.AddStock("cash", 0.f);
        m.AddFlow("cash", [](const SDState&) { return 100.f; });
        m.Run(12);
        Check(Near(m.Stock("cash"), 1200.f), "線性流入 12 步累積");
        Check(m.History("cash").size() == 13, "history 含初始值");
        Check(m.Month() == 12, "month 計數");
    }

    // ---- Euler 同步語義：A↔B 轉移總量守恆 ----
    {
        SDModel m;
        m.AddStock("a", 100.f);
        m.AddStock("b", 0.f);
        // 兩條 flow 都以 t 態求值：a 流出 10% 進 b
        m.AddFlow("a", [](const SDState& s) { return -0.1f * s.S("a"); });
        m.AddFlow("b", [](const SDState& s) { return 0.1f * s.S("a"); });
        m.Run(10);
        Check(Near(m.Stock("a") + m.Stock("b"), 100.f),
              "A→B 轉移總量守恆（flow 皆見 t 態）");
        Check(Near(m.Stock("a"), 100.f * std::pow(0.9f, 10), 1e-3f),
              "指數衰減解析解一致");
    }

    // ---- CLAMP 存量界 ----
    {
        SDModel m;
        m.AddStock("supplies", 5.f);          // 預設下界 0
        m.AddFlow("supplies", [](const SDState&) { return -10.f; });
        m.Step();
        Check(m.Stock("supplies") == 0.f, "存量不為負（CLAMP 0）");

        SDModel m2;
        m2.AddStock("morale", 0.9f, 0.f, 1.f); // 0–1 指數存量
        m2.AddFlow("morale", [](const SDState&) { return 0.5f; });
        m2.Step();
        Check(m2.Stock("morale") == 1.f, "指數存量 CLAMP 上界");
    }

    // ---- 參數 + 輔助變數 + 註冊序依賴 ----
    {
        SDModel m;
        m.AddStock("customers", 100.f);
        m.SetParam("base_churn", 0.05f);
        m.SetParam("cac", 10.f);
        m.SetParam("marketing", 200.f);
        // aux 依賴先註冊的 aux：new_cust → net
        m.AddAux("new_cust", [](const SDState& s) {
            return s.P("marketing") / s.P("cac");
        });
        m.AddAux("net_growth", [](const SDState& s) {
            return s.A("new_cust") - s.S("customers") * s.P("base_churn");
        });
        m.AddFlow("customers", [](const SDState& s) {
            return s.A("net_growth");
        });
        m.Step();
        Check(Near(m.Aux("new_cust"), 20.f), "aux：獲客=行銷/CAC");
        Check(Near(m.Stock("customers"), 115.f),
              "aux→flow 傳導（100+20-5）");
        m.SetParam("marketing", 0.f);          // 關掉行銷 → 純流失
        m.Step();
        Check(Near(m.Stock("customers"), 115.f * 0.95f),
              "步間改參數生效（情境掃描）");
    }

    // ---- 離散事件注入 ----
    {
        SDModel m;
        m.AddStock("funds", 100.f);
        m.AddEvent(3, "funds", -40.f);         // 第 3 步突發支出
        m.Run(2);
        Check(Near(m.Stock("funds"), 100.f), "事件前存量不變");
        m.Run(2);                              // 跨過 month 3
        Check(Near(m.Stock("funds"), 60.f), "事件注入一次性生效");
        m.Run(3);
        Check(Near(m.Stock("funds"), 60.f), "事件不重複觸發");
    }

    // ---- APrev 一階資訊延遲（指數平滑追蹤階躍）----
    {
        SDModel m;
        m.AddStock("real", 0.f);
        m.SetParam("tau", 4.f);
        m.AddAux("perceived", [](const SDState& s) {
            return s.APrev("perceived")
                 + (s.S("real") - s.APrev("perceived")) / s.P("tau");
        });
        m.Step();                              // 讓 aux 初始化
        m.AddEvent(1, "real", 100.f);          // 階躍
        m.Run(12);
        // 一階延遲：12 步後應接近但未達 100（(1-1/τ)^12≈0.045 殘差）
        const float p = m.Aux("perceived");
        Check(p > 90.f && p < 100.f,
              "APrev 資訊延遲：平滑追蹤階躍");
    }

    // ---- R1 增長環 × B3 現金流限制環（定性行為）----
    {
        // 迷你 RTS 經濟：工廠產能產生收入→投資擴產（R1），
        // 但產能擴張佔用營運資本——現金緩衝低時擴產率自動煞停（B3）
        SDModel m;
        m.AddStock("cash", 50.f);
        m.AddStock("capacity", 10.f);
        m.SetParam("margin", 0.3f);        // 每單位產能月收入
        m.SetParam("capex_per_unit", 5.f); // 每單位產能擴張成本
        m.SetParam("buffer", 10.f);        // 最低現金緩衝

        // B3：可投資現金 = max(0, cash - buffer)
        m.AddAux("investable", [](const SDState& s) {
            return std::fmax(0.f, s.S("cash") - s.P("buffer"));
        });
        // 擴產率 = 可投資現金的一部分 → 花費與產能增量
        m.AddAux("expansion", [](const SDState& s) {
            return 0.5f * s.A("investable") / s.P("capex_per_unit");
        });
        m.AddFlow("capacity", [](const SDState& s) { return s.A("expansion"); });
        m.AddFlow("cash", [](const SDState& s) {
            return s.S("capacity") * s.P("margin")      // R1 收入
                 - s.A("expansion") * s.P("capex_per_unit"); // 擴產支出
        });

        m.Run(36);
        const auto& cap = m.History("capacity");
        const auto& cash = m.History("cash");
        Check(cap[36] > cap[0], "36 個月產能成長（R1 生效）");
        // B3 特徵：早期現金被擴產吸低（<buffer 觸發投資煞停段）
        bool dipped = false;
        for (float c : cash)
            if (c < 25.f) dipped = true;
        Check(dipped, "現金緩衝曾被擴張壓低（B3 煞停段存在）");
        // 後期現金累積起來 → 複利增長
        Check(cash[36] > cash[0], "長期現金累積（R1 複利）");
    }

    // ---- 延遲鏈（AddDelayChain）：階躍輸入經多級平滑到達 ----
    {
        SDModel m;
        m.AddStock("signal", 0.f);
        m.AddEvent(0, "signal", 100.f);   // 第一步就注入
        // 3 級延遲鏈，總 tau=6：第 3 級輸出應明顯滯後
        m.AddDelayChain("lag3", [](const SDState& s) {
            return s.S("signal");
        }, 3, 6.f);
        m.Step(); // 初始化 aux
        m.Run(3);
        const float early = m.Aux("lag3");
        Check(early > 0.f && early < 60.f,
              "延遲鏈早期輸出 < 源（滯後）");
        m.Run(20);
        Check(Near(m.Aux("lag3"), 100.f, 5.f),
              "延遲鏈長期收斂到源值");
    }

    // ---- 機率性事件：prob=1 等價確定、prob=0 永不、同 seed 可重現 ----
    {
        SDModel m;
        m.AddStock("hp", 100.f);
        m.AddRandomEvent(1, "hp", -10.f, 1.f);  // 必發
        m.AddRandomEvent(2, "hp", -10.f, 0.f);  // 永不
        m.Run(4);
        Check(Near(m.Stock("hp"), 90.f),
              "prob=1 必觸發、prob=0 不觸發");

        // 同 seed 兩次結果一致（可重現）；不同 seed 可能分歧
        SDModel a, b;
        for (auto* mm : {&a, &b}) {
            mm->AddStock("x", 0.f);
            mm->SetSeed(7);
            for (int i = 0; i < 10; ++i)
                mm->AddRandomEvent(i, "x", 1.f, 0.5f);
            mm->Run(10);
        }
        Check(Near(a.Stock("x"), b.Stock("x")),
              "同 seed 隨機事件序列可重現");
        // 50% 機率 10 次，命中數應在合理範圍（不為 0/10 極端）
        Check(a.Stock("x") > 0.f && a.Stock("x") < 10.f,
              "隨機事件命中數在合理範圍");
    }

    // ---- KPI 探針 + 邊沿觸發 ----
    {
        SDModel m;
        m.AddStock("cash", 100.f);
        m.AddFlow("cash", [](const SDState& s) {
            return s.Month() < 4 ? -20.f : 15.f; // 先燒後補
        });
        // 保守模式旗標：現金低於緩衝 → 1
        m.AddProbe("low_cash", [](const SDState& s) {
            return s.S("cash") < 50.f ? 1.f : 0.f;
        });
        m.Run(10);
        const auto& h = m.ProbeHistory("low_cash");
        Check(h.size() == 10, "probe 每步一點");
        const auto breaches = m.BreachMonths("low_cash", 0.5f);
        Check(!breaches.empty(), "偵測到進入低現金");
        // 邊沿觸發：持續低現金只記一次進入點
        int count1 = 0;
        for (float v : h) if (v > 0.5f) ++count1;
        Check(static_cast<int>(breaches.size()) < count1,
              "邊沿觸發：持續 breach 僅記進入點");
    }

    // ---- dt 與缺名健壯性 ----
    {
        SDModel m;
        m.AddStock("x", 0.f);
        m.SetDt(0.5f);
        m.AddFlow("x", [](const SDState&) { return 10.f; });
        m.Run(4);
        Check(Near(m.Stock("x"), 20.f), "dt 縮放（0.5×10×4）");
        Check(Near(m.Stock("nonexist"), 0.f), "缺名存量回 0");
        Check(Near(m.Aux("nonexist"), 0.f), "缺名 aux 回 0");
    }

    std::printf("結果：%d 通過 / %d 失敗\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
