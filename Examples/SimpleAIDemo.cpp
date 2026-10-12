/**
 * Simple AI Agent Demo
 * 簡化的 AI Agent 功能演示
 */

#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <random>
#include <chrono>
#include <thread>

// 簡化的 AI 代理類型
enum class AgentType {
    Developer, Designer, Analyst, Multimodal, Planner, Communicator, ToolUser, Custom
};

// 任務優先級
enum class TaskPriority {
    Low = 0, Medium = 1, High = 2, Critical = 3
};

// 任務狀態
enum class TaskStatus {
    Pending, InProgress, Completed, Failed, Cancelled
};

// 簡化的任務結構
struct SimpleTask {
    std::string id;
    std::string description;
    std::string category;
    TaskPriority priority;
    TaskStatus status;
    float progress;
    
    SimpleTask()
        : priority(TaskPriority::Medium)
        , status(TaskStatus::Pending)
        , progress(0.0f) {
    }
};

// 簡化的代理描述
struct AgentDesc {
    std::string name;
    AgentType type;
    float performanceRating;
    float creativityLevel;
    float riskTolerance;
    bool canUseTools;
    bool canCollaborate;
    std::vector<std::string> capabilities;
    std::vector<std::string> specializations;
    
    AgentDesc()
        : type(AgentType::Custom)
        , performanceRating(0.8f)
        , creativityLevel(0.5f)
        , riskTolerance(0.5f)
        , canUseTools(true)
        , canCollaborate(true) {
    }
};

// 簡化的 AI 代理類
class SimpleAIAgent {
public:
    SimpleAIAgent(const AgentDesc& desc)
        : name(desc.name)
        , type(desc.type)
        , performanceRating(desc.performanceRating)
        , creativityLevel(desc.creativityLevel)
        , riskTolerance(desc.riskTolerance)
        , canUseTools(desc.canUseTools)
        , canCollaborate(desc.canCollaborate)
        , capabilities(desc.capabilities)
        , specializations(desc.specializations)
        , active(true)
        , completedTaskCount(0)
        , knowledgeShareCount(0) {
        
        std::cout << "創建 AI 代理: " << name << " (類型: " << static_cast<int>(type) << ")" << std::endl;
        std::cout << "  - 性能評分: " << performanceRating << std::endl;
        std::cout << "  - 創造力: " << creativityLevel << std::endl;
        std::cout << "  - 風險承受度: " << riskTolerance << std::endl;
        std::cout << "  - 工具使用: " << (canUseTools ? "是" : "否") << std::endl;
        std::cout << "  - 協作能力: " << (canCollaborate ? "是" : "否") << std::endl;
        std::cout << "  - 專業領域: " << specializations.size() << " 個" << std::endl;
    }
    
    void AssignTask(const SimpleTask& task) {
        tasks.push_back(task);
        std::cout << name << " 接受任務: " << task.description << std::endl;
    }
    
    void ProcessCurrentTask() {
        if (tasks.empty()) return;
        
        SimpleTask& task = tasks.back();
        if (task.status == TaskStatus::Pending) {
            task.status = TaskStatus::InProgress;
            std::cout << name << " 開始處理任務: " << task.description << std::endl;
        }
        
        if (task.status == TaskStatus::InProgress) {
            // 基於性能評分調整進度
            task.progress += 0.1f * performanceRating;
            
            if (task.progress >= 1.0f) {
                task.progress = 1.0f;
                task.status = TaskStatus::Completed;
                completedTaskCount++;
                std::cout << name << " 完成任務: " << task.description << std::endl;
            }
        }
    }
    
    bool CanCollaborateWith(const SimpleAIAgent* other) const {
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
    
    void ShareKnowledge(const std::string& knowledge, SimpleAIAgent* recipient) {
        if (!canCollaborate || !recipient) return;
        
        std::cout << name << " 向 " << recipient->GetName() << " 分享知識: " << knowledge << std::endl;
        knowledgeShareCount++;
    }
    
    // Getters
    std::string GetName() const { return name; }
    float GetPerformanceRating() const { return performanceRating; }
    size_t GetCompletedTaskCount() const { return completedTaskCount; }
    size_t GetKnowledgeShareCount() const { return knowledgeShareCount; }
    const std::vector<SimpleTask>& GetTasks() const { return tasks; }
    const std::vector<std::string>& GetCapabilities() const { return capabilities; }
    bool IsActive() const { return active; }
    
private:
    std::string name;
    AgentType type;
    float performanceRating;
    float creativityLevel;
    float riskTolerance;
    bool canUseTools;
    bool canCollaborate;
    std::vector<std::string> capabilities;
    std::vector<std::string> specializations;
    bool active;
    size_t completedTaskCount;
    size_t knowledgeShareCount;
    std::vector<SimpleTask> tasks;
};

// 簡化的代理管理器
class SimpleAgentManager {
public:
    SimpleAgentManager() {
        std::cout << "初始化 AI 代理管理器" << std::endl;
    }
    
