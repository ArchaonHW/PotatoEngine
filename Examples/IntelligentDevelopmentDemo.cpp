/**
 * Intelligent Development System Demo
 * Demonstrates AI-powered development automation
 */

#include "AI/IntelligentDevelopmentSystem.h"
#include "AI/LLMIntegration.h"
#include "AI/AIAgentSystem.h"
#include <iostream>
#include <memory>
#include <chrono>

using namespace Potato::AI;

int main() {
    std::cout << "=== Potato Engine Intelligent Development System Demo ===\n\n";
    
    // ============================================================================
    // 1. Initialize Systems
    // ============================================================================
    std::cout << "[1] Initializing Systems...\n";
    
    // Initialize LLM Manager
    LLMManager llmManager;
    auto localClient = std::make_unique<LocalModelClient>("llama-2-7b");
    llmManager.RegisterClient(LLMProvider::Local, std::move(localClient));
    llmManager.SetDefaultProvider(LLMProvider::Local);
    
    // Initialize AI Agent System
    AIAgentSystem agentSystem;
    agentSystem.Initialize();
    AIAgentManager* agentManager = agentSystem.GetAgentManager();
    
    // Initialize Intelligent Development System
    IntelligentDevelopmentSystem devSystem;
    devSystem.Initialize(llmManager.GetClient(LLMProvider::Local), agentManager);
    
    std::cout << "Systems initialized successfully\n\n";
    
    // ============================================================================
    // 2. Code Generation
    // ============================================================================
    std::cout << "[2] Code Generation Demo\n";
    std::cout << "========================\n";
    
    std::string specification = "Create a C++ class for a 2D vector with x and y coordinates, "
                              "including methods for addition, subtraction, dot product, and magnitude.";
    
    CodeGenerationResult codeResult = devSystem.GenerateCode(specification, "C++");
    
    if (codeResult.success) {
        std::cout << "Code Generation: SUCCESS\n";
        std::cout << "Generated Code:\n";
        std::cout << codeResult.generatedCode << "\n";
    } else {
        std::cout << "Code Generation: FAILED\n";
        std::cout << "Error: " << codeResult.error << "\n";
    }
    
    std::cout << "\n";
    
    // ============================================================================
    // 3. Code Analysis
    // ============================================================================
    std::cout << "[3] Code Analysis Demo\n";
    std::cout << "=====================\n";
    
    std::string sampleCode = R"(
#include <iostream>
#include <vector>

void process_data(std::vector<int>& data) {
    for (int i = 0; i < data.size(); i++) {
        if (data[i] % 2 == 0) {
            data[i] *= 2;
        } else {
            data[i] += 1;
        }
    }
    
    int sum = 0;
    for (int i = 0; i < data.size(); i++) {
        sum += data[i];
    }
    
    std::cout << "Sum: " << sum << std::endl;
}
)";
    
    CodeAnalysisResult analysisResult = devSystem.AnalyzeCode(sampleCode, "C++");
    
    std::cout << "Code Analysis Results:\n";
    std::cout << "  Lines of Code: " << analysisResult.lineCount << "\n";
    std::cout << "  Complexity: " << analysisResult.complexity << "\n";
    std::cout << "  Quality Score: " << analysisResult.qualityScore << "\n";
    std::cout << "  Issues: " << analysisResult.issues.size() << "\n";
    for (const auto& issue : analysisResult.issues) {
        std::cout << "    - " << issue << "\n";
    }
    std::cout << "  Suggestions: " << analysisResult.suggestions.size() << "\n";
    for (const auto& suggestion : analysisResult.suggestions) {
        std::cout << "    - " << suggestion << "\n";
    }
    
    std::cout << "\n";
    
    // ============================================================================
    // 4. Refactoring Suggestions
    // ============================================================================
    std::cout << "[4] Refactoring Suggestions Demo\n";
    std::cout << "================================\n";
    
    std::vector<RefactoringSuggestion> refactorSuggestions = devSystem.SuggestRefactoring(sampleCode);
    
    std::cout << "Refactoring Suggestions: " << refactorSuggestions.size() << "\n";
    for (const auto& suggestion : refactorSuggestions) {
        std::cout << "  Type: " << suggestion.type << "\n";
        std::cout << "  Description: " << suggestion.description << "\n";
        std::cout << "  Confidence: " << suggestion.confidence << "\n";
        std::cout << "\n";
    }
    
    // ============================================================================
    // 5. Test Generation
    // ============================================================================
    std::cout << "[5] Test Generation Demo\n";
    std::cout << "=======================\n";
    
    std::string functionCode = R"(
int add(int a, int b) {
    return a + b;
}

