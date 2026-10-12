/**
 * Enhanced AI Agent Example
 * 展示增強版 AI Agent 系統的功能
 */

#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <thread>

// 簡化的 AI Agent 系統接口（實際應該包含完整的頭文件）
namespace Potato {
namespace AI {

    // 簡化的類型定義（實際應該從 AIAgentSystem.h 導入）
    enum class AgentType {
        Developer, Designer, Analyst, Multimodal, Planner, Communicator, ToolUser, Custom
    };

    enum class TaskPriority {
        Low = 0, Medium = 1, High = 2, Critical = 3
    };

    enum class TaskStatus {
        Pending, InProgress, Completed, Failed, Cancelled, Blocked, Retry
    };

    enum class PerceptionType {
        Text, Image, Audio, Video, StructuredData, UnstructuredData
    };

    enum class MemoryType {
        ShortTerm, LongTerm, Episodic, Semantic, Procedural
    };

    enum class ToolType {
        CodeGeneration, CodeAnalysis, FileOperation, WebSearch, DatabaseQuery, APIRequest, DataProcessing, Custom
    };

    struct AgentTask {
        std::string id;
        std::string description;
        std::string category;
        TaskPriority priority;
        TaskStatus status;
        float progress;
        std::string result;
        std::string error;
        uint64_t createdTime;
        uint64_t completedTime;
        uint64_t estimatedTime;
        std::vector<std::string> dependencies;
        std::vector<std::string> requiredSkills;
        int retryCount;
        int maxRetries;

        AgentTask()
            : priority(TaskPriority::Medium)
            , status(TaskStatus::Pending)
            , progress(0.0f)
            , createdTime(0)
            , completedTime(0)
            , estimatedTime(0)
            , retryCount(0)
            , maxRetries(3) {
        }
    };

    struct AgentDesc {
        std::string name;
        AgentType type;
        bool autonomous;
        float performanceRating;
        float learningRate;
        std::vector<std::string> capabilities;
        std::vector<std::string> specializations;
        float creativityLevel;
        float riskTolerance;
        bool canUseTools;
        bool canCollaborate;
        bool canLearnFromPeers;
        int maxConcurrentTasks;

        AgentDesc()
            : type(AgentType::Custom)
            , autonomous(true)
            , performanceRating(0.8f)
            , learningRate(0.1f)
            , creativityLevel(0.5f)
            , riskTolerance(0.5f)
            , canUseTools(true)
            , canCollaborate(true)
            , canLearnFromPeers(true)
            , maxConcurrentTasks(3) {
        }
    };

    struct PerceptionData {
        PerceptionType type;
        std::string textData;
        std::string source;
        uint64_t timestamp;
        float confidence;

        PerceptionData()
            : type(PerceptionType::Text)
            , timestamp(0)
            , confidence(1.0f) {
        }
    };

    struct MemoryItem {
        MemoryType type;
        std::string content;
        std::vector<std::string> tags;
        uint64_t timestamp;
        float importance;
        float accessibility;

        MemoryItem()
            : type(MemoryType::ShortTerm)
            , timestamp(0)
            , importance(0.5f)
            , accessibility(0.5f) {
        }
    };

    struct ToolDescription {
        std::string name;
        ToolType type;
        std::string description;
        std::vector<std::string> requiredCapabilities;
        std::vector<std::string> inputParameters;
        std::vector<std::string> outputParameters;
        bool available;

        ToolDescription()
            : type(ToolType::Custom)
            , available(true) {
        }
    };

    struct ToolExecutionResult {
        std::string toolName;
        bool success;
        std::string output;
        std::string error;
        uint64_t executionTime;

        ToolExecutionResult()
            : success(false)
            , executionTime(0) {
        }
    };

    struct ExecutionPlan {
        std::string id;
        std::string description;
        std::string goal;
        std::vector<std::string> nodes;
        std::vector<std::string> requiredAgents;
        uint64_t startTime;
        uint64_t estimatedCompletionTime;
        float overallProgress;

