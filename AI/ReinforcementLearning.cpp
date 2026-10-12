/**
 * Potato Engine Reinforcement Learning Implementation
 */

#include "ReinforcementLearning.h"
#include "NeuralNetwork.h"
#include "QuantumSampler.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <random>
#include <sstream>
#include <iostream>
#include <fstream>

namespace Potato {
namespace AI {

namespace {
// NeuralNetwork 內含 unique_ptr 層，無法整體複製；逐層複製權重/偏差
void CopyNetworkWeights(const NeuralNetwork& src, NeuralNetwork& dst) {
    const auto& srcLayers = src.GetLayers();
    const auto& dstLayers = dst.GetLayers();
    for (size_t i = 0; i < srcLayers.size() && i < dstLayers.size(); i++) {
        dstLayers[i]->SetWeights(srcLayers[i]->GetWeights());
        dstLayers[i]->SetBiases(srcLayers[i]->GetBiases());
    }
}
} // namespace

// ============================================================================
// Q-Learning Agent
// ============================================================================

QLearningAgent::QLearningAgent(int numStates, int numActions,
                               float learningRate,
                               float discountFactor,
                               float explorationRate)
    : numStates(numStates)
    , numActions(numActions)
    , learningRate(learningRate)
    , discountFactor(discountFactor)
    , explorationRate(explorationRate)
    , rng(std::random_device{}()) {
    
    qTable.resize(numStates, std::vector<float>(numActions, 0.0f));
}

void QLearningAgent::EnableQuantumSampling(std::uint64_t seed) {
    quantumSampler = std::make_unique<QuantumSampler>(seed);
}
void QLearningAgent::DisableQuantumSampling() { quantumSampler.reset(); }

int QLearningAgent::SelectAction(int state, bool explore) {
    // Stream parity: the roll is only consumed on the explore path.
    if (explore && (quantumSampler
            ? quantumSampler->DrawDouble() < explorationRate
            : std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) < explorationRate)) {
        // Explore: random action
        return quantumSampler
            ? quantumSampler->DrawInt(numActions)
            : std::uniform_int_distribution<int>(0, numActions - 1)(rng);
    } else {
        // Exploit: best action
        int bestAction = 0;
        float bestValue = qTable[state][0];
        for (int i = 1; i < numActions; i++) {
            if (qTable[state][i] > bestValue) {
                bestValue = qTable[state][i];
                bestAction = i;
            }
        }
        return bestAction;
    }
}

void QLearningAgent::Update(int state, int action, float reward, int nextState, bool done) {
    float maxNextQ = 0.0f;
    if (!done) {
        for (int i = 0; i < numActions; i++) {
            if (qTable[nextState][i] > maxNextQ) {
                maxNextQ = qTable[nextState][i];
            }
        }
    }
    
    float target = reward + discountFactor * maxNextQ;
    float error = target - qTable[state][action];
    qTable[state][action] += learningRate * error;
}

void QLearningAgent::DecayLearningRate(float decay) {
    learningRate *= decay;
}

void QLearningAgent::DecayExplorationRate(float decay) {
    explorationRate *= decay;
}

float QLearningAgent::GetQValue(int state, int action) const {
    return qTable[state][action];
}

std::string QLearningAgent::Serialize() const {
    std::stringstream ss;
    ss << numStates << " " << numActions << "\n";
    ss << learningRate << " " << discountFactor << " " << explorationRate << "\n";
    
    for (const auto& row : qTable) {
        for (float val : row) {
            ss << val << " ";
        }
        ss << "\n";
    }
    
    return ss.str();
}

bool QLearningAgent::Deserialize(const std::string& data) {
    // 先解析到暫存，全部合法才提交——垃圾/截斷輸入不汙染 agent 狀態
    std::stringstream ss(data);

    int ns = 0, na = 0;
    float lr = 0.f, df = 0.f, er = 0.f;
    if (!(ss >> ns >> na) || !(ss >> lr >> df >> er)) {
        return false;
    }
    // 上界防禦：畸形頭部可能給出天文數字尺寸
    if (ns <= 0 || na <= 0 ||
        static_cast<long long>(ns) * na > 10'000'000) {
        return false;
    }

    std::vector<std::vector<float>> table(
        static_cast<size_t>(ns), std::vector<float>(na));
    for (auto& row : table) {
        for (float& val : row) {
            if (!(ss >> val)) {
                return false;
            }
        }
    }

    numStates = ns;
    numActions = na;
    learningRate = lr;
    discountFactor = df;
    explorationRate = er;
    qTable = std::move(table);
    return true;
}

// ============================================================================
// DQN Agent
// ============================================================================

DQNAgent::DQNAgent(int stateSize, int numActions,
                   float learningRate,
                   float discountFactor,
                   float explorationRate,
                   size_t replayBufferSize,
                   size_t batchSize)
    : stateSize(stateSize)
    , numActions(numActions)
    , learningRate(learningRate)
    , discountFactor(discountFactor)
    , explorationRate(explorationRate)
    , maxReplayBufferSize(replayBufferSize)
    , batchSize(batchSize)
    , rng(std::random_device{}()) {
    
    // Create networks
    // AddLayer 慣例：activation 標在輸入端層位，最後一層的宣告被丟棄——
    // Q 值輸出需 linear（含負 reward），relu 輸出會擋掉負向梯度
    qNetwork = std::make_unique<NeuralNetwork>();
    qNetwork->AddLayer(stateSize);
    qNetwork->AddLayer(128, "relu");
    qNetwork->AddLayer(128, "linear");
    qNetwork->AddLayer(numActions);
    qNetwork->Build();
    qNetwork->SetLossFunction("mse");

    targetNetwork = std::make_unique<NeuralNetwork>();
    targetNetwork->AddLayer(stateSize);
    targetNetwork->AddLayer(128, "relu");
    targetNetwork->AddLayer(128, "linear");
    targetNetwork->AddLayer(numActions);
    targetNetwork->Build();
    targetNetwork->SetLossFunction("mse");
    
    // Initialize target network with qNetwork weights
    targetNetwork->CopyWeightsFrom(*qNetwork);
}

DQNAgent::~DQNAgent() = default;

void DQNAgent::EnableQuantumSampling(std::uint64_t seed) {
    quantumSampler = std::make_unique<QuantumSampler>(seed);
}
void DQNAgent::DisableQuantumSampling() { quantumSampler.reset(); }

Action DQNAgent::SelectAction(const State& state, bool explore) {
    if (explore && (quantumSampler
            ? quantumSampler->DrawDouble() < explorationRate
            : std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) < explorationRate)) {
        // Explore: random action
        int actionId = quantumSampler
            ? quantumSampler->DrawInt(numActions)
            : std::uniform_int_distribution<int>(0, numActions - 1)(rng);
        return Action(actionId, "random");
    } else {
        // Exploit: best action from Q-network
        std::vector<float> qValues = PredictQValues(state);
        const int bestAction = static_cast<int>(std::distance(
            qValues.begin(),
            std::max_element(qValues.begin(), qValues.end())));
        return Action(bestAction, "greedy");
    }
}

