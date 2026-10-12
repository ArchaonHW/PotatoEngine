/**
 * Potato Engine AI Agent JNI 橋接層
 *
 * 給 Java/Spring 呈現層用的 session-based API：
 *   create → reset → stepAuto(×N) → endEpisode → destroy
 * 訓練/評估的逐格走位由 Java 端驅動迴圈（WebSocket 逐格推播），
 * 每步回傳 agent 的動作估值（Q 值或 policy 機率）供 UI 視覺化。
 *
 * 行為與 Examples/AIAgentTrainer.cpp 對齊：同樣的固定/隨機地圖生成、
 * one-hot 或 11 維 GetFeatureState 觀測編碼、撞牆 reward shaping、
 * DQN target network 每 20 步同步、episode 末 epsilon decay 0.995。
 */

#include <jni.h>

#include "AI/ReinforcementLearning.h"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace Potato::AI;

namespace {

// ---- 與 AIAgentTrainer.cpp 相同的地圖/編碼/shaping 邏輯 ----

std::vector<std::pair<int, int>> MakeObstacles(int size) {
    std::vector<std::pair<int, int>> obstacles;
    int wallX = size / 2;
    for (int y = 0; y < size - 2; y++) {
        obstacles.emplace_back(wallX, y);
    }
    return obstacles;
}

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

std::pair<int, int> RandomGoal(int size,
                               const std::vector<std::pair<int, int>>& obstacles,
                               std::mt19937& rng) {
    std::uniform_int_distribution<int> cell(0, size - 1);
    while (true) {
        std::pair<int, int> g = {cell(rng), cell(rng)};
        if (g == std::make_pair(0, 0)) continue;
        if (std::find(obstacles.begin(), obstacles.end(), g) == obstacles.end())
            return g;
    }
}

int StateIndex(const State& state, int width) {
    int x = static_cast<int>(state.features[0]);
    int y = static_cast<int>(state.features[1]);
    return y * width + x;
}

State EncodeOneHot(const State& state, int width, int height) {
    State encoded;
    encoded.features.assign(static_cast<size_t>(width * height), 0.0f);
    encoded.features[StateIndex(state, width)] = 1.0f;
    return encoded;
}

float ShapeReward(float reward, const State& from, const State& to) {
    if (from.features == to.features) reward -= 0.1f;
    return reward;
}

// ---- Session ----

struct AgentSession {
    std::string algo;  // "qlearning" | "dqn" | "pg" | "ac"
    int width = 0;
    bool randMaps = false;
    int stateSize = 0;
    int totalSteps = 0;

    std::mt19937 mapRng;
    std::unique_ptr<GridWorldEnvironment> env;

    std::unique_ptr<QLearningAgent> ql;
    std::unique_ptr<DQNAgent> dqn;
    std::unique_ptr<PolicyGradientAgent> pg;
    std::unique_ptr<ActorCriticAgent> ac;

    State curRaw;      // env 回傳的 {x,y}
    State curEncoded;  // agent 實際看到的觀測

    State Encode(const State& raw) {
        if (algo == "qlearning") return raw;  // tabular 用 index，不走 features
        return randMaps ? env->GetFeatureState()
                        : EncodeOneHot(raw, width, width);
    }

    void RegenerateEnv() {
        if (randMaps) {
            auto obstacles = MakeRandomObstacles(width, mapRng);
            env = std::make_unique<GridWorldEnvironment>(
                width, width, RandomGoal(width, obstacles, mapRng), obstacles);
        } else {
            env = std::make_unique<GridWorldEnvironment>(
                width, width, std::make_pair(width - 1, width - 1),
                MakeObstacles(width));
        }
    }
};

std::mutex g_mutex;
std::unordered_map<jlong, std::unique_ptr<AgentSession>> g_sessions;
std::atomic<jlong> g_nextId{1};

AgentSession* GetSession(jlong h) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_sessions.find(h);
    return it == g_sessions.end() ? nullptr : it->second.get();
}

