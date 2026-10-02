#include "AIAgentSystem.h"
#include <iostream>
#include <algorithm>
#include <random>
#include <chrono>
#include <ctime>
#include <sstream>
#include <fstream>

namespace Potato {
namespace AI {

// ============================================================================
// AIAgent 實現 - 增強版
// ============================================================================

AIAgent::AIAgent(const AgentDesc& desc)
    : name(desc.name)
    , type(desc.type)
    , autonomous(desc.autonomous)
    , performanceRating(desc.performanceRating)
    , learningRate(desc.learningRate)
    , capabilities(desc.capabilities)
    , specializations(desc.specializations)
    , creativityLevel(desc.creativityLevel)
    , riskTolerance(desc.riskTolerance)
    , canUseTools(desc.canUseTools)
    , canCollaborate(desc.canCollaborate)
    , canLearnFromPeers(desc.canLearnFromPeers)
    , maxConcurrentTasks(desc.maxConcurrentTasks)
    , currentTaskIndex(-1)
    , active(true)
    , completedTaskCount(0)
    , failedTaskCount(0)
    , knowledgeShareCount(0) {
    
    std::cout << "創建增強 AI 代理: " << name << " (類型: " << static_cast<int>(type) << ")" << std::endl;
    std::cout << "  - 專業領域: " << specializations.size() << " 個" << std::endl;
    std::cout << "  - 創造力: " << creativityLevel << std::endl;
    std::cout << "  - 風險承受度: " << riskTolerance << std::endl;
    std::cout << "  - 工具使用: " << (canUseTools ? "是" : "否") << std::endl;
    std::cout << "  - 協作能力: " << (canCollaborate ? "是" : "否") << std::endl;
}

AIAgent::~AIAgent() {
    std::cout << "銷毀 AI 代理: " << name << std::endl;
}

bool AIAgent::Initialize() {
    std::cout << "初始化 AI 代理: " << name << std::endl;
    return true;
}

void AIAgent::Shutdown() {
    std::cout << "關閉 AI 代理: " << name << std::endl;
}

void AIAgent::AssignTask(const AgentTask& task) {
    std::lock_guard<std::mutex> lock(taskMutex);
    
    tasks.push_back(task);
    
    if (currentTaskIndex < 0) {
        currentTaskIndex = 0;
    }
    
    std::cout << "分配任務給代理 " << name << ": " << task.description << std::endl;
}

void AIAgent::CompleteTask(const std::string& result) {
    std::lock_guard<std::mutex> lock(taskMutex);
    
    if (currentTaskIndex >= 0 && currentTaskIndex < static_cast<int>(tasks.size())) {
        tasks[currentTaskIndex].status = TaskStatus::Completed;
        tasks[currentTaskIndex].result = result;
        tasks[currentTaskIndex].progress = 1.0f;
        tasks[currentTaskIndex].completedTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        completedTaskCount++;
        
        std::cout << "代理 " << name << " 完成任務: " << result << std::endl;
        
        // 移動到下一個任務
        currentTaskIndex++;
        if (currentTaskIndex >= static_cast<int>(tasks.size())) {
            currentTaskIndex = -1;
        }
    }
}

void AIAgent::FailTask(const std::string& error) {
    std::lock_guard<std::mutex> lock(taskMutex);
    
    if (currentTaskIndex >= 0 && currentTaskIndex < static_cast<int>(tasks.size())) {
        tasks[currentTaskIndex].status = TaskStatus::Failed;
        tasks[currentTaskIndex].error = error;
        
        failedTaskCount++;
        
        std::cout << "代理 " << name << " 任務失敗: " << error << std::endl;
        
        // 移動到下一個任務
        currentTaskIndex++;
        if (currentTaskIndex >= static_cast<int>(tasks.size())) {
            currentTaskIndex = -1;
        }
    }
}

void AIAgent::CancelTask(const std::string& taskId) {
    std::lock_guard<std::mutex> lock(taskMutex);
    
    for (auto& task : tasks) {
        if (task.id == taskId) {
            task.status = TaskStatus::Cancelled;
            break;
        }
    }
}

void AIAgent::Update(float deltaTime) {
    if (!active) return;
    
    ProcessCurrentTask();
}

void AIAgent::ProcessCurrentTask() {
    std::lock_guard<std::mutex> lock(taskMutex);
    
    if (currentTaskIndex < 0 || currentTaskIndex >= static_cast<int>(tasks.size())) {
        return;
    }
    
    AgentTask& task = tasks[currentTaskIndex];
    
    if (task.status == TaskStatus::Pending) {
        task.status = TaskStatus::InProgress;
        std::cout << "代理 " << name << " 開始處理任務: " << task.description << std::endl;
    }
    
    if (task.status == TaskStatus::InProgress) {
        // 模擬任務進度
        task.progress += 0.01f * performanceRating;
        
        if (task.progress >= 1.0f) {
            task.progress = 1.0f;
            CompleteTask("任務完成");
        }
    }
}

Decision AIAgent::MakeDecision(const std::string& context, const std::vector<std::string>& options) {
    Decision decision;
    
    if (options.empty()) {
        decision.confidence = 0.0f;
        return decision;
    }
    
    // 簡單的決策邏輯
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, options.size() - 1);
    
    decision.action = options[dis(gen)];
    decision.confidence = 0.5f + (static_cast<float>(dis(gen)) / 100.0f);
    decision.reasoning.push_back("基於當前上下文的決策");
    decision.timestamp = std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    
    std::cout << "代理 " << name << " 決策: " << decision.action << " (信心: " << decision.confidence << ")" << std::endl;
    