void DQNAgent::TrainStep(const Experience& exp) {
    AddExperience(exp);
    TrainFromReplayBuffer();
}

void DQNAgent::TrainFromReplayBuffer() {
    if (replayBuffer.size() < batchSize) {
        return;
    }

    // Sample random batch
    std::vector<size_t> indices(replayBuffer.size());
    for (size_t i = 0; i < indices.size(); i++) indices[i] = i;
    std::shuffle(indices.begin(), indices.end(), rng);

    for (size_t i = 0; i < batchSize; i++) {
        const Experience& exp = replayBuffer[indices[i]];

        // 以 target network 計算 bootstrapped target（標準 DQN 做法：
        // 用 qNetwork 也算得出 target，但每次更新都追著自己會發散）
        std::vector<float> targetVec = qNetwork->Forward(exp.state.features);
        float target = exp.reward;
        if (!exp.done) {
            std::vector<float> nextQValues = PredictTargetQValues(exp.nextState);
            if (doubleDQN) {
                // Double DQN：動作選擇用線上網路、價值評估用 target 網路，
                // 拆掉 max 操作符的過估路徑
                std::vector<float> onlineQ = PredictQValues(exp.nextState);
                int aStar = static_cast<int>(std::distance(
                    onlineQ.begin(), std::max_element(onlineQ.begin(), onlineQ.end())));
                target += discountFactor * nextQValues[aStar];
            } else {
                target += discountFactor * *std::max_element(nextQValues.begin(),
                                                             nextQValues.end());
            }
        }
        targetVec[exp.action.id] = target;

        // 只把被執行動作的 Q 值往 target 拉，其餘動作的輸出維持不變
        qNetwork->TrainStep(exp.state.features, targetVec, learningRate);
    }
}

void DQNAgent::AddExperience(const Experience& exp) {
    replayBuffer.push_back(exp);
    
    if (replayBuffer.size() > maxReplayBufferSize) {
        replayBuffer.erase(replayBuffer.begin());
    }
}

void DQNAgent::DecayLearningRate(float decay) {
    learningRate *= decay;
}

void DQNAgent::DecayExplorationRate(float decay) {
    explorationRate *= decay;
}

void DQNAgent::UpdateTargetNetwork() {
    // Copy weights from qNetwork to targetNetwork
    // This is a simplified version
    targetNetwork->CopyWeightsFrom(*qNetwork);
}

float DQNAgent::PredictQValue(const State& state, int action) {
    std::vector<float> qValues = PredictQValues(state);
    return qValues[action];
}

std::vector<float> DQNAgent::PredictQValues(const State& state) {
    return qNetwork->Forward(state.features);
}

std::vector<float> DQNAgent::PredictTargetQValues(const State& state) {
    return targetNetwork->Forward(state.features);
}

std::string DQNAgent::Serialize() const {
    std::stringstream ss;
    // v2 比 v1 多一行 double 旗標；v1 檔案讀取時旗標為 false
    ss << "PDQNv2\n";
    ss << stateSize << " " << numActions << "\n";
    ss << learningRate << " " << discountFactor << " " << explorationRate
       << " " << (doubleDQN ? 1 : 0) << "\n";
    ss << qNetwork->Serialize();
    return ss.str();
}

bool DQNAgent::Deserialize(const std::string& data) {
    std::stringstream ss(data);

    std::string tok;
    ss >> tok;
    if (tok != "PDQNv1" && tok != "PDQNv2") return false;
    bool v2 = (tok == "PDQNv2");

    ss >> stateSize >> numActions;
    if (v2) {
        int dd = 0;
        ss >> learningRate >> discountFactor >> explorationRate >> dd;
        doubleDQN = (dd != 0);
    } else {
        ss >> learningRate >> discountFactor >> explorationRate;
        doubleDQN = false;
    }
    if (!ss) return false;

    // >> 停在参數行尾，剩餘內容即內嵌的 PNNv1 blob
    std::string netData((std::istreambuf_iterator<char>(ss)),
                        std::istreambuf_iterator<char>());
    if (!qNetwork->Deserialize(netData)) return false;

    // replay buffer 不落盤；target network 由載入後的 Q-network 重建
    replayBuffer.clear();
    targetNetwork->CopyWeightsFrom(*qNetwork);
    return true;
}

// ============================================================================
// Policy Gradient Agent
// ============================================================================

PolicyGradientAgent::PolicyGradientAgent(int stateSize, int numActions,
                                        float learningRate,
                                        float discountFactor)
    : stateSize(stateSize)
    , numActions(numActions)
    , learningRate(learningRate)
    , discountFactor(discountFactor)
    , rng(std::random_device{}()) {
    
    // softmax 在外層做——輸出端要 raw logits（linear），relu 會截斷負 logit
    policyNetwork = std::make_unique<NeuralNetwork>();
    policyNetwork->AddLayer(stateSize);
    policyNetwork->AddLayer(64, "relu");
    policyNetwork->AddLayer(64, "linear");
    policyNetwork->AddLayer(numActions);
    policyNetwork->Build();
}

