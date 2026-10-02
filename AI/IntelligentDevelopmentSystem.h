/**
 * Potato Engine Intelligent Development System
 * AI-powered development automation including code generation, analysis, testing, and optimization
 */

#pragma once

#include "LLMIntegration.h"
#include "RAGSystem.h"
#include "AgentChain.h"
#include "ToolFramework.h"
#include "AIAgentSystem.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>

namespace Potato {
namespace AI {

/**
 * Development Task Type
 */
enum class DevTaskType {
    CodeGeneration,      // Generate code from specifications
    CodeAnalysis,        // Analyze existing code
    CodeRefactoring,     // Refactor code for improvements
    TestGeneration,     // Generate test cases
    Documentation,       // Generate documentation
    BugFixing,          // Fix bugs automatically
    Optimization,        // Optimize performance
    DependencyAnalysis,  // Analyze dependencies
    ArchitectureReview,  // Review architecture
    CodeReview          // Review code quality
};

/**
 * Development Task
 */
struct DevTask {
    std::string id;
    DevTaskType type;
    std::string description;
    std::string context;
    std::unordered_map<std::string, std::string> parameters;
    TaskPriority priority;
    TaskStatus status;
    std::string result;
    std::string error;
    uint64_t createdTime;
    uint64_t completedTime;
    
    DevTask()
        : type(DevTaskType::CodeGeneration)
        , priority(TaskPriority::Medium)
        , status(TaskStatus::Pending)
        , createdTime(0)
        , completedTime(0) {
    }
};

/**
 * Code Analysis Result
 */
struct CodeAnalysisResult {
    std::string filePath;
    int lineCount;
    int complexity;
    std::vector<std::string> issues;
    std::vector<std::string> suggestions;
    std::vector<std::string> smells;
    float qualityScore;
    
    CodeAnalysisResult()
        : lineCount(0)
        , complexity(0)
        , qualityScore(0.0f) {
    }
};

/**
 * Code Generation Result
 */
struct CodeGenerationResult {
    std::string generatedCode;
    std::string language;
    std::vector<std::string> dependencies;
    std::vector<std::string> comments;
    bool success;
    std::string error;
    
    CodeGenerationResult()
        : success(false) {
    }
};

/**
 * Test Generation Result
 */
struct TestGenerationResult {
    std::string testCode;
    std::string testFramework;
    std::vector<std::string> testCases;
    int coverage;
    bool success;
    std::string error;
    
    TestGenerationResult()
        : coverage(0)
        , success(false) {
    }
};

/**
 * Refactoring Suggestion
 */
struct RefactoringSuggestion {
    std::string type;
    std::string description;
    std::string originalCode;
    std::string refactoredCode;
    std::string reason;
    float confidence;
    
    RefactoringSuggestion()
        : confidence(0.0f) {
    }
};

/**
 * Intelligent Development System
 * Main system for AI-powered development automation
 */
class IntelligentDevelopmentSystem {
public:
    IntelligentDevelopmentSystem();
    ~IntelligentDevelopmentSystem();
    
    // Initialization
    bool Initialize(ILLMClient* llmClient, AIAgentManager* agentManager);
    void Shutdown();
    
    // Configuration
    void SetRAGSystem(RAGSystem* ragSystem);
    void SetToolExecutor(ToolExecutor* executor);
    
    // Task Management
    std::string CreateTask(DevTaskType type, const std::string& description, const std::unordered_map<std::string, std::string>& params = {});
    DevTask* GetTask(const std::string& taskId);
    void CancelTask(const std::string& taskId);
    std::vector<DevTask> GetPendingTasks();
    std::vector<DevTask> GetCompletedTasks();
    
    // Code Generation
    CodeGenerationResult GenerateCode(
        const std::string& specification,
        const std::string& language = "C++",
        const std::string& context = "");
    
    CodeGenerationResult GenerateCodeFromPrompt(
        const std::vector<ChatMessage>& messages,
        const std::string& language = "C++");
    
    // Code Analysis
    CodeAnalysisResult AnalyzeCode(const std::string& code, const std::string& language = "C++");
    CodeAnalysisResult AnalyzeFile(const std::string& filePath);
    std::vector<CodeAnalysisResult> AnalyzeProject(const std::string& projectPath);
    
    // Code Refactoring
    std::vector<RefactoringSuggestion> SuggestRefactoring(const std::string& code);
    std::string ApplyRefactoring(const std::string& code, const RefactoringSuggestion& suggestion);
    
    // Test Generation
    TestGenerationResult GenerateTests(const std::string& code, const std::string& language = "C++");
    TestGenerationResult GenerateTestsForFunction(const std::string& functionCode);
    
