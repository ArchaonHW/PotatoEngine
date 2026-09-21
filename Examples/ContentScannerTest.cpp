/**
 * ContentScannerTest - Security 模組內容掃毒測試
 *
 * 驗證 ContentScanner 的偵測層：
 *   [特徵] EICAR 標準測試檔(記憶體掃描,不落盤避免觸發真 AV)
 *   [特徵] SHA-256 黑名單 / JSON 特徵庫載入與位元組特徵
 *   [啟發] 雙重副檔名(photo.jpg.exe)
 *   [啟發] 資料副檔名藏 PE/ELF 檔頭
 *   [啟發] 資料檔內嵌 PE payload(appended payload)
 *   [啟發] 腳本危險 API(os.execute / powershell -enc)
 *   [流程] ScanDirectory 彙總 / QuarantineFile 隔離 + 清單
 *   [邊界] 不存在的檔案 → Error;乾淨檔案 → Clean
 */

#include "Security/ContentScanner.h"
#include "Security/SecuritySystem.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace Potato::Security;
namespace fs = std::filesystem;

namespace {

int g_pass = 0;
int g_fail = 0;

void Pass(const char* name, const std::string& detail = "") {
    g_pass++;
    printf("  [PASS] %s%s%s\n", name,
           detail.empty() ? "" : "  -- ", detail.c_str());
}

void Fail(const char* name, const std::string& detail = "") {
    g_fail++;
    printf("  [FAIL] %s%s%s\n", name,
           detail.empty() ? "" : "  -- ", detail.c_str());
}

void Expect(bool cond, const char* name, const std::string& detail = "") {
    if (cond) Pass(name, detail); else Fail(name, detail);
}

// 最小合法 PE 檔頭：MZ + e_lfanew=0x80 + "PE\0\0"（共 0x84 bytes）
std::vector<uint8_t> MakePE() {
    std::vector<uint8_t> pe(0x84, 0);
    pe[0] = 'M'; pe[1] = 'Z';
    pe[0x3C] = 0x80;
    pe[0x80] = 'P'; pe[0x81] = 'E';
    return pe;
}

// PNG 魔數 + 填充
std::vector<uint8_t> MakePNG(size_t size = 256) {
    std::vector<uint8_t> png(size, 0xAA);
    static const uint8_t magic[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::memcpy(png.data(), magic, 8);
    return png;
}

bool WriteFile(const fs::path& p, const std::vector<uint8_t>& data) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
    return f.good();
}

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

} // anonymous namespace