PolicyGradientAgent::~PolicyGradientAgent() = default;

void PolicyGradientAgent::EnableQuantumSampling(std::uint64_t seed) {
    quantumSampler = std::make_unique<QuantumSampler>(seed);
}
void PolicyGradientAgent::DisableQuantumSampling() { quantumSampler.reset(); }

Action PolicyGradientAgent::SelectAction(const State& state, bool explore) {
    std::vector<float> probs = GetActionProbabilities(state);

    int actionId;
    if (explore) {
        // Sample action from probability distribution
        if (quantumSampler) {
            actionId = quantumAmpRounds > 0
                ? quantumSampler->DrawAmplified(
                      probs,
                      static_cast<int>(std::distance(
                          probs.begin(),
                          std::max_element(probs.begin(), probs.end()))),
                      quantumAmpRounds)
                : quantumSampler->DrawWeighted(probs);
        } else {
            std::discrete_distribution<int> dist(probs.begin(), probs.end());
            actionId = dist(rng);
        }
    } else {
        actionId = static_cast<int>(std::distance(
            probs.begin(), std::max_element(probs.begin(), probs.end())));
    }
    return Action(actionId, explore ? "policy" : "greedy");
}

std::string PolicyGradientAgent::Serialize() const {
    std::stringstream ss;
    ss << "PPGv1\n";
    ss << stateSize << " " << numActions << "\n";
    ss << learningRate << " " << discountFactor << "\n";
    ss << policyNetwork->Serialize();
    return ss.str();
}

bool PolicyGradientAgent::Deserialize(const std::string& data) {
    std::stringstream ss(data);

    std::string tok;
    ss >> tok;
    if (tok != "PPGv1") return false;

    ss >> stateSize >> numActions;
    ss >> learningRate >> discountFactor;
    if (!ss) return false;

    std::string netData((std::istreambuf_iterator<char>(ss)),
                        std::istreambuf_iterator<char>());
    if (!policyNetwork->Deserialize(netData)) return false;

    trajectory.clear();
    return true;
}

void PolicyGradientAgent::StoreTrajectory(const State& state, const Action& action, float reward) {
    TrajectoryStep step;
    step.state = state;
    step.action = action;
    step.reward = reward;
    trajectory.push_back(step);
}

void PolicyGradientAgent::UpdatePolicy() {
    if (trajectory.empty()) {
        return;
    }

    std::vector<float> returns = ComputeReturns();

    // baseline：returns 去均值除標準差——REINFORCE 降方差的標準做法
    float mean = 0.0f;
    for (float r : returns) mean += r;
    mean /= static_cast<float>(returns.size());
    float var = 0.0f;
    for (float r : returns) var += (r - mean) * (r - mean);
    var /= static_cast<float>(returns.size());
    float invStd = 1.0f / (std::sqrt(var) + 1e-8f);

    // REINFORCE：最大化 Σ adv·log π(a|s) → 最小化 -adv·log π。
    // 對 softmax logits 的梯度：d(-adv·log π_a)/dlogit_j = adv·(π_j - δ_ja)
    for (size_t i = 0; i < trajectory.size(); i++) {
        float adv = (returns[i] - mean) * invStd;

        std::vector<float> logits = policyNetwork->Forward(trajectory[i].state.features);
        std::vector<float> probs = Activation::Softmax(logits);

        std::vector<float> grad(probs.size());
        for (size_t j = 0; j < probs.size(); j++) {
            grad[j] = adv * (probs[j] - (static_cast<int>(j) == trajectory[i].action.id ? 1.0f : 0.0f));
        }
        policyNetwork->BackwardFromOutput(trajectory[i].state.features, grad, learningRate);
    }

    ClearTrajectory();
}

void PolicyGradientAgent::ClearTrajectory() {
    trajectory.clear();
}

std::vector<float> PolicyGradientAgent::ComputeReturns() {
    std::vector<float> returns(trajectory.size());
    float runningReturn = 0.0f;
    
    for (int i = static_cast<int>(trajectory.size()) - 1; i >= 0; i--) {
        runningReturn = trajectory[i].reward + discountFactor * runningReturn;
        returns[i] = runningReturn;
    }
    
    return returns;
}

std::vector<float> PolicyGradientAgent::GetActionProbabilities(const State& state) {
    std::vector<float> logits = policyNetwork->Forward(state.features);
    return Activation::Softmax(logits);
}

// ============================================================================
// Actor-Critic Agent
// ============================================================================

ActorCriticAgent::ActorCriticAgent(int stateSize, int numActions,
                                  float actorLearningRate,
                                  float criticLearningRate,
                                  float discountFactor)
    : stateSize(stateSize)
    , numActions(numActions)
    , actorLearningRate(actorLearningRate)
    , criticLearningRate(criticLearningRate)
    , discountFactor(discountFactor)
    , rng(std::random_device{}()) {
    
    // actor 輸出 raw logits（外層 softmax）；critic 輸出 V(s) 可為負——皆需 linear 輸出
    actorNetwork = std::make_unique<NeuralNetwork>();
    actorNetwork->AddLayer(stateSize);
    actorNetwork->AddLayer(64, "relu");
    actorNetwork->AddLayer(64, "linear");
    actorNetwork->AddLayer(numActions);
    actorNetwork->Build();

    criticNetwork = std::make_unique<NeuralNetwork>();
    criticNetwork->AddLayer(stateSize);
    criticNetwork->AddLayer(64, "relu");
    criticNetwork->AddLayer(64, "linear");
    criticNetwork->AddLayer(1);
    criticNetwork->Build();
}

ActorCriticAgent::~ActorCriticAgent() = default;