    return decision;
}

void AIAgent::RecordLearning(const std::string& context, const std::string& action, bool success) {
    std::lock_guard<std::mutex> lock(learningMutex);
    
    LearningData data;
    data.context = context;
    data.action = action;
    data.success = success;
    data.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    learningHistory.push_back(data);
    
    // 限制學習歷史大小
    if (learningHistory.size() > 1000) {
        learningHistory.erase(learningHistory.begin());
    }
    
    std::cout << "代理 " << name << " 記錄學習: " << action << " = " << (success ? "成功" : "失敗") << std::endl;
}

float AIAgent::GetSuccessRate() const {
    size_t total = completedTaskCount + failedTaskCount;
    if (total == 0) return 1.0f;
    return static_cast<float>(completedTaskCount) / static_cast<float>(total);
}

// ============================================================================
// 增強功能實現
// ============================================================================

void AIAgent::ProcessPerception(const PerceptionData& perception) {
    std::lock_guard<std::mutex> lock(perceptionMutex);
    
    perceptionQueue.push(perception);
    std::cout << "代理 " << name << " 接收到感知數據: " << static_cast<int>(perception.type) << std::endl;
    
    ProcessPerceptionQueue();
}

std::vector<PerceptionData> AIAgent::GetCurrentPerceptions() const {
    std::lock_guard<std::mutex> lock(perceptionMutex);
    return currentPerceptions;
}

void AIAgent::ProcessPerceptionQueue() {
    while (!perceptionQueue.empty()) {
        PerceptionData perception = perceptionQueue.front();
        perceptionQueue.pop();
        
        // 根據感知類型處理
        switch (perception.type) {
            case PerceptionType::Text:
                currentPerceptions.push_back(perception);
                break;
            case PerceptionType::Image:
                currentPerceptions.push_back(perception);
                break;
            case PerceptionType::Audio:
                currentPerceptions.push_back(perception);
                break;
            default:
                currentPerceptions.push_back(perception);
                break;
        }
    }
    
    // 限制當前感知數量
    if (currentPerceptions.size() > 50) {
        currentPerceptions.erase(currentPerceptions.begin());
    }
}

void AIAgent::StoreMemory(const MemoryItem& memory) {
    std::lock_guard<std::mutex> lock(memoryMutex);
    
    MemoryItem newMemory = memory;
    newMemory.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    memories.push_back(newMemory);
    
    // 建立索引
    for (const auto& tag : memory.tags) {
        memoryIndex[tag].push_back(newMemory);
    }
    
    std::cout << "代理 " << name << " 存儲記憶: " << memory.content.substr(0, 50) << "..." << std::endl;
    
    // 限制記憶數量
    if (memories.size() > 1000) {
        // 移除最不重要的記憶
        auto it = std::min_element(memories.begin(), memories.end(),
            [](const MemoryItem& a, const MemoryItem& b) {
                return a.importance < b.importance;
            });
        memories.erase(it);
    }
}

std::vector<MemoryItem> AIAgent::RetrieveMemory(const std::string& query, MemoryType type) {
    std::lock_guard<std::mutex> lock(memoryMutex);
    
    std::vector<MemoryItem> results;
    
    for (const auto& memory : memories) {
        if (memory.type == type) {
            // 簡單的文本匹配
            if (memory.content.find(query) != std::string::npos) {
                results.push_back(memory);
            }
        }
    }
    
    // 按重要性和可訪問性排序
    std::sort(results.begin(), results.end(),
        [](const MemoryItem& a, const MemoryItem& b) {
            return (a.importance * a.accessibility) > (b.importance * b.accessibility);
        });
    
    return results;
}

void AIAgent::ConsolidateMemories() {
    std::lock_guard<std::mutex> lock(memoryMutex);
    
    std::cout << "代理 " << name << " 整合記憶..." << std::endl;
    
    // 更新記憶可訪問性
    UpdateMemoryAccessibility();
    
    // 將短期記憶轉換為長期記憶
    for (auto& memory : memories) {
        if (memory.type == MemoryType::ShortTerm && memory.accessibility > 0.8f) {
            memory.type = MemoryType::LongTerm;
            std::cout << "  - 將記憶轉換為長期記憶" << std::endl;
        }
    }
}

void AIAgent::UpdateMemoryAccessibility() {
    for (auto& memory : memories) {
        // 基於時間衰減可訪問性
        uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        uint64_t age = now - memory.timestamp;
        
        float decay = std::exp(-static_cast<float>(age) / (1000.0f * 60.0f * 60.0f)); // 1小時衰減
        memory.accessibility = memory.accessibility * decay + 0.1f; // 最小可訪問性
    }
}

bool AIAgent::CanUseTool(const std::string& toolName) const {
    std::lock_guard<std::mutex> lock(toolMutex);
    return availableTools.find(toolName) != availableTools.end();
}

ToolExecutionResult AIAgent::UseTool(const std::string& toolName, 
                                      const std::unordered_map<std::string, std::string>& parameters) {
    std::lock_guard<std::mutex> lock(toolMutex);
    
    ToolExecutionResult result;
    result.toolName = toolName;
    
    auto it = availableTools.find(toolName);
    if (it == availableTools.end()) {
        result.success = false;
        result.error = "工具不可用: " + toolName;
        return result;
    }
    
    const ToolDescription& tool = it->second;
    
    // 模擬工具執行
    std::cout << "代理 " << name << " 使用工具: " << toolName << std::endl;
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // 這裡應該調用實際的工具執行邏輯
    // 目前為模擬實現
    result.success = true;
    result.output = "工具執行成功";
    
    auto endTime = std::chrono::high_resolution_clock::now();
    result.executionTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        endTime - startTime).count();
    
    return result;
}

void AIAgent::RegisterTool(const ToolDescription& tool) {
    std::lock_guard<std::mutex> lock(toolMutex);
    availableTools[tool.name] = tool;
    std::cout << "代理 " << name << " 註冊工具: " << tool.name << std::endl;
}

ExecutionPlan AIAgent::CreatePlan(const std::string& goal, 
                                 const std::vector<std::string>& constraints) {
    ExecutionPlan plan;
    plan.id = "plan_" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    plan.goal = goal;
    plan.startTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    std::cout << "代理 " << name << " 創建執行計畫: " << goal << std::endl;
    
    // 創建基本的計畫節點
    PlanNode node1;
    node1.id = "node_1";
    node1.description = "分析目標";
    node1.assignedAgent = name;
    node1.estimatedDuration = 1.0f;
    plan.nodes.push_back(node1);
    
    PlanNode node2;
    node2.id = "node_2";
    node2.description = "執行任務";
    node2.assignedAgent = name;
    node2.estimatedDuration = 5.0f;
    node2.dependencies.push_back("node_1");
    plan.nodes.push_back(node2);
    
    PlanNode node3;
    node3.id = "node_3";
    node3.description = "驗證結果";
    node3.assignedAgent = name;
    node3.estimatedDuration = 1.0f;
    node3.dependencies.push_back("node_2");
    plan.nodes.push_back(node3);
    
    plan.estimatedCompletionTime = plan.startTime + 
        static_cast<uint64_t>((1.0f + 5.0f + 1.0f) * 1000);
    
    return plan;
}

void AIAgent::ExecutePlan(ExecutionPlan& plan) {
    std::cout << "代理 " << name << " 執行計畫: " << plan.description << std::endl;
    
    // 簡單的計畫執行模擬
    for (auto& node : plan.nodes) {
        if (node.status == TaskStatus::Pending) {
            node.status = TaskStatus::InProgress;
            
            // 檢查依賴
            bool dependenciesMet = true;
            for (const auto& depId : node.dependencies) {
                auto depIt = std::find_if(plan.nodes.begin(), plan.nodes.end(),
                    [&depId](const PlanNode& n) { return n.id == depId; });
                if (depIt != plan.nodes.end() && depIt->status != TaskStatus::Completed) {
                    dependenciesMet = false;
                    break;
                }
            }
            
            if (dependenciesMet) {
                std::cout << "  - 執行節點: " << node.description << std::endl;
                node.status = TaskStatus::Completed;
                node.actualDuration = node.estimatedDuration;
            } else {
                node.status = TaskStatus::Blocked;
            }
        }
    }
    
    // 更新整體進度
    int completed = std::count_if(plan.nodes.begin(), plan.nodes.end(),
        [](const PlanNode& n) { return n.status == TaskStatus::Completed; });
    plan.overallProgress = static_cast<float>(completed) / static_cast<float>(plan.nodes.size());
}

bool AIAgent::CanCollaborateWith(const AIAgent* other) const {
    if (!canCollaborate || !other) return false;
    
    // 檢查專業領域是否相容
    for (const auto& spec : specializations) {
        for (const auto& otherSpec : other->specializations) {
            if (spec == otherSpec) {
                return true;
            }
        }
    }
    
    return false;
}

void AIAgent::ShareKnowledge(const std::string& knowledge, AIAgent* recipient) {
    if (!canCollaborate || !recipient) return;
    
    std::lock_guard<std::mutex> lock(learningMutex);
    
    SharedKnowledge shared;
    shared.knowledge = knowledge;
    shared.sourceAgent = name;
    shared.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    shared.usefulness = 0.5f; // 初始有用性
    
    recipient->ReceiveKnowledge(knowledge, this);
    knowledgeShareCount++;
    
    std::cout << "代理 " << name << " 向 " << recipient->GetName() << " 分享知識" << std::endl;
}

void AIAgent::ReceiveKnowledge(const std::string& knowledge, AIAgent* sender) {
    if (!canLearnFromPeers || !sender) return;
    
    std::lock_guard<std::mutex> lock(learningMutex);
    
    SharedKnowledge shared;
    shared.knowledge = knowledge;
    shared.sourceAgent = sender->GetName();
    shared.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    shared.usefulness = CalculateKnowledgeUsefulness(knowledge);
    
    sharedKnowledge.push_back(shared);
    
    // 存儲為記憶
    MemoryItem memory;
    memory.type = MemoryType::Semantic;
    memory.content = "從 " + sender->GetName() + " 學到: " + knowledge;
    memory.importance = shared.usefulness;
    memory.tags = {"collaborative", "peer-learning"};
    StoreMemory(memory);
    
    std::cout << "代理 " << name << " 從 " << sender->GetName() << " 接收知識" << std::endl;
}

