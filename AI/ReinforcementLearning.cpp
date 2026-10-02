/**
 * Potato Engine Reinforcement Learning Implementation
 */

#include "ReinforcementLearning.h"
#include "NeuralNetwork.h"
#include <algorithm>
#include <random>
#include <sstream>
#include <iostream>

namespace Potato {
namespace AI {

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

int QLearningAgent::SelectAction(int state, bool explore) {
    if (explore && std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) < explorationRate) {
        // Explore: random action
        return std::uniform_int_distribution<int>(0, numActions - 1)(rng);
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
    std::stringstream ss(data);
    
    ss >> numStates >> numActions;
    ss >> learningRate >> discountFactor >> explorationRate;
    
    qTable.resize(numStates, std::vector<float>(numActions));
    for (auto& row : qTable) {
        for (float& val : row) {
            ss >> val;
        }
    }
    
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
    qNetwork = new NeuralNetwork();
    qNetwork->AddLayer(stateSize);
    qNetwork->AddLayer(128, "relu");
    qNetwork->AddLayer(128, "relu");
    qNetwork->AddLayer(numActions);
    qNetwork->Build();
    qNetwork->SetLossFunction("mse");
    
    targetNetwork = new NeuralNetwork();
    targetNetwork->AddLayer(stateSize);
    targetNetwork->AddLayer(128, "relu");
    targetNetwork->AddLayer(128, "relu");
    targetNetwork->AddLayer(numActions);
    targetNetwork->Build();
    targetNetwork->SetLossFunction("mse");
    
    // Initialize target network with qNetwork weights
    targetNetwork->CopyWeightsFrom(*qNetwork);
}

Action DQNAgent::SelectAction(const State& state, bool explore) {
    if (explore && std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) < explorationRate) {
        // Explore: random action
        int actionId = std::uniform_int_distribution<int>(0, numActions - 1)(rng);
        return Action(actionId, "random");
    } else {
        // Exploit: best action from Q-network
        std::vector<float> qValues = PredictQValues(state);
        int bestAction = std::distance(qValues.begin(), 
                                     std::max_element(qValues.begin(), qValues.end()));
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
        
        // Compute target Q-value
        std::vector<float> nextQValues = PredictQValues(exp.nextState);
        float maxNextQ = *std::max_element(nextQValues.begin(), nextQValues.end());
        float target = exp.done ? exp.reward : exp.reward + discountFactor * maxNextQ;
        
        // Train network (simplified - would need proper target computation)
        // This is a placeholder for full DQN training
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

// ============================================================================
// Policy Gradient Agent
// ============================================================================

PolicyGradientAgent::PolicyGradientAgent(int stateSize, int numActions,
                                        float learningRate,
                                        float discountFactor)
    : stateSize(stateSize)
    , numActions(numActions)
    , learningRate(learningRate)
    , discountFactor(discountFactor) {
    
    policyNetwork = new NeuralNetwork();
    policyNetwork->AddLayer(stateSize);
    policyNetwork->AddLayer(64, "relu");
    policyNetwork->AddLayer(64, "relu");
    policyNetwork->AddLayer(numActions);
    policyNetwork->Build();
}

Action PolicyGradientAgent::SelectAction(const State& state) {
    std::vector<float> probs = GetActionProbabilities(state);
    
    // Sample action from probability distribution
    std::discrete_distribution<int> dist(probs.begin(), probs.end());
    int actionId = dist(rng);
    
    return Action(actionId, "policy");
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
    
    // Update policy using REINFORCE algorithm
    // This is a simplified version
    for (size_t i = 0; i < trajectory.size(); i++) {
        // Update policy network
        // Would need full implementation
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
    , discountFactor(discountFactor) {
    
    actorNetwork = new NeuralNetwork();
    actorNetwork->AddLayer(stateSize);
    actorNetwork->AddLayer(64, "relu");
    actorNetwork->AddLayer(64, "relu");
    actorNetwork->AddLayer(numActions);
    actorNetwork->Build();
    
    criticNetwork = new NeuralNetwork();
    criticNetwork->AddLayer(stateSize);
    criticNetwork->AddLayer(64, "relu");
    criticNetwork->AddLayer(1);
    criticNetwork->Build();
}

Action ActorCriticAgent::SelectAction(const State& state) {
    std::vector<float> probs = GetActionProbabilities(state);
    
    std::discrete_distribution<int> dist(probs.begin(), probs.end());
    int actionId = dist(rng);
    
    return Action(actionId, "actor_critic");
}

void ActorCriticAgent::TrainStep(const State& state, const Action& action,
                                 float reward, const State& nextState, bool done) {
    // Compute TD error
    float value = GetValue(state);
    float nextValue = done ? 0.0f : GetValue(nextState);
    float tdError = reward + discountFactor * nextValue - value;
    
    // Update critic
    // Update actor
    // This is a simplified version
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

int MultiArmedBandit::SelectArm() {
    if (std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) < explorationRate) {
        return std::uniform_int_distribution<int>(0, numArms - 1)(rng);
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
    
    if (IsGoal(currentPosition.first, currentPosition.second)) {
        result.reward = 1.0f;
        result.done = true;
        result.info = "Reached goal!";
    } else {
        result.reward = -0.01f; // Small penalty for each step
        result.done = false;
        result.info = "Continuing...";
    }
    
    return result;
}

std::vector<Action> GridWorldEnvironment::GetActionSpace() const {
    return actions;
}

int GridWorldEnvironment::GetStateSize() const {
    return 2; // x, y position
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
                std::cout << ". ";
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
                                  float epsilon) {
    static std::mt19937 rng(std::random_device{}());
    
    if (std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) < epsilon) {
        return actions[std::uniform_int_distribution<int>(0, actions.size() - 1)(rng)];
    } else {
        int bestIdx = std::distance(values.begin(), 
                                   std::max_element(values.begin(), values.end()));
        return actions[bestIdx];
    }
}

Action Exploration::UCB(const std::vector<Action>& actions,
                        const std::vector<float>& values,
                        const std::vector<int>& counts,
                        float c,
                        int totalSteps) {
    int bestIdx = 0;
    float bestUCB = -std::numeric_limits<float>::infinity();
    
    for (size_t i = 0; i < actions.size(); i++) {
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
                              float temperature) {
    static std::mt19937 rng(std::random_device{}());
    
    // Compute softmax probabilities
    std::vector<float> expValues(values.size());
    float sum = 0.0f;
    
    for (size_t i = 0; i < values.size(); i++) {
        expValues[i] = std::exp(values[i] / temperature);
        sum += expValues[i];
    }
    
    std::vector<float> probs(values.size());
    for (size_t i = 0; i < probs.size(); i++) {
        probs[i] = expValues[i] / sum;
    }
    
    // Sample from distribution
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

} // namespace AI
} // namespace Potato