bool ReadFile(const std::string& path, std::string& data) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    data = ss.str();
    return true;
}

bool WriteFile(const std::string& path, const std::string& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << data;
    return out.good();
}

std::string JStringToUtf8(JNIEnv* env, jstring s) {
    const char* chars = env->GetStringUTFChars(s, nullptr);
    std::string out(chars);
    env->ReleaseStringUTFChars(s, chars);
    return out;
}

}  // namespace

extern "C" {

// create(algo, gridSize, randMaps, seed) → session handle；<0 = 失敗
JNIEXPORT jlong JNICALL Java_com_potato_agent_jni_NativeAgent_nCreate(
    JNIEnv* env, jclass, jstring jAlgo, jint jGridSize, jboolean jRand,
    jlong jSeed) {
    std::string algo = JStringToUtf8(env, jAlgo);
    if (algo != "qlearning" && algo != "dqn" && algo != "pg" && algo != "ac")
        return -1;
    if (jGridSize < 2) return -2;
    if (jRand == JNI_TRUE && algo == "qlearning") return -3;

    auto s = std::make_unique<AgentSession>();
    s->algo = algo;
    s->width = jGridSize;
    s->randMaps = (jRand == JNI_TRUE);
    s->mapRng.seed(jSeed >= 0 ? static_cast<unsigned int>(jSeed)
                            : std::random_device{}());
    s->RegenerateEnv();
    s->stateSize = s->randMaps ? GridWorldEnvironment::FEATURE_STATE_SIZE
                               : s->width * s->width;

    const int numStates = s->width * s->width;
    const int numActions = 4;
    if (algo == "qlearning") {
        s->ql = std::make_unique<QLearningAgent>(numStates, numActions,
                                                 0.1f, 0.99f, 1.0f);
    } else if (algo == "dqn") {
        s->dqn = std::make_unique<DQNAgent>(s->stateSize, numActions,
                                            0.01f, 0.99f, 1.0f);
    } else if (algo == "pg") {
        s->pg = std::make_unique<PolicyGradientAgent>(s->stateSize, numActions,
                                                      0.01f, 0.99f);
    } else {
        s->ac = std::make_unique<ActorCriticAgent>(s->stateSize, numActions,
                                                   0.01f, 0.01f, 0.99f);
    }
    if (jSeed >= 0) {
        unsigned int seed = static_cast<unsigned int>(jSeed);
        if (s->ql) s->ql->SetSeed(seed);
        if (s->dqn) s->dqn->SetSeed(seed + 1111u);
        if (s->pg) s->pg->SetSeed(seed + 2222u);
        if (s->ac) s->ac->SetSeed(seed + 3333u);
    }

    jlong h = g_nextId.fetch_add(1);
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_sessions.emplace(h, std::move(s));
    }
    return h;
}

JNIEXPORT void JNICALL Java_com_potato_agent_jni_NativeAgent_nDestroy(
    JNIEnv*, jclass, jlong h) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_sessions.erase(h);
}

// reset() → {x, y}；randMaps 模式同時產生新地圖
JNIEXPORT jintArray JNICALL Java_com_potato_agent_jni_NativeAgent_nReset(
    JNIEnv* env, jclass, jlong h) {
    AgentSession* s = GetSession(h);
    if (!s) return nullptr;
    if (s->randMaps) s->RegenerateEnv();
    s->curRaw = s->env->Reset();
    s->curEncoded = s->Encode(s->curRaw);
    jint out[2] = {static_cast<jint>(s->curRaw.features[0]),
                   static_cast<jint>(s->curRaw.features[1])};
    jintArray arr = env->NewIntArray(2);
    env->SetIntArrayRegion(arr, 0, 2, out);
    return arr;
}

