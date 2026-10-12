/**
 * Potato Engine 本地 AI Agent 訓練器
 *
 * 在 GridWorld 環境訓練本地 RL agent，支援五種演算法：
 *   - --algo qlearning（預設）：tabular Q-Learning，模型檔 trained_agent.pql
 *   - --algo dqn：Deep Q-Network（experience replay + target network，
 *     --double 啟用 Double DQN 目標），模型檔 trained_agent.pdqn
 *   - --algo pg：Policy Gradient（REINFORCE + baseline），trained_agent.ppg
 *   - --algo ac：Actor-Critic（TD advantage），trained_agent.pac
 *   - --algo ppo：PPO（GAE + clipped surrogate），trained_agent.pppo
 *
 * 皆支援：訓練 → 存檔 → 重新載入 → 純 exploitation 評估 → 印出走位。
 * --resume 可載入既有模型續跑（DQN 的 replay buffer 不落盤，續跑時重新累積）。
 *
 * 泛化訓練：--rand 每 episode 換隨機地圖（牆/缺口/目標全隨機），
 * 觀測改用 env.GetFeatureState() 11 維特徵；--collect K 再撒 K 個
 * 隨機道具（14 維特徵，收完才准進終點）；--seed 讓地圖序列與
 * agent 採樣可重現。皆僅支援神經網路演算法（dqn/pg/ac/ppo）。
 *
 * 用法：
 *   AIAgentTrainer.exe [--algo qlearning|dqn|pg|ac|ppo] [--model <path>]
 *                      [--episodes <N>] [--size <N>] [--eval-only]
 *                      [--render] [--resume] [--explore <rate>]
 *                      [--rand] [--collect <K>] [--seed <N>]
 *                      [--double] [--log <csv>]
 */

#include "AI/ReinforcementLearning.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace Potato::AI;

namespace {

// GridWorldEnvironment 的 state.features = {x, y}；
// tabular Q-table 需要整數索引，映射為 y * width + x
int StateIndex(const State& state, int width) {
    int x = static_cast<int>(state.features[0]);
    int y = static_cast<int>(state.features[1]);
    return y * width + x;
}

// DQN 的網路輸入用 one-hot 狀態編碼——離散格點對 2 維連續座標的
// 梯度傳播太慢，one-hot 讓每格有獨立輸入通道，收斂速度接近 tabular
State EncodeState(const State& state, int width, int height) {
    State encoded;
    encoded.features.assign(static_cast<size_t>(width * height), 0.0f);
    encoded.features[StateIndex(state, width)] = 1.0f;
    return encoded;
}

struct TrainerConfig {
    std::string algo = "qlearning";
    std::string modelPath;
    bool modelSet = false;
    int gridSize = 6;
    int episodes = 2000;
    int maxStepsPerEpisode = 100;
    int evalEpisodes = 100;
    bool evalOnly = false;
    bool render = false;
    bool resume = false;
    float exploreRate = -1.0f;  // <0 表示沿用模型/預設值
    bool randMaps = false;      // 每 episode 隨機地圖（泛化訓練；神經網路演算法限定）
    int collectK = 0;           // >0 時每局撒 K 個道具，收完才算到終點（需 --rand）
    long seed = -1;             // <0 表示隨機（不可重現）
    bool doubleDQN = false;     // DQN 專用：Double DQN 目標計算
    std::string logPath;        // 非空時逐回合寫 CSV（RLTrainingLogger）
};

void PrintUsage(const char* argv0) {
    std::cout << "Usage: " << argv0 << " [--algo qlearning|dqn|pg|ac|ppo] [--model <path>]\n"
              << "       [--episodes <N>] [--size <N>] [--eval-only] [--render]\n"
              << "       [--resume] [--explore <rate>] [--rand] [--seed <N>]\n"
              << "       [--collect <K>] [--double] [--log <csv>]\n";
}

bool ParseArgs(int argc, char** argv, TrainerConfig& cfg) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        auto nextValue = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << name << " 缺少參數值\n";
                return {};
            }
            return argv[++i];
        };
        if (arg == "--algo") {
            cfg.algo = nextValue("--algo");
            if (cfg.algo.empty()) return false;
        } else if (arg == "--model") {
            cfg.modelPath = nextValue("--model");
            if (cfg.modelPath.empty()) return false;
            cfg.modelSet = true;
        } else if (arg == "--episodes") {
            cfg.episodes = std::atoi(nextValue("--episodes").c_str());
        } else if (arg == "--size") {
            cfg.gridSize = std::atoi(nextValue("--size").c_str());
        } else if (arg == "--eval-only") {
            cfg.evalOnly = true;
        } else if (arg == "--render") {
            cfg.render = true;
        } else if (arg == "--resume") {
            cfg.resume = true;
        } else if (arg == "--explore") {
            cfg.exploreRate = std::atof(nextValue("--explore").c_str());
        } else if (arg == "--rand") {
            cfg.randMaps = true;
        } else if (arg == "--seed") {
            cfg.seed = std::atol(nextValue("--seed").c_str());
        } else if (arg == "--collect") {
            cfg.collectK = std::atoi(nextValue("--collect").c_str());
        } else if (arg == "--double") {
            cfg.doubleDQN = true;
        } else if (arg == "--log") {
            cfg.logPath = nextValue("--log");
            if (cfg.logPath.empty()) return false;
        } else if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            return false;
        } else {
            std::cerr << "未知參數: " << arg << "\n";
            PrintUsage(argv[0]);
            return false;
        }
    }
    if (cfg.algo != "qlearning" && cfg.algo != "dqn" &&
        cfg.algo != "pg" && cfg.algo != "ac" && cfg.algo != "ppo") {
        std::cerr << "--algo 僅支援 qlearning / dqn / pg / ac / ppo\n";
        return false;
    }
    if (cfg.gridSize < 2) {
        std::cerr << "--size 至少為 2\n";
        return false;
    }
    if (cfg.randMaps && cfg.algo == "qlearning") {
        // tabular QL 的狀態索引綁定固定地圖，無法泛化——
        // 神經網路演算法（dqn/pg/ac/ppo）吃 GetFeatureState() 連續特徵才行
        std::cerr << "--rand 不支援 --algo qlearning（僅 dqn/pg/ac/ppo）\n";
        return false;
    }
    if (cfg.doubleDQN && cfg.algo != "dqn") {
        std::cerr << "--double 僅對 --algo dqn 有效\n";
        return false;
    }
    if (cfg.collectK > 0 && !cfg.randMaps) {
        // 收集狀態不在 one-hot/格子索引觀測裡——看不到道具學不了
        std::cerr << "--collect 需要搭配 --rand（特徵觀測才含道具資訊）\n";
        return false;
    }
    if (!cfg.modelSet) {
        if (cfg.algo == "dqn") cfg.modelPath = "trained_agent.pdqn";
        else if (cfg.algo == "pg") cfg.modelPath = "trained_agent.ppg";
        else if (cfg.algo == "ac") cfg.modelPath = "trained_agent.pac";
        else if (cfg.algo == "ppo") cfg.modelPath = "trained_agent.pppo";
        else cfg.modelPath = "trained_agent.pql";
    }
    return true;
}

