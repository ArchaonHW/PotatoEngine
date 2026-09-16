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

} // anonymous namespace

int main() {
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
        sec.StopMonitoring();
        if (late) ::FreeLibrary(late);
        std::filesystem::remove_all(dir, ec);

        detected ? Pass("監控執行緒偵測到注入")
                 : Fail("監控執行緒偵測到注入", "not detected within 2s");
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
