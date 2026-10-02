/**
 * Potato Engine Agent Chain and Graph System
 * Enables agents to work together in chains and graphs
 */

#pragma once

#include "AIAgentSystem.h"
#include "LLMIntegration.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <variant>

namespace Potato {
namespace AI {

/**
 * Chain Node Type
 */
enum class ChainNodeType {
    Agent,          // AIAgent
    LLM,            // LLM call
    Tool,           // Tool execution
    Condition,      // Conditional branch
    Loop,           // Loop iteration
    Parallel,        // Parallel execution
    SubChain        // Nested chain
};

/**
 * Chain Execution Result
 */
struct ChainResult {
    bool success;
    std::string output;
    std::string error;
    std::unordered_map<std::string, std::string> intermediateResults;
    uint64_t executionTime;
    
    ChainResult()
        : success(false)
        , executionTime(0) {
    }
};

/**
 * Chain Node
 */
struct ChainNode {
    std::string id;
    ChainNodeType type;
    std::string name;
    std::vector<std::string> dependencies;  // Node IDs this depends on
    std::unordered_map<std::string, std::string> parameters;
    
    // Agent-specific
    std::string agentId;
    
    // LLM-specific
    std::vector<ChatMessage> messages;
    LLMConfig llmConfig;
    
    // Tool-specific
    std::string toolName;
    std::unordered_map<std::string, std::string> toolParameters;
    
    // Condition-specific
    std::string conditionExpression;
    std::string trueBranchId;
    std::string falseBranchId;
    
    // Loop-specific
    std::string loopExpression;
    std::string loopBodyId;
    int maxIterations;
    
    // Parallel-specific
    std::vector<std::string> parallelNodeIds;
    
    ChainNode()
        : type(ChainNodeType::Agent)
        , maxIterations(100) {
    }
};

/**
 * Agent Chain
 * Linear execution of agents/nodes
 */
class AgentChain {
public:
    AgentChain(const std::string& name);
    ~AgentChain();
    
    // Node management
    void AddNode(const ChainNode& node);
    void RemoveNode(const std::string& nodeId);
    ChainNode* GetNode(const std::string& nodeId);
    const std::vector<ChainNode>& GetNodes() const;
    
    // Execution
    ChainResult Execute(
        const std::unordered_map<std::string, std::string>& inputs = {});
    
    ChainResult ExecuteAsync(
        const std::unordered_map<std::string, std::string>& inputs = {});
    
    // State management
    void Reset();
    void SetVariable(const std::string& key, const std::string& value);
    std::string GetVariable(const std::string& key);
    
    // Configuration
    void SetAIAgentManager(AIAgentManager* manager);
    void SetLLMClient(ILLMClient* client);
    
    // Validation
    bool Validate();
    std::vector<std::string> GetValidationErrors();
    
private:
    std::string name;
    std::vector<ChainNode> nodes;
    std::unordered_map<std::string, std::string> variables;
    AIAgentManager* agentManager;
    ILLMClient* llmClient;
    
    ChainResult ExecuteNode(const ChainNode& node);
    ChainResult ExecuteAgentNode(const ChainNode& node);
    ChainResult ExecuteLLMNode(const ChainNode& node);
    ChainResult ExecuteToolNode(const ChainNode& node);
    ChainResult ExecuteConditionNode(const ChainNode& node);
    ChainResult ExecuteLoopNode(const ChainNode& node);
    ChainResult ExecuteParallelNode(const ChainNode& node);
    
    bool EvaluateCondition(const std::string& expression);
    std::string EvaluateExpression(const std::string& expression);
};

/**
 * Agent Graph
 * Directed acyclic graph of agents
 */
class AgentGraph {
public:
    AgentGraph(const std::string& name);
    ~AgentGraph();
    
    // Node management
    void AddNode(const ChainNode& node);
    void RemoveNode(const std::string& nodeId);
    ChainNode* GetNode(const std::string& nodeId);
    const std::unordered_map<std::string, ChainNode>& GetNodes() const;
    
    // Edge management
    void AddEdge(const std::string& from, const std::string& to);
    void RemoveEdge(const std::string& from, const std::string& to);
    std::vector<std::string> GetDependencies(const std::string& nodeId);
    
    // Execution
    ChainResult Execute(
        const std::unordered_map<std::string, std::string>& inputs = {});
    
    // Topological sort
    std::vector<std::string> GetExecutionOrder();
    
    // Validation
    bool Validate();
    bool HasCycles();
    std::vector<std::string> GetValidationErrors();
    
    // Visualization
    std::string GenerateDOT();
    
    // Configuration
    void SetAIAgentManager(AIAgentManager* manager);
    void SetLLMClient(ILLMClient* client);
    
private:
    std::string name;
    std::unordered_map<std::string, ChainNode> nodes;
    std::unordered_map<std::string, std::vector<std::string>> edges;
    AIAgentManager* agentManager;
    ILLMClient* llmClient;
    
