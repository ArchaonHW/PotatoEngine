#include "AgentGUI.h"
#include <iostream>
#include <algorithm>
#include <sstream>

namespace Potato {
namespace GUI {

// ============================================================================
// GUIPanel 實現
// ============================================================================

GUIPanel::GUIPanel(const std::string& name, PanelType type)
    : name(name)
    , type(type)
    , visible(true) {
}

GUIPanel::~GUIPanel() {
}

void GUIPanel::Update(float deltaTime) {
    if (updateCallback) {
        updateCallback();
    }
}

// ============================================================================
// DashboardPanel 實現
// ============================================================================

DashboardPanel::DashboardPanel()
    : GUIPanel("儀表板", PanelType::Dashboard)
    , agentSystem(nullptr)
    , totalAgents(0)
    , activeAgents(0)
    , pendingTasks(0)
    , completedTasks(0)
    , successRate(0.0f) {
}

void DashboardPanel::Render() {
    std::cout << "\n📊 儀表板面板" << std::endl;
    std::cout << "====================================" << std::endl;
    
    if (agentSystem && agentSystem->GetAgentManager()) {
        auto manager = agentSystem->GetAgentManager();
        
        totalAgents = manager->GetAgentCount();
        activeAgents = manager->GetActiveAgentCount();
        pendingTasks = manager->GetPendingTaskCount();
        completedTasks = manager->GetCompletedTaskCount();
        
        std::cout << "代理總數: " << totalAgents << std::endl;
        std::cout << "活躍代理: " << activeAgents << std::endl;
        std::cout << "待處理任務: " << pendingTasks << std::endl;
        std::cout << "已完成任務: " << completedTasks << std::endl;
        
        if (completedTasks > 0) {
            successRate = static_cast<float>(completedTasks) / static_cast<float>(completedTasks + 0);
            std::cout << "成功率: " << (successRate * 100.0f) << "%" << std::endl;
        }
    } else {
        std::cout << "AI 系統未連接" << std::endl;
    }
    
    std::cout << "====================================" << std::endl;
}

void DashboardPanel::SetAgentSystem(AI::AIAgentSystem* system) {
    agentSystem = system;
}

// ============================================================================
// AgentManagerPanel 實現
// ============================================================================

AgentManagerPanel::AgentManagerPanel()
    : GUIPanel("代理管理器", PanelType::AgentManager)
    , agentSystem(nullptr)
    , selectedAgent(nullptr)
    , showAgentList(true)
    , showAgentDetails(false)
    , showTaskList(false)
    , showCreateAgentDialog(false)
    , newAgentType(AI::AgentType::Custom) {
}

void AgentManagerPanel::Render() {
    std::cout << "\n🤖 代理管理器面板" << std::endl;
    std::cout << "====================================" << std::endl;
    
    if (showCreateAgentDialog) {
        RenderCreateAgentDialog();
    }
    
    if (showAgentList) {
        RenderAgentList();
    }
    
    if (showAgentDetails && selectedAgent) {
        RenderAgentDetails();
    }
    
    if (showTaskList && selectedAgent) {
        RenderTaskList();
    }
    
    std::cout << "====================================" << std::endl;
}

void AgentManagerPanel::SetAgentSystem(AI::AIAgentSystem* system) {
    agentSystem = system;
}

void AgentManagerPanel::RenderAgentList() {
    std::cout << "\n📋 代理列表:" << std::endl;
    
    if (agentSystem && agentSystem->GetAgentManager()) {
        auto manager = agentSystem->GetAgentManager();
        const auto& agents = manager->GetAgents();
        
        if (agents.empty()) {
            std::cout << "  無代理" << std::endl;
        } else {
            for (size_t i = 0; i < agents.size(); ++i) {
                auto agent = agents[i];
                std::cout << "  [" << i << "] " << agent->GetName();
                std::cout << " (" << (agent->IsActive() ? "活躍" : "停用") << ")";
                std::cout << " 類型: " << static_cast<int>(agent->GetType());
                std::cout << " 任務: " << agent->GetTasks().size();
                std::cout << std::endl;
            }
        }
        
        std::cout << "\n命令:" << std::endl;
        std::cout << "  [c] 創建新代理" << std::endl;
        std::cout << "  [s] 選擇代理" << std::endl;
        std::cout << "  [d] 刪除代理" << std::endl;
    }
}

void AgentManagerPanel::RenderAgentDetails() {
    std::cout << "\n📝 代理詳情:" << std::endl;
    std::cout << "  名稱: " << selectedAgent->GetName() << std::endl;
    std::cout << "  類型: " << static_cast<int>(selectedAgent->GetType()) << std::endl;
    std::cout << "  狀態: " << (selectedAgent->IsActive() ? "活躍" : "停用") << std::endl;
    std::cout << "  自主: " << (selectedAgent->IsAutonomous() ? "是" : "否") << std::endl;
    std::cout << "  性能評分: " << selectedAgent->GetPerformanceRating() << std::endl;
    std::cout << "  學習率: " << selectedAgent->GetLearningRate() << std::endl;
    std::cout << "  完成任務: " << selectedAgent->GetCompletedTaskCount() << std::endl;
    std::cout << "  失敗任務: " << selectedAgent->GetFailedTaskCount() << std::endl;
    std::cout << "  成功率: " << (selectedAgent->GetSuccessRate() * 100.0f) << "%" << std::endl;
    
    std::cout << "\n命令:" << std::endl;
    std::cout << "  [t] 查看任務" << std::endl;
    std::cout << "  [b] 返回列表" << std::endl;
}

void AgentManagerPanel::RenderTaskList() {
    std::cout << "\n📝 任務列表:" << std::endl;
    
    const auto& tasks = selectedAgent->GetTasks();
    
    if (tasks.empty()) {
        std::cout << "  無任務" << std::endl;
    } else {
        for (size_t i = 0; i < tasks.size(); ++i) {
            const auto& task = tasks[i];
            std::cout << "  [" << i << "] " << task.description;
            std::cout << " 狀態: ";
            
            switch (task.status) {
                case AI::TaskStatus::Pending:
                    std::cout << "待處理";
                    break;
                case AI::TaskStatus::InProgress:
                    std::cout << "進行中";
                    break;
                case AI::TaskStatus::Completed:
                    std::cout << "已完成";
                    break;
                case AI::TaskStatus::Failed:
                    std::cout << "失敗";
                    break;
                case AI::TaskStatus::Cancelled:
                    std::cout << "已取消";
                    break;
            }
            
            std::cout << " 進度: " << (task.progress * 100.0f) << "%" << std::endl;
        }
    }
    
    std::cout << "\n命令:" << std::endl;
    std::cout << "  [b] 返回詳情" << std::endl;
}

void AgentManagerPanel::RenderCreateAgentDialog() {
    std::cout << "\n➕ 創建新代理:" << std::endl;
    std::cout << "  名稱: " << newAgentName << std::endl;
    std::cout << "  類型: " << static_cast<int>(newAgentType) << std::endl;
    std::cout << "  [0] 開發代理" << std::endl;
    std::cout << "  [1] 設計代理" << std::endl;
    std::cout << "  [2] 分析代理" << std::endl;
    std::cout << "  [3] 測試代理" << std::endl;
    std::cout << "  [4] 調試代理" << std::endl;
    std::cout << "  [5] 研究代理" << std::endl;
    std::cout << "  [c] 確認創建" << std::endl;
    std::cout << "  [x] 取消" << std::endl;
}

// ============================================================================
// TaskViewerPanel 實現
// ============================================================================

TaskViewerPanel::TaskViewerPanel()
    : GUIPanel("任務查看器", PanelType::TaskViewer)
    , agentSystem(nullptr)
    , filterStatus(AI::TaskStatus::Pending) {
}

void TaskViewerPanel::Render() {
    std::cout << "\n📋 任務查看器面板" << std::endl;
    std::cout << "====================================" << std::endl;
    
    RefreshTaskList();
    RenderTaskList();
    
    std::cout << "====================================" << std::endl;
}

void TaskViewerPanel::SetAgentSystem(AI::AIAgentSystem* system) {
    agentSystem = system;
}

void TaskViewerPanel::RefreshTaskList() {
    allTasks.clear();
    
    if (agentSystem && agentSystem->GetAgentManager()) {
        auto manager = agentSystem->GetAgentManager();
        const auto& agents = manager->GetAgents();
        
        for (const auto& agent : agents) {
            const auto& tasks = agent->GetTasks();
            allTasks.insert(allTasks.end(), tasks.begin(), tasks.end());
        }
    }
}

void TaskViewerPanel::RenderTaskList() {
    std::cout << "\n📝 所有任務:" << std::endl;
    
    if (allTasks.empty()) {
        std::cout << "  無任務" << std::endl;
    } else {
        for (size_t i = 0; i < allTasks.size(); ++i) {
            const auto& task = allTasks[i];
            std::cout << "  [" << i << "] " << task.description;
            std::cout << " 類別: " << task.category;
            std::cout << " 狀態: ";
            
            switch (task.status) {
                case AI::TaskStatus::Pending:
                    std::cout << "待處理";
                    break;
                case AI::TaskStatus::InProgress:
                    std::cout << "進行中";
                    break;
                case AI::TaskStatus::Completed:
                    std::cout << "已完成";
                    break;
                case AI::TaskStatus::Failed:
                    std::cout << "失敗";
                    break;
                case AI::TaskStatus::Cancelled:
                    std::cout << "已取消";
                    break;
            }
            
            std::cout << " 進度: " << (task.progress * 100.0f) << "%" << std::endl;
        }
    }
}

void TaskViewerPanel::RenderTaskDetails(const AI::AgentTask& task) {
    std::cout << "\n📝 任務詳情:" << std::endl;
    std::cout << "  ID: " << task.id << std::endl;
    std::cout << "  描述: " << task.description << std::endl;
    std::cout << "  類別: " << task.category << std::endl;
    std::cout << "  優先級: " << static_cast<int>(task.priority) << std::endl;
    std::cout << "  狀態: " << static_cast<int>(task.status) << std::endl;
    std::cout << "  進度: " << (task.progress * 100.0f) << "%" << std::endl;
    std::cout << "  結果: " << task.result << std::endl;
    std::cout << "  錯誤: " << task.error << std::endl;
}

// ============================================================================
// ConsolePanel 實現
// ============================================================================

ConsolePanel::ConsolePanel()
    : GUIPanel("控制台", PanelType::Console) {
}

void ConsolePanel::Render() {
    std::cout << "\n💻 控制台面板" << std::endl;
    std::cout << "====================================" << std::endl;
    
    RenderLogMessages();
    RenderInputField();
    
    std::cout << "====================================" << std::endl;
}

void ConsolePanel::AddLog(const std::string& message) {
    logMessages.push_back(message);
    
    // 限制日誌大小
    if (logMessages.size() > 100) {
        logMessages.erase(logMessages.begin());
    }
}

void ConsolePanel::ClearLog() {
    logMessages.clear();
}

void ConsolePanel::RenderLogMessages() {
    std::cout << "\n📜 日誌:" << std::endl;
    
    if (logMessages.empty()) {
        std::cout << "  無日誌" << std::endl;
    } else {
        for (const auto& message : logMessages) {
            std::cout << "  " << message << std::endl;
        }
    }
}

void ConsolePanel::RenderInputField() {
    std::cout << "\n🔧 命令輸入: " << inputBuffer << std::endl;
    std::cout << "  [Enter] 執行命令" << std::endl;
    std::cout << "  [c] 清除日誌" << std::endl;
}

// ============================================================================
// SettingsPanel 實現
// ============================================================================

SettingsPanel::SettingsPanel()
    : GUIPanel("設置", PanelType::Settings)
    , agentSystem(nullptr)
    , maxAgents(100)
    , learningEnabled(false)
    , updateInterval(0.1f)
    , currentTheme(GUITheme::Dark) {
}

void SettingsPanel::Render() {
    std::cout << "\n⚙️ 設置面板" << std::endl;
    std::cout << "====================================" << std::endl;
    
    std::cout << "\nAI 系統設置:" << std::endl;
    std::cout << "  最大代理數量: " << maxAgents << std::endl;
    std::cout << "  學習系統: " << (learningEnabled ? "啟用" : "禁用") << std::endl;
    std::cout << "  更新間隔: " << updateInterval << " 秒" << std::endl;
    
    std::cout << "\nGUI 設置:" << std::endl;
    std::cout << "  主題: " << static_cast<int>(currentTheme) << std::endl;
    std::cout << "  [0] 深色主題" << std::endl;
    std::cout << "  [1] 淺色主題" << std::endl;
    std::cout << "  [2] 馬鈴薯主題" << std::endl;
    
    std::cout << "\n命令:" << std::endl;
    std::cout << "  [a] 應用設置" << std::endl;
    std::cout << "  [r] 重置為默認" << std::endl;
    
    std::cout << "====================================" << std::endl;
}

void SettingsPanel::SetAgentSystem(AI::AIAgentSystem* system) {
    agentSystem = system;
    
    if (agentSystem && agentSystem->GetAgentManager()) {
        learningEnabled = agentSystem->GetAgentManager()->IsLearningEnabled();
    }
}

void SettingsPanel::ApplySettings() {
    if (agentSystem) {
        agentSystem->SetMaxAgents(maxAgents);
        agentSystem->SetLearningEnabled(learningEnabled);
        
        std::cout << "✓ 設置已應用" << std::endl;
    }
}

// ============================================================================
// StatisticsPanel 實現
// ============================================================================

StatisticsPanel::StatisticsPanel()
    : GUIPanel("統計", PanelType::Statistics)
    , agentSystem(nullptr) {
}

void StatisticsPanel::Render() {
    std::cout << "\n📊 統計面板" << std::endl;
    std::cout << "====================================" << std::endl;
    
    RenderPerformanceCharts();
    RenderAgentStatistics();
    RenderTaskStatistics();
    
    std::cout << "====================================" << std::endl;
}

void StatisticsPanel::SetAgentSystem(AI::AIAgentSystem* system) {
    agentSystem = system;
}

void StatisticsPanel::RenderPerformanceCharts() {
    std::cout << "\n📈 性能圖表:" << std::endl;
    std::cout << "  FPS 歷史: " << fpsHistory.size() << " 個數據點" << std::endl;
    std::cout << "  任務歷史: " << taskHistory.size() << " 個數據點" << std::endl;
    std::cout << "  代理歷史: " << agentHistory.size() << " 個數據點" << std::endl;
}

void StatisticsPanel::RenderAgentStatistics() {
    std::cout << "\n🤖 代理統計:" << std::endl;
    
    if (agentSystem && agentSystem->GetAgentManager()) {
        auto manager = agentSystem->GetAgentManager();
        const auto& agents = manager->GetAgents();
        
        std::cout << "  總代理數: " << agents.size() << std::endl;
        
        for (const auto& agent : agents) {
            std::cout << "  " << agent->GetName() << ": ";
            std::cout << "完成 " << agent->GetCompletedTaskCount() << " 任務, ";
            std::cout << "失敗 " << agent->GetFailedTaskCount() << " 任務, ";
            std::cout << "成功率 " << (agent->GetSuccessRate() * 100.0f) << "%" << std::endl;
        }
    }
}

void StatisticsPanel::RenderTaskStatistics() {
    std::cout << "\n📝 任務統計:" << std::endl;
    
    if (agentSystem && agentSystem->GetAgentManager()) {
        auto manager = agentSystem->GetAgentManager();
        
        std::cout << "  待處理任務: " << manager->GetPendingTaskCount() << std::endl;
        std::cout << "  已完成任務: " << manager->GetCompletedTaskCount() << std::endl;
    }
}

// ============================================================================
// CodeEditorPanel 實現
// ============================================================================

CodeEditorPanel::CodeEditorPanel()
    : GUIPanel("代碼編輯器", PanelType::CodeEditor)
    , agentSystem(nullptr)
    , showLineNumbers(true)
    , autoIndent(true)
    , syntaxHighlighting(true) {
}

void CodeEditorPanel::Render() {
    std::cout << "\n📝 代碼編輯器面板" << std::endl;
    std::cout << "====================================" << std::endl;
    
    RenderToolbar();
    RenderCodeEditor();
    
    std::cout << "====================================" << std::endl;
}

void CodeEditorPanel::SetAgentSystem(AI::AIAgentSystem* system) {
    agentSystem = system;
}

void CodeEditorPanel::RenderToolbar() {
    std::cout << "\n🔧 工具欄:" << std::endl;
    std::cout << "  [n] 新建文件" << std::endl;
    std::cout << "  [s] 保存文件" << std::endl;
    std::cout << "  [g] AI 生成代碼" << std::endl;
    std::cout << "  [o] 優化代碼" << std::endl;
}

void CodeEditorPanel::RenderCodeEditor() {
    std::cout << "\n📄 代碼編輯器:" << std::endl;
    std::cout << "  文件: " << currentFile << std::endl;
    std::cout << "  行號: " << (showLineNumbers ? "啟用" : "禁用") << std::endl;
    std::cout << "  自動縮進: " << (autoIndent ? "啟用" : "禁用") << std::endl;
    std::cout << "  語法高亮: " << (syntaxHighlighting ? "啟用" : "禁用") << std::endl;
    
    if (!codeBuffer.empty()) {
        std::cout << "\n代碼內容:" << std::endl;
        std::cout << codeBuffer << std::endl;
    }
}

// ============================================================================
// AssetBrowserPanel 實現
// ============================================================================

AssetBrowserPanel::AssetBrowserPanel()
    : GUIPanel("資產瀏覽器", PanelType::AssetBrowser)
    , currentPath("/") {
}

void AssetBrowserPanel::Render() {
    std::cout << "\n📁 資產瀏覽器面板" << std::endl;
    std::cout << "====================================" << std::endl;
    
    RenderFileTree();
    RenderFilePreview();
    
    std::cout << "====================================" << std::endl;
}

void AssetBrowserPanel::RenderFileTree() {
    std::cout << "\n📂 文件樹:" << std::endl;
    std::cout << "  當前路徑: " << currentPath << std::endl;
    
    if (files.empty()) {
        std::cout << "  無文件" << std::endl;
    } else {
        for (const auto& file : files) {
            std::cout << "  " << file << std::endl;
        }
    }
}

void AssetBrowserPanel::RenderFilePreview() {
    std::cout << "\n👁️ 文件預覽:" << std::endl;
    std::cout << "  選擇文件以預覽" << std::endl;
}

// ============================================================================
// GUIManager 實現
// ============================================================================

GUIManager::GUIManager()
    : currentTheme(GUITheme::Dark)
    , agentSystem(nullptr)
    , initialized(false) {
}

GUIManager::~GUIManager() {
    Shutdown();
}

bool GUIManager::Initialize() {
    std::cout << "初始化 GUI 管理器..." << std::endl;
    
    CreateDefaultPanels();
    ApplyThemeColors();
    
    initialized = true;
    std::cout << "✓ GUI 管理器初始化成功" << std::endl;
    
    return true;
}

void GUIManager::Shutdown() {
    if (!initialized) {
        return;
    }
    
    std::cout << "關閉 GUI 管理器..." << std::endl;
    
    for (auto panel : panels) {
        delete panel;
    }
    panels.clear();
    panelMap.clear();
    
    initialized = false;
    std::cout << "✓ GUI 管理器關閉完成" << std::endl;
}

void GUIManager::Update(float deltaTime) {
    for (auto panel : panels) {
        if (panel->IsVisible()) {
            panel->Update(deltaTime);
        }
    }
}

void GUIManager::Render() {
    std::cout << "\n🎨 GUI 渲染 - 主題: " << static_cast<int>(currentTheme) << std::endl;
    std::cout << "====================================" << std::endl;
    
    for (auto panel : panels) {
        if (panel->IsVisible()) {
            panel->Render();
        }
    }
    
    std::cout << "====================================" << std::endl;
}

void GUIManager::AddPanel(GUIPanel* panel) {
    if (panel) {
        panels.push_back(panel);
        panelMap[panel->GetName()] = panel;
        
        std::cout << "✓ 添加面板: " << panel->GetName() << std::endl;
    }
}

void GUIManager::RemovePanel(const std::string& name) {
    auto it = panelMap.find(name);
    if (it != panelMap.end()) {
        GUIPanel* panel = it->second;
        
        panels.erase(std::remove(panels.begin(), panels.end(), panel), panels.end());
        panelMap.erase(it);
        
        delete panel;
        
        std::cout << "✓ 移除面板: " << name << std::endl;
    }
}

GUIPanel* GUIManager::GetPanel(const std::string& name) {
    auto it = panelMap.find(name);
    if (it != panelMap.end()) {
        return it->second;
    }
    return nullptr;
}

void GUIManager::ShowPanel(const std::string& name) {
    auto panel = GetPanel(name);
    if (panel) {
        panel->SetVisible(true);
    }
}

void GUIManager::HidePanel(const std::string& name) {
    auto panel = GetPanel(name);
    if (panel) {
        panel->SetVisible(false);
    }
}

void GUIManager::SetTheme(GUITheme theme) {
    currentTheme = theme;
    ApplyThemeColors();
    
    std::cout << "設置主題: " << static_cast<int>(theme) << std::endl;
}

void GUIManager::SetAgentSystem(AI::AIAgentSystem* system) {
    agentSystem = system;
    
    // 將 AI 系統傳遞給所有面板
    for (auto panel : panels) {
        if (auto dashboard = dynamic_cast<DashboardPanel*>(panel)) {
            dashboard->SetAgentSystem(system);
        } else if (auto agentManager = dynamic_cast<AgentManagerPanel*>(panel)) {
            agentManager->SetAgentSystem(system);
        } else if (auto taskViewer = dynamic_cast<TaskViewerPanel*>(panel)) {
            taskViewer->SetAgentSystem(system);
        } else if (auto settings = dynamic_cast<SettingsPanel*>(panel)) {
            settings->SetAgentSystem(system);
        } else if (auto statistics = dynamic_cast<StatisticsPanel*>(panel)) {
            statistics->SetAgentSystem(system);
        } else if (auto codeEditor = dynamic_cast<CodeEditorPanel*>(panel)) {
            codeEditor->SetAgentSystem(system);
        }
    }
}

void GUIManager::LoadLayout(const std::string& layoutFile) {
    std::cout << "加載佈局: " << layoutFile << std::endl;
    // TODO: 實現佈局加載
}

void GUIManager::SaveLayout(const std::string& layoutFile) {
    std::cout << "保存佈局: " << layoutFile << std::endl;
    // TODO: 實現佈局保存
}

void GUIManager::CreateDefaultPanels() {
    AddPanel(new DashboardPanel());
    AddPanel(new AgentManagerPanel());
    AddPanel(new TaskViewerPanel());
    AddPanel(new ConsolePanel());
    AddPanel(new SettingsPanel());
    AddPanel(new StatisticsPanel());
    AddPanel(new CodeEditorPanel());
    AddPanel(new AssetBrowserPanel());
}

void GUIManager::ApplyThemeColors() {
    std::cout << "應用主題顏色: " << static_cast<int>(currentTheme) << std::endl;
    // TODO: 實現主題顏色應用
}

// ============================================================================
// AgentGUISystem 實現
// ============================================================================

AgentGUISystem::AgentGUISystem()
    : guiManager(new GUIManager())
    , agentSystem(nullptr)
    , initialized(false)
    , glfwWindow(nullptr) {
}

AgentGUISystem::~AgentGUISystem() {
    Shutdown();
}

bool AgentGUISystem::Initialize() {
    std::cout << "初始化 Agent GUI 系統..." << std::endl;
    
    if (!guiManager->Initialize()) {
        std::cerr << "錯誤: GUI 管理器初始化失敗" << std::endl;
        return false;
    }
    
    if (agentSystem) {
        guiManager->SetAgentSystem(agentSystem);
    }
    
    initialized = true;
    std::cout << "✓ Agent GUI 系統初始化成功" << std::endl;
    
    return true;
}

void AgentGUISystem::Shutdown() {
    if (!initialized) {
        return;
    }
    
    std::cout << "關閉 Agent GUI 系統..." << std::endl;
    
    if (guiManager) {
        guiManager->Shutdown();
    }
    
    initialized = false;
    std::cout << "✓ Agent GUI 系統關閉完成" << std::endl;
}

void AgentGUISystem::Update(float deltaTime) {
    if (!initialized) {
        return;
    }
    
    if (guiManager) {
        guiManager->Update(deltaTime);
    }
}

void AgentGUISystem::Render() {
    if (!initialized) {
        return;
    }
    
    if (guiManager) {
        guiManager->Render();
    }
}

void AgentGUISystem::SetAgentSystem(AI::AIAgentSystem* system) {
    agentSystem = system;
    
    if (guiManager) {
        guiManager->SetAgentSystem(system);
    }
}

void AgentGUISystem::SetTheme(GUITheme theme) {
    if (guiManager) {
        guiManager->SetTheme(theme);
    }
}

} // namespace GUI
} // namespace Potato