        ExecutionPlan()
            : startTime(0)
            , estimatedCompletionTime(0)
            , overallProgress(0.0f) {
        }
    };

    // 簡化的 AIAgent 類
    class AIAgent {
    public:
        AIAgent(const AgentDesc& desc)
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
            , active(true)
            , completedTaskCount(0)
            , failedTaskCount(0)
            , knowledgeShareCount(0) {
        }

        virtual ~AIAgent() {}

        virtual bool Initialize() {
            std::cout << "初始化增強 AI 代理: " << name << std::endl;
            std::cout << "  - 類型: " << static_cast<int>(type) << std::endl;
            std::cout << "  - 專業領域: " << specializations.size() << " 個" << std::endl;
            std::cout << "  - 創造力: " << creativityLevel << std::endl;
            std::cout << "  - 工具使用: " << (canUseTools ? "是" : "否") << std::endl;
            std::cout << "  - 協作能力: " << (canCollaborate ? "是" : "否") << std::endl;
            return true;
        }

        virtual void Shutdown() {
            std::cout << "關閉增強 AI 代理: " << name << std::endl;
        }

        void AssignTask(const AgentTask& task) {
            tasks.push_back(task);
            std::cout << "分配任務給 " << name << ": " << task.description << std::endl;
        }

        virtual void ProcessCurrentTask() {
            if (tasks.empty()) return;

            AgentTask& task = tasks.back();
            if (task.status == TaskStatus::Pending) {
                task.status = TaskStatus::InProgress;
                std::cout << name << " 開始處理任務: " << task.description << std::endl;
            }

            if (task.status == TaskStatus::InProgress) {
                task.progress += 0.1f * performanceRating;
                if (task.progress >= 1.0f) {
                    task.progress = 1.0f;
                    task.status = TaskStatus::Completed;
                    task.completedTime = getCurrentTime();
                    task.result = "任務完成";
                    completedTaskCount++;
                    std::cout << name << " 完成任務: " << task.description << std::endl;
                }
            }
        }

        virtual void ProcessPerception(const PerceptionData& perception) {
            std::cout << name << " 處理感知數據: " << static_cast<int>(perception.type) << std::endl;
            currentPerceptions.push_back(perception);
        }

        virtual void StoreMemory(const MemoryItem& memory) {
            memory.timestamp = getCurrentTime();
            memories.push_back(memory);
            std::cout << name << " 存儲記憶: " << memory.content.substr(0, 30) << "..." << std::endl;
        }

        virtual std::vector<MemoryItem> RetrieveMemory(const std::string& query, MemoryType type) {
            std::vector<MemoryItem> results;
            for (const auto& memory : memories) {
                if (memory.type == type && memory.content.find(query) != std::string::npos) {
                    results.push_back(memory);
                }
            }
            return results;
        }

        virtual bool CanUseTool(const std::string& toolName) const {
            return availableTools.find(toolName) != availableTools.end();
        }

        virtual ToolExecutionResult UseTool(const std::string& toolName) {
            ToolExecutionResult result;
            result.toolName = toolName;
            result.success = true;
            result.output = "工具執行成功";
            result.executionTime = 100;
            std::cout << name << " 使用工具: " << toolName << std::endl;
            return result;
        }

        virtual void RegisterTool(const ToolDescription& tool) {
            availableTools[tool.name] = tool;
            std::cout << name << " 註冊工具: " << tool.name << std::endl;
        }

        virtual ExecutionPlan CreatePlan(const std::string& goal) {
            ExecutionPlan plan;
            plan.id = "plan_" + std::to_string(getCurrentTime());
            plan.goal = goal;
            plan.startTime = getCurrentTime();
            plan.nodes = {"分析", "執行", "驗證"};
            plan.estimatedCompletionTime = plan.startTime + 10000;
            std::cout << name << " 創建計畫: " << goal << std::endl;
            return plan;
        }

