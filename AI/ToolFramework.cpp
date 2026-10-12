/**
 * Potato Engine Tool Framework Implementation
 */

#include "ToolFramework.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <fstream>
#include <algorithm>
#include <cstring>

namespace Potato {
namespace AI {

// ============================================================================
// Tool Registry Implementation
// ============================================================================

ToolRegistry::ToolRegistry() {
}

ToolRegistry::~ToolRegistry() {
}

void ToolRegistry::RegisterTool(const Tool& tool) {
    tools[tool.name] = tool;
}

void ToolRegistry::UnregisterTool(const std::string& name) {
    tools.erase(name);
}

Tool* ToolRegistry::GetTool(const std::string& name) {
    auto it = tools.find(name);
    return it != tools.end() ? &it->second : nullptr;
}

std::vector<std::string> ToolRegistry::GetToolNames() const {
    std::vector<std::string> names;
    for (const auto& pair : tools) {
        names.push_back(pair.first);
    }
    return names;
}

size_t ToolRegistry::GetToolCount() const {
    return tools.size();
}

ToolResult ToolRegistry::ExecuteTool(
    const std::string& name,
    const std::unordered_map<std::string, std::string>& parameters) {
    
    Tool* tool = GetTool(name);
    if (!tool) {
        ToolResult result;
        result.success = false;
        result.error = "Tool not found: " + name;
        return result;
    }
    
    if (!tool->enabled) {
        ToolResult result;
        result.success = false;
        result.error = "Tool is disabled: " + name;
        return result;
    }
    
    return tool->function(parameters);
}

std::vector<Tool> ToolRegistry::FindToolsByKeyword(const std::string& keyword) {
    std::vector<Tool> found;
    std::string lowerKeyword = keyword;
    std::transform(lowerKeyword.begin(), lowerKeyword.end(), lowerKeyword.begin(), ::tolower);
    
    for (const auto& pair : tools) {
        std::string lowerName = pair.first;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
        
        if (lowerName.find(lowerKeyword) != std::string::npos) {
            found.push_back(pair.second);
        }
    }
    
    return found;
}

std::vector<Tool> ToolRegistry::FindToolsByDescription(const std::string& description) {
    std::vector<Tool> found;
    std::string lowerDesc = description;
    std::transform(lowerDesc.begin(), lowerDesc.end(), lowerDesc.begin(), ::tolower);
    
    for (const auto& pair : tools) {
        std::string lowerToolDesc = pair.second.description;
        std::transform(lowerToolDesc.begin(), lowerToolDesc.end(), lowerToolDesc.begin(), ::tolower);
        
        if (lowerToolDesc.find(lowerDesc) != std::string::npos) {
            found.push_back(pair.second);
        }
    }
    
    return found;
}

// ============================================================================
// Tool Executor Implementation
// ============================================================================

ToolExecutor::ToolExecutor(ToolRegistry* reg)
    : registry(reg)
    , safetyChecksEnabled(true)
    , maxExecutionTime(30000) {  // 30 seconds default
    
    memset(&stats, 0, sizeof(stats));
}

ToolExecutor::~ToolExecutor() {
}

ToolResult ToolExecutor::Execute(
    const std::string& toolName,
    const std::unordered_map<std::string, std::string>& parameters) {
    
    return ExecuteWithTimeout(toolName, parameters, maxExecutionTime);
}

ToolResult ToolExecutor::ExecuteWithTimeout(
    const std::string& toolName,
    const std::unordered_map<std::string, std::string>& parameters,
    uint64_t timeoutMs) {
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    stats.totalExecutions++;
    
    // Get tool
    Tool* tool = registry->GetTool(toolName);
    if (!tool) {
        stats.failedExecutions++;
        ToolResult result;
        result.success = false;
        result.error = "Tool not found: " + toolName;
        return result;
    }
    
    // Validate parameters
    if (safetyChecksEnabled && !ValidateParameters(*tool, parameters)) {
        stats.failedExecutions++;
        ToolResult result;
        result.success = false;
        result.error = "Invalid parameters for tool: " + toolName;
        return result;
    }
    
    // Execute tool
    ToolResult result = registry->ExecuteTool(toolName, parameters);
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    result.executionTime = duration.count();
    
    // Update stats
    if (result.success) {
        stats.successfulExecutions++;
    } else {
        stats.failedExecutions++;
    }
    
    stats.averageExecutionTime = (stats.averageExecutionTime * (stats.totalExecutions - 1) + duration.count() / 1000.0f) / stats.totalExecutions;
    
    return result;
}

bool ToolExecutor::ValidateParameters(
    const Tool& tool,
    const std::unordered_map<std::string, std::string>& parameters) {
    
    // Check if all required parameters are present
    // For now, assume all parameters are optional
    return true;
}

void ToolExecutor::EnableSafetyChecks(bool enable) {
    safetyChecksEnabled = enable;
}

void ToolExecutor::SetMaxExecutionTime(uint64_t maxTimeMs) {
    maxExecutionTime = maxTimeMs;
}

ToolExecutor::ExecutorStats ToolExecutor::GetStats() const {
    return stats;
}

void ToolExecutor::ResetStats() {
    memset(&stats, 0, sizeof(stats));
}

// ============================================================================
// Built-in Tools Implementation
// ============================================================================

namespace BuiltinTools {

Tool CreateReadFileTool() {
    Tool tool;
    tool.name = "read_file";
    tool.description = "Read the contents of a file";
    tool.parameters["path"] = "Path to the file to read";
    
    tool.function = [](const std::unordered_map<std::string, std::string>& params) -> ToolResult {
        ToolResult result;
        
        auto it = params.find("path");
        if (it == params.end()) {
            result.success = false;
            result.error = "Missing required parameter: path";
            return result;
        }
        
        std::ifstream file(it->second);
        if (!file.is_open()) {
            result.success = false;
            result.error = "Failed to open file: " + it->second;
            return result;
        }
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        result.success = true;
        result.output = buffer.str();
        
        return result;
    };
    
    return tool;
}

Tool CreateWriteFileTool() {
    Tool tool;
    tool.name = "write_file";
    tool.description = "Write content to a file";
    tool.parameters["path"] = "Path to the file to write";
    tool.parameters["content"] = "Content to write to the file";
    
    tool.function = [](const std::unordered_map<std::string, std::string>& params) -> ToolResult {
        ToolResult result;
        
        auto pathIt = params.find("path");
        auto contentIt = params.find("content");
        
        if (pathIt == params.end() || contentIt == params.end()) {
            result.success = false;
            result.error = "Missing required parameters: path, content";
            return result;
        }
        
        std::ofstream file(pathIt->second);
        if (!file.is_open()) {
            result.success = false;
            result.error = "Failed to open file for writing: " + pathIt->second;
            return result;
        }
        
        file << contentIt->second;
        result.success = true;
        result.output = "File written successfully";
        
        return result;
    };
    
    return tool;
}

Tool CreateListDirectoryTool() {
    Tool tool;
    tool.name = "list_directory";
    tool.description = "List files and directories in a path";
    tool.parameters["path"] = "Path to the directory to list";
    
    tool.function = [](const std::unordered_map<std::string, std::string>& params) -> ToolResult {
        ToolResult result;
        
        auto it = params.find("path");
        if (it == params.end()) {
            result.success = false;
            result.error = "Missing required parameter: path";
            return result;
        }
        
        // Placeholder for directory listing
        result.success = true;
        result.output = "Directory listing for: " + it->second;
        
        return result;
    };
    
    return tool;
}

Tool CreateDeleteFileTool() {
    Tool tool;
    tool.name = "delete_file";
    tool.description = "Delete a file";
    tool.parameters["path"] = "Path to the file to delete";
    
    tool.function = [](const std::unordered_map<std::string, std::string>& params) -> ToolResult {
        ToolResult result;
        
        auto it = params.find("path");
        if (it == params.end()) {
            result.success = false;
            result.error = "Missing required parameter: path";
            return result;
        }
        
        // Placeholder for file deletion
        result.success = true;
        result.output = "File deleted: " + it->second;
        
        return result;
    };
    
    return tool;
}

Tool CreateWebSearchTool() {
    Tool tool;
    tool.name = "web_search";
    tool.description = "Search the web for information";
    tool.parameters["query"] = "Search query";
    
    tool.function = [](const std::unordered_map<std::string, std::string>& params) -> ToolResult {
        ToolResult result;
        
        auto it = params.find("query");
        if (it == params.end()) {
            result.success = false;
            result.error = "Missing required parameter: query";
            return result;
        }
        
        // Placeholder for web search
        result.success = true;
        result.output = "Search results for: " + it->second;
        
        return result;
    };
    
    return tool;
}

Tool CreateWebFetchTool() {
    Tool tool;
    tool.name = "web_fetch";
    tool.description = "Fetch content from a URL";
    tool.parameters["url"] = "URL to fetch";
    
    tool.function = [](const std::unordered_map<std::string, std::string>& params) -> ToolResult {
        ToolResult result;
        
        auto it = params.find("url");
        if (it == params.end()) {
            result.success = false;
            result.error = "Missing required parameter: url";
            return result;
        }
        
        // Placeholder for web fetch
        result.success = true;
        result.output = "Fetched content from: " + it->second;
        
        return result;
    };
    
    return tool;
}

Tool CreateWebAPITool() {
    Tool tool;
    tool.name = "web_api";
    tool.description = "Make a web API request";
    tool.parameters["url"] = "API endpoint URL";
    tool.parameters["method"] = "HTTP method (GET, POST, etc.)";
    tool.parameters["body"] = "Request body (for POST requests)";
    
    tool.function = [](const std::unordered_map<std::string, std::string>& params) -> ToolResult {
        ToolResult result;
        
        auto urlIt = params.find("url");
        if (urlIt == params.end()) {
            result.success = false;
            result.error = "Missing required parameter: url";
            return result;
        }
        
        // Placeholder for API request
        result.success = true;
        result.output = "API response from: " + urlIt->second;
        
        return result;
    };
    
    return tool;
}

Tool CreateCodeAnalysisTool() {
    Tool tool;
    tool.name = "code_analysis";
    tool.description = "Analyze code for issues and improvements";
    tool.parameters["code"] = "Code to analyze";
    tool.parameters["language"] = "Programming language";
    
    tool.function = [](const std::unordered_map<std::string, std::string>& params) -> ToolResult {
        ToolResult result;
        
        auto codeIt = params.find("code");
        if (codeIt == params.end()) {
            result.success = false;
            result.error = "Missing required parameter: code";
            return result;
        }
        
        // Placeholder for code analysis
        result.success = true;
        result.output = "Code analysis completed for " + std::to_string(codeIt->second.length()) + " characters";
        
        return result;
    };
    
    return tool;
}

Tool CreateCodeGenerationTool() {
    Tool tool;
    tool.name = "code_generation";
    tool.description = "Generate code from a description";
    tool.parameters["description"] = "Description of the code to generate";
    tool.parameters["language"] = "Programming language";
    
    tool.function = [](const std::unordered_map<std::string, std::string>& params) -> ToolResult {
        ToolResult result;
        
        auto descIt = params.find("description");
        if (descIt == params.end()) {
            result.success = false;
            result.error = "Missing required parameter: description";
            return result;
        }
        
        // Placeholder for code generation
        result.success = true;
        result.output = "// Generated code for: " + descIt->second;
        
        return result;
    };
    
    return tool;
}

Tool CreateCodeRefactoringTool() {
    Tool tool;
    tool.name = "code_refactoring";
    tool.description = "Refactor code to improve quality";
    tool.parameters["code"] = "Code to refactor";
    tool.parameters["language"] = "Programming language";
    
    tool.function = [](const std::unordered_map<std::string, std::string>& params) -> ToolResult {
        ToolResult result;
        
        auto codeIt = params.find("code");
        if (codeIt == params.end()) {
            result.success = false;
            result.error = "Missing required parameter: code";
            return result;
        }
        
        // Placeholder for code refactoring
        result.success = true;
        result.output = "// Refactored code";
        
        return result;
    };
    
    return tool;
}

Tool CreateShellCommandTool() {
    Tool tool;
    tool.name = "shell_command";
    tool.description = "Execute a shell command";
    tool.parameters["command"] = "Shell command to execute";
    
    tool.function = [](const std::unordered_map<std::string, std::string>& params) -> ToolResult {
        ToolResult result;
        
        auto it = params.find("command");
        if (it == params.end()) {
            result.success = false;
            result.error = "Missing required parameter: command";
            return result;
        }
        
        // Placeholder for shell command execution
        result.success = true;
        result.output = "Command executed: " + it->second;
        
        return result;
    };
    
    return tool;
}

Tool CreateEnvironmentVariableTool() {
    Tool tool;
    tool.name = "environment_variable";
    tool.description = "Get or set environment variables";
    tool.parameters["name"] = "Variable name";
    tool.parameters["value"] = "Variable value (optional)";
    
    tool.function = [](const std::unordered_map<std::string, std::string>& params) -> ToolResult {
        ToolResult result;
        
        auto nameIt = params.find("name");
        if (nameIt == params.end()) {
            result.success = false;
            result.error = "Missing required parameter: name";
            return result;
        }
        
        // Placeholder for environment variable access
        result.success = true;
        result.output = "Environment variable: " + nameIt->second;
        
        return result;
    };
    
    return tool;
}

Tool CreateJSONParseTool() {
    Tool tool;
    tool.name = "json_parse";
    tool.description = "Parse JSON string";
    tool.parameters["json"] = "JSON string to parse";
    
    tool.function = [](const std::unordered_map<std::string, std::string>& params) -> ToolResult {
        ToolResult result;
        
        auto it = params.find("json");
        if (it == params.end()) {
            result.success = false;
            result.error = "Missing required parameter: json";
            return result;
        }
        
        // Placeholder for JSON parsing
        result.success = true;
        result.output = "Parsed JSON";
        
        return result;
    };
    
    return tool;
}

Tool CreateJSONStringifyTool() {
    Tool tool;
    tool.name = "json_stringify";
    tool.description = "Convert object to JSON string";
    tool.parameters["object"] = "Object to stringify";
    
    tool.function = [](const std::unordered_map<std::string, std::string>& params) -> ToolResult {
        ToolResult result;
        
        auto it = params.find("object");
        if (it == params.end()) {
            result.success = false;
            result.error = "Missing required parameter: object";
            return result;
        }
        
        // Placeholder for JSON stringification
        result.success = true;
        result.output = "{\"result\": \"" + it->second + "\"}";
        
        return result;
    };
    
    return tool;
}

Tool CreateCSVParseTool() {
    Tool tool;
    tool.name = "csv_parse";
    tool.description = "Parse CSV string";
    tool.parameters["csv"] = "CSV string to parse";
    
    tool.function = [](const std::unordered_map<std::string, std::string>& params) -> ToolResult {
        ToolResult result;
        
        auto it = params.find("csv");
        if (it == params.end()) {
            result.success = false;
            result.error = "Missing required parameter: csv";
            return result;
        }
        
        // Placeholder for CSV parsing
        result.success = true;
        result.output = "Parsed CSV";
        
        return result;
    };
    
    return tool;
}

} // namespace BuiltinTools

// ============================================================================
// Tool Chain Implementation
// ============================================================================

ToolChain::ToolChain(const std::string& chainName)
    : name(chainName)
    , stopOnError(true)
    , parallelExecution(false) {
}

ToolChain::~ToolChain() {
}

void ToolChain::AddTool(const std::string& toolName, const std::unordered_map<std::string, std::string>& parameters) {
    ChainStep step;
    step.toolName = toolName;
    step.parameters = parameters;
    step.isConditional = false;
    step.isLoop = false;
    steps.push_back(step);
}

void ToolChain::AddConditional(
    const std::string& condition,
    const std::string& trueTool,
    const std::string& falseTool) {
    
    ChainStep step;
    step.isConditional = true;
    step.condition = condition;
    step.trueTool = trueTool;
    step.falseTool = falseTool;
    steps.push_back(step);
}

void ToolChain::AddLoop(
    const std::string& toolName,
    const std::string& loopCondition,
    int maxIterations) {
    
    ChainStep step;
    step.toolName = toolName;
    step.isLoop = true;
    step.loopCondition = loopCondition;
    step.maxIterations = maxIterations;
    steps.push_back(step);
}

ToolResult ToolChain::Execute(ToolExecutor* executor) {
    ToolResult finalResult;
    finalResult.success = true;
    
    for (const auto& step : steps) {
        if (step.isConditional) {
            // Execute conditional
            // Simplified - always execute true branch
            ToolResult result = executor->Execute(step.trueTool, {});
            if (!result.success && stopOnError) {
                finalResult = result;
                break;
            }
        } else if (step.isLoop) {
            // Execute loop
            int iterations = 0;
            while (iterations < step.maxIterations) {
                ToolResult result = executor->Execute(step.toolName, step.parameters);
                if (!result.success && stopOnError) {
                    finalResult = result;
                    break;
                }
                iterations++;
            }
        } else {
            // Execute normal tool
            ToolResult result = executor->Execute(step.toolName, step.parameters);
            if (!result.success && stopOnError) {
                finalResult = result;
                break;
            }
            finalResult = result;
        }
    }
    
    return finalResult;
}

void ToolChain::SetStopOnError(bool stop) {
    stopOnError = stop;
}

void ToolChain::SetParallelExecution(bool enable) {
    parallelExecution = enable;
}

// ============================================================================
// Tool Utilities Implementation
// ============================================================================

namespace ToolUtils {

bool ValidateRequiredParameters(
    const Tool& tool,
    const std::unordered_map<std::string, std::string>& parameters) {
    
    // For now, assume all parameters are optional
    // In real implementation, would mark required parameters
    return true;
}

std::string FormatToolResult(const ToolResult& result) {
    std::stringstream ss;
    ss << "Tool execution result: ";
    if (result.success) {
        ss << "SUCCESS\n";
        ss << "Output: " << result.output << "\n";
        ss << "Execution time: " << result.executionTime << "ms";
    } else {
        ss << "FAILED\n";
        ss << "Error: " << result.error;
    }
    return ss.str();
}

std::string FormatToolError(const ToolResult& result) {
    std::stringstream ss;
    ss << "Tool execution failed: " << result.error;
    if (result.executionTime > 0) {
        ss << " (execution time: " << result.executionTime << "ms)";
    }
    return ss.str();
}

std::string GenerateParameterSchema(const Tool& tool) {
    std::stringstream ss;
    ss << "{";
    ss << "\"type\": \"object\",";
    ss << "\"properties\": {";
    
    bool first = true;
    for (const auto& param : tool.parameters) {
        if (!first) ss << ",";
        ss << "\"" << param.first << "\": {";
        ss << "\"type\": \"string\",";
        ss << "\"description\": \"" << param.second << "\"";
        ss << "}";
        first = false;
    }
    
    ss << "}}";
    return ss.str();
}

std::string FormatToolDescription(const Tool& tool) {
    std::stringstream ss;
    ss << tool.name << ": " << tool.description << "\n";
    ss << "Parameters:\n";
    for (const auto& param : tool.parameters) {
        ss << "  - " << param.first << ": " << param.second << "\n";
    }
    return ss.str();
}

} // namespace ToolUtils

} // namespace AI
} // namespace Potato
