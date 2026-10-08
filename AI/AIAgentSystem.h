#pragma once

#include <functional>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <cstdint>
#include <queue>
#include <variant>

namespace Potato {
namespace AI {

/**
 * AI 代理類型 - 增強版
 */
enum class AgentType {
    Developer,      // 開發代理 - 代碼生成和優化
    Designer,       // 設計代理 - 創意和設計任務
    Analyst,        // 分析代理 - 數據分析和優化
    Tester,         // 測試代理 - 自動化測試
    Debugger,       // 調試代理 - 問題診斷和修復
    Researcher,     // 研究代理 - 技術研究和創新
    Multimodal,     // 多模態代理 - 圖像、音頻、視頻處理
    Planner,        // 計畫代理 - 複雜任務規劃
    Communicator,   // 溝通代理 - 自然語言處理和對話
    ToolUser,       // 工具代理 - 外部工具和API調用
    Custom         // 自定義代理
};

/**
 * 任務優先級
 */
enum class TaskPriority {
    Low = 0,
    Medium = 1,
    High = 2,
    Critical = 3
};

/**
 * 任務狀態
 */
enum class TaskStatus {
    Pending,
    InProgress,
    Completed,
    Failed,
    Cancelled,
    Blocked,
    Retry
};

/**
 * 感知類型
 */
enum class PerceptionType {
    Text,
    Image,
    Audio,
    Video,
    StructuredData,
    UnstructuredData
};

/**
 * 記憶類型
 */
enum class MemoryType {
    ShortTerm,      // 短期記憶 - 當前工作上下文
    LongTerm,       // 長期記憶 - 持久化知識
    Episodic,       // 情節記憶 - 具體事件和經驗
    Semantic,       // 語義記憶 - 抽象概念和事實
    Procedural      // 程序記憶 - 技能和操作
};

/**
 * 工具類型
 */
enum class ToolType {
    CodeGeneration,
    CodeAnalysis,
    FileOperation,
    WebSearch,
    DatabaseQuery,
    APIRequest,
    DataProcessing,
    Custom
};

/**
 * 任務描述 - 增強版
 */
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
    
    // 增強字段
    std::vector<std::string> dependencies;       // 任務依賴
    std::vector<std::string> requiredSkills;     // 所需技能
    std::unordered_map<std::string, std::string> context;  // 上下文信息
    int retryCount;                              // 重試次數
    int maxRetries;                              // 最大重試次數
    
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

/**
 * 代理描述 - 增強版
 */
struct AgentDesc {
    std::string name;
    AgentType type;
    bool autonomous;
    float performanceRating;
    float learningRate;
    std::vector<std::string> capabilities;
    
    // 增強字段
    std::vector<std::string> specializations;   // 專業領域
    float creativityLevel;                      // 創造力水平
    float riskTolerance;                        // 風險承受度
    bool canUseTools;                           // 工具使用能力
    bool canCollaborate;                        // 協作能力
    bool canLearnFromPeers;                     // 向同伴學習
    int maxConcurrentTasks;                     // 最大並發任務數
    
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

/**
 * 決策結果 - 增強版
 */
struct Decision {
    std::string action;
    float confidence;
    std::vector<std::string> reasoning;
    std::string timestamp;
    
    // 增強字段
    std::vector<std::string> alternatives;      // 替代方案
    std::unordered_map<std::string, float> riskFactors;  // 風險因素
    std::string reasoningChain;                 // 推理鏈
    bool requiresApproval;                      // 是否需要批准
    std::vector<std::string> prerequisites;    // 前置條件
    
    Decision()
        : confidence(0.0f)
        , requiresApproval(false) {
    }
};

/**
 * 感知數據
 */
struct PerceptionData {
    PerceptionType type;
    std::variant<std::string, std::vector<uint8_t>> data;
    std::string source;
    uint64_t timestamp;
    float confidence;
    
    PerceptionData()
        : type(PerceptionType::Text)
        , timestamp(0)
        , confidence(1.0f) {
    }
};

/**
 * 記憶項
 */
struct MemoryItem {
    MemoryType type;
    std::string content;
    std::vector<std::string> tags;
    uint64_t timestamp;
    float importance;
    float accessibility;  // 訪問頻率
    