// 障礙物：x = size/2 的半面牆（只留底部缺口），強迫 agent 學繞路
std::vector<std::pair<int, int>> MakeObstacles(int size) {
    std::vector<std::pair<int, int>> obstacles;
    int wallX = size / 2;
    for (int y = 0; y < size - 2; y++) {
        obstacles.emplace_back(wallX, y);
    }
    return obstacles;
}

// 隨機地圖（泛化訓練）：牆的方向/位置/缺口全隨機，保證可解——
// 牆永遠留一格缺口、目標 rejection sample 避開起點與障礙格
std::vector<std::pair<int, int>> MakeRandomObstacles(int size, std::mt19937& rng) {
    bool vertical = std::uniform_int_distribution<int>(0, 1)(rng) == 0;
    int wallPos = std::uniform_int_distribution<int>(1, size - 2)(rng);
    int gap = std::uniform_int_distribution<int>(0, size - 1)(rng);
    std::vector<std::pair<int, int>> obstacles;
    for (int i = 0; i < size; i++) {
        if (i == gap) continue;
        obstacles.emplace_back(vertical ? wallPos : i, vertical ? i : wallPos);
    }
    return obstacles;
}

// 隨機空格：避開起點 (0,0) 與 banned 列表（障礙/終點/已佔格）
std::pair<int, int> RandomFreeCell(int size, std::mt19937& rng,
                                   std::vector<std::pair<int, int>> banned) {
    std::uniform_int_distribution<int> cell(0, size - 1);
    while (true) {
        std::pair<int, int> p = {cell(rng), cell(rng)};
        if (p == std::make_pair(0, 0)) continue;
        if (std::find(banned.begin(), banned.end(), p) == banned.end())
            return p;
    }
}

std::pair<int, int> RandomGoal(int size,
                               const std::vector<std::pair<int, int>>& obstacles,
                               std::mt19937& rng) {
    return RandomFreeCell(size, rng, obstacles);
}

// 收集任務：撒 collectK 個不重複的道具格（避開起點/終點/牆/已放道具）
std::vector<std::pair<int, int>> RandomPickups(
        int size, int collectK,
        const std::pair<int, int>& goal,
        const std::vector<std::pair<int, int>>& obstacles,
        std::mt19937& rng) {
    std::vector<std::pair<int, int>> banned = obstacles;
    banned.push_back(goal);
    std::vector<std::pair<int, int>> pickups;
    for (int i = 0; i < collectK; i++) {
        auto p = RandomFreeCell(size, rng, banned);
        pickups.push_back(p);
        banned.push_back(p);
    }
    return pickups;
}