float AIAgent::CalculateKnowledgeUsefulness(const std::string& knowledge) {
    // 簡單的有用性計算：基於知識長度和關鍵詞
    float lengthScore = std::min(1.0f, static_cast<float>(knowledge.length()) / 100.0f);
    
    std::vector<std::string> positiveKeywords = {"成功", "最佳", "優化", "改進", "提升"};
    float keywordScore = 0.0f;
    for (const auto& keyword : positiveKeywords) {
        if (knowledge.find(keyword) != std::string::npos) {
            keywordScore += 0.2f;
        }
    }
    
    return (lengthScore + keywordScore) / 2.0f;
}

// ============================================================================
// 增強代理類型實現
// ============================================================================

// MultimodalAgent 實現
MultimodalAgent::MultimodalAgent(const AgentDesc& desc)
    : AIAgent(desc) {
    specializations.push_back("Image Processing");
    specializations.push_back("Audio Processing");
    specializations.push_back("Video Processing");
}

Decision MultimodalAgent::MakeDecision(const std::string& context, const std::vector<std::string>& options) {
    Decision decision = AIAgent::MakeDecision(context, options);
    
    // 多模態代理考慮多種感知輸入
    decision.reasoning.push_back("整合多種感知輸入進行決策");
    decision.confidence += 0.1f; // 多模態通常有更高的信心
    
    return decision;
}

void MultimodalAgent::ProcessCurrentTask() {
    AIAgent::ProcessCurrentTask();
    
    std::lock_guard<std::mutex> lock(taskMutex);
    
    if (currentTaskIndex >= 0 && currentTaskIndex < static_cast<int>(tasks.size())) {
        AgentTask& task = tasks[currentTaskIndex];
        
        if (task.status == TaskStatus::InProgress) {
            // 處理多模態任務
            if (task.category == "Image Analysis") {
                auto perceptionData = RetrieveMemory("image", MemoryType::ShortTerm);
                if (!perceptionData.empty()) {
                    std::string analysis = AnalyzeImage(std::vector<uint8_t>(
                        perceptionData[0].content.begin(),
                        perceptionData[0].content.end()));
                    task.result = analysis;
                }
            }
        }
    }
}

void MultimodalAgent::ProcessPerception(const PerceptionData& perception) {
    AIAgent::ProcessPerception(perception);
    
    // 多模態代理進行特殊處理
    std::cout << "多模態代理處理感知: " << static_cast<int>(perception.type) << std::endl;
    
    switch (perception.type) {
        case PerceptionType::Image:
            std::cout << "  - 圖像分析: " << perception.source << std::endl;
            break;
        case PerceptionType::Audio:
            std::cout << "  - 音頻分析: " << perception.source << std::endl;
            break;
        case PerceptionType::Video:
            std::cout << "  - 視頻分析: " << perception.source << std::endl;
            break;
        default:
            break;
    }
}

std::string MultimodalAgent::AnalyzeImage(const std::vector<uint8_t>& imageData) {
    // 模擬圖像分析
    return "圖像分析完成: 檢測到3個物體，置信度0.85";
}

std::string MultimodalAgent::AnalyzeAudio(const std::vector<uint8_t>& audioData) {
    // 模擬音頻分析
    return "音頻分析完成: 識別出人聲，音量中等";
}

std::string MultimodalAgent::AnalyzeVideo(const std::vector<uint8_t>& videoData) {
    // 模擬視頻分析
    return "視頻分析完成: 檢測到運動物體，幀率30fps";
}

std::string MultimodalAgent::GenerateMultimodalResponse(const std::vector<PerceptionData>& perceptions) {
    std::string response = "多模態響應: ";
    
    for (const auto& perception : perceptions) {
        switch (perception.type) {
            case PerceptionType::Text:
                response += "文本 ";
                break;
            case PerceptionType::Image:
                response += "圖像 ";
                break;
            case PerceptionType::Audio:
                response += "音頻 ";
                break;
            default:
                break;
        }
    }
    
    return response;
}

// PlannerAgent 實現
PlannerAgent::PlannerAgent(const AgentDesc& desc)
    : AIAgent(desc) {
    specializations.push_back("Task Planning");
    specializations.push_back("Resource Allocation");
    specializations.push_back("Dependency Management");
}

Decision PlannerAgent::MakeDecision(const std::string& context, const std::vector<std::string>& options) {
    Decision decision = AIAgent::MakeDecision(context, options);
    
    // 計畫代理考慮長期影響和依賴關係
    decision.reasoning.push_back("考慮長期影響和依賴關係");
    decision.requiresApproval = true; // 複雜計畫通常需要批准
    
    return decision;
}

void PlannerAgent::ProcessCurrentTask() {
    AIAgent::ProcessCurrentTask();
    
    std::lock_guard<std::mutex> lock(taskMutex);
    
    if (currentTaskIndex >= 0 && currentTaskIndex < static_cast<int>(tasks.size())) {
        AgentTask& task = tasks[currentTaskIndex];
        
        if (task.status == TaskStatus::InProgress && task.category == "Planning") {
            ExecutionPlan plan = CreatePlan(task.description, task.requiredSkills);
            task.result = "創建計畫: " + plan.id + "，包含 " + 
                          std::to_string(plan.nodes.size()) + " 個節點";
        }
    }
}

ExecutionPlan PlannerAgent::CreatePlan(const std::string& goal, const std::vector<std::string>& constraints) {
    ExecutionPlan plan = AIAgent::CreatePlan(goal, constraints);
    
    // 計畫代理創建更詳細的計畫
    std::cout << "計畫代理創建詳細計畫: " << goal << std::endl;
    
    // 添加約束檢查
    for (const auto& constraint : constraints) {
        PlanNode constraintNode;
        constraintNode.id = "constraint_" + constraint;
        constraintNode.description = "檢查約束: " + constraint;
        constraintNode.assignedAgent = name;
        constraintNode.estimatedDuration = 0.5f;
        plan.nodes.insert(plan.nodes.begin(), constraintNode);
    }
    
    // 優化計畫
    plan.nodes = OptimizePlan(plan.nodes);
    
    // 重新計算時間估計
    plan.estimatedCompletionTime = EstimatePlanDuration(plan);
    
    return plan;
}

void PlannerAgent::ExecutePlan(ExecutionPlan& plan) {
    AIAgent::ExecutePlan(plan);
    
    std::cout << "計畫代理執行計畫: " << plan.id << std::endl;
    
    // 計畫代理監控執行進度
    for (auto& node : plan.nodes) {
        if (node.status == TaskStatus::Blocked) {
            std::cout << "  - 節點被阻塞: " << node.description << std::endl;
            // 嘗試解決阻塞
            node.status = TaskStatus::Pending;
        }
    }
}

std::vector<PlanNode> PlannerAgent::DecomposeTask(const std::string& task) {
    std::vector<PlanNode> nodes;
    
    // 簡單的任務分解
    PlanNode analysis;
    analysis.id = "analysis";
    analysis.description = "分析任務: " + task;
    analysis.estimatedDuration = 1.0f;
    nodes.push_back(analysis);
    
    PlanNode execution;
    execution.id = "execution";
    execution.description = "執行任務";
    execution.dependencies.push_back("analysis");
    execution.estimatedDuration = 3.0f;
    nodes.push_back(execution);
    
    PlanNode validation;
    validation.id = "validation";
    validation.description = "驗證結果";
    validation.dependencies.push_back("execution");
    validation.estimatedDuration = 1.0f;
    nodes.push_back(validation);
    
    return nodes;
}

std::vector<std::string> PlannerAgent::IdentifyDependencies(const std::vector<PlanNode>& nodes) {
    std::vector<std::string> dependencies;
    
    for (const auto& node : nodes) {
        for (const auto& dep : node.dependencies) {
            dependencies.push_back(dep);
        }
    }
    
    return dependencies;
}

std::vector<PlanNode> PlannerAgent::OptimizePlan(const std::vector<PlanNode>& nodes) {
    std::vector<PlanNode> optimized = nodes;
    
    // 按依賴關係排序
    std::sort(optimized.begin(), optimized.end(),
        [](const PlanNode& a, const PlanNode& b) {
            return a.dependencies.size() < b.dependencies.size();
        });
    
    return optimized;
}

