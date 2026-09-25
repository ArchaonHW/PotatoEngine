/**
 * AuditLedger 實作——hash 鏈 append-only 稽核帳本
 */

#include "Security/AuditLedger.h"
#include "Security/SecuritySystem.h"   // ComputeSHA256 / GenerateRandomBytes
#include "Serialization/JsonParser.h"

#include <chrono>
#include <fstream>
#include <sstream>
#include <cstdio>

namespace Potato {
namespace Security {

namespace {

// 欄位長度前綴串接：消除 "ab|c" 與 "a|bc" 的拼接歧義
void Lp(std::string& out, const std::string& v) {
    out += std::to_string(v.size());
    out += ':';
    out += v;
    out += '|';
}

bool IsHex64(const std::string& s) {
    if (s.size() != 64) return false;
    for (char c : s) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

std::string JsonEscapeStr(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c; break;
        }
    }
    return out;
}

// 隨機 UUID v4（冪等鍵/外部引用編號）
std::string MakeUuid() {
    std::vector<uint8_t> b = GenerateRandomBytes(16);
    if (b.size() != 16) return "00000000-0000-4000-8000-000000000000";
    b[6] = static_cast<uint8_t>((b[6] & 0x0F) | 0x40);  // version 4
    b[8] = static_cast<uint8_t>((b[8] & 0x3F) | 0x80);  // variant 10
    char buf[37];
    std::snprintf(buf, sizeof(buf),
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
        b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    return std::string(buf);
}

int64_t NowUtcMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// 記錄 → 一行 JSON（layer 控制是否含 provenance）
std::string RecordToJson(const AuditRecord& r, bool withProvenance) {
    std::ostringstream j;
    j << "{\"seq\":" << r.seq
      << ",\"ts\":" << r.timestampUtcMs
      << ",\"id\":\"" << JsonEscapeStr(r.recordId) << "\""
      << ",\"cat\":\"" << JsonEscapeStr(r.category) << "\""
      << ",\"ev\":\"" << JsonEscapeStr(r.eventType) << "\""
      << ",\"ph\":\"" << r.payloadHash << "\"";
    if (withProvenance)
        j << ",\"prov\":\"" << JsonEscapeStr(r.provenance) << "\"";
    j << ",\"prev\":\"" << r.prevHash << "\""
      << ",\"rh\":\"" << r.recordHash << "\"}";
    return j.str();
}

// 對記錄向量做鏈驗證（Verify 與 LoadFromFile 共用）
LedgerVerifyResult VerifyVector(const std::vector<AuditRecord>& recs) {
    LedgerVerifyResult res;
    std::string prev = AuditLedger::GenesisHash();
    for (size_t i = 0; i < recs.size(); ++i) {
        const AuditRecord& r = recs[i];
        ++res.checked;
        auto fail = [&](const char* why) {
            res.firstBadSeq = r.seq;
            res.reason = why;
        };
        if (r.seq != i + 1) { fail("seq gap/dup"); return res; }
        if (r.category.empty() || r.eventType.empty() ||
            r.provenance.empty() || !IsHex64(r.payloadHash)) {
            fail("required field missing/invalid"); return res;
        }
        if (r.prevHash != prev) { fail("broken chain link"); return res; }
        if (AuditLedger::ComputeRecordHash(r) != r.recordHash) {
            fail("record hash mismatch"); return res;
        }
        prev = r.recordHash;
    }
    return res;
}

} // anonymous namespace

const char* AuditLedger::GenesisHash() {
    return "0000000000000000000000000000000000000000000000000000000000000000";
}

std::string AuditLedger::ComputeRecordHash(const AuditRecord& rec) {
    // 規範化前置影像：版本標記 + 長度前綴欄位（含 prevHash 鏈結）
    std::string pre = "PEAL1|";
    Lp(pre, std::to_string(rec.seq));
    Lp(pre, std::to_string(rec.timestampUtcMs));
    Lp(pre, rec.recordId);
    Lp(pre, rec.category);
    Lp(pre, rec.eventType);
    Lp(pre, rec.payloadHash);
    Lp(pre, rec.provenance);
    Lp(pre, rec.prevHash);
    return ComputeSHA256(pre.data(), pre.size());
}

bool AuditLedger::Append(const std::string& category,
                         const std::string& eventType,
                         const void* payload, size_t payloadLen,
                         const std::string& provenance,
                         AuditRecord* out) {
    if (!payload && payloadLen > 0) return false;
    const std::string ph = ComputeSHA256(payload ? payload : "", payloadLen);
    return AppendInternal(category, eventType, ph, provenance, out);
}

bool AuditLedger::Append(const std::string& category,
                         const std::string& eventType,
                         const std::string& payload,
                         const std::string& provenance,
                         AuditRecord* out) {
    return Append(category, eventType, payload.data(), payload.size(),
                  provenance, out);
}

bool AuditLedger::AppendHash(const std::string& category,
                             const std::string& eventType,
                             const std::string& payloadHashHex,
                             const std::string& provenance,
                             AuditRecord* out) {
    if (!IsHex64(payloadHashHex)) return false;
    return AppendInternal(category, eventType, payloadHashHex, provenance, out);
}

bool AuditLedger::AppendInternal(const std::string& category,
                                 const std::string& eventType,
                                 const std::string& payloadHashHex,
                                 const std::string& provenance,
                                 AuditRecord* out) {
    // NOT NULL 紀律：來源不明/無類別的記錄不入帳
    if (category.empty() || eventType.empty() || provenance.empty()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex);

    AuditRecord rec;
    rec.seq = static_cast<uint64_t>(records.size()) + 1;
    rec.timestampUtcMs = NowUtcMs();
    rec.recordId = MakeUuid();
    rec.category = category;
    rec.eventType = eventType;
    rec.payloadHash = payloadHashHex;
    rec.provenance = provenance;
    rec.prevHash = records.empty() ? GenesisHash() : records.back().recordHash;
    rec.recordHash = ComputeRecordHash(rec);

    records.push_back(std::move(rec));
    if (out) *out = records.back();
    return true;
}

LedgerVerifyResult AuditLedger::Verify() const {
    std::lock_guard<std::mutex> lock(mutex);
    return VerifyVector(records);
}

std::string AuditLedger::HeadHash() const {
    std::lock_guard<std::mutex> lock(mutex);
    return records.empty() ? std::string() : records.back().recordHash;
}

size_t AuditLedger::Size() const {
    std::lock_guard<std::mutex> lock(mutex);
    return records.size();
}

std::string AuditLedger::ExportJson(DisclosureLayer layer) const {
    std::lock_guard<std::mutex> lock(mutex);
    const std::string head = records.empty() ? "" : records.back().recordHash;

    if (layer == DisclosureLayer::L2_Public) {
        // 公開錨定層：只給筆數與鏈頭——足以讓外部驗「帳本沒被換掉」,
        // 但不揭露任何單筆內容
        std::ostringstream j;
        j << "{\"layer\":\"L2\",\"count\":" << records.size()
          << ",\"head\":\"" << head << "\"}";
        return j.str();
    }

    const bool full = (layer == DisclosureLayer::L0_Internal);
    std::ostringstream j;
    j << "{\"layer\":\"" << (full ? "L0" : "L1") << "\",\"count\":"
      << records.size() << ",\"head\":\"" << head << "\",\"records\":[";
    for (size_t i = 0; i < records.size(); ++i) {
        j << (i ? ",\n" : "\n") << "  " << RecordToJson(records[i], full);
    }
    j << (records.empty() ? "]}" : "\n]}");
    return j.str();
}

bool AuditLedger::SaveToFile(const std::string& filePath) const {
    std::lock_guard<std::mutex> lock(mutex);
    std::ofstream out(filePath, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    for (const auto& r : records) {
        out << RecordToJson(r, true) << '\n';
    }
    return out.good();
}

bool AuditLedger::LoadFromFile(const std::string& filePath,
                               LedgerVerifyResult* verifyOut) {
    std::ifstream f(filePath, std::ios::binary);
    if (!f) {
        if (verifyOut) {
            verifyOut->checked = 0;
            verifyOut->firstBadSeq = 0;
            verifyOut->reason = "file unreadable";  // 回傳值為準:false=未載入
        }
        return false;
    }

    std::vector<AuditRecord> loaded;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        JsonValue v;
        if (!JsonValue::ParseOk(line, v) || !v.IsObject()) {
            if (verifyOut) { verifyOut->reason = "line parse error"; }
            return false;
        }
        AuditRecord r;
        r.seq = static_cast<uint64_t>(v["seq"].AsNumber());
        r.timestampUtcMs = static_cast<int64_t>(v["ts"].AsNumber());
        r.recordId = v["id"].AsString();
        r.category = v["cat"].AsString();
        r.eventType = v["ev"].AsString();
        r.payloadHash = v["ph"].AsString();
        r.provenance = v["prov"].AsString();
        r.prevHash = v["prev"].AsString();
        r.recordHash = v["rh"].AsString();
        loaded.push_back(std::move(r));
    }

    // 先驗證再收帳：壞鏈不載入（fail-closed）
    LedgerVerifyResult res = VerifyVector(loaded);
    if (verifyOut) *verifyOut = res;
    if (!res.Passed()) return false;

    std::lock_guard<std::mutex> lock(mutex);
    records = std::move(loaded);
    return true;
}

} // namespace Security
} // namespace Potato