// 泛化 feature 編碼已升格為引擎 API：GridWorldEnvironment::GetFeatureState()
// （11 維 {x,y,dx,dy,四鄰感測,wallPos,gap,vert}，定義見 ReinforcementLearning.cpp）

// --rand 地圖生成器：啟用時每 Next() 產生新的隨機可解地圖，
// 停用時回傳固定 env 拷貝；seed 可選讓地圖序列可重現
struct MapGen {
    bool enabled = false;
    int collectK = 0;  // >0 時每張圖撒 K 個隨機道具
    std::mt19937 rng{std::random_device{}()};

    GridWorldEnvironment Next(int width, const GridWorldEnvironment& fixed) {
        if (!enabled) return fixed;
        auto obstacles = MakeRandomObstacles(width, rng);
        auto goal = RandomGoal(width, obstacles, rng);
        GridWorldEnvironment e(width, width, goal, obstacles);
        if (collectK > 0)
            e.SetPickups(RandomPickups(width, collectK, goal, obstacles, rng));
        return e;
    }
};

// 觀測編碼：rand → 11 維 GetFeatureState（位置/相對目標/四鄰感測/牆參數），
// 固定 → one-hot 格子索引。raw 與 env 內部位置一致，rand 路徑直接用 env
State EncodeObs(const TrainerConfig& cfg, const State& raw,
                const GridWorldEnvironment& e, int width) {
    return cfg.randMaps ? e.GetFeatureState()
                        : EncodeState(raw, width, width);
}

// 載入模型的統一評估：rand 每回合換新圖（量泛化）、固定模式同圖重跑。
// AgentT 需有 SelectAction(const State&, bool)；explore=true 為採樣 rollout
// （PG 的隨機策略本質），false 為 argmax
template <typename AgentT>
int EvalLoaded(const TrainerConfig& cfg, const GridWorldEnvironment& env,
               int width, AgentT& evalAgent, bool explore) {
    MapGen gen;
    gen.enabled = cfg.randMaps;
    gen.collectK = cfg.collectK;
    if (cfg.seed >= 0)
        gen.rng.seed(static_cast<unsigned int>(cfg.seed) + 9999u);
    if (cfg.seed >= 0)
        evalAgent.SetSeed(static_cast<unsigned int>(cfg.seed) + 7777u);

    if (cfg.randMaps)
        std::cout << "（每回合隨機新地圖——泛化測試）\n";

    int evalSuccess = 0;
    float evalReward = 0.0f;
    int evalSteps = 0;
    for (int ep = 0; ep < cfg.evalEpisodes; ep++) {
        GridWorldEnvironment cur = gen.Next(width, env);
        State state = EncodeObs(cfg, cur.Reset(), cur, width);
        for (int step = 0; step < cfg.maxStepsPerEpisode; step++) {
            auto result = cur.Step(evalAgent.SelectAction(state, explore));
            evalReward += result.reward;
            state = EncodeObs(cfg, result.nextState, cur, width);
            evalSteps++;
            if (result.done) {
                evalSuccess++;
                break;
            }
        }
    }
    std::cout << "評估 " << cfg.evalEpisodes << " 回合:\n"
              << "  成功率: " << (100.0f * evalSuccess / cfg.evalEpisodes) << "%\n"
              << "  平均 reward: " << (evalReward / cfg.evalEpisodes) << "\n"
              << "  平均步數: " << (static_cast<float>(evalSteps) / cfg.evalEpisodes)
              << "\n\n";

    if (cfg.render) {
        GridWorldEnvironment cur = gen.Next(width, env);
        auto policy = [&](const State& state) {
            return evalAgent.SelectAction(EncodeObs(cfg, state, cur, width),
                                          explore);
        };
        RenderGreedyTrajectory(cur, policy, cfg.maxStepsPerEpisode,
                               explore ? "採樣策略走位" : "貪婪策略走位");
    }
    return evalSuccess > 0 ? 0 : 1;
}

// reward shaping：撞牆/邊界（位置沒變）多扣分——
// 環境本身對撞牆只給 -0.01（同一般步驟），對 policy-gradient 系
// argmax 評估容易把「往牆走」鎖成最優；額外懲罰讓無效動作明確變差
float ShapeReward(float reward, const State& from, const State& to) {
    if (from.features == to.features) reward -= 0.1f;
    return reward;
}

bool WriteFile(const std::string& path, const std::string& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << data;
    return out.good();
}

bool ReadFile(const std::string& path, std::string& data) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    data = ss.str();
    return true;
}

