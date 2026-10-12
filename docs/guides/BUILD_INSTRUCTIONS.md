# Potato Engine 建置說明

## 🚀 快速建置指南

### Windows 建置

#### 方法 1: 使用建置腳本 (推薦)

1. **安裝必要工具**
   - 安裝 [Visual Studio 2022](https://visualstudio.microsoft.com/) (Community 版本免費)
   - 在安裝時選擇 "使用 C++ 的桌面開發" 工作負載
   - 安裝 [CMake](https://cmake.org/download/)

2. **運行建置腳本**
   ```batch
   # 打開 Visual Studio Developer Command Prompt
   # 運行:
   BuildEngine.bat
   ```

#### 方法 2: 手動建置

1. **安裝依賴**
   ```batch
   # 使用 vcpkg 安裝依賴
   vcpkg install glfw3 glad opengl openal-soft bullet3 imgui
   ```

2. **建置步驟**
   ```batch
   # 打開 Visual Studio Developer Command Prompt
   cd MingGoRTS
   mkdir build && cd build
   cmake .. -G "Visual Studio 17 2022" -A x64
   cmake --build . --config Release
   ```

3. **運行程序**
   ```batch
   cd bin\Release
   .\AIAgentGUIExample.exe
   ```

### Linux 建置

```bash
# 安裝依賴
sudo apt-get update
sudo apt-get install build-essential cmake
sudo apt-get install libglfw3-dev libgl1-mesa-dev
sudo apt-get install libopenal-dev libbullet-dev

# 建置
cd MingGoRTS
mkdir build && cd build
cmake ..
cmake --build . --config Release

# 運行
cd bin
./AIAgentGUIExample
```

### macOS 建置

```bash
# 安裝依賴
brew install cmake glfw openal-soft bullet

# 建置
cd MingGoRTS
mkdir build && cd build
cmake ..
cmake --build . --config Release

# 運行
cd bin
./AIAgentGUIExample
```

## 🔧 依賴庫說明

### 必要依賴
- **CMake 3.15+**: 建置系統
- **OpenGL 3.3+**: 圖形渲染
- **GLFW 3.3+**: 窗口和輸入管理
- **GLAD**: OpenGL 加載器 (通過 CMake 自動下載)

### 可選依賴
- **OpenAL**: 音訊系統 (通過 CMake 自動下載)
- **Bullet Physics**: 物理引擎 (可選)
- **Dear ImGui**: 即時 GUI (可選)

## 📦 輸出文件

建置成功後，可執行檔案將位於:
- Windows: `build/bin/Release/`
- Linux/macOS: `build/bin/`

### 主要可執行檔
- `SimpleExample.exe` - 簡單示例程序
- `PlatformTest.exe` - 平台系統測試
- `AIAgentGUIExample.exe` - AI Agent GUI 整合示例

## ⚠️ 常見問題

### CMake 找不到
- 下載並安裝 CMake: https://cmake.org/download/
- 將 CMake 添加到系統 PATH

### 找不到 Visual Studio
- 確保安裝了 Visual Studio 2022
- 使用 "Visual Studio Developer Command Prompt" 而不是普通命令提示符
- 或運行: `"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"`

### OpenGL 錯誤
- 確保顯卡驅動程式已更新
- 檢查 OpenGL 版本是否支持 3.3+

### 依賴庫缺失
- 使用 vcpkg 或 apt-get/brew 安裝缺失的依賴
- 或在 CMakeLists.txt 中禁用可選依賴

## 🎯 建置選項

### 調試建置
```batch
cmake --build . --config Debug
```

### Release 建置
```batch
cmake --build . --config Release
```

### 清理建置
```batch
# 刪除 build 目錄
rm -rf build
# 或
rmdir /s build
```

## 📊 性能優化

### Release 建置優化
Release 建置包含以下優化:
- /O2 優化級別
- 鏈接時優化 (LTO)
- 去除調試符號

### 自定義優化
在 CMakeLists.txt 中添加:
```cmake
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O3 /arch:AVX2")
```

## 🔍 調試

### 使用 Visual Studio 調試器
1. 在 Visual Studio 中打開 `build/PotatoEngine.sln`
2. 設置啟動項目
3. 按 F5 開始調試

### 使用 GDB (Linux)
```bash
gdb ./bin/AIAgentGUIExample
```

### 使用 LLDB (macOS)
```bash
lldb ./bin/AIAgentGUIExample
```

## 📚 更多資源

- [CMake 官方文檔](https://cmake.org/documentation/)
- [GLFW 官方文檔](https://www.glfw.org/documentation.html)
- [OpenGL 官方文檔](https://www.opengl.org/documentation/)
- [Visual Studio 文檔](https://docs.microsoft.com/en-us/visualstudio/)

---

**🥔 Potato Engine Team**