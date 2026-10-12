/**
 * Potato Engine Reinforcement Learning Module
 * Q-Learning, DQN, and other RL algorithms for AI agents
 */

#pragma once

#include "QuantumSampler.h"

#include <vector>
#include <unordered_map>
#include <memory>
#include <random>
#include <functional>

namespace Potato {
namespace AI {

class NeuralNetwork;
class QuantumSampler;

/**
 * Action space
 */
struct Action {
    int id;
    std::string name;
    std::vector<float> parameters;
    
    Action() : id(0) {}
    Action(int id, const std::string& name) : id(id), name(name) {}
};

/**
 * State representation
 */
struct State {
    std::vector<float> features;
    std::string description;
    uint64_t timestamp;
    
    State() : timestamp(0) {}
    State(const std::vector<float>& f) : features(f), timestamp(0) {}
};

/**
 * Experience tuple for replay buffer
 */
struct Experience {
    State state;
    Action action;
    float reward;
    State nextState;
    bool done;
    
    Experience() : reward(0.0f), done(false) {}
};

/**
 * Q-Learning Agent (Tabular)
 */
class QLearningAgent {
public:
    QLearningAgent(int numStates, int numActions, 
                   float learningRate = 0.1f, 
                   float discountFactor = 0.99f,
                   float explorationRate = 0.1f);
    
    // Select action
    int SelectAction(int state, bool explore = true);
    
    // Update Q-value
    void Update(int state, int action, float reward, int nextState, bool done);
    
    // Learning rate decay
    void DecayLearningRate(float decay = 0.995f);
    void DecayExplorationRate(float decay = 0.995f);

    // 續跑訓練用：Deserialize 後探索率已衰減，可直接重設再訓練
    void SetExplorationRate(float rate) { explorationRate = rate; }
    void SetLearningRate(float rate) { learningRate = rate; }
    float GetExplorationRate() const { return explorationRate; }
    // 可重現訓練：固定 epsilon-greedy 採樣序列
    void SetSeed(unsigned int seed) { rng.seed(seed); }

    // Story 13.3 — opt-in Born-rule sampling for action choice.
    // Same seed + call order ⇒ identical draws; default off.
    void EnableQuantumSampling(std::uint64_t seed);
    void DisableQuantumSampling();
    bool HasQuantumSampling() const { return quantumSampler != nullptr; }

    // Getters
    float GetQValue(int state, int action) const;
    const std::vector<std::vector<float>>& GetQTable() const { return qTable; }
    
    // Save/Load
    std::string Serialize() const;
    bool Deserialize(const std::string& data);
    
private:
    std::vector<std::vector<float>> qTable;
    int numStates;
    int numActions;
    float learningRate;
    float discountFactor;
    float explorationRate;
    
    std::mt19937 rng;
    std::unique_ptr<QuantumSampler> quantumSampler;
};

/**
 * Deep Q-Network (DQN) Agent
 */
class DQNAgent {
public:
    DQNAgent(int stateSize, int numActions,
             float learningRate = 0.001f,
             float discountFactor = 0.99f,
             float explorationRate = 0.1f,
             size_t replayBufferSize = 10000,
             size_t batchSize = 32);
    ~DQNAgent();

    DQNAgent(const DQNAgent&) = delete;
    DQNAgent& operator=(const DQNAgent&) = delete;

    // Select action
    Action SelectAction(const State& state, bool explore = true);

    // Train step
    void TrainStep(const Experience& exp);

    // Training from replay buffer
    void TrainFromReplayBuffer();

    // Add experience
    void AddExperience(const Experience& exp);

    // Learning rate and exploration decay
    void DecayLearningRate(float decay = 0.995f);
    void DecayExplorationRate(float decay = 0.995f);