    ~SimpleAgentManager() {
        for (auto* agent : agents) {
            delete agent;
        }
        agents.clear();
    }
    
    SimpleAIAgent* CreateAgent(const AgentDesc& desc) {
        SimpleAIAgent* agent = new SimpleAIAgent(desc);
        agents.push_back(agent);
        std::cout << "管理器創建代理: " << desc.name << " (總數: " << agents.size() << ")" << std::endl;
        return agent;
    }
    
    void AssignTaskToBestAgent(const SimpleTask& task) {
        SimpleAIAgent* bestAgent = FindBestAgentForTask(task);
        if (bestAgent) {
            bestAgent->AssignTask(task);
            std::cout << "分配任務給最佳代理: " << bestAgent->GetName() << std::endl;
        }
    }
    
    void Update(float deltaTime) {
        for (auto* agent : agents) {
            if (agent->IsActive()) {
                agent->ProcessCurrentTask();
            }
        }
    }
    
    size_t GetAgentCount() const { return agents.size(); }
    size_t GetActiveAgentCount() const {
        size_t count = 0;
        for (const auto* agent : agents) {
            if (agent->IsActive()) count++;
        }
        return count;
    }
    
    size_t GetTotalCompletedTasks() const {
        size_t count = 0;
        for (const auto* agent : agents) {
            count += agent->GetCompletedTaskCount();
        }
        return count;
    }
    
    const std::vector<SimpleAIAgent*>& GetAgents() const { return agents; }
    
private:
    SimpleAIAgent* FindBestAgentForTask(const SimpleTask& task) {
        SimpleAIAgent* bestAgent = nullptr;
        float bestScore = 0.0f;
        
        for (auto* agent : agents) {
            if (!agent->IsActive()) continue;
            
            float score = agent->GetPerformanceRating();
            
            // 檢查專業領域匹配
            for (const auto& capability : agent->GetCapabilities()) {
                if (capability == task.category) {
                    score += 0.2f;
                    break;
                }
            }
            
            if (score > bestScore) {
                bestScore = score;
                bestAgent = agent;
            }
        }
        
        return bestAgent;
    }
    
