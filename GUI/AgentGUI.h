#pragma once

#include "Core/CoreTypes.h"
#include "AI/AIAgentSystem.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace Potato {
namespace GUI {

/**
 * GUI 主題
 */
enum class GUITheme {
    Dark,
    Light,
    Potato,
    Custom
};

/**
 * GUI 面板類型
 */
enum class PanelType {
    Dashboard,      // 儀表板
    AgentManager,   // 代理管理器
    TaskViewer,     // 任務查看器
    Console,        // 控制台
    Settings,       // 設置
    Statistics,     // 統計
    CodeEditor,     // 代碼編輯器
    AssetBrowser   // 資產瀏覽器
};

/**
 * GUI 面板基類
 */
class GUIPanel {
public:
    GUIPanel(const std::string& name, PanelType type);
    virtual ~GUIPanel();
    
    // 渲染
    virtual void Render() = 0;
    
    // 更新
    virtual void Update(float deltaTime);
    
    // 狀態
    bool IsVisible() const { return visible; }
    void SetVisible(bool isVisible) { visible = isVisible; }
    
    const std::string& GetName() const { return name; }
    PanelType GetType() const { return type; }
    
    // 回調
    void SetUpdateCallback(std::function<void()> callback) {
        updateCallback = callback;
    }
    
protected:
    std::string name;
    PanelType type;
    bool visible;
    std::function<void()> updateCallback;
};

/**
 * 儀表板面板
 * 顯示系統狀態和代理活動
 */
class DashboardPanel : public GUIPanel {
public:
    DashboardPanel();
    
    void Render() override;
    void SetAgentSystem(AI::AIAgentSystem* system);
    
private:
    AI::AIAgentSystem* agentSystem;
    
    // 統計數據
    size_t totalAgents;
    size_t activeAgents;
    size_t pendingTasks;
    size_t completedTasks;
    float successRate;
};

/**
 * 代理管理器面板
 * 管理和監控 AI 代理
 */
class AgentManagerPanel : public GUIPanel {
public:
    AgentManagerPanel();
    
    void Render() override;
    void SetAgentSystem(AI::AIAgentSystem* system);
    
private:
    AI::AIAgentSystem* agentSystem;
    
    // 選中的代理
    AI::AIAgent* selectedAgent;
    
    // 代理列表視圖
    bool showAgentList;
    bool showAgentDetails;
    bool showTaskList;
    
    // 創建代理對話框
    bool showCreateAgentDialog;
    std::string newAgentName;
    AI::AgentType newAgentType;
    
    void RenderAgentList();
    void RenderAgentDetails();
    void RenderTaskList();
    void RenderCreateAgentDialog();
};

/**
 * 任務查看器面板
 * 查看和管理任務
 */
class TaskViewerPanel : public GUIPanel {
public:
    TaskViewerPanel();
    
    void Render() override;
    void SetAgentSystem(AI::AIAgentSystem* system);
    
private:
    AI::AIAgentSystem* agentSystem;
    
    // 任務列表
    std::vector<AI::AgentTask> allTasks;
    
    // 過濾器
    std::string filterCategory;
    AI::TaskStatus filterStatus;
    
    void RefreshTaskList();
    void RenderTaskList();
    void RenderTaskDetails(const AI::AgentTask& task);
};

/**
 * 控制台面板
 * 顯示日誌和執行命令
 */
class ConsolePanel : public GUIPanel {
public:
    ConsolePanel();
    
    void Render() override;
    
    // 日誌
    void AddLog(const std::string& message);
    void ClearLog();
    
private:
    std::vector<std::string> logMessages;
    std::string inputBuffer;
    
    void RenderLogMessages();
    void RenderInputField();
};

/**
 * 設置面板
 * 配置系統設置
 */
class SettingsPanel : public GUIPanel {
public:
    SettingsPanel();
    
    void Render() override;
    void SetAgentSystem(AI::AIAgentSystem* system);
    
private:
    AI::AIAgentSystem* agentSystem;
    
