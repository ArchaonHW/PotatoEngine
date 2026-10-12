#include "Simulation/SystemDynamics.h"

#include <algorithm>

namespace Potato {
namespace Simulation {

float SDState::S(const std::string& name) const {
    auto it = stocks->find(name);
    return it != stocks->end() ? it->second : 0.f;
}
float SDState::P(const std::string& name) const {
    auto it = params->find(name);
    return it != params->end() ? it->second : 0.f;
}
float SDState::A(const std::string& name) const {
    auto it = aux->find(name);
    return it != aux->end() ? it->second : 0.f;
}
float SDState::APrev(const std::string& name) const {
    auto it = auxPrev->find(name);
    return it != auxPrev->end() ? it->second : 0.f;
}

void SDModel::AddStock(const std::string& name, float initial,
                       float lo, float hi) {
    auto& s = stocks_[name];
    s.value = std::clamp(initial, lo, hi);
    s.lo = lo;
    s.hi = hi;
    if (history_[name].empty())
        history_[name].push_back(s.value);
}

void SDModel::SetParam(const std::string& name, float value) {
    params_[name] = value;
}

void SDModel::AddAux(const std::string& name, Fn fn) {
    auxFns_.emplace_back(name, std::move(fn));
}

void SDModel::AddFlow(const std::string& stock, Fn fn) {
    flowFns_.emplace_back(stock, std::move(fn));
}

void SDModel::AddEvent(int month, const std::string& stock, float delta) {
    events_.push_back({month, stock, delta, 1.f});
}

void SDModel::AddRandomEvent(int month, const std::string& stock,
                             float delta, float prob) {
    events_.push_back({month, stock, delta, std::clamp(prob, 0.f, 1.f)});
}

void SDModel::SetSeed(uint32 seed) {
    rng_.seed(seed);
}

void SDModel::AddDelayChain(const std::string& name, Fn src,
                            int stages, float tau) {
    stages = std::max(1, stages);
    const float stageTau = tau / stages;
    // 每級：out = prevSelf + (input − prevSelf)/stageTau
    // 第 1 級 input = src()；第 i>1 級 input = A(上一級本步值)
    // （註冊序即求值序，上一級此時已算完）
    std::string prev; // 空 = 吃 src
    for (int i = 1; i <= stages; ++i) {
        const std::string stageName =
            (i == stages) ? name : name + "#" + std::to_string(i);
        if (prev.empty()) {
            AddAux(stageName, [src, stageTau, stageName](const SDState& s) {
                const float cur = s.APrev(stageName);
                return cur + (src(s) - cur) / stageTau;
            });
        } else {
            const std::string prevStage = prev;
            AddAux(stageName,
                   [stageTau, stageName, prevStage](const SDState& s) {
                       const float cur = s.APrev(stageName);
                       return cur + (s.A(prevStage) - cur) / stageTau;
                   });
        }
        prev = stageName;
    }
}

void SDModel::Step() {
    // 建唯讀狀態視圖（流量求值時全部看到 t 態）
    std::map<std::string, float> stockView;
    for (const auto& [n, s] : stocks_)
        stockView[n] = s.value;

    SDState st;
    st.stocks = &stockView;
    st.params = &params_;
    st.aux = &aux_;
    st.auxPrev = &auxPrev_;
    st.month = month_;

    // 1. aux 依註冊序求值（aux 可引用先註冊的 aux）。
    //    auxPrev_ 必須在求值前先把上一步結果降為 prev——
    //    放在迴圈後會讓 APrev 延兩步而非一步。
    auxPrev_ = aux_;
    std::map<std::string, float> newAux;
    SDState ast = st;
    ast.aux = &newAux;
    for (const auto& [name, fn] : auxFns_)
        newAux[name] = fn ? fn(ast) : 0.f;
    aux_ = std::move(newAux);
    st.aux = &aux_;

    // 2. 所有流量以 t 態同步求值（Euler：先算完再套用，
    //    A↔B 互相轉移時總量守恆）
    std::map<std::string, float> deltas;
    for (const auto& [stock, fn] : flowFns_)
        deltas[stock] += fn ? fn(st) : 0.f;

    // 3. 離散事件注入（本月觸發；prob<1 走 RNG）
    std::uniform_real_distribution<float> uni(0.f, 1.f);
    for (const auto& e : events_)
        if (e.month == month_ && (e.prob >= 1.f || uni(rng_) < e.prob))
            deltas[e.stock] += e.delta;

    // 4. 套用 + CLAMP + 記錄
    ++month_;
    for (auto& [name, s] : stocks_) {
        s.value = std::clamp(s.value + dt_ * deltas[name], s.lo, s.hi);
        history_[name].push_back(s.value);
    }

    // 5. KPI 探針記錄（本步狀態——aux/存量都已是 t+1）
    for (const auto& [n, s] : stocks_)
        stockView[n] = s.value; // 重建 t+1 視圖
    st.month = month_;
    for (const auto& [name, fn] : probeFns_)
        probes_[name].push_back(fn ? fn(st) : 0.f);
}

void SDModel::Run(int steps) {
    for (int i = 0; i < steps; ++i)
        Step();
}

float SDModel::Stock(const std::string& name) const {
    auto it = stocks_.find(name);
    return it != stocks_.end() ? it->second.value : 0.f;
}

float SDModel::Aux(const std::string& name) const {
    auto it = aux_.find(name);
    return it != aux_.end() ? it->second : 0.f;
}

const std::vector<float>& SDModel::History(const std::string& name) const {
    static const std::vector<float> empty;
    auto it = history_.find(name);
    return it != history_.end() ? it->second : empty;
}

void SDModel::AddProbe(const std::string& name, Fn fn) {
    probeFns_.emplace_back(name, std::move(fn));
}

const std::vector<float>& SDModel::ProbeHistory(
    const std::string& name) const {
    static const std::vector<float> empty;
    auto it = probes_.find(name);
    return it != probes_.end() ? it->second : empty;
}

std::vector<int> SDModel::BreachMonths(const std::string& probe,
                                       float threshold) const {
    std::vector<int> out;
    const auto& h = ProbeHistory(probe);
    float prev = -std::numeric_limits<float>::infinity();
    for (size_t i = 0; i < h.size(); ++i) {
        if (h[i] > threshold && prev <= threshold)
            out.push_back(static_cast<int>(i) + 1); // 記錄的月份=步後值
        prev = h[i];
    }
    return out;
}

} // namespace Simulation
} // namespace Potato
