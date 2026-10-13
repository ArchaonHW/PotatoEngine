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
        call.id = "call_42";
        const ChatMessage m3 =
            LLMToolBridge::ToolResultMessage(call, ok);
        Check(m3.toolCallId == "call_42",
              "ToolResultMessage 帶回 tool_call_id");
    }

    // ---- OpenAI wire 回應解析 ----
    {
        const std::string wire = R"({"choices":[{"message":{
            "role":"assistant","content":null,
            "tool_calls":[{"id":"call_1","type":"function",
            "function":{"name":"zeta_eval",
            "arguments":"{\"sigma\":\"2\",\"t\":\"0\"}"}}]},
            "finish_reason":"tool_calls"}],
            "usage":{"prompt_tokens":10,"completion_tokens":5,
            "total_tokens":15}})";
        const LLMResponse r =
            LLMUtils::ParseOpenAiResponseJson(wire);
        Check(r.success && r.toolCalls.size() == 1,
              "OpenAI wire 解析出 tool_calls");
        if (r.toolCalls.size() == 1) {
            Check(r.toolCalls[0].id == "call_1" &&
                      r.toolCalls[0].name == "zeta_eval" &&
                      r.toolCalls[0].arguments.find(
                          "\"sigma\"") != std::string::npos,
                  "OpenAI tool_call id/name/arguments");
        }
    }
    {
        // 多 tool_calls + content 並存 + Unicode arguments
        const std::string wire = R"({"choices":[{"message":{
            "role":"assistant","content":"我先查兩項",
            "tool_calls":[
              {"id":"c1","function":{"name":"a",
               "arguments":"{}"}},
              {"id":"c2","function":{"name":"b",
               "arguments":"{\"x\":\"中\"}"}}]},
            "finish_reason":"tool_calls"}]})";
        const LLMResponse r =
            LLMUtils::ParseOpenAiResponseJson(wire);
        Check(r.success && r.toolCalls.size() == 2 &&
                  r.content == "我先查兩項" &&
                  r.toolCalls[1].arguments.find("中") !=
                      std::string::npos,
              "OpenAI 多 tool_calls + Unicode arguments");
    }
    {
        const LLMResponse r =
            LLMUtils::ParseOpenAiResponseJson("not json");
        Check(!r.success && !r.error.empty(),
              "OpenAI wire 壞 JSON 安全失敗");
        const LLMResponse r2 =
            LLMUtils::ParseOpenAiResponseJson(
                "{\"choices\":[{\"message\":{\"content\":\"hi\"}}]}");
        Check(r2.success && r2.toolCalls.empty() &&
                  r2.content == "hi",
              "OpenAI 純文字回應無 tool_calls");
    }

    // ---- Anthropic wire 回應解析 ----
    {
        const std::string wire = R"({"content":[
            {"type":"text","text":"查一下"},
            {"type":"tool_use","id":"toolu_1",
             "name":"zeta_mertens",
             "input":{"x":1000}}],
            "stop_reason":"tool_use",
            "usage":{"input_tokens":7,"output_tokens":3}})";
        const LLMResponse r =
            LLMUtils::ParseAnthropicResponseJson(wire);
        Check(r.success && r.content == "查一下" &&
                  r.toolCalls.size() == 1,
              "Anthropic 解析 text + tool_use");
        if (r.toolCalls.size() == 1)
            Check(r.toolCalls[0].id == "toolu_1" &&
                      r.toolCalls[0].name == "zeta_mertens" &&
                      r.toolCalls[0].arguments.find(
                          "1000") != std::string::npos,
                  "Anthropic tool_use input→arguments");
    }
    {
        const LLMResponse r =
            LLMUtils::ParseAnthropicResponseJson("{bad");
        Check(!r.success && !r.error.empty(),
              "Anthropic 壞 JSON 安全失敗");
    }

    // ---- ParseToolCall / ExtractToolCalls ----
    {
        const ToolCall c = LLMUtils::ParseToolCall(
            R"({"id":"k9","function":{"name":"f","arguments":"{\"a\":1}"}})");
        Check(c.id == "k9" && c.name == "f" &&
                  c.arguments.find("\"a\"") != std::string::npos,
              "ParseToolCall 巢狀 function 型");
        const ToolCall flat = LLMUtils::ParseToolCall(
            R"({"id":"k","name":"g","arguments":{"a":2}})");
        Check(flat.name == "g" &&
                  flat.arguments.find("2") != std::string::npos,
              "ParseToolCall 扁平+object arguments");
        const ToolCall bad =
            LLMUtils::ParseToolCall("not json");
        Check(bad.name.empty(), "ParseToolCall 壞 JSON→空");
    }

    // ---- 端到端 loop：scripted 回應→解析→派發→回填→第二輪 ----
    {
        const LLMResponse r1 =
            LLMUtils::ParseOpenAiResponseJson(
                R"({"choices":[{"message":{"role":"assistant",
                "content":null,"tool_calls":[{"id":"call_z",
                "type":"function","function":{"name":"zeta_eval",
                "arguments":"{\"sigma\":\"2\"}"}}]},
                "finish_reason":"tool_calls"}]})");
        Check(r1.success && r1.toolCalls.size() == 1,
              "loop 第一輪解析");

        std::vector<ChatMessage> convo;
        ChatMessage user;
        user.role = MessageRole::User;
        user.content = "zeta(2)=?";
        convo.push_back(user);

        ChatMessage asst;
        asst.role = MessageRole::Assistant;
        asst.content = r1.content;
        asst.toolCalls = r1.toolCalls;
        convo.push_back(asst);

        for (const auto& tc : r1.toolCalls) {
            const ToolResult tr =
                LLMToolBridge::ExecuteToolCall(reg, tc);
            Check(tr.success &&
                      tr.output.find("1.644934") !=
                          std::string::npos,
                  "loop 派發 zeta_eval");
            convo.push_back(
                LLMToolBridge::ToolResultMessage(tc, tr));
        }
        Check(convo.size() == 3 &&
                  convo[2].role == MessageRole::Tool &&
                  convo[2].toolCallId == "call_z" &&
                  convo[2].content.find("1.644934") !=
                      std::string::npos,
              "loop tool 訊息回填含結果+id");

        const LLMResponse r2 =
            LLMUtils::ParseOpenAiResponseJson(
                R"({"choices":[{"message":{"role":"assistant",
                "content":"ζ(2)=π²/6≈1.644934"},
                "finish_reason":"stop"}]})");
        Check(r2.success && r2.toolCalls.empty() &&
                  r2.content.find("1.644934") !=
                      std::string::npos,
              "loop 第二輪純文字收尾");
    }

    printf("\n=== 結果 ===\n  PASS: %d\n  FAIL: %d\n",
           g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