    MemoryItem()
        : type(MemoryType::ShortTerm)
        , timestamp(0)
        , importance(0.5f)
        , accessibility(0.5f) {
    }
};

/**
 * 工具描述
 */
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

/**
 * 工具執行結果
 */
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

/**
 * 計畫節點
 */
struct PlanNode {
    std::string id;
    std::string description;
    std::vector<std::string> dependencies;
    std::string assignedAgent;
    TaskStatus status;
    float estimatedDuration;
    float actualDuration;
    
    PlanNode()
        : status(TaskStatus::Pending)
        , estimatedDuration(0.0f)
        , actualDuration(0.0f) {
    }
};

/**
 * 執行計畫
 */
struct ExecutionPlan {
    std::string id;
    std::string description;
    std::string goal;
    std::vector<PlanNode> nodes;
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

/**
 * AI 代理基類 - 增強版
 */
class AIAgent {
public:
    AIAgent(const AgentDesc& desc);
    virtual ~AIAgent();
    
    // 生命周期
    virtual bool Initialize();
    virtual void Shutdown();
    
    // 任務管理
    void AssignTask(const AgentTask& task);
    void CompleteTask(const std::string& result);
    void FailTask(const std::string& error);
    void CancelTask(const std::string& taskId);
    
    // 執行
    virtual void Update(float deltaTime);
    virtual void ProcessCurrentTask();
    
    // 決策
    virtual Decision MakeDecision(const std::string& context, const std::vector<std::string>& options);
    
    // 感知處理 - 新增
    virtual void ProcessPerception(const PerceptionData& perception);
    virtual std::vector<PerceptionData> GetCurrentPerceptions() const;
    
    // 記憶管理 - 新增
    virtual void StoreMemory(const MemoryItem& memory);
    virtual std::vector<MemoryItem> RetrieveMemory(const std::string& query, MemoryType type);
    virtual void ConsolidateMemories();
    
    // 工具使用 - 新增
    virtual bool CanUseTool(const std::string& toolName) const;
    virtual ToolExecutionResult UseTool(const std::string& toolName, 
                                       const std::unordered_map<std::string, std::string>& parameters);
    virtual void RegisterTool(const ToolDescription& tool);
    
    // 計畫制定 - 新增
    virtual ExecutionPlan CreatePlan(const std::string& goal, 
                                    const std::vector<std::string>& constraints);
    virtual void ExecutePlan(ExecutionPlan& plan);
    
    // 協作 - 新增
    virtual bool CanCollaborateWith(const AIAgent* other) const;
    virtual void ShareKnowledge(const std::string& knowledge, AIAgent* recipient);
    virtual void ReceiveKnowledge(const std::string& knowledge, AIAgent* sender);
    
    // 狀態查詢
    bool IsActive() const { return active; }
    void SetActive(bool isActive) { active = isActive; }
    
    bool IsAutonomous() const { return autonomous; }
    bool CanCollaborate() const { return canCollaborate; }
    bool CanLearnFromPeers() const { return canLearnFromPeers; }
    const std::vector<std::string>& GetCapabilities() const { return capabilities; }
    
    // 任務查詢
    const std::vector<AgentTask>& GetTasks() const { return tasks; }
    const AgentTask* GetCurrentTask() const;
    
    // 信息查詢
    const std::string& GetName() const { return name; }
    AgentType GetType() const { return type; }
    float GetPerformanceRating() const { return performanceRating; }
    
    // 學習
    void RecordLearning(const std::string& context, const std::string& action, bool success);
    float GetLearningRate() const { return learningRate; }
    
    // 統計
    size_t GetCompletedTaskCount() const { return completedTaskCount; }
    size_t GetFailedTaskCount() const { return failedTaskCount; }
    float GetSuccessRate() const;
    
    // 新增統計
    size_t GetMemoryCount() const { return memories.size(); }
    size_t GetToolCount() const { return availableTools.size(); }
    size_t GetKnowledgeShareCount() const { return knowledgeShareCount; }
    
protected:
    std::string name;
    AgentType type;
    bool autonomous;
    float performanceRating;
    float learningRate;
    std::vector<std::string> capabilities;
    
    // 增強字段
    std::vector<std::string> specializations;
    float creativityLevel;
    float riskTolerance;
    bool canUseTools;
    bool canCollaborate;
    bool canLearnFromPeers;
    int maxConcurrentTasks;
    
