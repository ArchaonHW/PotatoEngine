/**
 * Potato Engine AI Agent JNI 橋接層——Web3/微服務擴充
 *
 * 與 PotatoAgentJni.cpp（RL GridWorld session API）同 DLL、不同 TU。
 * 此檔把引擎的完整 agent 平台暴露給 Java/Spring 層，
 * 參數與回傳一律 JSON 字串（Serialization/JsonParser.h 解析）：
 *
 *   NativeLlm    — nChat(configJson, messagesJson)：OpenAI/Anthropic/Local
 *   NativeChain  — nRun(specJson)：JSON 規格即時組裝 AgentChain 並執行，
 *                  回傳最終輸出 + 逐節點 intermediateResults；
 *                  nRegister/nUnregister/nListChains/nRunNamed 註冊鏈
 *                  重用,nRunAsync/nPoll 背景執行+節點事件輪詢；
 *                  spec 旗標 renderTools（headless 繪圖）/
 *                  zetaTools（RH 數值審計）
 *   NativeRag    — session 制知識庫：文件入庫/檢索/生成
 *   NativeRender — 全域唯一 headless RenderingServer + render_* 工具
 *                  （Godot singleton 語義，只應一個實例）
 *   NativeLedger — AuditLedger session：hash 鏈 append/verify/HeadHash/
 *                  分層揭露匯出，供 web3-service 外部錨定
 */

#include <jni.h>

#include "AI/AgentChain.h"
#include "AI/LLMIntegration.h"
#include "AI/LLMToolBridge.h"
#include "AI/RAGSystem.h"
#include "AI/RenderAgentTools.h"
#include "AI/ToolFramework.h"
#include "AI/ZetaAgentTools.h"
#include "Rendering/RenderingServerDefault.h"
#include "Security/AuditLedger.h"
#include "Serialization/JsonParser.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace Potato;
using namespace Potato::AI;
using namespace Potato::Security;

namespace {

// ============================================================================
// JSON 輸出 helpers（引擎只有 parser，輸出用最小的 writer）
// ============================================================================

std::string JsonEsc(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

std::string JQ(const std::string& s) { return "\"" + JsonEsc(s) + "\""; }

std::string JBool(bool b) { return b ? "true" : "false"; }

// JsonValue 最小序列化——委派 JsonValue::Dump
// （arguments input object → arguments JSON 用）
std::string JsonMin(const JsonValue& v) {
    return v.Dump();
}

// arguments 欄位可能是已序列化 JSON 字串或任意字串——
// 前者原樣輸出,後者 JQ 包成字串,保證外層 JSON 合法
std::string JArgs(const std::string& args) {
    if (args.empty()) return "{}";
    JsonValue probe;
    if (JsonValue::ParseOk(args, probe) &&
        (probe.IsObject() || probe.IsArray()))
        return args;
    return JQ(args);
}

std::string JErr(const std::string& err) {
    return "{\"success\":false,\"error\":" + JQ(err) + "}";
}

std::string JStrMap(const std::unordered_map<std::string, std::string>& m) {
    std::string out = "{";
    bool first = true;
    for (const auto& pair : m) {
        if (!first) out += ",";
        first = false;
        out += JQ(pair.first) + ":" + JQ(pair.second);
    }
    return out + "}";
}

std::string JToUtf8(JNIEnv* env, jstring s) {
    if (!s) return "";
    const char* chars = env->GetStringUTFChars(s, nullptr);
    std::string out(chars);
    env->ReleaseStringUTFChars(s, chars);
    return out;
}

// JSON object → string map（值一律 AsString 攤平）
std::unordered_map<std::string, std::string> ToStrMap(const JsonValue& v) {
    std::unordered_map<std::string, std::string> out;
    if (!v.IsObject()) return out;
    for (const auto& kv : v.objectValue) {
        out[kv.first] = kv.second.AsString();
    }
    return out;
}

// UTF-8 → UTF-16 轉換後 NewString——NewStringUTF 只吃 modified UTF-8,
// 碰到 supplementary plane / 嵌入 NUL 會噴錯
jstring ToJString(JNIEnv* env, const std::string& s) {
    std::vector<jchar> u16;
    u16.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp;
        size_t len;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c >> 5) == 0x6) { cp = c & 0x1F; len = 2; }
        else if ((c >> 4) == 0xE) { cp = c & 0x0F; len = 3; }
        else if ((c >> 3) == 0x1E) { cp = c & 0x07; len = 4; }
        else { cp = c; len = 1; }
        for (size_t k = 1; k < len && i + k < s.size(); k++) {
            cp = (cp << 6) | (s[i + k] & 0x3F);
        }
        i += len;
        if (cp > 0xFFFF) {
            u16.push_back(static_cast<jchar>(0xD800 + ((cp - 0x10000) >> 10)));
            u16.push_back(static_cast<jchar>(0xDC00 + ((cp - 0x10000) & 0x3FF)));
        } else {
            u16.push_back(static_cast<jchar>(cp));
        }
    }
    return env->NewString(u16.data(), static_cast<jsize>(u16.size()));
}

// ============================================================================
// LLM config / messages 解析
// ============================================================================

std::unique_ptr<ILLMClient> MakeClient(const JsonValue& cfg) {
    std::string provider = cfg["provider"].AsString("local");
    std::string apiKey = cfg["apiKey"].AsString();
    std::string baseURL = cfg["baseURL"].AsString();
    std::string modelPath = cfg["modelPath"].AsString();
    if (provider == "openai") {
        return std::make_unique<OpenAIClient>(apiKey, baseURL);
    }
    if (provider == "anthropic") {
        return std::make_unique<AnthropicClient>(apiKey, baseURL);
    }
    if (provider == "local") {
        return std::make_unique<LocalModelClient>(modelPath, baseURL);
    }
    return nullptr;
}