// 純 exploitation 評估：policy 由呼叫端提供（QL 走 state index、DQN 走 one-hot）
int EvalPolicy(GridWorldEnvironment& env,
               const std::function<Action(const State&)>& policy,
               int evalEpisodes,
               int maxSteps) {
    int evalSuccess = 0;
    float evalReward = 0.0f;
    int evalSteps = 0;
    for (int ep = 0; ep < evalEpisodes; ep++) {
        State state = env.Reset();
        for (int step = 0; step < maxSteps; step++) {
            auto result = env.Step(policy(state));
            evalReward += result.reward;
            state = result.nextState;
            evalSteps++;
            if (result.done) {
                evalSuccess++;
                break;
            }
        }
    }

    std::cout << "評估 " << evalEpisodes << " 回合:\n"
              << "  成功率: " << (100.0f * evalSuccess / evalEpisodes) << "%\n"
              << "  平均 reward: " << (evalReward / evalEpisodes) << "\n"
              << "  平均步數: " << (static_cast<float>(evalSteps) / evalEpisodes)
              << "\n\n";
    return evalSuccess;
}

// 以 env 重跑一次 rollout，把每步位置記下來印成路徑
void RenderGreedyTrajectory(GridWorldEnvironment& env,
                            const std::function<Action(const State&)>& policy,
                            int maxSteps,
                            const char* label = "貪婪策略走位") {
    State state = env.Reset();
    std::vector<std::pair<int, int>> path;
    path.emplace_back(static_cast<int>(state.features[0]),
                      static_cast<int>(state.features[1]));

    for (int step = 0; step < maxSteps; step++) {
        auto result = env.Step(policy(state));
        state = result.nextState;
        path.emplace_back(static_cast<int>(state.features[0]),
                          static_cast<int>(state.features[1]));
        if (result.done) break;
    }

    std::cout << label << "（" << path.size() - 1 << " 步）:\n  ";
    for (size_t i = 0; i < path.size(); i++) {
        std::cout << "(" << path[i].first << "," << path[i].second << ")";
        if (i + 1 < path.size()) std::cout << " -> ";
    }
    std::cout << "\n";

    env.Reset();
    env.Render();
}

int RunQLearning(const TrainerConfig& cfg, GridWorldEnvironment& env, int width) {
    const int numStates = width * width;
    const int numActions = 4;

    QLearningAgent agent(numStates, numActions,
                         /*learningRate*/ 0.1f,
                         /*discountFactor*/ 0.99f,
                         /*explorationRate*/ 1.0f);

    if (cfg.evalOnly) {
        std::string data;
        if (!ReadFile(cfg.modelPath, data) || !agent.Deserialize(data)) {
            std::cerr << "無法載入模型檔: " << cfg.modelPath << "\n";
            return 1;
        }
        std::cout << "已載入模型: " << cfg.modelPath << "\n\n";
    } else {
        // 續跑：先載入既有 Q-table，再繼續訓練
        if (cfg.resume) {
            std::string data;
            if (!ReadFile(cfg.modelPath, data) || !agent.Deserialize(data)) {
                std::cerr << "續跑失敗，無法載入模型檔: " << cfg.modelPath << "\n";
                return 1;
            }
            std::cout << "已載入既有模型續跑: " << cfg.modelPath << "\n";
        }
        if (cfg.exploreRate >= 0.0f) {
            agent.SetExplorationRate(cfg.exploreRate);
        }
        if (cfg.seed >= 0)
            agent.SetSeed(static_cast<unsigned int>(cfg.seed));
        std::cout << "開始訓練（" << cfg.episodes << " episodes"
                  << "，探索率 " << agent.GetExplorationRate() << "）...\n";

        int successCount = 0;
        for (int episode = 0; episode < cfg.episodes; episode++) {
            State state = env.Reset();
            float totalReward = 0.0f;

            for (int step = 0; step < cfg.maxStepsPerEpisode; step++) {
                int s = StateIndex(state, width);
                int actionId = agent.SelectAction(s, true);
                auto result = env.Step(Action(actionId, ""));
                int s2 = StateIndex(result.nextState, width);

                float reward = ShapeReward(result.reward, state, result.nextState);
                agent.Update(s, actionId, reward, s2, result.done);
                totalReward += reward;
                state = result.nextState;

                if (result.done) {
                    successCount++;
                    break;
                }
            }

            agent.DecayExplorationRate(0.995f);

            if (episode % 200 == 0 || episode == cfg.episodes - 1) {
                std::cout << "  Episode " << episode
                          << "  reward=" << totalReward
                          << "  累計成功=" << successCount << "\n";
            }
        }

        std::cout << "\n訓練完成: 成功率 "
                  << (100.0f * successCount / cfg.episodes) << "%\n";

        if (!WriteFile(cfg.modelPath, agent.Serialize())) {
            std::cerr << "模型寫入失敗: " << cfg.modelPath << "\n";
            return 1;
        }
        std::cout << "模型已儲存: " << cfg.modelPath << "\n\n";
    }

    // 驗證：全新 agent 從檔案載入，純 exploitation 評估
    std::cout << "=== 載入模型評估 ===\n";
    QLearningAgent evalAgent(numStates, numActions);
    std::string data;
    if (!ReadFile(cfg.modelPath, data) || !evalAgent.Deserialize(data)) {
        std::cerr << "模型載入失敗: " << cfg.modelPath << "\n";
        return 1;
    }

    auto policy = [&](const State& state) {
        return Action(evalAgent.SelectAction(StateIndex(state, width), false), "");
    };
    int evalSuccess = EvalPolicy(env, policy, cfg.evalEpisodes,
                                 cfg.maxStepsPerEpisode);
    if (cfg.render) {
        RenderGreedyTrajectory(env, policy, cfg.maxStepsPerEpisode);
    }
    return evalSuccess > 0 ? 0 : 1;
}

