#pragma once

/**
 * AuditLedger — 防竄改稽核帳本（hash 鏈 append-only 揭露層）
 *
 * 借用「區塊鏈揭露層」的核心紀律，套用到引擎事件稽核：
 *   - 只錨 hash 與摘要：帳本內永遠不存事件 payload 原文，
 *     只存其 SHA-256。要查證時由呼叫端出示原文重算比對
 *   - 每筆記錄 recordHash = SHA-256(所有欄位 + prevHash)，
 *     首尾相接成鏈——刪改任何一筆歷史記錄都會讓鏈斷裂
 *   - 零容忍調節：Verify() 逐筆重算，任何一筆不符即失敗，
 *     並回報第一筆斷點位置（不容忍部分一致）
 *   - NOT NULL 紀律：category/eventType/provenance 空白拒收；
 *     provenance 對應揭露案的 consent_ref——每筆記錄必須
 *     答得出「資料從哪來」
 *   - 禁止欄位：struct 根本沒有 payload 原文欄位，
 *     也沒有金鑰/身分欄位——不能存的東西就不要有欄位
 *   - 分層揭露：ExportJson(L0/L1/L2) 對應
 *     內部完整 / 去識別 / 公開錨定三種揭露層級
 *
 * 用途：安全違規證據鏈（作弊玩家刪不掉自己被偵測的紀錄）、
 *       存檔/經濟系統操作稽核、授權事件軌跡。
 *
 * 誠實標註：
 * - 帳本檔案在本機仍可被整個刪除——防的是「修改歷史不被發現」,
 *   不是「防止刪除」。整檔消失或重頭重建會改變 HeadHash，
 *   因此 HeadHash 應定期外部錨定（回傳伺服器/寫入存檔簽章）
 * - 非共識機制：這是單機寫入者的 tamper-evident log，
 *   不是分散式帳本；不發幣不碰錢包
 */

#include <string>
#include <vector>
#include <mutex>
#include <cstdint>
#include <cstddef>

namespace Potato {
namespace Security {

// 揭露層級：對誰揭露到哪種粒度
enum class DisclosureLayer {
    L0_Internal,      // 內部完整記錄（含 provenance）
    L1_Deidentified,  // 對外去識別：保留 hash/類別,剝除 provenance
    L2_Public         // 公開錨定：只有 head hash 與筆數
};

// 單筆帳本記錄（所有欄位皆為摘要,無原文）
struct AuditRecord {
    uint64_t    seq = 0;            // 單調序號，1 起
    int64_t     timestampUtcMs = 0; // UTC 毫秒
    std::string recordId;           // 隨機 UUID（冪等鍵/外部引用用）
    std::string category;           // 必填，如 "violation" / "save" / "economy"
    std::string eventType;          // 必填，如 "DebuggerDetected"
    std::string payloadHash;        // 必填，64 hex——payload 的 SHA-256,非原文
    std::string provenance;         // 必填，資料來源/同意參照（consent_ref 對應）
    std::string prevHash;           // 前一筆 recordHash；首筆為 64 個 '0'
    std::string recordHash;         // SHA-256(規範化欄位串 + prevHash)
};

// 鏈驗證結果（零容忍：任何一筆不符即不通過）
struct LedgerVerifyResult {
    uint64_t checked = 0;       // 驗證過的筆數
    uint64_t firstBadSeq = 0;   // 第一筆斷點的 seq；0 = 全部通過
    std::string reason;         // 斷點原因
    bool Passed() const { return firstBadSeq == 0; }
};

class AuditLedger {
public:
    // 追加一筆記錄；payload 由內部雜湊,原文不落帳。
    // category/eventType/provenance 任一為空 → 拒收回 false。
    // out 非空時寫回入帳後的完整記錄（含 seq/recordHash/prevHash）。
    bool Append(const std::string& category,
                const std::string& eventType,
                const void* payload, size_t payloadLen,
                const std::string& provenance,
                AuditRecord* out = nullptr);
    // 字串 payload 便捷版
    bool Append(const std::string& category,
                const std::string& eventType,
                const std::string& payload,
                const std::string& provenance,
                AuditRecord* out = nullptr);
    // 呼叫端已持有 payload 雜湊的版本（64 hex，否則拒收）
    bool AppendHash(const std::string& category,
                    const std::string& eventType,
                    const std::string& payloadHashHex,
                    const std::string& provenance,
                    AuditRecord* out = nullptr);

    // 逐筆重算整條鏈。空帳本視為通過。
    LedgerVerifyResult Verify() const;

    // 鏈頭 hash——即「錨定值」。定期取出外部留存,
    // 日後任何歷史竄改都會讓 HeadHash 對不上。空帳本回 ""。
    std::string HeadHash() const;
    size_t Size() const;
    const std::vector<AuditRecord>& Records() const { return records; }

    // 分層揭露匯出（JSON 字串）
    std::string ExportJson(DisclosureLayer layer) const;

    // 持久化：JSONL（一行一筆完整記錄）。
    // Load 會重建記錄並先驗證鏈——驗證失敗回 false 且帳本不載入。
    bool SaveToFile(const std::string& filePath) const;
    bool LoadFromFile(const std::string& filePath,
                      LedgerVerifyResult* verifyOut = nullptr);

    // 計算單筆記錄的規範化雜湊（公開以便外部稽核者重算）
    static std::string ComputeRecordHash(const AuditRecord& rec);
    // 首筆記錄的 prevHash 定值
    static const char* GenesisHash();  // 64 個 '0'

private:
    // 共用的記錄建構：欄位檢查 → 序號/時間/UUID → 鏈結雜湊
    bool AppendInternal(const std::string& category,
                        const std::string& eventType,
                        const std::string& payloadHashHex,
                        const std::string& provenance,
                        AuditRecord* out);

    std::vector<AuditRecord> records;
    mutable std::mutex mutex;
};

} // namespace Security
} // namespace Potato
