# 🥔 Potato Engine

**完全獨立的 C++ 遊戲引擎與 AI Agent 平台**

Potato Engine 是一個完全獨立的、現代化的 C++ 遊戲引擎，具備原生的 AI Agent 能力。**此版本已完全解除 Unreal Engine 5 依賴**，可以獨立運行和開發。

---

## ✨ 主要特色

### 🎮 完全獨立
- **不依賴 UE5**: 完全移除 Unreal Engine 5 依賴
- **純 C++ 實現**: 使用現代 C++17/20 標準
- **自包含建置**: 使用 CMake 建置系統
- **跨平台**: 支持 Windows、Linux、macOS

### 🤖 AI Agent 系統
- **多代理架構**: 支持多種 AI 代理類型
- **任務分配**: 智能任務分配和執行
- **決策制定**: 基於上下文的 AI 決策
- **學習系統**: 支持機器學習和適應

### 🎨 高性能渲染
- **多圖形 API**: 支持 OpenGL、DirectX、Vulkan
- **現代渲染管線**: PBR、光線追蹤、DLSS 支持
- **優化算法**: GPU 加速、多線程渲染
- **可擴展**: 易於添加新的渲染特性

### ⚙️ 完整的遊戲引擎功能
- **物理引擎**: 剛體模擬、碰撞檢測
- **音訊系統**: 3D 音訊、空間音訊
- **輸入系統**: 鍵盤、鼠標、手柄、觸控
- **資源管理**: 紋理、模型、著色器、音效
- **GUI 系統**: Dear ImGui 整合

---

## 📁 項目結構

```
PotatoEngine/
├── include/               # 頭文件
│   ├── Core/             # 核心系統
│   ├── Rendering/        # 渲染系統
│   ├── Physics/          # 物理系統
│   ├── Audio/            # 音訊系統
│   ├── Input/            # 輸入系統
│   ├── GUI/              # GUI 系統
│   ├── Resources/        # 資源管理
│   └── AI/               # AI 系統
├── src/                  # 實現文件
│   ├── Core/
│   ├── Rendering/
│   ├── Physics/
│   ├── Audio/
│   ├── Input/
│   ├── GUI/
│   ├── Resources/
│   └── AI/
├── external/             # 第三方庫
├── assets/               # 資產文件
├── examples/             # 示例程序
├── tests/                # 測試
├── docs/                 # 文檔
├── CMakeLists.txt        # CMake 建置文件
└── README.md             # 項目說明
```

---

## 🚀 快速開始

### 環境要求

- **編譯器**: 
  - Windows: MSVC 2019+ 或 MinGW
  - Linux: GCC 7+ 或 Clang 5+
  - macOS: Clang 5+
- **CMake**: 3.15 或更高版本
- **依賴庫**:
  - OpenGL 3.3+
  - GLFW 3.3+
  - GLAD
  - Dear ImGui
  - Bullet Physics (可選)
  - OpenAL (可選)

### 安裝依賴

#### Windows (使用 vcpkg)
```bash
vcpkg install glfw3 glad opengl glad openal-soft bullet3 imgui
```

#### Linux (Ubuntu/Debian)
```bash
sudo apt-get install build-essential cmake
sudo apt-get install libglfw3-dev libgl1-mesa-dev
sudo apt-get install libopenal-dev libbullet-dev
```

#### macOS (使用 Homebrew)
```bash
brew install cmake glfw openal-soft bullet
```

### 建置步驟

```bash
# 克隆項目
git clone https://github.com/PotatoEngine/PotatoEngine.git
cd PotatoEngine

# 創建建置目錄
mkdir build && cd build

# 配置 CMake
cmake ..

# 建置
cmake --build . --config Release

# 運行示例
cd bin
./PotatoExample
```

### Windows 建置

```bash
# 使用 Visual Studio
cmake -G "Visual Studio 16 2019" ..
cmake --build . --config Release

# 或使用 MinGW
cmake -G "MinGW Makefiles" ..
cmake --build .
```

---

## 💻 使用示例

### 基本初始化

```cpp
#include "Core/PotatoEngine.h"

using namespace Potato;

int main() {
    // 獲取引擎實例
    PotatoEngine& engine = PotatoEngine::GetInstance();
    
    // 配置引擎
    EngineConfig config;
    config.windowWidth = 1920;
    config.windowHeight = 1080;
    config.windowTitle = "My Potato Game";
    config.enablePhysics = true;
    config.enableAudio = true;
    config.enableGUI = true;
    config.enableAI = true;
    
    // 初始化引擎
    if (!engine.Initialize(config)) {
        return -1;
    }
    
    // 設置更新回調
    engine.SetUpdateCallback([](float deltaTime) {
        // 遊戲邏輯更新
    });
    
    // 運行主循環
    engine.RunMainLoop();
    
    // 關閉引擎
    engine.Shutdown();
    
    return 0;
}
```

### 使用 AI Agent

```cpp
#include "AI/PotatoAI.h"

// 創建 AI 代理
AgentDesc desc;
desc.name = "Developer";
desc.type = AgentType::Development;
desc.autonomous = true;

Agent* agent = engine.GetAI()->CreateAgent(desc);

// 分配任務
AgentTask task;
task.id = "task_001";
task.description = "Generate player controller";
task.priority = TaskPriority::High;

agent->AssignTask(task);

// 更新代理
agent->Update(deltaTime);
```

### 使用資源管理器

```cpp
#include "Resources/PotatoResourceManager.h"

// 加載紋理
Texture* texture = engine.GetResourceManager()->LoadTexture("textures/player.png");

// 加載著色器
Shader* shader = engine.GetResourceManager()->LoadShader(
    "shaders/vertex.glsl",
    "shaders/fragment.glsl"
);

// 使用著色器
shader->Bind();
shader->SetUniformMat4("modelMatrix", modelMatrix);
shader->SetUniformVec3("lightColor", glm::vec3(1.0f, 1.0f, 1.0f));
```

