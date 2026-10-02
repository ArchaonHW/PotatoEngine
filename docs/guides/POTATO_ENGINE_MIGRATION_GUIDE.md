# 🥔 Potato Engine - UE5 移除遷移指南

## 📋 概述

本文檔說明如何從 Unreal Engine 5 版本的 Potato Engine 遷移到完全獨立的 Potato Engine。

---

## 🎯 遷移目標

### 已完成的工作

1. **✅ 完全獨立的架構設計**
   - 設計了完整的獨立引擎架構
   - 文檔位於: `ARCHITECTURE.md`

2. **✅ 核心系統實現**
   - 引擎核心 (`PotatoEngine`)
   - 渲染系統 (`PotatoRenderer`, `PotatoOpenGL`)
   - 物理系統 (`PotatoPhysics`)
   - 音訊系統 (`PotatoAudio`)
   - 輸入系統 (`PotatoInput`)
   - GUI 系統 (`PotatoGUI`)
   - 資源管理系統 (`PotatoResourceManager`)
   - AI 系統 (`PotatoAI`)

3. **✅ 建置系統**
   - CMake 建置配置
   - 獨立於 Unreal Build Tool
   - 跨平台支持

4. **✅ 示例程序**
   - 簡單示例 (`SimpleExample.cpp`)
   - 編輯器示例 (`EditorExample.cpp`)

5. **✅ 文檔**
   - 完整的 README
   - 架構設計文檔
   - 使用指南

---

## 🔄 主要變更

### 1. 移除的 UE5 依賴

#### 移除的 UE5 型別
- ❌ `UObject`, `UActorComponent`
- ❌ `UCLASS`, `USTRUCT`, `UFUNCTION`, `UPROPERTY`
- ❌ `FString`, `TArray`, `TMap`, `TObjectPtr`
- ❌ `FDateTime`, `FVector`, `FRotator`
- ❌ `UE_LOG`, `BeginPlay`, `EndPlay`, `Tick`
- ❌ Unreal Header Tool
- ❌ `.generated.h` 文件

#### 替換為標準 C++
- ✅ `std::string` 替代 `FString`
- ✅ `std::vector` 替代 `TArray`
- ✅ `std::unordered_map` 替代 `TMap`
- ✅ `glm::vec3` 替代 `FVector`
- ✅ `std::chrono` 替代 `FDateTime`
- ✅ 標準 C++ 類替代 `UObject`

### 2. 新建的系統

#### 核心引擎
```cpp
// 新的獨立引擎核心
PotatoEngine& engine = PotatoEngine::GetInstance();
engine.Initialize(config);
engine.RunMainLoop();
engine.Shutdown();
```

#### 渲染系統
```cpp
// 多圖形 API 支持
PotatoRenderer* renderer = new PotatoOpenGL();
renderer->Initialize(config);
renderer->BeginFrame();
renderer->Present();
```

#### 物理系統
```cpp
// 獨立物理引擎
PotatoPhysics* physics = new PotatoPhysics();
physics->Initialize(config);
RigidBody* body = physics->CreateRigidBody(desc);
```

#### AI 系統
```cpp
// 原 AI Agent 能力保留
PotatoAI* ai = new PotatoAI();
ai->Initialize(config);
Agent* agent = ai->CreateAgent(desc);
```

---

## 📁 檔案結構對比

### 舊結構 (UE5 依賴)
```
MingGoRTS/
├── Source/MingGoRTS/
│   ├── Public/PotatoEngine/
│   │   ├── UPotatoAgentCore.h      // UE5 UObject
│   │   ├── UPotatoEditorInterface.h // UE5 UObject
│   │   └── ...
│   └── Private/PotatoEngine/
│       ├── UPotatoAgentCore.cpp
│       └── ...
├── Plugins/
│   ├── MingAI/MingAI.uplugin       // UE5 Plugin
│   └── ...
├── MingGoRTS.uproject              // UE5 Project
└── BuildPotatoEngine.bat           // UE5 Build Tool
```

### 新結構 (完全獨立)
```
PotatoEngine/
├── include/
│   ├── Core/
│   │   └── PotatoEngine.h          // 純 C++ 類
│   ├── Rendering/
│   │   └── PotatoRenderer.h         // 純 C++ 類
│   ├── Physics/
│   │   └── PotatoPhysics.h         // 純 C++ 類
│   ├── Audio/
│   │   └── PotatoAudio.h           // 純 C++ 類
│   ├── Input/
│   │   └── PotatoInput.h           // 純 C++ 類
│   ├── GUI/
│   │   └── PotatoGUI.h             // 純 C++ 類
│   ├── Resources/
│   │   └── PotatoResourceManager.h  // 純 C++ 類
│   └── AI/
│       └── PotatoAI.h              // 純 C++ 類
├── src/
│   ├── Core/
│   │   └── PotatoEngine.cpp
│   ├── Rendering/
│   │   └── PotatoRenderer.cpp
│   └── ...
├── examples/
│   ├── SimpleExample.cpp
│   └── EditorExample.cpp
├── CMakeLists.txt                   // CMake 建置
└── README.md
```

---

## 🛠️ 遷移步驟

### 步驟 1: 環境準備

1. **安裝 CMake**
   ```bash
   # Windows
   choco install cmake
   
   # Linux
   sudo apt-get install cmake
   
   # macOS
   brew install cmake
   ```

