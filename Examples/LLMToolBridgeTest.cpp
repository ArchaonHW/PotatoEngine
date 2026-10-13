// LLMToolBridgeTest — ToolRegistry ↔ LLM function-calling 橋的
// headless 驗證：schema 序列化、arguments JSON→string map、
// 派發/錯誤路徑、tool 訊息回填。

#include "AI/LLMToolBridge.h"
#include "AI/ZetaAgentTools.h"

#include <cstdio>
#include <string>
#include <unordered_map>

using namespace Potato::AI;

static int g_pass = 0;
static int g_fail = 0;

static void Check(bool ok, const char* name) {
    if (ok) { ++g_pass; printf("  [PASS] %s\n", name); }
    else    { ++g_fail; printf("  [FAIL] %s\n", name); }
}

int main() {
    printf("=== LLMToolBridge 測試 ===\n\n");

    ToolRegistry reg;
    // 一個簡單 echo 工具 + zeta 面
    Tool echo;
    echo.name = "echo_test";
    echo.description = "回顯 x 參數";
    echo.parameters["x"] = "回顯的字串";
    echo.parameters["n"] = "數值參數";
    echo.function = [](const std::unordered_map<std::string,
                                                 std::string>& p)
        -> ToolResult {
        ToolResult r;
        r.success = true;
        auto x = p.find("x"), n = p.find("n");
        r.output = "x=" + (x != p.end() ? x->second : std::string("?")) +
                   " n=" + (n != p.end() ? n->second : std::string("?"));
        return r;
    };
    reg.RegisterTool(echo);
    ZetaTools::RegisterZetaTools(reg);

    // ---- schema 序列化 ----
    {
        const ToolDefinition def =
            LLMToolBridge::ToToolDefinition(echo);
        Check(def.name == "echo_test",
              "ToToolDefinition name 帶過");
        Check(def.parametersSchema.find(
                  "\"type\":\"object\"") != std::string::npos &&
                  def.parametersSchema.find("\"x\"") !=
                      std::string::npos &&
                  def.parametersSchema.find("\"n\"") !=
                      std::string::npos,
              "ToToolDefinition schema 含 object+參數");
    }
    {
        const auto defs =
            LLMToolBridge::RegistryToolDefinitions(reg);
        Check(defs.size() == reg.GetToolCount(),
              "RegistryToolDefinitions 覆蓋全部工具");
        bool found = false;
        for (const auto& d : defs)
            if (d.name == "echo_test") found = true;
        Check(found, "RegistryToolDefinitions 含 echo_test");
    }

    // ---- arguments 解析 ----
    {
        std::unordered_map<std::string, std::string> out;
        std::string err;
        Check(LLMToolBridge::ParseToolArguments(
                  "{\"x\":\"hi\",\"n\":42,\"f\":true}", out,
                  err) &&
                  out["x"] == "hi" && out["n"] == "42" &&
                  out["f"] == "true",
              "ParseToolArguments 純量值轉字串");
    }
    {
        std::unordered_map<std::string, std::string> out;
        std::string err;
        Check(!LLMToolBridge::ParseToolArguments(
                  "{\"x\":{\"nested\":1}}", out, err) &&
                  !err.empty(),
              "ParseToolArguments 拒絕巢狀值");
        Check(!LLMToolBridge::ParseToolArguments(
                  "not json", out, err),
              "ParseToolArguments 拒絕非法 JSON");
        Check(!LLMToolBridge::ParseToolArguments(
                  "[1,2]", out, err),
              "ParseToolArguments 拒絕非 object");
        Check(LLMToolBridge::ParseToolArguments("", out, err) &&
                  out.empty(),
              "ParseToolArguments 空字串=無參數");
    }

    // ---- 派發 ----
    {
        ToolCall call;
        call.name = "echo_test";
        call.arguments = "{\"x\":\"ping\",\"n\":7}";
        const ToolResult r =
            LLMToolBridge::ExecuteToolCall(reg, call);
        Check(r.success && r.output == "x=ping n=7",
              "ExecuteToolCall 派發+參數解析");
    }
    {
        ToolCall call;
        call.name = "zeta_eval";
        call.arguments = "{\"sigma\":\"2\"}";
        const ToolResult r =
            LLMToolBridge::ExecuteToolCall(reg, call);
        Check(r.success &&
                  r.output.find("1.644934") !=
                      std::string::npos,
              "ExecuteToolCall 端到端打 zeta_eval");
    }
    {
        ToolCall call;
        call.name = "no_such_tool";
        call.arguments = "{}";
        Check(!LLMToolBridge::ExecuteToolCall(reg, call)
                   .success,
              "ExecuteToolCall 未知工具錯誤");
        call.name = "echo_test";
        call.arguments = "{bad";
        Check(!LLMToolBridge::ExecuteToolCall(reg, call)
                   .success,
              "ExecuteToolCall 壞 arguments 錯誤");
    }

    // ---- 結果 → tool 訊息 ----
    {
        ToolCall call;
        call.name = "echo_test";
        ToolResult ok;
        ok.success = true;
        ok.output = "done";
        const ChatMessage m =
            LLMToolBridge::ToolResultMessage(call, ok);
        Check(m.role == MessageRole::Tool &&
                  m.name == "echo_test" &&
                  m.content == "done",
              "ToolResultMessage 成功路徑");
        ToolResult bad;
        bad.success = false;
        bad.error = "boom";
        const ChatMessage m2 =
            LLMToolBridge::ToolResultMessage(call, bad);
        Check(m2.content.find("ERROR") != std::string::npos,
              "ToolResultMessage 錯誤路徑包 ERROR:");
    }

    printf("\n=== 結果 ===\n  PASS: %d\n  FAIL: %d\n",
           g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
