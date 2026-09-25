/**
 * AuditLedgerTest - 防竄改稽核帳本測試（headless,純引擎層）
 *
 * 驗證「區塊鏈揭露層」紀律在引擎事件稽核的落地：
 *   [入帳] hash 鏈 append-only：seq 單調、prevHash 鏈結、HeadHash
 *   [紀律] NOT NULL 欄位拒收、payload 只存 SHA-256 不存原文
 *   [調節] Verify 零容忍：竄改任一欄位/刪除中間筆/改寫 rh 即斷鏈
 *   [揭露] L0/L1/L2 分層匯出粒度
 *   [整合] SecurityManager 違規事件自動入帳
 */

#include "Security/AuditLedger.h"
#include "Security/IntegrityManifest.h"
#include "Security/SecuritySystem.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace Potato;
using namespace Potato::Security;
namespace fs = std::filesystem;

namespace {

int g_pass = 0;
int g_fail = 0;

void Check(bool cond, const char* name, const std::string& detail = "") {
    if (cond) {
        g_pass++;
        printf("  [PASS] %s%s%s\n", name,
               detail.empty() ? "" : "  -- ", detail.c_str());
    } else {
        g_fail++;
        printf("  [FAIL] %s%s%s\n", name,
               detail.empty() ? "" : "  -- ", detail.c_str());
    }
}

bool WriteFile(const fs::path& p, const std::string& text) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << text;
    return f.good();
}

std::string ReadFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// SHA-256("hello world") —— 公開測試向量,外部稽核者可獨立重算對帳
const char* kHelloWorldSha256 =
    "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9";

} // anonymous namespace

