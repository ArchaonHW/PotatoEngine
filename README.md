# PotatoEngine

獨立 C++20 遊戲引擎。本 repo 由 [MingGoRTS](https://github.com/ArchaonHW/MingGoRTS) monorepo
經 `git filter-repo` 歷史拆分產生，保留引擎相關路徑的完整提交歷史。

## 模組

| 分類 | 模組 |
|------|------|
| 核心 | Core（JobSystem/Profiler）、ECS、Events、GameObject、Memory、Scene、Time |
| 平台 | Platform、FileSystem（FileWatcher）、Input、Logging |
| 渲染 | Rendering（OpenGL/Camera/Shader/ModelLoader/ParticleSystem/SpriteAtlas/NeuralGraphics） |
| 遊戲系統 | Physics、Audio（miniaudio）、Resources、Serialization（JsonParser/SignedSaveFile） |
| AI | NeuralNetwork、ReinforcementLearning、NaturalLanguageProcessing、AIAgentSystem、LLMIntegration、RAGSystem、AgentChain、ToolFramework、IntelligentDevelopmentSystem |
| 安全 | Security（ContentScanner/DownloadGuard/IntegrityManifest/AuditLedger/ProtectedValue） |
| 其他 | MathUtils（header-only）、Networking（UdpSocket/LockstepSync）、Media、Quantum |

`external/` 為 vendored 第三方：glad_gen、tinygltf、miniaudio、imgui（git submodule）。

## 建置

需求：CMake ≥ 3.15、C++20 編譯器、OpenGL。GLFW 優先使用本機安裝
（`-DGLFW_ROOT=...`），找不到則走 FetchContent 3.3.8。

```bash
git submodule update --init   # external/imgui
```

**MSVC：**

```bash
cmake -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
```

**MinGW：**

```bash
cmake -B build-mingw -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build-mingw
```

## 測試

```bash
ctest --test-dir build -C Release
```

白名單 `POTATO_TESTS` 共 40 項引擎層測試；GL 依賴測試在無顯示環境會自行 SKIP。

## 消費方式

遊戲 repo（MingGoRTS）以 sibling checkout + `add_subdirectory` 消費本 repo，
`POTATO_ENGINE_ROOT`（預設 `../PotatoEngine`）指向本 checkout。被消費時只產出
函式庫 target；examples/tests/install 只在獨立建置時註冊。

## 文件

- [docs/index.md](./docs/index.md) — 完整文件索引
- [AGENTS.md](./AGENTS.md) — 模組邊界、消費關係與規範（貢獻前必讀）
- [SECURITY.md](./SECURITY.md) — 安全政策

## License

見 [LICENSE](./LICENSE)。