    std::vector<AgentTask> tasks;
    int currentTaskIndex;
    
    bool active;
    size_t completedTaskCount;
    size_t failedTaskCount;
    size_t knowledgeShareCount;
    
    // 感知系統
    std::queue<PerceptionData> perceptionQueue;
    std::vector<PerceptionData> currentPerceptions;
    
    // 記憶系統
    std::vector<MemoryItem> memories;
    std::unordered_map<std::string, std::vector<MemoryItem>> memoryIndex;
    
    // 工具系統
    std::unordered_map<std::string, ToolDescription> availableTools;
    
    // 學習數據
    struct LearningData {
        std::string context;
        std::string action;
        bool success;
        uint64_t timestamp;
    };
    std::vector<LearningData> learningHistory;
    
    // 協作數據
    struct SharedKnowledge {
        std::string knowledge;
        std::string sourceAgent;
        uint64_t timestamp;
        float usefulness;
    };
    std::vector<SharedKnowledge> sharedKnowledge;
    
    mutable std::mutex taskMutex;
    mutable std::mutex learningMutex;
    mutable std::mutex perceptionMutex;
    mutable std::mutex memoryMutex;
    mutable std::mutex toolMutex;
    
    // 內部方法
    void ProcessPerceptionQueue();
    void UpdateMemoryAccessibility();
    float CalculateKnowledgeUsefulness(const std::string& knowledge);
    // taskMutex 已持有時的任務完成路徑——ProcessCurrentTask 系列在鎖內
    // 必須走這裡，不能呼叫會再上鎖的 CompleteTask（std::mutex 非遞歸）。
    void CompleteTaskUnlocked(const std::string& result);
};

/**
 * 多模態代理 - 處理圖像、音頻、視頻等
 */
class MultimodalAgent : public AIAgent {
public:
    MultimodalAgent(const AgentDesc& desc);
    
    Decision MakeDecision(const std::string& context, const std::vector<std::string>& options) override;
    void ProcessCurrentTask() override;
    void ProcessPerception(const PerceptionData& perception) override;
    
private:
    std::string AnalyzeImage(const std::vector<uint8_t>& imageData);
    std::string AnalyzeAudio(const std::vector<uint8_t>& audioData);
    std::string AnalyzeVideo(const std::vector<uint8_t>& videoData);
    std::string GenerateMultimodalResponse(const std::vector<PerceptionData>& perceptions);
};

/**
 * 計畫代理 - 複雜任務規劃
 */
class PlannerAgent : public AIAgent {
public:
    PlannerAgent(const AgentDesc& desc);
    
    Decision MakeDecision(const std::string& context, const std::vector<std::string>& options) override;
    void ProcessCurrentTask() override;
    ExecutionPlan CreatePlan(const std::string& goal, const std::vector<std::string>& constraints) override;
    void ExecutePlan(ExecutionPlan& plan) override;
    
private:
    std::vector<PlanNode> DecomposeTask(const std::string& task);
    std::vector<std::string> IdentifyDependencies(const std::vector<PlanNode>& nodes);
    std::vector<PlanNode> OptimizePlan(const std::vector<PlanNode>& nodes);
    float EstimatePlanDuration(const ExecutionPlan& plan);
};

/**
 * 溝通代理 - 自然語言處理和對話
 */
class CommunicatorAgent : public AIAgent {
public:
    CommunicatorAgent(const AgentDesc& desc);
    
    Decision MakeDecision(const std::string& context, const std::vector<std::string>& options) override;
    void ProcessCurrentTask() override;
    
private:
    std::string ProcessNaturalLanguage(const std::string& text);
    std::string GenerateResponse(const std::string& input, const std::string& context);
    std::vector<std::string> ExtractIntent(const std::string& text);
    bool UnderstandSentiment(const std::string& text);
};

/**
 * 工具代理 - 外部工具和API調用
 */
class ToolUserAgent : public AIAgent {
public:
    ToolUserAgent(const AgentDesc& desc);
    
