/**
 * ContentScanner 實作——檔案內容掃毒 / 惡意內容偵測
 *
 * 全部檢查為靜態唯讀：不執行被掃描的內容、不修改原檔。
 * 隔離是唯一會動到檔案系統的操作（rename → quarantineDir + 寫清單）。
 */

#include "Security/ContentScanner.h"
#include "Security/SecuritySystem.h"   // ComputeFileSHA256
#include "Serialization/JsonParser.h"  // 特徵庫 JSON

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Potato {
namespace Security {

namespace {

// ---- 副檔名集合（小寫，不含點）----

// 可執行/可自動執行的副檔名：作為雙重副檔名的「外層」即為可疑
const std::unordered_set<std::string>& ExecutableExts() {
    static const std::unordered_set<std::string> s = {
        "exe", "dll", "com", "scr", "bat", "cmd", "ps1", "vbs", "vbe",
        "js", "jse", "wsf", "wsh", "msi", "msp", "hta", "cpl", "sys",
        "drv", "ocx", "pif", "lnk", "sh", "run", "jar", "reg"
    };
    return s;
}

// 資料/素材副檔名：這些檔案不應含執行檔頭；也作為雙重副檔名的「內層誘餌」
const std::unordered_set<std::string>& DataExts() {
    static const std::unordered_set<std::string> s = {
        "png", "jpg", "jpeg", "gif", "bmp", "tga", "webp", "ico", "svg",
        "wav", "mp3", "ogg", "flac", "txt", "md", "json", "xml", "csv",
        "yaml", "yml", "ini", "cfg", "pak", "vrm", "gltf", "glb", "fbx",
        "obj", "pdf", "doc", "docx", "xls", "xlsx", "ppt", "pptx", "mp4",
        "avi", "mov", "mkv", "ttf", "otf", "woff", "woff2", "pnn", "dds",
        "zip", "7z", "rar", "gz", "tar"
    };
    return s;
}

// 腳本副檔名：內容需做危險 API 字串掃描
const std::unordered_set<std::string>& ScriptExts() {
    static const std::unordered_set<std::string> s = {
        "lua", "js", "py", "ps1", "bat", "cmd", "vbs", "vbe",
        "sh", "rb", "pl", "php", "wsf", "wsh"
    };
    return s;
}

struct ScriptPattern {
    const char* needle;      // 小寫子字串
    ScanSeverity severity;
    const char* what;
};

// 腳本危險 API / LOLBin / 破壞性指令模式（小寫子字串比對）
const ScriptPattern kScriptPatterns[] = {
    // 破壞性 / 系統修改 — High
    {"rm -rf",              ScanSeverity::High,     "遞迴強制刪除指令"},
    {"mkfs.",               ScanSeverity::High,     "磁碟格式化指令"},
    {":(){ :|:& };:",       ScanSeverity::High,     "fork bomb"},
    {"format c:",           ScanSeverity::High,     "格式化系統碟"},
    {"reg delete",          ScanSeverity::High,     "登錄檔刪除"},
    {"bcdedit",             ScanSeverity::High,     "開機設定修改"},
    // 程式碼執行 / 下載執行 — High
    {"os.execute",          ScanSeverity::High,     "Lua os.execute 外部命令"},
    {"io.popen",            ScanSeverity::High,     "Lua io.popen 外部命令"},
    {"loadstring(",         ScanSeverity::High,     "Lua loadstring 動態執行"},
    {"os.system",           ScanSeverity::High,     "Python os.system 外部命令"},
    {"subprocess",          ScanSeverity::Medium,   "子行程建立"},
    {"invoke-expression",   ScanSeverity::High,     "PowerShell 動態執行"},
    {"downloadstring",      ScanSeverity::High,     "遠端下載字串執行"},
    {"downloadfile(",       ScanSeverity::High,     "遠端下載檔案"},
    {"certutil -urlcache",  ScanSeverity::High,     "certutil 下載 (LOLBin)"},
    {"certutil -decode",    ScanSeverity::High,     "certutil 解碼 payload (LOLBin)"},
    {"bitsadmin",           ScanSeverity::High,     "BITS 下載 (LOLBin)"},
    {"mshta ",              ScanSeverity::High,     "mshta 執行 HTA (LOLBin)"},
    {"wscript.shell",       ScanSeverity::High,     "WSH Shell 物件"},
    {"shell.application",   ScanSeverity::High,     "Shell.Application 物件"},
    {"powershell",          ScanSeverity::Medium,   "PowerShell 呼叫"},
    {"-encodedcommand",     ScanSeverity::High,     "PowerShell 編碼指令"},
    {"cmd.exe",             ScanSeverity::Medium,   "cmd 呼叫"},
    {"reg add",             ScanSeverity::Medium,   "登錄檔寫入"},
    // 記憶體注入 API（出現在腳本中極不尋常）— High
    {"virtualalloc",        ScanSeverity::High,     "VirtualAlloc（記憶體注入前兆）"},
    {"createremotethread",  ScanSeverity::High,     "CreateRemoteThread（行程注入）"},
    {"writeprocessmemory",  ScanSeverity::High,     "WriteProcessMemory（行程注入）"},
    // 動態求值 — Medium（混淆常用，誤報率較高）
    {"eval(",               ScanSeverity::Medium,   "eval 動態求值"},
    {"exec(",               ScanSeverity::Medium,   "exec 動態求值"},
    {"__import__",          ScanSeverity::Medium,   "Python 動態匯入"},
    {"createobject(",       ScanSeverity::Medium,   "COM 物件建立"},
    {"dofile(",             ScanSeverity::Medium,   "Lua dofile 載入外部腳本"},
    {"base64",              ScanSeverity::Low,      "Base64 編碼（可能為混淆 payload）"},
};

// EICAR 標準反病毒測試字串（業界通用,專門用來驗證掃描器有在運作）
const char kEicar[] =
    "X5O!P%@AP[4\\PZX54(P^)7CC)7}$EICAR-STANDARD-ANTIVIRUS-TEST-FILE!$H+H*";

std::string ToLower(std::string s) {
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// 取路徑最後一段的副檔名（小寫、不含點）；無副檔名回空字串
std::string ExtOf(const std::string& filename) {
    auto dot = filename.find_last_of('.');
    if (dot == std::string::npos || dot == filename.size() - 1) return "";
    return ToLower(filename.substr(dot + 1));
}

// 取「內層」副檔名：foo.png.exe → "png"；無內層回空字串
std::string InnerExtOf(const std::string& filename) {
    auto last = filename.find_last_of('.');
    if (last == std::string::npos || last == 0) return "";
    auto prev = filename.find_last_of('.', last - 1);
    if (prev == std::string::npos) return "";
    return ToLower(filename.substr(prev + 1, last - prev - 1));
}

// 在緩衝區中搜尋位元組序列（簡易 memmem）
bool ContainsBytes(const uint8_t* hay, size_t hayLen,
                   const uint8_t* needle, size_t needleLen) {
    if (needleLen == 0 || hayLen < needleLen) return false;
    const uint8_t* end = hay + (hayLen - needleLen);
    for (const uint8_t* p = hay; p <= end; ++p) {
        if (p[0] == needle[0] && std::memcmp(p, needle, needleLen) == 0)
            return true;
    }
    return false;
}

// 檢查 buf[off] 起是否為合法的 PE 檔頭（MZ + e_lfanew → "PE\0\0"）
bool IsPEAt(const uint8_t* buf, size_t size, size_t off) {
    if (off + 0x40 > size) return false;
    if (buf[off] != 'M' || buf[off + 1] != 'Z') return false;
    uint32_t peOff = static_cast<uint32_t>(buf[off + 0x3C])
                   | (static_cast<uint32_t>(buf[off + 0x3D]) << 8)
                   | (static_cast<uint32_t>(buf[off + 0x3E]) << 16)
                   | (static_cast<uint32_t>(buf[off + 0x3F]) << 24);
    size_t pe = off + peOff;
    if (pe + 4 > size) return false;
    return buf[pe] == 'P' && buf[pe + 1] == 'E' &&
           buf[pe + 2] == 0 && buf[pe + 3] == 0;
}

bool IsELFAt(const uint8_t* buf, size_t size, size_t off) {
    return off + 4 <= size && buf[off] == 0x7F && buf[off + 1] == 'E' &&
           buf[off + 2] == 'L' && buf[off + 3] == 'F';
}

bool IsMachOAt(const uint8_t* buf, size_t size, size_t off) {
    if (off + 4 > size) return false;
    uint32_t m = static_cast<uint32_t>(buf[off]) << 24 |
                 static_cast<uint32_t>(buf[off + 1]) << 16 |
                 static_cast<uint32_t>(buf[off + 2]) << 8 |
                 static_cast<uint32_t>(buf[off + 3]);
    return m == 0xFEEDFACE || m == 0xCEFAEDFE ||
           m == 0xFEEDFACF || m == 0xCFFAEDFE;
}

// "hex" → bytes；非法字元或奇數長度回空 vector
std::vector<uint8_t> HexToBytes(const std::string& hex) {
    std::vector<uint8_t> out;
    if (hex.size() % 2 != 0) return out;
    auto hv = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        int hi = hv(hex[i]), lo = hv(hex[i + 1]);
        if (hi < 0 || lo < 0) return {};
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return out;
}

std::string JsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char tmp[8];
                    std::snprintf(tmp, sizeof(tmp), "\\u%04x", c);
                    out += tmp;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

} // anonymous namespace

// ============================================================================
// ContentScanner
// ============================================================================

ContentScanner::ContentScanner() = default;  // EICAR 由 CheckSignatures 內建處理

void ContentScanner::AddBlacklistedHash(const std::string& sha256Hex) {
    std::string h = ToLower(sha256Hex);
    if (h.size() == 64) hashBlacklist.insert(h);
}

void ContentScanner::AddByteSignature(const std::string& ruleId,
                                      const std::string& hexPattern,
                                      const std::string& description) {
    auto bytes = HexToBytes(hexPattern);
    if (bytes.empty()) return;
    byteSigs.push_back({ruleId, description, std::move(bytes)});
}

int ContentScanner::LoadSignatureDB(const std::string& jsonPath) {
    std::ifstream f(jsonPath, std::ios::binary);
    if (!f) return -1;
    std::stringstream ss;
    ss << f.rdbuf();

    JsonValue root;
    if (!JsonValue::ParseOk(ss.str(), root)) return -1;

    int loaded = 0;
    const JsonValue& hashes = root["hashes"];
    if (hashes.type == JsonValue::Type::Array) {
        for (const auto& h : hashes.AsArray()) {
            if (h.type == JsonValue::Type::String) {
                AddBlacklistedHash(h.AsString());
                ++loaded;
            }
        }
    }
    const JsonValue& patterns = root["patterns"];
    if (patterns.type == JsonValue::Type::Array) {
        for (const auto& p : patterns.AsArray()) {
            const JsonValue& id = p["id"];
            const JsonValue& hex = p["hex"];
            if (id.type == JsonValue::Type::String &&
                hex.type == JsonValue::Type::String) {
                AddByteSignature(id.AsString(), hex.AsString(),
                                 p["description"].type == JsonValue::Type::String
                                     ? p["description"].AsString() : "");
                ++loaded;
            }
        }
    }
    return loaded;
}

size_t ContentScanner::SignatureCount() const {
    return hashBlacklist.size() + byteSigs.size();
}

FileScanResult ContentScanner::ScanFile(const std::string& filePath) const {
    FileScanResult r;
    r.path = filePath;

    // 1) 檔名啟發式（不需要讀檔即可判定）
    CheckFilename(filePath, r);

    // 2) 讀檔（上限 maxScanBytes；完整 SHA-256 仍由串流計算）
    std::ifstream f(filePath, std::ios::binary | std::ios::ate);
    if (!f) {
        r.verdict = ScanVerdict::Error;
        r.findings.push_back({"io.error", "無法開啟檔案", ScanSeverity::Info});
        return r;
    }
    std::streamoff fsize = f.tellg();
    if (fsize < 0) {
        r.verdict = ScanVerdict::Error;
        r.findings.push_back({"io.error", "無法取得檔案大小", ScanSeverity::Info});
        return r;
    }
    r.size = static_cast<uint64_t>(fsize);
    f.seekg(0);

    uint64_t toRead = std::min<uint64_t>(r.size, maxScanBytes);
    std::vector<uint8_t> content(static_cast<size_t>(toRead));
    if (toRead > 0)
        f.read(reinterpret_cast<char*>(content.data()),
               static_cast<std::streamsize>(toRead));
    f.close();

    if (r.size > maxScanBytes) {
        r.findings.push_back({"heur.oversize_skip",
            "檔案超過掃描上限,特徵比對僅覆蓋前段內容", ScanSeverity::Info});
    }

    // 3) SHA-256 黑名單
    r.sha256 = ComputeFileSHA256(filePath);
    if (!r.sha256.empty() && hashBlacklist.count(r.sha256)) {
        r.findings.push_back({"hash.blacklist",
            "SHA-256 命中已知惡意雜湊黑名單", ScanSeverity::Critical});
    }

    // 4) 魔數 vs 副檔名（資料檔不應是執行檔）
    if (detectExecInData)
        CheckMagicVsExtension(filePath, content, r);

    // 5) 位元組特徵庫 + EICAR
    CheckSignatures(content, r);

    // 6) 資料檔內嵌 PE（polyglot / appended payload）
    if (detectEmbeddedPE)
        CheckEmbeddedPE(filePath, content, r);

    // 7) 腳本內容掃描
    if (detectDangerScript) {
        std::string text(content.begin(), content.end());
        CheckScriptContent(filePath, text, r);
    }

    FinalizeVerdict(r);
    return r;
}

FileScanResult ContentScanner::ScanBuffer(const void* data, size_t size,
                                          const std::string& displayName) const {
    FileScanResult r;
    r.path = displayName;
    r.size = size;

    // 1) 檔名啟發式（displayName 扮演路徑角色）
    CheckFilename(displayName, r);

    // 2) 緩衝區（超過 maxScanBytes 的部分不進特徵比對）
    const auto* bytes = static_cast<const uint8_t*>(data);
    size_t scanLen = static_cast<size_t>(
        std::min<uint64_t>(size, maxScanBytes));
    std::vector<uint8_t> content(bytes, bytes + scanLen);
    if (size > maxScanBytes) {
        r.findings.push_back({"heur.oversize_skip",
            "緩衝區超過掃描上限,特徵比對僅覆蓋前段內容", ScanSeverity::Info});
    }

    // 3) SHA-256 黑名單（完整緩衝區）
    r.sha256 = ComputeSHA256(data, size);
    if (!r.sha256.empty() && hashBlacklist.count(r.sha256)) {
        r.findings.push_back({"hash.blacklist",
            "SHA-256 命中已知惡意雜湊黑名單", ScanSeverity::Critical});
    }

    // 4)~7) 與 ScanFile 相同的內容檢查鏈
    if (detectExecInData)
        CheckMagicVsExtension(displayName, content, r);
    CheckSignatures(content, r);
    if (detectEmbeddedPE)
        CheckEmbeddedPE(displayName, content, r);
    if (detectDangerScript) {
        std::string text(content.begin(), content.end());
        CheckScriptContent(displayName, text, r);
    }

    FinalizeVerdict(r);
    return r;
}

ScanSummary ContentScanner::ScanDirectory(const std::string& dirPath,
                                          bool recursive,
                                          ScanCallback cb) const {
    ScanSummary sum;
    std::error_code ec;
    namespace fs = std::filesystem;

    if (!fs::is_directory(dirPath, ec)) {
        FileScanResult r;
        r.path = dirPath;
        r.verdict = ScanVerdict::Error;
        r.findings.push_back({"io.error", "不是目錄或無法存取",
                              ScanSeverity::Info});
        sum.results.push_back(std::move(r));
        ++sum.errors;
        return sum;
    }

    auto handle = [&](const fs::directory_entry& e) {
        std::error_code eec;
        if (!e.is_regular_file(eec)) return;
        FileScanResult r = ScanFile(e.path().string());
        switch (r.verdict) {
            case ScanVerdict::Clean:      ++sum.clean;      break;
            case ScanVerdict::Suspicious: ++sum.suspicious; break;
            case ScanVerdict::Malicious:  ++sum.malicious;  break;
            case ScanVerdict::Error:      ++sum.errors;     break;
        }
        if (cb) cb(r);
        sum.results.push_back(std::move(r));
    };

    if (recursive) {
        for (fs::recursive_directory_iterator it(dirPath, ec), end;
             !ec && it != end; it.increment(ec)) {
            handle(*it);
        }
    } else {
        for (fs::directory_iterator it(dirPath, ec), end;
             !ec && it != end; it.increment(ec)) {
            handle(*it);
        }
    }
    return sum;
}

bool ContentScanner::QuarantineFile(const std::string& filePath,
                                    const std::string& quarantineDir,
                                    const FileScanResult* result,
                                    std::string* err) const {
    namespace fs = std::filesystem;
    std::error_code ec;

    // 未提供結果就先掃一次（拿 sha256 與 findings 寫進清單）
    FileScanResult local;
    if (!result) {
        local = ScanFile(filePath);
        result = &local;
    }

    fs::create_directories(quarantineDir, ec);
    if (ec) {
        if (err) *err = "無法建立隔離目錄: " + ec.message();
        return false;
    }

    std::string id = result->sha256.empty() ? "unknown" : result->sha256;
    fs::path dest = fs::path(quarantineDir) / (id + ".quarantine");

    // 優先 rename（同卷零拷貝）；跨卷失敗則 copy + remove
    fs::rename(filePath, dest, ec);
    if (ec) {
        ec.clear();
        fs::copy_file(filePath, dest, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            if (err) *err = "複製到隔離區失敗: " + ec.message();
            return false;
        }
        fs::remove(filePath, ec);
        if (ec) {
            if (err) *err = "已複製但刪除原檔失敗: " + ec.message();
            return false;
        }
    }

    // 寫隔離清單 <sha256>.json
    char ts[32] = {};
    std::time_t now = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    std::strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", &tmv);

    std::string manifest;
    manifest += "{\n";
    manifest += "  \"original_path\": \"" + JsonEscape(filePath) + "\",\n";
    manifest += "  \"sha256\": \"" + result->sha256 + "\",\n";
    manifest += "  \"size\": " + std::to_string(result->size) + ",\n";
    manifest += "  \"verdict\": \"" + std::string(ScanVerdictToString(result->verdict)) + "\",\n";
    manifest += "  \"quarantined_at\": \"" + std::string(ts) + "\",\n";
    manifest += "  \"findings\": [";
    for (size_t i = 0; i < result->findings.size(); ++i) {
        const auto& fnd = result->findings[i];
        manifest += (i ? "," : "") + std::string("\n    {\"rule_id\": \"") +
                    JsonEscape(fnd.ruleId) + "\", \"severity\": \"" +
                    ScanSeverityToString(fnd.severity) + "\", \"description\": \"" +
                    JsonEscape(fnd.description) + "\"}";
    }
    manifest += result->findings.empty() ? "]\n}\n" : "\n  ]\n}\n";

    fs::path manifestPath = fs::path(quarantineDir) / (id + ".json");
    std::ofstream mf(manifestPath, std::ios::binary | std::ios::trunc);
    if (!mf) {
        if (err) *err = "隔離完成但清單寫入失敗";
        return false;  // 檔案已搬走,但回報失敗讓呼叫端知道清單缺失
    }
    mf << manifest;
    return true;
}

// ============================================================================
// 各檢查實作
// ============================================================================

void ContentScanner::CheckFilename(const std::string& filePath,
                                   FileScanResult& r) const {
    if (!detectDoubleExt) return;
    std::string name = std::filesystem::path(filePath).filename().string();
    std::string outer = ExtOf(name);
    std::string inner = InnerExtOf(name);
    if (!inner.empty() && ExecutableExts().count(outer) &&
        DataExts().count(inner)) {
        r.findings.push_back({"heur.double_ext",
            "雙重副檔名偽裝:" + inner + " 檔偽裝 " + outer + " 可執行檔",
            ScanSeverity::High});
    }
}

void ContentScanner::CheckMagicVsExtension(const std::string& filePath,
                                           const std::vector<uint8_t>& head,
                                           FileScanResult& r) const {
    std::string ext = ExtOf(
        std::filesystem::path(filePath).filename().string());
    if (!DataExts().count(ext) || head.empty()) return;

    const uint8_t* b = head.data();
    size_t n = head.size();
    if (IsPEAt(b, n, 0)) {
        r.findings.push_back({"heur.exec_in_data",
            "資料副檔名 ." + ext + " 內容為 PE 執行檔", ScanSeverity::High});
    } else if (IsELFAt(b, n, 0)) {
        r.findings.push_back({"heur.exec_in_data",
            "資料副檔名 ." + ext + " 內容為 ELF 執行檔", ScanSeverity::High});
    } else if (IsMachOAt(b, n, 0)) {
        r.findings.push_back({"heur.exec_in_data",
            "資料副檔名 ." + ext + " 內容為 Mach-O 執行檔",
            ScanSeverity::High});
    } else if (n >= 2 && b[0] == '#' && b[1] == '!') {
        r.findings.push_back({"heur.exec_in_data",
            "資料副檔名 ." + ext + " 內容為 shebang 腳本",
            ScanSeverity::Medium});
    }
}

void ContentScanner::CheckSignatures(const std::vector<uint8_t>& content,
                                     FileScanResult& r) const {
    const uint8_t* data = content.data();
    size_t n = content.size();

    // EICAR（內建,可關閉）
    if (detectEicar &&
        ContainsBytes(data, n,
                      reinterpret_cast<const uint8_t*>(kEicar),
                      sizeof(kEicar) - 1)) {
        r.findings.push_back({"sig.eicar",
            "EICAR 標準反病毒測試檔案", ScanSeverity::Critical});
    }

    for (const auto& sig : byteSigs) {
        if (ContainsBytes(data, n, sig.bytes.data(), sig.bytes.size())) {
            r.findings.push_back({sig.ruleId,
                "位元組特徵命中: " + sig.description,
                ScanSeverity::High});
        }
    }
}

void ContentScanner::CheckEmbeddedPE(const std::string& filePath,
                                     const std::vector<uint8_t>& content,
                                     FileScanResult& r) const {
    // 只對資料副檔名檔案檢查「offset > 0 的內嵌 PE」
    std::string ext = ExtOf(
        std::filesystem::path(filePath).filename().string());
    if (!DataExts().count(ext) || content.size() < 0x40) return;

    const uint8_t* data = content.data();
    size_t n = content.size();
    for (size_t off = 1; off + 0x40 <= n; ++off) {
        if (data[off] == 'M' && data[off + 1] == 'Z' && IsPEAt(data, n, off)) {
            char buf[64];
            std::snprintf(buf, sizeof(buf),
                          "資料檔 offset %zu 處內嵌 PE 執行檔", off);
            r.findings.push_back({"heur.embedded_pe", buf,
                                  ScanSeverity::High});
            return;  // 一筆即可
        }
    }
}

void ContentScanner::CheckScriptContent(const std::string& filePath,
                                        const std::string& text,
                                        FileScanResult& r) const {
    std::string ext = ExtOf(
        std::filesystem::path(filePath).filename().string());
    if (!ScriptExts().count(ext) || text.empty()) return;

    std::string lower = ToLower(text);
    for (const auto& pat : kScriptPatterns) {
        if (lower.find(pat.needle) != std::string::npos) {
            r.findings.push_back({"heur.script",
                std::string("腳本含危險模式 \"") + pat.needle + "\": " +
                    pat.what,
                pat.severity});
        }
    }
}

void ContentScanner::FinalizeVerdict(FileScanResult& r) const {
    ScanSeverity worst = ScanSeverity::Info;
    for (const auto& f : r.findings)
        worst = std::max(worst, f.severity);
    // Info/Low 只記錄不升級;Medium 以上 → Suspicious;
    // 特徵庫與黑名單命中（ruleId 前綴 sig./hash.）→ Malicious
    for (const auto& f : r.findings) {
        if (f.ruleId.rfind("sig.", 0) == 0 || f.ruleId.rfind("hash.", 0) == 0) {
            r.verdict = ScanVerdict::Malicious;
            return;
        }
    }
    r.verdict = (worst >= ScanSeverity::Medium)
                    ? ScanVerdict::Suspicious : ScanVerdict::Clean;
}

// ============================================================================
// 字串轉換
// ============================================================================

const char* ScanVerdictToString(ScanVerdict v) {
    switch (v) {
        case ScanVerdict::Clean:      return "Clean";
        case ScanVerdict::Suspicious: return "Suspicious";
        case ScanVerdict::Malicious:  return "Malicious";
        case ScanVerdict::Error:      return "Error";
    }
    return "Unknown";
}

const char* ScanSeverityToString(ScanSeverity s) {
    switch (s) {
        case ScanSeverity::Info:     return "Info";
        case ScanSeverity::Low:      return "Low";
        case ScanSeverity::Medium:   return "Medium";
        case ScanSeverity::High:     return "High";
        case ScanSeverity::Critical: return "Critical";
    }
    return "Unknown";
}

} // namespace Security
} // namespace Potato