int RunDQN(const TrainerConfig& cfg, GridWorldEnvironment& env, int width) {
    const int numActions = 4;
    // rand 模式：11 維泛化特徵（GetFeatureState）；固定模式：one-hot
    const int stateSize = cfg.randMaps
        ? GridWorldEnvironment::FEATURE_STATE_SIZE + (cfg.collectK > 0 ? 3 : 0)
        : width * width;

    MapGen gen;
    gen.enabled = cfg.randMaps;
    gen.collectK = cfg.collectK;
    if (cfg.seed >= 0)
        gen.rng.seed(static_cast<unsigned int>(cfg.seed));

    auto encode = [&](const State& raw, const GridWorldEnvironment& e) {
        return EncodeObs(cfg, raw, e, width);
    };

    DQNAgent agent(stateSize, numActions,
                   /*learningRate*/ 0.01f,
                   /*discountFactor*/ 0.99f,
                   /*explorationRate*/ 1.0f);

    if (cfg.evalOnly) {
        std::string data;
        if (!ReadFile(cfg.modelPath, data) || !agent.Deserialize(data)) {
            std::cerr << "無法載入模型檔: " << cfg.modelPath << "\n";
            return 1;
        }
        std::cout << "已載入模型: " << cfg.modelPath << "\n\n";
    } else {
        if (cfg.resume) {
            std::string data;
            if (!ReadFile(cfg.modelPath, data) || !agent.Deserialize(data)) {
                std::cerr << "續跑失敗，無法載入模型檔: " << cfg.modelPath << "\n";
                return 1;
            }
            std::cout << "已載入既有模型續跑（replay buffer 重新累積）: "
                      << cfg.modelPath << "\n";
        }
        if (cfg.exploreRate >= 0.0f) {
            agent.SetExplorationRate(cfg.exploreRate);
        }
        if (cfg.seed >= 0)
            agent.SetSeed(static_cast<unsigned int>(cfg.seed) + 1111u);
        agent.SetDoubleDQN(cfg.doubleDQN);
        RLTrainingLogger loggerDQN(cfg.logPath);
        std::cout << "開始 DQN 訓練（" << cfg.episodes << " episodes"
                  << "，探索率 " << agent.GetExplorationRate()
                  << (cfg.randMaps ? "，隨機地圖" : "") << "）...\n";

        int successCount = 0;
        int totalSteps = 0;
        for (int episode = 0; episode < cfg.episodes; episode++) {
            GridWorldEnvironment cur = gen.Next(width, env);
            State state = encode(cur.Reset(), cur);
            float totalReward = 0.0f;
            int epSteps = 0;
            bool epDone = false;

            for (int step = 0; step < cfg.maxStepsPerEpisode; step++) {
                Action action = agent.SelectAction(state, true);
                auto result = cur.Step(action);
                State nextState = encode(result.nextState, cur);

                Experience exp;
                exp.state = state;
                exp.action = action;
                exp.reward = ShapeReward(result.reward, state, nextState);
                exp.nextState = nextState;
                exp.done = result.done;
                agent.TrainStep(exp);

                totalReward += exp.reward;
                state = nextState;
                totalSteps++;
                epSteps = step + 1;
                epDone = result.done;

                // 定期同步 target network，穩定 bootstrapped target
                if (totalSteps % 20 == 0) {
                    agent.UpdateTargetNetwork();
                }
                if (result.done) {
                    successCount++;
                    break;
                }
            }
            loggerDQN.LogEpisode(episode, totalReward, epSteps, epDone);

            agent.DecayExplorationRate(0.995f);

            if (episode % 200 == 0 || episode == cfg.episodes - 1) {
                std::cout << "  Episode " << episode
                          << "  reward=" << totalReward
                          << "  累計成功=" << successCount
                          << "  buffer=" << agent.GetReplayBufferSize() << "\n";
            }
        }
        loggerDQN.Flush();
        if (loggerDQN.IsOpen())
            std::cout << "訓練曲線已寫入: " << cfg.logPath << "\n";

        std::cout << "\n訓練完成: 成功率 "
                  << (100.0f * successCount / cfg.episodes) << "%\n";

        if (!WriteFile(cfg.modelPath, agent.Serialize())) {
            std::cerr << "模型寫入失敗: " << cfg.modelPath << "\n";
            return 1;
        }
        std::cout << "模型已儲存: " << cfg.modelPath << "\n\n";
    }

    // 驗證：全新 agent 從檔案載入，純 exploitation 評估
    // rand 模式每回合換新隨機圖——量測的是泛化成功率而非背圖
    std::cout << "=== 載入模型評估 ===\n";
    DQNAgent evalAgent(stateSize, numActions);
    std::string data;
    if (!ReadFile(cfg.modelPath, data) || !evalAgent.Deserialize(data)) {
        std::cerr << "模型載入失敗: " << cfg.modelPath << "\n";
        return 1;
    }
    return EvalLoaded(cfg, env, width, evalAgent, /*explore=*/false);
}

