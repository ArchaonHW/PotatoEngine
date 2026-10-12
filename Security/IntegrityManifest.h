#pragma once

/**
 * IntegrityManifest — 引擎/遊戲自身檔案完整性清單（SHA-256 白名單）
 *
 * 兩段式流程：
 *   1) 打包/發佈期：GenerateIntegrityManifest 遞迴雜湊安裝目錄
 *      （exe、dll、資產）寫出 manifest.json
 *   2) 啟動期：VerifyIntegrityManifest 逐檔重算 SHA-256 比對,
 *      不符/消失的檔案表示被竄改或植入
 *
 * Manifest 格式：
 *   {"version": 1,
 *    "files": [{"path": "bin/game.exe", "sha256": "<hex>", "size": 123}]}
 *   path 為相對 baseDir 的 POSIX 風格相對路徑。
 *
 * 誠實標註：
 * - manifest 本身也是檔案——未簽章的清單可被連檔案一起重算偽造。
 *   帶 hmacKey 的版本讓清單附上 HMAC-SHA256：攻擊者不知金鑰
 *   即無法產出合法 manifest。金鑰保管責任在呼叫端
 *   （內嵌於程式碼、伺服器下發、或 GenerateRandomBytes 後妥善存放）
 * - 完整 SHA-256 逐檔重算,大型安裝目錄的啟動掃描有 I/O 成本,
 *   建議只對可執行檔/關鍵資產建清單,或背景執行緒執行
 */

#include <string>
#include <vector>
#include <cstdint>

namespace Potato {
namespace Security {

// 驗證彙總結果
struct ManifestVerifyResult {
    int ok = 0;            // 雜湊相符
    int mismatch = 0;      // 檔案存在但雜湊不符（被竄改/版本不符）
    int missing = 0;       // 清單有記錄但檔案消失
    int unreadable = 0;    // 檔案存在但無法讀取
    // manifest 本身無法讀取/解析——fail-closed：什麼都沒驗到不算通過
    bool manifestError = false;
    std::vector<std::string> failures;  // "path (mismatch|missing|unreadable)"

    bool Passed() const {
        return !manifestError &&
               mismatch == 0 && missing == 0 && unreadable == 0;
    }
    int Total() const { return ok + mismatch + missing + unreadable; }
};

// 打包期：遞迴掃描 dirPath 下所有檔案,寫出 manifest JSON。
// excludeRelPrefixes：要跳過的相對路徑前綴（POSIX 風格,如 "Saves/"）。
// outJsonPath 若位於 dirPath 內,本身會被排除在清單外。
// hmacKey 非空時 manifest 附 "hmac" 欄位 = HMAC-SHA256(key, 規範化清單)；
// 規範化清單 = 每筆 "path\x00sha256\x00size\n" 依 path 排序後串接。
// 失敗（目錄不存在/無法寫入）回 false。
bool GenerateIntegrityManifest(
    const std::string& dirPath,
    const std::string& outJsonPath,
    const std::vector<std::string>& excludeRelPrefixes = {},
    const void* hmacKey = nullptr, size_t hmacKeyLen = 0);

// 啟動期：讀 manifestPath,逐檔以 baseDir + 相對路徑重算 SHA-256 比對。
// 金鑰策略 fail-closed：
//   - 給了 hmacKey → manifest 必須帶相符的 "hmac"（缺欄位/不符皆 manifestError）
//   - 沒給 hmacKey 但 manifest 帶 "hmac" → manifestError（簽章清單需金鑰驗證）
// manifest 無法讀取/解析時 manifestError=true 且 failures 含一筆說明。
ManifestVerifyResult VerifyIntegrityManifest(const std::string& manifestPath,
                                             const std::string& baseDir,
                                             const void* hmacKey = nullptr,
                                             size_t hmacKeyLen = 0);

} // namespace Security
} // namespace Potato
