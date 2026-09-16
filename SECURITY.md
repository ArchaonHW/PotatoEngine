# Security Policy / 安全政策

## 回報安全漏洞

如果你在本專案（Potato Engine / MingGoRTS）發現安全漏洞，請**不要**開公開 Issue。
請透過 GitHub Security Advisory 或私下聯絡維護者回報，我們會盡快處理。

## 支援範圍

| 元件 | 狀態 |
|------|------|
| `Security/` 安全模組 | 支援中 |
| 引擎核心模組 | 支援中 |
| `external/` 第三方依賴（imgui 等） | 跟隨上游更新 |

## 已實施的安全措施

### 建置時期（Compile/Link Hardening）

`CMakeLists.txt` 預設啟用 `POTATO_SECURITY_HARDENING=ON`：

- **MSVC**: `/sdl`（SDL 安全檢查）、`/GS`（堆疊 cookie）、`/guard:cf`（控制流防護）、`/DYNAMICBASE`（ASLR）、`/NXCOMPAT`（DEP）、`/HIGHENTROPYVA`、（高位址熵）、`/CETCOMPAT`（CET 陰影堆疊）
- **GCC/Clang**: `-fstack-protector-strong`、`-fstack-clash-protection`、`-fPIE`、`-D_FORTIFY_SOURCE=2`、連結 `-z relro -z now -z noexecstack`
- **MinGW**: `--dynamicbase`、`--nxcompat`、`--high-entropy-va`

關閉方式：`cmake -DPOTATO_SECURITY_HARDENING=OFF ..`

### 執行時期（Security/ 模組）

`Security/SecuritySystem.h` 提供：

- **反除錯**：`CheckDebugger()`（IsDebuggerPresent / CheckRemoteDebuggerPresent / Linux TracerPid / macOS P_TRACED）
- **延伸反除錯**：`CheckDebuggerExtended()`（ProcessDebugPort / DebugObjectHandle / DebugFlags / PEB.BeingDebugged / NtGlobalFlag）
- **計時異常偵測**：`CheckTimingAnomaly()` 偵測單步執行
- **硬體中斷點偵測**：`CheckHardwareBreakpoints()` 掃描全部執行緒 DR0-DR7
- **注入執行緒偵測**：`CheckInjectedThreads()` 執行緒起始位址不在任何模組內（shellcode / CreateRemoteThread）
- **執行緒 RIP 稽核**：`CheckThreadContexts()` 暫停各執行緒檢查指令指標是否落在模組內，補起始位址檢查的盲點（合法起點建立後跳入 shellcode）
- **隱藏模組偵測**：`CheckHiddenModules()` 掃描 `MEM_IMAGE` 區域交叉比對模組清單，抓 PEB unlinked / 手動映射的映像
- **可疑記憶體掃描**：`CheckExecutablePrivateMemory()` 偵測可執行的 `MEM_PRIVATE` 區域（shellcode staging）
- **IAT hook 偵測**：`CheckIATHooks()` import 表 entry 解析位址必須落在已載入模組內
- **關鍵 API inline hook 偵測**：`CheckCriticalApiHooks()` 驗證 ntdll syscall stub 前導碼（x64 恆為 `4C 8B D1 B8`），偵測攔截 `NtProtectVirtualMemory` / `NtWriteVirtualMemory` 等的 hook
- **外部 handle 偵測**：`CheckExternalHandles()` 枚舉系統 handle 表，找出持有本行程 handle 的外部行程；系統目錄內的持有者自動放行，`AddTrustedHandleHolder()` 可加白名單
- **已知工具行程掃描**：`CheckKnownToolProcesses()` 偵測 cheatengine / x64dbg / ollydbg / windbg / processhacker 等
- **Heap 完整性**：`CheckHeapIntegrity()`（HeapValidate）
- **監控心跳**：`IsMonitorAlive()` 監控執行緒被凍結/殺死時可偵測
- **DLL 注入偵測**：`ScanModules()` / `CheckLoadedModules()` — 三重判定：
  - 系統 DLL（`kernel32` 等）：必須位於系統目錄 **且** 通過 Authenticode 簽章驗證（WinVerifyTrust）
  - 信任 DLL（`AddTrustedModule`）：必須位於受信任目錄（exe 目錄 / `AddTrustedDirectory`）
  - 雜湊釘選（`AddTrustedModuleHash`）：額外比對檔案 SHA-256，可阻擋同路徑替換