    // 續跑訓練用：Deserialize 後探索率已衰減，可直接重設再訓練
    void SetExplorationRate(float rate) { explorationRate = rate; }
    void SetLearningRate(float rate) { learningRate = rate; }
    float GetExplorationRate() const { return explorationRate; }
    // 可重現訓練：固定 epsilon-greedy 採樣與 replay buffer 取樣序列
    void SetSeed(unsigned int seed) { rng.seed(seed); }

    // Story 13.3 — opt-in Born-rule sampling for action choice.
    void EnableQuantumSampling(std::uint64_t seed);
    void DisableQuantumSampling();
    bool HasQuantumSampling() const { return quantumSampler != nullptr; }

    // Double DQN：target 動作由 qNetwork 選、Q 值由 targetNetwork 評——
    // 修正標準 DQN 的 max 過估偏差（van Hasselt 2016）。
    // 序列化版本 PDQNv2；讀取 v1 模型時此旗標為 false
    void SetDoubleDQN(bool enable) { doubleDQN = enable; }
    bool IsDoubleDQN() const { return doubleDQN; }

    // Target network update
    void UpdateTargetNetwork();

    // Getters
    size_t GetReplayBufferSize() const { return replayBuffer.size(); }
    // 模型輸入維度（Deserialize 後反映檔案值）——消費端據此選擇狀態編碼
    int GetStateSize() const { return stateSize; }
    // 決策橋接用：讀取 Q 值向量（confidence/margin 計算）
    std::vector<float> GetQValues(const State& state) { return PredictQValues(state); }

    // 模型持久化（PDQNv1：超參數 + 內嵌 Q-network 的 PNNv1 blob；
    // replay buffer 不落盤，Deserialize 後 target network 由 Q-network 重建）
    std::string Serialize() const;
    bool Deserialize(const std::string& data);

private:
    std::unique_ptr<NeuralNetwork> qNetwork;
    std::unique_ptr<NeuralNetwork> targetNetwork;
    
    int stateSize;
    int numActions;
    float learningRate;
    float discountFactor;
    float explorationRate;
    bool doubleDQN = false;
    
    std::vector<Experience> replayBuffer;
    size_t maxReplayBufferSize;
    size_t batchSize;
    
    std::mt19937 rng;
    std::unique_ptr<QuantumSampler> quantumSampler;
    
    float PredictQValue(const State& state, int action);
    std::vector<float> PredictQValues(const State& state);
    std::vector<float> PredictTargetQValues(const State& state);
};

/**
 * Policy Gradient Agent (REINFORCE)
 */
class PolicyGradientAgent {
public:
    PolicyGradientAgent(int stateSize, int numActions,
                        float learningRate = 0.001f,
                        float discountFactor = 0.99f);
    ~PolicyGradientAgent();

    PolicyGradientAgent(const PolicyGradientAgent&) = delete;
    PolicyGradientAgent& operator=(const PolicyGradientAgent&) = delete;

    
    // Select action based on policy；explore=false 時取 argmax（評估用）
    Action SelectAction(const State& state, bool explore = true);

    // Store trajectory
    void StoreTrajectory(const State& state, const Action& action, float reward);

    // Update policy from trajectory
    void UpdatePolicy();

    // Clear trajectory
    void ClearTrajectory();

    // 決策橋接用：讀取動作機率分佈（confidence 計算）
    std::vector<float> GetProbabilities(const State& state) { return GetActionProbabilities(state); }
    // 模型輸入維度（Deserialize 後反映檔案值）——消費端據此選擇狀態編碼
    int GetStateSize() const { return stateSize; }
    // 可重現訓練：固定動作採樣序列
    void SetSeed(unsigned int seed) { rng.seed(seed); }

    // Story 13.3 — opt-in Born-rule sampling for action choice.
    void EnableQuantumSampling(std::uint64_t seed);
    void DisableQuantumSampling();
    bool HasQuantumSampling() const { return quantumSampler != nullptr; }
    // Story 13.4 — on the explore path, amplify the argmax action by
    // `rounds` Grover iterations; 0 = plain quantum sampling (default).
    void SetQuantumAmplification(int rounds) { quantumAmpRounds = rounds; }

