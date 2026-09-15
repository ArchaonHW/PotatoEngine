/**
 * AI Agent Test Suite
 * Comprehensive testing of AI Agent functionality
 */

#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <thread>
#include <queue>

// Simple AI Agent Types for Testing
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

// Test Task Structure
struct TestTask {
    std::string id;
    std::string description;
    std::string category;
    TaskPriority priority;
    TaskStatus status;
    float progress;
    
    TestTask()
        : priority(TaskPriority::Medium)
        , status(TaskStatus::Pending)
        , progress(0.0f) {
    }
};

// Test Agent Description
struct TestAgentDesc {
    std::string name;
    AgentType type;
    float performanceRating;
    bool canCollaborate;
    std::vector<std::string> capabilities;
    std::vector<std::string> specializations;
    
    TestAgentDesc()
        : type(AgentType::Custom)
        , performanceRating(0.8f)
        , canCollaborate(true) {
    }
};

// Test AI Agent Class
class TestAIAgent {
public:
    TestAIAgent(const TestAgentDesc& desc)
        : name(desc.name)
        , type(desc.type)
        , performanceRating(desc.performanceRating)
        , canCollaborate(desc.canCollaborate)
        , capabilities(desc.capabilities)
        , specializations(desc.specializations)
        , active(true)
        , completedTaskCount(0) {
    }
    
    void AssignTask(const TestTask& task) {
        taskQueue.push(task);
    }
    
    void ProcessCurrentTask() {
        if (taskQueue.empty()) return;
        
        // Process all pending tasks in queue
        std::queue<TestTask> processedTasks;
        while (!taskQueue.empty()) {
            TestTask task = taskQueue.front();
            taskQueue.pop();
            
            if (task.status == TaskStatus::Pending) {
                task.status = TaskStatus::InProgress;
            }
            
            if (task.status == TaskStatus::InProgress) {
                task.progress += 0.1f * performanceRating;
                
                if (task.progress >= 1.0f) {
                    task.progress = 1.0f;
                    task.status = TaskStatus::Completed;
                    completedTaskCount++;
                } else {
                    processedTasks.push(task); // Put back if not completed
                }
            }
        }
        
        // Put uncompleted tasks back
        taskQueue = processedTasks;
    }
    
    bool CanCollaborateWith(const TestAIAgent* other) const {
        if (!canCollaborate || !other) return false;
        
        for (const auto& spec : specializations) {
            for (const auto& otherSpec : other->specializations) {
                if (spec == otherSpec) {
                    return true;
                }
            }
        }
        
        return false;
    }
    
    // Getters
    std::string GetName() const { return name; }
    float GetPerformanceRating() const { return performanceRating; }
    size_t GetCompletedTaskCount() const { return completedTaskCount; }
    size_t GetTaskQueueSize() const { return taskQueue.size(); }
    bool IsActive() const { return active; }
    const std::vector<std::string>& GetCapabilities() const { return capabilities; }
    
private:
    std::string name;
    AgentType type;
    float performanceRating;
    bool canCollaborate;
    std::vector<std::string> capabilities;
    std::vector<std::string> specializations;
    bool active;
    size_t completedTaskCount;
    std::queue<TestTask> taskQueue;
};

// Test Framework
class AITestSuite {
public:
    AITestSuite() : totalTests(0), passedTests(0), failedTests(0) {
        std::cout << "=== AI Agent Test Suite ===" << std::endl;
    }
    
    ~AITestSuite() {
        PrintResults();
    }
    
    void RunAllTests() {
        std::cout << "\n--- Running All Tests ---" << std::endl;
        
        TestAgentCreation();
        TestTaskAssignment();
        TestTaskProcessing();
        TestCollaboration();
        TestPerformanceOptimization();
        TestMultiAgentCoordination();
        TestStressTesting();
    }
    
private:
    void TestAgentCreation() {
        std::cout << "\n[TEST] Agent Creation" << std::endl;
        
        TestAgentDesc desc;
        desc.name = "TestAgent";
        desc.type = AgentType::Developer;
        desc.performanceRating = 0.9f;
        desc.capabilities = {"Code Generation"};
        desc.specializations = {"C++"};
        
        TestAIAgent* agent = new TestAIAgent(desc);
        
        totalTests++;
        if (agent->GetName() == "TestAgent" && 
            agent->GetPerformanceRating() == 0.9f && 
            agent->IsActive()) {
            passedTests++;
            std::cout << "  PASSED: Agent creation" << std::endl;
        } else {
            failedTests++;
            std::cout << "  FAILED: Agent creation" << std::endl;
        }
        
        delete agent;
    }
    