// grid() → {w, h, goalX, goalY, nObs, ox1, oy1, ...}
JNIEXPORT jdoubleArray JNICALL Java_com_potato_agent_jni_NativeAgent_nGrid(
    JNIEnv* env, jclass, jlong h) {
    AgentSession* s = GetSession(h);
    if (!s) return nullptr;
    const auto& goal = s->env->GetGoal();
    const auto& obs = s->env->GetObstacles();
    std::vector<jdouble> data = {
        static_cast<jdouble>(s->env->GetWidth()),
        static_cast<jdouble>(s->env->GetHeight()),
        static_cast<jdouble>(goal.first), static_cast<jdouble>(goal.second),
        static_cast<jdouble>(obs.size())};
    for (const auto& o : obs) {
        data.push_back(o.first);
        data.push_back(o.second);
    }
    jdoubleArray arr = env->NewDoubleArray(static_cast<jsize>(data.size()));
    env->SetDoubleArrayRegion(arr, 0, static_cast<jsize>(data.size()),
                              data.data());
    return arr;
}

// 目前觀測下各動作的估值：QL/DQN → Q 值，PG/AC → action 機率 → double[4]
JNIEXPORT jdoubleArray JNICALL Java_com_potato_agent_jni_NativeAgent_nActionValues(
    JNIEnv* env, jclass, jlong h) {
    AgentSession* s = GetSession(h);
    if (!s) return nullptr;
    std::vector<float> v(4, 0.0f);
    if (s->ql) {
        int idx = StateIndex(s->curRaw, s->width);
        for (int a = 0; a < 4; a++) v[a] = s->ql->GetQValue(idx, a);
    } else if (s->dqn) {
        v = s->dqn->GetQValues(s->curEncoded);
    } else if (s->pg) {
        v = s->pg->GetProbabilities(s->curEncoded);
    } else if (s->ac) {
        v = s->ac->GetProbabilities(s->curEncoded);
    }
    jdouble tmp[4] = {v[0], v[1], v[2], v[3]};
    jdoubleArray arr = env->NewDoubleArray(4);
    env->SetDoubleArrayRegion(arr, 0, 4, tmp);
    return arr;
}

// stepAuto(explore, learn)：agent 選動作 → env step → 可選學習更新。
// 回傳 {x, y, rawReward, shapedReward, done, actionId, v0, v1, v2, v3}
JNIEXPORT jdoubleArray JNICALL Java_com_potato_agent_jni_NativeAgent_nStepAuto(
    JNIEnv* env, jclass, jlong h, jboolean jExplore, jboolean jLearn) {
    AgentSession* s = GetSession(h);
    if (!s) return nullptr;
    const bool explore = (jExplore == JNI_TRUE);
    const bool learn = (jLearn == JNI_TRUE);

    Action action(0, "");
    if (s->ql) {
        int idx = StateIndex(s->curRaw, s->width);
        action.id = s->ql->SelectAction(idx, explore);
    } else if (s->dqn) {
        action = s->dqn->SelectAction(s->curEncoded, explore);
    } else if (s->pg) {
        action = s->pg->SelectAction(s->curEncoded, explore);
    } else {
        action = s->ac->SelectAction(s->curEncoded, explore);
    }

    auto result = s->env->Step(action);
    float shaped = ShapeReward(result.reward, s->curRaw, result.nextState);
    State nextEncoded = s->Encode(result.nextState);

    if (learn) {
        if (s->ql) {
            s->ql->Update(StateIndex(s->curRaw, s->width), action.id, shaped,
                          StateIndex(result.nextState, s->width), result.done);
        } else if (s->dqn) {
            Experience exp;
            exp.state = s->curEncoded;
            exp.action = action;
            exp.reward = shaped;
            exp.nextState = nextEncoded;
            exp.done = result.done;
            s->dqn->TrainStep(exp);
            if (++s->totalSteps % 20 == 0) s->dqn->UpdateTargetNetwork();
        } else if (s->pg) {
            s->pg->StoreTrajectory(s->curEncoded, action, shaped);
        } else if (s->ac) {
            s->ac->TrainStep(s->curEncoded, action, shaped, nextEncoded,
                             result.done);
        }
    }

    s->curRaw = result.nextState;
    s->curEncoded = nextEncoded;

    std::vector<float> v(4, 0.0f);
    if (s->ql) {
        int idx = StateIndex(s->curRaw, s->width);
        for (int a = 0; a < 4; a++) v[a] = s->ql->GetQValue(idx, a);
    } else if (s->dqn) {
        v = s->dqn->GetQValues(s->curEncoded);
    } else if (s->pg) {
        v = s->pg->GetProbabilities(s->curEncoded);
    } else if (s->ac) {
        v = s->ac->GetProbabilities(s->curEncoded);
    }

    jdouble out[10] = {
        static_cast<jdouble>(s->curRaw.features[0]),
        static_cast<jdouble>(s->curRaw.features[1]),
        static_cast<jdouble>(result.reward),
        static_cast<jdouble>(shaped),
        result.done ? 1.0 : 0.0,
        static_cast<jdouble>(action.id),
        v[0], v[1], v[2], v[3]};
    jdoubleArray arr = env->NewDoubleArray(10);
    env->SetDoubleArrayRegion(arr, 0, 10, out);
    return arr;
}