    // 模型持久化（PPGv1：超參數 + 內嵌 policy network 的 PNNv1 blob）
    std::string Serialize() const;
    bool Deserialize(const std::string& data);

private:
    std::unique_ptr<NeuralNetwork> policyNetwork;

    int stateSize;
    int numActions;
    float learningRate;
    float discountFactor;

    struct TrajectoryStep {
        State state;
        Action action;
        float reward;
    };
    std::vector<TrajectoryStep> trajectory;
    std::mt19937 rng;
    std::unique_ptr<QuantumSampler> quantumSampler;
    int quantumAmpRounds = 0;

    std::vector<float> ComputeReturns();
    std::vector<float> GetActionProbabilities(const State& state);
};

/**
 * Actor-Critic Agent
 */
class ActorCriticAgent {
public:
    ActorCriticAgent(int stateSize, int numActions,
                    float actorLearningRate = 0.001f,
                    float criticLearningRate = 0.001f,
                    float discountFactor = 0.99f);
    ~ActorCriticAgent();

    ActorCriticAgent(const ActorCriticAgent&) = delete;
    ActorCriticAgent& operator=(const ActorCriticAgent&) = delete;

    // Select action；explore=false 時取 argmax（評估用）
    Action SelectAction(const State& state, bool explore = true);

    // Train step
    void TrainStep(const State& state, const Action& action,
                   float reward, const State& nextState, bool done);

    // 決策橋接用：讀取動作機率分佈 / 狀態價值（confidence 計算）
    std::vector<float> GetProbabilities(const State& state) { return GetActionProbabilities(state); }
    float GetStateValue(const State& state) { return GetValue(state); }
    // 模型輸入維度（Deserialize 後反映檔案值）——消費端據此選擇狀態編碼
    int GetStateSize() const { return stateSize; }
    // 可重現訓練：固定動作採樣序列
    void SetSeed(unsigned int seed) { rng.seed(seed); }

    // Story 13.3 — opt-in Born-rule sampling for action choice.
    void EnableQuantumSampling(std::uint64_t seed);
    void DisableQuantumSampling();
    bool HasQuantumSampling() const { return quantumSampler != nullptr; }
    // Story 13.4 — Grover amplification rounds on the explore path.
    void SetQuantumAmplification(int rounds) { quantumAmpRounds = rounds; }

    // 模型持久化（PACv1：超參數 + actor/critic 兩段長度前綴 PNNv1 blob）
    std::string Serialize() const;
    bool Deserialize(const std::string& data);

private:
    std::unique_ptr<NeuralNetwork> actorNetwork;
    std::unique_ptr<NeuralNetwork> criticNetwork;
    
    int stateSize;
    int numActions;
    float actorLearningRate;
    float criticLearningRate;
    float discountFactor;
    std::mt19937 rng;
    std::unique_ptr<QuantumSampler> quantumSampler;
    int quantumAmpRounds = 0;
    
    std::vector<float> GetActionProbabilities(const State& state);
    float GetValue(const State& state);
};

/**
 * PPO Agent（Proximal Policy Optimization, Schulman 2017）
 * on-policy：StoreTransition 收集 rollout → Update() 以 clipped
 * surrogate + GAE 做 K 個 epoch 的 minibatch 更新。
 * 比裸 A2C 穩：策略更新幅度被 clip 約束，不怕單步炸掉。
 */
class PPOAgent {
public:
    PPOAgent(int stateSize, int numActions,
             float actorLearningRate = 0.0003f,
             float criticLearningRate = 0.001f,
             float discountFactor = 0.99f,
             float clipEpsilon = 0.2f,
             float gaeLambda = 0.95f,
             int updateEpochs = 4);
    ~PPOAgent();

    PPOAgent(const PPOAgent&) = delete;
    PPOAgent& operator=(const PPOAgent&) = delete;

