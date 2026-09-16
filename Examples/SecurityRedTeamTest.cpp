/**
 * SecurityRedTeamTest - Security 模組對抗性測試（紅隊演練）
 *
 * 以「攻擊者視角」逐一攻擊 SecurityManager 的每項防禦，驗證偵測能力。
 * 所有攻擊皆在本測試行程內完成，不涉及任何惡意程式碼：
 *
 *   [攻擊 1] 竄改檔案          -> 應觸發 IntegrityMismatch
 *   [攻擊 2] 竄改受保護記憶體  -> 應觸發 MemoryTampered
 *   [攻擊 3] 載入未授權 DLL    -> 應觸發 UntrustedModule（模擬 DLL 注入）
 *   [攻擊 4] 冒名白名單 DLL    -> 預期「繞過成功」（揭露純檔名比對的盲點）
 *   [攻擊 5] 監控中注入        -> 背景監控執行緒應偵測到
 *   [偵測面] 反除錯 / 計時異常 -> 附加除錯器時才會觸發，此處回報狀態
 *
 * 另含基線測試：SHA-256 / CRC32 標準向量、SecureZeroMemory、誤報率。
 *
 * 編譯（VS Developer Command Prompt 或 vcvars64 環境）：
 *   cl /std:c++20 /EHsc /I . Examples\SecurityRedTeamTest.cpp Security\SecuritySystem.cpp
 *   cl /LD /std:c++20 /EHsc Examples\FakeCheatModule.cpp /Fe:FakeCheat.dll
 */

#include "Security/SecuritySystem.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
    #include <windows.h>
#endif

using namespace Potato::Security;

namespace {

int g_pass = 0;
int g_fail = 0;
int g_info = 0;
int g_bypass = 0;

std::mutex g_reportsMutex;
std::vector<SecurityReport> g_reports;

void ReportResult(const char* status, const char* name, const std::string& detail = "") {
    printf("  [%s] %s%s%s\n", status, name,
           detail.empty() ? "" : "  -- ", detail.c_str());
}

void Pass(const char* name, const std::string& detail = "") {
    g_pass++;
    ReportResult("PASS", name, detail);
}

void Fail(const char* name, const std::string& detail = "") {
    g_fail++;
    ReportResult("FAIL", name, detail);
}

void Info(const char* name, const std::string& detail = "") {
    g_info++;
    ReportResult("INFO", name, detail);
}

// 攻擊成功繞過防禦 = 防禦盲點（不是測試程式錯誤）
void Bypass(const char* name, const std::string& detail = "") {
    g_bypass++;
    ReportResult("BYPASS", name, detail);
}

bool SawViolation(ViolationType type) {
    std::lock_guard<std::mutex> lock(g_reportsMutex);
    for (const auto& r : g_reports) {
        if (r.type == type) return true;
    }
    return false;
}

void ClearReports() {
    std::lock_guard<std::mutex> lock(g_reportsMutex);
    g_reports.clear();
}

void Section(const char* title) {
    printf("\n=== %s ===\n", title);
}

std::string BaseName(const std::string& path) {
    size_t sep = path.find_last_of("\\/");
    return (sep == std::string::npos) ? path : path.substr(sep + 1);
}

#ifdef _WIN32
// patch 測試用的無害函數：noinline 確保有獨立程式碼可改
__declspec(noinline) int DummyPatchTarget() {
    volatile int x = 42;
    return x;
}

// 攻擊 16 用：執行緒「合法起點」（位於本 exe 內），進入後跳入 shellcode。
// CheckInjectedThreads 只看起始位址抓不到這種；CheckThreadContexts 看 RIP 才抓得到。
volatile void* g_shellcodeTarget = nullptr;
DWORD WINAPI LegitTrampoline(LPVOID) {
    auto f = reinterpret_cast<void(*)()>(
        const_cast<void*>(g_shellcodeTarget));
    f();
    return 0;
}
#endif

} // anonymous namespace

int main(int argc, char* argv[]) {
#ifdef _WIN32
    // 外部 handle 測試的子行程模式：開一個目標行程的 handle 後掛著
    if (argc >= 3 && strcmp(argv[1], "--hold-handle") == 0) {
        DWORD target = static_cast<DWORD>(atoi(argv[2]));
        HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
                                 FALSE, target);
        if (!h) return 2;
        printf("child holding handle\n");
        ::Sleep(15000);
        ::CloseHandle(h);
        return 0;
    }
#endif
    (void)argc; (void)argv;

    printf("====================================================\n");
    printf(" Potato Engine Security Red-Team Test\n");
    printf(" 對抗性測試 Security/ 模組的偵測能力\n");
    printf("====================================================\n");

    auto& sec = SecurityManager::GetInstance();
    sec.Initialize();
    sec.SetViolationCallback([](const SecurityReport& r) {
        std::lock_guard<std::mutex> lock(g_reportsMutex);
        g_reports.push_back(r);
        printf("    >> violation: %s - %s\n", ViolationTypeToString(r.type), r.details.c_str());
    });

    // 遊戲 exe 本身也是模組，不在白名單內會被誤報；正常整合時必須加入
