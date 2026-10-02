/**
 * Potato Engine LLM Integration System
 * Integrates with various LLM providers (OpenAI, Anthropic, Local Models)
 */

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <future>

namespace Potato {
namespace AI {

/**
 * LLM Provider Type
 */
enum class LLMProvider {
    OpenAI,
    Anthropic,
    Local,
    Custom
};

/**
 * Message Role
 */
enum class MessageRole {
    System,
    User,
    Assistant,
    Tool
};

/**
 * Chat Message
 */
struct ChatMessage {
    MessageRole role;
    std::string content;
    std::string name;  // Optional name for the message
    
    ChatMessage()
        : role(MessageRole::User) {
    }
    
    ChatMessage(MessageRole r, const std::string& c)
        : role(r), content(c) {
    }
};

/**
 * LLM Response
 */
struct LLMResponse {
    std::string content;
    std::string finishReason;
    int promptTokens;
    int completionTokens;
    int totalTokens;
    float confidence;
    bool success;
    std::string error;
    
    LLMResponse()
        : promptTokens(0)
        , completionTokens(0)
        , totalTokens(0)
        , confidence(0.0f)
        , success(false) {
    }
};

/**
 * LLM Configuration
 */
struct LLMConfig {
    LLMProvider provider;
    std::string apiKey;
    std::string baseURL;
    std::string model;
    float temperature;
    float topP;
    int maxTokens;
    int frequencyPenalty;
    int presencePenalty;
    std::vector<std::string> stopSequences;
    bool stream;
    
    LLMConfig()
        : provider(LLMProvider::OpenAI)
        , temperature(0.7f)
        , topP(1.0f)
        , maxTokens(2048)
        , frequencyPenalty(0)
        , presencePenalty(0)
        , stream(false) {
    }
};

/**
 * Tool Call
 */
struct ToolCall {
    std::string id;
    std::string name;
    std::string arguments;  // JSON string
    
    ToolCall() {
    }
};

/**
 * Tool Definition
 */
struct ToolDefinition {
    std::string name;
    std::string description;
    std::string parametersSchema;  // JSON Schema
    
    ToolDefinition() {
    }
};

/**
 * LLM Client Interface
 */
class ILLMClient {
public:
    virtual ~ILLMClient() = default;
    
    // Basic chat completion
    virtual LLMResponse ChatCompletion(
        const std::vector<ChatMessage>& messages,
        const LLMConfig& config) = 0;
    
    // Chat completion with tools
    virtual LLMResponse ChatCompletionWithTools(
        const std::vector<ChatMessage>& messages,
        const std::vector<ToolDefinition>& tools,
        const LLMConfig& config) = 0;
    
    // Streaming chat completion
    virtual void ChatCompletionStream(
        const std::vector<ChatMessage>& messages,
        const LLMConfig& config,
        std::function<void(const std::string& chunk)> callback) = 0;
    
    // Async chat completion
    virtual std::future<LLMResponse> ChatCompletionAsync(
        const std::vector<ChatMessage>& messages,
        const LLMConfig& config) = 0;
    
    // Embedding generation
    virtual std::vector<float> GenerateEmbedding(
        const std::string& text,
        const std::string& model = "text-embedding-ada-002") = 0;
    
    // Batch embedding generation
    virtual std::vector<std::vector<float>> GenerateEmbeddings(
        const std::vector<std::string>& texts,
        const std::string& model = "text-embedding-ada-002") = 0;
    
    // Validation
    virtual bool ValidateConfig(const LLMConfig& config) = 0;
    virtual std::string GetDefaultModel() = 0;
};

/**
 * OpenAI Client
 */
class OpenAIClient : public ILLMClient {
public:
    OpenAIClient(const std::string& apiKey);
    ~OpenAIClient() override;
    
    LLMResponse ChatCompletion(
        const std::vector<ChatMessage>& messages,
        const LLMConfig& config) override;
    