---

## 📚 核心系統

### 渲染系統
- **PotatoRenderer**: 渲染器接口
- **PotatoOpenGL**: OpenGL 實現
- **PotatoDirectX**: DirectX 實現 (開發中)
- **PotatoVulkan**: Vulkan 實現 (開發中)

### 物理系統
- **PotatoPhysics**: 物理引擎核心
- **RigidBody**: 剛體模擬
- **CollisionShape**: 碰撞形狀
- **支持**: 盒、球、膠囊、圓柱

### 音訊系統
- **PotatoAudio**: 音訊引擎
- **Sound**: 音效播放
- **Music**: 音樂播放
- **3D 音訊**: 空間音訊支持

### 輸入系統
- **PotatoInput**: 輸入管理
- **鍵盤**: 完整鍵盤支持
- **鼠標**: 按鈕、位置、滾動
- **手柄**: 多手柄支持

### GUI 系統
- **PotatoGUI**: GUI 核心
- **Dear ImGui**: 即時 GUI
- **控件**: 按鈕、滑塊、輸入框等
- **主題**: 可自訂主題

### 資源管理
- **PotatoResourceManager**: 資源管理器
- **Texture**: 紋理管理
- **Model**: 模型管理
- **Shader**: 著色器管理
- **引用計數**: 自動資源管理

### AI 系統
- **PotatoAI**: AI 引擎
- **Agent**: AI 代理
- **任務系統**: 任務分配和執行
- **決策系統**: AI 決策制定
- **學習系統**: 機器學習支持

---

## 🔧 配置選項

### 引擎配置
```cpp
struct EngineConfig {
    int windowWidth = 1920;
    int windowHeight = 1080;
    std::string windowTitle = "Potato Engine";
    bool enableVSync = true;
    bool enableFullscreen = false;
    int targetFPS = 60;
    bool enablePhysics = true;
    bool enableAudio = true;
    bool enableGUI = true;
    bool enableAI = true;
    std::string assetPath = "./assets";
};
```

### 渲染配置
```cpp
struct RenderConfig {
    RendererType type = RendererType::OpenGL;
    int windowWidth = 1920;
    int windowHeight = 1080;
    bool enableVSync = true;
    glm::vec4 clearColor = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
};
```

### 物理配置
```cpp
struct PhysicsConfig {
    glm::vec3 gravity = glm::vec3(0.0f, -9.8f, 0.0f);
    int maxSolverIterations = 10;
    float fixedTimeStep = 1.0f / 60.0f;
    bool enableDebugDraw = false;
};
```

---

## 📊 性能目標

- **啟動時間**: <5 秒
- **內存使用**: <500MB (基礎系統)
- **FPS**: 60+ FPS (中等場景)
- **加載時間**: <2 秒 (小型場景)
- **代理響應**: <50ms

---

## 🛠️ 開發狀態

### 已完成 ✅
- [x] 核心引擎框架
- [x] OpenGL 渲染器
- [x] 物理系統框架
- [x] 音訊系統框架
- [x] 輸入系統
- [x] GUI 系統
- [x] 資源管理系統
- [x] AI Agent 系統
- [x] CMake 建置系統

### 開發中 🚧
- [ ] DirectX 渲染器
- [ ] Vulkan 渲染器
- [ ] Bullet Physics 完整整合
- [ ] OpenAL 完整整合
- [ ] 模型加載器
- [ ] 動畫系統
- [ ] 粒子系統
- [ ] 編輯器應用

### 規劃中 📋
- [ ] 網絡多人遊戲
- [ ] 物理材質系統
- [ ] 高級著色器
- [ ] 性能分析器
- [ ] 調試器
- [ ] 移動平台支持

---

## 🤝 貢獻

歡迎貢獻！請遵循以下步驟：

1. Fork 項目
2. 創建特性分支 (`git checkout -b feature/AmazingFeature`)
3. 提交更改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 開啟 Pull Request

---

## 📄 許可證

此項目採用 MIT 許可證 - 詳見 [LICENSE](LICENSE) 文件

---

## 📞 聯繫方式

- **項目主頁**: https://github.com/PotatoEngine/PotatoEngine
- **問題報告**: https://github.com/PotatoEngine/PotatoEngine/issues
- **文檔**: https://docs.potatoengine.com

---

## 🙏 致謝

感謝以下開源項目：

- [GLFW](https://www.glfw.org/) - 窗口和輸入管理
- [GLAD](https://github.com/Dav1dde/glad) - OpenGL 加載器
- [Dear ImGui](https://github.com/ocornut/imgui) - 即時 GUI
- [Bullet Physics](https://pybullet.org/) - 物理引擎
- [OpenAL](https://www.openal.org/) - 音訊 API
- [stb](https://github.com/nothings/stb) - 圖像加載

---

## 🎯 未來路線

### 短期目標 (3-6 個月)
- 完成 DirectX 和 Vulkan 渲染器
- 完整整合 Bullet Physics
- 完整整合 OpenAL
- 添加模型加載器
- 實現編輯器應用

### 中期目標 (6-12 個月)
- 實現動畫系統
- 實現粒子系統
- 添加網絡支持
- 實現性能分析器
- 添加移動平台支持

### 長期目標 (1-2 年)
- 完整的編輯器套件
- 可視化腳本系統
- 高級 AI 特性
- 雲端渲染支持
- VR/AR 支持

---

**🥔 Potato Engine - 完全獨立的遊戲引擎**

*版本: 1.0.0*  
*狀態: 活躍開發中*  
*完全解除 UE5 依賴*