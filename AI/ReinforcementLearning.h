/**
 * Potato Engine Reinforcement Learning Module
 * Q-Learning, DQN, and other RL algorithms for AI agents
 */

#pragma once

#include <vector>
#include <unordered_map>
#include <memory>
#include <random>
#include <functional>

namespace Potato {
namespace AI {

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
    
    // Target network update
    void UpdateTargetNetwork();
    
    // Getters
    size_t GetReplayBufferSize() const { return replayBuffer.size(); }
    
private:
    class NeuralNetwork* qNetwork;
    class NeuralNetwork* targetNetwork;
    
    int stateSize;
    int numActions;
    float learningRate;
    float discountFactor;
    float explorationRate;
    
    std::vector<Experience> replayBuffer;
    size_t maxReplayBufferSize;
    size_t batchSize;
    
    std::mt19937 rng;
    
    float PredictQValue(const State& state, int action);
    std::vector<float> PredictQValues(const State& state);
};

/**
 * Policy Gradient Agent (REINFORCE)
 */
class PolicyGradientAgent {
public:
    PolicyGradientAgent(int stateSize, int numActions,
                        float learningRate = 0.001f,
                        float discountFactor = 0.99f);
    
    // Select action based on policy
    Action SelectAction(const State& state);
    
    // Store trajectory
    void StoreTrajectory(const State& state, const Action& action, float reward);
    
    // Update policy from trajectory
    void UpdatePolicy();
    
    // Clear trajectory
    void ClearTrajectory();
    
private:
    class NeuralNetwork* policyNetwork;
    
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
    
    // Select action
    Action SelectAction(const State& state);
    
    // Train step
    void TrainStep(const State& state, const Action& action, 
                   float reward, const State& nextState, bool done);
    
private:
    class NeuralNetwork* actorNetwork;
    class NeuralNetwork* criticNetwork;
    
    int stateSize;
    int numActions;
    float actorLearningRate;
    float criticLearningRate;
    float discountFactor;
    
    std::mt19937 rng;
    
    std::vector<float> GetActionProbabilities(const State& state);
    float GetValue(const State& state);
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
    
private:
    int numArms;
    float explorationRate;
    std::vector<float> values;
    std::vector<int> counts;
    
    std::mt19937 rng;
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
    GridWorldEnvironment(int width, int height, 
                        const std::pair<int, int>& goal,
                        const std::vector<std::pair<int, int>>& obstacles = {});
    
    State Reset() override;
    StepResult Step(const Action& action) override;
    std::vector<Action> GetActionSpace() const override;
    int GetStateSize() const override;
    void Render() override;
    
private:
    int width;
    int height;
    std::pair<int, int> goal;
    std::vector<std::pair<int, int>> obstacles;
    std::pair<int, int> currentPosition;
    std::vector<Action> actions;
    
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
    // Epsilon-greedy
    Action EpsilonGreedy(const std::vector<Action>& actions,
                         const std::vector<float>& values,
                         float epsilon);
    
    // Upper Confidence Bound (UCB)
    Action UCB(const std::vector<Action>& actions,
               const std::vector<float>& values,
               const std::vector<int>& counts,
               float c = 1.0f,
               int totalSteps = 0);
    
    // Boltzmann (softmax) exploration
    Action Boltzmann(const std::vector<Action>& actions,
                     const std::vector<float>& values,
                     float temperature = 1.0f);
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
