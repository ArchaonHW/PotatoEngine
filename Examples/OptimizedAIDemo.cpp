/**
 * Optimized AI Agent Demo
 * Performance-optimized AI Agent functionality
 */

#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <random>
#include <chrono>
#include <thread>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <queue>

// Optimized AI Agent Types
enum class AgentType {
    Developer, Designer, Analyst, Multimodal, Planner, Communicator, ToolUser, Custom
};

// Task Priority
enum class TaskPriority {
    Low = 0, Medium = 1, High = 2, Critical = 3
};

// Task Status
enum class TaskStatus {
    Pending, InProgress, Completed, Failed, Cancelled
};

// Optimized Task Structure
struct OptimizedTask {
    std::string id;
    std::string description;
    std::string category;
    TaskPriority priority;
    TaskStatus status;
    float progress;
    uint64_t createdTime;
    uint64_t completedTime;
    
    OptimizedTask()
        : priority(TaskPriority::Medium)
        , status(TaskStatus::Pending)
        , progress(0.0f)
        , createdTime(0)
        , completedTime(0) {
    }
};

// Optimized Agent Description
struct OptimizedAgentDesc {
    std::string name;
    AgentType type;
    float performanceRating;
    float creativityLevel;
    float riskTolerance;
    bool canUseTools;
    bool canCollaborate;
    std::vector<std::string> capabilities;
    std::vector<std::string> specializations;
    
    OptimizedAgentDesc()
        : type(AgentType::Custom)
        , performanceRating(0.8f)
        , creativityLevel(0.5f)
        , riskTolerance(0.5f)
        , canUseTools(true)
        , canCollaborate(true) {
    }
};

// Memory Item (Optimized)
struct MemoryItem {
    std::string content;
    float importance;
    float accessibility;
    uint64_t timestamp;
    std::vector<std::string> tags;
    
    MemoryItem()
        : importance(0.5f)
        , accessibility(0.5f)
        , timestamp(0) {
    }
};

// Optimized AI Agent Class
class OptimizedAIAgent {
public:
    OptimizedAIAgent(const OptimizedAgentDesc& desc)
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
        , knowledgeShareCount(0)
        , lastAccessTime(0) {
        
        std::cout << "Creating Optimized AI Agent: " << name << " (Type: " << static_cast<int>(type) << ")" << std::endl;
        std::cout << "  - Performance Rating: " << performanceRating << std::endl;
        std::cout << "  - Creativity Level: " << creativityLevel << std::endl;
        std::cout << "  - Risk Tolerance: " << riskTolerance << std::endl;
        std::cout << "  - Tool Usage: " << (canUseTools ? "Yes" : "No") << std::endl;
        std::cout << "  - Collaboration: " << (canCollaborate ? "Yes" : "No") << std::endl;
        std::cout << "  - Specializations: " << specializations.size() << std::endl;
    }
    
    void AssignTask(const OptimizedTask& task) {
        taskQueue.push(task);
        std::cout << name << " queues task: " << task.description << std::endl;
    }
    
    void ProcessCurrentTask() {
        if (taskQueue.empty()) return;
        
        OptimizedTask& task = taskQueue.front();
        lastAccessTime = getCurrentTime();
        
        if (task.status == TaskStatus::Pending) {
            task.status = TaskStatus::InProgress;
            std::cout << name << " starts processing task: " << task.description << std::endl;
        }
        
        if (task.status == TaskStatus::InProgress) {
            // Progress based on performance rating
            task.progress += 0.1f * performanceRating;
            
            if (task.progress >= 1.0f) {
                task.progress = 1.0f;
                task.status = TaskStatus::Completed;
                task.completedTime = getCurrentTime();
                completedTaskCount++;
                std::cout << name << " completed task: " << task.description << std::endl;
                taskQueue.pop();
            }
        }
    }
    
    void StoreMemory(const MemoryItem& memory) {
        MemoryItem newMemory = memory;
        newMemory.timestamp = getCurrentTime();
        memories.push_back(newMemory);
        
        // Optimize memory storage - remove least important if too many
        if (memories.size() > 100) {
            auto it = std::min_element(memories.begin(), memories.end(),
                [](const MemoryItem& a, const MemoryItem& b) {
                    return (a.importance * a.accessibility) < (b.importance * b.accessibility);
                });
            memories.erase(it);
        }
        
        std::cout << name << " stored memory: " << memory.content.substr(0, 30) << "..." << std::endl;
    }
    
    std::vector<MemoryItem> RetrieveMemory(const std::string& query) {
        std::vector<MemoryItem> results;
        
        for (auto& memory : memories) {
            if (memory.content.find(query) != std::string::npos) {
                // Update accessibility on access (make a copy to modify)
                MemoryItem updatedMemory = memory;
                updatedMemory.accessibility = std::min(1.0f, memory.accessibility + 0.1f);
                results.push_back(updatedMemory);
            }
        }
        
        return results;
    }
    