    LLMResponse ChatCompletionWithTools(
        const std::vector<ChatMessage>& messages,
        const std::vector<ToolDefinition>& tools,
        const LLMConfig& config) override;
    
    void ChatCompletionStream(
        const std::vector<ChatMessage>& messages,
        const LLMConfig& config,
        std::function<void(const std::string& chunk)> callback) override;
    
    std::future<LLMResponse> ChatCompletionAsync(
        const std::vector<ChatMessage>& messages,
        const LLMConfig& config) override;
    
    std::vector<float> GenerateEmbedding(
        const std::string& text,
        const std::string& model = "text-embedding-ada-002") override;
    
    std::vector<std::vector<float>> GenerateEmbeddings(
        const std::vector<std::string>& texts,
        const std::string& model = "text-embedding-ada-002") override;
    
    bool ValidateConfig(const LLMConfig& config) override;
    std::string GetDefaultModel() override;
    
private:
    std::string apiKey;
    std::string baseURL;
    
    std::string MakeRequest(const std::string& endpoint, const std::string& jsonBody);
    LLMResponse ParseResponse(const std::string& jsonResponse);
};

/**
 * Anthropic Claude Client
 */
class AnthropicClient : public ILLMClient {
public:
    AnthropicClient(const std::string& apiKey);
    ~AnthropicClient() override;
    
    LLMResponse ChatCompletion(
        const std::vector<ChatMessage>& messages,
        const LLMConfig& config) override;
    
    LLMResponse ChatCompletionWithTools(
        const std::vector<ChatMessage>& messages,
        const std::vector<ToolDefinition>& tools,
        const LLMConfig& config) override;
    
    void ChatCompletionStream(
        const std::vector<ChatMessage>& messages,
        const LLMConfig& config,
        std::function<void(const std::string& chunk)> callback) override;
    
    std::future<LLMResponse> ChatCompletionAsync(
        const std::vector<ChatMessage>& messages,
        const LLMConfig& config) override;
    
    std::vector<float> GenerateEmbedding(
        const std::string& text,
        const std::string& model = "") override;
    
    std::vector<std::vector<float>> GenerateEmbeddings(
        const std::vector<std::string>& texts,
        const std::string& model = "") override;
    
    bool ValidateConfig(const LLMConfig& config) override;
    std::string GetDefaultModel() override;
    
private:
    std::string apiKey;
    std::string baseURL;
    
    std::string MakeRequest(const std::string& endpoint, const std::string& jsonBody);
    LLMResponse ParseResponse(const std::string& jsonResponse);
};

/**
 * Local Model Client (Ollama/Llama.cpp)
 */
class LocalModelClient : public ILLMClient {
public:
    LocalModelClient(const std::string& modelPath);
    ~LocalModelClient() override;
    
    LLMResponse ChatCompletion(
        const std::vector<ChatMessage>& messages,
        const LLMConfig& config) override;
    
    LLMResponse ChatCompletionWithTools(
        const std::vector<ChatMessage>& messages,
        const std::vector<ToolDefinition>& tools,
        const LLMConfig& config) override;
    
    void ChatCompletionStream(
        const std::vector<ChatMessage>& messages,
        const LLMConfig& config,
        std::function<void(const std::string& chunk)> callback) override;
    
    std::future<LLMResponse> ChatCompletionAsync(
        const std::vector<ChatMessage>& messages,
        const LLMConfig& config) override;
    
    std::vector<float> GenerateEmbedding(
        const std::string& text,
        const std::string& model = "") override;
    
    std::vector<std::vector<float>> GenerateEmbeddings(
        const std::vector<std::string>& texts,
        const std::string& model = "") override;
    
    bool ValidateConfig(const LLMConfig& config) override;
    std::string GetDefaultModel() override;
    
    // Model management
    bool LoadModel(const std::string& modelPath);
    void UnloadModel();
    
private:
    std::string modelPath;
    void* modelHandle;  // Opaque handle to model
    