        virtual void ExecutePlan(ExecutionPlan& plan) {
            std::cout << name << " 執行計畫: " << plan.id << std::endl;
            plan.overallProgress = 1.0f;
        }

        virtual bool CanCollaborateWith(const AIAgent* other) const {
            if (!canCollaborate || !other) return false;
            for (const auto& spec : specializations) {
                for (const auto& otherSpec : other->specializations) {
                    if (spec == otherSpec) return true;
                }
            }
            return false;
        }

        virtual void ShareKnowledge(const std::string& knowledge, AIAgent* recipient) {
            if (!canCollaborate || !recipient) return;
            std::cout << name << " 向 " << recipient->GetName() << " 分享知識" << std::endl;
            knowledgeShareCount++;
        }

        // 狀單的實現方法
        std::string GetName() const { return name; }
        float GetPerformanceRating() const { return performanceRating; }
        size_t GetCompletedTaskCount() const { return completedTaskCount; }
        size_t GetMemoryCount() const { return memories.size(); }
        size_t GetToolCount() const { return availableTools.size(); }
        size_t GetKnowledgeShareCount() const { return knowledgeShareCount; }
        const std::vector<AgentTask>& GetTasks() const { return tasks; }
        bool IsActive() const { return active; }

    protected:
        std::string name;
        AgentType type;
        bool autonomous;
        float performanceRating;
        float learningRate;
        std::vector<std::string> capabilities;
        std::vector<std::string> specializations;
        float creativityLevel;
        float riskTolerance;
        bool canUseTools;
        bool canCollaborate;
        bool canLearnFromPeers;
        bool active;
        size_t completedTaskCount;
        size_t failedTaskCount;
        size_t knowledgeShareCount;

        std::vector<AgentTask> tasks;
        std::vector<PerceptionData> currentPerceptions;
        std::vector<MemoryItem> memories;
        std::unordered_map<std::string, ToolDescription> availableTools;

        uint64_t getCurrentTime() const {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        }
    };

    // 專門化代理類
    class MultimodalAgent : public AIAgent {
    public:
        MultimodalAgent(const AgentDesc& desc) : AIAgent(desc) {
            specializations.push_back("Image Processing");
            specializations.push_back("Audio Processing");
        }

        void ProcessPerception(const PerceptionData& perception) override {
            AIAgent::ProcessPerception(perception);
            std::cout << "多模態代理處理: " << static_cast<int>(perception.type) << std::endl;
        }
    };

    class PlannerAgent : public AIAgent {
    public:
        PlannerAgent(const AgentDesc& desc) : AIAgent(desc) {
            specializations.push_back("Task Planning");
            specializations.push_back("Resource Allocation");
        }

        ExecutionPlan CreatePlan(const std::string& goal) override {
            ExecutionPlan plan = AIAgent::CreatePlan(goal);
            plan.nodes = {"分析需求", "設計架構", "實現核心", "測試驗證", "部署發布"};
            return plan;
        }
    };

    class CommunicatorAgent : public AIAgent {
    public:
        CommunicatorAgent(const AgentDesc& desc) : AIAgent(desc) {
            specializations.push_back("Natural Language Processing");
            specializations.push_back("Dialog Management");
        }
    };

    class ToolUserAgent : public AIAgent {
    public:
        ToolUserAgent(const AgentDesc& desc) : AIAgent(desc) {
            specializations.push_back("Tool Integration");
            specializations.push_back("API Interaction");
        }
    };

} // namespace AI
} // namespace Potato