// PG（REINFORCE）：整回合 rollout 存 trajectory，episode 末一次性 UpdatePolicy
int RunPolicyGradient(const TrainerConfig& cfg, GridWorldEnvironment& env, int width) {
    const int numActions = 4;
    const int stateSize = cfg.randMaps
        ? GridWorldEnvironment::FEATURE_STATE_SIZE + (cfg.collectK > 0 ? 3 : 0)
        : width * width;

    MapGen gen;
    gen.enabled = cfg.randMaps;
    gen.collectK = cfg.collectK;
    if (cfg.seed >= 0)
        gen.rng.seed(static_cast<unsigned int>(cfg.seed));

    PolicyGradientAgent agent(stateSize, numActions,
                              /*learningRate*/ 0.01f,
                              /*discountFactor*/ 0.99f);

    if (cfg.evalOnly) {
        std::string data;
        if (!ReadFile(cfg.modelPath, data) || !agent.Deserialize(data)) {
            std::cerr << "無法載入模型檔: " << cfg.modelPath << "\n";
            return 1;
        }
        std::cout << "已載入模型: " << cfg.modelPath << "\n\n";
    } else {
        if (cfg.resume) {
            std::string data;
            if (!ReadFile(cfg.modelPath, data) || !agent.Deserialize(data)) {
                std::cerr << "續跑失敗，無法載入模型檔: " << cfg.modelPath << "\n";
                return 1;
            }
            std::cout << "已載入既有模型續跑: " << cfg.modelPath << "\n";
        }
        if (cfg.seed >= 0)
            agent.SetSeed(static_cast<unsigned int>(cfg.seed) + 2222u);
        std::cout << "開始 Policy Gradient 訓練（" << cfg.episodes << " episodes"
                  << (cfg.randMaps ? "，隨機地圖" : "") << "）...\n";

        int successCount = 0;
        for (int episode = 0; episode < cfg.episodes; episode++) {
            GridWorldEnvironment cur = gen.Next(width, env);
            State state = EncodeObs(cfg, cur.Reset(), cur, width);
            float totalReward = 0.0f;

            for (int step = 0; step < cfg.maxStepsPerEpisode; step++) {
                Action action = agent.SelectAction(state, true);
                auto result = cur.Step(action);
                State nextState = EncodeObs(cfg, result.nextState, cur, width);

                float reward = ShapeReward(result.reward, state, nextState);
                agent.StoreTrajectory(state, action, reward);
                totalReward += reward;
                state = nextState;

                if (result.done) {
                    successCount++;
                    break;
                }
            }
            agent.UpdatePolicy();

            if (episode % 200 == 0 || episode == cfg.episodes - 1) {
                std::cout << "  Episode " << episode
                          << "  reward=" << totalReward
                          << "  累計成功=" << successCount << "\n";
            }
        }

        std::cout << "\n訓練完成: 成功率 "
                  << (100.0f * successCount / cfg.episodes) << "%\n";

        if (!WriteFile(cfg.modelPath, agent.Serialize())) {
            std::cerr << "模型寫入失敗: " << cfg.modelPath << "\n";
            return 1;
        }
        std::cout << "模型已儲存: " << cfg.modelPath << "\n\n";
    }

    std::cout << "=== 載入模型評估 ===\n";
    std::cout << "（PG 為隨機策略——評估用採樣 rollout，argmax 非其設計目標）\n";
    PolicyGradientAgent evalAgent(stateSize, numActions);
    std::string data;
    if (!ReadFile(cfg.modelPath, data) || !evalAgent.Deserialize(data)) {
        std::cerr << "模型載入失敗: " << cfg.modelPath << "\n";
        return 1;
    }
    return EvalLoaded(cfg, env, width, evalAgent, /*explore=*/true);
}