2. **安裝依賴庫**
   ```bash
   # 使用 vcpkg (Windows)
   vcpkg install glfw3 glad opengl imgui
   
   # 使用 apt (Linux)
   sudo apt-get install libglfw3-dev libgl1-mesa-dev
   ```

### 步驟 2: 建置新引擎

```bash
cd PotatoEngine
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

### 步驟 3: 代碼遷移

#### 遷移 UPotatoAgentCore

**舊代碼 (UE5):**
```cpp
// UPotatoAgentCore.h
UCLASS()
class UPotatoAgentCore : public UObject {
    GENERATED_BODY()
    
public:
    UFUNCTION(BlueprintCallable)
    FString CreateAgent(EPotatoAgentType Type, const FString& Name);
    
    UPROPERTY(EditAnywhere)
    TArray<FPotatoAgent> Agents;
};
```

**新代碼 (獨立):**
```cpp
// PotatoAI.h
class PotatoAI {
public:
    Agent* CreateAgent(const AgentDesc& desc);
    
private:
    std::vector<Agent*> m_agents;
};
```

#### 遷移 UPotatoEditorInterface

**舊代碼 (UE5):**
```cpp
// UPotatoEditorInterface.h
UCLASS()
class UPotatoEditorInterface : public UUserWidget {
    GENERATED_BODY()
    
public:
    UFUNCTION(BlueprintCallable)
    void ShowDashboard();
};
```

**新代碼 (獨立):**
```cpp
// PotatoGUI.h
class PotatoGUI {
public:
    bool Begin(const std::string& name);
    void End();
    
    void Text(const std::string& text);
    bool Button(const std::string& label);
};
```

### 步驟 4: 資源遷移

1. **複製資產文件**
   ```bash
   cp -r MingGoRTS/Content PotatoEngine/assets/
   ```

2. **轉換資產格式**
   - UE5 資產 (.uasset) 需要轉換為標準格式
   - 紋理: .png, .jpg
   - 模型: .obj, .gltf
   - 音訊: .wav, .ogg

### 步驟 5: 測試

```bash
# 運行簡單示例
cd bin
./PotatoExample

# 運行編輯器示例
./PotatoEditor
```

---

## ⚠️ 注意事項

### 1. API 差異

| UE5 API | 獨立 API | 說明 |
|---------|----------|------|
| `UFUNCTION` | 普通函數 | 移除 Blueprint 整合 |
| `UPROPERTY` | 普通成員 | 移除反射系統 |
| `FString` | `std::string` | 使用標準字串 |
| `TArray` | `std::vector` | 使用標準容器 |
| `TMap` | `std::unordered_map` | 使用標準映射 |
| `UE_LOG` | `std::cout` | 使用標準輸出 |

### 2. 生命週期差異

| UE5 | 獨立引擎 |
|-----|----------|
| `BeginPlay()` | 構造函數/初始化 |
| `EndPlay()` | 析構函數/關閉 |
| `Tick()` | 更新回調 |
| `GetWorld()` | 引擎實例 |

### 3. 未實現的功能

以下功能需要在獨立版本中重新實現：

- ❌ Blueprint 可視化腳本
- ❌ UE 編輯器集成
- ❌ Unreal 資產系統
- ❌ C++ 反射系統
- ❌ Garbage Collection (使用智能指針替代)

---

## 🎯 當前狀態

### ✅ 已完成

- [x] 架構設計
- [x] 核心引擎框架
- [x] OpenGL 渲染器
- [x] 物理系統框架
- [x] 音訊系統框架
- [x] 輸入系統
- [x] GUI 系統
- [x] 資源管理系統
- [x] AI 系統
- [x] CMake 建置系統
- [x] 示例程序
- [x] 文檔

### 🚧 開發中

- [ ] DirectX 渲染器
- [ ] Vulkan 渲染器
- [ ] Bullet Physics 完整整合
- [ ] OpenAL 完整整合
- [ ] 模型加載器
- [ ] 動畫系統
- [ ] 粒子系統

### 📋 規劃中

- [ ] 編輯器應用
- [ ] 性能分析器
- [ ] 調試器
- [ ] 網絡支持
- [ ] 移動平台支持

---

## 🚀 使用建議

### 對於新項目

直接使用新的獨立 Potato Engine：

```bash
cd PotatoEngine
mkdir build && cd build
cmake ..
cmake --build .
./bin/PotatoExample
```

### 對於現有 UE5 項目

1. 評估遷移成本
2. 選擇關鍵模組優先遷移
3. 逐步替換 UE5 依賴
4. 保持兩個版本並行開發
5. 完成遷移後移除 UE5

---

## 📞 支持資源

- **架構文檔**: `ARCHITECTURE.md`
- **README**: `README.md`
- **示例代碼**: `examples/`
- **測試**: `tests/`

---

## 🎓 學習資源

### 圖形編程
- Learn OpenGL: https://learnopengl.com
- Vulkan Tutorial: https://vulkan-tutorial.com

### 物理編程
- Bullet Physics: https://pybullet.org
- Game Physics Engine Development (書籍)

### 音訊編程
- OpenAL Programming Guide
- Game Audio Programming (書籍)

---

**🥔 Potato Engine - 完全獨立版本**

*遷移指南版本: 1.0.0*  
*最後更新: 2024*