LLMConfig ParseLLMConfig(const JsonValue& cfg) {
    LLMConfig out;
    std::string provider = cfg["provider"].AsString("local");
    if (provider == "openai") out.provider = LLMProvider::OpenAI;
    else if (provider == "anthropic") out.provider = LLMProvider::Anthropic;
    else if (provider == "local") out.provider = LLMProvider::Local;
    else out.provider = LLMProvider::Custom;
    out.apiKey = cfg["apiKey"].AsString();
    out.baseURL = cfg["baseURL"].AsString();
    out.model = cfg["model"].AsString();
    out.temperature = cfg["temperature"].AsFloat(out.temperature);
    out.topP = cfg["topP"].AsFloat(out.topP);
    out.maxTokens = cfg["maxTokens"].AsInt(out.maxTokens);
    out.stream = cfg["stream"].AsBool(false);
    for (const auto& s : cfg["stop"].AsArray()) {
        out.stopSequences.push_back(s.AsString());
    }
    return out;
}

MessageRole ParseRole(const std::string& role) {
    if (role == "system") return MessageRole::System;
    if (role == "assistant") return MessageRole::Assistant;
    if (role == "tool") return MessageRole::Tool;
    return MessageRole::User;
}

// {id,name,arguments} → ToolCall；arguments 允許字串或 object
ToolCall ParseToolCall(const JsonValue& j) {
    ToolCall tc;
    tc.id = j["id"].AsString();
    tc.name = j["name"].AsString();
    tc.arguments = j["arguments"].AsString();
    if (tc.arguments.empty() && !j["arguments"].IsNull()) {
        // object/array 形式——最小序列化轉回字串
        tc.arguments = JsonMin(j["arguments"]);
    }
    return tc;
}

std::vector<ChatMessage> ParseMessages(const JsonValue& arr) {
    std::vector<ChatMessage> out;
    for (const auto& m : arr.AsArray()) {
        ChatMessage msg;
        msg.role = ParseRole(m["role"].AsString("user"));
        msg.content = m["content"].AsString();
        msg.name = m["name"].AsString();
        msg.toolCallId = m["toolCallId"].AsString(
            m["tool_call_id"].AsString());
        for (const auto& tc : m["toolCalls"].AsArray()) {
            msg.toolCalls.push_back(ParseToolCall(tc));
        }
        out.push_back(std::move(msg));
    }
    return out;
}

// {name,description,parameters|schema} → ToolDefinition
std::vector<ToolDefinition> ParseToolDefs(const JsonValue& arr) {
    std::vector<ToolDefinition> out;
    for (const auto& t : arr.AsArray()) {
        ToolDefinition d;
        d.name = t["name"].AsString();
        d.description = t["description"].AsString();
        // parameters 可直接給字串或 object
        const JsonValue& schema = t["parameters"].IsNull()
            ? t["schema"] : t["parameters"];
        if (schema.IsString()) {
            d.parametersSchema = schema.AsString();
        } else if (!schema.IsNull()) {
            d.parametersSchema = JsonMin(schema);
        }
        if (d.parametersSchema.empty()) {
            d.parametersSchema = "{\"type\":\"object\"}";
        }
        if (!d.name.empty()) out.push_back(std::move(d));
    }
    return out;
}

std::string LLMResponseToJson(const LLMResponse& r) {
    std::string out = "{"
        "\"success\":" + JBool(r.success) +
        ",\"content\":" + JQ(r.content) +
        ",\"finishReason\":" + JQ(r.finishReason) +
        ",\"promptTokens\":" + std::to_string(r.promptTokens) +
        ",\"completionTokens\":" + std::to_string(r.completionTokens) +
        ",\"totalTokens\":" + std::to_string(r.totalTokens) +
        ",\"toolCalls\":[";
    bool first = true;
    for (const auto& tc : r.toolCalls) {
        if (!first) out += ",";
        first = false;
        out += "{\"id\":" + JQ(tc.id) +
               ",\"name\":" + JQ(tc.name) +
               ",\"arguments\":" + JArgs(tc.arguments) + "}";
    }
    out += "],\"error\":" + JQ(r.error) + "}";
    return out;
}

// ============================================================================
// NativeRender — 全域唯一 headless server + tool registry
// ============================================================================

std::mutex g_renderMutex;
UniquePtr<RenderingServerDefault> g_renderServer;
std::unique_ptr<ToolRegistry> g_renderRegistry;
size_t g_renderToolCount = 0;

std::string ToolResultToJson(const ToolResult& r) {
    return "{"
        "\"success\":" + JBool(r.success) +
        ",\"output\":" + JQ(r.output) +
        ",\"error\":" + JQ(r.error) +
        ",\"executionTime\":" + std::to_string(r.executionTime) +
        ",\"metadata\":" + JStrMap(r.metadata) + "}";
}

bool EnsureRenderServer() {
    if (g_renderServer) return true;
    g_renderServer = RenderingServerDefault::CreateHeadless();
    if (!g_renderServer) return false;
    g_renderRegistry = std::make_unique<ToolRegistry>();
    g_renderToolCount =
        RenderTools::RegisterRenderTools(*g_renderRegistry, g_renderServer.get());
    return true;
}

// ============================================================================
// Session 池（RAG / Ledger 各一份）
// ============================================================================

template <typename T>
struct SessionPool {
    std::mutex mutex;
    std::unordered_map<jlong, std::unique_ptr<T>> sessions;
    std::atomic<jlong> nextId{1};

    jlong Put(std::unique_ptr<T> s) {
        jlong h = nextId.fetch_add(1);
        std::lock_guard<std::mutex> lock(mutex);
        sessions.emplace(h, std::move(s));
        return h;
    }
    // 呼叫端必須已持有 Lock()——池操作與引擎物件呼叫共用同一把鎖,
    // 引擎物件多數非執行緒安全,在此做全域序列化
    T* Get(jlong h) {
        auto it = sessions.find(h);
        return it == sessions.end() ? nullptr : it->second.get();
    }
    std::lock_guard<std::mutex> Lock() {
        return std::lock_guard<std::mutex>(mutex);
    }
    void Erase(jlong h) {
        std::lock_guard<std::mutex> lock(mutex);
        sessions.erase(h);
    }
};

