# 安全稽核報告 (Security Audit Report)

日期：2026-09-16
範圍：`C:\HWC` 工作區 — MingGoRTS / PotatoEngine（自有程式碼，`external/` 第三方庫除外）

## 已修復的漏洞

### 1. `strncpy` 未保證 null 結尾 — 緩衝區越界讀取風險

**位置**：`MingGoRTS_IDE/GUI/IDEGUI.cpp`（4 處：套用 AI 建議、程式碼生成、修 bug、優化）

`strncpy` 在來源長度 ≥ 目的地大小時**不會**寫入 `\0`。`editorBuffer[8192]` 塞滿後，後續 `std::string(state.editorBuffer)` 會越界讀取直到遇到隨機的 `\0` — 可能洩漏記憶體內容或當機。

**修復**：每處 `strncpy` 後補 `state.editorBuffer[sizeof(state.editorBuffer) - 1] = '\0';`

### 2. DLL 搜尋順序劫持（DLL Search-Order Hijacking）

**位置**：`Platform/PlatformSystem.cpp` `StandardPlatformManager::LoadLibrary`

原實作 `LoadLibraryA(path)` 使用預設搜尋順序 — 若傳入裸檔名，攻擊者在工作目錄放置同名惡意 DLL 即可被載入（木馬植入的經典手法）。

**修復**：改用 `LoadLibraryExA(path, nullptr, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)`，限制搜尋範圍為系統目錄與 DLL 所在目錄；並拒絕空路徑。

### 3. 存檔路徑遍歷（Path Traversal）

**位置**：`Serialization/Serialization.cpp` `GetSaveSlotPath`

`saveSlot` 直接拼接進檔案路徑：`"./Saves/" + saveSlot + ".json"`。
傳入 `../../Windows/System32/x` 之類的值即可讀寫任意路徑。

**修復**：`saveSlot` 採白名單消毒 — 只允許 `[a-zA-Z0-9_-]`，其餘字元過濾；空結果回退為 `invalid_slot`。

### 4. JSON 注入 / 格式破壞

**位置**：
- `Serialization/Serialization.cpp`：`SceneNodeData`、`GameObjectData`、`GameStateData` 的 `Serialize()`
- `AI/LLMIntegration.cpp`：`ChatCompletion`、`ChatCompletionWithTools`、Anthropic client、tool call 序列化

字串值（`name`、`tag`、`content`、tool `name`/`description` 等）未跳脫直接嵌入 JSON。內容含 `"`、`\`、換行即產生非法 JSON；惡意輸入可注入額外欄位（對 LLM API 請求尤其危險 — 可篡改 messages 結構）。

**修復**：兩檔案各加入 `EscapeJsonString`/`EscapeJson` helper（跳脫 `"` `\` 控制字元），所有字串值改走跳脫。`parametersSchema`、`call.arguments` 為預期內嵌 JSON，保持原樣。

## 未發現問題的項目

- 無硬編碼秘密（掃描結果僅為 docs/skills 中的範例佔位字串，如 `'YOUR_API_TOKEN'`、測試密碼 `'SecurePass123!'`）
- 無 `system()`/`popen()`/`CreateProcess` 等命令執行呼叫
- LLM API key 走建構子注入（`OpenAIClient(apiKey)`），未見寫入 log
- `external/imgui` 為 git submodule（上游 ocornut/imgui），非複製貼上的魔改版

## 新增的安全機制

| 層面 | 措施 | 檔案 |
|------|------|------|
| 編譯 | MSVC `/sdl /GS /guard:cf` + `/DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA /CETCOMPAT`；GCC `-fstack-protector-strong -fPIE -D_FORTIFY_SOURCE=2` + `relro/now/noexecstack` | `CMakeLists.txt` |
| 執行 | 反除錯、計時異常、DLL 白名單、SHA-256 檔案校驗、CRC32 記憶體快照、背景監控 | `Security/SecuritySystem.{h,cpp}` |
| CI | 秘密掃描、禁用函式掃描、submodule 檢查（原 workflow 為空殼，已改為實際檢查） | `.github/workflows/security-scan.yml` |
| 本地 | pre-commit 秘密掃描 + 危險函式警告 | `.githooks/pre-commit` |
| 檔案 | `.gitignore` 封鎖 `.env/.pem/.key/id_rsa` 等秘密檔 | `.gitignore` |

## 殘餘風險與建議

1. **`FetchContent` 依賴以 tag 鎖定**（`glfw 3.3.8`、`glad v2.0.4`）— tag 可被上游 force-push 移動。正式發布前建議改鎖 commit SHA。
2. **`FileSystem::ResolvePath` 無沙箱限制** — 目前設計允許任意絕對路徑與 `..`。若未來載入玩家/網路提供的檔名，應在呼叫端驗證，或新增 `SetSandboxRoot()` 將解析限制在指定根目錄下。
3. **`Deserialize` 系列目前為 placeholder**（未真正解析 JSON）— 未來實作時務必使用有長度檢查的解析器，避免 buffer 相關漏洞。
4. **反作弊強度有限** — `Security/` 模組為偵測層。對抗有決心的攻擊者需搭配伺服器端權威驗證、程式碼簽章（Authenticode）、以及商用反作弊方案（EAC/BattlEye 等）。
5. **工作區根目錄有一個 `nul` 殘留檔案**（`C:\HWC\nul`，68 bytes）— 應是 `> nul` 重導向在 Git Bash 下產生的誤建檔案，可刪除。