void ActorCriticAgent::EnableQuantumSampling(std::uint64_t seed) {
    quantumSampler = std::make_unique<QuantumSampler>(seed);
}
void ActorCriticAgent::DisableQuantumSampling() { quantumSampler.reset(); }

Action ActorCriticAgent::SelectAction(const State& state, bool explore) {
    std::vector<float> probs = GetActionProbabilities(state);

    int actionId;
    if (explore) {
        if (quantumSampler) {
            actionId = quantumAmpRounds > 0
                ? quantumSampler->DrawAmplified(
                      probs,
                      static_cast<int>(std::distance(
                          probs.begin(),
                          std::max_element(probs.begin(), probs.end()))),
                      quantumAmpRounds)
                : quantumSampler->DrawWeighted(probs);
        } else {
            std::discrete_distribution<int> dist(probs.begin(), probs.end());
            actionId = dist(rng);
        }
    } else {
        actionId = static_cast<int>(std::distance(
            probs.begin(), std::max_element(probs.begin(), probs.end())));
    }
    return Action(actionId, explore ? "actor_critic" : "greedy");
}

std::string ActorCriticAgent::Serialize() const {
    std::stringstream ss;
    std::string actorBlob = actorNetwork->Serialize();
    std::string criticBlob = criticNetwork->Serialize();

    // 兩段 blob 以長度前綴分隔（rest-of-stream 只能用在最後一段）
    ss << "PACv1\n";
    ss << stateSize << " " << numActions << "\n";
    ss << actorLearningRate << " " << criticLearningRate << " " << discountFactor << "\n";
    ss << actorBlob.size() << "\n" << actorBlob;
    ss << criticBlob.size() << "\n" << criticBlob;
    return ss.str();
}

bool ActorCriticAgent::Deserialize(const std::string& data) {
    std::stringstream ss(data);

    std::string tok;
    ss >> tok;
    if (tok != "PACv1") return false;

    ss >> stateSize >> numActions;
    ss >> actorLearningRate >> criticLearningRate >> discountFactor;
    if (!ss) return false;

    auto readBlob = [&ss](std::string& out) -> bool {
        size_t len = 0;
        ss >> len;
        ss.get();  // 吃掉長度後的換行
        if (!ss || len == 0 || len > (64u << 20)) return false;
        out.resize(len);
        ss.read(out.data(), static_cast<std::streamsize>(len));
        return ss.good() || ss.eof();
    };

    std::string actorBlob, criticBlob;
    if (!readBlob(actorBlob) || !readBlob(criticBlob)) return false;
    if (!actorNetwork->Deserialize(actorBlob)) return false;
    if (!criticNetwork->Deserialize(criticBlob)) return false;
    return true;
}

void ActorCriticAgent::TrainStep(const State& state, const Action& action,
                                 float reward, const State& nextState, bool done) {
    // TD error = r + γV(s') - V(s)，兼作 critic target 與 actor advantage
    float value = GetValue(state);
    float nextValue = done ? 0.0f : GetValue(nextState);
    float tdTarget = reward + discountFactor * nextValue;
    float tdError = tdTarget - value;

    // critic：MSE 往 TD target 拉
    criticNetwork->TrainStep(state.features, {tdTarget}, criticLearningRate);

    // actor：advantage-weighted policy gradient
    // d(-A·log π_a)/dlogit_j = A·(π_j - δ_ja)
    std::vector<float> probs = GetActionProbabilities(state);
    std::vector<float> grad(probs.size());
    for (size_t j = 0; j < probs.size(); j++) {
        grad[j] = tdError * (probs[j] - (static_cast<int>(j) == action.id ? 1.0f : 0.0f));
    }
    actorNetwork->BackwardFromOutput(state.features, grad, actorLearningRate);
}

std::vector<float> ActorCriticAgent::GetActionProbabilities(const State& state) {
    std::vector<float> logits = actorNetwork->Forward(state.features);
    return Activation::Softmax(logits);
}

float ActorCriticAgent::GetValue(const State& state) {
    std::vector<float> value = criticNetwork->Forward(state.features);
    return value[0];
}

// ============================================================================
// Multi-Armed Bandit
// ============================================================================

MultiArmedBandit::MultiArmedBandit(int numArms, float explorationRate)
    : numArms(numArms)
    , explorationRate(explorationRate)
    , rng(std::random_device{}()) {
    
    values.resize(numArms, 0.0f);
    counts.resize(numArms, 0);
}

void MultiArmedBandit::EnableQuantumSampling(std::uint64_t seed) {
    quantumSampler = std::make_unique<QuantumSampler>(seed);
}
void MultiArmedBandit::DisableQuantumSampling() { quantumSampler.reset(); }

int MultiArmedBandit::SelectArm() {
    if (quantumSampler
            ? quantumSampler->DrawDouble() < explorationRate
            : std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) < explorationRate) {
        return quantumSampler
            ? quantumSampler->DrawInt(numArms)
            : std::uniform_int_distribution<int>(0, numArms - 1)(rng);
    } else {
        int bestArm = 0;
        float bestValue = values[0];
        for (int i = 1; i < numArms; i++) {
            if (counts[i] > 0 && values[i] > bestValue) {
                bestValue = values[i];
                bestArm = i;
            }
        }
        return bestArm;
    }
}

void MultiArmedBandit::Update(int arm, float reward) {
    counts[arm]++;
    float n = static_cast<float>(counts[arm]);
    values[arm] = values[arm] + (reward - values[arm]) / n;
}

// ============================================================================
// Grid World Environment
// ============================================================================

GridWorldEnvironment::GridWorldEnvironment(int width, int height,
                                         const std::pair<int, int>& goal,
                                         const std::vector<std::pair<int, int>>& obstacles)
    : width(width)
    , height(height)
    , goal(goal)
    , obstacles(obstacles) {
    
    // Initialize actions
    actions = {
        Action(0, "up"),
        Action(1, "down"),
        Action(2, "left"),
        Action(3, "right")
    };
}