struct RagSession {
    std::unique_ptr<RAGSystem> rag;
    std::unique_ptr<KnowledgeBase> kb;
    std::unique_ptr<ILLMClient> llm;
};

SessionPool<RagSession> g_rag;
SessionPool<AuditLedger> g_ledger;

RetrievalStrategy ParseStrategy(const std::string& s) {
    if (s == "semantic") return RetrievalStrategy::Semantic;
    if (s == "keyword") return RetrievalStrategy::Keyword;
    if (s == "bm25") return RetrievalStrategy::BM25;
    return RetrievalStrategy::Hybrid;
}

}  // namespace

extern "C" {

// ============================================================================
// NativeLlm
// ============================================================================

// nChat(configJson, messagesJson) → LLMResponse JSON。
// configJson: {provider:"openai"|"anthropic"|"local", apiKey, model,
//              modelPath, baseURL, temperature, topP, maxTokens, stop[],
//              tools?:[{name,description,parameters|schema}]}
//   —— tools 存在時走 ChatCompletionWithTools（LLM 可回 toolCalls）
// messagesJson: [{role:"system"|"user"|"assistant"|"tool", content, name?,
//               toolCallId?, toolCalls?}]
JNIEXPORT jstring JNICALL Java_com_potato_agent_jni_NativeLlm_nChat(
    JNIEnv* env, jclass, jstring jConfig, jstring jMessages) {
    JsonValue cfg = JsonValue::Parse(JToUtf8(env, jConfig));
    JsonValue msgs = JsonValue::Parse(JToUtf8(env, jMessages));
    if (!cfg.IsObject()) return ToJString(env, JErr("config JSON 解析失敗"));
    if (!msgs.IsArray()) return ToJString(env, JErr("messages 需為陣列"));

    auto client = MakeClient(cfg);
    if (!client) {
        return ToJString(env, JErr("未知 provider: " + cfg["provider"].AsString()));
    }
    const LLMConfig llmCfg = ParseLLMConfig(cfg);
    const std::vector<ChatMessage> chat = ParseMessages(msgs);
    const std::vector<ToolDefinition> tools =
        ParseToolDefs(cfg["tools"]);
    LLMResponse r = tools.empty()
        ? client->ChatCompletion(chat, llmCfg)
        : client->ChatCompletionWithTools(chat, tools, llmCfg);
    return ToJString(env, LLMResponseToJson(r));
}

// ChatMessage → JSON（回放對話用）
std::string MessageToJson(const ChatMessage& m) {
    const char* role = "user";
    switch (m.role) {
        case MessageRole::System:    role = "system"; break;
        case MessageRole::Assistant: role = "assistant"; break;
        case MessageRole::Tool:      role = "tool"; break;
        default: break;
    }
    std::string out = std::string("{\"role\":\"") + role +
        "\",\"content\":" + JQ(m.content);
    if (!m.name.empty()) out += ",\"name\":" + JQ(m.name);
    if (!m.toolCallId.empty())
        out += ",\"toolCallId\":" + JQ(m.toolCallId);
    if (!m.toolCalls.empty()) {
        out += ",\"toolCalls\":[";
        bool first = true;
        for (const auto& tc : m.toolCalls) {
            if (!first) out += ",";
            first = false;
            out += "{\"id\":" + JQ(tc.id) +
                   ",\"name\":" + JQ(tc.name) +
                   ",\"arguments\":" + JArgs(tc.arguments) + "}";
        }
        out += "]";
    }
    return out + "}";
}

// nToolLoop(configJson, messagesJson) → {success,content,rounds,
//   finishReason,messages:[...],error}
// configJson 同 nChat,外加：
//   tools?:[...]      顯式工具定義（缺省=依旗標建的 registry 全部）
//   zetaTools?:bool   掛 zeta_* 工具面（無狀態）
//   renderTools?:bool 掛 render_* 工具面（g_renderMutex 全程持有）
//   maxRounds?:int    預設 8
JNIEXPORT jstring JNICALL Java_com_potato_agent_jni_NativeLlm_nToolLoop(
    JNIEnv* env, jclass, jstring jConfig, jstring jMessages) {
    JsonValue cfg = JsonValue::Parse(JToUtf8(env, jConfig));
    JsonValue msgs = JsonValue::Parse(JToUtf8(env, jMessages));
    if (!cfg.IsObject()) return ToJString(env, JErr("config JSON 解析失敗"));
    if (!msgs.IsArray()) return ToJString(env, JErr("messages 需為陣列"));

    auto client = MakeClient(cfg);
    if (!client) {
        return ToJString(env, JErr("未知 provider: " + cfg["provider"].AsString()));
    }

    // 依旗標建本 loop 私有的 registry；render 面全程持鎖
    // （render_* 共用全域 headless server）
    ToolRegistry reg;
    if (cfg["zetaTools"].AsBool(false))
        ZetaTools::RegisterZetaTools(reg);

    std::unique_lock<std::mutex> renderLock(g_renderMutex,
                                            std::defer_lock);
    if (cfg["renderTools"].AsBool(false)) {
        renderLock.lock();
        if (!EnsureRenderServer())
            return ToJString(env, JErr("render server 初始化失敗"));
        RenderTools::RegisterRenderTools(reg, g_renderServer.get());
    }

    std::vector<ToolDefinition> tools = ParseToolDefs(cfg["tools"]);
    if (tools.empty())
        tools = LLMToolBridge::RegistryToolDefinitions(reg);

    const int maxRounds = cfg["maxRounds"].AsInt(8);
    const LLMToolBridge::ToolLoopResult lr =
        LLMToolBridge::RunToolLoop(
            *client, reg, ParseMessages(msgs), tools,
            ParseLLMConfig(cfg), maxRounds);

    std::string out = "{"
        "\"success\":" + JBool(lr.success) +
        ",\"content\":" + JQ(lr.final.content) +
        ",\"finishReason\":" + JQ(lr.final.finishReason) +
        ",\"rounds\":" + std::to_string(lr.rounds) +
        ",\"messages\":[";
    bool first = true;
    for (const auto& m : lr.messages) {
        if (!first) out += ",";
        first = false;
        out += MessageToJson(m);
    }
    out += "],\"error\":" + JQ(lr.error) + "}";
    return ToJString(env, out);
}

}  // extern "C"（暫時關閉——C 連結不允許 C++ 型別簽名）