int main() {
    std::cout << "=== 增強版 AI Agent 系統示例 ===" << std::endl;

    using namespace Potato::AI;

    // 創建增強代理
    std::cout << "\n--- 創建增強代理 ---" << std::endl;

    AgentDesc multimodalDesc;
    multimodalDesc.name = "VisionMaster";
    multimodalDesc.type = AgentType::Multimodal;
    multimodalDesc.canUseTools = true;
    multimodalDesc.canCollaborate = true;
    multimodalDesc.creativityLevel = 0.8f;

    MultimodalAgent* multimodalAgent = new MultimodalAgent(multimodalDesc);
    multimodalAgent->Initialize();

    AgentDesc plannerDesc;
    plannerDesc.name = "StrategicPlanner";
    plannerDesc.type = AgentType::Planner;
    plannerDesc.canUseTools = true;
    plannerDesc.canCollaborate = true;
    plannerDesc.riskTolerance = 0.3f;

    PlannerAgent* plannerAgent = new PlannerAgent(plannerDesc);
    plannerAgent->Initialize();

    AgentDesc communicatorDesc;
    communicatorDesc.name = "DialogMaster";
    communicatorDesc.type = AgentType::Communicator;
    communicatorDesc.canCollaborate = true;
    communicatorDesc.creativityLevel = 0.9f;

    CommunicatorAgent* communicatorAgent = new CommunicatorAgent(communicatorDesc);
    communicatorAgent->Initialize();

    AgentDesc toolUserDesc;
    toolUserDesc.name = "ToolExpert";
    toolUserDesc.type = AgentType::ToolUser;
    toolUserDesc.canUseTools = true;
    toolUserDesc.specializations = {"API Development", "System Integration"};

    ToolUserAgent* toolUserAgent = new ToolUserAgent(toolUserDesc);
    toolUserAgent->Initialize();

    // 感知處理演示
    std::cout << "\n--- 感知處理演示 ---" << std::endl;

    PerceptionData textPerception;
    textPerception.type = PerceptionType::Text;
    textPerception.textData = "分析用戶請求";
    textPerception.source = "user_input";
    textPerception.confidence = 0.95f;

    multimodalAgent->ProcessPerception(textPerception);

    // 記憶管理演示
    std::cout << "\n--- 記憶管理演示 ---" << std::endl;

    MemoryItem memory1;
    memory1.type = MemoryType::Semantic;
    memory1.content = "Potato Engine 是一個獨立的遊戲引擎";
    memory1.tags = {"engine", "gamedev"};
    memory1.importance = 0.9f;

    multimodalAgent->StoreMemory(memory1);

    MemoryItem memory2;
    memory2.type = MemoryType::Episodic;
    memory2.content = "成功修復了數學標頭衝突問題";
    memory2.tags = {"bugfix", "math"};
    memory2.importance = 0.8f;

    plannerAgent->StoreMemory(memory2);

    // 檢索記憶
    std::vector<MemoryItem> memories = multimodalAgent->RetrieveMemory("engine", MemoryType::Semantic);
    std::cout << "檢索到 " << memories.size() << " 條相關記憶" << std::endl;

    // 工具使用演示
    std::cout << "\n--- 工具使用演示 ---" << std::endl;

    ToolDescription codeGenTool;
    codeGenTool.name = "CodeGenerator";
    codeGenTool.type = ToolType::CodeGeneration;
    codeGenTool.description = "生成代碼的工具";
    codeGenTool.requiredCapabilities = {"Code Generation"};

    toolUserAgent->RegisterTool(codeGenTool);

    ToolExecutionResult result = toolUserAgent->UseTool("CodeGenerator");
    std::cout << "工具執行結果: " << (result.success ? "成功" : "失敗") << std::endl;
    if (result.success) {
        std::cout << "輸出: " << result.output << std::endl;
    }

    // 計畫制定演示
    std::cout << "\n--- 計畫制定演示 ---" << std::endl;

    std::string goal = "開發完整的遊戲引擎";
    ExecutionPlan plan = plannerAgent->CreatePlan(goal);
    std::cout << "計畫包含 " << plan.nodes.size() << " 個節點" << std::endl;
    for (const auto& node : plan.nodes) {
        std::cout << "  - " << node << std::endl;
    }

    plannerAgent->ExecutePlan(plan);
    std::cout << "計畫執行進度: " << plan.overallProgress * 100 << "%" << std::endl;

    // 協作演示
    std::cout << "\n--- 協作演示 ---" << std::endl;

    std::cout << "檢查協作兼容性:" << std::endl;
    std::cout << "  - VisionMaster <-> StrategicPlanner: " 
              << (multimodalAgent->CanCollaborateWith(plannerAgent) ? "兼容" : "不兼容") << std::endl;
    std::cout << "  - VisionMaster <-> DialogMaster: " 
              << (multimodalAgent->CanCollaborateWith(communicatorAgent) ? "兼容" : "不兼容") << std::endl;

    multimodalAgent->ShareKnowledge("圖像處理最佳實踐", plannerAgent);
    communicatorAgent->ShareKnowledge("自然語言處理技巧", multimodalAgent);

    // 任務分配演示
    std::cout << "\n--- 任務分配演示 ---" << std::endl;

    AgentTask task1;
    task1.id = "task_001";
    task1.description = "實現渲染系統";
    task1.category = "Graphics";
    task1.priority = TaskPriority::High;
    task1.requiredSkills = {"OpenGL", "Graphics Programming"};

    AgentTask task2;
    task2.id = "task_002";
    task2.description = "設計用戶界面";
    task2.category = "UI Design";
    task2.priority = TaskPriority::Medium;
    task2.requiredSkills = {"UI Design", "User Experience"};

    multimodalAgent->AssignTask(task1);
    communicatorAgent->AssignTask(task2);

    // 處理任務
    std::cout << "\n--- 處理任務 ---" << std::endl;

    for (int i = 0; i < 10; i++) {
        multimodalAgent->ProcessCurrentTask();
        communicatorAgent->ProcessCurrentTask();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // 系統統計
    std::cout << "\n=== 系統統計 ===" << std::endl;
    std::cout << "VisionMaster:" << std::endl;
    std::cout << "  - 完成任務: " << multimodalAgent->GetCompletedTaskCount() << std::endl;
    std::cout << "  - 記憶數: " << multimodalAgent->GetMemoryCount() << std::endl;
    std::cout << "  - 工具數: " << multimodalAgent->GetToolCount() << std::endl;
    std::cout << "  - 知識分享: " << multimodalAgent->GetKnowledgeShareCount() << std::endl;

    std::cout << "StrategicPlanner:" << std::endl;
    std::cout << "  - 完成任務: " << plannerAgent->GetCompletedTaskCount() << std::endl;
    std::cout << "  - 記憶數: " << plannerAgent->GetMemoryCount() << std::endl;
    std::cout << "  - 知識分享: " << plannerAgent->GetKnowledgeShareCount() << std::endl;

    std::cout << "DialogMaster:" << std::endl;
    std::cout << "  - 完成任務: " << communicatorAgent->GetCompletedTaskCount() << std::endl;
    std::cout << "  - 知識分享: " << communicatorAgent->GetKnowledgeShareCount() << std::endl;

    std::cout << "ToolExpert:" << std::endl;
    std::cout << "  - 完成任務: " << toolUserAgent->GetCompletedTaskCount() << std::endl;
    std::cout << "  - 工具數: " << toolUserAgent->GetToolCount() << std::endl;

    // 清理
    std::cout << "\n--- 清理資源 ---" << std::endl;

    multimodalAgent->Shutdown();
    plannerAgent->Shutdown();
    communicatorAgent->Shutdown();
    toolUserAgent->Shutdown();

    delete multimodalAgent;
    delete plannerAgent;
    delete communicatorAgent;
    delete toolUserAgent;

    std::cout << "\n=== 增強版 AI Agent 系統示例完成 ===" << std::endl;

    return 0;
}
