/**
 * 訓練模型驅動的本地 AI Agent 示範
 *
 * RLGridAgent（AIAgent 子類，AgentType::Custom）載入 AIAgentTrainer
 * 產出的模型檔（.pql/.pdqn/.ppg/.pac，自動偵測格式），在 GridWorld
 * 任務中每步呼叫 MakeDecision 做 argmax 決策——取代基類的隨機選擇，
 * confidence 由 Q 值 margin / policy 機率給出。
 *
 * 用法：TrainedAgentDemo.exe [--model <path>] [--algo <a>] [--size <N>]
 */

#include "AI/AIAgentSystem.h"
#include "AI/ReinforcementLearning.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace Potato::AI;

namespace {

const std::vector<std::string> kActions = {"up", "down", "left", "right"};

int ArgMax(const std::vector<float>& v, size_t n) {
    int best = 0;
    for (size_t i = 1; i < n && i < v.size(); i++)
        if (v[i] > v[best]) best = static_cast<int>(i);
    return best;
}

// --algo 字串 → RLPolicy 載入提示（省略則依檔頭自動偵測）
RLPolicy::Algo ParseAlgoHint(const std::string& s) {
    if (s == "qlearning" || s == "ql") return RLPolicy::Algo::QLearning;
    if (s == "dqn") return RLPolicy::Algo::DQN;
    if (s == "pg")  return RLPolicy::Algo::PolicyGradient;
    if (s == "ac")  return RLPolicy::Algo::ActorCritic;
    if (s == "ppo") return RLPolicy::Algo::PPO;
    return RLPolicy::Algo::Unknown;
}

} // namespace

// 以訓練好的 RL 模型為決策核心的 AIAgent——決策核心走引擎的
// RLPolicy 統一介面（檔頭自動辨識 .pql/.pdqn/.ppg/.pac/.pppo），
// 不再手動分派各 agent 類別
class RLGridAgent : public AIAgent {
public:
    RLGridAgent(const AgentDesc& desc, int gridWidth)
        : AIAgent(desc), width(gridWidth) {}

    // 泛化特徵編碼需要讀取環境（goal/牆結構）——one-hot 模型用不到
    void SetEnv(const GridWorldEnvironment* e) { env = e; }

    // 模型檔宣告的輸入維度（Deserialize 後才準確；QL 回格子數）
    int ModelInputSize() const {
        int s = policy.GetStateSize();
        return s > 0 ? s : width * width;
    }

    const char* AlgoName() const { return policy.AlgoName(); }

    bool LoadBrain(const std::string& path, RLPolicy::Algo hint) {
        return policy.Load(path, hint);
    }

    // context 格式 "x,y"；options 序對應 env 動作 id（0=up..3=right）
    Decision MakeDecision(const std::string& context,
                          const std::vector<std::string>& options) override {
        int x = 0, y = 0;
        const char* s = context.c_str();
        const char* const e = s + context.size();
        const auto rx = std::from_chars(s, e, x);
        if (rx.ec != std::errc() || rx.ptr == e || *rx.ptr != ',')
            return AIAgent::MakeDecision(context, options);  // 解析失敗退回基類
        const auto ry = std::from_chars(rx.ptr + 1, e, y);
        if (ry.ec != std::errc() ||
            policy.GetAlgo() == RLPolicy::Algo::Unknown) {
            return AIAgent::MakeDecision(context, options);  // 解析失敗退回基類
        }

        // QL 走離散狀態索引；神經網路演算法依模型輸入維度選編碼：
        // width² → one-hot（固定圖訓練），其他 → GetFeatureState 泛化特徵
        // （--rand/--collect 訓練的模型；env 內部位置與 context 一致）
        std::vector<float> scores;
        if (policy.GetAlgo() == RLPolicy::Algo::QLearning) {
            scores = policy.GetTabularScores(y * width + x);
        } else {
            State encoded;
            if (ModelInputSize() != width * width && env) {
                encoded = env->GetFeatureState();
            } else {
                encoded.features.assign(static_cast<size_t>(width * width), 0.0f);
                encoded.features[y * width + x] = 1.0f;
            }
            scores = policy.GetActionScores(encoded);
        }
        const std::string scoreLabel =
            policy.OutputsProbabilities() ? "P" : "Q";
        if (scores.empty())
            return AIAgent::MakeDecision(context, options);

        const size_t n = std::min(options.size(), scores.size());
        int best = ArgMax(scores, n);

        // confidence：PG/AC 直接取 argmax 機率；Q 值用 margin 制
        // (best−second)/(best−worst) 映射 [0.5,1]——Q 值群集時 softmax 會趨近
        // 均勻分佈（~0.25），看不出決策強度
        float conf;
        if (scoreLabel == "P") {
            conf = scores[best];
        } else {
            float second = -1e30f, worst = 1e30f;
            for (size_t i = 0; i < n; i++) {
                if (static_cast<int>(i) != best) second = std::max(second, scores[i]);
                worst = std::min(worst, scores[i]);
            }
            conf = 0.5f + 0.5f * (scores[best] - second) /
                   (std::fabs(scores[best] - worst) + 1e-6f);
        }

        Decision d;
        d.action = options[best];
        d.confidence = conf;
        std::ostringstream rs;
        rs << scoreLabel << "(" << context << ")=[";
        for (size_t i = 0; i < n; i++) {
            rs << options[i] << ":" << scores[i];
            if (i + 1 < n) rs << " ";
        }
        rs << "]";
        d.reasoning.push_back(rs.str());
        d.reasoning.push_back("argmax → " + options[best]);
        d.timestamp = "0";
        return d;
    }

private:
    int width;
    const GridWorldEnvironment* env = nullptr;
    RLPolicy policy;
};