float PlannerAgent::EstimatePlanDuration(const ExecutionPlan& plan) {
    float totalDuration = 0.0f;
    
    for (const auto& node : plan.nodes) {
        totalDuration += node.estimatedDuration;
    }
    
    // 添加20%的緩衝時間
    return totalDuration * 1.2f;
}

// CommunicatorAgent 實現
CommunicatorAgent::CommunicatorAgent(const AgentDesc& desc)
    : AIAgent(desc) {
    specializations.push_back("Natural Language Processing");
    specializations.push_back("Dialog Management");
    specializations.push_back("Sentiment Analysis");
}

Decision CommunicatorAgent::MakeDecision(const std::string& context, const std::vector<std::string>& options) {
    Decision decision = AIAgent::MakeDecision(context, options);
    
    // 溝通代理考慮語境和情感
    decision.reasoning.push_back("考慮語境和情感因素");
    
    return decision;
}

void CommunicatorAgent::ProcessCurrentTask() {
    AIAgent::ProcessCurrentTask();
    
    std::lock_guard<std::mutex> lock(taskMutex);
    
    if (currentTaskIndex >= 0 && currentTaskIndex < static_cast<int>(tasks.size())) {
        AgentTask& task = tasks[currentTaskIndex];
        
        if (task.status == TaskStatus::InProgress && task.category == "Communication") {
            std::string response = ProcessNaturalLanguage(task.description);
            task.result = response;
        }
    }
}

std::string CommunicatorAgent::ProcessNaturalLanguage(const std::string& text) {
    // 模擬自然語言處理
    std::vector<std::string> intents = ExtractIntent(text);
    bool hasPositiveSentiment = UnderstandSentiment(text);
    
    std::string response = "處理文本: " + text + "\n";
    response += "識別意圖: ";
    for (const auto& intent : intents) {
        response += intent + " ";
    }
    response += std::string("\n情感: ") + (hasPositiveSentiment ? "正面" : "負面");
    
    return response;
}

std::string CommunicatorAgent::GenerateResponse(const std::string& input, const std::string& context) {
    // 模擬響應生成
    return "回應: 基於上下文 \"" + context + "\"，我理解您的輸入 \"" + input + "\"";
}

std::vector<std::string> CommunicatorAgent::ExtractIntent(const std::string& text) {
    std::vector<std::string> intents;
    
    // 簡單的意圖提取
    if (text.find("幫助") != std::string::npos) {
        intents.push_back("help");
    }
    if (text.find("信息") != std::string::npos) {
        intents.push_back("information");
    }
    if (text.find("執行") != std::string::npos) {
        intents.push_back("execution");
    }
    
    if (intents.empty()) {
        intents.push_back("general");
    }
    
    return intents;
}

bool CommunicatorAgent::UnderstandSentiment(const std::string& text) {
    // 簡單的情感分析
    std::vector<std::string> positiveWords = {"好", "優秀", "成功", "喜歡", "讚美"};
    std::vector<std::string> negativeWords = {"壞", "失敗", "不喜歡", "錯誤", "問題"};
    
    int positiveCount = 0;
    int negativeCount = 0;
    
    for (const auto& word : positiveWords) {
        if (text.find(word) != std::string::npos) {
            positiveCount++;
        }
    }
    
    for (const auto& word : negativeWords) {
        if (text.find(word) != std::string::npos) {
            negativeCount++;
        }
    }
    
    return positiveCount >= negativeCount;
}

// ToolUserAgent 實現
ToolUserAgent::ToolUserAgent(const AgentDesc& desc)
    : AIAgent(desc) {
    specializations.push_back("Tool Integration");
    specializations.push_back("API Interaction");
    specializations.push_back("External Services");
}

Decision ToolUserAgent::MakeDecision(const std::string& context, const std::vector<std::string>& options) {
    Decision decision = AIAgent::MakeDecision(context, options);
    
    // 工具代理考慮可用工具和資源
    decision.reasoning.push_back("考慮可用工具和外部資源");
    
    return decision;
}

void ToolUserAgent::ProcessCurrentTask() {
    AIAgent::ProcessCurrentTask();
    
    std::lock_guard<std::mutex> lock(taskMutex);
    
    if (currentTaskIndex >= 0 && currentTaskIndex < static_cast<int>(tasks.size())) {
        AgentTask& task = tasks[currentTaskIndex];
        
        if (task.status == TaskStatus::InProgress && task.category == "Tool Usage") {
            // 執行工具操作
            std::unordered_map<std::string, std::string> params;
            for (const auto& [key, value] : task.context) {
                params[key] = value;
            }
            
            ToolExecutionResult result = UseTool(task.description, params);
            task.result = FormatToolOutput(result);
        }
    }
}

ToolExecutionResult ToolUserAgent::UseTool(const std::string& toolName, 
                                            const std::unordered_map<std::string, std::string>& parameters) {
    ToolExecutionResult result = AIAgent::UseTool(toolName, parameters);
    
    std::cout << "工具代理使用工具: " << toolName << std::endl;
    
    // 工具代理進行額外的工具驗證
    if (CanUseTool(toolName)) {
        auto it = availableTools.find(toolName);
        if (it != availableTools.end()) {
            bool valid = ValidateToolParameters(it->second, parameters);
            if (!valid) {
                result.success = false;
                result.error = "工具參數驗證失敗";
            }
        }
    }
    
    return result;
}

std::vector<std::string> ToolUserAgent::DiscoverAvailableTools() {
    std::vector<std::string> tools;
    
    std::lock_guard<std::mutex> lock(toolMutex);
    for (const auto& [name, desc] : availableTools) {
        tools.push_back(name);
    }
    
    return tools;
}

bool ToolUserAgent::ValidateToolParameters(const ToolDescription& tool, 
                                           const std::unordered_map<std::string, std::string>& parameters) {
    // 檢查所有必需參數是否存在
    for (const auto& requiredParam : tool.inputParameters) {
        if (parameters.find(requiredParam) == parameters.end()) {
            return false;
        }
    }
    
    return true;
}

std::string ToolUserAgent::FormatToolOutput(const ToolExecutionResult& result) {
    std::string output = "工具執行結果:\n";
    output += "工具: " + result.toolName + "\n";
    output += std::string("狀態: ") + (result.success ? "成功" : "失敗") + "\n";
    output += "執行時間: " + std::to_string(result.executionTime) + "ms\n";
    
    if (result.success) {
        output += "輸出: " + result.output + "\n";
    } else {
        output += "錯誤: " + result.error + "\n";
    }
    
    return output;
}

// ============================================================================
// AIAgentManager 增強實現
// ============================================================================

AIAgentManager::AIAgentManager()
    : learningEnabled(true)
    , peerLearningEnabled(true)
    , collaborationEnabled(true)
    , updateInterval(0.1f)
    , updateTimer(0.0f)
    , maxAgents(10) {
}

AIAgentManager::~AIAgentManager() {
    for (auto* agent : agents) {
        delete agent;
    }
    agents.clear();
    agentMap.clear();
}

bool AIAgentManager::Initialize() {
    std::cout << "初始化 AI 代理管理器" << std::endl;
    std::cout << "  - 學習系統: " << (learningEnabled ? "啟用" : "禁用") << std::endl;
    std::cout << "  - 同伴學習: " << (peerLearningEnabled ? "啟用" : "禁用") << std::endl;
    std::cout << "  - 協作模式: " << (collaborationEnabled ? "啟用" : "禁用") << std::endl;
    return true;
}

void AIAgentManager::Shutdown() {
    std::cout << "關閉 AI 代理管理器" << std::endl;
    
    std::lock_guard<std::mutex> lock(agentsMutex);
    for (auto* agent : agents) {
        agent->Shutdown();
        delete agent;
    }
    agents.clear();
    agentMap.clear();
}