    std::string MakeLocalRequest(const std::string& prompt);
    LLMResponse ParseLocalResponse(const std::string& response);
};

/**
 * LLM Manager
 * Manages multiple LLM clients and provides unified interface
 */
class LLMManager {
public:
    LLMManager();
    ~LLMManager();
    
    // Client management
    void RegisterClient(LLMProvider provider, std::unique_ptr<ILLMClient> client);
    void UnregisterClient(LLMProvider provider);
    ILLMClient* GetClient(LLMProvider provider);
    void SetDefaultProvider(LLMProvider provider);
    
    // Unified chat completion
    LLMResponse Chat(
        const std::vector<ChatMessage>& messages,
        const LLMConfig& config = LLMConfig());
    
    LLMResponse ChatWithTools(
        const std::vector<ChatMessage>& messages,
        const std::vector<ToolDefinition>& tools,
        const LLMConfig& config = LLMConfig());
    
    // Streaming
    void ChatStream(
        const std::vector<ChatMessage>& messages,
        const LLMConfig& config,
        std::function<void(const std::string& chunk)> callback);
    
    // Embeddings
    std::vector<float> GenerateEmbedding(
        const std::string& text,
        const std::string& model = "");
    
    std::vector<std::vector<float>> GenerateEmbeddings(
        const std::vector<std::string>& texts,
        const std::string& model = "");
    
    // Fallback
    void EnableFallback(bool enable);
    void SetFallbackProvider(LLMProvider provider);
    
    // Rate limiting
    void SetRateLimit(int requestsPerMinute);
    void EnableRateLimiting(bool enable);
    
    // Caching
    void EnableCaching(bool enable);
    void ClearCache();
    
    // Statistics
    struct LLMStats {
        size_t totalRequests;
        size_t successfulRequests;
        size_t failedRequests;
        size_t totalTokens;
        float averageResponseTime;
    };
    LLMStats GetStats() const;
    void ResetStats();
    
private:
    std::unordered_map<LLMProvider, std::unique_ptr<ILLMClient>> clients;
    LLMProvider defaultProvider;
    LLMProvider fallbackProvider;
    bool fallbackEnabled;
    
    bool rateLimitingEnabled;
    int rateLimit;
    std::vector<uint64_t> requestTimestamps;
    
    bool cachingEnabled;
    struct CacheEntry {
        std::string key;
        LLMResponse response;
        uint64_t timestamp;
    };
    std::vector<CacheEntry> cache;
    
    LLMStats stats;
    
    bool CheckRateLimit();
    std::string GenerateCacheKey(const std::vector<ChatMessage>& messages, const LLMConfig& config);
    LLMResponse* GetFromCache(const std::string& key);
    void AddToCache(const std::string& key, const LLMResponse& response);
};

/**
 * LLM Integration Utilities
 */
namespace LLMUtils {
    // Message formatting
    std::vector<ChatMessage> FormatSystemPrompt(const std::string& systemPrompt);
    std::vector<ChatMessage> AddUserMessage(const std::vector<ChatMessage>& messages, const std::string& content);
    std::vector<ChatMessage> AddAssistantMessage(const std::vector<ChatMessage>& messages, const std::string& content);
    
    // Tool formatting
    std::string FormatToolCall(const ToolCall& call);
    ToolCall ParseToolCall(const std::string& json);
    
    // Response processing
    std::string ExtractContent(const LLMResponse& response);
    std::vector<ToolCall> ExtractToolCalls(const LLMResponse& response);
    
    // Config helpers
    LLMConfig CreateOpenAIConfig(const std::string& apiKey, const std::string& model = "gpt-4");
    LLMConfig CreateAnthropicConfig(const std::string& apiKey, const std::string& model = "claude-3-opus-20240229");
    LLMConfig CreateLocalConfig(const std::string& modelPath);
    
    // Validation
    bool ValidateMessages(const std::vector<ChatMessage>& messages);
    bool ValidateToolDefinitions(const std::vector<ToolDefinition>& tools);
}

} // namespace AI
} // namespace Potato