State GridWorldEnvironment::Reset() {
    currentPosition = {0, 0};
    std::fill(collected.begin(), collected.end(), false);

    State state;
    state.features = {static_cast<float>(currentPosition.first),
                     static_cast<float>(currentPosition.second)};
    state.description = "Reset to (0,0)";
    return state;
}

RLEnvironment::StepResult GridWorldEnvironment::Step(const Action& action) {
    StepResult result;
    
    int dx = 0, dy = 0;
    switch (action.id) {
        case 0: dy = -1; break; // up
        case 1: dy = 1; break;  // down
        case 2: dx = -1; break; // left
        case 3: dx = 1; break;  // right
    }
    
    int newX = currentPosition.first + dx;
    int newY = currentPosition.second + dy;
    
    if (IsValidPosition(newX, newY) && !IsObstacle(newX, newY)) {
        currentPosition = {newX, newY};
    }

    result.nextState.features = {static_cast<float>(currentPosition.first),
                                 static_cast<float>(currentPosition.second)};

    // 收集：踩到未收集的 pickup 標記並給 bonus
    float pickupBonus = 0.0f;
    for (size_t i = 0; i < pickups.size(); i++) {
        if (!collected[i] && pickups[i] == currentPosition) {
            collected[i] = true;
            pickupBonus = 0.3f;
            break;
        }
    }

    // 有未收道具時踩到終點不算完成
    if (IsGoal(currentPosition.first, currentPosition.second) &&
        GetRemainingPickups() == 0) {
        result.reward = 1.0f + pickupBonus;
        result.done = true;
        result.info = "Reached goal!";
    } else {
        result.reward = -0.01f + pickupBonus;
        result.done = false;
        result.info = "Continuing...";
    }

    return result;
}

void GridWorldEnvironment::SetPickups(const std::vector<std::pair<int, int>>& p) {
    pickups = p;
    collected.assign(pickups.size(), false);
}

int GridWorldEnvironment::GetRemainingPickups() const {
    int n = 0;
    for (bool c : collected) n += c ? 0 : 1;
    return n;
}

std::vector<Action> GridWorldEnvironment::GetActionSpace() const {
    return actions;
}

int GridWorldEnvironment::GetStateSize() const {
    return 2; // x, y position
}

State GridWorldEnvironment::GetFeatureState() const {
    int x = currentPosition.first;
    int y = currentPosition.second;
    float inv = 1.0f / static_cast<float>(width - 1);

    auto blocked = [&](int bx, int by) {
        if (bx < 0 || by < 0 || bx >= width || by >= height) return 1.0f;
        return IsObstacle(bx, by) ? 1.0f : 0.0f;
    };

    // 回推直線牆參數：所有障礙 x 相同 → 垂直牆（wallPos 是共同 x），
    // 否則視為水平牆。缺口 = 牆線上未被覆蓋的格子。
    // 非直線牆的任意障礙集合會退化為近似值，特徵仍有參考價值
    float wallPosF = 0.0f, gapF = 0.0f, vertF = 0.0f;
    if (!obstacles.empty()) {
        bool vertical = true;
        for (const auto& o : obstacles) {
            if (o.first != obstacles.front().first) {
                vertical = false;
                break;
            }
        }
        vertF = vertical ? 1.0f : 0.0f;
        int wallPos = vertical ? obstacles.front().first
                               : obstacles.front().second;
        wallPosF = wallPos * inv;

        std::vector<bool> covered(static_cast<size_t>(width), false);
        for (const auto& o : obstacles) {
            int along = vertical ? o.second : o.first;
            if (along >= 0 && along < width) covered[along] = true;
        }
        for (int i = 0; i < width; i++) {
            if (!covered[i]) {
                gapF = i * inv;
                break;
            }
        }
    }

    State s;
    s.features = {x * inv, y * inv,
                  (goal.first - x) * inv, (goal.second - y) * inv,
                  blocked(x, y - 1), blocked(x, y + 1),
                  blocked(x - 1, y), blocked(x + 1, y),
                  wallPosF, gapF, vertF};

    // 收集任務追加 3 維：最近未收道具的相對向量 + 剩餘比例。
    // 全收完時向量歸零——agent 轉而追 goal 分量
    if (!pickups.empty()) {
        int best = -1, bestDist = 1 << 30;
        for (size_t i = 0; i < pickups.size(); i++) {
            if (collected[i]) continue;
            int d = std::abs(pickups[i].first - x) + std::abs(pickups[i].second - y);
            if (d < bestDist) { bestDist = d; best = static_cast<int>(i); }
        }
        float pdx = 0.0f, pdy = 0.0f;
        if (best >= 0) {
            pdx = (pickups[best].first - x) * inv;
            pdy = (pickups[best].second - y) * inv;
        }
        s.features.push_back(pdx);
        s.features.push_back(pdy);
        s.features.push_back(static_cast<float>(GetRemainingPickups()) /
                             static_cast<float>(pickups.size()));
    }
    return s;
}

void GridWorldEnvironment::Render() {
    std::cout << "Grid World (" << width << "x" << height << "):\n";
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if (currentPosition == std::make_pair(x, y)) {
                std::cout << "A ";
            } else if (goal == std::make_pair(x, y)) {
                std::cout << "G ";
            } else if (IsObstacle(x, y)) {
                std::cout << "# ";
            } else {
                bool isPickup = false;
                for (size_t i = 0; i < pickups.size(); i++) {
                    if (!collected[i] && pickups[i] == std::make_pair(x, y)) {
                        isPickup = true;
                        break;
                    }
                }
                std::cout << (isPickup ? "o " : ". ");
            }
        }
        std::cout << "\n";
    }
    std::cout << "\n";
}

bool GridWorldEnvironment::IsObstacle(int x, int y) const {
    for (const auto& obs : obstacles) {
        if (obs == std::make_pair(x, y)) {
            return true;
        }
    }
    return false;
}

bool GridWorldEnvironment::IsGoal(int x, int y) const {
    return goal == std::make_pair(x, y);
}