    // Select action；explore=false 時取 argmax（評估用）
    Action SelectAction(const State& state, bool explore = true);

    // rollout 收集：每步記一筆（logProb/value 由內部補記亦可——
    // 此 API 由呼叫端給，因為 SelectAction 與 Step 之間可能隔了其他邏輯）
    void StoreTransition(const State& state, const Action& action,
                         float reward, bool done);

    // rollout 結束（或達 batchSize）後呼叫：算 GAE、跑 updateEpochs
    // 個 epoch 的 clipped surrogate 更新，然後清空 buffer
    void Update();

    // 決策橋接用：機率分佈 / 狀態價值（confidence 計算）
    std::vector<float> GetProbabilities(const State& state);
    float GetStateValue(const State& state);
    // 模型輸入維度（Deserialize 後反映檔案值）
    int GetStateSize() const { return stateSize; }
    void SetSeed(unsigned int seed) { rng.seed(seed); }

    // Story 13.3 — opt-in Born-rule sampling for action choice.
    void EnableQuantumSampling(std::uint64_t seed);
    void DisableQuantumSampling();
    bool HasQuantumSampling() const { return quantumSampler != nullptr; }
    // Story 13.4 — Grover amplification rounds on the explore path.
    void SetQuantumAmplification(int rounds) { quantumAmpRounds = rounds; }

    // 模型持久化（PPPOv1：超參數 + actor/critic 兩段長度前綴 PNNv1 blob）
    std::string Serialize() const;
    bool Deserialize(const std::string& data);

private:
    std::unique_ptr<NeuralNetwork> actorNetwork;
    std::unique_ptr<NeuralNetwork> criticNetwork;

    int stateSize;
    int numActions;
    float actorLearningRate;
    float criticLearningRate;
    float discountFactor;
    float clipEpsilon;
    float gaeLambda;
    int updateEpochs;

    struct Transition {
        State state;
        Action action;
        float reward;
        float logProb;   // log π_old(a|s)——rollout 時凍結的策略
        float value;     // V_old(s)
        bool done;
    };
    std::vector<Transition> buffer;

    std::mt19937 rng;
    std::unique_ptr<QuantumSampler> quantumSampler;
    int quantumAmpRounds = 0;

    std::vector<float> ComputeGAE() const;  // 回傳每步 advantage（最後一步 done 截斷）
};

/**
 * 訓練曲線記錄：逐回合寫 CSV（episode,reward,steps,success），
 * 給 AIAgentTrainer --log 與研究實驗用；檔案不可寫時靜默退化
 */
class RLTrainingLogger {
public:
    explicit RLTrainingLogger(const std::string& csvPath);

    bool IsOpen() const { return open; }
    void LogEpisode(int episode, float reward, int steps, bool success);
    void Flush();

private:
    std::string path;
    bool open = false;
    std::vector<std::string> rows;  // 緩衝，Flush 時一次落盤（atomic-ish）
};

/**
 * RL 決策介面卡：把序列化模型檔包成統一推論介面，
 * 供 AIAgentSystem / 遊戲端掛載——Load 依檔頭 magic
 * （PQL 裸表/PDQNv1-2/PPGv1/PACv1/PPPOv1）自動辨識演算法。
 * 觀測編碼由消費端提供（GridWorld 用 GetFeatureState/one-hot）。
 */
class RLPolicy {
public:
    enum class Algo { Unknown, QLearning, DQN, PolicyGradient, ActorCritic, PPO };

    // algo=Unknown 時依檔頭自動偵測；QL 無 magic，作為最後 fallback
    bool Load(const std::string& modelPath, Algo algo = Algo::Unknown);
    bool LoadFromString(const std::string& data, Algo algo = Algo::Unknown);