#ifdef _WIN32
    char exePath[MAX_PATH] = {};
    ::GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    sec.AddTrustedModule(BaseName(exePath));
#endif

    // ------------------------------------------------------------------
    Section("[基線] 密碼學 / 校驗原語正確性");
    // ------------------------------------------------------------------
    {
        // SHA-256 標準測試向量
        const std::string abc = "abc";
        bool ok1 = ComputeSHA256(abc.data(), abc.size()) ==
                   "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
        ok1 ? Pass("SHA-256(\"abc\")") : Fail("SHA-256(\"abc\")", "hash mismatch");

        bool ok2 = ComputeSHA256("", 0) ==
                   "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
        ok2 ? Pass("SHA-256(empty)") : Fail("SHA-256(empty)", "hash mismatch");

        // CRC32 標準測試向量 "123456789" -> 0xCBF43926
        const char* digits = "123456789";
        bool ok3 = ComputeCRC32(digits, 9) == 0xCBF43926u;
        ok3 ? Pass("CRC32(\"123456789\")") : Fail("CRC32(\"123456789\")", "crc mismatch");

        // SecureZeroMemory 確實清除
        char secret[32];
        memset(secret, 0xAB, sizeof(secret));
        SecureZeroMemory(secret, sizeof(secret));
        bool cleared = true;
        for (char c : secret) if (c != 0) { cleared = false; break; }
        cleared ? Pass("SecureZeroMemory") : Fail("SecureZeroMemory", "residue remains");
    }

    // ------------------------------------------------------------------
    Section("[攻擊 1] 竄改檔案 -> VerifyFileIntegrity");
    // ------------------------------------------------------------------
    {
        auto tmp = std::filesystem::temp_directory_path() / "potato_redteam_asset.bin";
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            f << "PLAYER_GOLD=100;PLAYER_HP=80;CHECKSUM_DATA";
        }
        const std::string good = ComputeFileSHA256(tmp.string());

        ClearReports();
        bool intact = sec.VerifyFileIntegrity(tmp.string(), good);
        (intact && !SawViolation(ViolationType::IntegrityMismatch))
            ? Pass("原始檔案通過校驗") : Fail("原始檔案通過校驗", "false positive");

        // 攻擊：竄改檔案一個 byte（如修改存檔數值）
        {
            std::fstream f(tmp, std::ios::binary | std::ios::in | std::ios::out);
            f.seekp(12);
            f.put('9'); // 100 -> 900
        }
        ClearReports();
        bool stillOk = sec.VerifyFileIntegrity(tmp.string(), good);
        (!stillOk && SawViolation(ViolationType::IntegrityMismatch))
            ? Pass("偵測到檔案竄改") : Fail("偵測到檔案竄改", "tampered file accepted!");

        // 攻擊：檔案消失
        std::filesystem::remove(tmp);
        ClearReports();
        bool missing = sec.VerifyFileIntegrity(tmp.string(), good);
        (!missing && SawViolation(ViolationType::IntegrityMismatch))
            ? Pass("偵測到檔案遺失") : Fail("偵測到檔案遺失", "missing file accepted!");
    }

    // ------------------------------------------------------------------
    Section("[攻擊 2] 竄改受保護記憶體 -> GuardRegion/VerifyRegion");
    // ------------------------------------------------------------------
    {
        struct PlayerStats { uint32_t hp, gold, level; };
        PlayerStats stats{80, 100, 5};
        uint32_t guard = sec.GuardRegion(&stats, sizeof(stats));

        ClearReports();
        bool intact = sec.VerifyRegion(guard, &stats, sizeof(stats));
        (intact && !SawViolation(ViolationType::MemoryTampered))
            ? Pass("原始記憶體通過校驗") : Fail("原始記憶體通過校驗", "false positive");

        // 攻擊：作弊器直接覆寫記憶體數值
        stats.gold = 999999;
        ClearReports();
        bool stillOk = sec.VerifyRegion(guard, &stats, sizeof(stats));
        (!stillOk && SawViolation(ViolationType::MemoryTampered))
            ? Pass("偵測到記憶體竄改") : Fail("偵測到記憶體竄改", "tampered memory accepted!");

        // 邊界：不存在的 guardId 應回傳 false（且不回報）
        sec.UnguardRegion(guard);
        ClearReports();
        bool orphan = sec.VerifyRegion(guard, &stats, sizeof(stats));
        (!orphan && g_reports.empty())
            ? Pass("Unguard 後驗證正確拒絕") : Fail("Unguard 後驗證正確拒絕");
    }

