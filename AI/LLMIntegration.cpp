/**
 * Potato Engine LLM Integration System Implementation
 */

#include "LLMIntegration.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <thread>
#include <mutex>
#include <algorithm>
#include <regex>
#include <cstdio>
#include <cstring>

namespace Potato {
namespace AI {

// 跳脫 JSON 字串中的特殊字元，防止產生不合法 JSON 與注入
static std::string EscapeJson(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 8);
    for (char c : input) {
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

// ============================================================================
// OpenAI Client Implementation
// ============================================================================

OpenAIClient::OpenAIClient(const std::string& key)
    : apiKey(key)
    , baseURL("https://api.openai.com/v1") {
}

OpenAIClient::~OpenAIClient() {
}

LLMResponse OpenAIClient::ChatCompletion(
    const std::vector<ChatMessage>& messages,
    const LLMConfig& config) {
    
    LLMResponse response;
    
    // Build JSON request
    std::stringstream json;
    json << "{";
    json << "\"model\":\"" << EscapeJson(config.model) << "\",";
    json << "\"messages\":[";
    
    for (size_t i = 0; i < messages.size(); i++) {
        if (i > 0) json << ",";
        json << "{";
        json << "\"role\":\"";
        switch (messages[i].role) {
            case MessageRole::System: json << "system"; break;
            case MessageRole::User: json << "user"; break;
            case MessageRole::Assistant: json << "assistant"; break;
            case MessageRole::Tool: json << "tool"; break;
        }
        json << "\",";
        json << "\"content\":\"" << EscapeJson(messages[i].content) << "\"";
        json << "}";
    }
    
    json << "],";
    json << "\"temperature\":" << config.temperature << ",";
    json << "\"max_tokens\":" << config.maxTokens << ",";
    json << "\"top_p\":" << config.topP;
    json << "}";
    
    // Make request (placeholder - would use HTTP client in real implementation)
    std::string jsonResponse = MakeRequest("/chat/completions", json.str());
    response = ParseResponse(jsonResponse);
    
    return response;
}

LLMResponse OpenAIClient::ChatCompletionWithTools(
    const std::vector<ChatMessage>& messages,
    const std::vector<ToolDefinition>& tools,
    const LLMConfig& config) {
    
    LLMResponse response;
    
    // Build JSON request with tools
    std::stringstream json;
    json << "{";
    json << "\"model\":\"" << EscapeJson(config.model) << "\",";
    json << "\"messages\":[";
    
    for (size_t i = 0; i < messages.size(); i++) {
        if (i > 0) json << ",";
        json << "{";
        json << "\"role\":\"";
        switch (messages[i].role) {
            case MessageRole::System: json << "system"; break;
            case MessageRole::User: json << "user"; break;
            case MessageRole::Assistant: json << "assistant"; break;
            case MessageRole::Tool: json << "tool"; break;
        }
        json << "\",";
        json << "\"content\":\"" << EscapeJson(messages[i].content) << "\"";
        json << "}";
    }
    
    json << "],";
    json << "\"tools\":[";
    
    for (size_t i = 0; i < tools.size(); i++) {
        if (i > 0) json << ",";
        json << "{";
        json << "\"type\":\"function\",";
        json << "\"function\":{";
        json << "\"name\":\"" << EscapeJson(tools[i].name) << "\",";
        json << "\"description\":\"" << EscapeJson(tools[i].description) << "\",";
        json << "\"parameters\":" << tools[i].parametersSchema;
        json << "}";
        json << "}";
    }
    
    json << "],";
    json << "\"temperature\":" << config.temperature << ",";
    json << "\"max_tokens\":" << config.maxTokens;
    json << "}";
    
    std::string jsonResponse = MakeRequest("/chat/completions", json.str());
    response = ParseResponse(jsonResponse);
    
    return response;
}

void OpenAIClient::ChatCompletionStream(
    const std::vector<ChatMessage>& messages,
    const LLMConfig& config,
    std::function<void(const std::string& chunk)> callback) {
    
    // Placeholder for streaming implementation
    // In real implementation, would use HTTP streaming
    LLMResponse response = ChatCompletion(messages, config);
    if (response.success) {
        callback(response.content);
    }
}

std::future<LLMResponse> OpenAIClient::ChatCompletionAsync(
    const std::vector<ChatMessage>& messages,
    const LLMConfig& config) {
    
    return std::async(std::launch::async, [this, messages, config]() {
        return ChatCompletion(messages, config);
    });
}

std::vector<float> OpenAIClient::GenerateEmbedding(
    const std::string& text,
    const std::string& model) {
    
    // Placeholder - would call OpenAI embedding API
    // Return dummy embedding for now
    std::vector<float> embedding(1536, 0.0f);  // OpenAI ada-002 dimension
    for (size_t i = 0; i < embedding.size(); i++) {
        embedding[i] = static_cast<float>(rand()) / RAND_MAX;
    }
    return embedding;
}

std::vector<std::vector<float>> OpenAIClient::GenerateEmbeddings(
    const std::vector<std::string>& texts,
    const std::string& model) {
    
    std::vector<std::vector<float>> embeddings;
    for (const auto& text : texts) {
        embeddings.push_back(GenerateEmbedding(text, model));
    }
    return embeddings;
}

bool OpenAIClient::ValidateConfig(const LLMConfig& config) {
    return !config.apiKey.empty() && !config.model.empty();
}

std::string OpenAIClient::GetDefaultModel() {
    return "gpt-4";
}

std::string OpenAIClient::MakeRequest(const std::string& endpoint, const std::string& jsonBody) {
    // Placeholder - would use HTTP client (curl, libcurl, etc.)
    // For now, return a mock response
    return R"({
        "choices": [{
            "message": {
                "role": "assistant",
                "content": "This is a simulated response from OpenAI API."
            },
            "finish_reason": "stop"
        }],
        "usage": {
            "prompt_tokens": 10,
            "completion_tokens": 20,
            "total_tokens": 30
        }
    })";
}

LLMResponse OpenAIClient::ParseResponse(const std::string& jsonResponse) {
    LLMResponse response;
    
    // Placeholder - would parse JSON properly
    // For now, set mock values
    response.content = "This is a simulated response from OpenAI API.";
    response.success = true;
    response.promptTokens = 10;
    response.completionTokens = 20;
    response.totalTokens = 30;
    response.finishReason = "stop";
    
    return response;
}

// ============================================================================
// Anthropic Client Implementation
// ============================================================================

AnthropicClient::AnthropicClient(const std::string& key)
    : apiKey(key)
    , baseURL("https://api.anthropic.com") {
}

AnthropicClient::~AnthropicClient() {
}

LLMResponse AnthropicClient::ChatCompletion(
    const std::vector<ChatMessage>& messages,
    const LLMConfig& config) {
    
    LLMResponse response;
    
    // Build JSON request for Anthropic API
    std::stringstream json;
    json << "{";
    json << "\"model\":\"" << EscapeJson(config.model) << "\",";
    json << "\"max_tokens\":" << config.maxTokens << ",";
    json << "\"messages\":[";
    
    for (size_t i = 0; i < messages.size(); i++) {
        if (i > 0) json << ",";
        json << "{";
        json << "\"role\":\"";
        switch (messages[i].role) {
            case MessageRole::User: json << "user"; break;
            case MessageRole::Assistant: json << "assistant"; break;
            default: json << "user"; break;
        }
        json << "\",";
        json << "\"content\":\"" << EscapeJson(messages[i].content) << "\"";
        json << "}";
    }
    
    json << "]}";
    
    std::string jsonResponse = MakeRequest("/v1/messages", json.str());
    response = ParseResponse(jsonResponse);
    
    return response;
}

LLMResponse AnthropicClient::ChatCompletionWithTools(
    const std::vector<ChatMessage>& messages,
    const std::vector<ToolDefinition>& tools,
    const LLMConfig& config) {
    
    // Anthropic tool calling implementation
    return ChatCompletion(messages, config);
}

void AnthropicClient::ChatCompletionStream(
    const std::vector<ChatMessage>& messages,
    const LLMConfig& config,
    std::function<void(const std::string& chunk)> callback) {
    
    LLMResponse response = ChatCompletion(messages, config);
    if (response.success) {
        callback(response.content);
    }
}

std::future<LLMResponse> AnthropicClient::ChatCompletionAsync(
    const std::vector<ChatMessage>& messages,
    const LLMConfig& config) {
    
    return std::async(std::launch::async, [this, messages, config]() {
        return ChatCompletion(messages, config);
    });
}

std::vector<float> AnthropicClient::GenerateEmbedding(
    const std::string& text,
    const std::string& model) {
    
    // Anthropic doesn't have a public embedding API yet
    // Return dummy embedding
    std::vector<float> embedding(1536, 0.0f);
    for (size_t i = 0; i < embedding.size(); i++) {
        embedding[i] = static_cast<float>(rand()) / RAND_MAX;
    }
    return embedding;
}

std::vector<std::vector<float>> AnthropicClient::GenerateEmbeddings(
    const std::vector<std::string>& texts,
    const std::string& model) {
    
    std::vector<std::vector<float>> embeddings;
    for (const auto& text : texts) {
        embeddings.push_back(GenerateEmbedding(text, model));
    }
    return embeddings;
}

bool AnthropicClient::ValidateConfig(const LLMConfig& config) {
    return !config.apiKey.empty() && !config.model.empty();
}

std::string AnthropicClient::GetDefaultModel() {
    return "claude-3-opus-20240229";
}

std::string AnthropicClient::MakeRequest(const std::string& endpoint, const std::string& jsonBody) {
    // Placeholder for HTTP request
    return R"({
        "content": [{
            "type": "text",
            "text": "This is a simulated response from Anthropic API."
        }],
        "stop_reason": "end_turn"
    })";
}