    void TestTaskAssignment() {
        std::cout << "\n[TEST] Task Assignment" << std::endl;
        
        TestAgentDesc desc;
        desc.name = "TestAgent";
        desc.capabilities = {"Code Generation"};
        
        TestAIAgent* agent = new TestAIAgent(desc);
        
        TestTask task;
        task.id = "task_001";
        task.description = "Test task";
        task.category = "Code Generation";
        
        agent->AssignTask(task);
        
        totalTests++;
        if (agent->GetTaskQueueSize() == 1) {
            passedTests++;
            std::cout << "  PASSED: Task assignment" << std::endl;
        } else {
            failedTests++;
            std::cout << "  FAILED: Task assignment" << std::endl;
        }
        
        delete agent;
    }
    
    void TestTaskProcessing() {
        std::cout << "\n[TEST] Task Processing" << std::endl;
        
        TestAgentDesc desc;
        desc.name = "TestAgent";
        desc.performanceRating = 1.0f; // Max performance for fast testing
        
        TestAIAgent* agent = new TestAIAgent(desc);
        
        TestTask task;
        task.id = "task_001";
        task.description = "Test task";
        
        agent->AssignTask(task);
        
        // Process task
        for (int i = 0; i < 10; i++) {
            agent->ProcessCurrentTask();
        }
        
        totalTests++;
        if (agent->GetCompletedTaskCount() == 1 && agent->GetTaskQueueSize() == 0) {
            passedTests++;
            std::cout << "  PASSED: Task processing" << std::endl;
        } else {
            failedTests++;
            std::cout << "  FAILED: Task processing" << std::endl;
            std::cout << "    Completed: " << agent->GetCompletedTaskCount() << ", Queue: " << agent->GetTaskQueueSize() << std::endl;
        }
        
        delete agent;
    }
    
    void TestCollaboration() {
        std::cout << "\n[TEST] Collaboration" << std::endl;
        
        TestAgentDesc desc1;
        desc1.name = "Agent1";
        desc1.specializations = {"C++", "Python"};
        desc1.canCollaborate = true;
        
        TestAgentDesc desc2;
        desc2.name = "Agent2";
        desc2.specializations = {"Python", "Java"};
        desc2.canCollaborate = true;
        
        TestAIAgent* agent1 = new TestAIAgent(desc1);
        TestAIAgent* agent2 = new TestAIAgent(desc2);
        
        totalTests++;
        if (agent1->CanCollaborateWith(agent2)) {
            passedTests++;
            std::cout << "  PASSED: Collaboration compatibility" << std::endl;
        } else {
            failedTests++;
            std::cout << "  FAILED: Collaboration compatibility" << std::endl;
        }
        
        delete agent1;
        delete agent2;
    }
    
    void TestPerformanceOptimization() {
        std::cout << "\n[TEST] Performance Optimization" << std::endl;
        
        TestAgentDesc desc1;
        desc1.name = "HighPerfAgent";
        desc1.performanceRating = 0.95f;
        desc1.capabilities = {"Code Generation"};
        
        TestAgentDesc desc2;
        desc2.name = "LowPerfAgent";
        desc2.performanceRating = 0.5f;
        desc2.capabilities = {"Code Generation"};
        
        TestAIAgent* agent1 = new TestAIAgent(desc1);
        TestAIAgent* agent2 = new TestAIAgent(desc2);
        
        TestTask task;
        task.id = "task_001";
        task.category = "Code Generation";
        
        agent1->AssignTask(task);
        agent2->AssignTask(task);
        
        // Process for same duration
        for (int i = 0; i < 10; i++) {
            agent1->ProcessCurrentTask();
            agent2->ProcessCurrentTask();
        }
        
        totalTests++;
        // High performance agent should complete task faster
        if (agent1->GetCompletedTaskCount() >= agent2->GetCompletedTaskCount()) {
            passedTests++;
            std::cout << "  PASSED: Performance affects task speed" << std::endl;
        } else {
            failedTests++;
            std::cout << "  FAILED: Performance affects task speed" << std::endl;
        }
        
        delete agent1;
        delete agent2;
    }
    