    Algo GetAlgo() const { return algo; }
    const char* AlgoName() const;
    // 神經網路演算法的輸入維度（QL 回 -1，它的「狀態」是離散索引）
    int GetStateSize() const;
    // QL 專用：離散狀態索引 → 動作
    int SelectTabularAction(int state, bool explore = false);
    // NN 演算法通用：feature 向量 → 動作
    Action SelectAction(const State& state, bool explore = false);

    // 決策橋接用（confidence/margin 計算）：
    // QL 專用——離散狀態索引 → 各動作 Q 值列
    std::vector<float> GetTabularScores(int state) const;
    // NN 演算法通用——feature 向量 → Q 值（DQN）或動作機率（PG/AC/PPO）
    std::vector<float> GetActionScores(const State& state) const;
    // 輸出層語義：PG/AC/PPO 為機率分佈，QL/DQN 為 Q 值
    bool OutputsProbabilities() const { return pg || ac || ppo; }

private:
    Algo algo = Algo::Unknown;
    std::unique_ptr<QLearningAgent> ql;
    std::unique_ptr<DQNAgent> dqn;
    std::unique_ptr<PolicyGradientAgent> pg;
    std::unique_ptr<ActorCriticAgent> ac;
    std::unique_ptr<PPOAgent> ppo;
};

/**
 * Multi-Armed Bandit (for simple reward maximization)
 */
class MultiArmedBandit {
public:
    MultiArmedBandit(int numArms, float explorationRate = 0.1f);
    
    // Select arm
    int SelectArm();
    
    // Update with reward
    void Update(int arm, float reward);
    
    // Get estimated values
    const std::vector<float>& GetValues() const { return values; }
    const std::vector<int>& GetCounts() const { return counts; }
    
    // Story 13.3 — opt-in Born-rule sampling for arm choice.
    void EnableQuantumSampling(std::uint64_t seed);
    void DisableQuantumSampling();
    bool HasQuantumSampling() const { return quantumSampler != nullptr; }
    
private:
    int numArms;
    float explorationRate;
    std::vector<float> values;
    std::vector<int> counts;
    
    std::mt19937 rng;
    std::unique_ptr<QuantumSampler> quantumSampler;
};

/**
 * Environment interface for RL
 */
class RLEnvironment {
public:
    virtual ~RLEnvironment() = default;
    
    struct StepResult {
        State nextState;
        float reward;
        bool done;
        std::string info;
    };
    
    // Reset environment
    virtual State Reset() = 0;
    
    // Step environment
    virtual StepResult Step(const Action& action) = 0;
    
    // Get action space
    virtual std::vector<Action> GetActionSpace() const = 0;
    
    // Get state space size
    virtual int GetStateSize() const = 0;
    
    // Render (optional)
    virtual void Render() {}
};

/**
 * Simple Grid World Environment
 */
class GridWorldEnvironment : public RLEnvironment {
public:
    // GetFeatureState() 輸出維度——訓練/推論端建立 agent 時對齊輸入
    static constexpr int FEATURE_STATE_SIZE = 11;

    GridWorldEnvironment(int width, int height, 
                        const std::pair<int, int>& goal,
                        const std::vector<std::pair<int, int>>& obstacles = {});
    
    State Reset() override;
    StepResult Step(const Action& action) override;
    std::vector<Action> GetActionSpace() const override;
    int GetStateSize() const override;
    void Render() override;

    // 泛化訓練用：feature 編碼需要讀取目標/障礙配置
    const std::pair<int, int>& GetGoal() const { return goal; }
    const std::vector<std::pair<int, int>>& GetObstacles() const { return obstacles; }
    int GetWidth() const { return width; }
    int GetHeight() const { return height; }

    // 泛化觀測（11 維）：{x, y, dx, dy, bU, bD, bL, bR, wallPos, gap, vert}
    // 鄰格感測順序同動作 0-3（up/down/left/right）；wallPos/gap/vert 由
    // 障礙格回推直線牆參數（牆軸位置、缺口位置、是否垂直），
    // 讓跨地圖泛化成為完全可觀測的 MDP
    State GetFeatureState() const;