AIAgent* AIAgentManager::CreateAgent(const AgentDesc& desc) {
    std::lock_guard<std::mutex> lock(agentsMutex);
    
    if (agents.size() >= maxAgents) {
        std::cout << "已達到最大代理數量: " << maxAgents << std::endl;
        return nullptr;
    }
    
    AIAgent* agent = nullptr;
    
    // 根據類型創建對應的代理
    switch (desc.type) {
        case AgentType::Developer:
            agent = new DeveloperAgent(desc);
            break;
        case AgentType::Designer:
            agent = new DesignerAgent(desc);
            break;
        case AgentType::Analyst:
            agent = new AnalystAgent(desc);
            break;
        case AgentType::Multimodal:
            agent = new MultimodalAgent(desc);
            break;
        case AgentType::Planner:
            agent = new PlannerAgent(desc);
            break;
        case AgentType::Communicator:
            agent = new CommunicatorAgent(desc);
            break;
        case AgentType::ToolUser:
            agent = new ToolUserAgent(desc);
            break;
        default:
            agent = new AIAgent(desc);
            break;
    }
    
    if (agent) {
        agent->Initialize();
        agents.push_back(agent);
        agentMap[desc.name] = agent;
        
        std::cout << "創建代理成功: " << desc.name << " (總數: " << agents.size() << ")" << std::endl;
    }
    
    return agent;
}

void AIAgentManager::DestroyAgent(const std::string& agentId) {
    std::lock_guard<std::mutex> lock(agentsMutex);
    
    auto it = agentMap.find(agentId);
    if (it != agentMap.end()) {
        AIAgent* agent = it->second;
        agent->Shutdown();
        
        agents.erase(std::remove(agents.begin(), agents.end(), agent), agents.end());
        agentMap.erase(it);
        
        delete agent;
        std::cout << "銷毀代理: " << agentId << std::endl;
    }
}

AIAgent* AIAgentManager::GetAgent(const std::string& agentId) {
    std::lock_guard<std::mutex> lock(agentsMutex);
    auto it = agentMap.find(agentId);
    return (it != agentMap.end()) ? it->second : nullptr;
}

void AIAgentManager::AssignTaskToAgent(const std::string& agentId, const AgentTask& task) {
    std::lock_guard<std::mutex> lock(agentsMutex);
    
    // 已持有 agentsMutex，直接查表避免遞迴上鎖死結
    auto it = agentMap.find(agentId);
    AIAgent* agent = (it != agentMap.end()) ? it->second : nullptr;
    if (agent) {
        agent->AssignTask(task);
        std::cout << "分配任務給代理: " << agentId << " - " << task.description << std::endl;
    }
}

void AIAgentManager::AssignTaskToBestAgent(const AgentTask& task) {
    std::lock_guard<std::mutex> lock(agentsMutex);
    
    AIAgent* bestAgent = FindBestAgentForTask(task);
    if (bestAgent) {
        bestAgent->AssignTask(task);
        std::cout << "分配任務給最佳代理: " << bestAgent->GetName() << std::endl;
    }
}

void AIAgentManager::AssignTaskToAllAgents(const AgentTask& task) {
    std::lock_guard<std::mutex> lock(agentsMutex);
    
    for (auto* agent : agents) {
        agent->AssignTask(task);
    }
    
    std::cout << "分配任務給所有代理: " << task.description << std::endl;
}

void AIAgentManager::AssignComplexTask(const ExecutionPlan& plan) {
    std::lock_guard<std::mutex> lock(agentsMutex);
    
    std::cout << "分配複雜任務計畫: " << plan.description << std::endl;
    
    for (const auto& node : plan.nodes) {
        AgentTask task;
        task.id = node.id;
        task.description = node.description;
        task.estimatedTime = static_cast<uint64_t>(node.estimatedDuration * 1000);
        
        if (!node.assignedAgent.empty()) {
            AssignTaskToAgent(node.assignedAgent, task);
        } else {
            AssignTaskToBestAgent(task);
        }
    }
}

Decision AIAgentManager::MakeGroupDecision(const std::string& context, const std::vector<std::string>& options) {
    std::vector<Decision> individualDecisions = GetIndividualDecisions(context, options);
    
    // 簡單的集體決策：選擇信心最高的
    Decision bestDecision;
    float maxConfidence = 0.0f;
    
    for (const auto& decision : individualDecisions) {
        if (decision.confidence > maxConfidence) {
            maxConfidence = decision.confidence;
            bestDecision = decision;
        }
    }
    
    bestDecision.reasoning.push_back("集體決策基於 " + std::to_string(individualDecisions.size()) + " 個代理的意見");
    
    return bestDecision;
}

std::vector<Decision> AIAgentManager::GetIndividualDecisions(const std::string& context, 
                                                           const std::vector<std::string>& options) {
    std::lock_guard<std::mutex> lock(agentsMutex);
    
    std::vector<Decision> decisions;
    
    for (auto* agent : agents) {
        if (agent->IsActive()) {
            Decision decision = agent->MakeDecision(context, options);
            decisions.push_back(decision);
        }
    }
    
    return decisions;
}

void AIAgentManager::EnableCollaboration(bool enable) {
    collaborationEnabled = enable;
    std::cout << "協作模式: " << (enable ? "啟用" : "禁用") << std::endl;
}

void AIAgentManager::InitiateCollaboration(const std::string& topic, const std::vector<std::string>& agentIds) {
    if (!collaborationEnabled) {
        std::cout << "協作模式已禁用" << std::endl;
        return;
    }
    
    std::cout << "啟動協作: " << topic << " (參與者: " << agentIds.size() << ")" << std::endl;
    
    collaborativeTopics.push_back(topic);
    
    // 讓參與的代理分享知識
    for (const auto& agentId : agentIds) {
        AIAgent* agent = GetAgent(agentId);
        if (agent && agent->CanCollaborate()) {
            std::string knowledge = "關於 " + topic + " 的專業知識";
            BroadcastKnowledge(knowledge, agentId);
        }
    }
}

void AIAgentManager::BroadcastKnowledge(const std::string& knowledge, const std::string& sourceAgentId) {
    std::lock_guard<std::mutex> lock(agentsMutex);
    
    // 已持有 agentsMutex，直接查表避免遞迴上鎖死結
    auto it = agentMap.find(sourceAgentId);
    AIAgent* sourceAgent = (it != agentMap.end()) ? it->second : nullptr;
    if (!sourceAgent) return;
    
    for (auto* agent : agents) {
        if (agent->GetName() != sourceAgentId && agent->CanLearnFromPeers()) {
            if (sourceAgent->CanCollaborateWith(agent)) {
                sourceAgent->ShareKnowledge(knowledge, agent);
            }
        }
    }
}

std::vector<std::string> AIAgentManager::GetCollaborativeInsights(const std::string& topic) {
    std::vector<std::string> insights;
    
    std::lock_guard<std::mutex> lock(agentsMutex);
    
    for (auto* agent : agents) {
        std::vector<MemoryItem> memories = agent->RetrieveMemory(topic, MemoryType::Semantic);
        for (const auto& memory : memories) {
            insights.push_back(memory.content);
        }
    }
    
    return insights;
}

void AIAgentManager::RegisterSharedTool(const ToolDescription& tool) {
    std::lock_guard<std::mutex> lock(toolsMutex);
    
    sharedTools[tool.name] = tool;
    std::cout << "註冊共享工具: " << tool.name << std::endl;
    
    // 讓所有能使用工具的代理註冊此工具
    std::lock_guard<std::mutex> agentLock(agentsMutex);
    for (auto* agent : agents) {
        if (agent->CanUseTool(tool.name)) {
            agent->RegisterTool(tool);
        }
    }
}

void AIAgentManager::UnregisterSharedTool(const std::string& toolName) {
    std::lock_guard<std::mutex> lock(toolsMutex);
    
    sharedTools.erase(toolName);
    std::cout << "註銷共享工具: " << toolName << std::endl;
}

std::vector<ToolDescription> AIAgentManager::GetAvailableTools() const {
    std::lock_guard<std::mutex> lock(toolsMutex);
    
    std::vector<ToolDescription> tools;
    for (const auto& [name, desc] : sharedTools) {
        tools.push_back(desc);
    }
    
    return tools;
}