bool GridWorldEnvironment::IsValidPosition(int x, int y) const {
    return x >= 0 && x < width && y >= 0 && y < height;
}

// ============================================================================
// RL Training Session
// ============================================================================

RLTrainingSession::RLTrainingSession() {
}

void RLTrainingSession::Train(RLEnvironment& env,
                              std::function<Action(const State&)> policy,
                              int numEpisodes,
                              int maxStepsPerEpisode) {
    episodeRewards.resize(numEpisodes);
    episodeLengths.resize(numEpisodes);
    
    for (int episode = 0; episode < numEpisodes; episode++) {
        State state = env.Reset();
        float totalReward = 0.0f;
        int steps = 0;
        
        for (int step = 0; step < maxStepsPerEpisode; step++) {
            Action action = policy(state);
            auto result = env.Step(action);
            
            totalReward += result.reward;
            state = result.nextState;
            steps++;
            
            if (result.done) {
                break;
            }
        }
        
        episodeRewards[episode] = totalReward;
        episodeLengths[episode] = steps;
        
        if (episode % 100 == 0) {
            std::cout << "Episode " << episode << ", Reward: " << totalReward << "\n";
        }
    }
}

float RLTrainingSession::Evaluate(RLEnvironment& env,
                                   std::function<Action(const State&)> policy,
                                   int numEpisodes,
                                   int maxStepsPerEpisode) {
    float totalReward = 0.0f;
    
    for (int episode = 0; episode < numEpisodes; episode++) {
        State state = env.Reset();
        
        for (int step = 0; step < maxStepsPerEpisode; step++) {
            Action action = policy(state);
            auto result = env.Step(action);
            
            totalReward += result.reward;
            state = result.nextState;
            
            if (result.done) {
                break;
            }
        }
    }
    
    return totalReward / static_cast<float>(numEpisodes);
}

// ============================================================================
// Reward Shaping
// ============================================================================

float RewardShaper::PotentialBasedShaping(float reward,
                                           const State& state,
                                           const State& nextState,
                                           std::function<float(const State&)> potential,
                                           float gamma) {
    float shapedReward = reward + gamma * potential(nextState) - potential(state);
    return shapedReward;
}

float RewardShaper::SparseToDense(float reward, int steps, float shapingWeight) {
    return reward - shapingWeight * static_cast<float>(steps);
}

float RewardShaper::CurriculumScaling(float reward,
                                       float difficulty,
                                       float minReward,
                                       float maxReward) {
    float scaled = reward * difficulty;
    return std::min(maxReward, std::max(minReward, scaled));
}

// ============================================================================
// Exploration Strategies
// ============================================================================

Action Exploration::EpsilonGreedy(const std::vector<Action>& actions,
                                  const std::vector<float>& values,
                                  float epsilon,
                                  std::mt19937& rng) {
    // Only the first `n` paired entries participate — a shorter
    // values vector must never index past its end.
    const size_t n = std::min(actions.size(), values.size());
    if (n == 0) return Action(-1, "invalid");

    if (std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) < epsilon) {
        return actions[std::uniform_int_distribution<int>(
            0, static_cast<int>(n) - 1)(rng)];
    }
    const int bestIdx = static_cast<int>(std::distance(
        values.begin(), std::max_element(values.begin(),
                                         values.begin() + n)));
    return actions[bestIdx];
}

Action Exploration::UCB(const std::vector<Action>& actions,
                        const std::vector<float>& values,
                        const std::vector<int>& counts,
                        float c,
                        int totalSteps) {
    const size_t n = std::min(actions.size(),
                              std::min(values.size(), counts.size()));
    if (n == 0) return Action(-1, "invalid");
    if (totalSteps < 0) totalSteps = 0;

    int bestIdx = 0;
    float bestUCB = -std::numeric_limits<float>::infinity();

    for (size_t i = 0; i < n; i++) {
        float ucb;
        if (counts[i] == 0) {
            ucb = std::numeric_limits<float>::infinity();
        } else {
            ucb = values[i] + c * std::sqrt(std::log(static_cast<float>(totalSteps + 1)) / counts[i]);
        }

        if (ucb > bestUCB) {
            bestUCB = ucb;
            bestIdx = static_cast<int>(i);
        }
    }

    return actions[bestIdx];
}

Action Exploration::Boltzmann(const std::vector<Action>& actions,
                              const std::vector<float>& values,
                              float temperature,
                              std::mt19937& rng) {
    const size_t n = std::min(actions.size(), values.size());
    if (n == 0) return Action(-1, "invalid");

    // temperature <= 0 collapses to greedy — no stream consumed
    if (temperature <= 0.0f) {
        const int bestIdx = static_cast<int>(std::distance(
            values.begin(), std::max_element(values.begin(),
                                             values.begin() + n)));
        return actions[bestIdx];
    }

    // Softmax with max-subtraction so large values can't overflow exp
    const float maxValue = *std::max_element(values.begin(),
                                             values.begin() + n);
    std::vector<float> probs(n);
    float sum = 0.0f;
    for (size_t i = 0; i < n; i++) {
        probs[i] = std::exp((values[i] - maxValue) / temperature);
        sum += probs[i];
    }
    if (!(sum > 0.0f)) return Action(-1, "invalid");
    for (size_t i = 0; i < n; i++) probs[i] /= sum;

    std::discrete_distribution<int> dist(probs.begin(), probs.end());
    return actions[dist(rng)];
}

// ============================================================================
// Curriculum Learning
// ============================================================================

CurriculumLearning::CurriculumLearning()
    : currentStage(0) {
}

void CurriculumLearning::AddStage(const Stage& stage) {
    stages.push_back(stage);
}

void CurriculumLearning::Train(RLEnvironment& env,
                               std::function<Action(const State&)> policy) {
    for (size_t i = 0; i < stages.size(); i++) {
        const Stage& stage = stages[i];
        currentStage = i;
        
        std::cout << "Starting stage: " << stage.name << " (difficulty: " << stage.difficulty << ")\n";
        
        // Setup environment for this stage
        if (stage.setup) {
            stage.setup(env);
        }
        
        // Train for this stage's episodes
        // Would need actual training loop here
        
        std::cout << "Completed stage: " << stage.name << "\n";
    }
}