LLMResponse AnthropicClient::ParseResponse(const std::string& jsonResponse) {
    LLMResponse response;
    response.content = "This is a simulated response from Anthropic API.";
    response.success = true;
    response.promptTokens = 10;
    response.completionTokens = 20;
    response.totalTokens = 30;
    response.finishReason = "end_turn";
    return response;
}

// ============================================================================
// Local Model Client Implementation
// ============================================================================

LocalModelClient::LocalModelClient(const std::string& path)
    : modelPath(path)
    , modelHandle(nullptr) {
}

LocalModelClient::~LocalModelClient() {
    UnloadModel();
}

LLMResponse LocalModelClient::ChatCompletion(
    const std::vector<ChatMessage>& messages,
    const LLMConfig& config) {
    
    LLMResponse response;
    
    // Build prompt from messages
    std::stringstream prompt;
    for (const auto& msg : messages) {
        switch (msg.role) {
            case MessageRole::System:
                prompt << "System: " << msg.content << "\n";
                break;
            case MessageRole::User:
                prompt << "User: " << msg.content << "\n";
                break;
            case MessageRole::Assistant:
                prompt << "Assistant: " << msg.content << "\n";
                break;
            default:
                break;
        }
    }
    prompt << "Assistant:";
    
    std::string localResponse = MakeLocalRequest(prompt.str());
    response = ParseLocalResponse(localResponse);
    
    return response;
}