- **即時載入攔截**：`EnableImageLoadNotify()`（LdrRegisterDllNotification）消除輪詢掃描的 TOCTOU 空窗
- **檔案完整性**：`VerifyFileIntegrity()`（SHA-256）驗證資源/執行檔未被竄改
- **記憶體防竄改**：`GuardRegion()` / `VerifyRegion()` / `VerifyAllGuards()` — keyed HMAC-SHA256（每行程隨機金鑰，攻擊者無法重算校驗值），監控執行緒自動定期驗證
- **自身程式碼完整性**：`GuardOwnCode()` / `VerifyOwnCode()` 對 .text 區段建立 HMAC 快照，偵測 inline patch / hook
- **背景監控**：`StartMonitoring()` 定期執行全部檢查
- **敏感資料清除**：`SecureZeroMemory()` 防止編譯器最佳化移除清除操作

對抗性驗證：`Examples/SecurityRedTeamTest.cpp` + `FakeCheatModule.cpp` 為紅隊測試
（模擬注入、冒名 DLL、記憶體竄改、雜湊釘選繞過、硬體中斷點、shellcode 執行緒、
.text patch、即時載入攔截、隱藏模組、RWX 記憶體、IAT hook、外部 handle、
syscall stub hook、作弊工具行程、RIP 稽核），目前 **39 PASS / 0 FAIL / 0 BYPASS**。

使用範例：

```cpp
#include "Security/SecuritySystem.h"
using namespace Potato::Security;

auto& sec = SecurityManager::GetInstance();
sec.Initialize();
sec.SetViolationCallback([](const SecurityReport& r) {
    // 記錄 / 上報 / 終止程式
    Log("SECURITY VIOLATION: %s - %s", ViolationTypeToString(r.type), r.details.c_str());
});
sec.StartMonitoring(5000); // 每 5 秒檢查一次
```

> 注意：這些機制是「偵測與嚇阻」層級，無法完全阻止有決心的攻擊者。
> 遊戲上線前應搭配伺服器端驗證、程式碼簽章與專業反作弊方案。

### 開發流程

- `.githooks/pre-commit`：秘密掃描（API key、私鑰、密碼模式）、危險函式警告、分支保護
- `.github/workflows/security-scan.yml`：CI 秘密掃描、禁用函式掃描（`gets/strcpy/strcat/sprintf/vsprintf/scanf`）、命令執行 API 警告
- `.gitignore`：封鎖 `.env`、`.pem`、`.key`、私鑰、credentials 等檔案

## 安全編碼規範

1. **禁用函式**：`gets`、`strcpy`、`strcat`、`sprintf`、`vsprintf`、`scanf` — 使用 `snprintf`、`strncpy`+手動結尾、或 `std::string`
2. **字串拷貝後必須保證 null 結尾**
3. **DLL 載入**：使用 `LoadLibraryEx` + `LOAD_LIBRARY_SEARCH_DEFAULT_DIRS`，禁止相對路徑
4. **使用者輸入路徑**：必須驗證/消毒（參考 `SerializationManager::GetSaveSlotPath` 的白名單過濾）
5. **JSON 拼接**：所有字串值必須經過跳脫（參考 `EscapeJsonString`）
6. **秘密管理**：API key 從環境變數或設定檔讀取，永不硬編碼；用畢以 `SecureZeroMemory` 清除
7. **第三方依賴**：FetchContent/submodule 應鎖定至 commit SHA（tag 可被移動）

## 已知限制

- `imgui`（external/）為第三方庫，內含 `sprintf`/`sscanf` 等用法，屬上游程式碼，跟隨上游更新處理
- `FetchContent` 依賴（glfw、glad）目前以 tag 鎖定；正式發布前建議改鎖 commit SHA