int multiply(int a, int b) {
    return a * b;
}
)";
    
    TestGenerationResult testResult = devSystem.GenerateTestsForFunction(functionCode);
    
    if (testResult.success) {
        std::cout << "Test Generation: SUCCESS\n";
        std::cout << "Test Framework: " << testResult.testFramework << "\n";
        std::cout << "Estimated Coverage: " << testResult.coverage << "%\n";
        std::cout << "Test Cases: " << testResult.testCases.size() << "\n";
        std::cout << "Generated Tests:\n";
        std::cout << testResult.testCode << "\n";
    } else {
        std::cout << "Test Generation: FAILED\n";
        std::cout << "Error: " << testResult.error << "\n";
    }
    
    std::cout << "\n";
    
    // ============================================================================
    // 6. Documentation Generation
    // ============================================================================
    std::cout << "[6] Documentation Generation Demo\n";
    std::cout << "===================================\n";
    
    std::string documentation = devSystem.GenerateDocumentation(sampleCode);
    
    std::cout << "Generated Documentation:\n";
    std::cout << documentation << "\n";
    
    std::cout << "\n";
    
    // ============================================================================
    // 7. Code Optimization
    // ============================================================================
    std::cout << "[7] Code Optimization Demo\n";
    std::cout << "=========================\n";
    
    std::vector<std::string> optimizations = devSystem.SuggestOptimizations(sampleCode);
    
    std::cout << "Optimization Suggestions: " << optimizations.size() << "\n";
    for (const auto& opt : optimizations) {
        std::cout << "  - " << opt << "\n";
    }
    
    std::cout << "\n";
    
    // ============================================================================
    // 8. Development Assistant
    // ============================================================================
    std::cout << "[8] Development Assistant Demo\n";
    std::cout << "==============================\n";
    
    DevelopmentAssistant assistant(&devSystem);
    
    // Explain code
    std::string explanation = assistant.ExplainCode(sampleCode);
    std::cout << "Code Explanation:\n";
    std::cout << explanation << "\n";
    
    std::cout << "\n";
    
    // ============================================================================
    // 9. Task Management
    // ============================================================================
    std::cout << "[9] Task Management Demo\n";
    std::cout << "=======================\n";
    
    std::string taskId1 = devSystem.CreateTask(DevTaskType::CodeGeneration, "Create Player class");
    std::string taskId2 = devSystem.CreateTask(DevTaskType::CodeAnalysis, "Analyze GameEngine.cpp");
    std::string taskId3 = devSystem.CreateTask(DevTaskType::TestGeneration, "Generate tests for Utils");
    
    std::cout << "Created tasks: " << devSystem.GetPendingTasks().size() << "\n";
    
    // Complete a task
    DevTask* task = devSystem.GetTask(taskId1);
    if (task) {
        task->status = TaskStatus::Completed;
        task->result = "Player class created successfully";
        task->completedTime = std::chrono::system_clock::now().time_since_epoch().count();
    }
    
    std::cout << "Pending tasks: " << devSystem.GetPendingTasks().size() << "\n";
    std::cout << "Completed tasks: " << devSystem.GetCompletedTasks().size() << "\n";
    
    std::cout << "\n";
    
    // ============================================================================
    // 10. Statistics
    // ============================================================================
    std::cout << "[10] System Statistics\n";
    std::cout << "=====================\n";
    
    IntelligentDevelopmentSystem::DevSystemStats stats = devSystem.GetStats();
    
    std::cout << "Total Tasks: " << stats.totalTasks << "\n";
    std::cout << "Completed Tasks: " << stats.completedTasks << "\n";
    std::cout << "Failed Tasks: " << stats.failedTasks << "\n";
    std::cout << "Code Generations: " << stats.codeGenerations << "\n";
    std::cout << "Code Analyses: " << stats.codeAnalyses << "\n";
    std::cout << "Test Generations: " << stats.testGenerations << "\n";
    std::cout << "Average Task Time: " << stats.averageTaskTime << "s\n";
    
    std::cout << "\n";
    
    // ============================================================================
    // 11. Utilities Demo
    // ============================================================================
    std::cout << "[11] Utilities Demo\n";
    std::cout << "==================\n";
    
    int complexity = DevSystemUtils::CalculateComplexity(sampleCode);
    int loc = DevSystemUtils::CalculateLinesOfCode(sampleCode);
    float mi = DevSystemUtils::CalculateMaintainabilityIndex(sampleCode);
    
    std::cout << "Code Metrics:\n";
    std::cout << "  Lines of Code: " << loc << "\n";
    std::cout << "  Cyclomatic Complexity: " << complexity << "\n";
    std::cout << "  Maintainability Index: " << mi << "\n";
    
    std::cout << "\n";
    
    // ============================================================================
    // 12. Cleanup
    // ============================================================================
    std::cout << "[12] Shutting down...\n";
    
    devSystem.Shutdown();
    agentSystem.Shutdown();
    
    std::cout << "Shutdown complete\n";
    
    std::cout << "\n=== Demo Complete ===\n";
    std::cout << "Potato Engine Intelligent Development System is ready!\n";
    std::cout << "This system can automate code generation, analysis, testing, and more!\n";
    
    return 0;
}