namespace {

// ============================================================================
// NativeChain helpers
// ============================================================================

std::string ChainResultToJson(const ChainResult& r) {
    return "{"
        "\"success\":" + JBool(r.success) +
        ",\"output\":" + JQ(r.output) +
        ",\"error\":" + JQ(r.error) +
        ",\"executionTime\":" + std::to_string(r.executionTime) +
        ",\"nodes\":" + JStrMap(r.intermediateResults) + "}";
}

// spec.nodes → chain.AddNode（nRun / nRegister / nRunAsync 共用）
void FillChainNodes(AgentChain& chain, const JsonValue& spec) {
    for (const auto& jn : spec["nodes"].AsArray()) {
        ChainNode node;
        node.id = jn["id"].AsString();
        node.name = jn["name"].AsString(node.id);
        std::string type = jn["type"].AsString("tool");
        if (type == "agent") node.type = ChainNodeType::Agent;
        else if (type == "llm") node.type = ChainNodeType::LLM;
        else if (type == "condition") node.type = ChainNodeType::Condition;
        else if (type == "loop") node.type = ChainNodeType::Loop;
        else if (type == "parallel") node.type = ChainNodeType::Parallel;
        else node.type = ChainNodeType::Tool;

        node.agentId = jn["agentId"].AsString();
        node.toolName = jn["tool"].AsString(jn["toolName"].AsString());
        node.conditionExpression = jn["expr"].AsString(
            jn["condition"].AsString());
        node.trueBranchId = jn["true"].AsString(jn["trueBranch"].AsString());
        node.falseBranchId = jn["false"].AsString(jn["falseBranch"].AsString());
        node.loopExpression = jn["loopExpr"].AsString(
            jn["expr"].AsString());
        node.loopBodyId = jn["body"].AsString(jn["loopBody"].AsString());
        node.maxIterations = jn["maxIterations"].AsInt(100);

        for (const auto& d : jn["dependencies"].AsArray()) {
            node.dependencies.push_back(d.AsString());
        }
        for (const auto& p : jn["nodes"].AsArray()) {
            node.parallelNodeIds.push_back(p.AsString());
        }
        node.toolParameters = ToStrMap(jn["params"]);
        node.parameters = ToStrMap(jn["parameters"]);
        if (jn["messages"].IsArray()) {
            node.messages = ParseMessages(jn["messages"]);
        }
        if (jn["llm"].IsObject()) {
            node.llmConfig = ParseLLMConfig(jn["llm"]);
        } else if (spec["llm"].IsObject()) {
            node.llmConfig = ParseLLMConfig(spec["llm"]);
        }

        chain.AddNode(node);
    }
}

// 註冊鏈持有物——chain + 其依賴的 LLM client；renderTools 標記
// 表示執行時需持有 g_renderMutex；zetaTools 無狀態、建期即掛。
// tools 為每鏈私有的 ToolRegistry——zeta_* 建期掛入,render_*
// 在執行期（renderLock 持有中）掛入同一份
struct RegisteredChain {
    std::unique_ptr<AgentChain> chain;
    std::unique_ptr<ILLMClient> llm;
    bool renderTools = false;
    bool zetaTools = false;
    std::unique_ptr<ToolRegistry> tools;
    bool renderToolsMounted = false;
    std::mutex execMutex;  // chain->variables 非 reentrant——執行序列化
};

std::unique_ptr<RegisteredChain> BuildRegisteredChain(const JsonValue& spec) {
    auto rc = std::make_unique<RegisteredChain>();
    rc->chain = std::make_unique<AgentChain>(spec["name"].AsString("chain"));
    rc->renderTools = spec["renderTools"].AsBool(false);
    rc->zetaTools = spec["zetaTools"].AsBool(false);
    if (rc->zetaTools) {
        rc->tools = std::make_unique<ToolRegistry>();
        ZetaTools::RegisterZetaTools(*rc->tools);
    }
    if (spec["llm"].IsObject()) {
        rc->llm = MakeClient(spec["llm"]);
        if (rc->llm) rc->chain->SetLLMClient(rc->llm.get());
    }
    FillChainNodes(*rc->chain, spec);
    return rc;
}

// 執行期工具掛載——呼叫端須在 rc.renderTools 時已持有
// g_renderMutex（render_* 與 nInvoke 共用全域 server 的序列化鎖）;
// zeta_* 無共享狀態,不需任何鎖
void MountExecTools(RegisteredChain& rc) {
    if (rc.renderTools && EnsureRenderServer() &&
        !rc.renderToolsMounted) {
        if (!rc.tools) rc.tools = std::make_unique<ToolRegistry>();
        RenderTools::RegisterRenderTools(*rc.tools,
                                         g_renderServer.get());
        rc.renderToolsMounted = true;
    }
    if (rc.tools) rc.chain->SetToolRegistry(rc.tools.get());
}

std::mutex g_chainsMutex;
std::unordered_map<std::string, std::unique_ptr<RegisteredChain>> g_chains;

// ============================================================================
// async 執行狀態——nRunAsync 起背景執行緒,nPoll 拉節點事件與最終結果
// ============================================================================

struct AsyncExec {
    std::mutex m;
    std::deque<std::string> events;   // 每筆為 {"node":..,"output":..} JSON
    bool done = false;
    std::string resultJson;
    std::unordered_map<std::string, std::string> inputs;
    std::unique_ptr<RegisteredChain> rc;
    std::thread worker;
};

std::mutex g_execsMutex;
std::unordered_map<jlong, std::unique_ptr<AsyncExec>> g_execs;
std::atomic<jlong> g_nextExecId{1};

void AsyncRunBody(AsyncExec* e) {
    e->rc->chain->onNodeComplete =
        [e](const std::string& nodeId, const std::string& output) {
            std::lock_guard<std::mutex> lock(e->m);
            e->events.push_back(
                "{\"node\":" + JQ(nodeId) +
                ",\"output\":" + JQ(output) + "}");
        };

    ChainResult r;
    {
        std::unique_lock<std::mutex> renderLock(g_renderMutex,
                                                std::defer_lock);
        if (e->rc->renderTools) renderLock.lock();
        MountExecTools(*e->rc);
        r = e->rc->chain->Execute(e->inputs);
    }
    std::lock_guard<std::mutex> lock(e->m);
    e->resultJson = ChainResultToJson(r);
    e->done = true;
}

}  // namespace