int main() {
    printf("=== AuditLedger 防竄改帳本測試 ===\n\n");

    const fs::path dir = fs::temp_directory_path() / "potato_ledger_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    // ========================================================================
    printf("-- [1] 入帳與 hash 鏈 --\n");
    {
        AuditLedger ledger;
        Check(ledger.HeadHash().empty(), "空帳本 HeadHash 為空");

        AuditRecord r1, r2;
        Check(ledger.Append("economy", "GoldTransfer",
                            "hello world", "tx#1001", &r1) && r1.seq == 1,
              "首筆 seq=1");
        Check(r1.prevHash == AuditLedger::GenesisHash(),
              "首筆 prevHash = genesis");
        Check(r1.payloadHash == kHelloWorldSha256,
              "payload 只存 SHA-256（公開測試向量對上）");
        Check(r1.recordHash.size() == 64, "recordHash 為 64 hex");
        Check(!r1.recordId.empty(), "recordId（冪等鍵）已產生");

        Check(ledger.Append("save", "SaveWrite",
                            "save-bytes-v1", "slot#3", &r2),
              "第二筆入帳");
        Check(r2.seq == 2 && r2.prevHash == r1.recordHash,
              "第二筆鏈結首筆 recordHash");
        Check(ledger.HeadHash() == r2.recordHash, "HeadHash 為鏈尾");

        Check(ledger.Verify().Passed(), "Verify 全鏈通過",
              "checked=" + std::to_string(ledger.Verify().checked));
    }

    // ========================================================================
    printf("-- [2] NOT NULL / 禁止欄位紀律 --\n");
    {
        AuditLedger ledger;
        Check(!ledger.Append("", "Ev", "x", "src"),
              "空 category 拒收");
        Check(!ledger.Append("cat", "", "x", "src"),
              "空 eventType 拒收");
        Check(!ledger.Append("cat", "Ev", "x", ""),
              "空 provenance 拒收（consent_ref NOT NULL 對應）");
        Check(!ledger.AppendHash("cat", "Ev", "not-a-hash", "src"),
              "AppendHash 非 64-hex 拒收");
        Check(ledger.AppendHash("cat", "Ev", kHelloWorldSha256, "src"),
              "AppendHash 合法雜湊入帳");
        Check(ledger.Size() == 1, "僅一筆合法記錄入帳");
    }

    // ========================================================================
    printf("-- [3] 零容忍調節：存檔→竄改→斷鏈偵測 --\n");
    {
        const fs::path ledgerFile = dir / "audit.jsonl";

        {
            AuditLedger ledger;
            ledger.Append("violation", "DebuggerDetected", "evidence-a", "sm");
            ledger.Append("economy", "ItemSell", "evidence-b", "tx#2002");
            ledger.Append("save", "SaveWrite", "evidence-c", "slot#1");
            Check(ledger.SaveToFile(ledgerFile.string()), "JSONL 存檔成功");
        }

        // 乾淨載入
        {
            AuditLedger loaded;
            LedgerVerifyResult vr;
            Check(loaded.LoadFromFile(ledgerFile.string(), &vr),
                  "乾淨檔案載入並驗證通過");
            Check(vr.checked == 3, "驗證筆數 = 3");
        }

        // 竄改第 2 筆的 eventType → recordHash 對不上
        {
            std::string content = ReadFile(ledgerFile);
            size_t pos = content.find("ItemSell");
            Check(pos != std::string::npos, "找到竄改目標");
            content.replace(pos, 8, "ItemBuy!");
            WriteFile(dir / "tampered.jsonl", content);

            AuditLedger loaded;
            LedgerVerifyResult vr;
            Check(!loaded.LoadFromFile((dir / "tampered.jsonl").string(), &vr),
                  "竄改 eventType → 載入拒絕");
            Check(vr.firstBadSeq == 2, "斷點定位在第 2 筆",
                  "seq=" + std::to_string(vr.firstBadSeq));
            Check(loaded.Size() == 0, "壞鏈不載入（fail-closed）");
        }

        // 刪除中間筆 → seq gap / 鏈結斷裂
        {
            std::ifstream f(ledgerFile);
            std::string l1, l2, l3;
            std::getline(f, l1); std::getline(f, l2); std::getline(f, l3);
            WriteFile(dir / "deleted.jsonl", l1 + "\n" + l3 + "\n");

            AuditLedger loaded;
            LedgerVerifyResult vr;
            Check(!loaded.LoadFromFile((dir / "deleted.jsonl").string(), &vr),
                  "刪除中間筆 → 載入拒絕");
            Check(!vr.Passed() && vr.firstBadSeq == 3,
                  "斷點定位在被刪後第一筆",
                  "seq=" + std::to_string(vr.firstBadSeq) + " " + vr.reason);
        }

        // 檔案不存在 → false
        {
            AuditLedger loaded;
            Check(!loaded.LoadFromFile((dir / "nope.jsonl").string()),
                  "檔案不存在 → 載入失敗");
        }
    }

    // ========================================================================
    printf("-- [4] L0/L1/L2 分層揭露 --\n");
    {
        AuditLedger ledger;
        ledger.Append("violation", "UntrustedModule", "mod-evidence", "sm");
        ledger.Append("economy", "GoldTransfer", "gold-evidence", "tx#9");

        const std::string l0 = ledger.ExportJson(DisclosureLayer::L0_Internal);
        const std::string l1 = ledger.ExportJson(DisclosureLayer::L1_Deidentified);
        const std::string l2 = ledger.ExportJson(DisclosureLayer::L2_Public);

        Check(l0.find("\"prov\":\"sm\"") != std::string::npos,
              "L0 含 provenance（內部完整層）");
        Check(l1.find("\"prov\"") == std::string::npos,
              "L1 剝除 provenance（去識別層）");
        Check(l1.find("UntrustedModule") != std::string::npos,
              "L1 保留事件類別與 hash");
        Check(l2.find("\"records\"") == std::string::npos &&
              l2.find("\"head\":\"" + ledger.HeadHash() + "\"") != std::string::npos,
              "L2 只揭露筆數與鏈頭（公開錨定）");
    }

    // ========================================================================
    printf("-- [5] SecurityManager 違規自動入帳 --\n");
    {
        // 造一個會失敗的 manifest 場景,讓違規事件經帳本留存證據
        const fs::path installDir = dir / "install";
        fs::create_directories(installDir);
        WriteFile(installDir / "game.exe", "original binary");
        const fs::path manifest = installDir / "integrity.json";
        GenerateIntegrityManifest(installDir.string(), manifest.string(), {});
        WriteFile(installDir / "game.exe", "tampered binary");

        AuditLedger ledger;
        SecurityManager& sm = SecurityManager::GetInstance();
        sm.Initialize();
        sm.SetAuditLedger(&ledger);
        sm.SetViolationCallback(nullptr);

        Check(!sm.CheckIntegrityManifest(manifest.string(), installDir.string()),
              "完整性檢查失敗（預期）");
        Check(ledger.Size() >= 1, "違規已自動入帳",
              "records=" + std::to_string(ledger.Size()));

        bool sawViolation = false;
        for (const auto& r : ledger.Records()) {
            if (r.category == "violation" &&
                r.eventType == "IntegrityMismatch" &&
                r.provenance == "security_manager") {
                sawViolation = true;
            }
        }
        Check(sawViolation, "帳內有 IntegrityMismatch 違規記錄");
        Check(ledger.Verify().Passed(), "違規帳鏈完整");

        sm.SetAuditLedger(nullptr);
        sm.Shutdown();
    }

    // 清理測試殘留
    fs::remove_all(dir, ec);

    printf("\n=== 結果: %d PASS / %d FAIL ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