    void OptimizeMemory() {
        // Update memory accessibility based on time decay
        uint64_t currentTime = getCurrentTime();
        
        for (auto& memory : memories) {
            uint64_t age = currentTime - memory.timestamp;
            float decay = std::exp(-static_cast<float>(age) / (1000.0f * 60.0f)); // 1 hour decay
            memory.accessibility = memory.accessibility * decay + 0.1f; // minimum accessibility
        }
    }
    
    bool CanCollaborateWith(const OptimizedAIAgent* other) const {
        if (!canCollaborate || !other) return false;
        
        // Optimized collaboration check - use hash set for faster lookup
        std::unordered_set<std::string> mySpecs(specializations.begin(), specializations.end());
        
        for (const auto& otherSpec : other->specializations) {
            if (mySpecs.find(otherSpec) != mySpecs.end()) {
                return true;
            }
        }
        
        return false;
    }
    
    void ShareKnowledge(const std::string& knowledge, OptimizedAIAgent* recipient) {
        if (!canCollaborate || !recipient) return;
        
        std::cout << name << " shares knowledge with " << recipient->GetName() << ": " << knowledge << std::endl;
        knowledgeShareCount++;
        
        // Store sharing as memory
        MemoryItem memory;
        memory.content = "Shared with " + recipient->GetName() + ": " + knowledge;
        memory.importance = 0.7f;
        memory.tags = {"collaboration", "knowledge_share"};
        StoreMemory(memory);
    }
    
    // Getters
    std::string GetName() const { return name; }
    float GetPerformanceRating() const { return performanceRating; }
    size_t GetCompletedTaskCount() const { return completedTaskCount; }
    size_t GetKnowledgeShareCount() const { return knowledgeShareCount; }
    size_t GetTaskQueueSize() const { return taskQueue.size(); }
    size_t GetMemoryCount() const { return memories.size(); }
    bool IsActive() const { return active; }
    const std::vector<std::string>& GetCapabilities() const { return capabilities; }
    
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
    uint64_t lastAccessTime;
    
    std::queue<OptimizedTask> taskQueue;
    std::vector<MemoryItem> memories;
    
    uint64_t getCurrentTime() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }
};

// Optimized Agent Manager
class OptimizedAgentManager {
public:
    OptimizedAgentManager() {
        std::cout << "Initializing Optimized AI Agent Manager" << std::endl;
    }
    
    ~OptimizedAgentManager() {
        for (auto* agent : agents) {
            delete agent;
        }
        agents.clear();
    }
    
    OptimizedAIAgent* CreateAgent(const OptimizedAgentDesc& desc) {
        OptimizedAIAgent* agent = new OptimizedAIAgent(desc);
        agents.push_back(agent);
        std::cout << "Manager created agent: " << desc.name << " (Total: " << agents.size() << ")" << std::endl;
        return agent;
    }
    
    void AssignTaskToBestAgent(const OptimizedTask& task) {
        OptimizedAIAgent* bestAgent = FindBestAgentForTask(task);
        if (bestAgent) {
            bestAgent->AssignTask(task);
            std::cout << "Assigned task to best agent: " << bestAgent->GetName() << std::endl;
        }
    }
    
    void Update(float deltaTime) {
        for (auto* agent : agents) {
            if (agent->IsActive()) {
                agent->ProcessCurrentTask();
            }
        }
        
        // Periodic memory optimization
        static int updateCounter = 0;
        if (++updateCounter % 10 == 0) {
            OptimizeAllMemories();
        }
        
        (void)deltaTime; // Suppress unused parameter warning
    }
    