int main(int argc, char** argv) {
    std::cout << "=== Potato Engine 訓練模型 AI Agent 示範 ===\n\n";

    std::string modelPath = "trained_agent.pql";
    std::string algo;
    int gridSize = 6;
    int maxSteps = 50;
    int collectK = 0;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&](const char* n) -> std::string {
            if (i + 1 >= argc) { std::cerr << n << " 缺少參數值\n"; return {}; }
            return argv[++i];
        };
        if (a == "--model") { modelPath = next("--model"); if (modelPath.empty()) return 1; }
        else if (a == "--algo") { algo = next("--algo"); if (algo.empty()) return 1; }
        else if (a == "--size") gridSize = std::atoi(next("--size").c_str());
        else if (a == "--steps") maxSteps = std::atoi(next("--steps").c_str());
        else if (a == "--collect") collectK = std::atoi(next("--collect").c_str());
        else { std::cerr << "未知參數: " << a << "\n"; return 1; }
    }
    std::cout << "模型: " << modelPath << "\n\n";

    // 與訓練器相同的環境（半面牆）
    const std::pair<int, int> goal = {gridSize - 1, gridSize - 1};
    std::vector<std::pair<int, int>> obstacles;
    for (int y = 0; y < gridSize - 2; y++) obstacles.emplace_back(gridSize / 2, y);
    GridWorldEnvironment env(gridSize, gridSize, goal, obstacles);

    // --collect K：擺 K 個隨機道具（避開起點/終點/牆），
    // 對齊 trainer --rand --collect 訓出的 14 維模型
    if (collectK > 0) {
        std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<int> cell(0, gridSize - 1);
        std::vector<std::pair<int, int>> banned = obstacles;
        banned.push_back(goal);
        std::vector<std::pair<int, int>> pickups;
        while (static_cast<int>(pickups.size()) < collectK) {
            auto p = std::make_pair(cell(rng), cell(rng));
            if (p == std::make_pair(0, 0) ||
                std::find(banned.begin(), banned.end(), p) != banned.end())
                continue;
            pickups.push_back(p);
            banned.push_back(p);
        }
        env.SetPickups(pickups);
    }

    // 建立 RL 驅動的 agent（走 AIAgent 生命周期）
    AgentDesc desc;
    desc.name = "RLGridAgent";
    desc.type = AgentType::Custom;
    desc.capabilities = {"grid_navigation", "trained_policy"};
    RLGridAgent agent(desc, gridSize);
    agent.SetEnv(&env);
    agent.Initialize();

    if (!agent.LoadBrain(modelPath, ParseAlgoHint(algo))) {
        std::cerr << "模型載入失敗: " << modelPath << "\n";
        return 1;
    }
    std::cout << "決策核心已載入（演算法 " << agent.AlgoName()
              << "，輸入維度 " << agent.ModelInputSize()
              << (agent.ModelInputSize() == gridSize * gridSize
                      ? "：one-hot 固定圖"
                      : "：泛化特徵")
              << "）\n\n";

    AgentTask task;
    task.id = "reach-goal";
    task.description = "Navigate (0,0) to goal";
    task.priority = TaskPriority::High;
    agent.AssignTask(task);

    State state = env.Reset();
    env.Render();

    bool done = false;
    int steps = 0;
    for (; steps < maxSteps && !done; steps++) {
        std::ostringstream ctx;
        ctx << static_cast<int>(state.features[0]) << ","
            << static_cast<int>(state.features[1]);

        Decision d = agent.MakeDecision(ctx.str(), kActions);
        int actionId = static_cast<int>(std::distance(
            kActions.begin(), std::find(kActions.begin(), kActions.end(), d.action)));
        if (actionId >= 4) actionId = 0;

        auto result = env.Step(Action(actionId, d.action));
        state = result.nextState;
        done = result.done;

        std::cout << "step " << steps << "  (" << ctx.str() << ") → " << d.action
                  << "  conf=" << d.confidence << "\n";
        for (const auto& r : d.reasoning)
            std::cout << "    " << r << "\n";

        // 寫入情節記憶：狀態→動作
        MemoryItem mem;
        mem.type = MemoryType::Episodic;
        mem.content = ctx.str() + " -> " + d.action;
        mem.importance = d.confidence;
        agent.StoreMemory(mem);
    }

    env.Render();

    if (done) {
        agent.CompleteTask("goal reached in " + std::to_string(steps) + " steps");
        agent.RecordLearning("gridworld", "navigate", true);
        std::cout << "\n任務完成：" << steps << " 步到達目標\n";
        std::cout << "  情節記憶: " << agent.GetMemoryCount() << " 筆\n";
        std::cout << "  成功率統計: " << agent.GetSuccessRate() << "\n";
    } else {
        agent.FailTask("step limit reached");
        agent.RecordLearning("gridworld", "navigate", false);
        std::cout << "\n任務失敗：超過 " << maxSteps << " 步未到達\n";
    }

    agent.Shutdown();
    return done ? 0 : 1;
}