extern "C" {

// ============================================================================
// NativeChain
// ============================================================================

// nRun(specJson) → ChainResult JSON
// specJson: {
//   name, inputs:{k:v},
//   llm: {...同 nChat config（有 LLM 節點才需要）},
//   renderTools: bool——掛 headless render_* 工具（Tool 節點用）,
//   zetaTools: bool——掛 zeta_* 數值工具（RH 有限驗證;無狀態免鎖）,
//   nodes: [
//     {id, type:"agent"|"llm"|"tool"|"condition"|"loop"|"parallel",
//      name?, agentId?, messages?, llm?,
//      tool?, params?, expr?, true?, false?, body?, maxIterations?,
//      nodes?, dependencies?, params(general)?}
//   ]
// }
JNIEXPORT jstring JNICALL Java_com_potato_agent_jni_NativeChain_nRun(
    JNIEnv* env, jclass, jstring jSpec) {
    JsonValue spec = JsonValue::Parse(JToUtf8(env, jSpec));
    if (!spec.IsObject()) return ToJString(env, JErr("spec JSON 解析失敗"));

    auto rc = BuildRegisteredChain(spec);

    // Tool registry：renderTools=true 時掛 headless render_* 工具,
    // zetaTools=true 時掛 zeta_* 數值工具（無狀態,建期已入 tools）。
    // render 鎖持有到 Execute 結束——render_* 與 nInvoke 序列化
    std::unique_lock<std::mutex> renderLock(g_renderMutex, std::defer_lock);
    if (rc->renderTools) renderLock.lock();
    MountExecTools(*rc);

    std::unordered_map<std::string, std::string> inputs =
        ToStrMap(spec["inputs"]);

    ChainResult r = rc->chain->Execute(inputs);
    return ToJString(env, ChainResultToJson(r));
}

// ============================================================================
// 註冊鏈——nRegister 存 spec,nRunNamed 依名執行（重用同一份節點組裝）
// ============================================================================

// nRegister(specJson) → {success, name}；同名覆蓋
JNIEXPORT jstring JNICALL Java_com_potato_agent_jni_NativeChain_nRegister(
    JNIEnv* env, jclass, jstring jSpec) {
    JsonValue spec = JsonValue::Parse(JToUtf8(env, jSpec));
    if (!spec.IsObject()) return ToJString(env, JErr("spec JSON 解析失敗"));
    std::string name = spec["name"].AsString();
    if (name.empty()) return ToJString(env, JErr("spec 需帶 name"));

    auto rc = BuildRegisteredChain(spec);
    if (rc->chain->GetNodes().empty()) {
        return ToJString(env, JErr("spec 無節點"));
    }
    {
        std::lock_guard<std::mutex> lock(g_chainsMutex);
        g_chains[name] = std::move(rc);
    }
    return ToJString(env,
        "{\"success\":true,\"name\":" + JQ(name) + "}");
}

JNIEXPORT jstring JNICALL Java_com_potato_agent_jni_NativeChain_nUnregister(
    JNIEnv* env, jclass, jstring jName) {
    std::lock_guard<std::mutex> lock(g_chainsMutex);
    return ToJString(env,
        "{\"success\":true,\"removed\":" +
        std::to_string(g_chains.erase(JToUtf8(env, jName))) + "}");
}

JNIEXPORT jstring JNICALL Java_com_potato_agent_jni_NativeChain_nListChains(
    JNIEnv* env, jclass) {
    std::lock_guard<std::mutex> lock(g_chainsMutex);
    std::string out = "{\"chains\":[";
    bool first = true;
    for (const auto& kv : g_chains) {
        if (!first) out += ",";
        first = false;
        out += JQ(kv.first);
    }
    return ToJString(env, out + "]}");
}

// nRunNamed(name, inputsJson) → ChainResult JSON
JNIEXPORT jstring JNICALL Java_com_potato_agent_jni_NativeChain_nRunNamed(
    JNIEnv* env, jclass, jstring jName, jstring jInputs) {
    RegisteredChain* rc;
    {
        std::lock_guard<std::mutex> lock(g_chainsMutex);
        auto it = g_chains.find(JToUtf8(env, jName));
        if (it == g_chains.end()) {
            return ToJString(env, JErr("chain 不存在: " + JToUtf8(env, jName)));
        }
        rc = it->second.get();
    }
    std::lock_guard<std::mutex> execLock(rc->execMutex);
    std::unique_lock<std::mutex> renderLock(g_renderMutex, std::defer_lock);
    if (rc->renderTools) renderLock.lock();
    MountExecTools(*rc);
    rc->chain->Reset();
    JsonValue inputs = JsonValue::Parse(JToUtf8(env, jInputs));
    ChainResult r = rc->chain->Execute(ToStrMap(inputs));
    return ToJString(env, ChainResultToJson(r));
}

// ============================================================================
// async 執行——nRunAsync 起背景執行緒,nPoll 拉節點事件與最終結果
// ============================================================================

// nRunAsync(specJson) → {execId}——spec 可含 name（註冊鏈也可走 async）
JNIEXPORT jstring JNICALL Java_com_potato_agent_jni_NativeChain_nRunAsync(
    JNIEnv* env, jclass, jstring jSpec) {
    JsonValue spec = JsonValue::Parse(JToUtf8(env, jSpec));
    if (!spec.IsObject()) return ToJString(env, JErr("spec JSON 解析失敗"));

    auto e = std::make_unique<AsyncExec>();
    e->rc = BuildRegisteredChain(spec);
    if (e->rc->chain->GetNodes().empty()) {
        return ToJString(env, JErr("spec 無節點"));
    }
    e->inputs = ToStrMap(spec["inputs"]);

    jlong id = g_nextExecId.fetch_add(1);
    AsyncExec* raw = e.get();
    {
        std::lock_guard<std::mutex> lock(g_execsMutex);
        g_execs.emplace(id, std::move(e));
    }
    raw->worker = std::thread(AsyncRunBody, raw);
    return ToJString(env,
        "{\"success\":true,\"execId\":" + std::to_string(id) + "}");
}

// nPoll(execId) → {done, events:[...], result:{...}|null}
// done=true 且回傳後 exec 自動回收（worker join + 移除）
JNIEXPORT jstring JNICALL Java_com_potato_agent_jni_NativeChain_nPoll(
    JNIEnv* env, jclass, jlong execId) {
    std::lock_guard<std::mutex> mapLock(g_execsMutex);
    auto it = g_execs.find(execId);
    if (it == g_execs.end()) {
        return ToJString(env, JErr("exec 不存在或已回收"));
    }
    AsyncExec* e = it->second.get();

    std::string out;
    bool done;
    {
        std::lock_guard<std::mutex> lock(e->m);
        out = "{\"done\":" + JBool(e->done) + ",\"events\":[";
        bool first = true;
        for (const auto& ev : e->events) {
            if (!first) out += ",";
            first = false;
            out += ev;
        }
        e->events.clear();
        out += "],\"result\":";
        if (e->done) out += e->resultJson; else out += "null";
        out += "}";
        done = e->done;
    }
    if (done) {
        if (e->worker.joinable()) e->worker.join();
        g_execs.erase(it);
    }
    return ToJString(env, out);
}

// ============================================================================
// NativeRag
// ============================================================================

JNIEXPORT jlong JNICALL Java_com_potato_agent_jni_NativeRag_nCreate(
    JNIEnv*, jclass) {
    auto s = std::make_unique<RagSession>();
    s->rag = std::make_unique<RAGSystem>(
        std::make_unique<InMemoryVectorDB>());
    s->kb = std::make_unique<KnowledgeBase>(s->rag.get());
    s->rag->Initialize(nullptr);
    return g_rag.Put(std::move(s));
}

JNIEXPORT void JNICALL Java_com_potato_agent_jni_NativeRag_nDestroy(
    JNIEnv*, jclass, jlong h) {
    g_rag.Erase(h);
}

// nConfigure(h, cfgJson)：{chunkSize, chunkOverlap, topK,
//   similarityThreshold, strategy, llm:{...}}
JNIEXPORT jboolean JNICALL Java_com_potato_agent_jni_NativeRag_nConfigure(
    JNIEnv* env, jclass, jlong h, jstring jCfg) {
    auto lock = g_rag.Lock();
    RagSession* s = g_rag.Get(h);
    if (!s) return JNI_FALSE;
    JsonValue cfg = JsonValue::Parse(JToUtf8(env, jCfg));
    if (!cfg.IsObject()) return JNI_FALSE;

    RAGConfig rc = s->rag->GetConfig();
    rc.chunkSize = cfg["chunkSize"].AsInt(rc.chunkSize);
    rc.chunkOverlap = cfg["chunkOverlap"].AsInt(rc.chunkOverlap);
    rc.topK = cfg["topK"].AsInt(rc.topK);
    rc.similarityThreshold =
        cfg["similarityThreshold"].AsFloat(rc.similarityThreshold);
    rc.strategy = ParseStrategy(cfg["strategy"].AsString("hybrid"));
    rc.rerank = cfg["rerank"].AsBool(rc.rerank);
    rc.enableCaching = cfg["enableCaching"].AsBool(rc.enableCaching);
    s->rag->SetConfig(rc);

    if (cfg["llm"].IsObject()) {
        s->llm = MakeClient(cfg["llm"]);
        if (s->llm) s->rag->Initialize(s->llm.get());
    }
    return JNI_TRUE;
}

// nAddDocument(h, docJson)：{id?, title, content, source?, tags?[]}
JNIEXPORT jboolean JNICALL Java_com_potato_agent_jni_NativeRag_nAddDocument(
    JNIEnv* env, jclass, jlong h, jstring jDoc) {
    auto lock = g_rag.Lock();
    RagSession* s = g_rag.Get(h);
    if (!s) return JNI_FALSE;
    JsonValue j = JsonValue::Parse(JToUtf8(env, jDoc));
    if (!j.IsObject() || j["content"].AsString().empty()) return JNI_FALSE;

    Document doc = RAGUtils::CreateDocument(
        j["title"].AsString(), j["content"].AsString());
    if (!j["id"].AsString().empty()) doc.id = j["id"].AsString();
    doc.source = j["source"].AsString();
    doc.url = j["url"].AsString();
    doc.author = j["author"].AsString();
    for (const auto& t : j["tags"].AsArray()) {
        doc.tags.push_back(t.AsString());
    }
    return s->rag->AddDocument(doc) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_com_potato_agent_jni_NativeRag_nRemoveDocument(
    JNIEnv* env, jclass, jlong h, jstring jId) {
    auto lock = g_rag.Lock();
    RagSession* s = g_rag.Get(h);
    if (!s) return JNI_FALSE;
    return s->rag->RemoveDocument(JToUtf8(env, jId)) ? JNI_TRUE : JNI_FALSE;
}

// nRetrieve(h, query, topK) → chunk JSON 陣列
JNIEXPORT jstring JNICALL Java_com_potato_agent_jni_NativeRag_nRetrieve(
    JNIEnv* env, jclass, jlong h, jstring jQuery, jint jTopK) {
    auto lock = g_rag.Lock();
    RagSession* s = g_rag.Get(h);
    if (!s) return ToJString(env, JErr("rag session 不存在"));

    auto chunks = s->rag->Retrieve(JToUtf8(env, jQuery),
                                   jTopK > 0 ? jTopK : 5);
    std::string out = "{\"success\":true,\"chunks\":[";
    bool first = true;
    for (const auto& c : chunks) {
        if (!first) out += ",";
        first = false;
        out += "{\"id\":" + JQ(c.id) +
               ",\"content\":" + JQ(c.content) +
               ",\"source\":" + JQ(c.source) +
               ",\"score\":" + std::to_string(c.relevanceScore) + "}";
    }
    return ToJString(env, out + "]}");
}

// nGenerate(h, query) → {answer, chunks}
JNIEXPORT jstring JNICALL Java_com_potato_agent_jni_NativeRag_nGenerate(
    JNIEnv* env, jclass, jlong h, jstring jQuery) {
    auto lock = g_rag.Lock();
    RagSession* s = g_rag.Get(h);
    if (!s) return ToJString(env, JErr("rag session 不存在"));

    std::string query = JToUtf8(env, jQuery);
    std::string answer = s->rag->GenerateResponse(query);
    return ToJString(env,
        "{\"success\":true,\"answer\":" + JQ(answer) + "}");
}

// nExport(h, path) / nImport(h, path)——知識庫文件持久化
// （chunk/embedding 不落地，import 時重建）
JNIEXPORT jboolean JNICALL Java_com_potato_agent_jni_NativeRag_nExport(
    JNIEnv* env, jclass, jlong h, jstring jPath) {
    auto lock = g_rag.Lock();
    RagSession* s = g_rag.Get(h);
    if (!s) return JNI_FALSE;
    return s->kb->Export(JToUtf8(env, jPath)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_com_potato_agent_jni_NativeRag_nImport(
    JNIEnv* env, jclass, jlong h, jstring jPath) {
    auto lock = g_rag.Lock();
    RagSession* s = g_rag.Get(h);
    if (!s) return JNI_FALSE;
    return s->kb->Import(JToUtf8(env, jPath)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL Java_com_potato_agent_jni_NativeRag_nStats(
    JNIEnv* env, jclass, jlong h) {
    auto lock = g_rag.Lock();
    RagSession* s = g_rag.Get(h);
    if (!s) return ToJString(env, JErr("rag session 不存在"));
    auto st = s->rag->GetStats();
    return ToJString(env,
        "{\"success\":true"
        ",\"documents\":" + std::to_string(s->kb->GetDocumentCount()) +
        ",\"chunks\":" + std::to_string(s->kb->GetChunkCount()) +
        ",\"totalQueries\":" + std::to_string(st.totalQueries) +
        ",\"totalRetrievals\":" + std::to_string(st.totalRetrievals) +
        ",\"totalGenerations\":" + std::to_string(st.totalGenerations) + "}");
}

// ============================================================================
// NativeRender（全域唯一 headless server——Godot singleton 語義）
// ============================================================================

// nInit() → 註冊的工具數；<0 = 失敗
JNIEXPORT jint JNICALL Java_com_potato_agent_jni_NativeRender_nInit(
    JNIEnv*, jclass) {
    std::lock_guard<std::mutex> lock(g_renderMutex);
    return EnsureRenderServer() ? static_cast<jint>(g_renderToolCount) : -1;
}

JNIEXPORT void JNICALL Java_com_potato_agent_jni_NativeRender_nShutdown(
    JNIEnv*, jclass) {
    std::lock_guard<std::mutex> lock(g_renderMutex);
    g_renderRegistry.reset();
    g_renderServer.reset();
    g_renderToolCount = 0;
}

// nInvoke(toolName, paramsJson) → ToolResult JSON
JNIEXPORT jstring JNICALL Java_com_potato_agent_jni_NativeRender_nInvoke(
    JNIEnv* env, jclass, jstring jTool, jstring jParams) {
    std::lock_guard<std::mutex> lock(g_renderMutex);
    if (!EnsureRenderServer()) {
        return ToJString(env, JErr("render server 初始化失敗"));
    }
    JsonValue p = JsonValue::Parse(JToUtf8(env, jParams));
    std::unordered_map<std::string, std::string> params = ToStrMap(p);
    ToolResult r = g_renderRegistry->ExecuteTool(JToUtf8(env, jTool), params);
    return ToJString(env, ToolResultToJson(r));
}

// nListTools() → [{name, description, parameters:{name:desc}}]
JNIEXPORT jstring JNICALL Java_com_potato_agent_jni_NativeRender_nListTools(
    JNIEnv* env, jclass) {
    std::lock_guard<std::mutex> lock(g_renderMutex);
    if (!EnsureRenderServer()) {
        return ToJString(env, JErr("render server 初始化失敗"));
    }
    std::string out = "[";
    bool first = true;
    for (const auto& name : g_renderRegistry->GetToolNames()) {
        Tool* t = g_renderRegistry->GetTool(name);
        if (!t) continue;
        if (!first) out += ",";
        first = false;
        out += "{\"name\":" + JQ(t->name) +
               ",\"description\":" + JQ(t->description) +
               ",\"parameters\":" + JStrMap(t->parameters) + "}";
    }
    return ToJString(env, out + "]");
}

JNIEXPORT jstring JNICALL Java_com_potato_agent_jni_NativeRender_nStats(
    JNIEnv* env, jclass) {
    std::lock_guard<std::mutex> lock(g_renderMutex);
    if (!g_renderServer) {
        return ToJString(env, JErr("render server 未初始化"));
    }
    RenderServerStats st = g_renderServer->get_stats();
    return ToJString(env,
        "{\"success\":true"
        ",\"canvasItems\":" + std::to_string(st.canvasItems) +
        ",\"sceneInstances\":" + std::to_string(st.sceneInstances) +
        ",\"drawCalls\":" + std::to_string(st.drawCalls) +
        ",\"frames\":" + std::to_string(st.frames) + "}");
}

// ============================================================================
// NativeLedger
// ============================================================================

// nCreate(loadPath)：path 非空時 LoadFromFile（驗證失敗 → <0）
JNIEXPORT jlong JNICALL Java_com_potato_agent_jni_NativeLedger_nCreate(
    JNIEnv* env, jclass, jstring jPath) {
    auto ledger = std::make_unique<AuditLedger>();
    std::string path = JToUtf8(env, jPath);
    if (!path.empty()) {
        LedgerVerifyResult vr;
        if (!ledger->LoadFromFile(path, &vr)) {
            return -(static_cast<jlong>(vr.firstBadSeq) + 1);  // 負值回報斷點
        }
    }
    return g_ledger.Put(std::move(ledger));
}

JNIEXPORT void JNICALL Java_com_potato_agent_jni_NativeLedger_nDestroy(
    JNIEnv*, jclass, jlong h) {
    g_ledger.Erase(h);
}

// nAppend(h, category, eventType, payload, provenance) → AuditRecord JSON
JNIEXPORT jstring JNICALL Java_com_potato_agent_jni_NativeLedger_nAppend(
    JNIEnv* env, jclass, jlong h, jstring jCat, jstring jType,
    jstring jPayload, jstring jProv) {
    auto lock = g_ledger.Lock();
    AuditLedger* l = g_ledger.Get(h);
    if (!l) return ToJString(env, JErr("ledger session 不存在"));

    AuditRecord rec;
    if (!l->Append(JToUtf8(env, jCat), JToUtf8(env, jType),
                   JToUtf8(env, jPayload), JToUtf8(env, jProv), &rec)) {
        return ToJString(env, JErr("Append 拒收（category/eventType/"
                                   "provenance 不得為空）"));
    }
    return ToJString(env,
        "{\"success\":true"
        ",\"seq\":" + std::to_string(rec.seq) +
        ",\"recordId\":" + JQ(rec.recordId) +
        ",\"recordHash\":" + JQ(rec.recordHash) +
        ",\"prevHash\":" + JQ(rec.prevHash) +
        ",\"payloadHash\":" + JQ(rec.payloadHash) +
        ",\"timestampUtcMs\":" + std::to_string(rec.timestampUtcMs) + "}");
}

// nAppendHash(h, category, eventType, payloadHashHex, provenance)
JNIEXPORT jstring JNICALL Java_com_potato_agent_jni_NativeLedger_nAppendHash(
    JNIEnv* env, jclass, jlong h, jstring jCat, jstring jType,
    jstring jHash, jstring jProv) {
    auto lock = g_ledger.Lock();
    AuditLedger* l = g_ledger.Get(h);
    if (!l) return ToJString(env, JErr("ledger session 不存在"));

    AuditRecord rec;
    if (!l->AppendHash(JToUtf8(env, jCat), JToUtf8(env, jType),
                       JToUtf8(env, jHash), JToUtf8(env, jProv), &rec)) {
        return ToJString(env, JErr("AppendHash 拒收（hash 需 64 hex、"
                                   "必填欄位不得為空）"));
    }
    return ToJString(env,
        "{\"success\":true"
        ",\"seq\":" + std::to_string(rec.seq) +
        ",\"recordId\":" + JQ(rec.recordId) +
        ",\"recordHash\":" + JQ(rec.recordHash) +
        ",\"prevHash\":" + JQ(rec.prevHash) +
        ",\"timestampUtcMs\":" + std::to_string(rec.timestampUtcMs) + "}");
}

JNIEXPORT jstring JNICALL Java_com_potato_agent_jni_NativeLedger_nVerify(
    JNIEnv* env, jclass, jlong h) {
    auto lock = g_ledger.Lock();
    AuditLedger* l = g_ledger.Get(h);
    if (!l) return ToJString(env, JErr("ledger session 不存在"));
    LedgerVerifyResult r = l->Verify();
    return ToJString(env,
        "{\"success\":true"
        ",\"passed\":" + JBool(r.Passed()) +
        ",\"checked\":" + std::to_string(r.checked) +
        ",\"firstBadSeq\":" + std::to_string(r.firstBadSeq) +
        ",\"reason\":" + JQ(r.reason) + "}");
}

JNIEXPORT jstring JNICALL Java_com_potato_agent_jni_NativeLedger_nHeadHash(
    JNIEnv* env, jclass, jlong h) {
    auto lock = g_ledger.Lock();
    AuditLedger* l = g_ledger.Get(h);
    if (!l) return nullptr;
    return ToJString(env, l->HeadHash());
}

JNIEXPORT jlong JNICALL Java_com_potato_agent_jni_NativeLedger_nSize(
    JNIEnv*, jclass, jlong h) {
    auto lock = g_ledger.Lock();
    AuditLedger* l = g_ledger.Get(h);
    return l ? static_cast<jlong>(l->Size()) : -1;
}

// nExport(h, layer)：0=L0_Internal 1=L1_Deidentified 2=L2_Public
JNIEXPORT jstring JNICALL Java_com_potato_agent_jni_NativeLedger_nExport(
    JNIEnv* env, jclass, jlong h, jint jLayer) {
    auto lock = g_ledger.Lock();
    AuditLedger* l = g_ledger.Get(h);
    if (!l) return ToJString(env, JErr("ledger session 不存在"));
    DisclosureLayer layer = jLayer == 1 ? DisclosureLayer::L1_Deidentified
                          : jLayer == 2 ? DisclosureLayer::L2_Public
                                        : DisclosureLayer::L0_Internal;
    return ToJString(env, l->ExportJson(layer));
}

JNIEXPORT jboolean JNICALL Java_com_potato_agent_jni_NativeLedger_nSave(
    JNIEnv* env, jclass, jlong h, jstring jPath) {
    auto lock = g_ledger.Lock();
    AuditLedger* l = g_ledger.Get(h);
    if (!l) return JNI_FALSE;
    return l->SaveToFile(JToUtf8(env, jPath)) ? JNI_TRUE : JNI_FALSE;
}

}  // extern "C"
