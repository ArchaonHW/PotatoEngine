/**
 * AI Agent Demo
 * Demonstrates enhanced AI Agent functionality
 */

#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <random>
#include <chrono>
#include <thread>

// Simple AI Agent Types
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

// Simple Task Structure
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

// Simple Agent Description
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

// Simple AI Agent Class
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
        
        std::cout << "Creating AI Agent: " << name << " (Type: " << static_cast<int>(type) << ")" << std::endl;
        std::cout << "  - Performance Rating: " << performanceRating << std::endl;
        std::cout << "  - Creativity Level: " << creativityLevel << std::endl;
        std::cout << "  - Risk Tolerance: " << riskTolerance << std::endl;
        std::cout << "  - Tool Usage: " << (canUseTools ? "Yes" : "No") << std::endl;
        std::cout << "  - Collaboration: " << (canCollaborate ? "Yes" : "No") << std::endl;
        std::cout << "  - Specializations: " << specializations.size() << std::endl;
    }
    
    void AssignTask(const SimpleTask& task) {
        tasks.push_back(task);
        std::cout << name << " accepts task: " << task.description << std::endl;
    }
    
    void ProcessCurrentTask() {
        if (tasks.empty()) return;
        
        SimpleTask& task = tasks.back();
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
                completedTaskCount++;
                std::cout << name << " completed task: " << task.description << std::endl;
            }
        }
    }
    
    bool CanCollaborateWith(const SimpleAIAgent* other) const {
        if (!canCollaborate || !other) return false;
        
        // Check if specializations are compatible
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
        
        std::cout << name << " shares knowledge with " << recipient->GetName() << ": " << knowledge << std::endl;
        knowledgeShareCount++;
    }
    
    // Getters
    std::string GetName() const { return name; }
    float GetPerformanceRating() const { return performanceRating; }
    size_t GetCompletedTaskCount() const { return completedTaskCount; }
    size_t GetKnowledgeShareCount() const { return knowledgeShareCount; }
    const std::vector<SimpleTask>& GetTasks() const { return tasks; }
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
    std::vector<SimpleTask> tasks;
};

// Simple Agent Manager
class SimpleAgentManager {
public:
    SimpleAgentManager() {
        std::cout << "Initializing AI Agent Manager" << std::endl;
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
        std::cout << "Manager created agent: " << desc.name << " (Total: " << agents.size() << ")" << std::endl;
        return agent;
    }
    
    void AssignTaskToBestAgent(const SimpleTask& task) {
        SimpleAIAgent* bestAgent = FindBestAgentForTask(task);
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
        (void)deltaTime; // Suppress unused parameter warning
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
    
    std::vector<SimpleAIAgent*> agents;
};

int main() {
    std::cout << "=== Enhanced AI Agent System Demo ===" << std::endl;
    
    // Create agent manager
    SimpleAgentManager manager;
    
    // Create enhanced agents
    std::cout << "\n--- Creating Enhanced Agents ---" << std::endl;
    
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
    
    // Assign tasks
    std::cout << "\n--- Assigning Tasks ---" << std::endl;
    
    SimpleTask task1;
    task1.id = "task_001";
    task1.description = "Implement rendering system";
    task1.category = "Code Generation";
    task1.priority = TaskPriority::High;
    
    manager.AssignTaskToBestAgent(task1);
    
    SimpleTask task2;
    task2.id = "task_002";
    task2.description = "Design user interface";
    task2.category = "Design Generation";
    task2.priority = TaskPriority::Medium;
    
    manager.AssignTaskToBestAgent(task2);
    
    SimpleTask task3;
    task3.id = "task_003";
    task3.description = "Analyze image data";
    task3.category = "Image Processing";
    task3.priority = TaskPriority::High;
    
    manager.AssignTaskToBestAgent(task3);
    
    SimpleTask task4;
    task4.id = "task_004";
    task4.description = "Create development plan";
    task4.category = "Task Planning";
    task4.priority = TaskPriority::Critical;
    
    manager.AssignTaskToBestAgent(task4);
    
    // Collaboration demo
    std::cout << "\n--- Collaboration Demo ---" << std::endl;
    
    std::cout << "Checking collaboration compatibility:" << std::endl;
    std::cout << "  - CodeMaster <-> CreativeMind: " 
              << (developer->CanCollaborateWith(designer) ? "Compatible" : "Not Compatible") << std::endl;
    std::cout << "  - CodeMaster <-> VisionAI: " 
              << (developer->CanCollaborateWith(multimodal) ? "Compatible" : "Not Compatible") << std::endl;
    std::cout << "  - CreativeMind <-> StrategicPlanner: " 
              << (designer->CanCollaborateWith(planner) ? "Compatible" : "Not Compatible") << std::endl;
    
    // Knowledge sharing
    std::cout << "\n--- Knowledge Sharing ---" << std::endl;
    
    developer->ShareKnowledge("C++ best practices", multimodal);
    designer->ShareKnowledge("UX design principles", planner);
    multimodal->ShareKnowledge("Image processing algorithms", developer);
    
    // Process tasks
    std::cout << "\n--- Processing Tasks ---" << std::endl;
    
    for (int i = 0; i < 15; i++) {
        manager.Update(0.1f);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // System statistics
    std::cout << "\n=== System Statistics ===" << std::endl;
    std::cout << "Total Agents: " << manager.GetAgentCount() << std::endl;
    std::cout << "Active Agents: " << manager.GetActiveAgentCount() << std::endl;
    std::cout << "Total Completed Tasks: " << manager.GetTotalCompletedTasks() << std::endl;
    
    std::cout << "\n=== Agent Detailed Statistics ===" << std::endl;
    for (const auto* agent : manager.GetAgents()) {
        std::cout << agent->GetName() << ":" << std::endl;
        std::cout << "  - Completed Tasks: " << agent->GetCompletedTaskCount() << std::endl;
        std::cout << "  - Knowledge Shares: " << agent->GetKnowledgeShareCount() << std::endl;
        std::cout << "  - Performance Rating: " << agent->GetPerformanceRating() << std::endl;
    }
    
    std::cout << "\n=== Enhanced AI Agent System Demo Complete ===" << std::endl;
    
    return 0;
}
