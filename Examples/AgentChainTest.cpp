// AgentChainTest - Agent 鏈/圖執行測試（全 headless）
//
// 覆蓋：LLM 節點（fake client）、Tool 節點（真 registry
//   執行+{var} 參數展開）、Condition 真分支與結果傳播、
//   Loop、Parallel 真並行、AgentGraph 拓撲執行與上游
//   輸出傳遞。

#include "AgentChain.h"
#include "ToolFramework.h"

#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

using namespace Potato::AI;

static int g_pass = 0;
static int g_fail = 0;

static void Check(bool ok, const char* name) {
    if (ok) { g_pass++; printf("  [PASS] %s\n", name); }
    else    { g_fail++; printf("  [FAIL] %s\n", name); }
}

namespace {

// 回聲假 LLM：回傳最後一則 user 訊息
class FakeLLM : public ILLMClient {
public:
    LLMResponse ChatCompletion(
        const std::vector<ChatMessage>& messages,
        const LLMConfig&) override {
        LLMResponse r;
        r.success = true;
        r.content = "llm:" +
            (messages.empty() ? "" : messages.back().content);
        return r;
    }
    LLMResponse ChatCompletionWithTools(
        const std::vector<ChatMessage>&,
        const std::vector<ToolDefinition>&,
        const LLMConfig&) override {
        return LLMResponse();
    }
    void ChatCompletionStream(
        const std::vector<ChatMessage>&, const LLMConfig&,
        std::function<void(const std::string&)>) override {}
    std::future<LLMResponse> ChatCompletionAsync(
        const std::vector<ChatMessage>&,
        const LLMConfig&) override {
        return std::future<LLMResponse>();
    }
    std::vector<float> GenerateEmbedding(
        const std::string&,
        const std::string&) override {
        return {};
    }
    std::vector<std::vector<float>> GenerateEmbeddings(
        const std::vector<std::string>&,
        const std::string&) override {
        return {};
    }
    bool ValidateConfig(const LLMConfig&) override {
        return true;
    }
    std::string GetDefaultModel() override { return "fake"; }
};

ToolResult Ok(const std::string& s) {
    ToolResult r;
    r.success = true;
    r.output = s;
    return r;
}

Tool MakeEchoTool() {
    Tool t;
    t.name = "echo";
    t.description = "echo text";
    t.requiredParameters = {"text"};
    t.function = [](const std::unordered_map<
                    std::string, std::string>& p) {
        return Ok("T:" + p.at("text"));
    };
    return t;
}

} // namespace