// AC（Actor-Critic）：每步 critic TD 更新 + actor advantage 更新（online）
int RunActorCritic(const TrainerConfig& cfg, GridWorldEnvironment& env, int width) {
    const int numActions = 4;
    const int stateSize = cfg.randMaps
        ? GridWorldEnvironment::FEATURE_STATE_SIZE + (cfg.collectK > 0 ? 3 : 0)
        : width * width;

    MapGen gen;
    gen.enabled = cfg.randMaps;
    gen.collectK = cfg.collectK;
    if (cfg.seed >= 0)
        gen.rng.seed(static_cast<unsigned int>(cfg.seed));

    ActorCriticAgent agent(stateSize, numActions,
                           /*actorLearningRate*/ 0.01f,
                           /*criticLearningRate*/ 0.01f,
                           /*discountFactor*/ 0.99f);

    if (cfg.evalOnly) {
        std::string data;
        if (!ReadFile(cfg.modelPath, data) || !agent.Deserialize(data)) {
            std::cerr << "無法載入模型檔: " << cfg.modelPath << "\n";
            return 1;
        }
        std::cout << "已載入模型: " << cfg.modelPath << "\n\n";
    } else {
        if (cfg.resume) {
            std::string data;
            if (!ReadFile(cfg.modelPath, data) || !agent.Deserialize(data)) {
                std::cerr << "續跑失敗，無法載入模型檔: " << cfg.modelPath << "\n";
                return 1;
            }
            std::cout << "已載入既有模型續跑: " << cfg.modelPath << "\n";
        }
        if (cfg.seed >= 0)
            agent.SetSeed(static_cast<unsigned int>(cfg.seed) + 3333u);
        std::cout << "開始 Actor-Critic 訓練（" << cfg.episodes << " episodes"
                  << (cfg.randMaps ? "，隨機地圖" : "") << "）...\n";

        int successCount = 0;
        for (int episode = 0; episode < cfg.episodes; episode++) {
            GridWorldEnvironment cur = gen.Next(width, env);
            State state = EncodeObs(cfg, cur.Reset(), cur, width);
            float totalReward = 0.0f;

            for (int step = 0; step < cfg.maxStepsPerEpisode; step++) {
                Action action = agent.SelectAction(state, true);
                auto result = cur.Step(action);
                State nextState = EncodeObs(cfg, result.nextState, cur, width);

                float reward = ShapeReward(result.reward, state, nextState);
                agent.TrainStep(state, action, reward, nextState, result.done);
                totalReward += reward;
                state = nextState;

                if (result.done) {
                    successCount++;
                    break;
                }
            }

            if (episode % 200 == 0 || episode == cfg.episodes - 1) {
                std::cout << "  Episode " << episode
                          << "  reward=" << totalReward
                          << "  累計成功=" << successCount << "\n";
            }
        }

        std::cout << "\n訓練完成: 成功率 "
                  << (100.0f * successCount / cfg.episodes) << "%\n";

        if (!WriteFile(cfg.modelPath, agent.Serialize())) {
            std::cerr << "模型寫入失敗: " << cfg.modelPath << "\n";
            return 1;
        }
        std::cout << "模型已儲存: " << cfg.modelPath << "\n\n";
    }

    std::cout << "=== 載入模型評估 ===\n";
    ActorCriticAgent evalAgent(stateSize, numActions);
    std::string data;
    if (!ReadFile(cfg.modelPath, data) || !evalAgent.Deserialize(data)) {
        std::cerr << "模型載入失敗: " << cfg.modelPath << "\n";
        return 1;
    }
    return EvalLoaded(cfg, env, width, evalAgent, /*explore=*/false);
}

