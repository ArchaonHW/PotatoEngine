#include "AI/AIAgentSystem.h"
#include "GUI/AgentGUI.h"
#include "Logging/Logger.h"
#include "Time/TimeManager.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace Potato;
using namespace Potato::AI;
using namespace Potato::GUI;

/**
 * AI Agent GUI 整合示例
 * 展示如何將 AI Agent 系統與 GUI 系統整合
 */
class AIAgentGUIExample {
public:
    AIAgentGUIExample() : running(false) {
    }
    
    ~AIAgentGUIExample() {
        Shutdown();
    }
    
    bool Initialize() {
        std::cout << "\n🚀 初始化 AI Agent GUI 示例..." << std::endl;
        std::cout << "====================================" << std::endl;
        
        // 初始化 AI 系統
        aiSystem = std::make_unique<AIAgentSystem>();
        if (!aiSystem->Initialize()) {
            std::cerr << "❌ AI 系統初始化失敗" << std::endl;
            return false;
        }
        std::cout << "✓ AI 系統初始化成功" << std::endl;
        
        // 初始化 GUI 系統
        guiSystem = std::make_unique<AgentGUISystem>();
        if (!guiSystem->Initialize()) {
            std::cerr << "❌ GUI 系統初始化失敗" << std::endl;
            return false;
        }
        std::cout << "✓ GUI 系統初始化成功" << std::endl;
        
        // 連接系統
        guiSystem->SetAgentSystem(aiSystem.get());
        std::cout << "✓ 系統連接完成" << std::endl;
        
        // 創建示例代理
        CreateExampleAgents();
        
        // 設置主題
        guiSystem->SetTheme(GUITheme::Dark);
        
        std::cout << "====================================" << std::endl;
        std::cout << "✓ 初始化完成" << std::endl;
        
        return true;
    }
    
    void Shutdown() {
        if (!running) {
            return;
        }
        
        std::cout << "\n🛑 關閉 AI Agent GUI 示例..." << std::endl;
        
        running = false;
        
        if (guiSystem) {
            guiSystem->Shutdown();
        }
        
        if (aiSystem) {
            aiSystem->Shutdown();
        }
        
        std::cout << "✓ 關閉完成" << std::endl;
    }
    
    void Run() {
        running = true;
        
        std::cout << "\n🎮 開始運行 AI Agent GUI 示例..." << std::endl;
        std::cout << "====================================" << std::endl;
        
        // 運行主循環
        while (running) {
            float deltaTime = GetDeltaTime();
            
            // 更新 AI 系統
            aiSystem->Update(deltaTime);
            
            // 更新 GUI 系統
            guiSystem->Update(deltaTime);
            
            // 渲染 GUI
            if (frameCount % 60 == 0) { // 每秒渲染一次
                guiSystem->Render();
                aiSystem->PrintStatistics();
            }
            
            frameCount++;
            
            // 控制幀率
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            
            // 運行一段時間後自動停止
            if (frameCount > 300) { // 5秒後停止
                running = false;
            }
        }
        
        std::cout << "====================================" << std::endl;
        std::cout << "🎮 運行結束" << std::endl;
    }
    
private:
    void CreateExampleAgents() {
        std::cout << "\n🤖 創建示例代理..." << std::endl;
        
        auto manager = aiSystem->GetAgentManager();
        
        // 創建開發代理
        AgentDesc devDesc;
        devDesc.name = "CodeMaster";
        devDesc.type = AgentType::Developer;
        devDesc.autonomous = true;
        devDesc.performanceRating = 0.9f;
        devDesc.learningRate = 0.15f;
        devDesc.capabilities = {"Code Generation", "Code Optimization", "Debugging"};
        
        AIAgent* devAgent = manager->CreateAgent(devDesc);
        if (devAgent) {
            std::cout << "✓ 創建開發代理: " << devAgent->GetName() << std::endl;
        }
        
        // 創建設計代理
        AgentDesc designDesc;
        designDesc.name = "CreativeBot";
        designDesc.type = AgentType::Designer;
        designDesc.autonomous = true;
        designDesc.performanceRating = 0.85f;
        designDesc.learningRate = 0.2f;
        designDesc.capabilities = {"Design Generation", "Brainstorming", "UX Design"};
        
        AIAgent* designAgent = manager->CreateAgent(designDesc);
        if (designAgent) {
            std::cout << "✓ 創建設計代理: " << designAgent->GetName() << std::endl;
        }
        
        // 創建分析代理
        AgentDesc analystDesc;
        analystDesc.name = "DataWizard";
        analystDesc.type = AgentType::Analyst;
        analystDesc.autonomous = true;
        analystDesc.performanceRating = 0.88f;
        analystDesc.learningRate = 0.12f;
        analystDesc.capabilities = {"Data Analysis", "Statistics", "Insights Generation"};
        
        AIAgent* analystAgent = manager->CreateAgent(analystDesc);
        if (analystAgent) {
            std::cout << "✓ 創建分析代理: " << analystAgent->GetName() << std::endl;
        }
        
        // 分配示例任務
        AssignExampleTasks();
    }
    
