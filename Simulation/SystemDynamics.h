#pragma once

#include "Core/CoreTypes.h"

#include <functional>
#include <limits>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace Potato {
namespace Simulation {

/**
 * SDState — 流量/輔助變數求值時的唯讀狀態視圖。
 * lambda 透過它讀本步（t）的存量、參數與已求值的輔助變數，
 * 以及上一步（t-1）的輔助變數（資訊延遲/平滑鏈用）。
 */
class SDState {
public:
    float S(const std::string& stock) const;  // 存量（t）
    float P(const std::string& param) const;  // 參數
    float A(const std::string& aux) const;    // 輔助變數（本步已算）
    float APrev(const std::string& aux) const;// 輔助變數（上一步值）
    int Month() const { return month; }

private:
    friend class SDModel;
    const std::map<std::string, float>* stocks = nullptr;
    const std::map<std::string, float>* params = nullptr;
    const std::map<std::string, float>* aux = nullptr;      // 本步
    const std::map<std::string, float>* auxPrev = nullptr;  // 上一步
    int month = 0;
};

/**
 * SDModel — 系統動力學存量-流量模擬器（離散步進，Euler 語義）。
 *
 * 對應文件《企業全維度整體運營》的方法論，泛化為遊戲模擬基建：
 *   - Stock：有單位、有初始值、可設上下界（CLAMP）的累積量——
 *     金錢、人口、補給品、民心、部隊戰力都適用
 *   - Flow：每步對某存量的淨流量（正=流入），以 t 狀態求值
 *   - Aux：輔助變數（比率、效率、缺口——**不自然累積**，每步重算）
 *   - Param：外生參數（CAC、流失率…可在步間調整做情境掃描）
 *   - Event：指定月份的離散衝擊（一次性存量加減——
 *     補確定性模型缺的事件注入層）
 *
 * 更新語義（Euler）：aux 依註冊序求值 → 所有 flow 以 t 狀態求值
 * → 同步套用 stock[t+1] = stock[t] + dt·net_flow → CLAMP → 記錄。
 * 這保證 A↔B 互相轉移時總量守恆（flow 都看到 t 而非混合時態）。
 *
 * 延遲建模：以 APrev 做一階指數平滑（資訊延遲）——
 *   AddAux("perceived", [](s){ return s.APrev("perceived")
 *          + (s.S("real") - s.APrev("perceived")) / s.P("tau"); });
 * 多階延遲鏈就串多個 aux。實體延遲（pipeline delay）用中間存量串。
 *
 * 預設 dt=1（對應月模型）；SetDt 可切週/季。純 CPU、決定性，
 * headless 可測——與引擎測試慣例一致。
 *
 * 用途示例：RTS 經濟（資金/原料/產能/訂單回饋環）、人口與民心、
 *   補給線存量、AI 敵國的戰略資源模擬。
 */
class SDModel {
public:
    using Fn = std::function<float(const SDState&)>;

    // ---- 建模（任意順序；aux/flow 的求值序 = 註冊序）----
    // 存量：lo/hi 為 CLAMP 界（預設 0..+inf 防負庫存類錯誤）
    void AddStock(const std::string& name, float initial,
                  float lo = 0.f,
                  float hi = std::numeric_limits<float>::infinity());
    void SetParam(const std::string& name, float value);
    void AddAux(const std::string& name, Fn fn);
    // 某存量的淨流量（多個 AddFlow 同名存量 = 流量相加）
    void AddFlow(const std::string& stock, Fn fn);
    // 離散事件：到達 month 步時對存量一次性加 delta
    void AddEvent(int month, const std::string& stock, float delta);
    // 機率性事件：到達 month 步時以 prob 機率觸發（確定性主模型
    // 之外的隨機衝擊層——風險事件、需求變更、bug 爆擊）。
    // SetSeed 控制種子；同 seed 結果完全可重現（測試可用）。
    void AddRandomEvent(int month, const std::string& stock,
                        float delta, float prob);
    void SetSeed(uint32 seed);

    // 物質/資訊延遲鏈：把 src 的輸出經 stages 級一階延遲攤開，
    // 總延遲時間約 tau（每級 tau/stages）。產生 aux name，
    // 以 APrev 鏈實現——近似原文件缺的 DELAY 算子
    // （stages 越大越接近純 pipeline delay）。
    void AddDelayChain(const std::string& name, Fn src,
                       int stages, float tau);

    // KPI 觀測探針：每步記錄 fn 求值到 ProbeHistory
    // （對應文件的 KPI 觸發/保守模式——flag 用 fn 回 0/1 即可）
    void AddProbe(const std::string& name, Fn fn);

    void SetDt(float dt) { dt_ = dt; }

    // ---- 執行 ----
    void Step();          // 推進一個 dt
    void Run(int steps);  // 連跑 N 步

    // ---- 查詢 ----
    float Stock(const std::string& name) const;
    float Aux(const std::string& name) const;   // 上一步求值結果
    int Month() const { return month_; }
    // 歷史軌跡（含初始值，長度 = month_+1）——情境比較/繪圖用
    const std::vector<float>& History(const std::string& stock) const;
    // 探針歷史（每步一點，長度 = month_）
    const std::vector<float>& ProbeHistory(const std::string& name) const;
    // 觸發月份掃描：probe 值上穿 threshold 的月份清單
    // （前一月 ≤ 閾值、本月 > 閾值才算一次觸發——邊沿觸發語義）
    std::vector<int> BreachMonths(const std::string& probe,
                                  float threshold) const;

    // 保守模式類布林觸發的消費端寫法：
    //   model.AddAux("conservative", [](s){ return
    //       (s.S("cash") < s.P("min_cash") ||
    //        s.S("compliance") < s.P("crit")) ? 1.f : 0.f; });
    // aux 本即可承載布林旗標（0/1），不需專用機制。

private:
    struct StockDef { float value, lo, hi; };
    struct Event { int month; std::string stock; float delta; float prob; };

    std::map<std::string, StockDef> stocks_;
    std::map<std::string, float> params_;
    std::vector<std::pair<std::string, Fn>> auxFns_;   // 有序
    std::vector<std::pair<std::string, Fn>> flowFns_;  // 有序
    std::vector<std::pair<std::string, Fn>> probeFns_; // KPI 觀測
    std::vector<Event> events_;
    std::map<std::string, float> aux_;                 // 本步
    std::map<std::string, float> auxPrev_;             // 上一步
    std::map<std::string, std::vector<float>> history_;
    std::map<std::string, std::vector<float>> probes_;
    std::mt19937 rng_{42};  // 固定預設種子：不設 SetSeed 也可重現
    float dt_ = 1.f;
    int month_ = 0;
};

} // namespace Simulation
} // namespace Potato