    void OptimizeAllMemories() {
        for (auto* agent : agents) {
            agent->OptimizeMemory();
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
    
    size_t GetTotalMemoryCount() const {
        size_t count = 0;
        for (const auto* agent : agents) {
            count += agent->GetMemoryCount();
        }
        return count;
    }
    
    const std::vector<OptimizedAIAgent*>& GetAgents() const { return agents; }
    
private:
    OptimizedAIAgent* FindBestAgentForTask(const OptimizedTask& task) {
        OptimizedAIAgent* bestAgent = nullptr;
        float bestScore = 0.0f;
        
        for (auto* agent : agents) {
            if (!agent->IsActive()) continue;
            
            float score = agent->GetPerformanceRating();
            
            // Consider task queue size - prefer less busy agents
            score -= agent->GetTaskQueueSize() * 0.05f;
            
            // Check capability matching
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
    
    std::vector<OptimizedAIAgent*> agents;
};

int main() {
    std::cout << "=== Optimized AI Agent System Demo ===" << std::endl;
    
    // Create optimized agent manager
    OptimizedAgentManager manager;
    
    // Create optimized agents
    std::cout << "\n--- Creating Optimized Agents ---" << std::endl;
    
    OptimizedAgentDesc developerDesc;
    developerDesc.name = "CodeMaster";
    developerDesc.type = AgentType::Developer;
    developerDesc.performanceRating = 0.9f;
    developerDesc.capabilities = {"Code Generation", "Code Analysis", "Optimization"};
    developerDesc.specializations = {"C++", "Python", "Software Architecture"};
    developerDesc.canUseTools = true;
    developerDesc.canCollaborate = true;
    
    OptimizedAIAgent* developer = manager.CreateAgent(developerDesc);
    
    OptimizedAgentDesc designerDesc;
    designerDesc.name = "CreativeMind";
    designerDesc.type = AgentType::Designer;
    designerDesc.performanceRating = 0.85f;
    designerDesc.creativityLevel = 0.9f;
    designerDesc.capabilities = {"Design Generation", "Brainstorming", "Prototyping"};
    designerDesc.specializations = {"UI Design", "UX Design", "Game Design"};
    designerDesc.canCollaborate = true;
    
    OptimizedAIAgent* designer = manager.CreateAgent(designerDesc);
    
    OptimizedAgentDesc multimodalDesc;
    multimodalDesc.name = "VisionAI";
    multimodalDesc.type = AgentType::Multimodal;
    multimodalDesc.performanceRating = 0.8f;
    multimodalDesc.capabilities = {"Image Processing", "Audio Processing", "Video Processing"};
    multimodalDesc.specializations = {"Computer Vision", "Speech Recognition", "Video Analysis"};
    multimodalDesc.canUseTools = true;
    multimodalDesc.canCollaborate = true;
    
    OptimizedAIAgent* multimodal = manager.CreateAgent(multimodalDesc);
    
    // Assign tasks
    std::cout << "\n--- Assigning Tasks ---" << std::endl;
    
    OptimizedTask task1;
    task1.id = "task_001";
    task1.description = "Implement rendering system";
    task1.category = "Code Generation";
    task1.priority = TaskPriority::High;
    
    manager.AssignTaskToBestAgent(task1);
    
    OptimizedTask task2;
    task2.id = "task_002";
    task2.description = "Design user interface";
    task2.category = "Design Generation";
    task2.priority = TaskPriority::Medium;
    
    manager.AssignTaskToBestAgent(task2);
    
    OptimizedTask task3;
    task3.id = "task_003";
    task3.description = "Analyze image data";
    task3.category = "Image Processing";
    task3.priority = TaskPriority::High;
    
    manager.AssignTaskToBestAgent(task3);
    
    // Memory management demo
    std::cout << "\n--- Memory Management Demo ---" << std::endl;
    
    MemoryItem memory1;
    memory1.content = "C++ best practices for game development";
    memory1.importance = 0.9f;
    memory1.tags = {"C++", "gamedev", "best_practices"};
    
    developer->StoreMemory(memory1);
    
    MemoryItem memory2;
    memory2.content = "UI design principles for mobile games";
    memory2.importance = 0.8f;
    memory2.tags = {"UI", "mobile", "design"};
    
    designer->StoreMemory(memory2);
    
    // Retrieve memory
    std::vector<MemoryItem> memories = developer->RetrieveMemory("C++");
    std::cout << "Retrieved " << memories.size() << " memories about C++" << std::endl;
    
    // Process tasks
    std::cout << "\n--- Processing Tasks ---" << std::endl;
    
    for (int i = 0; i < 15; i++) {
        manager.Update(0.1f);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Collaboration demo
    std::cout << "\n--- Collaboration Demo ---" << std::endl;
    
    std::cout << "Checking collaboration compatibility:" << std::endl;
    std::cout << "  - CodeMaster <-> CreativeMind: " 
              << (developer->CanCollaborateWith(designer) ? "Compatible" : "Not Compatible") << std::endl;
    std::cout << "  - CodeMaster <-> VisionAI: " 
              << (developer->CanCollaborateWith(multimodal) ? "Compatible" : "Not Compatible") << std::endl;
    
    // Knowledge sharing
    developer->ShareKnowledge("Optimization techniques", multimodal);
    designer->ShareKnowledge("UX patterns", developer);
    
    // System statistics
    std::cout << "\n=== Optimized System Statistics ===" << std::endl;
    std::cout << "Total Agents: " << manager.GetAgentCount() << std::endl;
    std::cout << "Active Agents: " << manager.GetActiveAgentCount() << std::endl;
    std::cout << "Total Completed Tasks: " << manager.GetTotalCompletedTasks() << std::endl;
    std::cout << "Total Memory Items: " << manager.GetTotalMemoryCount() << std::endl;
    
    std::cout << "\n=== Agent Detailed Statistics ===" << std::endl;
    for (const auto* agent : manager.GetAgents()) {
        std::cout << agent->GetName() << ":" << std::endl;
        std::cout << "  - Completed Tasks: " << agent->GetCompletedTaskCount() << std::endl;
        std::cout << "  - Knowledge Shares: " << agent->GetKnowledgeShareCount() << std::endl;
        std::cout << "  - Memory Items: " << agent->GetMemoryCount() << std::endl;
        std::cout << "  - Task Queue Size: " << agent->GetTaskQueueSize() << std::endl;
        std::cout << "  - Performance Rating: " << agent->GetPerformanceRating() << std::endl;
    }
    
    std::cout << "\n=== Optimized AI Agent System Demo Complete ===" << std::endl;
    
    return 0;
}
