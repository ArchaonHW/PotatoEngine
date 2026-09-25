/**
 * IntegrityManifest 實作——SHA-256 檔案完整性清單（可選 HMAC 簽章）
 */

#include "Security/IntegrityManifest.h"
#include "Security/SecuritySystem.h"   // ComputeFileSHA256 / ComputeHMACSHA256
#include "Serialization/JsonParser.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace Potato {
namespace Security {

namespace {

struct Entry {
    std::string rel;
    std::string sha256;
    uint64_t size = 0;
};

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

bool HasPrefix(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() &&
           s.compare(0, prefix.size(), prefix) == 0;
}

// 規範化清單內容：每筆 "path\x00sha256\x00size\n"，呼叫端需先依 path 排序。
// 與 JSON 排版無關——HMAC 涵蓋語意內容而非格式
std::string CanonicalEntries(const std::vector<Entry>& entries) {
    std::string out;
    for (const auto& e : entries) {
        out += e.rel;
        out += '\x00';
        out += e.sha256;
        out += '\x00';
        out += std::to_string(e.size);
        out += '\n';
    }
    return out;
}

// 常數時間 hex 比對（防 timing oracle；雜湊比對本身不敏感但保持一致紀律）
bool ConstantTimeEqual(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    uint8_t diff = 0;
    for (size_t i = 0; i < a.size(); ++i)
        diff |= static_cast<uint8_t>(a[i] ^ b[i]);
    return diff == 0;
}

void SortEntries(std::vector<Entry>& entries) {
    std::sort(entries.begin(), entries.end(),
              [](const Entry& a, const Entry& b) { return a.rel < b.rel; });
}

} // anonymous namespace

bool GenerateIntegrityManifest(
    const std::string& dirPath,
    const std::string& outJsonPath,
    const std::vector<std::string>& excludeRelPrefixes,
    const void* hmacKey, size_t hmacKeyLen) {
    namespace fs = std::filesystem;
    std::error_code ec;

    const fs::path base = fs::absolute(dirPath, ec);
    if (ec || !fs::is_directory(base, ec)) return false;

    // manifest 輸出檔本身若在掃描範圍內要排除（不可能雜湊自己）
    std::string outAbs;
    {
        fs::path op = fs::absolute(outJsonPath, ec);
        if (!ec) outAbs = op.lexically_normal().generic_string();
    }

    std::vector<Entry> entries;
    for (fs::recursive_directory_iterator it(base, ec), end;
         !ec && it != end; it.increment(ec)) {
        std::error_code eec;
        if (!it->is_regular_file(eec)) continue;

        const fs::path& p = it->path();
        std::string rel = fs::relative(p, base, eec).generic_string();
        if (eec || rel.empty()) continue;

        bool excluded = false;
        for (const auto& pre : excludeRelPrefixes) {
            if (HasPrefix(rel, pre)) { excluded = true; break; }
        }
        if (excluded) continue;

        if (!outAbs.empty() &&
            p.lexically_normal().generic_string() == outAbs) {
            continue;
        }

        std::string hash = ComputeFileSHA256(p.string());
        if (hash.empty()) continue;  // 讀不到的檔案不進清單

        const uint64_t sz = fs::file_size(p, eec);
        if (eec) continue;

        entries.push_back({std::move(rel), std::move(hash), sz});
    }
    SortEntries(entries);

    std::ostringstream json;
    json << "{\n  \"version\": 1,\n  \"files\": [";
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        json << (i ? ",\n" : "\n");
        json << "    {\"path\": \"" << JsonEscapeStr(e.rel)
             << "\", \"sha256\": \"" << e.sha256
             << "\", \"size\": " << e.size << "}";
    }
    json << (entries.empty() ? "]" : "\n  ]");

    if (hmacKey && hmacKeyLen > 0) {
        const std::string canonical = CanonicalEntries(entries);
        json << ",\n  \"hmac\": \""
             << ComputeHMACSHA256(hmacKey, hmacKeyLen,
                                  canonical.data(), canonical.size())
             << "\"";
    }
    json << "\n}\n";

    std::ofstream out(outJsonPath, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << json.str();
    return out.good();
}

ManifestVerifyResult VerifyIntegrityManifest(const std::string& manifestPath,
                                             const std::string& baseDir,
                                             const void* hmacKey,
                                             size_t hmacKeyLen) {
    namespace fs = std::filesystem;
    ManifestVerifyResult res;

    std::ifstream f(manifestPath, std::ios::binary);
    if (!f) {
        res.manifestError = true;
        res.failures.push_back(manifestPath + " (manifest unreadable)");
        return res;
    }
    std::stringstream ss;
    ss << f.rdbuf();

    JsonValue root;
    if (!JsonValue::ParseOk(ss.str(), root)) {
        res.manifestError = true;
        res.failures.push_back(manifestPath + " (manifest parse error)");
        return res;
    }

    // ---- HMAC 金鑰策略（fail-closed 雙向）----
    const std::string storedHmac = root["hmac"].AsString();
    const bool hasHmac = !storedHmac.empty();
    const bool hasKey = hmacKey && hmacKeyLen > 0;
    if (hasHmac && !hasKey) {
        res.manifestError = true;
        res.failures.push_back(manifestPath + " (signed manifest, no key)");
        return res;
    }
    if (!hasHmac && hasKey) {
        res.manifestError = true;
        res.failures.push_back(manifestPath + " (unsigned manifest)");
        return res;
    }

    std::vector<Entry> entries;
    for (const auto& e : root["files"].AsArray()) {
        const std::string rel = e["path"].AsString();
        const std::string hash = e["sha256"].AsString();
        if (rel.empty() || hash.empty()) continue;
        Entry ent;
        ent.rel = rel;
        ent.sha256 = hash;
        ent.size = static_cast<uint64_t>(e["size"].AsNumber(0));
        entries.push_back(std::move(ent));
    }

    if (hasHmac) {
        SortEntries(entries);
        const std::string canonical = CanonicalEntries(entries);
        const std::string actual = ComputeHMACSHA256(
            hmacKey, hmacKeyLen, canonical.data(), canonical.size());
        if (!ConstantTimeEqual(actual, storedHmac)) {
            res.manifestError = true;
            res.failures.push_back(manifestPath + " (hmac mismatch)");
            return res;
        }
    }

    const fs::path base = fs::path(baseDir);
    for (const auto& ent : entries) {
        // 防禦：清單路徑不得逃出 baseDir（拒絕絕對路徑與 ..）
        const fs::path relPath = fs::path(ent.rel);
        if (relPath.is_absolute() ||
            ent.rel.find("..") != std::string::npos) {
            ++res.mismatch;
            res.failures.push_back(ent.rel + " (unsafe path)");
            continue;
        }

        const fs::path full = base / relPath;
        std::error_code ec;
        if (!fs::exists(full, ec)) {
            ++res.missing;
            res.failures.push_back(ent.rel + " (missing)");
            continue;
        }

        std::string actual = ComputeFileSHA256(full.string());
        if (actual.empty()) {
            ++res.unreadable;
            res.failures.push_back(ent.rel + " (unreadable)");
        } else if (!ConstantTimeEqual(actual, ent.sha256)) {
            ++res.mismatch;
            res.failures.push_back(ent.rel + " (mismatch)");
        } else {
            ++res.ok;
        }
    }
    return res;
}

} // namespace Security
} // namespace Potato