#ifdef _WIN32
    // ------------------------------------------------------------------
    Section("[攻擊 3] 載入未授權 DLL（模擬注入）-> CheckLoadedModules");
    // ------------------------------------------------------------------
    HMODULE fakeCheat = nullptr;
    {
        // 找 FakeCheat.dll：優先找 exe 同目錄
        std::filesystem::path dllPath =
            std::filesystem::path(exePath).parent_path() / "FakeCheat.dll";
        if (!std::filesystem::exists(dllPath)) {
            dllPath = "FakeCheat.dll"; // 退回工作目錄
        }

        ClearReports();
        auto baseline = sec.FindUntrustedModules();
        if (!baseline.empty()) {
            std::string names;
            for (const auto& n : baseline) names += (names.empty() ? "" : ", ") + n;
            Info("基線未信任模組（白名單可能缺漏）", names);
        }

        fakeCheat = ::LoadLibraryW(dllPath.wstring().c_str());
        if (!fakeCheat) {
            Fail("載入 FakeCheat.dll", "LoadLibrary failed, err=" +
                 std::to_string(::GetLastError()) + " path=" + dllPath.string());
        } else {
            auto now = sec.FindUntrustedModules();
            bool flagged = std::find(now.begin(), now.end(), "fakecheat.dll") != now.end();
            bool reported = false;
            sec.CheckLoadedModules();
            reported = SawViolation(ViolationType::UntrustedModule);
            (flagged && reported)
                ? Pass("偵測到注入的 FakeCheat.dll")
                : Fail("偵測到注入的 FakeCheat.dll", "untrusted DLL not flagged!");

            // 修補路徑：加入信任後不再回報
            sec.AddTrustedModule("fakecheat.dll");
            ClearReports();
            auto after = sec.FindUntrustedModules();
            bool clearedFlag = std::find(after.begin(), after.end(), "fakecheat.dll") == after.end();
            clearedFlag ? Pass("AddTrustedModule 後放行") : Fail("AddTrustedModule 後放行");
        }
    }

    // ------------------------------------------------------------------
    Section("[攻擊 4] 冒名白名單 DLL（繞過嘗試，應被路徑+簽章檢查擋下）");
    // ------------------------------------------------------------------
    {
        // 把 FakeCheat.dll 複製後改名為系統白名單內的模組名
        // （dsound.dll 在系統白名單中且通常未載入 -> 可真正載入第二個模組）
        const char* spoofName = "dsound.dll";
        if (::GetModuleHandleA(spoofName) != nullptr) spoofName = "xaudio2_9.dll";
        if (::GetModuleHandleA(spoofName) != nullptr) spoofName = "xinput9_1_0.dll";

        auto src = std::filesystem::path(exePath).parent_path() / "FakeCheat.dll";
        auto trySpoof = [&](const std::filesystem::path& dst, const char* label) {
            std::error_code ec;
            std::filesystem::create_directories(dst.parent_path());
            std::filesystem::copy_file(src, dst,
                std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) { Info(label, "copy failed: " + ec.message()); return; }
            HMODULE h = ::LoadLibraryExW(dst.wstring().c_str(), nullptr,
                                       LOAD_WITH_ALTERED_SEARCH_PATH);
            if (!h) {
                Info(label, "LoadLibrary failed, err=" + std::to_string(::GetLastError()));
            } else {
                ClearReports();
                sec.CheckLoadedModules();
                auto untrusted = sec.FindUntrustedModules();
                bool flagged = std::find(untrusted.begin(), untrusted.end(), spoofName)
                               != untrusted.end();
                flagged ? Pass(label, std::string(spoofName) + " 被標記")
                        : Bypass(label, std::string(spoofName) + " 未被偵測！");
                ::FreeLibrary(h);
            }
            std::filesystem::remove(dst, ec); // 只刪測試檔，不動目錄
        };

        // 4a. 暫存目錄（非信任目錄）的冒名系統 DLL
        trySpoof(std::filesystem::temp_directory_path() / "potato_redteam_spoof" / spoofName,
                 "偵測暫存目錄冒名 DLL");

        // 4b. 更進一步：放到 exe 同目錄（DLL 搜尋順序劫持路徑）
        //     系統模組名仍必須位於系統目錄 + 有效簽章 -> 應仍被偵測
        trySpoof(std::filesystem::path(exePath).parent_path() / spoofName,
                 "偵測 exe 目錄冒名 DLL");
    }

    // ------------------------------------------------------------------
    Section("[防禦] 雜湊釘選 AddTrustedModuleHash");
    // ------------------------------------------------------------------
    if (fakeCheat) {
        auto dllPath = std::filesystem::path(exePath).parent_path() / "FakeCheat.dll";

        // 釘選錯誤雜湊 -> 已載入的 fakecheat.dll 應立刻變回不受信任
        sec.AddTrustedModuleHash("fakecheat.dll", std::string(64, '0'));
        ClearReports();
        auto now = sec.FindUntrustedModules();
        bool flagged = std::find(now.begin(), now.end(), "fakecheat.dll") != now.end();
        flagged ? Pass("錯誤雜湊被偵測") : Fail("錯誤雜湊被偵測", "hash mismatch missed");

        // 釘選正確雜湊 -> 恢復信任
        std::string realHash = ComputeFileSHA256(dllPath.string());
        sec.AddTrustedModuleHash("fakecheat.dll", realHash);
        ClearReports();
        now = sec.FindUntrustedModules();
        flagged = std::find(now.begin(), now.end(), "fakecheat.dll") != now.end();
        (!flagged) ? Pass("正確雜湊放行") : Fail("正確雜湊放行", "correct hash rejected");
    }

    // ------------------------------------------------------------------
    Section("[攻擊 6] 硬體中斷點（DR 暫存器）-> CheckHardwareBreakpoints");
    // ------------------------------------------------------------------
    {
        // 建立暫停的執行緒，對它設 DR0 硬體中斷點（Cheat Engine 手法）
        HANDLE th = ::CreateThread(nullptr, 0,
            [](LPVOID) -> DWORD { ::Sleep(5000); return 0; },
            nullptr, CREATE_SUSPENDED, nullptr);
        if (!th) {
            Info("硬體中斷點測試", "CreateThread failed");
        } else {
            CONTEXT ctx{};
            ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            ::GetThreadContext(th, &ctx);
            ctx.Dr0 = reinterpret_cast<DWORD_PTR>(&DummyPatchTarget);
            ctx.Dr7 = 0x1; // L0 enable
            ::SetThreadContext(th, &ctx);

            ClearReports();
            bool detected = sec.CheckHardwareBreakpoints() &&
                            SawViolation(ViolationType::HardwareBreakpoint);
            detected ? Pass("偵測到硬體中斷點")
                     : Fail("偵測到硬體中斷點", "DR breakpoint missed");

            // 清掉後不應再回報
            ctx.Dr7 = 0; ctx.Dr0 = 0;
            ::SetThreadContext(th, &ctx);
            ClearReports();
            bool clean = !sec.CheckHardwareBreakpoints();
            clean ? Pass("清除後放行") : Fail("清除後放行", "still flagged");

            ::TerminateThread(th, 0);
            ::CloseHandle(th);
        }
    }

    // ------------------------------------------------------------------
    Section("[攻擊 7] Shellcode 執行緒 -> CheckInjectedThreads");
    // ------------------------------------------------------------------
    {
        // 在 VirtualAlloc 的可執行記憶體跑執行緒 = CreateRemoteThread+shellcode 的特徵
        void* code = ::VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE,
                                    PAGE_EXECUTE_READWRITE);
        if (!code) {
            Info("Shellcode 執行緒測試", "VirtualAlloc failed");
        } else {
            // 組一個最小函數：xor eax,eax; ret（只會立即回傳，無惡意行為）
            uint8_t sc[] = { 0x31, 0xC0, 0xC3 };
            memcpy(code, sc, sizeof(sc));
            ::FlushInstructionCache(::GetCurrentProcess(), code, sizeof(sc));

            // 暫停建立：確保枚舉時執行緒還活著
            DWORD tid = 0;
            HANDLE th = ::CreateThread(nullptr, 0,
                reinterpret_cast<LPTHREAD_START_ROUTINE>(code),
                nullptr, CREATE_SUSPENDED, &tid);
            if (!th) {
                Info("Shellcode 執行緒測試", "CreateThread failed");
            } else {
                ClearReports();
                bool detected = sec.CheckInjectedThreads() &&
                                SawViolation(ViolationType::InjectedThread);
                detected ? Pass("偵測到 shellcode 執行緒")
                         : Fail("偵測到 shellcode 執行緒", "injected thread missed");
                ::TerminateThread(th, 0);
                ::CloseHandle(th);
            }
            ::VirtualFree(code, 0, MEM_RELEASE);
        }
    }

    // ------------------------------------------------------------------
    Section("[攻擊 8] Patch 自身程式碼 -> GuardOwnCode/VerifyOwnCode");
    // ------------------------------------------------------------------
    {
        sec.GuardOwnCode();
        ClearReports();
        bool intact = sec.VerifyOwnCode();
        intact ? Pass("自身 .text 校驗通過") : Fail("自身 .text 校驗通過", "false positive");

        // 攻擊：inline patch 一個 byte（模擬 hook 安裝）
        uint8_t* target = reinterpret_cast<uint8_t*>(&DummyPatchTarget);
        DWORD oldProt = 0;
        if (::VirtualProtect(target, 16, PAGE_EXECUTE_READWRITE, &oldProt)) {
            uint8_t orig = target[0];
            target[0] = orig ^ 0xFF;
            ::FlushInstructionCache(::GetCurrentProcess(), target, 16);

            ClearReports();
            bool detected = !sec.VerifyOwnCode() &&
                            SawViolation(ViolationType::CodeTampered);
            detected ? Pass("偵測到 .text patch")
                     : Fail("偵測到 .text patch", "code patch missed");

            target[0] = orig; // 還原
            ::FlushInstructionCache(::GetCurrentProcess(), target, 16);
            ::VirtualProtect(target, 16, oldProt, &oldProt);
            ClearReports();
            bool restored = sec.VerifyOwnCode();
            restored ? Pass("還原後校驗通過")
                     : Fail("還原後校驗通過", "still tampered after restore");
        } else {
            Info(".text patch 測試", "VirtualProtect failed");
        }
    }

    // ------------------------------------------------------------------
    Section("[攻擊 9] 即時載入攔截 -> EnableImageLoadNotify");
    // ------------------------------------------------------------------
    {
        bool armed = sec.EnableImageLoadNotify();
        if (!armed) {
            Info("即時載入通知", "LdrRegisterDllNotification 不可用");
        } else {
            Pass("EnableImageLoadNotify 註冊成功");

            auto src = std::filesystem::path(exePath).parent_path() / "FakeCheat.dll";
            auto dst = std::filesystem::temp_directory_path() / "potato_redteam_rt"
                       / "sneaky.dll";
            std::error_code ec;
            std::filesystem::create_directories(dst.parent_path());
            std::filesystem::copy_file(src, dst,
                std::filesystem::copy_options::overwrite_existing, ec);

            ClearReports();
            HMODULE h = ec ? nullptr : ::LoadLibraryW(dst.wstring().c_str());
            // 即時通知應在載入後極短時間內觸發（不等掃描週期）
            bool caught = false;
            for (int i = 0; i < 20 && !caught; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                caught = SawViolation(ViolationType::UntrustedModule);
            }
            caught ? Pass("即時攔截到 sneaky.dll 載入")
                   : Fail("即時攔截到 sneaky.dll 載入", "not notified");
            if (h) ::FreeLibrary(h);
            std::filesystem::remove_all(dst.parent_path(), ec);
            sec.DisableImageLoadNotify();
        }
    }

    // ------------------------------------------------------------------
    Section("[攻擊 10] 隱藏模組（SEC_IMAGE 手動映射）-> CheckHiddenModules");
    // ------------------------------------------------------------------
    {
        // 用 SEC_IMAGE 檔案映射製造 MEM_IMAGE 區域 —— 等同手動映射 DLL
        // 的效果：存在映像但不會進 PEB 模組清單，EnumProcessModules 看不到
        auto src = std::filesystem::path(exePath).parent_path() / "FakeCheat.dll";
        HANDLE hf = ::CreateFileW(src.wstring().c_str(), GENERIC_READ,
                                  FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        HANDLE map = hf ? ::CreateFileMappingW(hf, nullptr,
                            PAGE_READONLY | SEC_IMAGE, 0, 0, nullptr) : nullptr;
        void* view = map ? ::MapViewOfFile(map, FILE_MAP_READ, 0, 0, 0) : nullptr;

        if (!view) {
            Info("隱藏模組測試", "SEC_IMAGE map failed, err=" +
                 std::to_string(::GetLastError()));
        } else {
            ClearReports();
            bool detected = sec.CheckHiddenModules() &&
                            SawViolation(ViolationType::HiddenModule);
            detected ? Pass("偵測到隱藏映像")
                     : Fail("偵測到隱藏映像", "hidden image missed");
            ::UnmapViewOfFile(view);
        }
        if (map) ::CloseHandle(map);
        if (hf) ::CloseHandle(hf);
    }

    // ------------------------------------------------------------------
    Section("[攻擊 11] RWX 私有記憶體 -> CheckExecutablePrivateMemory");
    // ------------------------------------------------------------------
    {
        void* rwx = ::VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE,
                                   PAGE_EXECUTE_READWRITE);
        if (!rwx) {
            Info("RWX 記憶體測試", "VirtualAlloc failed");
        } else {
            ClearReports();
            bool detected = sec.CheckExecutablePrivateMemory() &&
                            SawViolation(ViolationType::SuspiciousMemory);
            detected ? Pass("偵測到 RWX 私有區域")
                     : Fail("偵測到 RWX 私有區域", "executable private region missed");
            ::VirtualFree(rwx, 0, MEM_RELEASE);
        }
    }

    // ------------------------------------------------------------------
    Section("[攻擊 12] IAT hook -> CheckIATHooks");
    // ------------------------------------------------------------------
    {
        HMODULE self = ::GetModuleHandleW(nullptr);
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(self);
        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(
            reinterpret_cast<uint8_t*>(self) + dos->e_lfanew);
        auto& impDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];

        // 按「函數名」找 IAT entry 來 patch：挑 CheckIATHooks 內部不會呼叫的函數
        // （掃描路徑只用 EnumProcessModules/GetModuleInformation）。
        bool hooked = false;
        DWORD oldProt = 0;
        IMAGE_THUNK_DATA* hookedThunk = nullptr;
        uintptr_t orig = 0;
        const char* preferFn[] = { "HeapValidate", "VirtualFree",
                                   "GetSystemDirectoryW", "GetWindowsDirectoryW",
                                   "FlushInstructionCache", "TerminateThread" };
        if (impDir.VirtualAddress) {
            auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
                reinterpret_cast<uint8_t*>(self) + impDir.VirtualAddress);
            for (; desc->Name && !hooked; desc++) {
                auto* int_ = reinterpret_cast<IMAGE_THUNK_DATA*>(  // 名稱表
                    reinterpret_cast<uint8_t*>(self) +
                    (desc->OriginalFirstThunk ? desc->OriginalFirstThunk
                                              : desc->FirstThunk));
                auto* iat = reinterpret_cast<IMAGE_THUNK_DATA*>(
                    reinterpret_cast<uint8_t*>(self) + desc->FirstThunk);
                for (int i = 0; int_[i].u1.AddressOfData && !hooked; i++) {
                    if (IMAGE_SNAP_BY_ORDINAL(int_[i].u1.Ordinal)) continue;
                    auto* ibn = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(
                        reinterpret_cast<uint8_t*>(self) + int_[i].u1.AddressOfData);
                    for (const char* want : preferFn) {
                        if (strcmp(reinterpret_cast<const char*>(ibn->Name), want) == 0) {
                            if (::VirtualProtect(&iat[i], sizeof(iat[i]),
                                                 PAGE_READWRITE, &oldProt)) {
                                orig = static_cast<uintptr_t>(iat[i].u1.Function);
                                iat[i].u1.Function =
                                    reinterpret_cast<uintptr_t>(&oldProt);
                                hookedThunk = &iat[i];
                                hooked = true;
                            }
                            break;
                        }
                    }
                }
            }
        }
        if (!hooked) {
            Info("IAT hook 測試", "no writable import entry found");
        } else {
            ClearReports();
            bool detected = sec.CheckIATHooks() &&
                            SawViolation(ViolationType::HookDetected);
            detected ? Pass("偵測到 IAT hook")
                     : Fail("偵測到 IAT hook", "hooked import missed");
            hookedThunk->u1.Function = orig; // 還原
            ::VirtualProtect(hookedThunk, sizeof(*hookedThunk), oldProt, &oldProt);
        }
    }

    // ------------------------------------------------------------------
    Section("[攻擊 13] 外部行程 OpenProcess -> CheckExternalHandles");
    // ------------------------------------------------------------------
    {
        // 啟動子行程對我們 OpenProcess（Cheat Engine 開 handle 的特徵）
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "\"%s\" --hold-handle %lu",
                 exePath, static_cast<unsigned long>(::GetCurrentProcessId()));
        STARTUPINFOA si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (!::CreateProcessA(nullptr, cmd, nullptr, nullptr, FALSE,
                            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            Info("外部 handle 測試", "CreateProcess failed, err=" +
                 std::to_string(::GetLastError()));
        } else {
            ::Sleep(800); // 等子行程 OpenProcess
            // 診斷：子行程是否還活著（失敗會 exit code 2 提早結束）
            DWORD childExit = STILL_ACTIVE;
            ::GetExitCodeProcess(pi.hProcess, &childExit);
            if (childExit != STILL_ACTIVE) {
                Info("外部 handle 測試", "child exited early, code=" +
                     std::to_string(childExit));
            }
            ClearReports();
            std::string diag;
            bool detected = sec.CheckExternalHandles(&diag) &&
                            SawViolation(ViolationType::ExternalHandle);
            detected ? Pass("偵測到外部 handle")
                     : Fail("偵測到外部 handle", "missed: " + diag);
            ::TerminateProcess(pi.hProcess, 0);
            ::CloseHandle(pi.hProcess);
            ::CloseHandle(pi.hThread);
        }
    }

    // ------------------------------------------------------------------
    Section("[攻擊 14] Patch ntdll syscall stub（inline hook）-> CheckCriticalApiHooks");
    // ------------------------------------------------------------------
    {
        ClearReports();
        bool clean = !sec.CheckCriticalApiHooks();
        clean ? Pass("乾淨狀態 syscall stub 完整")
              : Fail("乾淨狀態 syscall stub 完整", "unexpected hook report");

        auto* fn = reinterpret_cast<uint8_t*>(::GetProcAddress(
            ::GetModuleHandleW(L"ntdll.dll"), "NtWriteVirtualMemory"));
        uint8_t orig = 0;
        bool patched = false;
        DWORD oldProt = 0;
        if (fn && ::VirtualProtect(fn, 8, PAGE_EXECUTE_READWRITE, &oldProt)) {
            orig = fn[0];
            fn[0] = 0xE9; // jmp rel32 —— 典型 inline hook 起頭
            ::VirtualProtect(fn, 8, oldProt, &oldProt);
            patched = true;
        }

        ClearReports();
        bool detected = patched && sec.CheckCriticalApiHooks() &&
                        SawViolation(ViolationType::ApiHook);
        detected ? Pass("偵測到 syscall stub hook")
                 : Fail("偵測到 syscall stub hook",
                        patched ? "missed" : "VirtualProtect failed");

        if (patched) {
            // VirtualProtect 失敗時 fn 仍是 RX 頁,直接寫會 AV——先檢查結果
            if (::VirtualProtect(fn, 8, PAGE_EXECUTE_READWRITE, &oldProt)) {
                fn[0] = orig;
                ::VirtualProtect(fn, 8, oldProt, &oldProt);
            } else {
                Fail("還原 patched byte", "VirtualProtect restore failed");
                patched = false; // 標記未能還原,下面「還原後放行」會正確失敗
            }
        }
        ClearReports();
        bool restored = !sec.CheckCriticalApiHooks();
        restored ? Pass("還原後放行")
                 : Fail("還原後放行", "still flagged after restore");
    }

    // ------------------------------------------------------------------
    Section("[攻擊 15] 作弊工具行程 + handle 白名單 -> CheckKnownToolProcesses");
    // ------------------------------------------------------------------
    {
        // 把測試 exe 複製成 cheatengine64.exe 執行（工具名黑名單 + 持 handle）
        auto toolDir = std::filesystem::temp_directory_path() / "potato_redteam_tool";
        std::error_code ec;
        std::filesystem::create_directories(toolDir, ec);
        auto fakeTool = toolDir / "cheatengine64.exe";
        std::filesystem::copy_file(exePath, fakeTool,
            std::filesystem::copy_options::overwrite_existing, ec);

        auto spawnTool = [&](PROCESS_INFORMATION& pi) {
            char cmd[512];
            snprintf(cmd, sizeof(cmd), "\"%s\" --hold-handle %lu",
                     fakeTool.string().c_str(),
                     static_cast<unsigned long>(::GetCurrentProcessId()));
            STARTUPINFOA si{};
            si.cb = sizeof(si);
            return ::CreateProcessA(nullptr, cmd, nullptr, nullptr, FALSE,
                                    CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi) != 0;
        };
        auto killTool = [&](PROCESS_INFORMATION& pi) {
            ::TerminateProcess(pi.hProcess, 0);
            ::CloseHandle(pi.hProcess);
            ::CloseHandle(pi.hThread);
        };

        PROCESS_INFORMATION pi1{};
        if (ec || !spawnTool(pi1)) {
            Info("作弊工具測試", "無法建立測試工具行程");
        } else {
            ::Sleep(800);
            ClearReports();
            bool toolFound = sec.CheckKnownToolProcesses() &&
                             SawViolation(ViolationType::ExternalTool);
            toolFound ? Pass("偵測到作弊工具行程")
                      : Fail("偵測到作弊工具行程", "cheatengine64.exe missed");

            ClearReports();
            std::string diag;
            bool handleFlagged = sec.CheckExternalHandles(&diag) &&
                                 SawViolation(ViolationType::ExternalHandle);
            handleFlagged ? Pass("偵測到工具持有的 handle")
                          : Fail("偵測到工具持有的 handle", "missed: " + diag);
            killTool(pi1);

            // 白名單：把工具名加入 trusted holder 後，新 pid 不再回報
            sec.AddTrustedHandleHolder("cheatengine64.exe");
            PROCESS_INFORMATION pi2{};
            if (spawnTool(pi2)) {
                ::Sleep(800);
                ClearReports();
                bool stillFlagged = sec.CheckExternalHandles(&diag) &&
                                    SawViolation(ViolationType::ExternalHandle);
                stillFlagged ? Fail("白名單持有者放行", "whitelisted holder reported")
                             : Pass("白名單持有者放行");
                killTool(pi2);
            } else {
                // spawn 失敗不能靜默跳過——白名單驗證根本沒跑到
                Fail("白名單持有者放行", "spawn tool process failed");
            }
        }
        std::filesystem::remove_all(toolDir, ec);
    }

    // ------------------------------------------------------------------
    Section("[攻擊 16] 合法起點執行緒跳入 shellcode -> CheckThreadContexts");
    // ------------------------------------------------------------------
    {
        auto* sc = reinterpret_cast<uint8_t*>(::VirtualAlloc(
            nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!sc) {
            Info("RIP 稽核測試", "VirtualAlloc failed");
        } else {
            sc[0] = 0xEB; sc[1] = 0xFE; // jmp $ 無限迴圈
            g_shellcodeTarget = sc;
            HANDLE t = ::CreateThread(nullptr, 0, LegitTrampoline,
                                      nullptr, 0, nullptr);

            // 等執行緒跳進 shellcode：輪詢 CheckThreadContexts 取代固定
            // Sleep(100)——低負載機器上 100ms 內執行緒可能還沒跳到目標
            bool det = false;
            for (int i = 0; i < 100 && !det; ++i) {
                ::Sleep(20);
                ClearReports();
                det = sec.CheckThreadContexts() &&
                      SawViolation(ViolationType::InjectedThread);
            }
            det ? Pass("偵測到執行緒 RIP 在模組外")
                : Fail("偵測到執行緒 RIP 在模組外", "missed after 2s polling");

            // 對照：起始位址在模組內，CheckInjectedThreads 抓不到這隻
            ClearReports();
            bool startAddrMiss = !sec.CheckInjectedThreads();
            startAddrMiss ? Info("對照組",
                "CheckInjectedThreads 未回報——證明 RIP 稽核是必要的互補")
                          : Info("對照組",
                "CheckInjectedThreads 也回報（可能有其他可疑執行緒）");

            ::TerminateThread(t, 0);
            ::CloseHandle(t);
            ::VirtualFree(sc, 0, MEM_RELEASE);
            g_shellcodeTarget = nullptr;
        }
    }

    // ------------------------------------------------------------------
    Section("[攻擊 5] 背景監控中注入 -> StartMonitoring");
    // ------------------------------------------------------------------
    if (fakeCheat) {
        // 先把 fakecheat 移出信任清單，模擬監控進行中被注入
        // （實務上 StartMonitoring 會每個週期掃描全部模組）
        // 這裡改為：載入一個「尚未加入信任」的新模組副本
        auto src = std::filesystem::path(exePath).parent_path() / "FakeCheat.dll";
        auto dir = std::filesystem::temp_directory_path() / "potato_redteam_late";
        std::filesystem::create_directories(dir);
        auto dst = dir / "late_injected.dll";
        std::error_code ec;
        std::filesystem::copy_file(src, dst,
            std::filesystem::copy_options::overwrite_existing, ec);

        ClearReports();
        sec.StartMonitoring(100); // 100ms 週期

        HMODULE late = ec ? nullptr
            : ::LoadLibraryW(dst.wstring().c_str());

        // 等待監控執行緒偵測（最多 2 秒）
        bool detected = false;
        for (int i = 0; i < 20 && !detected; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            detected = SawViolation(ViolationType::UntrustedModule);
        }
        if (late) ::FreeLibrary(late);
        std::filesystem::remove_all(dir, ec);

        detected ? Pass("監控執行緒偵測到注入")
                 : Fail("監控執行緒偵測到注入", "not detected within 2s");

        // 監控中竄改受保護記憶體 -> VerifyAllGuards 自動偵測
        static uint32_t cheatGold = 100;
        uint32_t guardId = sec.GuardRegion(&cheatGold, sizeof(cheatGold));
        ClearReports();
        cheatGold = 999999; // 模擬作弊器寫入
        bool autoCaught = false;
        for (int i = 0; i < 20 && !autoCaught; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            autoCaught = SawViolation(ViolationType::MemoryTampered);
        }
        // 監控運行中心跳應為存活
        bool alive = sec.IsMonitorAlive(2000);
        alive ? Pass("監控心跳存活") : Fail("監控心跳存活", "heartbeat stale");

        sec.StopMonitoring();
        sec.UnguardRegion(guardId);
        cheatGold = 100;

        autoCaught ? Pass("監控自動偵測記憶體竄改")
                   : Fail("監控自動偵測記憶體竄改", "not detected within 2s");

        // 心跳：監控停止後 IsMonitorAlive 應失效
        bool dead = !sec.IsMonitorAlive(200);
        dead ? Pass("監控停止後心跳失效")
             : Fail("監控停止後心跳失效", "stale heartbeat accepted");
    }
#endif

    // ------------------------------------------------------------------
    Section("[偵測面] 反除錯 / 計時異常狀態");
    // ------------------------------------------------------------------
    {
        ClearReports();
        bool dbg = sec.CheckDebugger();
        dbg ? Info("CheckDebugger", "偵測到除錯器（測試正被除錯？）")
            : Info("CheckDebugger", "無除錯器。要驗證此防禦：用 VS/WinDbg/x64dbg 附加本測試行程");

        ClearReports();
        bool dbg2 = sec.CheckDebuggerExtended();
        dbg2 ? Info("CheckDebuggerExtended", "偵測到除錯器")
             : Info("CheckDebuggerExtended",
                    "DebugPort/DebugObject/DebugFlags/PEB 皆正常");

        // 計時異常誤報率：連續執行 200 次統計最大耗時
        int anomalies = 0;
        for (int i = 0; i < 200; i++) {
            ClearReports();
            if (sec.CheckTimingAnomaly()) anomalies++;
        }
        Info("CheckTimingAnomaly x200",
            "異常次數=" + std::to_string(anomalies) +
            "（>0 表示 50ms 閾值在負載下可能誤報；要驗證偵測：除錯器中單步執行）");
    }

    // ------------------------------------------------------------------
    Section("結果摘要");
    // ------------------------------------------------------------------
    printf("  PASS:   %d\n", g_pass);
    printf("  FAIL:   %d\n", g_fail);
    printf("  BYPASS: %d  (防禦盲點，建議強化)\n", g_bypass);
    printf("  INFO:   %d\n", g_info);
    printf("\n");

    if (g_bypass > 0) {
        printf("已揭露的防禦盲點：\n");
        printf("  - 模組白名單僅比對「檔名」。攻擊者把 DLL 改名為信任模組名即可繞過。\n");
        printf("    建議：改為比對模組完整路徑（限 System32/遊戲目錄）、\n");
        printf("          Authenticode 簽章驗證，或對信任模組做 SHA-256 白名單。\n");
        printf("  - 掃描屬 TOCTOU：注入發生在兩次掃描之間時存在偵測空窗。\n");
        printf("  - CheckTimingAnomaly 的固定 50ms 閾值在高負載機器上可能誤報。\n");
    }

    sec.Shutdown();
    return g_fail == 0 ? 0 : 1;
}
