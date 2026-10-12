/**
 * SecurityPipelineTest - 掃毒功能整合管線測試
 *
 * 驗證 ContentScanner 的三個整合點：
 *   [管線] ResourceManager 載入掃描策略
 *          Off/UntrustedOnly/All、Malicious 拒載、Suspicious 可配置拒載、
 *          已快取資源不重掃
 *   [落盤] DownloadGuard 目錄監控
 *          新檔落盤即掃、Modified 重掃、Malicious 自動隔離
 *   [自身] IntegrityManifest 完整性白名單
 *          產生/驗證/竄改偵測/消失偵測 + SecurityManager 回呼整合
 */

#include "Resources/ResourceManager.h"
#include "Security/AuditLedger.h"
#include "Security/ContentScanner.h"
#include "Security/DownloadGuard.h"
#include "Security/IntegrityManifest.h"
#include "Security/SecuritySystem.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

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
    fflush(stdout);
}

// 假 loader：不觸碰 GL/音訊,回傳堆上 int 哨兵值
class FakeIntLoader : public IResourceLoader {
public:
    static constexpr int kSentinel = 0x51CA;
    int loadCalls = 0;
    int unloadCalls = 0;

    bool Load(const std::string& path, void** resource) override {
        ++loadCalls;
        lastPath = path;
        *resource = new int(kSentinel);
        return true;
    }
    void Unload(void* resource) override {
        ++unloadCalls;
        delete static_cast<int*>(resource);
    }
    ResourceType GetType() const override { return ResourceType::Texture; }

    std::string lastPath;
};

bool WriteFile(const fs::path& p, const std::string& text) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << text;
    return f.good();
}

bool HasFinding(const FileScanResult& r, const char* ruleId) {
    for (const auto& f : r.findings)
        if (f.ruleId == ruleId) return true;
    return false;
}

// "MZ!!TROJAN" 的 hex——配合 AddByteSignature 產生 Malicious 判定,
// 不落 EICAR 避免觸發真實防毒軟體
const char* kTrojanHex = "4d5a212154524f4a414e";
const char* kTrojanContent = "MZ!!TROJAN";

} // anonymous namespace

