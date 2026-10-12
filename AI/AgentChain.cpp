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
#include <future>
#include <unordered_set>

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
    
    // 被 condition/loop/parallel 引用的子節點是「子例程」——
    // 只在被調用時執行,不佔主序列步驟（避免雙重副作用）
    std::unordered_set<std::string> internal;
    for (const auto& n : nodes) {
        if (!n.trueBranchId.empty()) internal.insert(n.trueBranchId);
        if (!n.falseBranchId.empty()) internal.insert(n.falseBranchId);
        if (!n.loopBodyId.empty()) internal.insert(n.loopBodyId);
        for (const auto& id : n.parallelNodeIds) internal.insert(id);
    }
    
    // Execute nodes in order
    for (const auto& node : nodes) {
        if (internal.count(node.id)) {
            continue;
        }
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
    
    // 同步語義（API 保留給未來真非同步排程）
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

void AgentChain::SetToolRegistry(ToolRegistry* registry) {
    toolExecutor = registry
        ? std::make_unique<ToolExecutor>(registry) : nullptr;
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

namespace {
// 節點執行上下文——chain 與 graph 共用同一套調度。
// find 依 id 查節點（condition 分支/loop body/parallel 子節點）;
// vars 為執行期變數表（inputs + 各節點輸出）
struct ExecCtx {
    AIAgentManager* agents = nullptr;
    ILLMClient* llm = nullptr;
    ToolExecutor* tools = nullptr;
    std::unordered_map<std::string, std::string>* vars = nullptr;
    std::function<ChainNode*(const std::string&)> find;
};

std::string SubstVars(const std::string& s, const ExecCtx& ctx) {
    return ctx.vars ? ChainUtils::SubstituteVariables(s, *ctx.vars)
                    : s;
}

// 條件求值：先 {var} 展開,再支援
//   a == b / a != b（字串）;a > < >= <= b（雙邊可轉數字則
//   數值比較,否則字串序）;!expr 否定;true/false 字面;
//   裸 var → 存在、非空且非 "false"/"0"
bool EvalCond(const std::string& expr, const ExecCtx& ctx) {
    std::string e = SubstVars(expr, ctx);
    // 去首尾空白
    const auto ws = e.find_first_not_of(" \t");
    if (ws == std::string::npos) return true;
    e = e.substr(ws);
    e = e.substr(0, e.find_last_not_of(" \t") + 1);

    if (e.front() == '!') return !EvalCond(e.substr(1), ctx);

    const auto trim = [](std::string s) {
        const auto l = s.find_first_not_of(" \t");
        if (l == std::string::npos) return std::string();
        const auto r = s.find_last_not_of(" \t");
        return s.substr(l, r - l + 1);
    };
    const char* ops[] = {"==", "!=", ">=", "<=", ">", "<"};
    for (const char* op : ops) {
        const auto pos = e.find(op);
        if (pos == std::string::npos) continue;
        const std::string a = trim(e.substr(0, pos));
        const std::string b =
            trim(e.substr(pos + 2 - (op[1] ? 0 : 1)));
        const auto num = [](const std::string& s, double& v) {
            try { v = std::stod(s); return true; }
            catch (...) { return false; }
        };
        double na = 0.0, nb = 0.0;
        const bool bothNum = num(a, na) && num(b, nb);
        const int cmp = bothNum
            ? (na < nb ? -1 : na > nb ? 1 : 0)
            : a.compare(b);
        const std::string o = op;
        if (o == "==") return cmp == 0;
        if (o == "!=") return cmp != 0;
        if (o == ">")  return cmp > 0;
        if (o == "<")  return cmp < 0;
        if (o == ">=") return cmp >= 0;
        if (o == "<=") return cmp <= 0;
    }
    if (e == "true") return true;
    if (e == "false" || e == "0") return false;
    // 裸 token：變數名或字面——非空即真
    return !e.empty();
}

ChainResult RunNode(const ChainNode& node, ExecCtx& ctx);

ChainResult RunAgentNode(const ChainNode& node, ExecCtx& ctx) {
    ChainResult result;
    if (!ctx.agents) {
        result.success = false;
        result.error = "Agent manager not set";
        return result;
    }
    AIAgent* agent = ctx.agents->GetAgent(node.agentId);
    if (!agent) {
        result.success = false;
        result.error = "Agent not found: " + node.agentId;
        return result;
    }
    AgentTask task;
    task.id = node.id;
    task.description = SubstVars(node.name, ctx);
    ctx.agents->AssignTaskToAgent(node.agentId, task);
    agent->Update(1.0f);
    result.success = true;
    result.output = "Agent " + node.agentId + " completed task";
    return result;
}

ChainResult RunLLMNode(const ChainNode& node, ExecCtx& ctx) {
    ChainResult result;
    if (!ctx.llm) {
        result.success = false;
        result.error = "LLM client not set";
        return result;
    }
    std::vector<ChatMessage> processed;
    for (const auto& msg : node.messages) {
        ChatMessage m = msg;
        m.content = SubstVars(msg.content, ctx);
        processed.push_back(m);
    }
    LLMResponse r = ctx.llm->ChatCompletion(processed,
                                            node.llmConfig);
    result.success = r.success;
    result.output = r.content;
    result.error = r.error;
    return result;
}

ChainResult RunToolNode(const ChainNode& node, ExecCtx& ctx) {
    ChainResult result;
    if (!ctx.tools) {
        result.success = false;
        result.error =
            "Tool registry not set (tool: " + node.toolName + ")";
        return result;
    }
    // node.parameters 與 toolParameters 合併,{var} 展開
    std::unordered_map<std::string, std::string> params =
        node.parameters;
    for (const auto& kv : node.toolParameters)
        params[kv.first] = kv.second;
    for (auto& kv : params)
        kv.second = SubstVars(kv.second, ctx);
    ToolResult tr = ctx.tools->Execute(node.toolName, params);
    result.success = tr.success;
    result.output = tr.output;
    result.error = tr.error;
    return result;
}

ChainResult RunConditionNode(const ChainNode& node,
                             ExecCtx& ctx) {
    ChainResult result;
    const bool cond = EvalCond(node.conditionExpression, ctx);
    result.intermediateResults["condition.branch"] =
        cond ? "true" : "false";
    const std::string& pick =
        cond ? node.trueBranchId : node.falseBranchId;
    if (pick.empty()) {
        result.success = true;
        return result;
    }
    ChainNode* branch = ctx.find ? ctx.find(pick) : nullptr;
    if (!branch) {
        result.success = false;
        result.error = "Branch node not found: " + pick;
        return result;
    }
    result = RunNode(*branch, ctx);
    result.intermediateResults["condition.branch"] =
        cond ? "true" : "false";
    return result; // 分支結果直接傳播（成功/失敗/輸出）
}

ChainResult RunLoopNode(const ChainNode& node, ExecCtx& ctx) {
    ChainResult result;
    int iterations = 0;
    while (iterations < node.maxIterations &&
           EvalCond(node.loopExpression, ctx)) {
        ChainNode* body =
            ctx.find ? ctx.find(node.loopBodyId) : nullptr;
        if (!body) break;
        ChainResult bodyResult = RunNode(*body, ctx);
        if (!bodyResult.success) {
            result.success = false;
            result.error = "Loop body failed: " +
                           bodyResult.error;
            return result;
        }
        if (ctx.vars)
            (*ctx.vars)[node.loopBodyId] = bodyResult.output;
        iterations++;
    }
    result.success = true;
    result.output = "Loop completed with " +
                    std::to_string(iterations) + " iterations";
    return result;
}

ChainResult RunParallelNode(const ChainNode& node,
                            ExecCtx& ctx) {
    ChainResult result;
    // 真並行：子節點指標先解析,再各自 async。
    // vars 為唯讀共用（子節點輸出不回寫）——呼叫方須
    // 確保平行子節點不共用非 thread-safe 的 agent 實例
    std::vector<const ChainNode*> children;
    for (const auto& id : node.parallelNodeIds) {
        ChainNode* n = ctx.find ? ctx.find(id) : nullptr;
        if (!n) {
            result.success = false;
            result.error = "Parallel child not found: " + id;
            return result;
        }
        children.push_back(n);
    }
    std::vector<std::future<ChainResult>> futs;
    for (const ChainNode* child : children) {
        futs.push_back(std::async(std::launch::async,
            [child, &ctx]() { return RunNode(*child, ctx); }));
    }
    std::string merged;
    for (auto& f : futs) {
        ChainResult r = f.get();
        if (!r.success) {
            result.success = false;
            result.error = "Parallel child failed: " + r.error;
            return result;
        }
        if (!merged.empty()) merged += "\n";
        merged += r.output;
    }
    result.success = true;
    result.output = merged;
    return result;
}

ChainResult RunNode(const ChainNode& node, ExecCtx& ctx) {
    switch (node.type) {
        case ChainNodeType::Agent:
            return RunAgentNode(node, ctx);
        case ChainNodeType::LLM:
            return RunLLMNode(node, ctx);
        case ChainNodeType::Tool:
            return RunToolNode(node, ctx);
        case ChainNodeType::Condition:
            return RunConditionNode(node, ctx);
        case ChainNodeType::Loop:
            return RunLoopNode(node, ctx);
        case ChainNodeType::Parallel:
            return RunParallelNode(node, ctx);
        default: {
            ChainResult result;
            result.success = false;
            result.error = "Unknown node type";
            return result;
        }
    }
}
} // namespace

ChainResult AgentChain::ExecuteNode(const ChainNode& node) {
    ExecCtx ctx;
    ctx.agents = agentManager;
    ctx.llm = llmClient;
    ctx.tools = toolExecutor.get();
    ctx.vars = &variables;
    ctx.find = [this](const std::string& id) {
        return GetNode(id);
    };
    return RunNode(node, ctx);
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
    
    // Execute nodes in order——vars 累積 inputs+各節點輸出,
    // 下游節點 {var} 展開可引用上游結果
    std::unordered_map<std::string, std::string> vars = inputs;
    std::unordered_map<std::string, std::string> nodeOutputs;
    
    for (const auto& nodeId : executionOrder) {
        auto it = nodes.find(nodeId);
        if (it == nodes.end()) {
            continue;
        }
        
        ChainNode& node = it->second;
        
        // Execute node
        ChainResult nodeResult = ExecuteNode(node, vars);
        
        if (!nodeResult.success) {
            result.success = false;
            result.error = "Node " + nodeId + " failed: " + nodeResult.error;
            break;
        }
        
        nodeOutputs[nodeId] = nodeResult.output;
        vars[nodeId] = nodeResult.output;
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

void AgentGraph::SetToolRegistry(ToolRegistry* registry) {
    toolExecutor = registry
        ? std::make_unique<ToolExecutor>(registry) : nullptr;
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
    std::unordered_map<std::string, std::string>& vars) {

    // 與 AgentChain 同一套節點調度——圖執行不再是空殼
    ExecCtx ctx;
    ctx.agents = agentManager;
    ctx.llm = llmClient;
    ctx.tools = toolExecutor.get();
    ctx.vars = &vars;
    ctx.find = [this](const std::string& id) {
        return GetNode(id);
    };
    return RunNode(node, ctx);
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
    if (!chain) {
        return;
    }
    chain->SetAIAgentManager(agentManager);
    chain->SetLLMClient(llmClient);
    chain->SetToolRegistry(toolRegistry);
    // 以鏈名註冊（舊版誤用首節點 id——撞名且空鏈會
    // .front() 崩潰）
    chains[chain->GetName()] = std::move(chain);
}

void ChainOrchestrator::UnregisterChain(const std::string& name) {
    chains.erase(name);
}

AgentChain* ChainOrchestrator::GetChain(const std::string& name) {
    auto it = chains.find(name);
    return it != chains.end() ? it->second.get() : nullptr;
}

void ChainOrchestrator::RegisterGraph(std::unique_ptr<AgentGraph> graph) {
    if (!graph) {
        return;
    }
    graph->SetAIAgentManager(agentManager);
    graph->SetLLMClient(llmClient);
    graph->SetToolRegistry(toolRegistry);
    graphs[graph->GetName()] = std::move(graph);
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

void ChainOrchestrator::SetToolRegistry(ToolRegistry* registry) {
    toolRegistry = registry;
    for (auto& pair : chains) {
        pair.second->SetToolRegistry(registry);
    }
    for (auto& pair : graphs) {
        pair.second->SetToolRegistry(registry);
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