    // Documentation
    std::string GenerateDocumentation(const std::string& code);
    std::string GenerateAPIReference(const std::string& headerFile);
    std::string GenerateUserGuide(const std::string& projectPath);
    
    // Bug Fixing
    std::string FixBug(const std::string& code, const std::string& bugDescription);
    std::string AnalyzeBug(const std::string& errorLog);
    
    // Optimization
    std::string OptimizeCode(const std::string& code);
    std::vector<std::string> SuggestOptimizations(const std::string& code);
    
    // Dependency Analysis
    std::vector<std::string> AnalyzeDependencies(const std::string& projectPath);
    std::vector<std::string> DetectCircularDependencies(const std::string& projectPath);
    
    // Architecture Review
    std::string ReviewArchitecture(const std::string& projectPath);
    std::vector<std::string> SuggestArchitectureImprovements(const std::string& projectPath);
    
    // Code Review
    std::string ReviewCode(const std::string& code, const std::string& diff = "");
    std::vector<std::string> GetReviewComments(const std::string& code);
    
    // Batch Operations
    void AnalyzeProjectAsync(const std::string& projectPath, std::function<void(const std::vector<CodeAnalysisResult>&)> callback);
    void GenerateTestsAsync(const std::string& projectPath, std::function<void(const std::vector<TestGenerationResult>&)> callback);
    
    // Statistics
    struct DevSystemStats {
        size_t totalTasks;
        size_t completedTasks;
        size_t failedTasks;
        size_t codeGenerations;
        size_t codeAnalyses;
        size_t testGenerations;
        float averageTaskTime;
    };
    DevSystemStats GetStats() const;
    void ResetStats();
    
private:
    friend class DevelopmentAssistant;

    ILLMClient* llmClient;
    AIAgentManager* agentManager;
    RAGSystem* ragSystem;
    ToolExecutor* toolExecutor;
    
    std::unordered_map<std::string, DevTask> tasks;
    DevSystemStats stats;
    
    // Internal methods
    std::string BuildPrompt(const std::string& task, const std::string& context);
    std::string ExtractCodeFromResponse(const std::string& response);
    CodeAnalysisResult ParseAnalysisResult(const std::string& response);
    TestGenerationResult ParseTestResult(const std::string& response);
};

/**
 * Development Chain Templates
 * Pre-built chains for common development workflows
 */
namespace DevChainTemplates {
    // Full development workflow
    std::unique_ptr<AgentChain> CreateFullDevWorkflow(
        const std::string& specification,
        const std::string& language);
    
    // Code review workflow
    std::unique_ptr<AgentChain> CreateCodeReviewWorkflow(
        const std::string& code,
        const std::string& language);
    
    // Bug fixing workflow
    std::unique_ptr<AgentChain> CreateBugFixingWorkflow(
        const std::string& code,
        const std::string& bugDescription);
    
    // Optimization workflow
    std::unique_ptr<AgentChain> CreateOptimizationWorkflow(
        const std::string& code);
    
    // Documentation workflow
    std::unique_ptr<AgentChain> CreateDocumentationWorkflow(
        const std::string& projectPath);
}

/**
 * Development Assistant
 * Interactive assistant for developers
 */
class DevelopmentAssistant {
public:
    DevelopmentAssistant(IntelligentDevelopmentSystem* devSystem);
    ~DevelopmentAssistant();
    
    // Interactive help
    std::string Ask(const std::string& question);
    std::string ExplainCode(const std::string& code);
    std::string SuggestImprovement(const std::string& code);
    
    // Learning
    void LearnFromContext(const std::string& code, const std::string& context);
    void LearnFromFeedback(const std::string& taskId, bool wasHelpful);
    
    // Personalization
    void SetCodingStyle(const std::unordered_map<std::string, std::string>& style);
    void SetPreferences(const std::unordered_map<std::string, std::string>& prefs);
    
private:
    IntelligentDevelopmentSystem* devSystem;
    std::unordered_map<std::string, std::string> codingStyle;
    std::unordered_map<std::string, std::string> preferences;
};

/**
 * Dev System Utilities
 */
namespace DevSystemUtils {
    // Code formatting
    std::string FormatCode(const std::string& code, const std::string& language);
    std::string IndentCode(const std::string& code, int spaces);
    
    // Code extraction
    std::string ExtractFunction(const std::string& code, const std::string& functionName);
    std::string ExtractClass(const std::string& code, const std::string& className);
    
    // Metrics calculation
    int CalculateComplexity(const std::string& code);
    int CalculateLinesOfCode(const std::string& code);
    float CalculateMaintainabilityIndex(const std::string& code);
    
    // Validation
    bool ValidateSyntax(const std::string& code, const std::string& language);
    std::vector<std::string> GetSyntaxErrors(const std::string& code, const std::string& language);
}

} // namespace AI
} // namespace Potato