LLMResponse LocalModelClient::ChatCompletionWithTools(
    const std::vector<ChatMessage>& messages,
    const std::vector<ToolDefinition>& tools,
    const LLMConfig& config) {
    
    return ChatCompletion(messages, config);
}

void LocalModelClient::ChatCompletionStream(
    const std::vector<ChatMessage>& messages,
    const LLMConfig& config,
    std::function<void(const std::string& chunk)> callback) {
    
    LLMResponse response = ChatCompletion(messages, config);
    if (response.success) {
        callback(response.content);
    }
}

std::future<LLMResponse> LocalModelClient::ChatCompletionAsync(
    const std::vector<ChatMessage>& messages,
    const LLMConfig& config) {
    
    return std::async(std::launch::async, [this, messages, config]() {
        return ChatCompletion(messages, config);
    });
}

std::vector<float> LocalModelClient::GenerateEmbedding(
    const std::string& text,
    const std::string& model) {
    
    // Local model embedding
    std::vector<float> embedding(768, 0.0f);  // Common dimension
    for (size_t i = 0; i < embedding.size(); i++) {
        embedding[i] = static_cast<float>(rand()) / RAND_MAX;
    }
    return embedding;
}

std::vector<std::vector<float>> LocalModelClient::GenerateEmbeddings(
    const std::vector<std::string>& texts,
    const std::string& model) {
    
    std::vector<std::vector<float>> embeddings;
    for (const auto& text : texts) {
        embeddings.push_back(GenerateEmbedding(text, model));
    }
    return embeddings;
}

bool LocalModelClient::ValidateConfig(const LLMConfig& config) {
    return !config.model.empty();
}

std::string LocalModelClient::GetDefaultModel() {
    return "llama-2-7b";
}

bool LocalModelClient::LoadModel(const std::string& modelPath) {
    // Placeholder for model loading
    // Would use llama.cpp or similar
    modelHandle = reinterpret_cast<void*>(1);  // Dummy handle
    return true;
}

void LocalModelClient::UnloadModel() {
    if (modelHandle) {
        // Placeholder for model unloading
        modelHandle = nullptr;
    }
}

std::string LocalModelClient::MakeLocalRequest(const std::string& prompt) {
    // Placeholder for local model inference
    // Would call llama.cpp or Ollama API
    return "This is a simulated response from local model.";
}

