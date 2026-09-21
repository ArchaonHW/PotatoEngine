# PotatoEngine

獨立 C++20 遊戲引擎 repo——由 `C:\HWC\MingGoRTS` monorepo 經 `git filter-repo`
歷史拆分產生（保留引擎相關路徑的完整提交歷史）。扁平模組目錄
（`<Module>/*.h|.cpp` 混放）與 monorepo 一致，不再做 include/src 重排。

## 模組清單

AI（僅 NeuralNetwork）、Audio、Core、ECS、Events、FileSystem、GameObject、
Input、Logging、MathUtils、Media、Memory、Physics、Platform、Quantum、
Rendering、Resources、Scene、Security、Serialization、Time、build-video、
external（vendored：glad_gen/tinygltf/imgui submodule）

## 消費關係（重要）

- 遊戲 repo（MingGoRTS）以 sibling checkout + `add_subdirectory` 消費本 repo：
  `POTATO_ENGINE_ROOT`（預設 `../PotatoEngine`）指向本 checkout。
- 依賴方向恆為 `PotatoEngine ← Gameplay ← Campaign`——
  **本 repo 不得引用 Gameplay/Campaign/MingGoRTS_IDE 的檔案或標頭**。
- 引擎測試範例全在 standalone 守衛
  `if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)` 內：
  被消費時只產出 `PotatoEngine`/`NeuralNetwork`/`Quantum`/`Media`/`glad`/`glfw`
  這些 lib target；examples/tests/install/CPack 只在獨立建置時註冊。
- `glfw` 為 imported target 時必須 `GLOBAL`——imported target 預設是
  目錄範圍，不提升則消費端看不到（會退化成 `-lglfw` 裸連結失敗）。
- 消費端直編引擎源碼的路徑一律寫 `${POTATO_ENGINE_ROOT}/...`。
  注意 `PotatoEngine` PUBLIC 定義 `GLFW_INCLUDE_NONE`——
  需要系統 `gl.h` 的 target（如 IDE_GUI）不可連結引擎 lib，
  以直編所需源碼代替（現例：`Rendering/ImageCodec.cpp`、`Logging/Logger.cpp`）。

## 建置與驗證

- 需求：CMake ≥3.15、C++20 編譯器、OpenGL；GLFW 優先本機
  （`-DGLFW_ROOT`），找不到走 FetchContent 3.3.8。
- `external/imgui` 是 git submodule：`git submodule update --init` 後才可建置
  （PortraitBaker 等 target 直編 imgui 源碼）。
- MinGW：`cmake -B build-mingw -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release`
  ；`if(MINGW)` 已加 `-static-libgcc -static-libstdc++ -static`。
- MSVC：`cmake -B build -G "Visual Studio 18 2026" -A x64`。
- 測試：`ctest -C Release`（白名單 `POTATO_TESTS`，29 項引擎層測試；
  GL 依賴測試無顯示環境自行 SKIP）。
- 本地驗證標準：MSVC 與 MinGW 皆建置+ctest 全綠。
- `assets/neural/` 為 gitignore 排除的本機產物：`NeuralArtTool`
  以 MiniMax-H3 示範影片抽幀重訓六個 `.pnn`（sr2x/denoise/colorize/
  texfield/terrain/normalmap）；`NeuralArtDemo` 從 repo 根目錄執行，
  載入權重各跑一次推論並輸出 PNG 至 `neural_demo_out/`。

## 規範

- Commit message 用 conventional commits（CI commitlint）。
- `external/` 既有第三方內容不改動；新增第三方僅限新增子目錄並附
  `README.md`（來源/版本/license）；實作巨集集中單一 TU。
- 不引第三方 JSON 庫——用 `Serialization/JsonParser.h`。
- 回覆與註解用繁體中文；識別符與檔名保持英文。