// ============================================================================
// PPO Agent
// ============================================================================

PPOAgent::PPOAgent(int stateSize, int numActions,
                   float actorLearningRate,
                   float criticLearningRate,
                   float discountFactor,
                   float clipEpsilon,
                   float gaeLambda,
                   int updateEpochs)
    : stateSize(stateSize)
    , numActions(numActions)
    , actorLearningRate(actorLearningRate)
    , criticLearningRate(criticLearningRate)
    , discountFactor(discountFactor)
    , clipEpsilon(clipEpsilon)
    , gaeLambda(gaeLambda)
    , updateEpochs(updateEpochs) {

    // actor 出 raw logits（外層 softmax）；critic 出純量 V——皆 linear 收尾
    actorNetwork = std::make_unique<NeuralNetwork>();
    actorNetwork->AddLayer(stateSize);
    actorNetwork->AddLayer(64, "relu");
    actorNetwork->AddLayer(64, "linear");
    actorNetwork->AddLayer(numActions);
    actorNetwork->Build();

    criticNetwork = std::make_unique<NeuralNetwork>();
    criticNetwork->AddLayer(stateSize);
    criticNetwork->AddLayer(64, "relu");
    criticNetwork->AddLayer(64, "linear");
    criticNetwork->AddLayer(1);
    criticNetwork->Build();
    criticNetwork->SetLossFunction("huber");  // 回歸 V：Huber 抗離群
}

PPOAgent::~PPOAgent() = default;

void PPOAgent::EnableQuantumSampling(std::uint64_t seed) {
    quantumSampler = std::make_unique<QuantumSampler>(seed);
}
void PPOAgent::DisableQuantumSampling() { quantumSampler.reset(); }

Action PPOAgent::SelectAction(const State& state, bool explore) {
    std::vector<float> probs = GetProbabilities(state);
    int actionId;
    if (explore) {
        if (quantumSampler) {
            actionId = quantumAmpRounds > 0
                ? quantumSampler->DrawAmplified(
                      probs,
                      static_cast<int>(std::distance(
                          probs.begin(),
                          std::max_element(probs.begin(), probs.end()))),
                      quantumAmpRounds)
                : quantumSampler->DrawWeighted(probs);
        } else {
            std::discrete_distribution<int> dist(probs.begin(), probs.end());
            actionId = dist(rng);
        }
    } else {
        actionId = static_cast<int>(std::distance(
            probs.begin(), std::max_element(probs.begin(), probs.end())));
    }
    return Action(actionId, explore ? "ppo" : "greedy");
}

void PPOAgent::StoreTransition(const State& state, const Action& action,
                               float reward, bool done) {
    std::vector<float> probs = GetProbabilities(state);
    float p = probs[std::min<int>(action.id, numActions - 1)];
    Transition t;
    t.state = state;
    t.action = action;
    t.reward = reward;
    t.logProb = std::log(p + 1e-8f);
    t.value = GetStateValue(state);
    t.done = done;
    buffer.push_back(t);
}

std::vector<float> PPOAgent::ComputeGAE() const {
    std::vector<float> adv(buffer.size(), 0.0f);
    float gae = 0.0f;
    for (int i = static_cast<int>(buffer.size()) - 1; i >= 0; i--) {
        float nextV = buffer[i].done ? 0.0f
            : ((i + 1 < static_cast<int>(buffer.size())) ? buffer[i + 1].value : 0.0f);
        float delta = buffer[i].reward + discountFactor * nextV - buffer[i].value;
        gae = delta + discountFactor * gaeLambda * (buffer[i].done ? 0.0f : gae);
        adv[i] = gae;
    }
    return adv;
}

void PPOAgent::Update() {
    if (buffer.empty()) return;

    std::vector<float> adv = ComputeGAE();
    // advantage 標準化——PPO 穩定性的關鍵之一
    float mean = 0.0f;
    for (float a : adv) mean += a;
    mean /= static_cast<float>(adv.size());
    float var = 0.0f;
    for (float a : adv) var += (a - mean) * (a - mean);
    var /= static_cast<float>(adv.size());
    float invStd = 1.0f / (std::sqrt(var) + 1e-8f);

    // critic target = advantage + V_old（GAE 的等價回傳形式）
    for (int epoch = 0; epoch < updateEpochs; epoch++) {
        for (size_t i = 0; i < buffer.size(); i++) {
            const Transition& t = buffer[i];
            float a = (adv[i] - mean) * invStd;

            // critic：V(s) → A + V_old（即 discounted return 的 GAE 估計）
            criticNetwork->TrainStep(t.state.features,
                                     {a + t.value}, criticLearningRate);

            // actor：clipped surrogate——ratio 超過 [1±ε] 時梯度截斷
            std::vector<float> probs = GetProbabilities(t.state);
            float p = probs[std::min<int>(t.action.id, numActions - 1)];
            float ratio = std::exp(std::log(p + 1e-8f) - t.logProb);
            bool clipped = (a > 0.0f && ratio > 1.0f + clipEpsilon) ||
                           (a < 0.0f && ratio < 1.0f - clipEpsilon);
            if (clipped) continue;  // clip 外梯度為 0

            std::vector<float> grad(probs.size());
            for (size_t j = 0; j < probs.size(); j++) {
                grad[j] = a * (probs[j] -
                    (static_cast<int>(j) == t.action.id ? 1.0f : 0.0f));
            }
            actorNetwork->BackwardFromOutput(t.state.features, grad,
                                             actorLearningRate);
        }
    }
    buffer.clear();
}

std::vector<float> PPOAgent::GetProbabilities(const State& state) {
    return Activation::Softmax(actorNetwork->Forward(state.features));
}