    Decision MakeDecision(const std::string& context, const std::vector<std::string>& options) override;
    void ProcessCurrentTask() override;
    ToolExecutionResult UseTool(const std::string& toolName, 
                               const std::unordered_map<std::string, std::string>& parameters) override;
    
private:
    std::vector<std::string> DiscoverAvailableTools();
    bool ValidateToolParameters(const ToolDescription& tool, 
                               const std::unordered_map<std::string, std::string>& parameters);
    std::string FormatToolOutput(const ToolExecutionResult& result);
};

/**
 * 開發代理 - 代碼生成與優化
 */
class DeveloperAgent : public AIAgent {
public:
    DeveloperAgent(const AgentDesc& desc);
    
    Decision MakeDecision(const std::string& context, const std::vector<std::string>& options) override;
    void ProcessCurrentTask() override;
    
private:
    std::string GenerateCode(const std::string& description);
    bool OptimizeCode(const std::string& code);
};

/**
 * 設計代理 - 設計生成與創意發想
 */
class DesignerAgent : public AIAgent {
public:
    DesignerAgent(const AgentDesc& desc);
    
    Decision MakeDecision(const std::string& context, const std::vector<std::string>& options) override;
    void ProcessCurrentTask() override;
    
private:
    std::string GenerateDesign(const std::string& description);
    std::vector<std::string> BrainstormIdeas(const std::string& topic);
};

/**
 * 分析代理 - 數據分析與洞察生成
 */
class AnalystAgent : public AIAgent {
public:
    AnalystAgent(const AgentDesc& desc);
    
    Decision MakeDecision(const std::string& context, const std::vector<std::string>& options) override;
    void ProcessCurrentTask() override;
    
private:
    std::string AnalyzeData(const std::string& data);
    std::vector<std::string> GenerateInsights(const std::string& analysis);
};

/**
 * AI 代理管理器 - 增強版
 */
class AIAgentManager {
public:
    AIAgentManager();
    ~AIAgentManager();
    
    // 生命周期
    bool Initialize();
    void Shutdown();
    
    // 代理管理
    AIAgent* CreateAgent(const AgentDesc& desc);
    void DestroyAgent(const std::string& agentId);
    AIAgent* GetAgent(const std::string& agentId);
    const std::vector<AIAgent*>& GetAgents() const { return agents; }
    
    // 任務分配
    void AssignTaskToAgent(const std::string& agentId, const AgentTask& task);
    void AssignTaskToBestAgent(const AgentTask& task);
    void AssignTaskToAllAgents(const AgentTask& task);
    void AssignComplexTask(const ExecutionPlan& plan);  // 新增
    
    // 集體決策
    Decision MakeGroupDecision(const std::string& context, const std::vector<std::string>& options);
    std::vector<Decision> GetIndividualDecisions(const std::string& context, 
                                                 const std::vector<std::string>& options);  // 新增
    
    // 協作管理 - 新增
    void EnableCollaboration(bool enable);
    void InitiateCollaboration(const std::string& topic, const std::vector<std::string>& agentIds);
    void BroadcastKnowledge(const std::string& knowledge, const std::string& sourceAgentId);
    std::vector<std::string> GetCollaborativeInsights(const std::string& topic);
    
    // 資源管理 - 新增
    void RegisterSharedTool(const ToolDescription& tool);
    void UnregisterSharedTool(const std::string& toolName);
    std::vector<ToolDescription> GetAvailableTools() const;
    
    // 更新
    void Update(float deltaTime);
    
    // 學習系統
    void EnableLearning(bool enable);
    bool IsLearningEnabled() const;
    void RecordGroupLearning(const std::string& context, const std::string& action, bool success);
    void EnablePeerLearning(bool enable);  // 新增
    
    // 統計
    size_t GetAgentCount() const { return agents.size(); }
    size_t GetActiveAgentCount() const;
    size_t GetPendingTaskCount() const;
    size_t GetCompletedTaskCount() const;
    
    // 新增統計
    size_t GetTotalMemoryCount() const;
    size_t GetTotalToolCount() const;
    size_t GetKnowledgeShareCount() const;
    float GetAveragePerformanceRating() const;
    
    // 配置
    void SetMaxAgents(size_t maxAgents);
    void SetUpdateInterval(float interval);
    void SetCollaborationMode(bool enable);  // 新增
    
private:
    std::vector<AIAgent*> agents;
    std::unordered_map<std::string, AIAgent*> agentMap;
    
    bool learningEnabled;
    bool peerLearningEnabled;  // 新增
    bool collaborationEnabled;  // 新增
    float updateInterval;
    float updateTimer;
    