void AIAgentManager::Update(float deltaTime) {
    updateTimer += deltaTime;
    
    if (updateTimer >= updateInterval) {
        updateTimer = 0.0f;
        
        std::lock_guard<std::mutex> lock(agentsMutex);
        
        for (auto* agent : agents) {
            if (agent->IsActive()) {
                agent->Update(updateInterval);
            }
        }
        
        if (collaborationEnabled) {
            FacilitateCollaboration();
        }
        
        if (peerLearningEnabled) {
            AggregateKnowledge();
        }
        
        UpdateAgentPerformance();
    }
}

void AIAgentManager::EnableLearning(bool enable) {
    learningEnabled = enable;
    std::cout << "學習系統: " << (enable ? "啟用" : "禁用") << std::endl;
}

bool AIAgentManager::IsLearningEnabled() const {
    return learningEnabled;
}

void AIAgentManager::RecordGroupLearning(const std::string& context, const std::string& action, bool success) {
    if (!learningEnabled) return;
    
    std::lock_guard<std::mutex> lock(agentsMutex);
    
    for (auto* agent : agents) {
        agent->RecordLearning(context, action, success);
    }
}

void AIAgentManager::EnablePeerLearning(bool enable) {
    peerLearningEnabled = enable;
    std::cout << "同伴學習: " << (enable ? "啟用" : "禁用") << std::endl;
}

size_t AIAgentManager::GetActiveAgentCount() const {
    std::lock_guard<std::mutex> lock(agentsMutex);
    
    size_t count = 0;
    for (const auto* agent : agents) {
        if (agent->IsActive()) {
            count++;
        }
    }
    
    return count;
}

size_t AIAgentManager::GetPendingTaskCount() const {
    std::lock_guard<std::mutex> lock(agentsMutex);
    
    size_t count = 0;
    for (const auto* agent : agents) {
        const auto& tasks = agent->GetTasks();
        for (const auto& task : tasks) {
            if (task.status == TaskStatus::Pending) {
                count++;
            }
        }
    }
    
    return count;
}

size_t AIAgentManager::GetCompletedTaskCount() const {
    std::lock_guard<std::mutex> lock(agentsMutex);
    
    size_t count = 0;
    for (const auto* agent : agents) {
        count += agent->GetCompletedTaskCount();
    }
    
    return count;
}

size_t AIAgentManager::GetTotalMemoryCount() const {
    std::lock_guard<std::mutex> lock(agentsMutex);
    
    size_t count = 0;
    for (const auto* agent : agents) {
        count += agent->GetMemoryCount();
    }
    
    return count;
}

size_t AIAgentManager::GetTotalToolCount() const {
    std::lock_guard<std::mutex> lock(agentsMutex);
    
    size_t count = 0;
    for (const auto* agent : agents) {
        count += agent->GetToolCount();
    }
    
    return count;
}

size_t AIAgentManager::GetKnowledgeShareCount() const {
    std::lock_guard<std::mutex> lock(agentsMutex);
    
    size_t count = 0;
    for (const auto* agent : agents) {
        count += agent->GetKnowledgeShareCount();
    }
    
    return count;
}

float AIAgentManager::GetAveragePerformanceRating() const {
    std::lock_guard<std::mutex> lock(agentsMutex);
    
    if (agents.empty()) return 0.0f;
    
    float total = 0.0f;
    for (const auto* agent : agents) {
        total += agent->GetPerformanceRating();
    }
    
    return total / static_cast<float>(agents.size());
}

void AIAgentManager::SetMaxAgents(size_t maxAgents) {
    this->maxAgents = maxAgents;
    std::cout << "設置最大代理數量: " << maxAgents << std::endl;
}

void AIAgentManager::SetUpdateInterval(float interval) {
    this->updateInterval = interval;
    std::cout << "設置更新間隔: " << interval << "s" << std::endl;
}

void AIAgentManager::SetCollaborationMode(bool enable) {
    collaborationEnabled = enable;
    std::cout << "設置協作模式: " << (enable ? "啟用" : "禁用") << std::endl;
}

AIAgent* AIAgentManager::FindBestAgentForTask(const AgentTask& task) {
    AIAgent* bestAgent = nullptr;
    float bestScore = 0.0f;
    
    for (auto* agent : agents) {
        if (!agent->IsActive()) continue;
        
        float score = agent->GetPerformanceRating();
        
        // 檢查技能匹配
        for (const auto& skill : task.requiredSkills) {
            for (const auto& capability : agent->GetCapabilities()) {
                if (capability == skill) {
                    score += 0.2f;
                    break;
                }
            }
        }
        
        if (score > bestScore) {
            bestScore = score;
            bestAgent = agent;
        }
    }
    
    return bestAgent;
}

void AIAgentManager::UpdateAgentPerformance() {
    // 基於任務完成率更新性能評分
    for (auto* agent : agents) {
        float successRate = agent->GetSuccessRate();
        float currentRating = agent->GetPerformanceRating();
        
        // 緩慢調整評分
        float newRating = currentRating * 0.9f + successRate * 0.1f;
        
        // 這裡應該有設置評分的方法，目前為模擬
        std::cout << "更新代理 " << agent->GetName() << " 性能: " 
                  << currentRating << " -> " << newRating << std::endl;
    }
}

void AIAgentManager::FacilitateCollaboration() {
    // 促進代理之間的協作
    for (size_t i = 0; i < agents.size(); i++) {
        for (size_t j = i + 1; j < agents.size(); j++) {
            if (agents[i]->CanCollaborateWith(agents[j])) {
                // 自動分享相關知識
                if (agents[i]->GetKnowledgeShareCount() < 10) {
                    std::string knowledge = "協作知識分享";
                    agents[i]->ShareKnowledge(knowledge, agents[j]);
                }
            }
        }
    }
}

void AIAgentManager::AggregateKnowledge() {
    // 聚合知識並識別有用的模式
    std::cout << "聚合團體知識..." << std::endl;
    
    // 這裡可以實現更複雜的知識聚合邏輯
    // 目前為簡單的模擬
}

// ============================================================================
// AIAgentSystem 增強實現
// ============================================================================

AIAgentSystem::AIAgentSystem()
    : agentManager(std::make_unique<AIAgentManager>())
    , eventBus(nullptr)
    , initialized(false)
    , multimodalEnabled(false)
    , advancedPlanningEnabled(false)
    , toolIntegrationEnabled(false)
    , debugMode(false) {
}

AIAgentSystem::~AIAgentSystem() {
    Shutdown();
}

bool AIAgentSystem::Initialize() {
    std::cout << "初始化 AI 代理系統" << std::endl;
    
    if (!agentManager->Initialize()) {
        return false;
    }
    
    initialized = true;
    return true;
}

void AIAgentSystem::Shutdown() {
    if (!initialized) return;
    
    std::cout << "關閉 AI 代理系統" << std::endl;
    
    if (agentManager) {
        agentManager->Shutdown();
    }
    
    initialized = false;
}

void AIAgentSystem::Update(float deltaTime) {
    if (!initialized) return;
    
    agentManager->Update(deltaTime);
}

void AIAgentSystem::SetEventBus(void* eventBus) {
    this->eventBus = eventBus;
    std::cout << "設置事件匯流排" << std::endl;
}

void AIAgentSystem::SetMaxAgents(size_t maxAgents) {
    agentManager->SetMaxAgents(maxAgents);
}

void AIAgentSystem::SetLearningEnabled(bool enabled) {
    agentManager->EnableLearning(enabled);
}

void AIAgentSystem::SetCollaborationEnabled(bool enabled) {
    agentManager->SetCollaborationMode(enabled);
}

void AIAgentSystem::EnableMultimodalProcessing(bool enable) {
    multimodalEnabled = enable;
    std::cout << "多模態處理: " << (enable ? "啟用" : "禁用") << std::endl;
}

void AIAgentSystem::EnableAdvancedPlanning(bool enable) {
    advancedPlanningEnabled = enable;
    std::cout << "高級計畫: " << (enable ? "啟用" : "禁用") << std::endl;
}

void AIAgentSystem::EnableToolIntegration(bool enable) {
    toolIntegrationEnabled = enable;
    std::cout << "工具整合: " << (enable ? "啟用" : "禁用") << std::endl;
}

void AIAgentSystem::SetGlobalContext(const std::unordered_map<std::string, std::string>& context) {
    globalContext = context;
    std::cout << "設置全局上下文: " << context.size() << " 項" << std::endl;
}