float PPOAgent::GetStateValue(const State& state) {
    return criticNetwork->Forward(state.features)[0];
}

std::string PPOAgent::Serialize() const {
    std::stringstream ss;
    std::string actorBlob = actorNetwork->Serialize();
    std::string criticBlob = criticNetwork->Serialize();
    ss << "PPPOv1\n";
    ss << stateSize << " " << numActions << "\n";
    ss << actorLearningRate << " " << criticLearningRate << " "
       << discountFactor << " " << clipEpsilon << " " << gaeLambda << "\n";
    ss << actorBlob.size() << "\n" << actorBlob;
    ss << criticBlob.size() << "\n" << criticBlob;
    return ss.str();
}

bool PPOAgent::Deserialize(const std::string& data) {
    std::stringstream ss(data);
    std::string tok;
    ss >> tok;
    if (tok != "PPPOv1") return false;
    ss >> stateSize >> numActions;
    ss >> actorLearningRate >> criticLearningRate >> discountFactor
       >> clipEpsilon >> gaeLambda;
    if (!ss) return false;

    auto readBlob = [&ss](std::string& out) -> bool {
        size_t len = 0;
        ss >> len;
        ss.get();
        if (!ss || len == 0 || len > (64u << 20)) return false;
        out.resize(len);
        ss.read(out.data(), static_cast<std::streamsize>(len));
        return ss.good() || ss.eof();
    };
    std::string actorBlob, criticBlob;
    if (!readBlob(actorBlob) || !readBlob(criticBlob)) return false;
    if (!actorNetwork->Deserialize(actorBlob)) return false;
    if (!criticNetwork->Deserialize(criticBlob)) return false;
    buffer.clear();
    return true;
}

// ============================================================================
// RLTrainingLogger
// ============================================================================

RLTrainingLogger::RLTrainingLogger(const std::string& csvPath) : path(csvPath) {
    rows.push_back("episode,reward,steps,success");
    open = !path.empty();
}

void RLTrainingLogger::LogEpisode(int episode, float reward,
                                  int steps, bool success) {
    if (!open) return;
    std::ostringstream ss;
    ss << episode << "," << reward << "," << steps << "," << (success ? 1 : 0);
    rows.push_back(ss.str());
}

void RLTrainingLogger::Flush() {
    if (!open) return;
    // temp+rename：寫到一半被 kill 不會留下半份 CSV
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f) { open = false; return; }
        for (const auto& r : rows) f << r << "\n";
    }
    std::remove(path.c_str());
    std::rename(tmp.c_str(), path.c_str());
}

// ============================================================================
// RLPolicy（統一推論介面卡）
// ============================================================================

namespace {
// 依檔頭 magic 偵測：PDQN/PPG/PAC/PPPO 有版本字串；
// QL 序列化無 magic（首行是 "numStates numActions"），作 fallback
RLPolicy::Algo DetectAlgo(const std::string& data) {
    if (data.compare(0, 6, "PDQNv1") == 0 || data.compare(0, 6, "PDQNv2") == 0)
        return RLPolicy::Algo::DQN;
    if (data.compare(0, 5, "PPGv1") == 0) return RLPolicy::Algo::PolicyGradient;
    if (data.compare(0, 5, "PACv1") == 0) return RLPolicy::Algo::ActorCritic;
    if (data.compare(0, 6, "PPPOv1") == 0) return RLPolicy::Algo::PPO;
    // QL 裸表：第一行恰為兩個正整數
    std::istringstream ss(data);
    int ns, na;
    std::string rest;
    if (ss >> ns >> na && ns > 0 && na > 0 &&
        ns * static_cast<long long>(na) <= 10'000'000)
        return RLPolicy::Algo::QLearning;
    return RLPolicy::Algo::Unknown;
}
} // anonymous namespace

bool RLPolicy::Load(const std::string& modelPath, Algo hint) {
    std::ifstream f(modelPath, std::ios::binary);
    if (!f) return false;
    std::string data((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
    return LoadFromString(data, hint);
}

bool RLPolicy::LoadFromString(const std::string& data, Algo hint) {
    Algo a = (hint == Algo::Unknown) ? DetectAlgo(data) : hint;
    switch (a) {
        case Algo::QLearning: {
            auto p = std::make_unique<QLearningAgent>(1, 1);
            if (!p->Deserialize(data)) return false;
            ql = std::move(p); break;
        }
        case Algo::DQN: {
            auto p = std::make_unique<DQNAgent>(1, 1);
            if (!p->Deserialize(data)) return false;
            dqn = std::move(p); break;
        }
        case Algo::PolicyGradient: {
            auto p = std::make_unique<PolicyGradientAgent>(1, 1);
            if (!p->Deserialize(data)) return false;
            pg = std::move(p); break;
        }
        case Algo::ActorCritic: {
            auto p = std::make_unique<ActorCriticAgent>(1, 1, 0.001f, 0.001f);
            if (!p->Deserialize(data)) return false;
            ac = std::move(p); break;
        }
        case Algo::PPO: {
            auto p = std::make_unique<PPOAgent>(1, 1);
            if (!p->Deserialize(data)) return false;
            ppo = std::move(p); break;
        }
        default:
            return false;
    }
    algo = a;
    return true;
}

int RLPolicy::GetStateSize() const {
    if (dqn) return dqn->GetStateSize();
    if (pg) return pg->GetStateSize();
    if (ac) return ac->GetStateSize();
    if (ppo) return ppo->GetStateSize();
    return -1;
}

int RLPolicy::SelectTabularAction(int state, bool explore) {
    if (ql) return ql->SelectAction(state, explore);
    return -1;
}

Action RLPolicy::SelectAction(const State& state, bool explore) {
    if (dqn) return dqn->SelectAction(state, explore);
    if (pg) return pg->SelectAction(state, explore);
    if (ac) return ac->SelectAction(state, explore);
    if (ppo) return ppo->SelectAction(state, explore);
    return Action(-1, "no_policy");
}

} // namespace AI
} // namespace Potato
