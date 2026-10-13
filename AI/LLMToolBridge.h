#pragma once

/**
 * LLMToolBridge — ToolRegistry（ToolFramework）↔ LLM function
 * calling（LLMIntegration）的雙向橋接。
 *
 * 引擎的 render 系/zeta 系工具全是 string-map 參數；LLM 端是
 * JSON Schema + arguments JSON object。本橋把 ToolRegistry
 * 序列化成 ChatCompletionWithTools 吃的 ToolDefinition，
 * 再把回傳的 ToolCall.arguments 解析回 string map 派發——
 * LLM 由此可直接驅動 RID 繪圖與 RH 數值審計。
 *
 * 對齊兩側的誠實邊界：工具參數全 string，schema 一律
 * {"type":"string"}；arguments 解析失敗/未知工具回
 * ToolResult 錯誤而非 throw。
 */

#include "LLMIntegration.h"
#include "ToolFramework.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace Potato {
namespace AI {
namespace LLMToolBridge {

// ---- Registry → Schema ----

// 單一 Tool → OpenAI 式 function-calling schema：
// {"type":"object","properties":{"p":{"type":"string",
// "description":"..."}},"required":[]}
// 引擎工具參數全為 string——schema 如實標示。
ToolDefinition ToToolDefinition(const Tool& tool);

// 整個 registry 的可呼叫面（GetToolNames 序）。
std::vector<ToolDefinition> RegistryToolDefinitions(
    const ToolRegistry& registry);

// ---- ToolCall → Dispatch ----

// arguments JSON object → string map。值轉字串
// （number/bool 以原文字面量），巢狀 object/array 拒絕。
// 失敗回 false 並填 err。
bool ParseToolArguments(
    const std::string& json,
    std::unordered_map<std::string, std::string>& out,
    std::string& err);

// 單次派發：arguments 解析失敗/工具未註冊回 ToolResult
// 錯誤，不 throw。
ToolResult ExecuteToolCall(ToolRegistry& registry,
                           const ToolCall& call);

// 結果 → role=Tool 的 ChatMessage（name=call.name，
// content 為 output 或 "ERROR: ..."）——下一輪 messages
// 直接接回去。
ChatMessage ToolResultMessage(const ToolCall& call,
                              const ToolResult& result);

// ---- Multi-round loop ----

struct ToolLoopResult {
    bool success = false;
    LLMResponse final;                 // 最後一輪 LLM 回應（純文字或錯誤）
    std::vector<ChatMessage> messages; // 含 assistant/tool 回放的完整對話
    int rounds = 0;                    // 實際執行的 tool 派發輪數
    std::string error;
};

// 完整的 tool-calling 迴圈：送 messages+tools → 若回應含
// toolCalls 則逐個 ExecuteToolCall 並把 assistant/tool 訊息
// 回放進對話 → 再送，直到 LLM 回純文字或達 maxRounds。
// LLM 傳輸錯誤/回應錯誤即中止；maxRounds 用盡視為失敗
// （防 tool ping-pong 無限迴圈），messages 保留現場。
ToolLoopResult RunToolLoop(
    ILLMClient& client,
    ToolRegistry& registry,
    std::vector<ChatMessage> messages,
    const std::vector<ToolDefinition>& tools,
    const LLMConfig& config,
    int maxRounds = 8);

} // namespace LLMToolBridge
} // namespace AI
} // namespace Potato