void AIAgentSystem::PrintStatistics() {
    std::cout << "=== AI 代理系統統計 ===" << std::endl;
    std::cout << "代理總數: " << agentManager->GetAgentCount() << std::endl;
    std::cout << "活躍代理: " << agentManager->GetActiveAgentCount() << std::endl;
    std::cout << "待處理任務: " << agentManager->GetPendingTaskCount() << std::endl;
    std::cout << "已完成任務: " << agentManager->GetCompletedTaskCount() << std::endl;
    std::cout << "總記憶數: " << agentManager->GetTotalMemoryCount() << std::endl;
    std::cout << "總工具數: " << agentManager->GetTotalToolCount() << std::endl;
    std::cout << "知識分享: " << agentManager->GetKnowledgeShareCount() << std::endl;
    std::cout << "平均性能: " << agentManager->GetAveragePerformanceRating() << std::endl;
    std::cout << "=====================" << std::endl;
}

std::string AIAgentSystem::GetSystemReport() {
    std::stringstream report;
    
    report << "=== AI 代理系統報告 ===\n";
    report << "系統狀態: " << (initialized ? "運行中" : "已停止") << "\n";
    report << "多模態處理: " << (multimodalEnabled ? "啟用" : "禁用") << "\n";
    report << "高級計畫: " << (advancedPlanningEnabled ? "啟用" : "禁用") << "\n";
    report << "工具整合: " << (toolIntegrationEnabled ? "啟用" : "禁用") << "\n";
    report << "調試模式: " << (debugMode ? "啟用" : "禁用") << "\n";
    report << "======================\n";
    report << "代理統計:\n";
    report << "  總數: " << agentManager->GetAgentCount() << "\n";
    report << "  活躍: " << agentManager->GetActiveAgentCount() << "\n";
    report << "  平均性能: " << agentManager->GetAveragePerformanceRating() << "\n";
    report << "======================\n";
    report << "任務統計:\n";
    report << "  待處理: " << agentManager->GetPendingTaskCount() << "\n";
    report << "  已完成: " << agentManager->GetCompletedTaskCount() << "\n";
    report << "======================\n";
    report << "知識統計:\n";
    report << "  總記憶: " << agentManager->GetTotalMemoryCount() << "\n";
    report << "  總工具: " << agentManager->GetTotalToolCount() << "\n";
    report << "  知識分享: " << agentManager->GetKnowledgeShareCount() << "\n";
    report << "======================\n";
    
    return report.str();
}

void AIAgentSystem::EnableDebugMode(bool enable) {
    debugMode = enable;
    std::cout << "調試模式: " << (enable ? "啟用" : "禁用") << std::endl;
}

void AIAgentSystem::LogAgentStates() {
    if (!debugMode) return;
    
    std::cout << "=== 代理狀態日誌 ===" << std::endl;
    
    const auto& agents = agentManager->GetAgents();
    for (const auto* agent : agents) {
        std::cout << "代理: " << agent->GetName() << std::endl;
        std::cout << "  活躍: " << (agent->IsActive() ? "是" : "否") << std::endl;
        std::cout << "  任務數: " << agent->GetTasks().size() << std::endl;
        std::cout << "  成功率: " << agent->GetSuccessRate() << std::endl;
        std::cout << "  記憶數: " << agent->GetMemoryCount() << std::endl;
    }
    
    std::cout << "===================" << std::endl;
}

void AIAgentSystem::LogTaskProgress() {
    if (!debugMode) return;
    
    std::cout << "=== 任務進度日誌 ===" << std::endl;
    
    const auto& agents = agentManager->GetAgents();
    for (const auto* agent : agents) {
        const auto& tasks = agent->GetTasks();
        for (const auto& task : tasks) {
            std::cout << "任務: " << task.description << std::endl;
            std::cout << "  狀態: " << static_cast<int>(task.status) << std::endl;
            std::cout << "  進度: " << task.progress * 100 << "%" << std::endl;
        }
    }
    
    std::cout << "===================" << std::endl;
}

// ============================================================================
// AIAgentFactory 實現
// ============================================================================

AIAgent* AIAgentFactory::CreateAgent(AgentType type, const AgentDesc& desc) {
    switch (type) {
        case AgentType::Developer:
            return new DeveloperAgent(desc);
        case AgentType::Designer:
            return new DesignerAgent(desc);
        case AgentType::Analyst:
            return new AnalystAgent(desc);
        case AgentType::Multimodal:
            return new MultimodalAgent(desc);
        case AgentType::Planner:
            return new PlannerAgent(desc);
        case AgentType::Communicator:
            return new CommunicatorAgent(desc);
        case AgentType::ToolUser:
            return new ToolUserAgent(desc);
        default:
            return new AIAgent(desc);
    }
}

std::vector<AgentType> AIAgentFactory::GetAvailableAgentTypes() {
    return {
        AgentType::Developer,
        AgentType::Designer,
        AgentType::Analyst,
        AgentType::Tester,
        AgentType::Debugger,
        AgentType::Researcher,
        AgentType::Multimodal,
        AgentType::Planner,
        AgentType::Communicator,
        AgentType::ToolUser
    };
}

AgentDesc AIAgentFactory::GetDefaultDescription(AgentType type) {
    AgentDesc desc;
    desc.type = type;
    
    switch (type) {
        case AgentType::Developer:
            desc.name = "DeveloperAgent";
            desc.capabilities = {"Code Generation", "Code Analysis", "Optimization"};
            desc.specializations = {"C++", "Python", "Software Architecture"};
            break;
        case AgentType::Designer:
            desc.name = "DesignerAgent";
            desc.capabilities = {"Design Generation", "Brainstorming", "Prototyping"};
            desc.specializations = {"UI Design", "UX Design", "Game Design"};
            desc.creativityLevel = 0.8f;
            break;
        case AgentType::Analyst:
            desc.name = "AnalystAgent";
            desc.capabilities = {"Data Analysis", "Insight Generation", "Pattern Recognition"};
            desc.specializations = {"Statistics", "Machine Learning", "Business Intelligence"};
            break;
        case AgentType::Multimodal:
            desc.name = "MultimodalAgent";
            desc.capabilities = {"Image Processing", "Audio Processing", "Video Processing"};
            desc.specializations = {"Computer Vision", "Speech Recognition", "Video Analysis"};
            break;
        case AgentType::Planner:
            desc.name = "PlannerAgent";
            desc.capabilities = {"Task Planning", "Resource Allocation", "Dependency Management"};
            desc.specializations = {"Project Management", "Workflow Optimization", "Strategic Planning"};
            break;
        case AgentType::Communicator:
            desc.name = "CommunicatorAgent";
            desc.capabilities = {"Natural Language Processing", "Dialog Management", "Sentiment Analysis"};
            desc.specializations = {"Customer Service", "Translation", "Content Generation"};
            break;
        case AgentType::ToolUser:
            desc.name = "ToolUserAgent";
            desc.capabilities = {"Tool Integration", "API Interaction", "External Services"};
            desc.specializations = {"API Development", "System Integration", "Automation"};
            break;
        default:
            desc.name = "CustomAgent";
            break;
    }
    
    return desc;
}

// ============================================================================
// AIAgentCoordinator 實現
// ============================================================================

AIAgentCoordinator::AIAgentCoordinator() {
    std::cout << "初始化 AI 代理協調器" << std::endl;
}

AIAgentCoordinator::~AIAgentCoordinator() {
    std::cout << "關閉 AI 代理協調器" << std::endl;
}

void AIAgentCoordinator::RegisterAgent(AIAgent* agent) {
    std::lock_guard<std::mutex> lock(coordinatorMutex);
    
    if (agent) {
        registeredAgents[agent->GetName()] = agent;
        std::cout << "註冊代理到協調器: " << agent->GetName() << std::endl;
    }
}

void AIAgentCoordinator::UnregisterAgent(const std::string& agentId) {
    std::lock_guard<std::mutex> lock(coordinatorMutex);
    
    auto it = registeredAgents.find(agentId);
    if (it != registeredAgents.end()) {
        registeredAgents.erase(it);
        std::cout << "從協調器註銷代理: " << agentId << std::endl;
    }
}