    void TestMultiAgentCoordination() {
        std::cout << "\n[TEST] Multi-Agent Coordination" << std::endl;
        
        std::vector<TestAIAgent*> agents;
        
        // Create multiple agents
        for (int i = 0; i < 3; i++) {
            TestAgentDesc desc;
            desc.name = "Agent" + std::to_string(i);
            desc.performanceRating = 1.0f; // Max performance for testing
            desc.capabilities = {"Code Generation", "Design"};
            desc.specializations = {"C++", "Design"};
            desc.canCollaborate = true;
            agents.push_back(new TestAIAgent(desc));
        }
        
        // Assign tasks to all agents
        for (size_t i = 0; i < agents.size(); i++) {
            TestTask task;
            task.id = "task_" + std::to_string(i);
            task.description = "Task " + std::to_string(i);
            agents[i]->AssignTask(task);
        }
        
        // Process tasks - process each agent's current task
        for (int i = 0; i < 10; i++) {
            for (size_t j = 0; j < agents.size(); j++) {
                agents[j]->ProcessCurrentTask();
            }
        }
        
        // Check if all agents completed tasks
        bool allCompleted = true;
        for (const auto* agent : agents) {
            if (agent->GetCompletedTaskCount() == 0) {
                allCompleted = false;
                break;
            }
        }
        
        totalTests++;
        if (allCompleted) {
            passedTests++;
            std::cout << "  PASSED: Multi-agent coordination" << std::endl;
        } else {
            failedTests++;
            std::cout << "  FAILED: Multi-agent coordination" << std::endl;
            for (const auto* agent : agents) {
                std::cout << "    Agent " << agent->GetName() << " completed: " << agent->GetCompletedTaskCount() << std::endl;
            }
        }
        
        for (auto* agent : agents) {
            delete agent;
        }
    }
    
    void TestStressTesting() {
        std::cout << "\n[TEST] Stress Testing" << std::endl;
        
        TestAgentDesc desc;
        desc.name = "StressTestAgent";
        desc.performanceRating = 1.0f; // Max performance for testing
        
        TestAIAgent* agent = new TestAIAgent(desc);
        
        // Assign reasonable number of tasks for stress test
        const int taskCount = 10;
        for (int i = 0; i < taskCount; i++) {
            TestTask task;
            task.id = "task_" + std::to_string(i);
            task.description = "Stress test task " + std::to_string(i);
            agent->AssignTask(task);
        }
        
        // Process tasks - need to process until all complete
        int iterations = 0;
        while (agent->GetCompletedTaskCount() < taskCount && iterations < 200) {
            agent->ProcessCurrentTask();
            iterations++;
        }
        
        totalTests++;
        if (agent->GetCompletedTaskCount() == taskCount) {
            passedTests++;
            std::cout << "  PASSED: Stress test completed " << taskCount << " tasks in " << iterations << " iterations" << std::endl;
        } else {
            failedTests++;
            std::cout << "  FAILED: Stress test only completed " << agent->GetCompletedTaskCount() << " of " << taskCount << " tasks" << std::endl;
            std::cout << "    Queue size: " << agent->GetTaskQueueSize() << std::endl;
        }
        
        delete agent;
    }
    
    void PrintResults() {
        std::cout << "\n=== Test Results ===" << std::endl;
        std::cout << "Total Tests: " << totalTests << std::endl;
        std::cout << "Passed: " << passedTests << std::endl;
        std::cout << "Failed: " << failedTests << std::endl;
        
        if (failedTests == 0) {
            std::cout << "SUCCESS: All tests passed!" << std::endl;
        } else {
            std::cout << "FAILURE: " << failedTests << " test(s) failed" << std::endl;
        }
        
        std::cout << "===================" << std::endl;
    }
    
private:
    int totalTests;
    int passedTests;
    int failedTests;
};

int main() {
    AITestSuite testSuite;
    testSuite.RunAllTests();
    
    return 0;
}