// episode 收尾：PG 更新 policy 並清 trajectory；QL/DQN epsilon decay 0.995
JNIEXPORT void JNICALL Java_com_potato_agent_jni_NativeAgent_nEndEpisode(
    JNIEnv*, jclass, jlong h, jboolean jLearn) {
    AgentSession* s = GetSession(h);
    if (!s) return;
    if (jLearn != JNI_TRUE) return;
    if (s->pg) {
        s->pg->UpdatePolicy();
        s->pg->ClearTrajectory();
    }
    if (s->ql) s->ql->DecayExplorationRate(0.995f);
    if (s->dqn) s->dqn->DecayExplorationRate(0.995f);
}

JNIEXPORT void JNICALL Java_com_potato_agent_jni_NativeAgent_nSetExplorationRate(
    JNIEnv*, jclass, jlong h, jdouble rate) {
    AgentSession* s = GetSession(h);
    if (!s) return;
    if (s->ql) s->ql->SetExplorationRate(static_cast<float>(rate));
    if (s->dqn) s->dqn->SetExplorationRate(static_cast<float>(rate));
}

JNIEXPORT jdouble JNICALL Java_com_potato_agent_jni_NativeAgent_nGetExplorationRate(
    JNIEnv*, jclass, jlong h) {
    AgentSession* s = GetSession(h);
    if (!s) return -1.0;
    if (s->ql) return s->ql->GetExplorationRate();
    if (s->dqn) return s->dqn->GetExplorationRate();
    return -1.0;  // PG/AC 無 epsilon
}

JNIEXPORT jboolean JNICALL Java_com_potato_agent_jni_NativeAgent_nSaveModel(
    JNIEnv* env, jclass, jlong h, jstring jPath) {
    AgentSession* s = GetSession(h);
    if (!s) return JNI_FALSE;
    std::string data;
    if (s->ql) data = s->ql->Serialize();
    else if (s->dqn) data = s->dqn->Serialize();
    else if (s->pg) data = s->pg->Serialize();
    else data = s->ac->Serialize();
    return WriteFile(JStringToUtf8(env, jPath), data) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_com_potato_agent_jni_NativeAgent_nLoadModel(
    JNIEnv* env, jclass, jlong h, jstring jPath) {
    AgentSession* s = GetSession(h);
    if (!s) return JNI_FALSE;
    std::string data;
    if (!ReadFile(JStringToUtf8(env, jPath), data)) return JNI_FALSE;
    if (s->ql) return s->ql->Deserialize(data) ? JNI_TRUE : JNI_FALSE;
    if (s->dqn) return s->dqn->Deserialize(data) ? JNI_TRUE : JNI_FALSE;
    if (s->pg) return s->pg->Deserialize(data) ? JNI_TRUE : JNI_FALSE;
    return s->ac->Deserialize(data) ? JNI_TRUE : JNI_FALSE;
}

}  // extern "C"
