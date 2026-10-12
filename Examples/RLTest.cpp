/**
 * RLTest - AI/ReinforcementLearning 測試（headless）
 *
 * 覆蓋：
 *   [環境] GridWorld 基本性質：Reset、撞牆不動、到達目標 done+reward、
 *          泛化訓練用 getter（GetGoal/GetObstacles/GetWidth/GetHeight）
 *   [QL]   Update → Serialize → Deserialize round-trip，Q 值與貪婪動作一致
 *   [DQN]  replay 訓練數步後 round-trip，GetQValues 逐元素一致
 *   [PG]   trajectory + UpdatePolicy 後 round-trip，GetProbabilities 一致
 *   [AC]   TrainStep 後 round-trip，GetProbabilities + GetStateValue 一致
 *   [容錯] Deserialize 餵垃圾資料一律回傳 false
 */

#include "ReinforcementLearning.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace Potato::AI;

namespace {

int g_pass = 0;
int g_fail = 0;

void Expect(bool cond, const char* name, const std::string& detail = "") {
    if (cond) {
        g_pass++;
        printf("  [PASS] %s%s%s\n", name,
               detail.empty() ? "" : "  -- ", detail.c_str());
    } else {
        g_fail++;
        printf("  [FAIL] %s%s%s\n", name,
               detail.empty() ? "" : "  -- ", detail.c_str());
    }
}

bool VecNear(const std::vector<float>& a, const std::vector<float>& b,
             float eps = 1e-5f) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        if (std::fabs(a[i] - b[i]) > eps) return false;
    }
    return true;
}

// 讓神經網路 agent 的權重偏離初始值（序列化才有可驗證內容）
Experience MakeExp(int s, int action, float reward, bool done) {
    Experience e;
    e.state.features = {s * 0.25f, s * -0.125f};
    e.action = Action(action, "");
    e.reward = reward;
    e.nextState.features = {(s + 1) * 0.25f, (s + 1) * -0.125f};
    e.done = done;
    return e;
}

} // anonymous namespace