    bool DetectCycleDFS(
        const std::string& node,
        std::unordered_map<std::string, int>& visited);
    
    ChainResult ExecuteNode(
        const ChainNode& node,
        const std::unordered_map<std::string, std::string>& inputs);
};

/**
 * Chain Builder
 * Fluent interface for building chains
 */
class ChainBuilder {
public:
    ChainBuilder(const std::string& name);
    ~ChainBuilder();
    
    // Node builders
    ChainBuilder& Agent(const std::string& id, const std::string& agentId);
    ChainBuilder& LLM(const std::string& id, const std::vector<ChatMessage>& messages);
    ChainBuilder& Tool(const std::string& id, const std::string& toolName);
    ChainBuilder& Condition(const std::string& id, const std::string& condition);
    ChainBuilder& Loop(const std::string& id, const std::string& loopExpr);
    ChainBuilder& Parallel(const std::string& id, const std::vector<std::string>& nodeIds);
    
    // Parameter builders
    ChainBuilder& WithParameter(const std::string& key, const std::string& value);
    ChainBuilder& WithDependency(const std::string& nodeId);
    ChainBuilder& WithTrueBranch(const std::string& nodeId);
    ChainBuilder& WithFalseBranch(const std::string& nodeId);
    ChainBuilder& WithLoopBody(const std::string& nodeId);
    ChainBuilder& WithMaxIterations(int max);
    
    // Build
    std::unique_ptr<AgentChain> BuildChain();
    std::unique_ptr<AgentGraph> BuildGraph();
    
private:
    std::string name;
    ChainNode currentNode;
    std::vector<ChainNode> nodes;
    std::unordered_map<std::string, std::vector<std::string>> edges;
};

/**
 * Chain Templates
 * Pre-built chain patterns
 */
namespace ChainTemplates {
    // Sequential chain
    std::unique_ptr<AgentChain> CreateSequentialChain(
        const std::string& name,
        const std::vector<std::string>& agentIds);
    
    // Parallel chain
    std::unique_ptr<AgentChain> CreateParallelChain(
        const std::string& name,
        const std::vector<std::string>& agentIds);
    
    // Router chain (conditional routing)
    std::unique_ptr<AgentChain> CreateRouterChain(
        const std::string& name,
        const std::string& condition,
        const std::string& trueAgentId,
        const std::string& falseAgentId);
    
    // Loop chain
    std::unique_ptr<AgentChain> CreateLoopChain(
        const std::string& name,
        const std::string& agentId,
        const std::string& loopCondition);
    
    // Map-reduce chain
    std::unique_ptr<AgentChain> CreateMapReduceChain(
        const std::string& name,
        const std::vector<std::string>& mapAgentIds,
        const std::string& reduceAgentId);
}

/**
 * Chain Orchestration
 * Manages multiple chains and their execution
 */
class ChainOrchestrator {
public:
    ChainOrchestrator();
    ~ChainOrchestrator();
    
    // Chain management
    void RegisterChain(std::unique_ptr<AgentChain> chain);
    void UnregisterChain(const std::string& name);
    AgentChain* GetChain(const std::string& name);
    
    // Graph management
    void RegisterGraph(std::unique_ptr<AgentGraph> graph);
    void UnregisterGraph(const std::string& name);
    AgentGraph* GetGraph(const std::string& name);
    
    // Execution
    ChainResult ExecuteChain(
        const std::string& name,
        const std::unordered_map<std::string, std::string>& inputs = {});
    
    ChainResult ExecuteGraph(
        const std::string& name,
        const std::unordered_map<std::string, std::string>& inputs = {});
    
    // Batch execution
    std::vector<ChainResult> ExecuteChains(
        const std::vector<std::string>& chainNames,
        const std::unordered_map<std::string, std::string>& inputs = {});
    
    // Configuration
    void SetAIAgentManager(AIAgentManager* manager);
    void SetLLMClient(ILLMClient* client);
    
    // Statistics
    struct OrchestratorStats {
        size_t totalExecutions;
        size_t successfulExecutions;
        size_t failedExecutions;
        float averageExecutionTime;
    };
    OrchestratorStats GetStats() const;
    void ResetStats();
    
private:
    std::unordered_map<std::string, std::unique_ptr<AgentChain>> chains;
    std::unordered_map<std::string, std::unique_ptr<AgentGraph>> graphs;
    AIAgentManager* agentManager;
    ILLMClient* llmClient;
    OrchestratorStats stats;
};

/**
 * Chain Utilities
 */
namespace ChainUtils {
    // Variable substitution
    std::string SubstituteVariables(
        const std::string& text,
        const std::unordered_map<std::string, std::string>& variables);
    
    // Result merging
    std::unordered_map<std::string, std::string> MergeResults(
        const std::vector<ChainResult>& results);
    
    // Error handling
    std::string FormatChainError(const ChainResult& result);
    
    // Execution logging
    void LogChainExecution(const std::string& chainName, const ChainResult& result);
}

} // namespace AI
} // namespace Potato