    // 收集任務（可選）：設 pickups 後 agent 須踩完全部道具再到終點
    // 才算 done；踩到道具給 +0.3 reward。feature 觀測自動擴為 14 維
    // （追加最近道具的 dx,dy 與剩餘比例）
    void SetPickups(const std::vector<std::pair<int, int>>& p);
    const std::vector<std::pair<int, int>>& GetPickups() const { return pickups; }
    int GetRemainingPickups() const;
    // feature 維度隨 pickups 有無切換——消費端以此對齊 agent 輸入
    int GetFeatureStateSize() const {
        return FEATURE_STATE_SIZE + (pickups.empty() ? 0 : 3);
    }

private:
    int width;
    int height;
    std::pair<int, int> goal;
    std::vector<std::pair<int, int>> obstacles;
    std::pair<int, int> currentPosition;
    std::vector<Action> actions;
    std::vector<std::pair<int, int>> pickups;
    std::vector<bool> collected;

    bool IsObstacle(int x, int y) const;
    bool IsGoal(int x, int y) const;
    bool IsValidPosition(int x, int y) const;
};

/**
 * Training session for RL
 */
class RLTrainingSession {
public:
    RLTrainingSession();
    
    // Training loop
    void Train(RLEnvironment& env, 
               std::function<Action(const State&)> policy,
               int numEpisodes,
               int maxStepsPerEpisode);
    
    // Evaluation
    float Evaluate(RLEnvironment& env,
                  std::function<Action(const State&)> policy,
                  int numEpisodes,
                  int maxStepsPerEpisode);
    
    // Get training statistics
    const std::vector<float>& GetEpisodeRewards() const { return episodeRewards; }
    const std::vector<int>& GetEpisodeLengths() const { return episodeLengths; }
    
private:
    std::vector<float> episodeRewards;
    std::vector<int> episodeLengths;
};

/**
 * Reward shaping for better learning
 */
class RewardShaper {
public:
    // Potential-based reward shaping
    static float PotentialBasedShaping(float reward, 
                                       const State& state,
                                       const State& nextState,
                                       std::function<float(const State&)> potential,
                                       float gamma = 0.99f);
    
    // Sparse to dense reward conversion
    static float SparseToDense(float reward, 
                               int steps,
                               float shapingWeight = 0.01f);
    
    // Curriculum learning reward scaling
    static float CurriculumScaling(float reward, 
                                   float difficulty,
                                   float minReward,
                                   float maxReward);
};

/**
 * Exploration strategies
 */
namespace Exploration {
    // Epsilon-greedy — rng is the caller's seeded stream (never
    // hidden OS entropy): same stream state ⇒ same choice.
    // Returns Action(-1) when there is nothing to pick.
    Action EpsilonGreedy(const std::vector<Action>& actions,
                         const std::vector<float>& values,
                         float epsilon,
                         std::mt19937& rng);
    
    // Upper Confidence Bound (UCB)
    Action UCB(const std::vector<Action>& actions,
               const std::vector<float>& values,
               const std::vector<int>& counts,
               float c = 1.0f,
               int totalSteps = 0);
    
    // Boltzmann (softmax) exploration — temperature <= 0 selects
    // the argmax deterministically without touching the stream.
    Action Boltzmann(const std::vector<Action>& actions,
                     const std::vector<float>& values,
                     float temperature,
                     std::mt19937& rng);
}

/**
 * Curriculum Learning
 */
class CurriculumLearning {
public:
    struct Stage {
        std::string name;
        float difficulty;
        int numEpisodes;
        std::function<void(RLEnvironment&)> setup;
    };
    
    CurriculumLearning();
    
    void AddStage(const Stage& stage);
    void Train(RLEnvironment& env, 
               std::function<Action(const State&)> policy);
    
private:
    std::vector<Stage> stages;
    size_t currentStage;
};

} // namespace AI
} // namespace Potato