LLMResponse LocalModelClient::ParseLocalResponse(const std::string& response) {
    LLMResponse resp;
    resp.content = response;
    resp.success = true;
    resp.promptTokens = 10;
    resp.completionTokens = 20;
    resp.totalTokens = 30;
    resp.finishReason = "stop";
    return resp;
}

// ============================================================================
// LLM Manager Implementation
// ============================================================================

LLMManager::LLMManager()
    : defaultProvider(LLMProvider::OpenAI)
    , fallbackProvider(LLMProvider::Anthropic)
    , fallbackEnabled(false)
    , rateLimitingEnabled(false)
    , rateLimit(60)
    , cachingEnabled(false) {
    
    memset(&stats, 0, sizeof(stats));
}

LLMManager::~LLMManager() {
}

void LLMManager::RegisterClient(LLMProvider provider, std::unique_ptr<ILLMClient> client) {
    clients[provider] = std::move(client);
}

void LLMManager::UnregisterClient(LLMProvider provider) {
    clients.erase(provider);
}

ILLMClient* LLMManager::GetClient(LLMProvider provider) {
    auto it = clients.find(provider);
    if (it != clients.end()) {
        return it->second.get();
    }
    return nullptr;
}

void LLMManager::SetDefaultProvider(LLMProvider provider) {
    defaultProvider = provider;
}

