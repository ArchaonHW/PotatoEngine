# AI Agent GUI 系統指南

## 🎯 系統概述

AI Agent GUI 系統是 Potato Engine 的核心功能，將強大的 AI 代理系統與直觀的圖形介面完美整合，為用戶提供完整的 AI 驅動開發體驗。

## 🏗️ 系統架構

### 核心組件

1. **AI Agent 系統** (`AI/AIAgentSystem.h/cpp`)
   - 代理管理：創建、管理、協調多個 AI 代理
   - 任務分配：智能任務分配和負載平衡
   - 決策引擎：單代理和群體決策
   - 學習系統：從執行結果中學習和優化

2. **GUI 系統** (`GUI/AgentGUI.h/cpp`)
   - 儀表板面板：系統狀態和代理活動監控
   - 代理管理器面板：代理創建、管理、監控
   - 任務查看器面板：任務狀態和進度查看
   - 控制台面板：日誌顯示和命令執行
   - 設置面板：系統配置和參數調整
   - 統計面板：性能數據和統計分析
   - 代碼編輯器面板：AI 代碼生成和編輯
   - 資產瀏覽器面板：資產管理和預覽

## 🤖 AI 代理類型

### 可用代理類型

1. **開發代理** (`DeveloperAgent`)
   - 代碼生成和優化
   - 調試和問題修復
   - 架構設計和實現

2. **設計代理** (`DesignerAgent`)
   - 創意和設計任務
   - UI/UX 設計
   - 創意頭腦風暴

3. **分析代理** (`AnalystAgent`)
   - 數據分析和優化
   - 性能監控
   - 洞察生成

4. **測試代理** (`TesterAgent`)
   - 自動化測試
   - 質量保證
   - Bug 檢測

5. **調試代理** (`DebuggerAgent`)
   - 問題診斷
   - 錯誤分析
   - 修復建議

6. **研究代理** (`ResearcherAgent`)
   - 技術研究
   - 创新探索
   - 最佳實踐調查

## 📊 GUI 面板功能

### 儀表板面板
- 實時系統狀態監控
- 代理活動統計
- 任務完成進度
- 性能指標顯示

### 代理管理器面板
- 創建和刪除代理
- 代理詳情查看
- 任務分配管理
- 代理性能監控

### 任務查看器面板
- 任務列表顯示
- 任務狀態過濾
- 任務詳情查看
- 任務進度追蹤

### 控制台面板
- 系統日誌顯示
- 命令執行
- 錯誤消息查看
- 日誌過濾和搜索

### 設置面板
- 系統參數配置
- 主題選擇
- 性能調整
- 學習系統開關

### 統計面板
- 性能圖表
- 代理統計
- 任務統計
- 趨勢分析

### 代碼編輯器面板
- AI 代碼生成
- 代碼優化
- 語法高亮
- 文件管理

### 資產瀏覽器面板
- 資產文件樹
- 資產預覽
- 資產管理
- AI 資產生成

## 🚀 使用方法

### 初始化系統

```cpp
#include "AI/AIAgentSystem.h"
#include "GUI/AgentGUI.h"

// 創建 AI 系統
auto aiSystem = std::make_unique<Potato::AI::AIAgentSystem>();
aiSystem->Initialize();

// 創建 GUI 系統
auto guiSystem = std::make_unique<Potato::GUI::AgentGUISystem>();
guiSystem->Initialize();

// 連接系統
guiSystem->SetAgentSystem(aiSystem.get());
```

### 創建代理

```cpp
auto manager = aiSystem->GetAgentManager();

// 創建開發代理
Potato::AI::AgentDesc devDesc;
devDesc.name = "CodeMaster";
devDesc.type = Potato::AI::AgentType::Developer;
devDesc.autonomous = true;
devDesc.performanceRating = 0.9f;
devDesc.learningRate = 0.15f;
devDesc.capabilities = {"Code Generation", "Code Optimization", "Debugging"};

Potato::AI::AIAgent* devAgent = manager->CreateAgent(devDesc);
```

### 分配任務

```cpp
// 創建任務
Potato::AI::AgentTask task;
task.id = "task_001";
task.description = "Generate player controller system";
task.category = "Code Generation";
task.priority = Potato::AI::TaskPriority::High;
task.estimatedTime = 10000;

// 分配給特定代理
manager->AssignTaskToAgent("CodeMaster", task);

// 或分配給最佳代理
manager->AssignTaskToBestAgent(task);

// 或分配給所有代理
manager->AssignTaskToAllAgents(task);
```

### 群體決策