    void AssignExampleTasks() {
        std::cout << "\n📋 分配示例任務..." << std::endl;
        
        auto manager = aiSystem->GetAgentManager();
        
        // 為開發代理分配任務
        AgentTask codeTask;
        codeTask.id = "task_001";
        codeTask.description = "Generate player controller system";
        codeTask.category = "Code Generation";
        codeTask.priority = TaskPriority::High;
        codeTask.estimatedTime = 10000;
        
        manager->AssignTaskToAgent("CodeMaster", codeTask);
        std::cout << "✓ 分配代碼任務給 CodeMaster" << std::endl;
        
        // 為設計代理分配任務
        AgentTask designTask;
        designTask.id = "task_002";
        designTask.description = "Design game UI layout";
        designTask.category = "Design";
        designTask.priority = TaskPriority::Medium;
        designTask.estimatedTime = 8000;
        
        manager->AssignTaskToAgent("CreativeBot", designTask);
        std::cout << "✓ 分配設計任務給 CreativeBot" << std::endl;
        
        // 為分析代理分配任務
        AgentTask analysisTask;
        analysisTask.id = "task_003";
        analysisTask.description = "Analyze player behavior data";
        analysisTask.category = "Analysis";
        analysisTask.priority = TaskPriority::Medium;
        analysisTask.estimatedTime = 12000;
        
        manager->AssignTaskToAgent("DataWizard", analysisTask);
        std::cout << "✓ 分配分析任務給 DataWizard" << std::endl;
        
        // 測試群體決策
        TestGroupDecision();
    }
    
    void TestGroupDecision() {
        std::cout << "\n🧠 測試群體決策..." << std::endl;
        
        auto manager = aiSystem->GetAgentManager();
        
        std::string context = "Select the best rendering backend for the game";
        std::vector<std::string> options = {
            "OpenGL",
            "DirectX 11",
            "DirectX 12",
            "Vulkan"
        };
        
        Decision decision = manager->MakeGroupDecision(context, options);
        
        std::cout << "群體決策結果:" << std::endl;
        std::cout << "  選擇: " << decision.action << std::endl;
        std::cout << "  信心: " << decision.confidence << std::endl;
        std::cout << "  推理: ";
        for (const auto& reason : decision.reasoning) {
            std::cout << reason << "; ";
        }
        std::cout << std::endl;
    }
    
    float GetDeltaTime() {
        static auto lastTime = std::chrono::high_resolution_clock::now();
        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;
        return deltaTime;
    }
    
    std::unique_ptr<AIAgentSystem> aiSystem;
    std::unique_ptr<AgentGUISystem> guiSystem;
    bool running;
    size_t frameCount = 0;
};

/**
 * 主函數
 */
int main() {
    std::cout << "\n🥔 Potato Engine - AI Agent GUI 整合示例" << std::endl;
    std::cout << "====================================" << std::endl;
    std::cout << "版本: 1.0.0" << std::endl;
    std::cout << "作者: Potato Engine Team" << std::endl;
    std::cout << "====================================" << std::endl;
    
    AIAgentGUIExample example;
    
    if (!example.Initialize()) {
        std::cerr << "❌ 初始化失敗" << std::endl;
        return 1;
    }
    
    example.Run();
    
    std::cout << "\n✓ 示例運行完成" << std::endl;
    
    return 0;
}