#pragma once

/**
 * Potato Engine Content Scanner — 檔案內容掃毒 / 惡意內容偵測
 *
 * 用途：遊戲載入玩家上傳/下載的內容（地圖、模組、存檔、素材）之前，
 * 先對檔案做靜態掃描，攔截已知惡意雜湊、偽裝成資料的可執行檔、
 * 雙重副檔名、內嵌 PE payload、以及腳本中的危險 API 呼叫。
 *
 * 偵測層級：
 * - 已知惡意：SHA-256 黑名單、位元組特徵庫（含內建 EICAR 測試特徵）
 * - 啟發式：雙重副檔名、資料副檔名藏執行檔頭、資料檔內嵌 MZ/PE、
 *           腳本檔案中的危險 API 字串
 *
 * 設計原則（同 SecuritySystem）：
 * - 自包含：僅依賴標準庫 + SecuritySystem.h 的 SHA-256 + JsonParser.h
 * - 只偵測、不移除：判定結果由呼叫端決定處置（拒載、隔離、上報）
 * - 啟發式會有誤報：Suspicious 是「需要人工/策略複查」而非確診
 */

#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <functional>
#include <cstdint>
#include <cstddef>

namespace Potato {
namespace Security {

// 掃描判定
enum class ScanVerdict {
    Clean,       // 無發現
    Suspicious,  // 啟發式命中（誤報可能，建議隔離複查）
    Malicious,   // 命中已知惡意特徵/雜湊
    Error        // 無法讀取/掃描
};

enum class ScanSeverity {
    Info,
    Low,
    Medium,
    High,
    Critical
};

// 單筆發現
struct ScanFinding {
    std::string ruleId;       // 規則識別碼,如 "sig.eicar" / "heur.double_ext"
    std::string description;  // 人類可讀說明
    ScanSeverity severity = ScanSeverity::Info;
};

// 單檔掃描結果
struct FileScanResult {
    std::string path;
    std::string sha256;                        // 小寫 hex；讀取失敗時為空
    uint64_t size = 0;
    ScanVerdict verdict = ScanVerdict::Clean;
    std::vector<ScanFinding> findings;
};

// 目錄掃描彙總
struct ScanSummary {
    std::vector<FileScanResult> results;
    int clean = 0, suspicious = 0, malicious = 0, errors = 0;
};

// 掃描進度/發現回呼（每掃完一個檔案呼叫一次）
using ScanCallback = std::function<void(const FileScanResult&)>;

// ============================================================================
// ContentScanner
// ============================================================================
class ContentScanner {
public:
    ContentScanner();

    // ---- 特徵庫 ----
    // 加入單筆 SHA-256 黑名單（64 字元 hex，大小寫不拘）
    void AddBlacklistedHash(const std::string& sha256Hex);
    // 加入位元組特徵：hex 字串（"4d5a"）代表檔案內容中要搜尋的位元序列
    void AddByteSignature(const std::string& ruleId, const std::string& hexPattern,
                          const std::string& description);
    // 從 JSON 檔載入特徵庫：
    // {"hashes": ["<sha256>", ...],
    //  "patterns": [{"id": "...", "hex": "4d5a...", "description": "..."}]}
    // 回傳成功載入的條目數；-1 表示檔案/格式錯誤
    int LoadSignatureDB(const std::string& jsonPath);
    size_t SignatureCount() const;

    // ---- 掃描 ----
    // 掃描單一檔案；content 掃描上限由 SetMaxScanBytes 控制
    // （超過部分不做特徵比對，但仍會計算完整 SHA-256 並記一筆 Info）
    FileScanResult ScanFile(const std::string& filePath) const;
    // 掃描記憶體緩衝區——供「先掃後寫」管線：下載完成的 bytes 在落盤前
    // 即可判定。displayName 用於副檔名相關啟發式與結果顯示。
    FileScanResult ScanBuffer(const void* data, size_t size,
                              const std::string& displayName) const;
    // 掃描目錄；recursive 深入子目錄。cb 每掃完一檔觸發一次（可為空）
    ScanSummary ScanDirectory(const std::string& dirPath, bool recursive = true,
                              ScanCallback cb = nullptr) const;

    // ---- 隔離 ----
    // 將檔案移入 quarantineDir（改名為 <sha256>.quarantine），
    // 並寫出 <sha256>.json 清單（原路徑/雜湊/發現/時間戳）。
    // result 可為 nullptr（會自行先掃描）。失敗回 false 並填 err。
    bool QuarantineFile(const std::string& filePath, const std::string& quarantineDir,
                        const FileScanResult* result, std::string* err) const;

    // ---- 設定 ----
    void SetMaxScanBytes(uint64_t bytes) { maxScanBytes = bytes; }
    void SetDetectEicar(bool on) { detectEicar = on; }
    void SetDetectDoubleExtension(bool on) { detectDoubleExt = on; }
    void SetDetectExecutableInData(bool on) { detectExecInData = on; }
    void SetDetectEmbeddedPE(bool on) { detectEmbeddedPE = on; }
    void SetDetectDangerousScript(bool on) { detectDangerScript = on; }

private:
    struct ByteSig {
        std::string ruleId;
        std::string description;
        std::vector<uint8_t> bytes;
    };

    // ScanFile/ScanBuffer 共用的內容檢查鏈
    void RunContentChecks(FileScanResult& r,
                          const std::vector<uint8_t>& content) const;

    // 各啟發式檢查，命中時 push finding
    void CheckFilename(const std::string& filePath, FileScanResult& r) const;
    void CheckMagicVsExtension(const std::string& filePath,
                               const std::vector<uint8_t>& head,
                               FileScanResult& r) const;
    void CheckSignatures(const std::vector<uint8_t>& content,
                         FileScanResult& r) const;
    void CheckEmbeddedPE(const std::string& filePath,
                         const std::vector<uint8_t>& content,
                         FileScanResult& r) const;
    void CheckScriptContent(const std::string& filePath,
                            const std::string& text,
                            FileScanResult& r) const;
    void FinalizeVerdict(FileScanResult& r) const;

    std::unordered_set<std::string> hashBlacklist;   // 小寫 sha256 hex
    std::vector<ByteSig> byteSigs;
    uint64_t maxScanBytes = 32ull * 1024 * 1024;     // 內容掃描上限
    bool detectEicar = true;
    bool detectDoubleExt = true;
    bool detectExecInData = true;
    bool detectEmbeddedPE = true;
    bool detectDangerScript = true;
};

// 判定/嚴重度 → 可讀字串（供 log）
const char* ScanVerdictToString(ScanVerdict v);
const char* ScanSeverityToString(ScanSeverity s);

} // namespace Security
} // namespace Potato