int main() {
    printf("=== ContentScanner 內容掃毒測試 ===\n\n");

    const fs::path dir = fs::temp_directory_path() / "potato_scanner_test";
    const fs::path quarantine = fs::temp_directory_path() / "potato_quarantine";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::remove_all(quarantine, ec);
    fs::create_directories(dir);

    ContentScanner scanner;

    // ---- [特徵] EICAR（ScanBuffer,不落盤）----
    printf("-- EICAR 記憶體掃描 --\n");
    {
        const char* eicar =
            "X5O!P%@AP[4\\PZX54(P^)7CC)7}$EICAR-STANDARD-ANTIVIRUS-TEST-FILE!$H+H*";
        auto r = scanner.ScanBuffer(eicar, std::strlen(eicar), "eicar.com");
        Expect(r.verdict == ScanVerdict::Malicious &&
               HasFinding(r, "sig.eicar"), "EICAR 判定 Malicious",
               ScanVerdictToString(r.verdict));

        ContentScanner noEicar;
        noEicar.SetDetectEicar(false);
        auto r2 = noEicar.ScanBuffer(eicar, std::strlen(eicar), "eicar.com");
        Expect(!HasFinding(r2, "sig.eicar"), "關閉 EICAR 開關後不命中");
    }

    // ---- [特徵] SHA-256 黑名單 ----
    printf("-- SHA-256 黑名單 --\n");
    fs::path badFile = dir / "evil_payload.bin";
    {
        std::string payload = "this is a pretend malware payload";
        WriteFile(badFile, payload);
        std::string hash = ComputeSHA256(payload.data(), payload.size());
        ContentScanner s2;
        s2.AddBlacklistedHash(hash);
        auto r = s2.ScanFile(badFile.string());
        Expect(r.verdict == ScanVerdict::Malicious &&
               HasFinding(r, "hash.blacklist"), "黑名單雜湊判定 Malicious");
        Expect(r.sha256 == hash, "結果含正確 SHA-256");
    }

    // ---- [特徵] JSON 特徵庫 ----
    printf("-- JSON 特徵庫 --\n");
    {
        fs::path dbPath = dir / "sigdb.json";
        // 位元組特徵 "MZ!!TROJAN" 的 hex
        WriteFile(dbPath,
            "{\n"
            "  \"hashes\": [],\n"
            "  \"patterns\": [{\"id\": \"sig.trojan_marker\", "
            "\"hex\": \"4d5a212154524f4a414e\", "
            "\"description\": \"測試木馬標記\"}]\n"
            "}\n");
        ContentScanner s3;
        int n = s3.LoadSignatureDB(dbPath.string());
        Expect(n == 1 && s3.SignatureCount() == 1,
               "特徵庫載入 1 條", std::to_string(n));

        std::vector<uint8_t> trojan = {'M','Z','!','!','T','R','O','J','A','N'};
        trojan.resize(512, 0);
        auto r = s3.ScanBuffer(trojan.data(), trojan.size(), "sample.bin");
        Expect(r.verdict == ScanVerdict::Malicious &&
               HasFinding(r, "sig.trojan_marker"),
               "位元組特徵命中 → Malicious");
    }

    // ---- [啟發] 雙重副檔名 ----
    printf("-- 雙重副檔名 --\n");
    {
        fs::path dbl = dir / "vacation_photo.jpg.exe";
        WriteFile(dbl, MakePNG());  // 內容甚至是 PNG,重點是檔名
        auto r = scanner.ScanFile(dbl.string());
        Expect(r.verdict == ScanVerdict::Suspicious &&
               HasFinding(r, "heur.double_ext"),
               "photo.jpg.exe 判定 Suspicious");
    }

    // ---- [啟發] 資料副檔名藏執行檔頭 ----
    printf("-- 偽裝執行檔 --\n");
    {
        fs::path fake = dir / "innocent.png";
        WriteFile(fake, MakePE());
        auto r = scanner.ScanFile(fake.string());
        Expect(r.verdict == ScanVerdict::Suspicious &&
               HasFinding(r, "heur.exec_in_data"),
               "PE 偽裝 .png 判定 Suspicious");

        fs::path elf = dir / "font.ttf";
        std::vector<uint8_t> elfBytes = {0x7F, 'E', 'L', 'F'};
        elfBytes.resize(256, 0);
        WriteFile(elf, elfBytes);
        auto r2 = scanner.ScanFile(elf.string());
        Expect(HasFinding(r2, "heur.exec_in_data"),
               "ELF 偽裝 .ttf 命中 exec_in_data");
    }

    // ---- [啟發] 內嵌 PE payload ----
    printf("-- 內嵌 PE --\n");
    {
        fs::path polyglot = dir / "screenshot.png";
        std::vector<uint8_t> buf = MakePNG(512);
        auto pe = MakePE();
        std::memcpy(buf.data() + 256, pe.data(), pe.size());  // offset 256 內嵌
        WriteFile(polyglot, buf);
        auto r = scanner.ScanFile(polyglot.string());
        Expect(HasFinding(r, "heur.embedded_pe"),
               "PNG offset 256 內嵌 PE 命中 embedded_pe");
    }

    // ---- [啟發] 腳本危險 API ----
    printf("-- 腳本內容掃描 --\n");
    {
        fs::path evilLua = dir / "mod.lua";
        WriteFile(evilLua, "os.execute('rm -rf /')\n");
        auto r = scanner.ScanFile(evilLua.string());
        Expect(r.verdict == ScanVerdict::Suspicious &&
               HasFinding(r, "heur.script"),
               "os.execute + rm -rf 判定 Suspicious");

        fs::path ps = dir / "setup.ps1";
        WriteFile(ps, "powershell -EncodedCommand AAAA\n");
        auto r2 = scanner.ScanFile(ps.string());
        Expect(HasFinding(r2, "heur.script"),
               "powershell -EncodedCommand 命中");

        fs::path okLua = dir / "safe.lua";
        WriteFile(okLua, "print('hello world')\n");
        auto r3 = scanner.ScanFile(okLua.string());
        Expect(r3.verdict == ScanVerdict::Clean && r3.findings.empty(),
               "乾淨 lua 判定 Clean");
    }

    // ---- [邊界] 乾淨檔 / 不存在檔 ----
    printf("-- 邊界情況 --\n");
    {
        fs::path clean = dir / "readme.txt";
        WriteFile(clean, "just a normal text file\n");
        auto r = scanner.ScanFile(clean.string());
        Expect(r.verdict == ScanVerdict::Clean, "乾淨文字檔 → Clean");

        auto r2 = scanner.ScanFile((dir / "nonexistent.bin").string());
        Expect(r2.verdict == ScanVerdict::Error,
               "不存在的檔案 → Error");
    }

    // ---- [流程] 目錄掃描彙總 ----
    printf("-- ScanDirectory --\n");
    {
        auto sum = scanner.ScanDirectory(dir.string(), true);
        Expect(sum.results.size() >= 8, "目錄掃描涵蓋全部檔案",
               std::to_string(sum.results.size()) + " files");
        Expect(sum.suspicious >= 3,
               "彙總 suspicious 計數", std::to_string(sum.suspicious));
        Expect(sum.clean >= 2, "彙總 clean 計數",
               std::to_string(sum.clean));
        int counted = sum.clean + sum.suspicious + sum.malicious + sum.errors;
        Expect(counted == (int)sum.results.size(), "計數與結果一致");
    }

    // ---- [流程] 隔離 ----
    printf("-- QuarantineFile --\n");
    {
        fs::path victim = dir / "virus_drop.lua";
        WriteFile(victim, "os.execute('cmd.exe /c del /q *')\n");
        auto r = scanner.ScanFile(victim.string());

        std::string err;
        bool ok = scanner.QuarantineFile(victim.string(),
                                         quarantine.string(), &r, &err);
        Expect(ok, "隔離執行成功", err);
        Expect(!fs::exists(victim), "原檔已移走");
        fs::path manifest = quarantine / (r.sha256 + ".json");
        Expect(fs::exists(manifest), "清單檔存在");
        fs::path quarantined = quarantine / (r.sha256 + ".quarantine");
        Expect(fs::exists(quarantined), "隔離檔存在");

        // 清單內容可解析且記錄原路徑
        std::ifstream mf(manifest);
        std::string text((std::istreambuf_iterator<char>(mf)),
                         std::istreambuf_iterator<char>());
        Expect(text.find("original_path") != std::string::npos &&
               text.find("virus_drop.lua") != std::string::npos,
               "清單記錄原路徑");
    }

    // 清理測試殘留
    fs::remove_all(dir, ec);
    fs::remove_all(quarantine, ec);

    printf("\n=== 結果: %d PASS / %d FAIL ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
