/**
 * Ultimate AI Agent Demo
 * Demonstrates the complete AI capabilities of Potato Engine
 * Including LLM integration, RAG, Agent Chains, and Tool Framework
 */

#include "AI/AIAgentSystem.h"
#include "AI/LLMIntegration.h"
#include "AI/RAGSystem.h"
#include "AI/AgentChain.h"
#include "AI/ToolFramework.h"
#include <iostream>
#include <memory>

using namespace Potato::AI;

int main() {
    std::cout << "=== Potato Engine Ultimate AI Agent Demo ===\n\n";
    
    // ============================================================================
    // 1. Initialize AI Agent System
    // ============================================================================
    std::cout << "[1] Initializing AI Agent System...\n";
    
    AIAgentSystem agentSystem;
    if (!agentSystem.Initialize()) {
        std::cout << "Failed to initialize AI Agent System\n";
        return 1;
    }
    
    AIAgentManager* agentManager = agentSystem.GetAgentManager();
    agentSystem.SetLearningEnabled(true);
    agentSystem.SetCollaborationEnabled(true);
    
    std::cout << "AI Agent System initialized successfully\n\n";
    
    // ============================================================================
    // 2. Create Specialized Agents
    // ============================================================================
    std::cout << "[2] Creating Specialized Agents...\n";
    
    // Developer Agent
    AgentDesc devDesc;
    devDesc.name = "Developer";
    devDesc.type = AgentType::Developer;
    devDesc.autonomous = true;
    devDesc.capabilities = {"code_generation", "code_analysis", "debugging"};
    devDesc.canUseTools = true;
    devDesc.canCollaborate = true;
    
    AIAgent* developerAgent = agentManager->CreateAgent(devDesc);
    std::cout << "Created Developer Agent\n";
    
    // Analyst Agent
    AgentDesc analystDesc;
    analystDesc.name = "Analyst";
    analystDesc.type = AgentType::Analyst;
    analystDesc.autonomous = true;
    analystDesc.capabilities = {"data_analysis", "pattern_recognition", "optimization"};
    analystDesc.canUseTools = true;
    
    AIAgent* analystAgent = agentManager->CreateAgent(analystDesc);
    std::cout << "Created Analyst Agent\n";
    
    // Planner Agent
    AgentDesc plannerDesc;
    plannerDesc.name = "Planner";
    plannerDesc.type = AgentType::Planner;
    plannerDesc.autonomous = true;
    plannerDesc.capabilities = {"task_planning", "resource_allocation", "scheduling"};
    
    AIAgent* plannerAgent = agentManager->CreateAgent(plannerDesc);
    std::cout << "Created Planner Agent\n";
    
    std::cout << "Total agents created: " << agentManager->GetAgentCount() << "\n\n";
    
    // ============================================================================
    // 3. Initialize LLM Integration
    // ============================================================================
    std::cout << "[3] Initializing LLM Integration...\n";
    
    LLMManager llmManager;
    
    // Register OpenAI client (placeholder - would use real API key)
    // auto openaiClient = std::make_unique<OpenAIClient>("your-api-key");
    // llmManager.RegisterClient(LLMProvider::OpenAI, std::move(openaiClient));
    
    // Register Anthropic client (placeholder)
    // auto anthropicClient = std::make_unique<AnthropicClient>("your-api-key");
    // llmManager.RegisterClient(LLMProvider::Anthropic, std::move(anthropicClient));
    
    // Register local model client
    auto localClient = std::make_unique<LocalModelClient>("llama-2-7b");
    llmManager.RegisterClient(LLMProvider::Local, std::move(localClient));
    llmManager.SetDefaultProvider(LLMProvider::Local);
    
    llmManager.EnableCaching(true);
    llmManager.EnableRateLimiting(true);
    llmManager.SetRateLimit(60);
    
    std::cout << "LLM Integration initialized successfully\n";
    std::cout << "Default provider: Local Model\n\n";
    
    // ============================================================================
    // 4. Initialize RAG System
    // ============================================================================
    std::cout << "[4] Initializing RAG System...\n";
    
    auto vectorDB = std::make_unique<InMemoryVectorDB>();
    RAGSystem ragSystem(std::move(vectorDB));
    
    ragSystem.Initialize(llmManager.GetClient(LLMProvider::Local));
    
    RAGConfig ragConfig;
    ragConfig.chunkSize = 512;
    ragConfig.chunkOverlap = 50;
    ragConfig.topK = 5;
    ragConfig.similarityThreshold = 0.7f;
    ragConfig.strategy = RetrievalStrategy::Hybrid;
    ragConfig.rerank = true;
    ragSystem.SetConfig(ragConfig);
    
    // Add sample documents
    Document doc1 = RAGUtils::CreateDocument("Potato Engine Guide", 
        "Potato Engine is a powerful game engine with AI capabilities. "
        "It supports neural networks, reinforcement learning, and natural language processing. "
        "The engine is designed for creating AI-driven games and applications.");
    
    Document doc2 = RAGUtils::CreateDocument("AI Agent System",
        "The AI Agent System provides intelligent agents that can perform various tasks. "
        "Agents can collaborate, learn from experience, and use tools. "
        "Supported agent types include Developer, Analyst, Planner, and more.");
    
    ragSystem.AddDocument(doc1);
    ragSystem.AddDocument(doc2);
    
    std::cout << "RAG System initialized successfully\n";
    std::cout << "Documents added: " << vectorDB->GetDocumentCount() << "\n";
    std::cout << "Chunks created: " << vectorDB->GetChunkCount() << "\n\n";
    
    // ============================================================================
    // 5. Initialize Tool Framework
    // ============================================================================
    std::cout << "[5] Initializing Tool Framework...\n";
    
    ToolRegistry toolRegistry;
    
    // Register built-in tools
    toolRegistry.RegisterTool(BuiltinTools::CreateReadFileTool());
    toolRegistry.RegisterTool(BuiltinTools::CreateWriteFileTool());
    toolRegistry.RegisterTool(BuiltinTools::CreateWebSearchTool());
    toolRegistry.RegisterTool(BuiltinTools::CreateCodeAnalysisTool());
    toolRegistry.RegisterTool(BuiltinTools::CreateCodeGenerationTool());
    
    ToolExecutor toolExecutor(&toolRegistry);
    toolExecutor.EnableSafetyChecks(true);
    toolExecutor.SetMaxExecutionTime(30000);
    
    std::cout << "Tool Framework initialized successfully\n";
    std::cout << "Tools registered: " << toolRegistry.GetToolCount() << "\n\n";
    
    // ============================================================================
    // 6. Create Agent Chain
    // ============================================================================
    std::cout << "[6] Creating Agent Chain...\n";
    
    // Create a sequential chain
    auto sequentialChain = ChainTemplates::CreateSequentialChain(
        "Development Chain",
        {"Developer", "Analyst", "Planner"});
    
    sequentialChain->SetAIAgentManager(agentManager);
    sequentialChain->SetLLMClient(llmManager.GetClient(LLMProvider::Local));
    
    std::cout << "Agent Chain created successfully\n";
    std::cout << "Chain validation: " << (sequentialChain->Validate() ? "PASSED" : "FAILED") << "\n\n";
    
    // ============================================================================
    // 7. Execute Agent Chain
    // ============================================================================
    std::cout << "[7] Executing Agent Chain...\n";
    
    ChainResult chainResult = sequentialChain->Execute();
    
    if (chainResult.success) {
        std::cout << "Chain execution SUCCESS\n";
        std::cout << "Output: " << chainResult.output << "\n";
        std::cout << "Execution time: " << chainResult.executionTime << "ms\n";
    } else {
        std::cout << "Chain execution FAILED\n";
        std::cout << "Error: " << chainResult.error << "\n";
    }
    
    std::cout << "\n";
    
    // ============================================================================
    // 8. RAG Query
    // ============================================================================
    std::cout << "[8] Testing RAG Query...\n";
    
    std::string query = "What are the capabilities of Potato Engine?";
    std::string ragResponse = ragSystem.GenerateResponse(query);
    
    std::cout << "Query: " << query << "\n";
    std::cout << "Response: " << ragResponse << "\n\n";
    
    // ============================================================================
    // 9. Tool Execution
    // ============================================================================
    std::cout << "[9] Testing Tool Execution...\n";
    
    std::unordered_map<std::string, std::string> toolParams;
    toolParams["code"] = "int main() { return 0; }";
    toolParams["language"] = "C++";
    
    ToolResult toolResult = toolExecutor.Execute("code_analysis", toolParams);
    
    if (toolResult.success) {
        std::cout << "Tool execution SUCCESS\n";
        std::cout << "Output: " << toolResult.output << "\n";
        std::cout << "Execution time: " << toolResult.executionTime << "ms\n";
    } else {
        std::cout << "Tool execution FAILED\n";
        std::cout << "Error: " << toolResult.error << "\n";
    }
    
    std::cout << "\n";
    
    // ============================================================================
    // 10. Statistics
    // ============================================================================
    std::cout << "[10] System Statistics\n";
    std::cout << "========================\n";
    
    // Agent System Statistics
    std::cout << "AI Agent System:\n";
    std::cout << "  Total agents: " << agentManager->GetAgentCount() << "\n";
    std::cout << "  Active agents: " << agentManager->GetActiveAgentCount() << "\n";
    std::cout << "  Total memories: " << agentManager->GetTotalMemoryCount() << "\n";
    std::cout << "  Total tools: " << agentManager->GetTotalToolCount() << "\n";
    std::cout << "  Knowledge shares: " << agentManager->GetKnowledgeShareCount() << "\n";
    std::cout << "  Average performance: " << agentManager->GetAveragePerformanceRating() << "\n\n";
    
    // LLM Statistics
    LLMManager::LLMStats llmStats = llmManager.GetStats();
    std::cout << "LLM Manager:\n";
    std::cout << "  Total requests: " << llmStats.totalRequests << "\n";
    std::cout << "  Successful requests: " << llmStats.successfulRequests << "\n";
    std::cout << "  Failed requests: " << llmStats.failedRequests << "\n";
    std::cout << "  Total tokens: " << llmStats.totalTokens << "\n";
    std::cout << "  Average response time: " << llmStats.averageResponseTime << "s\n\n";
    
    // RAG Statistics
    RAGSystem::RAGStats ragStats = ragSystem.GetStats();
    std::cout << "RAG System:\n";
    std::cout << "  Total queries: " << ragStats.totalQueries << "\n";
    std::cout << "  Total retrievals: " << ragStats.totalRetrievals << "\n";
    std::cout << "  Total generations: " << ragStats.totalGenerations << "\n";
    std::cout << "  Average retrieval time: " << ragStats.averageRetrievalTime << "s\n";
    std::cout << "  Average generation time: " << ragStats.averageGenerationTime << "s\n\n";
    
    // Tool Executor Statistics
    ToolExecutor::ExecutorStats toolStats = toolExecutor.GetStats();
    std::cout << "Tool Executor:\n";
    std::cout << "  Total executions: " << toolStats.totalExecutions << "\n";
    std::cout << "  Successful executions: " << toolStats.successfulExecutions << "\n";
    std::cout << "  Failed executions: " << toolStats.failedExecutions << "\n";
    std::cout << "  Average execution time: " << toolStats.averageExecutionTime << "s\n\n";
    
    // ============================================================================
    // 11. Cleanup
    // ============================================================================
    std::cout << "[11] Shutting down...\n";
    
    agentSystem.Shutdown();
    ragSystem.Shutdown();
    
    std::cout << "Shutdown complete\n";
    
    std::cout << "\n=== Demo Complete ===\n";
    std::cout << "Potato Engine is now the ultimate AI Agent platform!\n";
    
    return 0;
}
