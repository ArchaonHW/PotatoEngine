#include "AI/LLMToolBridge.h"

#include "Serialization/JsonParser.h"

#include <cmath>
#include <cstdio>
#include <sstream>

namespace Potato {
namespace AI {
namespace LLMToolBridge {

static std::string EscapeJson(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (const char c : in) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

ToolDefinition ToToolDefinition(const Tool& tool) {
    ToolDefinition def;
    def.name = tool.name;
    def.description = tool.description;
    std::ostringstream schema;
    schema << "{\"type\":\"object\",\"properties\":{";
    bool first = true;
    for (const auto& [pname, pdesc] : tool.parameters) {
        if (!first) schema << ",";
        first = false;
        schema << "\"" << EscapeJson(pname)
               << "\":{\"type\":\"string\",\"description\":\""
               << EscapeJson(pdesc) << "\"}";
    }
    schema << "},\"required\":[]}";
    def.parametersSchema = schema.str();
    return def;
}

std::vector<ToolDefinition> RegistryToolDefinitions(
    const ToolRegistry& registry) {
    std::vector<ToolDefinition> defs;
    for (const std::string& name : registry.GetToolNames()) {
        const Tool* tool =
            const_cast<ToolRegistry&>(registry).GetTool(name);
        if (tool) defs.push_back(ToToolDefinition(*tool));
    }
    return defs;
}

bool ParseToolArguments(
    const std::string& json,
    std::unordered_map<std::string, std::string>& out,
    std::string& err) {
    out.clear();
    if (json.empty()) return true; // 無參數工具
    const JsonValue v = JsonValue::Parse(json);
    if (!v.IsObject()) {
        err = "tool arguments 必須是 JSON object";
        return false;
    }
    for (const auto& [key, val] : v.objectValue) {
        if (val.IsObject() || val.IsArray() || val.IsNull()) {
            err = "參數 \"" + key +
                  "\" 是巢狀/空值——工具參數只收純量";
            return false;
        }
        if (val.IsNumber()) {
            // AsString 對 number 走 std::to_string 固定六位
            // 小數——LLM 送 42 應得到 "42" 讓 atoi/atof
            // 解析路徑一致
            const double d = val.AsNumber();
            if (d == std::floor(d) &&
                std::fabs(d) < 1e15) {
                out[key] = std::to_string(
                    static_cast<long long>(d));
            } else {
                char buf[40];
                std::snprintf(buf, sizeof(buf), "%.12g", d);
                out[key] = buf;
            }
        } else {
            out[key] = val.AsString();
        }
    }
    return true;
}

ToolResult ExecuteToolCall(ToolRegistry& registry,
                           const ToolCall& call) {
    std::unordered_map<std::string, std::string> params;
    std::string err;
    if (!ParseToolArguments(call.arguments, params, err)) {
        ToolResult r;
        r.success = false;
        r.error = "tool_call \"" + call.name +
                  "\" arguments: " + err;
        return r;
    }
    return registry.ExecuteTool(call.name, params);
}

ChatMessage ToolResultMessage(const ToolCall& call,
                              const ToolResult& result) {
    ChatMessage m;
    m.role = MessageRole::Tool;
    m.name = call.name;
    m.toolCallId = call.id; // OpenAI/Anthropic 都要求回填
    m.content = result.success
                    ? result.output
                    : "ERROR: " + result.error;
    return m;
}

ToolLoopResult RunToolLoop(
    ILLMClient& client,
    ToolRegistry& registry,
    std::vector<ChatMessage> messages,
    const std::vector<ToolDefinition>& tools,
    const LLMConfig& config,
    int maxRounds) {

    ToolLoopResult out;
    if (maxRounds < 1) maxRounds = 1;

    for (int round = 0; ; ++round) {
        const LLMResponse r =
            client.ChatCompletionWithTools(messages, tools, config);
        if (!r.success) {
            out.error = r.error.empty() ? "LLM 呼叫失敗" : r.error;
            out.final = r;
            out.messages = std::move(messages);
            return out;
        }
        if (r.toolCalls.empty()) {
            // 純文字收尾——成功
            out.success = true;
            out.final = r;
            out.messages = std::move(messages);
            return out;
        }
        if (round >= maxRounds) break; // 超額工具請求→中止

        // 回放 assistant 訊息（含 toolCalls，provider 要求原樣帶回）
        ChatMessage asst;
        asst.role = MessageRole::Assistant;
        asst.content = r.content;
        asst.toolCalls = r.toolCalls;
        messages.push_back(std::move(asst));

        // 逐個派發並回填 tool 結果
        for (const auto& tc : r.toolCalls) {
            const ToolResult tr = ExecuteToolCall(registry, tc);
            messages.push_back(ToolResultMessage(tc, tr));
        }
        out.rounds++;
    }

    out.error = "超過 maxRounds——可能存在 tool ping-pong";
    out.messages = std::move(messages);
    return out;
}

} // namespace LLMToolBridge
} // namespace AI
} // namespace Potato