LLMResponse LLMManager::Chat(
    const std::vector<ChatMessage>& messages,
    const LLMConfig& config) {
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    stats.totalRequests++;
    
    // Check rate limit
    if (rateLimitingEnabled && !CheckRateLimit()) {
        LLMResponse response;
        response.success = false;
        response.error = "Rate limit exceeded";
        stats.failedRequests++;
        return response;
    }
    
    // Check cache
    if (cachingEnabled) {
        std::string cacheKey = GenerateCacheKey(messages, config);
        LLMResponse* cached = GetFromCache(cacheKey);
        if (cached) {
            stats.successfulRequests++;
            return *cached;
        }
    }
    
    // Get client
    ILLMClient* client = GetClient(defaultProvider);
    if (!client) {
        stats.failedRequests++;
        LLMResponse response;
        response.success = false;
        response.error = "No client available for default provider";
        return response;
    }
    
    // Make request
    LLMResponse response = client->ChatCompletion(messages, config);
    
    // Fallback if enabled and failed
    if (!response.success && fallbackEnabled) {
        ILLMClient* fallbackClient = GetClient(fallbackProvider);
        if (fallbackClient) {
            response = fallbackClient->ChatCompletion(messages, config);
        }
    }
    
    // Update stats
    if (response.success) {
        stats.successfulRequests++;
        stats.totalTokens += response.totalTokens;
        
        // Add to cache
        if (cachingEnabled) {
            std::string cacheKey = GenerateCacheKey(messages, config);
            AddToCache(cacheKey, response);
        }
    } else {
        stats.failedRequests++;
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    stats.averageResponseTime = (stats.averageResponseTime * (stats.totalRequests - 1) + duration.count() / 1000.0f) / stats.totalRequests;
    
    return response;
}

LLMResponse LLMManager::ChatWithTools(
    const std::vector<ChatMessage>& messages,
    const std::vector<ToolDefinition>& tools,
    const LLMConfig& config) {
    
    ILLMClient* client = GetClient(defaultProvider);
    if (!client) {
        LLMResponse response;
        response.success = false;
        response.error = "No client available";
        return response;
    }
    
    return client->ChatCompletionWithTools(messages, tools, config);
}

void LLMManager::ChatStream(
    const std::vector<ChatMessage>& messages,
    const LLMConfig& config,
    std::function<void(const std::string& chunk)> callback) {
    
    ILLMClient* client = GetClient(defaultProvider);
    if (client) {
        client->ChatCompletionStream(messages, config, callback);
    }
}

std::vector<float> LLMManager::GenerateEmbedding(
    const std::string& text,
    const std::string& model) {
    
    ILLMClient* client = GetClient(defaultProvider);
    if (client) {
        return client->GenerateEmbedding(text, model);
    }
    return std::vector<float>();
}

std::vector<std::vector<float>> LLMManager::GenerateEmbeddings(
    const std::vector<std::string>& texts,
    const std::string& model) {
    
    ILLMClient* client = GetClient(defaultProvider);
    if (client) {
        return client->GenerateEmbeddings(texts, model);
    }
    return std::vector<std::vector<float>>();
}

void LLMManager::EnableFallback(bool enable) {
    fallbackEnabled = enable;
}

void LLMManager::SetFallbackProvider(LLMProvider provider) {
    fallbackProvider = provider;
}

void LLMManager::SetRateLimit(int requestsPerMinute) {
    rateLimit = requestsPerMinute;
}

void LLMManager::EnableRateLimiting(bool enable) {
    rateLimitingEnabled = enable;
}

void LLMManager::EnableCaching(bool enable) {
    cachingEnabled = enable;
}

void LLMManager::ClearCache() {
    cache.clear();
}

LLMManager::LLMStats LLMManager::GetStats() const {
    return stats;
}

void LLMManager::ResetStats() {
    memset(&stats, 0, sizeof(stats));
}

bool LLMManager::CheckRateLimit() {
    auto now = std::chrono::system_clock::now();
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    
    // Remove old timestamps (older than 1 minute)
    requestTimestamps.erase(
        std::remove_if(requestTimestamps.begin(), requestTimestamps.end(),
            [now_ms](uint64_t ts) { return now_ms - ts > 60000; }),
        requestTimestamps.end());
    
    // Check if under limit
    if (requestTimestamps.size() >= static_cast<size_t>(rateLimit)) {
        return false;
    }
    
    requestTimestamps.push_back(now_ms);
    return true;
}

std::string LLMManager::GenerateCacheKey(const std::vector<ChatMessage>& messages, const LLMConfig& config) {
    std::stringstream key;
    key << config.model << "|";
    for (const auto& msg : messages) {
        key << static_cast<int>(msg.role) << ":" << msg.content << "|";
    }
    return key.str();
}

LLMResponse* LLMManager::GetFromCache(const std::string& key) {
    auto now = std::chrono::system_clock::now();
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    
    for (auto& entry : cache) {
        if (entry.key == key && (now_ms - entry.timestamp) < 3600000) {  // 1 hour TTL
            return &entry.response;
        }
    }
    return nullptr;
}

void LLMManager::AddToCache(const std::string& key, const LLMResponse& response) {
    auto now = std::chrono::system_clock::now();
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    
    CacheEntry entry;
    entry.key = key;
    entry.response = response;
    entry.timestamp = now_ms;
    
    cache.push_back(entry);
    
    // Limit cache size
    if (cache.size() > 1000) {
        cache.erase(cache.begin());
    }
}

// ============================================================================
// LLM Utilities Implementation
// ============================================================================

namespace LLMUtils {

std::vector<ChatMessage> FormatSystemPrompt(const std::string& systemPrompt) {
    std::vector<ChatMessage> messages;
    messages.emplace_back(MessageRole::System, systemPrompt);
    return messages;
}

std::vector<ChatMessage> AddUserMessage(const std::vector<ChatMessage>& messages, const std::string& content) {
    auto result = messages;
    result.emplace_back(MessageRole::User, content);
    return result;
}

std::vector<ChatMessage> AddAssistantMessage(const std::vector<ChatMessage>& messages, const std::string& content) {
    auto result = messages;
    result.emplace_back(MessageRole::Assistant, content);
    return result;
}

std::string FormatToolCall(const ToolCall& call) {
    std::stringstream ss;
    ss << "{\"id\":\"" << EscapeJson(call.id) << "\",\"name\":\"" << EscapeJson(call.name) << "\",\"arguments\":" << call.arguments << "}";
    return ss.str();
}

ToolCall ParseToolCall(const std::string& json) {
    ToolCall call;
    // Placeholder for JSON parsing
    return call;
}

std::string ExtractContent(const LLMResponse& response) {
    return response.content;
}

std::vector<ToolCall> ExtractToolCalls(const LLMResponse& response) {
    // Placeholder for tool call extraction
    return std::vector<ToolCall>();
}

LLMConfig CreateOpenAIConfig(const std::string& apiKey, const std::string& model) {
    LLMConfig config;
    config.provider = LLMProvider::OpenAI;
    config.apiKey = apiKey;
    config.model = model;
    return config;
}

LLMConfig CreateAnthropicConfig(const std::string& apiKey, const std::string& model) {
    LLMConfig config;
    config.provider = LLMProvider::Anthropic;
    config.apiKey = apiKey;
    config.model = model;
    return config;
}

LLMConfig CreateLocalConfig(const std::string& modelPath) {
    LLMConfig config;
    config.provider = LLMProvider::Local;
    config.model = modelPath;
    return config;
}

bool ValidateMessages(const std::vector<ChatMessage>& messages) {
    return !messages.empty();
}

bool ValidateToolDefinitions(const std::vector<ToolDefinition>& tools) {
    for (const auto& tool : tools) {
        if (tool.name.empty() || tool.description.empty()) {
            return false;
        }
    }
    return true;
}

} // namespace LLMUtils

} // namespace AI
} // namespace Potato