int main() {
    printf("=== ReinforcementLearning 測試 ===\n\n");

    // ---- [GridWorld 環境] ----
    printf("-- GridWorld --\n");
    {
        // 牆在 x=2（y<4），目標 (4,4)
        std::vector<std::pair<int, int>> obstacles;
        for (int y = 0; y < 4; y++) obstacles.emplace_back(2, y);
        GridWorldEnvironment env(5, 5, {4, 4}, obstacles);

        Expect(env.GetWidth() == 5 && env.GetHeight() == 5, "寬高 getter");
        Expect(env.GetGoal() == std::make_pair(4, 4), "GetGoal");
        Expect(env.GetObstacles().size() == 4, "GetObstacles");
        Expect(env.GetActionSpace().size() == 4, "四個動作");

        State s = env.Reset();
        Expect(s.features == std::vector<float>({0.0f, 0.0f}),
               "Reset 回起點 (0,0)");

        // 往右撞牆（(2,0) 是障礙前要先到 (1,0)）：(0,0)→右→(1,0)→右→撞牆
        env.Step(Action(3, ""));
        auto r = env.Step(Action(3, ""));
        Expect(r.nextState.features == std::vector<float>({1.0f, 0.0f}),
               "撞牆原地不動");
        Expect(!r.done, "撞牆未完成");
        Expect(r.reward < 0.0f, "撞牆負 reward");

        // 邊界：往上出界也原地不動
        r = env.Step(Action(0, ""));
        Expect(r.nextState.features == std::vector<float>({1.0f, 0.0f}),
               "出界原地不動");

        // 無牆環境直達目標檢查 done+reward
        GridWorldEnvironment open(3, 3, {2, 0});
        open.Reset();
        open.Step(Action(3, ""));
        r = open.Step(Action(3, ""));
        Expect(r.done && r.reward == 1.0f, "到達目標 done+reward=1");
    }

    // ---- [QLearning round-trip] ----
    printf("-- QLearning --\n");
    {
        QLearningAgent agent(36, 4, 0.1f, 0.99f);
        for (int i = 0; i < 50; i++) {
            agent.Update(i % 36, i % 4, (i % 5) * 0.4f - 0.5f,
                         (i + 1) % 36, i % 7 == 0);
        }
        std::string blob = agent.Serialize();
        Expect(!blob.empty(), "Serialize 非空");

        QLearningAgent copy(36, 4);
        Expect(copy.Deserialize(blob), "Deserialize 成功");

        bool same = true;
        for (int s = 0; s < 36; s++) {
            for (int a = 0; a < 4; a++) {
                if (std::fabs(agent.GetQValue(s, a) - copy.GetQValue(s, a)) > 1e-6f)
                    same = false;
            }
        }
        Expect(same, "Q-table 全部一致");
        Expect(agent.SelectAction(5, false) == copy.SelectAction(5, false),
               "貪婪動作一致");
        Expect(std::fabs(agent.GetExplorationRate() -
                         copy.GetExplorationRate()) < 1e-6f,
               "探索率隨模型保存");
    }

    // ---- [DQN round-trip] ----
    printf("-- DQN --\n");
    {
        DQNAgent agent(2, 4, 0.01f, 0.99f, 1.0f, 64, 8);
        for (int i = 0; i < 80; i++) {
            agent.TrainStep(MakeExp(i % 8, i % 4, (i % 5) * 0.4f - 0.5f,
                                    i % 7 == 0));
        }
        Expect(agent.GetReplayBufferSize() == 64, "buffer 滿載截斷");

        std::string blob = agent.Serialize();
        Expect(blob.compare(0, 6, "PDQNv2") == 0, "PDQNv2 magic");

        DQNAgent copy(2, 4);
        Expect(copy.Deserialize(blob), "Deserialize 成功");
        Expect(!copy.IsDoubleDQN(), "v2 預設 double=false");

        bool same = true;
        for (int s = 0; s < 8; s++) {
            State probe({s * 0.25f, s * -0.125f});
            if (!VecNear(agent.GetQValues(probe), copy.GetQValues(probe)))
                same = false;
        }
        Expect(same, "Q 值向量逐元素一致");

        // doubleDQN 旗標隨模型持久化
        agent.SetDoubleDQN(true);
        DQNAgent copy2(2, 4);
        Expect(copy2.Deserialize(agent.Serialize()) && copy2.IsDoubleDQN(),
               "doubleDQN 旗標 round-trip");

        // v1 向後相容：把 v2 頭換回 v1、去掉旗標欄位應能讀
        {
            std::string v1 = blob;
            v1.replace(0, 6, "PDQNv1");
            // v1 參數行只有三欄——砍掉結尾 " 0"
            auto pos = v1.find('\n');
            auto pos2 = v1.find('\n', pos + 1);
            auto pos3 = v1.find('\n', pos2 + 1);
            std::string paramLine = v1.substr(pos2 + 1, pos3 - pos2 - 1);
            auto lastSpace = paramLine.rfind(' ');
            v1.erase(pos2 + 1 + lastSpace, pos3 - pos2 - 1 - lastSpace);
            DQNAgent legacy(2, 4);
            Expect(legacy.Deserialize(v1) && !legacy.IsDoubleDQN(),
                   "PDQNv1 相容讀取");
        }
    }

    // ---- [PolicyGradient round-trip] ----
    printf("-- PolicyGradient --\n");
    {
        PolicyGradientAgent agent(2, 4, 0.01f, 0.99f);
        for (int i = 0; i < 12; i++) {
            State s({i * 0.25f, i * -0.125f});
            agent.StoreTrajectory(s, Action(i % 4, ""),
                                  (i % 5) * 0.4f - 0.5f);
        }
        agent.UpdatePolicy();

        std::string blob = agent.Serialize();
        Expect(blob.compare(0, 5, "PPGv1") == 0, "PPGv1 magic");

        PolicyGradientAgent copy(2, 4);
        Expect(copy.Deserialize(blob), "Deserialize 成功");

        bool same = true;
        for (int s = 0; s < 8; s++) {
            State probe({s * 0.25f, s * -0.125f});
            if (!VecNear(agent.GetProbabilities(probe),
                         copy.GetProbabilities(probe)))
                same = false;
        }
        Expect(same, "機率分佈一致");
    }

    // ---- [ActorCritic round-trip] ----
    printf("-- ActorCritic --\n");
    {
        ActorCriticAgent agent(2, 4, 0.01f, 0.01f, 0.99f);
        for (int i = 0; i < 20; i++) {
            Experience e = MakeExp(i % 8, i % 4, (i % 5) * 0.4f - 0.5f,
                                   i % 7 == 0);
            agent.TrainStep(e.state, e.action, e.reward, e.nextState, e.done);
        }

        std::string blob = agent.Serialize();
        Expect(blob.compare(0, 5, "PACv1") == 0, "PACv1 magic");

        ActorCriticAgent copy(2, 4);
        Expect(copy.Deserialize(blob), "Deserialize 成功");

        bool same = true;
        for (int s = 0; s < 8; s++) {
            State probe({s * 0.25f, s * -0.125f});
            if (!VecNear(agent.GetProbabilities(probe),
                         copy.GetProbabilities(probe)) ||
                std::fabs(agent.GetStateValue(probe) -
                          copy.GetStateValue(probe)) > 1e-5f)
                same = false;
        }
        Expect(same, "actor 機率 + critic 價值一致");
    }

    // ---- [收集任務] ----
    printf("-- 收集任務 --\n");
    {
        GridWorldEnvironment env(4, 4, {3, 0});
        env.SetPickups({{1, 0}});
        Expect(env.GetRemainingPickups() == 1, "設 1 個道具");
        Expect(env.GetFeatureStateSize() == 14, "有道具時 feature 14 維");

        env.Reset();
        Expect(env.GetFeatureState().features.size() == 14, "feature 向量 14 維");
        auto r = env.Step(Action(3, ""));  // (1,0) 踩道具
        Expect(env.GetRemainingPickups() == 0, "踩到自動收集");
        Expect(r.reward > 0.0f, "收集給正 reward");

        env.Step(Action(3, ""));           // (2,0)
        r = env.Step(Action(3, ""));       // (3,0) 終點+收完 → done
        Expect(r.done && r.reward >= 1.0f, "收完道具到終點才 done");

        // 未收道具踩終點：不算完成
        GridWorldEnvironment env2(4, 4, {2, 0});
        env2.SetPickups({{3, 3}});
        env2.Reset();
        env2.Step(Action(3, ""));
        r = env2.Step(Action(3, ""));      // (2,0) 是終點但道具還在
        Expect(!r.done, "未收完踩終點不算完成");

        env2.Reset();
        Expect(env2.GetRemainingPickups() == 1, "Reset 還原道具");
    }

    // ---- [PPO round-trip] ----
    printf("-- PPO --\n");
    {
        PPOAgent agent(2, 4, 0.001f, 0.001f, 0.99f, 0.2f, 0.95f, 2);
        for (int i = 0; i < 16; i++) {
            State s({i * 0.25f, i * -0.125f});
            Action a = agent.SelectAction(s, true);
            agent.StoreTransition(s, a, (i % 5) * 0.4f - 0.5f, i % 7 == 0);
        }
        agent.Update();  // 清 buffer + 更新權重

        std::string blob = agent.Serialize();
        Expect(blob.compare(0, 6, "PPPOv1") == 0, "PPPOv1 magic");

        PPOAgent copy(2, 4);
        Expect(copy.Deserialize(blob), "Deserialize 成功");

        bool same = true;
        for (int s = 0; s < 8; s++) {
            State probe({s * 0.25f, s * -0.125f});
            if (!VecNear(agent.GetProbabilities(probe),
                         copy.GetProbabilities(probe)) ||
                std::fabs(agent.GetStateValue(probe) -
                          copy.GetStateValue(probe)) > 1e-5f)
                same = false;
        }
        Expect(same, "actor 機率 + critic 價值一致");
    }

    // ---- [RLPolicy 介面卡] ----
    printf("-- RLPolicy --\n");
    {
        // NN 模型（PDQNv2）自動偵測 + SelectAction
        DQNAgent src(2, 4, 0.01f, 0.99f, 0.0f, 64, 8);
        for (int i = 0; i < 40; i++) {
            src.TrainStep(MakeExp(i % 8, i % 4, (i % 5) * 0.4f - 0.5f,
                                  i % 7 == 0));
        }
        RLPolicy pol;
        Expect(pol.LoadFromString(src.Serialize()), "DQN 自動偵測");
        Expect(pol.GetAlgo() == RLPolicy::Algo::DQN, "algo=DQN");
        Expect(pol.GetStateSize() == 2, "StateSize=2");
        State probe({0.5f, -0.5f});
        Action act = pol.SelectAction(probe, false);
        Expect(act.id >= 0 && act.id < 4, "SelectAction 合法動作");

        // QL 裸表（無 magic）fallback 偵測
        QLearningAgent qsrc(36, 4);
        RLPolicy qpol;
        Expect(qpol.LoadFromString(qsrc.Serialize()), "QL 裸表偵測");
        Expect(qpol.GetAlgo() == RLPolicy::Algo::QLearning, "algo=QL");
        Expect(qpol.SelectTabularAction(5) >= 0, "SelectTabularAction 合法");
    }

    // ---- [容錯] ----
    printf("-- 容錯 --\n");
    {
        const std::string garbage = "this is not a model file\n12345";
        QLearningAgent ql(4, 4);
        Expect(!ql.Deserialize(garbage), "QL 拒絕垃圾");
        DQNAgent dqn(2, 4);
        Expect(!dqn.Deserialize(garbage), "DQN 拒絕垃圾");
        PolicyGradientAgent pg(2, 4);
        Expect(!pg.Deserialize(garbage), "PG 拒絕垃圾");
        ActorCriticAgent ac(2, 4);
        Expect(!ac.Deserialize(garbage), "AC 拒絕垃圾");
        PPOAgent ppo(2, 4);
        Expect(!ppo.Deserialize(garbage), "PPO 拒絕垃圾");
        RLPolicy pol;
        Expect(!pol.LoadFromString(garbage), "RLPolicy 拒絕垃圾");
        // 空字串也要拒絕
        Expect(!ql.Deserialize(""), "QL 拒絕空字串");
    }

    printf("\n=== 結果: %d PASS / %d FAIL ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