void AIAgentCoordinator::CoordinateTask(const AgentTask& task) {
    std::lock_guard<std::mutex> lock(coordinatorMutex);
    
    std::cout << "協調任務: " << task.description << std::endl;
    
    // 根據任務需求選擇最合適的代理團隊
    std::vector<std::string> teamIds = SuggestAgentTeam(task.category);
    
    // 分配任務給團隊
    for (const auto& agentId : teamIds) {
        auto it = registeredAgents.find(agentId);
        if (it != registeredAgents.end()) {
            it->second->AssignTask(task);
        }
    }
}

void AIAgentCoordinator::CoordinateCollaboration(const std::string& topic, const std::vector<std::string>& agentIds) {
    std::lock_guard<std::mutex> lock(coordinatorMutex);
    
    std::cout << "協調協作: " << topic << " (參與者: " << agentIds.size() << ")" << std::endl;
    
    // 檢查代理之間的協作兼容性
    for (size_t i = 0; i < agentIds.size(); i++) {
        for (size_t j = i + 1; j < agentIds.size(); j++) {
            auto agentA = registeredAgents.find(agentIds[i]);
            auto agentB = registeredAgents.find(agentIds[j]);
            
            if (agentA != registeredAgents.end() && agentB != registeredAgents.end()) {
                if (agentA->second->CanCollaborateWith(agentB->second)) {
                    std::cout << "  - " << agentIds[i] << " 可以與 " << agentIds[j] << " 協作" << std::endl;
                } else {
                    std::cout << "  - " << agentIds[i] << " 不能與 " << agentIds[j] << " 協作" << std::endl;
                }
            }
        }
    }
}

std::vector<std::string> AIAgentCoordinator::SuggestAgentTeam(const std::string& taskType) {
    std::vector<std::string> teamIds;
    
    // 簡單的團隊建議邏輯
    for (const auto& [id, agent] : registeredAgents) {
        // 檢查代理是否適合該任務類型
        const auto& capabilities = agent->GetCapabilities();
        for (const auto& capability : capabilities) {
            if (capability == taskType || capability.find(taskType) != std::string::npos) {
                teamIds.push_back(id);
                break;
            }
        }
    }
    
    return teamIds;
}

void AIAgentCoordinator::OptimizeResourceAllocation() {
    std::lock_guard<std::mutex> lock(coordinatorMutex);
    
    std::cout << "優化資源分配..." << std::endl;
    
    // 基於代理性能和負載優化資源分配
    for (auto& [id, agent] : registeredAgents) {
        float performance = agent->GetPerformanceRating();
        size_t taskCount = agent->GetTasks().size();
        
        std::cout << "代理 " << id << ": 性能=" << performance 
                  << ", 任務=" << taskCount << std::endl;
        
        // 根據性能和負載調整任務分配
        if (performance > 0.8f && taskCount < 3) {
            std::cout << "  - 可以接受更多任務" << std::endl;
        } else if (performance < 0.5f || taskCount > 5) {
            std::cout << "  - 需要減少任務負載" << std::endl;
        }
    }
}


// ============================================================================
// DeveloperAgent 實現
// ============================================================================

DeveloperAgent::DeveloperAgent(const AgentDesc& desc)
    : AIAgent(desc) {
}

Decision DeveloperAgent::MakeDecision(const std::string& context, const std::vector<std::string>& options) {
    Decision decision = AIAgent::MakeDecision(context, options);
    
    // 開發代理的決策可能考慮代碼效率和最佳實踐
    decision.reasoning.push_back("考慮代碼效率和最佳實踐");
    
    return decision;
}

void DeveloperAgent::ProcessCurrentTask() {
    AIAgent::ProcessCurrentTask();
    
    std::lock_guard<std::mutex> lock(taskMutex);
    
    if (currentTaskIndex >= 0 && currentTaskIndex < static_cast<int>(tasks.size())) {
        AgentTask& task = tasks[currentTaskIndex];
        
        if (task.status == TaskStatus::InProgress && task.category == "Code Generation") {
            std::string code = GenerateCode(task.description);
            if (!code.empty()) {
                CompleteTask("生成代碼: " + code);
            }
        }
    }
}

std::string DeveloperAgent::GenerateCode(const std::string& description) {
    std::cout << "開發代理生成代碼: " << description << std::endl;
    
    // 簡化的代碼生成邏輯
    std::string code = "void " + description + "() {\n    // Implementation\n}";
    
    return code;
}

bool DeveloperAgent::OptimizeCode(const std::string& code) {
    std::cout << "開發代理優化代碼" << std::endl;
    return true;
}

// ============================================================================
// DesignerAgent 實現
// ============================================================================

DesignerAgent::DesignerAgent(const AgentDesc& desc)
    : AIAgent(desc) {
}

Decision DesignerAgent::MakeDecision(const std::string& context, const std::vector<std::string>& options) {
    Decision decision = AIAgent::MakeDecision(context, options);
    
    // 設計代理的決策可能考慮用戶體驗和創意性
    decision.reasoning.push_back("考慮用戶體驗和創意性");
    
    return decision;
}

void DesignerAgent::ProcessCurrentTask() {
    AIAgent::ProcessCurrentTask();
    
    std::lock_guard<std::mutex> lock(taskMutex);
    
    if (currentTaskIndex >= 0 && currentTaskIndex < static_cast<int>(tasks.size())) {
        AgentTask& task = tasks[currentTaskIndex];
        
        if (task.status == TaskStatus::InProgress && task.category == "Design") {
            std::string design = GenerateDesign(task.description);
            if (!design.empty()) {
                CompleteTask("生成設計: " + design);
            }
        }
    }
}

std::string DesignerAgent::GenerateDesign(const std::string& description) {
    std::cout << "設計代理生成設計: " << description << std::endl;
    
    // 簡化的設計生成邏輯
    std::string design = "Design concept for: " + description;
    
    return design;
}

std::vector<std::string> DesignerAgent::BrainstormIdeas(const std::string& topic) {
    std::cout << "設計代理頭腦風暴: " << topic << std::endl;
    
    std::vector<std::string> ideas;
    ideas.push_back("Idea 1 for " + topic);
    ideas.push_back("Idea 2 for " + topic);
    ideas.push_back("Idea 3 for " + topic);
    
    return ideas;
}

// ============================================================================
// AnalystAgent 實現
// ============================================================================

AnalystAgent::AnalystAgent(const AgentDesc& desc)
    : AIAgent(desc) {
}

Decision AnalystAgent::MakeDecision(const std::string& context, const std::vector<std::string>& options) {
    Decision decision = AIAgent::MakeDecision(context, options);
    
    // 分析代理的決策可能考慮數據驅證和統計
    decision.reasoning.push_back("考慮數據驗證和統計");
    
    return decision;
}

void AnalystAgent::ProcessCurrentTask() {
    AIAgent::ProcessCurrentTask();
    
    std::lock_guard<std::mutex> lock(taskMutex);
    
    if (currentTaskIndex >= 0 && currentTaskIndex < static_cast<int>(tasks.size())) {
        AgentTask& task = tasks[currentTaskIndex];
        
        if (task.status == TaskStatus::InProgress && task.category == "Analysis") {
            std::string analysis = AnalyzeData(task.description);
            if (!analysis.empty()) {
                CompleteTask("分析結果: " + analysis);
            }
        }
    }
}

std::string AnalystAgent::AnalyzeData(const std::string& data) {
    std::cout << "分析代理分析數據: " << data << std::endl;
    
    // 簡化的數據分析邏輯
    std::string analysis = "Analysis of: " + data;
    
    return analysis;
}

std::vector<std::string> AnalystAgent::GenerateInsights(const std::string& analysis) {
    std::cout << "分析代理生成洞察: " << analysis << std::endl;
    
    std::vector<std::string> insights;
    insights.push_back("Insight 1 from " + analysis);
    insights.push_back("Insight 2 from " + analysis);
    
    return insights;
}

} // namespace AI
} // namespace Potato