    std::vector<SimpleAIAgent*> agents;
};

int main() {
    std::cout << "=== 增強版 AI Agent 系統演示 ===" << std::endl;
    
    // 創建代理管理器
    SimpleAgentManager manager;
    
    // 創建增強代理
    std::cout << "\n--- 創建增強代理 ---" << std::endl;
    
    AgentDesc developerDesc;
    developerDesc.name = "CodeMaster";
    developerDesc.type = AgentType::Developer;
    developerDesc.performanceRating = 0.9f;
    developerDesc.capabilities = {"Code Generation", "Code Analysis", "Optimization"};
    developerDesc.specializations = {"C++", "Python", "Software Architecture"};
    developerDesc.canUseTools = true;
    developerDesc.canCollaborate = true;
    
    SimpleAIAgent* developer = manager.CreateAgent(developerDesc);
    
    AgentDesc designerDesc;
    designerDesc.name = "CreativeMind";
    designerDesc.type = AgentType::Designer;
    designerDesc.performanceRating = 0.85f;
    designerDesc.creativityLevel = 0.9f;
    designerDesc.capabilities = {"Design Generation", "Brainstorming", "Prototyping"};
    designerDesc.specializations = {"UI Design", "UX Design", "Game Design"};
    designerDesc.canCollaborate = true;
    
    SimpleAIAgent* designer = manager.CreateAgent(designerDesc);
    
    AgentDesc multimodalDesc;
    multimodalDesc.name = "VisionAI";
    multimodalDesc.type = AgentType::Multimodal;
    multimodalDesc.performanceRating = 0.8f;
    multimodalDesc.capabilities = {"Image Processing", "Audio Processing", "Video Processing"};
    multimodalDesc.specializations = {"Computer Vision", "Speech Recognition", "Video Analysis"};
    multimodalDesc.canUseTools = true;
    multimodalDesc.canCollaborate = true;
    
    SimpleAIAgent* multimodal = manager.CreateAgent(multimodalDesc);
    
    AgentDesc plannerDesc;
    plannerDesc.name = "StrategicPlanner";
    plannerDesc.type = AgentType::Planner;
    plannerDesc.performanceRating = 0.88f;
    plannerDesc.riskTolerance = 0.3f;
    plannerDesc.capabilities = {"Task Planning", "Resource Allocation", "Dependency Management"};
    plannerDesc.specializations = {"Project Management", "Workflow Optimization", "Strategic Planning"};
    plannerDesc.canCollaborate = true;
    
    SimpleAIAgent* planner = manager.CreateAgent(plannerDesc);
    
    // 分配任務
    std::cout << "\n--- 分配任務 ---" << std::endl;
    
    SimpleTask task1;
    task1.id = "task_001";
    task1.description = "實現渲染系統";
    task1.category = "Code Generation";
    task1.priority = TaskPriority::High;
    
    manager.AssignTaskToBestAgent(task1);
    
    SimpleTask task2;
    task2.id = "task_002";
    task2.description = "設計用戶界面";
    task2.category = "Design Generation";
    task2.priority = TaskPriority::Medium;
    
    manager.AssignTaskToBestAgent(task2);
    
    SimpleTask task3;
    task3.id = "task_003";
    task3.description = "分析圖像數據";
    task3.category = "Image Processing";
    task3.priority = TaskPriority::High;
    
    manager.AssignTaskToBestAgent(task3);
    
    SimpleTask task4;
    task4.id = "task_004";
    task4.description = "制定開發計畫";
    task4.category = "Task Planning";
    task4.priority = TaskPriority::Critical;
    
    manager.AssignTaskToBestAgent(task4);
    
    // 協作演示
    std::cout << "\n--- 協作演示 ---" << std::endl;
    
    std::cout << "檢查協作兼容性:" << std::endl;
    std::cout << "  - CodeMaster <-> CreativeMind: " 
              << (developer->CanCollaborateWith(designer) ? "兼容" : "不兼容") << std::endl;
    std::cout << "  - CodeMaster <-> VisionAI: " 
              << (developer->CanCollaborateWith(multimodal) ? "兼容" : "不兼容") << std::endl;
    std::cout << "  - CreativeMind <-> StrategicPlanner: " 
              << (designer->CanCollaborateWith(planner) ? "兼容" : "不兼容") << std::endl;
    
    // 知識分享
    std::cout << "\n--- 知識分享 ---" << std::endl;
    
    developer->ShareKnowledge("C++ 最佳實踐", multimodal);
    designer->ShareKnowledge("用戶體驗設計原則", planner);
    multimodal->ShareKnowledge("圖像處理算法", developer);
    
    // 處理任務
    std::cout << "\n--- 處理任務 ---" << std::endl;
    
    for (int i = 0; i < 15; i++) {
        manager.Update(0.1f);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // 系統統計
    std::cout << "\n=== 系統統計 ===" << std::endl;
    std::cout << "代理總數: " << manager.GetAgentCount() << std::endl;
    std::cout << "活躍代理: " << manager.GetActiveAgentCount() << std::endl;
    std::cout << "總完成任務: " << manager.GetTotalCompletedTasks() << std::endl;
    
    std::cout << "\n=== 代理詳細統計 ===" << std::endl;
    for (const auto* agent : manager.GetAgents()) {
        std::cout << agent->GetName() << ":" << std::endl;
        std::cout << "  - 完成任務: " << agent->GetCompletedTaskCount() << std::endl;
        std::cout << "  - 知識分享: " << agent->GetKnowledgeShareCount() << std::endl;
        std::cout << "  - 性能評分: " << agent->GetPerformanceRating() << std::endl;
    }
    
    std::cout << "\n=== 增強版 AI Agent 系統演示完成 ===" << std::endl;
    
    return 0;
}