    size_t maxAgents;
    
    // 共享資源 - 新增
    std::unordered_map<std::string, ToolDescription> sharedTools;
    std::vector<std::string> collaborativeTopics;
    
    mutable std::mutex agentsMutex;
    mutable std::mutex tasksMutex;
    mutable std::mutex toolsMutex;  // 新增
    
    AIAgent* FindBestAgentForTask(const AgentTask& task);
    void UpdateAgentPerformance();
    void FacilitateCollaboration();  // 新增
    void AggregateKnowledge();  // 新增
};

/**
 * AI 代理事件 - 增強版
 */
struct AgentEvent {
    enum class Type {
        Created,
        TaskAssigned,
        TaskCompleted,
        TaskFailed,
        DecisionMade,
        LearningOccurred,
        PerceptionProcessed,  // 新增
        MemoryStored,         // 新增
        ToolUsed,             // 新增
        KnowledgeShared,      // 新增
        CollaborationStarted, // 新增
        PlanCreated,          // 新增
        PlanExecuted          // 新增
    };
    
    Type type;
    std::string agentId;
    std::string data;
    uint64_t timestamp;
    float confidence;  // 新增
    
    AgentEvent()
        : timestamp(0)
        , confidence(1.0f) {
    }
};

/**
 * AI 代理系統 - 增強版
 * 整合所有 AI 代理功能的主系統
 */
class AIAgentSystem {
public:
    AIAgentSystem();
    ~AIAgentSystem();
    
    // 初始化
    bool Initialize();
    void Shutdown();
    
    // 主系統
    void Update(float deltaTime);
    
    // 代理管理
    AIAgentManager* GetAgentManager() { return agentManager.get(); }
    
    // 事件系統
    void SetEventBus(void* eventBus);  // 改為void*避免依賴
    
    // 配置
    void SetMaxAgents(size_t maxAgents);
    void SetLearningEnabled(bool enabled);
    void SetCollaborationEnabled(bool enabled);  // 新增
    
    // 高級功能 - 新增
    void EnableMultimodalProcessing(bool enable);
    void EnableAdvancedPlanning(bool enable);
    void EnableToolIntegration(bool enable);
    void SetGlobalContext(const std::unordered_map<std::string, std::string>& context);
    
    // 統計
    void PrintStatistics();
    std::string GetSystemReport();  // 新增
    
    // 調試 - 新增
    void EnableDebugMode(bool enable);
    void LogAgentStates();
    void LogTaskProgress();
    
private:
    std::unique_ptr<AIAgentManager> agentManager;
    void* eventBus;
    
    bool initialized;
    
    // 增強功能標誌
    bool multimodalEnabled;
    bool advancedPlanningEnabled;
    bool toolIntegrationEnabled;
    bool debugMode;
    
    // 全局上下文
    std::unordered_map<std::string, std::string> globalContext;
    
    // 性能監控
    struct PerformanceMetrics {
        float averageDecisionTime;
        float averageTaskCompletionTime;
        size_t totalDecisions;
        size_t totalTasksCompleted;
        float collaborationSuccessRate;
    };
    PerformanceMetrics metrics;
};

/**
 * AI 代理工廠 - 新增
 * 用於創建不同類型的代理
 */
class AIAgentFactory {
public:
    static AIAgent* CreateAgent(AgentType type, const AgentDesc& desc);
    static std::vector<AgentType> GetAvailableAgentTypes();
    static AgentDesc GetDefaultDescription(AgentType type);
};

/**
 * AI 代理協調器 - 新增
 * 管理代理之間的協作和資源分配
 */
class AIAgentCoordinator {
public:
    AIAgentCoordinator();
    ~AIAgentCoordinator();
    
    void RegisterAgent(AIAgent* agent);
    void UnregisterAgent(const std::string& agentId);
    
    void CoordinateTask(const AgentTask& task);
    void CoordinateCollaboration(const std::string& topic, const std::vector<std::string>& agentIds);
    
    std::vector<std::string> SuggestAgentTeam(const std::string& taskType);
    void OptimizeResourceAllocation();
    
private:
    std::unordered_map<std::string, AIAgent*> registeredAgents;
    mutable std::mutex coordinatorMutex;
};

} // namespace AI
} // namespace Potato