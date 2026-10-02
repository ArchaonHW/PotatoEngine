/**
 * Potato Engine Tool Framework
 * LangChain-style tool execution and management
 */

#pragma once

#include "LLMIntegration.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <variant>

namespace Potato {
namespace AI {

/**
 * Tool Execution Result
 */
struct ToolResult {
    bool success;
    std::string output;
    std::string error;
    std::unordered_map<std::string, std::string> metadata;
    uint64_t executionTime;
    
    ToolResult()
        : success(false)
        , executionTime(0) {
    }
};

/**
 * Tool Function Signature
 */
using ToolFunction = std::function<ToolResult(const std::unordered_map<std::string, std::string>&)>;

/**
 * Tool Definition
 */
struct Tool {
    std::string name;
    std::string description;
    std::unordered_map<std::string, std::string> parameters;  // name -> description
    ToolFunction function;
    bool enabled;
    
    Tool()
        : enabled(true) {
    }
};

/**
 * Tool Registry
 * Manages available tools
 */
class ToolRegistry {
public:
    ToolRegistry();
    ~ToolRegistry();
    
    // Tool management
    void RegisterTool(const Tool& tool);
    void UnregisterTool(const std::string& name);
    Tool* GetTool(const std::string& name);
    std::vector<std::string> GetToolNames() const;
    size_t GetToolCount() const;
    
    // Tool execution
    ToolResult ExecuteTool(
        const std::string& name,
        const std::unordered_map<std::string, std::string>& parameters);
    
    // Tool discovery
    std::vector<Tool> FindToolsByKeyword(const std::string& keyword);
    std::vector<Tool> FindToolsByDescription(const std::string& description);
    
private:
    std::unordered_map<std::string, Tool> tools;
};

/**
 * Tool Executor
 * Executes tools with safety and monitoring
 */
class ToolExecutor {
public:
    ToolExecutor(ToolRegistry* registry);
    ~ToolExecutor();
    
    // Execution
    ToolResult Execute(
        const std::string& toolName,
        const std::unordered_map<std::string, std::string>& parameters);
    
    // Execution with timeout
    ToolResult ExecuteWithTimeout(
        const std::string& toolName,
        const std::unordered_map<std::string, std::string>& parameters,
        uint64_t timeoutMs);
    
    // Validation
    bool ValidateParameters(
        const Tool& tool,
        const std::unordered_map<std::string, std::string>& parameters);
    
    // Safety
    void EnableSafetyChecks(bool enable);
    void SetMaxExecutionTime(uint64_t maxTimeMs);
    
    // Statistics
    struct ExecutorStats {
        size_t totalExecutions;
        size_t successfulExecutions;
        size_t failedExecutions;
        size_t timeoutExecutions;
        float averageExecutionTime;
    };
    ExecutorStats GetStats() const;
    void ResetStats();
    
private:
    ToolRegistry* registry;
    bool safetyChecksEnabled;
    uint64_t maxExecutionTime;
    ExecutorStats stats;
};

/**
 * Built-in Tools
 */
namespace BuiltinTools {
    // File operations
    Tool CreateReadFileTool();
    Tool CreateWriteFileTool();
    Tool CreateListDirectoryTool();
    Tool CreateDeleteFileTool();
    
    // Web operations
    Tool CreateWebSearchTool();
    Tool CreateWebFetchTool();
    Tool CreateWebAPITool();
    
    // Code operations
    Tool CreateCodeAnalysisTool();
    Tool CreateCodeGenerationTool();
    Tool CreateCodeRefactoringTool();
    
    // System operations
    Tool CreateShellCommandTool();
    Tool CreateEnvironmentVariableTool();
    
    // Data operations
    Tool CreateJSONParseTool();
    Tool CreateJSONStringifyTool();
    Tool CreateCSVParseTool();
}

/**
 * Tool Chain
 * Chains multiple tools together
 */
class ToolChain {
public:
    ToolChain(const std::string& name);
    ~ToolChain();
    
    // Chain building
    void AddTool(const std::string& toolName, const std::unordered_map<std::string, std::string>& parameters);
    void AddConditional(
        const std::string& condition,
        const std::string& trueTool,
        const std::string& falseTool);
    void AddLoop(
        const std::string& toolName,
        const std::string& loopCondition,
        int maxIterations);
    
    // Execution
    ToolResult Execute(ToolExecutor* executor);
    
    // Configuration
    void SetStopOnError(bool stop);
    void SetParallelExecution(bool enable);
    
private:
    std::string name;
    struct ChainStep {
        std::string toolName;
        std::unordered_map<std::string, std::string> parameters;
        bool isConditional;
        std::string condition;
        std::string trueTool;
        std::string falseTool;
        bool isLoop;
        std::string loopCondition;
        int maxIterations;
    };
    std::vector<ChainStep> steps;
    bool stopOnError;
    bool parallelExecution;
};

/**
 * Tool Utilities
 */
namespace ToolUtils {
    // Parameter validation
    bool ValidateRequiredParameters(
        const Tool& tool,
        const std::unordered_map<std::string, std::string>& parameters);
    
    // Result formatting
    std::string FormatToolResult(const ToolResult& result);
    std::string FormatToolError(const ToolResult& result);
    
    // JSON schema generation
    std::string GenerateParameterSchema(const Tool& tool);
    
    // Tool description formatting
    std::string FormatToolDescription(const Tool& tool);
}

} // namespace AI
} // namespace Potato