int RunPPO(const TrainerConfig& cfg, GridWorldEnvironment& env, int width) {
    const int numActions = 4;
    const int stateSize = cfg.randMaps
        ? GridWorldEnvironment::FEATURE_STATE_SIZE + (cfg.collectK > 0 ? 3 : 0)
        : width * width;

    MapGen gen;
    gen.enabled = cfg.randMaps;
    gen.collectK = cfg.collectK;
    if (cfg.seed >= 0)
        gen.rng.seed(static_cast<unsigned int>(cfg.seed));

    PPOAgent agent(stateSize, numActions,
                   /*actorLR*/ 0.003f, /*criticLR*/ 0.01f,
                   /*gamma*/ 0.99f, /*clip*/ 0.2f, /*gaeLambda*/ 0.95f,
                   /*epochs*/ 4);

    if (cfg.evalOnly) {
        std::string data;
        if (!ReadFile(cfg.modelPath, data) || !agent.Deserialize(data)) {
            std::cerr << "無法載入模型檔: " << cfg.modelPath << "\n";
            return 1;
        }
        std::cout << "已載入模型: " << cfg.modelPath << "\n\n";
    } else {
        if (cfg.resume) {
            std::string data;
            if (!ReadFile(cfg.modelPath, data) || !agent.Deserialize(data)) {
                std::cerr << "續跑失敗，無法載入模型檔: " << cfg.modelPath << "\n";
                return 1;
            }
            std::cout << "已載入既有模型續跑: " << cfg.modelPath << "\n";
        }
        if (cfg.seed >= 0)
            agent.SetSeed(static_cast<unsigned int>(cfg.seed) + 4444u);
        RLTrainingLogger logger(cfg.logPath);
        std::cout << "開始 PPO 訓練（" << cfg.episodes << " episodes"
                  << (cfg.randMaps ? "，隨機地圖" : "") << "）...\n";

        int successCount = 0;
        for (int episode = 0; episode < cfg.episodes; episode++) {
            GridWorldEnvironment cur = gen.Next(width, env);
            State state = EncodeObs(cfg, cur.Reset(), cur, width);
            float totalReward = 0.0f;
            int epSteps = 0;
            bool epDone = false;

            // on-policy：整條 trajectory 進 buffer，回合末一次 Update
            for (int step = 0; step < cfg.maxStepsPerEpisode; step++) {
                Action action = agent.SelectAction(state, true);
                auto result = cur.Step(action);
                State nextState = EncodeObs(cfg, result.nextState, cur, width);

                float reward = ShapeReward(result.reward, state, nextState);
                agent.StoreTransition(state, action, reward, result.done);

                totalReward += reward;
                state = nextState;
                epSteps = step + 1;
                epDone = result.done;
                if (result.done) {
                    successCount++;
                    break;
                }
            }
            agent.Update();
            logger.LogEpisode(episode, totalReward, epSteps, epDone);

            if (episode % 200 == 0 || episode == cfg.episodes - 1) {
                std::cout << "  Episode " << episode
                          << "  reward=" << totalReward
                          << "  累計成功=" << successCount << "\n";
            }
        }
        logger.Flush();
        if (logger.IsOpen())
            std::cout << "訓練曲線已寫入: " << cfg.logPath << "\n";

        std::cout << "\n訓練完成: 成功率 "
                  << (100.0f * successCount / cfg.episodes) << "%\n";

        if (!WriteFile(cfg.modelPath, agent.Serialize())) {
            std::cerr << "模型寫入失敗: " << cfg.modelPath << "\n";
            return 1;
        }
        std::cout << "模型已儲存: " << cfg.modelPath << "\n\n";
    }

    std::cout << "=== 載入模型評估 ===\n";
    PPOAgent evalAgent(stateSize, numActions);
    std::string data;
    if (!ReadFile(cfg.modelPath, data) || !evalAgent.Deserialize(data)) {
        std::cerr << "模型載入失敗: " << cfg.modelPath << "\n";
        return 1;
    }
    return EvalLoaded(cfg, env, width, evalAgent, /*explore=*/false);
}

} // namespace

int main(int argc, char** argv) {
    std::cout << "=== Potato Engine 本地 AI Agent 訓練器 ===\n\n";

    TrainerConfig cfg;
    if (!ParseArgs(argc, argv, cfg)) {
        return 1;
    }

    const int width = cfg.gridSize;
    const std::pair<int, int> goal = {width - 1, width - 1};
    auto obstacles = MakeObstacles(width);

    GridWorldEnvironment env(width, width, goal, obstacles);

    std::cout << "演算法: " << cfg.algo << "\n"
              << "環境: " << width << "x" << width << " GridWorld\n"
              << "  起點: (0,0)  目標: (" << goal.first << "," << goal.second << ")\n"
              << "  障礙物: " << obstacles.size() << " 格\n"
              << "  狀態數: " << width * width << "  動作數: 4\n\n";

    int rc;
    if (cfg.algo == "dqn")      rc = RunDQN(cfg, env, width);
    else if (cfg.algo == "pg")  rc = RunPolicyGradient(cfg, env, width);
    else if (cfg.algo == "ac")  rc = RunActorCritic(cfg, env, width);
    else if (cfg.algo == "ppo") rc = RunPPO(cfg, env, width);
    else                        rc = RunQLearning(cfg, env, width);
    if (rc != 0) {
        std::cerr << "評估失敗：訓練後 agent 無法到達目標\n";
        return rc;
    }

    std::cout << "=== 訓練流程完成 ===\n";
    return 0;
}