int main() {
    printf("=== AgentChain 測試 ===\n\n");

    FakeLLM llm;
    ToolRegistry reg;
    reg.RegisterTool(MakeEchoTool());

    // [1] LLM 節點 + {var} 展開
    {
        AgentChain chain("c1");
        chain.SetLLMClient(&llm);
        ChainNode n;
        n.id = "ask";
        n.type = ChainNodeType::LLM;
        n.messages = {
            ChatMessage(MessageRole::User, "tell me {topic}")};
        chain.AddNode(n);
        ChainResult r =
            chain.Execute({{"topic", "physics"}});
        Check(r.success && r.output == "llm:tell me physics",
              "LLM 節點執行+{var} 展開");
    }

    // [2] Tool 節點真執行 + 參數展開
    {
        AgentChain chain("c2");
        chain.SetToolRegistry(&reg);
        ChainNode n;
        n.id = "run";
        n.type = ChainNodeType::Tool;
        n.toolName = "echo";
        n.toolParameters = {{"text", "{input}"}};
        chain.AddNode(n);
        ChainResult r = chain.Execute({{"input", "data42"}});
        Check(r.success && r.output == "T:data42",
              "Tool 節點真執行+參數展開");

        // 未設 registry → 錯誤而非假成功
        AgentChain bare("c2b");
        bare.AddNode(n);
        r = bare.Execute({});
        Check(!r.success &&
                  r.error.find("not set") != std::string::npos,
              "Tool 節點無 registry 報錯");
    }

    // [3] Condition：真分支求值 + 結果傳播
    {
        AgentChain chain("c3");
        chain.SetToolRegistry(&reg);
        ChainNode emit;
        emit.id = "e";
        emit.type = ChainNodeType::Tool;
        emit.toolName = "echo";
        emit.toolParameters = {{"text", "big"}};
        ChainNode okT;
        okT.id = "t_true";
        okT.type = ChainNodeType::Tool;
        okT.toolName = "echo";
        okT.toolParameters = {{"text", "yes-{e}"}};
        ChainNode okF;
        okF.id = "t_false";
        okF.type = ChainNodeType::Tool;
        okF.toolName = "echo";
        okF.toolParameters = {{"text", "no"}};
        ChainNode cond;
        cond.id = "cond";
        cond.type = ChainNodeType::Condition;
        cond.conditionExpression = "{e} == T:big";
        cond.trueBranchId = "t_true";
        cond.falseBranchId = "t_false";
        chain.AddNode(emit);
        chain.AddNode(okT);
        chain.AddNode(okF);
        chain.AddNode(cond);
        ChainResult r = chain.Execute({});
        Check(r.success && r.output == "T:yes-T:big",
              "Condition == 分支+{上游輸出} 展開");

        // 數值比較 + false 分支（cnt 直接為輸入變數）
        AgentChain c4("c4");
        c4.SetToolRegistry(&reg);
        ChainNode condNum = cond;
        condNum.conditionExpression = "{cnt} > 10";
        c4.AddNode(okT);
        c4.AddNode(okF);
        c4.AddNode(condNum);
        r = c4.Execute({{"cnt", "5"}});
        Check(r.success && r.output == "T:no",
              "Condition 數值比較走 false 分支");
    }

    // [4] Loop：計數到條件失效
    {
        AgentChain chain("c5");
        chain.SetToolRegistry(&reg);
        // body 每次把 cnt 變數加 1——用工具累積
        Tool inc;
        inc.name = "inc";
        inc.description = "increment";
        inc.requiredParameters = {"n"};
        inc.function = [](const std::unordered_map<
                          std::string, std::string>& p) {
            return Ok(std::to_string(std::stoi(p.at("n")) + 1));
        };
        reg.RegisterTool(inc);
        // body 讀自己上一輪輸出——自引用計數器
        ChainNode body;
        body.id = "body";
        body.type = ChainNodeType::Tool;
        body.toolName = "inc";
        body.toolParameters = {{"n", "{body}"}};
        ChainNode loop;
        loop.id = "loop";
        loop.type = ChainNodeType::Loop;
        loop.loopExpression = "{body} < 3";
        loop.loopBodyId = "body";
        loop.maxIterations = 10;
        chain.AddNode(body);
        chain.AddNode(loop);
        ChainResult r = chain.Execute({{"body", "0"}});
        Check(r.success &&
                  r.output == "Loop completed with 3 iterations",
              "Loop 依條件迭代至失效");
    }

    // [5] Parallel：子節點真並行 + 合併輸出
    {
        AgentChain chain("c6");
        chain.SetToolRegistry(&reg);
        ChainNode t1;
        t1.id = "pa";
        t1.type = ChainNodeType::Tool;
        t1.toolName = "echo";
        t1.toolParameters = {{"text", "A"}};
        ChainNode t2 = t1;
        t2.id = "pb";
        t2.toolParameters = {{"text", "B"}};
        ChainNode par;
        par.id = "par";
        par.type = ChainNodeType::Parallel;
        par.parallelNodeIds = {"pa", "pb"};
        chain.AddNode(t1);
        chain.AddNode(t2);
        chain.AddNode(par);
        ChainResult r = chain.Execute({});
        Check(r.success &&
                  r.output.find("T:A") != std::string::npos &&
                  r.output.find("T:B") != std::string::npos,
              "Parallel 子節點皆執行且輸出合併");
    }

    // [6] AgentGraph：拓撲排序 + 上游輸出流向下游
    {
        AgentGraph g("g1");
        g.SetToolRegistry(&reg);
        ChainNode a;
        a.id = "first";
        a.type = ChainNodeType::Tool;
        a.toolName = "echo";
        a.toolParameters = {{"text", "seed"}};
        ChainNode b;
        b.id = "second";
        b.type = ChainNodeType::Tool;
        b.toolName = "echo";
        b.toolParameters = {{"text", "{first}-next"}};
        g.AddNode(a);
        g.AddNode(b);
        g.AddEdge("first", "second");
        Check(g.Validate() && !g.HasCycles(),
              "DAG 驗證/無環");
        ChainResult r = g.Execute({});
        Check(r.success && r.output == "T:T:seed-next",
              "Graph 拓撲執行+上游輸出傳遞");

        // 環偵測
        g.AddEdge("second", "first");
        Check(g.HasCycles(), "加邊成環被偵測");
    }

    // [7] ChainOrchestrator：按名註冊/執行 + 依賴傳播
    {
        auto chain = std::make_unique<AgentChain>("named_chain");
        ChainNode n;
        n.id = "only";
        n.type = ChainNodeType::Tool;
        n.toolName = "echo";
        n.toolParameters = {{"text", "orch"}};
        chain->AddNode(n);

        ChainOrchestrator orch;
        orch.SetToolRegistry(&reg);
        orch.RegisterChain(std::move(chain));
        Check(orch.GetChain("named_chain") != nullptr &&
                  orch.GetChain("only") == nullptr,
              "Orchestrator 以鏈名註冊（非首節點 id）");
        ChainResult r = orch.ExecuteChain("named_chain", {});
        Check(r.success && r.output == "T:orch",
              "Orchestrator 執行+registry 傳播");

        // 空鏈註冊不崩潰
        orch.RegisterChain(std::make_unique<AgentChain>("empty"));
        Check(orch.GetChain("empty") != nullptr,
              "空鏈註冊安全");
    }

    printf("\n=== 結果 ===\n  PASS: %d\n  FAIL: %d\n",
           g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