int main() {
    printf("=== SecurityPipeline 掃毒整合測試 ===\n\n");

    const fs::path dir = fs::temp_directory_path() / "potato_pipeline_test";
    const fs::path modsDir = dir / "mods";
    const fs::path watchDir = dir / "inbox";
    const fs::path quarantine = dir / "quarantine";
    const fs::path installDir = dir / "install";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(modsDir);
    fs::create_directories(watchDir);
    fs::create_directories(installDir);

    // ========================================================================
    printf("-- [1] ResourceManager 載入掃描策略 --\n");
    {
        ResourceManager rm;
        rm.Initialize();
        auto loader = MakeShared<FakeIntLoader>();
        rm.GetCache().RegisterLoader(ResourceType::Texture, loader);

        std::vector<FileScanResult> scanned;
        rm.SetResourceScanCallback(
            [&](const FileScanResult& r) { scanned.push_back(r); });

        // 測試檔案：惡意（特徵命中）/ 可疑（雙重副檔名）/ 乾淨
        fs::path evil = modsDir / "evil.png";
        WriteFile(evil, kTrojanContent);
        fs::path sus = modsDir / "photo.jpg.exe";
        WriteFile(sus, "harmless bytes here");
        fs::path clean = modsDir / "readme.txt";
        WriteFile(clean, "just a readme");

        // Off：完全不掃,惡意檔也放行
        rm.SetContentScanPolicy(ResourceScanPolicy::Off);
        Check(rm.LoadTexture(evil.string()) != nullptr,
              "policy Off：惡意檔放行（不掃描）");
        Check(scanned.empty(), "policy Off：零掃描呼叫");
        rm.GetCache().UnloadAll();

        // All + 惡意特徵：Malicious 拒載,loader 未被呼叫
        rm.GetContentScanner().AddByteSignature("sig.test_marker",
                                                kTrojanHex, "測試木馬標記");
        rm.SetContentScanPolicy(ResourceScanPolicy::All);
        Check(rm.LoadTexture(evil.string()) == nullptr,
              "policy All：Malicious 拒載");
        Check(loader->loadCalls == 1,
              "拒載時 loader 未被呼叫", std::to_string(loader->loadCalls));
        Check(!scanned.empty() &&
              scanned.back().verdict == ScanVerdict::Malicious,
              "回呼收到 Malicious 判定");

        // Suspicious 預設放行但回呼有記錄
        Check(rm.LoadTexture(sus.string()) != nullptr,
              "Suspicious 預設放行");
        Check(scanned.back().verdict == ScanVerdict::Suspicious,
              "回呼記錄 Suspicious 判定");
        rm.GetCache().UnloadAll();

        // blockSuspicious：升級為拒載
        rm.SetScanBlockSuspicious(true);
        int before = loader->loadCalls;
        Check(rm.LoadTexture(sus.string()) == nullptr,
              "blockSuspicious：Suspicious 拒載");
        Check(loader->loadCalls == before, "拒載時 loader 未被呼叫");
        rm.SetScanBlockSuspicious(false);

        // 乾淨檔正常載入；第二次命中快取不重掃
        size_t scanCountBefore = scanned.size();
        Check(rm.LoadTexture(clean.string()) != nullptr, "乾淨檔載入成功");
        Check(scanned.size() == scanCountBefore + 1, "首載掃描一次");
        Check(rm.LoadTexture(clean.string()) != nullptr, "快取命中仍回資源");
        Check(scanned.size() == scanCountBefore + 1,
              "快取命中不重複掃描（SHA-256 不重算）");
        rm.GetCache().UnloadAll();

        // UntrustedOnly：目錄外不掃、目錄內掃
        rm.SetContentScanPolicy(ResourceScanPolicy::UntrustedOnly);
        rm.AddUntrustedPath(modsDir.string());
        fs::path trusted = dir / "trusted.txt";
        WriteFile(trusted, "engine-owned file");

        size_t n0 = scanned.size();
        rm.LoadTexture(trusted.string());
        Check(scanned.size() == n0, "UntrustedOnly：目錄外不掃描");
        rm.GetCache().UnloadAll();

        rm.LoadTexture(clean.string());
        Check(scanned.size() == n0 + 1, "UntrustedOnly：目錄內檔案有掃描");
        Check(rm.LoadTexture(evil.string()) == nullptr,
              "UntrustedOnly：目錄內惡意檔拒載");

        rm.Shutdown();
    }

    // ========================================================================
    printf("-- [2] DownloadGuard 落盤即掃 --\n");
    {
        DownloadGuard guard;
        guard.Scanner().AddByteSignature("sig.test_marker",
                                         kTrojanHex, "測試木馬標記");
        guard.SetAutoQuarantine(true, quarantine.string());

        int callbackCount = 0;
        guard.SetCallback([&](const FileScanResult&) { ++callbackCount; });

        Check(guard.Watch(watchDir.string()), "Watch 目錄成功");
        Check(guard.Poll().empty(), "基準線無事件");

        // 乾淨檔落盤 → 掃描 → Clean
        WriteFile(watchDir / "hello.txt", "hello world");
        auto r1 = guard.Poll();
        Check(r1.size() == 1 && r1[0].verdict == ScanVerdict::Clean,
              "新檔落盤即掃 → Clean");
        Check(callbackCount == 1, "回呼觸發一次");

        // 惡意檔落盤 → Malicious → 自動隔離
        fs::path drop = watchDir / "payload.bin";
        WriteFile(drop, kTrojanContent);
        auto r2 = guard.Poll();
        Check(r2.size() == 1 && r2[0].verdict == ScanVerdict::Malicious,
              "惡意檔落盤即掃 → Malicious");
        Check(!fs::exists(drop), "Malicious 已移出監視目錄");
        Check(fs::exists(quarantine / (r2[0].sha256 + ".quarantine")),
              "隔離檔存在");
        Check(fs::exists(quarantine / (r2[0].sha256 + ".json")),
              "隔離清單存在");

        // 隔離造成的 Deleted 事件不產生掃描
        Check(guard.Poll().empty(), "Deleted 事件不觸發掃描");

        // Modified 重掃：乾淨檔被改成惡意內容
        // （用不同 payload——同雜湊的隔離檔名相同會互蓋,需兩種內容才有兩筆）
        WriteFile(watchDir / "update.txt", "v1 clean");
        guard.Poll();
        WriteFile(watchDir / "update.txt",
                  std::string(kTrojanContent) + "-variant");
        auto r3 = guard.Poll();
        Check(r3.size() == 1 && r3[0].verdict == ScanVerdict::Malicious,
              "Modified 事件重掃 → Malicious");

        // 關閉 Modified 掃描
        guard.SetScanModified(false);
        WriteFile(watchDir / "mod2.txt", "clean");
        guard.Poll();
        WriteFile(watchDir / "mod2.txt", "still clean but changed!");
        Check(guard.Poll().empty(), "scanModified=false 時修改不掃");

        guard.Unwatch();
        Check(!guard.IsWatching(), "Unwatch 後停止監視");
    }

    // ========================================================================
    printf("-- [3] IntegrityManifest 自身完整性 --\n");
    {
        WriteFile(installDir / "game.exe", "fake game binary");
        WriteFile(installDir / "engine.dll", "fake engine binary");
        fs::create_directories(installDir / "assets");
        WriteFile(installDir / "assets" / "tex.png", "fake png");
        fs::create_directories(installDir / "Saves");
        WriteFile(installDir / "Saves" / "save1.dat", "save data");

        fs::path manifest = installDir / "integrity.json";
        Check(GenerateIntegrityManifest(
                  installDir.string(), manifest.string(), {"Saves/"}),
              "產生完整性清單");

        // 清單應含 3 檔（exe/dll/png）,排除 Saves/ 與自身
        auto res = VerifyIntegrityManifest(manifest.string(),
                                           installDir.string());
        Check(res.Passed() && res.ok == 3,
              "全新驗證通過", "ok=" + std::to_string(res.ok));

        // 竄改一個檔案 → mismatch
        WriteFile(installDir / "engine.dll", "tampered engine binary");
        res = VerifyIntegrityManifest(manifest.string(), installDir.string());
        Check(!res.Passed() && res.mismatch == 1 && res.ok == 2,
              "竄改偵測 → mismatch=1");

        // 刪除一個檔案 → missing
        WriteFile(installDir / "engine.dll", "fake engine binary");
        fs::remove(installDir / "assets" / "tex.png");
        res = VerifyIntegrityManifest(manifest.string(), installDir.string());
        Check(res.missing == 1, "檔案消失 → missing=1");

        // manifest 不存在 → 失敗
        res = VerifyIntegrityManifest((dir / "nonexistent.json").string(),
                                      installDir.string());
        Check(!res.Passed() && res.Total() == 0,
              "manifest 不存在 → 失敗");

        // SecurityManager 整合：違規經 IntegrityMismatch 回呼上報
        WriteFile(installDir / "game.exe", "rootkit'd binary");
        SecurityManager& sm = SecurityManager::GetInstance();
        sm.Initialize();
        int violations = 0;
        ViolationType lastType = ViolationType::None;
        sm.SetViolationCallback([&](const SecurityReport& rep) {
            ++violations;
            lastType = rep.type;
        });
        Check(!sm.CheckIntegrityManifest(manifest.string(),
                                         installDir.string()),
              "CheckIntegrityManifest 偵測失敗回 false");
        Check(violations >= 1 && lastType == ViolationType::IntegrityMismatch,
              "違規經 IntegrityMismatch 回呼上報",
              std::to_string(violations) + " violations");
        sm.SetViolationCallback(nullptr);
        sm.Shutdown();
    }

    // ========================================================================
    printf("-- [4] 簽章完整性清單（HMAC 防清單偽造）--\n");
    {
        const fs::path signedDir = dir / "signed";
        fs::create_directories(signedDir);
        WriteFile(signedDir / "a.exe", "binary a");
        WriteFile(signedDir / "b.dll", "binary b");
        const std::string key = "test-hmac-key-0123456789";
        const std::string wrongKey = "wrong-key";
        const fs::path sman = signedDir / "integrity.json";

        Check(GenerateIntegrityManifest(signedDir.string(), sman.string(), {},
                                        key.data(), key.size()),
              "產生簽章清單");

        auto res = VerifyIntegrityManifest(sman.string(), signedDir.string(),
                                           key.data(), key.size());
        Check(res.Passed() && res.ok == 2, "簽章清單驗證通過");

        res = VerifyIntegrityManifest(sman.string(), signedDir.string());
        Check(res.manifestError, "簽章清單缺金鑰 → manifestError");

        res = VerifyIntegrityManifest(sman.string(), signedDir.string(),
                                      wrongKey.data(), wrongKey.size());
        Check(res.manifestError, "錯金鑰 → manifestError");

        // 竄改檔案：簽章清單本身未被動,HMAC 仍有效 → 逐檔比對抓 mismatch
        WriteFile(signedDir / "a.exe", "trojan'd a");
        res = VerifyIntegrityManifest(sman.string(), signedDir.string(),
                                      key.data(), key.size());
        Check(!res.Passed() && res.mismatch == 1 && !res.manifestError,
              "竄改檔案 → mismatch（HMAC 仍有效）");

        // 攻擊者重算未簽章清單企圖掩蓋 → 金鑰策略 fail-closed 擋下
        const fs::path fakeMan = dir / "fake_manifest.json";
        Check(GenerateIntegrityManifest(signedDir.string(), fakeMan.string()),
              "攻擊者可產生未簽章清單");
        res = VerifyIntegrityManifest(fakeMan.string(), signedDir.string(),
                                      key.data(), key.size());
        Check(res.manifestError, "未簽章清單 + 金鑰 → manifestError");

        // 手改簽章清單的 sha256 欄位 → HMAC 不符
        {
            std::ifstream mf(sman);
            std::string text((std::istreambuf_iterator<char>(mf)),
                             std::istreambuf_iterator<char>());
            mf.close();
            auto pos = text.find("\"sha256\": \"");
            Check(pos != std::string::npos, "清單含 sha256 欄位");
            // 把第一個 hex 字元翻面（0↔1 等）
            char& c = text[pos + 11];
            c = (c == '0') ? '1' : '0';
            WriteFile(sman, text);
        }
        res = VerifyIntegrityManifest(sman.string(), signedDir.string(),
                                      key.data(), key.size());
        Check(res.manifestError, "清單被手改 → hmac mismatch");
    }

    // ========================================================================
    printf("-- [5] DownloadGuard 稽核入帳 --\n");
    {
        const fs::path watch2 = dir / "inbox2";
        const fs::path quar2 = dir / "quar2";
        fs::create_directories(watch2);

        AuditLedger ledger;
        DownloadGuard guard;
        guard.Scanner().AddByteSignature("sig.test_marker",
                                         kTrojanHex, "測試木馬標記");
        guard.SetAutoQuarantine(true, quar2.string());
        guard.SetAuditLedger(&ledger);
        Check(guard.Watch(watch2.string()), "Watch 目錄成功");
        guard.Poll();  // 基準線

        WriteFile(watch2 / "clean.txt", "harmless");
        guard.Poll();
        Check(ledger.Size() == 0, "Clean 判定不入帳");

        WriteFile(watch2 / "bad.bin", kTrojanContent);
        auto r = guard.Poll();
        Check(r.size() == 1 && r[0].verdict == ScanVerdict::Malicious,
              "惡意檔判定 Malicious");
        Check(ledger.Size() == 2,
              "scan + quarantine 各入一帳", std::to_string(ledger.Size()));

        const auto& recs = ledger.Records();
        Check(recs.size() >= 2 &&
              recs[0].category == "scan" &&
              recs[0].eventType == "Malicious" &&
              recs[1].category == "quarantine" &&
              recs[1].eventType == "ok",
              "帳本類別/事件型別正確");

        auto lv = ledger.Verify();
        Check(lv.Passed() && lv.checked == 2, "帳本鏈完整可驗證");
        Check(!ledger.HeadHash().empty(), "HeadHash 可供外部錨定");
    }

    // ========================================================================
    printf("-- [6] 簽章特徵庫 + 隔離區管理 --\n");
    {
        const std::string sigKey = "sigdb-key-0123456789";
        const std::string wrongKey = "wrong-key";
        const fs::path sigJson = dir / "sigdb_src.json";
        WriteFile(sigJson,
            "{\"hashes\": [], \"patterns\": [{\"id\": \"sig.signed_test\", "
            "\"hex\": \"4d5a2121\", \"description\": \"signed db entry\"}]}");
        const fs::path signedDb = dir / "sigdb.signed";

        Check(SignFile(sigJson.string(), signedDb.string(),
                       sigKey.data(), sigKey.size()),
              "SignFile 產生簽章特徵庫");

        ContentScanner s;
        Check(s.LoadSignedSignatureDB(signedDb.string(),
                                      sigKey.data(), sigKey.size()) == 1,
              "簽章特徵庫載入 1 條");
        Check(s.SignatureCount() == 1, "SignatureCount == 1");

        ContentScanner s2;
        Check(s2.LoadSignedSignatureDB(signedDb.string(),
                                       wrongKey.data(), wrongKey.size()) == -1,
              "錯金鑰 → -1");
        Check(s2.LoadSignedSignatureDB(sigJson.string(),
                                       sigKey.data(), sigKey.size()) == -1,
              "未簽章 JSON → -1");
        Check(s2.SignatureCount() == 0, "驗證失敗不載入任何條目");

        // 竄改簽章 blob 一個位元組
        {
            std::ifstream bf(signedDb, std::ios::binary);
            std::vector<uint8_t> blob((std::istreambuf_iterator<char>(bf)),
                                      std::istreambuf_iterator<char>());
            bf.close();
            blob[4] ^= 0xFF;
            const fs::path corrupt = dir / "sigdb_corrupt.signed";
            std::ofstream cf(corrupt, std::ios::binary | std::ios::trunc);
            cf.write(reinterpret_cast<const char*>(blob.data()),
                     static_cast<std::streamsize>(blob.size()));
            cf.close();
            Check(s2.LoadSignedSignatureDB(corrupt.string(),
                                           sigKey.data(), sigKey.size()) == -1,
                  "竄改 blob → -1");
        }

        // 隔離區管理：[2] 已隔離 payload.bin 與 update.txt 兩筆
        auto q = s.ListQuarantine(quarantine.string());
        Check(q.size() == 2, "ListQuarantine 列出 2 筆",
              std::to_string(q.size()));
        Check(!q.empty() && !q[0].originalPath.empty() &&
              !q[0].sha256.empty(),
              "清單含原路徑與雜湊");

        if (q.size() >= 2) {
            const fs::path restored = dir / "restored.bin";
            std::string err;
            Check(s.RestoreFromQuarantine(quarantine.string(), q[0].sha256,
                                          restored.string(), &err),
                  "RestoreFromQuarantine 成功", err);
            Check(ComputeFileSHA256(restored.string()) == q[0].sha256,
                  "還原檔雜湊與清單一致");
            Check(!fs::exists(quarantine / (q[0].sha256 + ".quarantine")),
                  "還原後隔離檔已移出");

            Check(!s.RestoreFromQuarantine(quarantine.string(), "deadbeef",
                                         (dir / "x.bin").string(), &err),
                  "不存在的 sha256 → 失敗");

            // 隔離檔被竄改 → 拒絕還原
            const fs::path qfile =
                quarantine / (q[1].sha256 + ".quarantine");
            WriteFile(qfile, "tampered quarantine content");
            Check(!s.RestoreFromQuarantine(quarantine.string(), q[1].sha256,
                                         (dir / "y.bin").string(), &err),
                  "隔離檔雜湊不符 → 拒絕還原");
        }
    }

    // ========================================================================
    printf("-- [7] 白名單雜湊 + 遞迴監視 --\n");
    {
        const std::string trojanHash =
            ComputeSHA256(kTrojanContent, std::strlen(kTrojanContent));

        // 白名單：內容命中特徵但雜湊在白名單 → Clean（跳過啟發式）
        ContentScanner s;
        s.AddByteSignature("sig.test_marker", kTrojanHex, "t");
        s.AddWhitelistedHash(trojanHash);
        auto r = s.ScanBuffer(kTrojanContent, std::strlen(kTrojanContent),
                              "known_mod.bin");
        Check(r.verdict == ScanVerdict::Clean &&
              HasFinding(r, "hash.whitelist"),
              "白名單命中 → Clean + info finding");

        // 黑白名單同時命中 → 黑名單優先
        s.AddBlacklistedHash(trojanHash);
        r = s.ScanBuffer(kTrojanContent, std::strlen(kTrojanContent),
                         "known_mod.bin");
        Check(r.verdict == ScanVerdict::Malicious,
              "黑白同單 → 黑名單優先 Malicious");

        // JSON 特徵庫的 whitelist 欄位
        const fs::path wldb = dir / "sigdb_wl.json";
        WriteFile(wldb, "{\"whitelist\": [\"" + trojanHash + "\"]}");
        ContentScanner s3;
        Check(s3.LoadSignatureDB(wldb.string()) == 1,
              "特徵庫 whitelist 欄位載入");
        Check(s3.SignatureCount() == 1, "白名單計入 SignatureCount");

        // 遞迴監視
        const fs::path rdir = dir / "rw";
        fs::create_directories(rdir / "sub_a");  // 既有子目錄
        DownloadGuard g;
        g.Scanner().AddByteSignature("sig.test_marker", kTrojanHex, "t");
        Check(g.Watch(rdir.string(), true), "遞迴 Watch 成功");
        g.Poll();  // 基準線

        // 既有子目錄內丟檔 → 抓到
        WriteFile(rdir / "sub_a" / "f.bin", kTrojanContent);
        auto rr = g.Poll();
        Check(rr.size() == 1 && rr[0].verdict == ScanVerdict::Malicious,
              "既有子目錄新檔 → Malicious");

        // 新增子目錄 + 檔案 → 自動納入監視並即掃既有檔
        fs::create_directories(rdir / "sub_b");
        WriteFile(rdir / "sub_b" / "drop.bin",
                  std::string(kTrojanContent) + "-deep");
        rr = g.Poll();
        Check(rr.size() == 1 && rr[0].verdict == ScanVerdict::Malicious,
              "新子目錄內檔案即掃 → Malicious",
              std::to_string(rr.size()) + " results");
    }

    // 清理測試殘留
    fs::remove_all(dir, ec);

    printf("\n=== 結果: %d PASS / %d FAIL ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
