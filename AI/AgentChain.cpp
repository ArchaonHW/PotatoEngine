/**
 * Potato Engine Agent Chain and Graph System Implementation
 */

#include "AgentChain.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <queue>
#include <stack>
#include <cstring>

namespace Potato {
namespace AI {

// ============================================================================
// Agent Chain Implementation
// ============================================================================

AgentChain::AgentChain(const std::string& chainName)
    : name(chainName)
    , agentManager(nullptr)
    , llmClient(nullptr) {
}

AgentChain::~AgentChain() {
}

void AgentChain::AddNode(const ChainNode& node) {
    nodes.push_back(node);
}

void AgentChain::RemoveNode(const std::string& nodeId) {
    nodes.erase(
        std::remove_if(nodes.begin(), nodes.end(),
            [&nodeId](const ChainNode& n) { return n.id == nodeId; }),
        nodes.end());
}

ChainNode* AgentChain::GetNode(const std::string& nodeId) {
    for (auto& node : nodes) {
        if (node.id == nodeId) {
            return &node;
        }
    }
    return nullptr;
}

const std::vector<ChainNode>& AgentChain::GetNodes() const {
    return nodes;
}

ChainResult AgentChain::Execute(
    const std::unordered_map<std::string, std::string>& inputs) {
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    ChainResult result;
    result.success = true;
    
    // Set input variables
    for (const auto& input : inputs) {
        variables[input.first] = input.second;
    }
    
    // Execute nodes in order
    for (const auto& node : nodes) {
        // Check dependencies
        bool dependenciesMet = true;
        for (const auto& dep : node.dependencies) {
            if (variables.find(dep) == variables.end()) {
                dependenciesMet = false;
                break;
            }
        }
        
        if (!dependenciesMet) {
            continue;
        }
        
        // Execute node
        ChainResult nodeResult = ExecuteNode(node);
        
        if (!nodeResult.success) {
            result.success = false;
            result.error = "Node " + node.id + " failed: " + nodeResult.error;
            break;
        }
        
        // Store output
        variables[node.id] = nodeResult.output;
        
        // Store intermediate results
        for (const auto& intermediate : nodeResult.intermediateResults) {
            variables[intermediate.first] = intermediate.second;
        }
    }
    
    // Set final output
    if (!nodes.empty()) {
        result.output = variables[nodes.back().id];
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    result.executionTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        endTime - startTime).count();
    
    return result;
}

ChainResult AgentChain::ExecuteAsync(
    const std::unordered_map<std::string, std::string>& inputs) {
    
    // Placeholder for async execution
    return Execute(inputs);
}

void AgentChain::Reset() {
    variables.clear();
}

void AgentChain::SetVariable(const std::string& key, const std::string& value) {
    variables[key] = value;
}

std::string AgentChain::GetVariable(const std::string& key) {
    auto it = variables.find(key);
    return it != variables.end() ? it->second : "";
}

void AgentChain::SetAIAgentManager(AIAgentManager* manager) {
    agentManager = manager;
}

void AgentChain::SetLLMClient(ILLMClient* client) {
    llmClient = client;
}

bool AgentChain::Validate() {
    return GetValidationErrors().empty();
}

std::vector<std::string> AgentChain::GetValidationErrors() {
    std::vector<std::string> errors;
    
    // Check for duplicate IDs
    std::unordered_map<std::string, int> idCounts;
    for (const auto& node : nodes) {
        idCounts[node.id]++;
    }
    
    for (const auto& pair : idCounts) {
        if (pair.second > 1) {
            errors.push_back("Duplicate node ID: " + pair.first);
        }
    }
    
    // Check dependencies
    for (const auto& node : nodes) {
        for (const auto& dep : node.dependencies) {
            bool found = false;
            for (const auto& n : nodes) {
                if (n.id == dep) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                errors.push_back("Node " + node.id + " depends on non-existent node: " + dep);
            }
        }
    }
    
    return errors;
}

ChainResult AgentChain::ExecuteNode(const ChainNode& node) {
    switch (node.type) {
        case ChainNodeType::Agent:
            return ExecuteAgentNode(node);
        case ChainNodeType::LLM:
            return ExecuteLLMNode(node);
        case ChainNodeType::Tool:
            return ExecuteToolNode(node);
        case ChainNodeType::Condition:
            return ExecuteConditionNode(node);
        case ChainNodeType::Loop:
            return ExecuteLoopNode(node);
        case ChainNodeType::Parallel:
            return ExecuteParallelNode(node);
        default:
            ChainResult result;
            result.success = false;
            result.error = "Unknown node type";
            return result;
    }
}

ChainResult AgentChain::ExecuteAgentNode(const ChainNode& node) {
    ChainResult result;
    
    if (!agentManager) {
        result.success = false;
        result.error = "Agent manager not set";
        return result;
    }
    
    AIAgent* agent = agentManager->GetAgent(node.agentId);
    if (!agent) {
        result.success = false;
        result.error = "Agent not found: " + node.agentId;
        return result;
    }
    
    // Create task
    AgentTask task;
    task.id = node.id;
    task.description = node.name;
    
    // Assign task
    agentManager->AssignTaskToAgent(node.agentId, task);
    
    // Wait for completion (simplified)
    agent->Update(1.0f);
    
    result.success = true;
    result.output = "Agent " + node.agentId + " completed task";
    
    return result;
}

ChainResult AgentChain::ExecuteLLMNode(const ChainNode& node) {
    ChainResult result;
    
    if (!llmClient) {
        result.success = false;
        result.error = "LLM client not set";
        return result;
    }
    
    // Substitute variables in messages
    std::vector<ChatMessage> processedMessages;
    for (const auto& msg : node.messages) {
        ChatMessage processedMsg = msg;
        processedMsg.content = ChainUtils::SubstituteVariables(msg.content, variables);
        processedMessages.push_back(processedMsg);
    }
    
    // Call LLM
    LLMResponse llmResponse = llmClient->ChatCompletion(processedMessages, node.llmConfig);
    
    if (llmResponse.success) {
        result.success = true;
        result.output = llmResponse.content;
    } else {
        result.success = false;
        result.error = llmResponse.error;
    }
    
    return result;
}

ChainResult AgentChain::ExecuteToolNode(const ChainNode& node) {
    ChainResult result;
    
    // Placeholder for tool execution
    result.success = true;
    result.output = "Tool " + node.toolName + " executed";
    
    return result;
}

ChainResult AgentChain::ExecuteConditionNode(const ChainNode& node) {
    ChainResult result;
    
    bool condition = EvaluateCondition(node.conditionExpression);
    
    if (condition) {
        if (!node.trueBranchId.empty()) {
            ChainNode* trueNode = GetNode(node.trueBranchId);
            if (trueNode) {
                result = ExecuteNode(*trueNode);
            }
        }
    } else {
        if (!node.falseBranchId.empty()) {
            ChainNode* falseNode = GetNode(node.falseBranchId);
            if (falseNode) {
                result = ExecuteNode(*falseNode);
            }
        }
    }
    
    result.success = true;
    return result;
}

ChainResult AgentChain::ExecuteLoopNode(const ChainNode& node) {
    ChainResult result;
    
    int iterations = 0;
    while (iterations < node.maxIterations) {
        bool shouldContinue = EvaluateCondition(node.loopExpression);
        if (!shouldContinue) {
            break;
        }
        
        ChainNode* bodyNode = GetNode(node.loopBodyId);
        if (bodyNode) {
            ChainResult bodyResult = ExecuteNode(*bodyNode);
            if (!bodyResult.success) {
                result.success = false;
                result.error = "Loop body failed";
                return result;
            }
        }
        
        iterations++;
    }
    
    result.success = true;
    result.output = "Loop completed with " + std::to_string(iterations) + " iterations";
    
    return result;
}

ChainResult AgentChain::ExecuteParallelNode(const ChainNode& node) {
    ChainResult result;
    
    // Placeholder for parallel execution
    // In real implementation, would use threads or async
    
    for (const auto& nodeId : node.parallelNodeIds) {
        ChainNode* parallelNode = GetNode(nodeId);
        if (parallelNode) {
            ChainResult parallelResult = ExecuteNode(*parallelNode);
            if (!parallelResult.success) {
                result.success = false;
                result.error = "Parallel node " + nodeId + " failed";
                return result;
            }
        }
    }
    
    result.success = true;
    result.output = "Parallel execution completed";
    
    return result;
}

bool AgentChain::EvaluateCondition(const std::string& expression) {
    // Simple variable evaluation
    // In real implementation, would use expression parser
    
    if (expression.empty()) {
        return true;
    }
    
    // Check if variable exists and is not empty
    auto it = variables.find(expression);
    if (it != variables.end()) {
        return !it->second.empty();
    }
    
    // Check for boolean values
    if (expression == "true") return true;
    if (expression == "false") return false;
    
    return false;
}

std::string AgentChain::EvaluateExpression(const std::string& expression) {
    // Simple variable substitution
    return ChainUtils::SubstituteVariables(expression, variables);
}

// ============================================================================
// Agent Graph Implementation
// ============================================================================

AgentGraph::AgentGraph(const std::string& graphName)
    : name(graphName)
    , agentManager(nullptr)
    , llmClient(nullptr) {
}

AgentGraph::~AgentGraph() {
}

void AgentGraph::AddNode(const ChainNode& node) {
    nodes[node.id] = node;
}

void AgentGraph::RemoveNode(const std::string& nodeId) {
    nodes.erase(nodeId);
    edges.erase(nodeId);
    
    // Remove edges pointing to this node
    for (auto& pair : edges) {
        pair.second.erase(
            std::remove(pair.second.begin(), pair.second.end(), nodeId),
            pair.second.end());
    }
}

ChainNode* AgentGraph::GetNode(const std::string& nodeId) {
    auto it = nodes.find(nodeId);
    return it != nodes.end() ? &it->second : nullptr;
}

const std::unordered_map<std::string, ChainNode>& AgentGraph::GetNodes() const {
    return nodes;
}

void AgentGraph::AddEdge(const std::string& from, const std::string& to) {
    edges[from].push_back(to);
}

void AgentGraph::RemoveEdge(const std::string& from, const std::string& to) {
    auto it = edges.find(from);
    if (it != edges.end()) {
        it->second.erase(
            std::remove(it->second.begin(), it->second.end(), to),
            it->second.end());
    }
}

std::vector<std::string> AgentGraph::GetDependencies(const std::string& nodeId) {
    auto it = edges.find(nodeId);
    return it != edges.end() ? it->second : std::vector<std::string>();
}

ChainResult AgentGraph::Execute(
    const std::unordered_map<std::string, std::string>& inputs) {
    
    ChainResult result;
    result.success = true;
    
    // Get execution order (topological sort)
    std::vector<std::string> executionOrder = GetExecutionOrder();
    
    if (executionOrder.empty()) {
        result.success = false;
        result.error = "No valid execution order (possible cycle)";
        return result;
    }
    
    // Execute nodes in order
    std::unordered_map<std::string, std::string> nodeOutputs;
    
    for (const auto& nodeId : executionOrder) {
        auto it = nodes.find(nodeId);
        if (it == nodes.end()) {
            continue;
        }
        
        ChainNode& node = it->second;
        
        // Execute node
        ChainResult nodeResult = ExecuteNode(node, inputs);
        
        if (!nodeResult.success) {
            result.success = false;
            result.error = "Node " + nodeId + " failed: " + nodeResult.error;
            break;
        }
        
        nodeOutputs[nodeId] = nodeResult.output;
    }
    
    if (result.success && !executionOrder.empty()) {
        result.output = nodeOutputs[executionOrder.back()];
    }
    
    return result;
}

std::vector<std::string> AgentGraph::GetExecutionOrder() {
    std::vector<std::string> order;
    std::unordered_map<std::string, int> inDegree;
    
    // Calculate in-degrees
    for (const auto& pair : nodes) {
        inDegree[pair.first] = 0;
    }
    
    for (const auto& pair : edges) {
        for (const auto& target : pair.second) {
            inDegree[target]++;
        }
    }
    
    // Kahn's algorithm for topological sort
    std::queue<std::string> queue;
    for (const auto& pair : inDegree) {
        if (pair.second == 0) {
            queue.push(pair.first);
        }
    }
    
    while (!queue.empty()) {
        std::string current = queue.front();
        queue.pop();
        order.push_back(current);
        
        for (const auto& neighbor : edges[current]) {
            inDegree[neighbor]--;
            if (inDegree[neighbor] == 0) {
                queue.push(neighbor);
            }
        }
    }
    
    return order;
}

bool AgentGraph::Validate() {
    return !HasCycles() && GetValidationErrors().empty();
}

bool AgentGraph::HasCycles() {
    std::unordered_map<std::string, int> visited;
    
    for (const auto& pair : nodes) {
        visited[pair.first] = 0;  // 0 = unvisited, 1 = visiting, 2 = visited
    }
    
    for (const auto& pair : nodes) {
        if (visited[pair.first] == 0) {
            if (DetectCycleDFS(pair.first, visited)) {
                return true;
            }
        }
    }
    
    return false;
}

std::vector<std::string> AgentGraph::GetValidationErrors() {
    std::vector<std::string> errors;
    
    if (HasCycles()) {
        errors.push_back("Graph contains cycles");
    }
    
    return errors;
}

std::string AgentGraph::GenerateDOT() {
    std::stringstream dot;
    dot << "digraph " << name << " {\n";
    
    for (const auto& pair : nodes) {
        dot << "  \"" << pair.first << "\" [label=\"" << pair.second.name << "\"];\n";
    }
    
    for (const auto& pair : edges) {
        for (const auto& target : pair.second) {
            dot << "  \"" << pair.first << "\" -> \"" << target << "\";\n";
        }
    }
    
    dot << "}\n";
    return dot.str();
}

void AgentGraph::SetAIAgentManager(AIAgentManager* manager) {
    agentManager = manager;
}

void AgentGraph::SetLLMClient(ILLMClient* client) {
    llmClient = client;
}

bool AgentGraph::DetectCycleDFS(
    const std::string& node,
    std::unordered_map<std::string, int>& visited) {
    
    visited[node] = 1;  // Visiting
    
    for (const auto& neighbor : edges[node]) {
        if (visited[neighbor] == 1) {
            return true;  // Back edge found
        }
        if (visited[neighbor] == 0) {
            if (DetectCycleDFS(neighbor, visited)) {
                return true;
            }
        }
    }
    
    visited[node] = 2;  // Visited
    return false;
}

ChainResult AgentGraph::ExecuteNode(
    const ChainNode& node,
    const std::unordered_map<std::string, std::string>& inputs) {
    
    // Simplified execution - reuse chain logic
    // In real implementation, would have proper graph execution
    
    ChainResult result;
    result.success = true;
    result.output = "Node " + node.id + " executed";
    
    return result;
}

// ============================================================================
// Chain Builder Implementation
// ============================================================================

ChainBuilder::ChainBuilder(const std::string& chainName)
    : name(chainName) {
}

ChainBuilder::~ChainBuilder() {
}

ChainBuilder& ChainBuilder::Agent(const std::string& id, const std::string& agentId) {
    currentNode = ChainNode();
    currentNode.id = id;
    currentNode.type = ChainNodeType::Agent;
    currentNode.agentId = agentId;
    return *this;
}

ChainBuilder& ChainBuilder::LLM(const std::string& id, const std::vector<ChatMessage>& messages) {
    currentNode = ChainNode();
    currentNode.id = id;
    currentNode.type = ChainNodeType::LLM;
    currentNode.messages = messages;
    return *this;
}

ChainBuilder& ChainBuilder::Tool(const std::string& id, const std::string& toolName) {
    currentNode = ChainNode();
    currentNode.id = id;
    currentNode.type = ChainNodeType::Tool;
    currentNode.toolName = toolName;
    return *this;
}

ChainBuilder& ChainBuilder::Condition(const std::string& id, const std::string& condition) {
    currentNode = ChainNode();
    currentNode.id = id;
    currentNode.type = ChainNodeType::Condition;
    currentNode.conditionExpression = condition;
    return *this;
}

ChainBuilder& ChainBuilder::Loop(const std::string& id, const std::string& loopExpr) {
    currentNode = ChainNode();
    currentNode.id = id;
    currentNode.type = ChainNodeType::Loop;
    currentNode.loopExpression = loopExpr;
    return *this;
}

ChainBuilder& ChainBuilder::Parallel(const std::string& id, const std::vector<std::string>& nodeIds) {
    currentNode = ChainNode();
    currentNode.id = id;
    currentNode.type = ChainNodeType::Parallel;
    currentNode.parallelNodeIds = nodeIds;
    return *this;
}

ChainBuilder& ChainBuilder::WithParameter(const std::string& key, const std::string& value) {
    currentNode.parameters[key] = value;
    return *this;
}

ChainBuilder& ChainBuilder::WithDependency(const std::string& nodeId) {
    currentNode.dependencies.push_back(nodeId);
    return *this;
}

ChainBuilder& ChainBuilder::WithTrueBranch(const std::string& nodeId) {
    currentNode.trueBranchId = nodeId;
    return *this;
}

ChainBuilder& ChainBuilder::WithFalseBranch(const std::string& nodeId) {
    currentNode.falseBranchId = nodeId;
    return *this;
}

ChainBuilder& ChainBuilder::WithLoopBody(const std::string& nodeId) {
    currentNode.loopBodyId = nodeId;
    return *this;
}

ChainBuilder& ChainBuilder::WithMaxIterations(int max) {
    currentNode.maxIterations = max;
    return *this;
}

std::unique_ptr<AgentChain> ChainBuilder::BuildChain() {
    nodes.push_back(currentNode);
    
    auto chain = std::make_unique<AgentChain>(name);
    for (const auto& node : nodes) {
        chain->AddNode(node);
    }
    
    return chain;
}

std::unique_ptr<AgentGraph> ChainBuilder::BuildGraph() {
    nodes.push_back(currentNode);
    
    auto graph = std::make_unique<AgentGraph>(name);
    for (const auto& node : nodes) {
        graph->AddNode(node);
    }
    
    for (const auto& pair : edges) {
        for (const auto& target : pair.second) {
            graph->AddEdge(pair.first, target);
        }
    }
    
    return graph;
}

// ============================================================================
// Chain Templates Implementation
// ============================================================================

namespace ChainTemplates {

std::unique_ptr<AgentChain> CreateSequentialChain(
    const std::string& name,
    const std::vector<std::string>& agentIds) {
    
    auto chain = std::make_unique<AgentChain>(name);
    
    for (size_t i = 0; i < agentIds.size(); i++) {
        ChainNode node;
        node.id = "node_" + std::to_string(i);
        node.type = ChainNodeType::Agent;
        node.agentId = agentIds[i];
        node.name = "Agent " + agentIds[i];
        
        if (i > 0) {
            node.dependencies.push_back("node_" + std::to_string(i - 1));
        }
        
        chain->AddNode(node);
    }
    
    return chain;
}

std::unique_ptr<AgentChain> CreateParallelChain(
    const std::string& name,
    const std::vector<std::string>& agentIds) {
    
    auto chain = std::make_unique<AgentChain>(name);
    
    // Create parallel node
    ChainNode parallelNode;
    parallelNode.id = "parallel";
    parallelNode.type = ChainNodeType::Parallel;
    parallelNode.name = "Parallel Execution";
    
    std::vector<std::string> nodeIds;
    for (size_t i = 0; i < agentIds.size(); i++) {
        std::string nodeId = "agent_" + std::to_string(i);
        nodeIds.push_back(nodeId);
        
        ChainNode agentNode;
        agentNode.id = nodeId;
        agentNode.type = ChainNodeType::Agent;
        agentNode.agentId = agentIds[i];
        agentNode.name = "Agent " + agentIds[i];
        
        chain->AddNode(agentNode);
    }
    
    parallelNode.parallelNodeIds = nodeIds;
    chain->AddNode(parallelNode);
    
    return chain;
}

std::unique_ptr<AgentChain> CreateRouterChain(
    const std::string& name,
    const std::string& condition,
    const std::string& trueAgentId,
    const std::string& falseAgentId) {
    
    auto chain = std::make_unique<AgentChain>(name);
    
    // Condition node
    ChainNode conditionNode;
    conditionNode.id = "condition";
    conditionNode.type = ChainNodeType::Condition;
    conditionNode.conditionExpression = condition;
    conditionNode.name = "Router";
    conditionNode.trueBranchId = "true_branch";
    conditionNode.falseBranchId = "false_branch";
    chain->AddNode(conditionNode);
    
    // True branch
    ChainNode trueNode;
    trueNode.id = "true_branch";
    trueNode.type = ChainNodeType::Agent;
    trueNode.agentId = trueAgentId;
    trueNode.name = "True Branch";
    trueNode.dependencies.push_back("condition");
    chain->AddNode(trueNode);
    
    // False branch
    ChainNode falseNode;
    falseNode.id = "false_branch";
    falseNode.type = ChainNodeType::Agent;
    falseNode.agentId = falseAgentId;
    falseNode.name = "False Branch";
    falseNode.dependencies.push_back("condition");
    chain->AddNode(falseNode);
    
    return chain;
}

std::unique_ptr<AgentChain> CreateLoopChain(
    const std::string& name,
    const std::string& agentId,
    const std::string& loopCondition) {
    
    auto chain = std::make_unique<AgentChain>(name);
    
    // Loop node
    ChainNode loopNode;
    loopNode.id = "loop";
    loopNode.type = ChainNodeType::Loop;
    loopNode.loopExpression = loopCondition;
    loopNode.loopBodyId = "loop_body";
    loopNode.name = "Loop";
    chain->AddNode(loopNode);
    
    // Loop body
    ChainNode bodyNode;
    bodyNode.id = "loop_body";
    bodyNode.type = ChainNodeType::Agent;
    bodyNode.agentId = agentId;
    bodyNode.name = "Loop Body";
    bodyNode.dependencies.push_back("loop");
    chain->AddNode(bodyNode);
    
    return chain;
}

std::unique_ptr<AgentChain> CreateMapReduceChain(
    const std::string& name,
    const std::vector<std::string>& mapAgentIds,
    const std::string& reduceAgentId) {
    
    auto chain = std::make_unique<AgentChain>(name);
    
    // Map phase (parallel)
    ChainNode mapNode;
    mapNode.id = "map";
    mapNode.type = ChainNodeType::Parallel;
    mapNode.name = "Map Phase";
    
    std::vector<std::string> mapNodeIds;
    for (size_t i = 0; i < mapAgentIds.size(); i++) {
        std::string nodeId = "map_agent_" + std::to_string(i);
        mapNodeIds.push_back(nodeId);
        
        ChainNode agentNode;
        agentNode.id = nodeId;
        agentNode.type = ChainNodeType::Agent;
        agentNode.agentId = mapAgentIds[i];
        agentNode.name = "Map Agent " + mapAgentIds[i];
        
        chain->AddNode(agentNode);
    }
    
    mapNode.parallelNodeIds = mapNodeIds;
    chain->AddNode(mapNode);
    
    // Reduce phase
    ChainNode reduceNode;
    reduceNode.id = "reduce";
    reduceNode.type = ChainNodeType::Agent;
    reduceNode.agentId = reduceAgentId;
    reduceNode.name = "Reduce Phase";
    reduceNode.dependencies.push_back("map");
    chain->AddNode(reduceNode);
    
    return chain;
}

} // namespace ChainTemplates

// ============================================================================
// Chain Orchestration Implementation
// ============================================================================

ChainOrchestrator::ChainOrchestrator()
    : agentManager(nullptr)
    , llmClient(nullptr) {
    
    memset(&stats, 0, sizeof(stats));
}

ChainOrchestrator::~ChainOrchestrator() {
}

void ChainOrchestrator::RegisterChain(std::unique_ptr<AgentChain> chain) {
    chain->SetAIAgentManager(agentManager);
    chain->SetLLMClient(llmClient);
    chains[chain->GetNodes().front().id] = std::move(chain);
}

void ChainOrchestrator::UnregisterChain(const std::string& name) {
    chains.erase(name);
}

AgentChain* ChainOrchestrator::GetChain(const std::string& name) {
    auto it = chains.find(name);
    return it != chains.end() ? it->second.get() : nullptr;
}

void ChainOrchestrator::RegisterGraph(std::unique_ptr<AgentGraph> graph) {
    graph->SetAIAgentManager(agentManager);
    graph->SetLLMClient(llmClient);
    graphs[graph->GetNodes().begin()->first] = std::move(graph);
}

void ChainOrchestrator::UnregisterGraph(const std::string& name) {
    graphs.erase(name);
}

AgentGraph* ChainOrchestrator::GetGraph(const std::string& name) {
    auto it = graphs.find(name);
    return it != graphs.end() ? it->second.get() : nullptr;
}

ChainResult ChainOrchestrator::ExecuteChain(
    const std::string& name,
    const std::unordered_map<std::string, std::string>& inputs) {
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    stats.totalExecutions++;
    
    AgentChain* chain = GetChain(name);
    if (!chain) {
        stats.failedExecutions++;
        ChainResult result;
        result.success = false;
        result.error = "Chain not found: " + name;
        return result;
    }
    
    ChainResult result = chain->Execute(inputs);
    
    if (result.success) {
        stats.successfulExecutions++;
    } else {
        stats.failedExecutions++;
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    stats.averageExecutionTime = (stats.averageExecutionTime * (stats.totalExecutions - 1) + duration.count() / 1000.0f) / stats.totalExecutions;
    
    return result;
}

ChainResult ChainOrchestrator::ExecuteGraph(
    const std::string& name,
    const std::unordered_map<std::string, std::string>& inputs) {
    
    AgentGraph* graph = GetGraph(name);
    if (!graph) {
        ChainResult result;
        result.success = false;
        result.error = "Graph not found: " + name;
        return result;
    }
    
    return graph->Execute(inputs);
}

std::vector<ChainResult> ChainOrchestrator::ExecuteChains(
    const std::vector<std::string>& chainNames,
    const std::unordered_map<std::string, std::string>& inputs) {
    
    std::vector<ChainResult> results;
    for (const auto& name : chainNames) {
        results.push_back(ExecuteChain(name, inputs));
    }
    return results;
}

void ChainOrchestrator::SetAIAgentManager(AIAgentManager* manager) {
    agentManager = manager;
    for (auto& pair : chains) {
        pair.second->SetAIAgentManager(manager);
    }
    for (auto& pair : graphs) {
        pair.second->SetAIAgentManager(manager);
    }
}

void ChainOrchestrator::SetLLMClient(ILLMClient* client) {
    llmClient = client;
    for (auto& pair : chains) {
        pair.second->SetLLMClient(client);
    }
    for (auto& pair : graphs) {
        pair.second->SetLLMClient(client);
    }
}

ChainOrchestrator::OrchestratorStats ChainOrchestrator::GetStats() const {
    return stats;
}

void ChainOrchestrator::ResetStats() {
    memset(&stats, 0, sizeof(stats));
}

// ============================================================================
// Chain Utilities Implementation
// ============================================================================

namespace ChainUtils {

std::string SubstituteVariables(
    const std::string& text,
    const std::unordered_map<std::string, std::string>& variables) {
    
    std::string result = text;
    
    for (const auto& pair : variables) {
        std::string placeholder = "{" + pair.first + "}";
        size_t pos = 0;
        while ((pos = result.find(placeholder, pos)) != std::string::npos) {
            result.replace(pos, placeholder.length(), pair.second);
            pos += pair.second.length();
        }
    }
    
    return result;
}

std::unordered_map<std::string, std::string> MergeResults(
    const std::vector<ChainResult>& results) {
    
    std::unordered_map<std::string, std::string> merged;
    
    for (size_t i = 0; i < results.size(); i++) {
        merged["result_" + std::to_string(i)] = results[i].output;
        
        for (const auto& pair : results[i].intermediateResults) {
            merged[pair.first] = pair.second;
        }
    }
    
    return merged;
}

std::string FormatChainError(const ChainResult& result) {
    std::stringstream ss;
    ss << "Chain execution failed: " << result.error;
    if (result.executionTime > 0) {
        ss << " (execution time: " << result.executionTime << "ms)";
    }
    return ss.str();
}

void LogChainExecution(const std::string& chainName, const ChainResult& result) {
    std::cout << "[Chain] " << chainName << ": ";
    if (result.success) {
        std::cout << "SUCCESS (" << result.executionTime << "ms)" << std::endl;
    } else {
        std::cout << "FAILED - " << result.error << std::endl;
    }
}

} // namespace ChainUtils

} // namespace AI
} // namespace Potato