```cpp
std::string context = "Select the best rendering backend for the game";
std::vector<std::string> options = {
    "OpenGL",
    "DirectX 11",
    "DirectX 12",
    "Vulkan"
};

Potato::AI::Decision decision = manager->MakeGroupDecision(context, options);

std::cout << "選擇: " << decision.action << std::endl;
std::cout << "信心: " << decision.confidence << std::endl;
```

### 主循環

```cpp
while (running) {
    float deltaTime = GetDeltaTime();
    
    // 更新 AI 系統
    aiSystem->Update(deltaTime);
    
    // 更新 GUI 系統
    guiSystem->Update(deltaTime);
    
    // 渲染 GUI
    guiSystem->Render();
    
    // 控制幀率
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
}
```

## 📁 文件結構

```
PotatoEngine/
├── AI/
│   ├── AIAgentSystem.h          # AI 系統頭文件
│   └── AIAgentSystem.cpp        # AI 系統實現
├── GUI/
│   ├── AgentGUI.h               # GUI 系統頭文件
│   └── AgentGUI.cpp             # GUI 系統實現
└── Examples/
    └── AIAgentGUIExample.cpp    # 整合示例程序
```

## 🔧 配置選項

### AI 系統配置

```cpp
// 設置最大代理數量
aiSystem->SetMaxAgents(100);

// 啟用學習系統
aiSystem->SetLearningEnabled(true);

// 設置更新間隔
aiSystem->GetAgentManager()->SetUpdateInterval(0.1f);
```

### GUI 配置

```cpp
// 設置主題
guiSystem->SetTheme(Potato::GUI::GUITheme::Dark);

// 顯示/隱藏面板
guiSystem->GetGUIManager()->ShowPanel("儀表板");
guiSystem->GetGUIManager()->HidePanel("控制台");

// 加載/保存佈局
guiSystem->GetGUIManager()->LoadLayout("layout.json");
guiSystem->GetGUIManager()->SaveLayout("layout.json");
```

## 📊 性能特性

- **高效任務分配**: 基於代理性能和負載的智能分配
- **並行處理**: 多代理並行執行任務
- **動態負載平衡**: 自動調整代理負載
- **異步日誌**: 不阻塞主循環的日誌系統
- **記存管理**: 智能記存分配和回收

## 🎓 學習系統

AI 代理具備學習能力，可以從執行結果中學習：

```cpp
// 記錄學習數據
agent->RecordLearning("Code Generation", "Generated player controller", true);

// 查看學習歷史
const auto& learningHistory = agent->GetLearningHistory();

// 更新性能評分
agent->UpdatePerformanceRating();
```

## 🔍 調試和監控

### 查看統計信息

```cpp
// 打印 AI 系統統計
aiSystem->PrintStatistics();

// 查看特定代理統計
auto agent = manager->GetAgent("CodeMaster");
std::cout << "完成任務: " << agent->GetCompletedTaskCount() << std::endl;
std::cout << "成功率: " << agent->GetSuccessRate() << std::endl;
```

### 控制台日誌

```cpp
auto consolePanel = guiSystem->GetGUIManager()->GetPanel("控制台");
if (auto console = dynamic_cast<Potato::GUI::ConsolePanel*>(consolePanel)) {
    console->AddLog("系統初始化完成");
    console->ClearLog();
}
```

## 🚧 限制和注意事項

1. **性能限制**: 大量代理可能影響性能，建議合理設置最大代理數量
2. **記存使用**: 學習歷史會佔用記存，定期清理或限制大小
3. **決策質量**: 群體決策依賴於代理數量和多樣性
4. **線程安全**: 某些操作需要加鎖，注意死鎖風險

## 🔮 未來發展

### 計劃功能

1. **真實 GUI 框架整合**: 整合 Dear ImGui 或其他 GUI 框架
2. **網絡支持**: 支持分布式代理和遠程控制
3. **持久化**: 代理狀態和學習數據的持久化存儲
4. **插件系統**: 支持自定義代理類型和 GUI 面板
5. **可視化**: 代理活動和數據流的可視化

## 📚 相關文檔

- [Potato Engine 完整指南](../POTATO_ENGINE_OVERVIEW.md)
- [架構設計](../architecture/POTATO_ENGINE_ARCHITECTURE.md)
- [文檔索引](../index.md)

## 🤝 貢獻

歡迎貢獻代碼、報告問題或提出建議！

## 📄 許可證

MIT License

---

**🥔 Potato Engine - AI Agent GUI 系統**
*強大的 AI 代理系統，直觀的圖形介面*