    // 設置項
    size_t maxAgents;
    bool learningEnabled;
    float updateInterval;
    GUITheme currentTheme;
    
    void ApplySettings();
};

/**
 * 統計面板
 * 顯示詳細統計信息
 */
class StatisticsPanel : public GUIPanel {
public:
    StatisticsPanel();
    
    void Render() override;
    void SetAgentSystem(AI::AIAgentSystem* system);
    
private:
    AI::AIAgentSystem* agentSystem;
    
    // 性能數據
    std::vector<float> fpsHistory;
    std::vector<float> taskHistory;
    std::vector<float> agentHistory;
    
    void RenderPerformanceCharts();
    void RenderAgentStatistics();
    void RenderTaskStatistics();
};

/**
 * 代碼編輯器面板
 * 編輯和生成代碼
 */
class CodeEditorPanel : public GUIPanel {
public:
    CodeEditorPanel();
    
    void Render() override;
    void SetAgentSystem(AI::AIAgentSystem* system);
    
private:
    AI::AIAgentSystem* agentSystem;
    
    // 代碼編輯器
    std::string codeBuffer;
    std::string currentFile;
    
    // 編輯器設置
    bool showLineNumbers;
    bool autoIndent;
    bool syntaxHighlighting;
    
    void RenderCodeEditor();
    void RenderToolbar();
};

/**
 * 資產瀏覽器面板
 * 瀏覽和管理資產
 */
class AssetBrowserPanel : public GUIPanel {
public:
    AssetBrowserPanel();
    
    void Render() override;
    
private:
    std::string currentPath;
    std::vector<std::string> files;
    
    void RenderFileTree();
    void RenderFilePreview();
};

/**
 * GUI 管理器
 * 管理所有 GUI 面板和主題
 */
class GUIManager {
public:
    GUIManager();
    ~GUIManager();
    
    // 初始化
    bool Initialize();
    void Shutdown();
    
    // 主循環
    void Update(float deltaTime);
    void Render();
    
    // 面板管理
    void AddPanel(GUIPanel* panel);
    void RemovePanel(const std::string& name);
    GUIPanel* GetPanel(const std::string& name);
    void ShowPanel(const std::string& name);
    void HidePanel(const std::string& name);
    
    // 主題
    void SetTheme(GUITheme theme);
    GUITheme GetTheme() const { return currentTheme; }
    
    // AI 系統
    void SetAgentSystem(AI::AIAgentSystem* system);
    
    // 佈局
    void LoadLayout(const std::string& layoutFile);
    void SaveLayout(const std::string& layoutFile);
    
    // 狀態
    bool IsInitialized() const { return initialized; }
    
private:
    std::vector<GUIPanel*> panels;
    std::unordered_map<std::string, GUIPanel*> panelMap;
    
    GUITheme currentTheme;
    AI::AIAgentSystem* agentSystem;
    
    bool initialized;
    
    void CreateDefaultPanels();
    void ApplyThemeColors();
};

/**
 * Agent GUI 系統
 * 完整的 AI 代理圖形介面系統
 */
class AgentGUISystem {
public:
    AgentGUISystem();
    ~AgentGUISystem();
    
    // 初始化
    bool Initialize();
    void Shutdown();
    
    // 主循環
    void Update(float deltaTime);
    void Render();
    
    // 系統
    GUIManager* GetGUIManager() { return guiManager.get(); }
    
    // AI 系統
    void SetAgentSystem(AI::AIAgentSystem* system);
    
    // 主題
    void SetTheme(GUITheme theme);
    
    // 狀態
    bool IsInitialized() const { return initialized; }
    
private:
    std::unique_ptr<GUIManager> guiManager;
    AI::AIAgentSystem* agentSystem;
    
    bool initialized;
    
    // 框架
    void* glfwWindow;
};

} // namespace GUI
} // namespace Potato