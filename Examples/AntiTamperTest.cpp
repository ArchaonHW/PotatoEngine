/**
 * AntiTamperTest - 記憶體混淆與資料簽章測試
 *
 * 驗證 Security 模組新增的「預防層」防駭件：
 *   [混淆] ProtectedValue<int>：明文不落記憶體（掃描物件 bytes 找不到值）
 *   [混淆] 每次寫入換 key：同一值前後 cipher 位元組不同
 *   [偵測] 外部直接 patch 物件記憶體 → Get() 觸發 IsTampered
 *   [簽章] ComputeHMACSHA256 符合 RFC 4231 測試向量
 *   [簽章] SignData/VerifySignedData：roundtrip / 錯金鑰 / 截斷 / 翻位元
 *   [檔案] SignFile/VerifySignedFile：roundtrip + 竄改檔拒絕
 *   [隨機] GenerateRandomBytes：長度正確、兩次產出不同
 */

#include "Security/SecuritySystem.h"
#include "Security/ProtectedValue.h"

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

void Expect(bool cond, const char* name, const std::string& detail = "") {
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

// 在物件記憶體中搜尋 needle 的明文位元序列（模擬記憶體掃描器）
bool BytesContain(const void* hay, size_t hayLen,
                  const void* needle, size_t needleLen) {
    const uint8_t* h = static_cast<const uint8_t*>(hay);
    const uint8_t* n = static_cast<const uint8_t*>(needle);
    if (needleLen == 0 || hayLen < needleLen) return false;
    for (size_t i = 0; i + needleLen <= hayLen; i++)
        if (std::memcmp(h + i, n, needleLen) == 0) return true;
    return false;
}

} // anonymous namespace

int main() {
    printf("=== AntiTamper 防竄改測試 ===\n\n");

    // ---- [混淆] ProtectedValue 基本語意 ----
    printf("-- ProtectedValue 基本操作 --\n");
    {
        ProtectedValue<int> score(100);
        Expect(static_cast<int>(score) == 100, "初始值讀取正確");
        score += 250;
        Expect(static_cast<int>(score) == 350, "operator+= 正確");
        ++score;
        Expect(static_cast<int>(score) == 351, "operator++ 正確");
        score = -42;
        Expect(static_cast<int>(score) == -42, "負數寫讀正確");
        Expect(!score.IsTampered(), "正常操作不誤報 tampered");

        ProtectedValue<float> hp(75.5f);
        hp -= 25.5f;
        Expect(std::fabs(static_cast<float>(hp) - 50.0f) < 1e-4f,
               "float 型別 roundtrip 正確");
    }

    // ---- [混淆] 明文不落記憶體 ----
    printf("-- 明文掃描抗性 --\n");
    {
        // 用一個不太可能巧合出現的哨兵值
        ProtectedValue<int> secret(0x1234ABCD);
        int probe = 0x1234ABCD;
        Expect(!BytesContain(&secret, sizeof(secret), &probe, sizeof(probe)),
               "物件記憶體中找不到明文值");

        // 同一值重寫後 cipher 應不同（key 輪換）
        std::vector<uint8_t> snap1(sizeof(secret));
        std::memcpy(snap1.data(), &secret, sizeof(secret));
        secret = 0x1234ABCD;  // 同值重寫
        std::vector<uint8_t> snap2(sizeof(secret));
        std::memcpy(snap2.data(), &secret, sizeof(secret));
        Expect(snap1 != snap2, "同值重寫後儲存位元組不同（rekey）");
        Expect(static_cast<int>(secret) == 0x1234ABCD, "重寫後值仍正確");
    }

    // ---- [偵測] 外部記憶體 patch ----
    printf("-- 外部竄改偵測 --\n");
    {
        ProtectedValue<int> ammo(30);
        // 模擬作弊器：直接寫物件記憶體（不經 Set）
        auto* raw = reinterpret_cast<uint8_t*>(&ammo);
        raw[0] ^= 0xFF;  // 翻轉 cipher 第一個位元組
        volatile int ignored = ammo.Get();  // Get 觸發 seal 驗證
        (void)ignored;
        Expect(ammo.IsTampered(), "直接 patch cipher → IsTampered");
        ammo.ResetTampered();
        ammo = 30;
        Expect(!ammo.IsTampered(), "ResetTampered + 合法寫入後清除");
    }

    // ---- [簽章] HMAC-SHA256 標準向量 ----
    printf("-- HMAC-SHA256 RFC 4231 --\n");
    {
        // Test Case 1：key = 0x0b × 20, data = "Hi There"
        std::vector<uint8_t> key1(20, 0x0b);
        const char* data1 = "Hi There";
        std::string mac1 = ComputeHMACSHA256(key1.data(), key1.size(),
                                             data1, std::strlen(data1));
        Expect(mac1 == "b0344c61d8db38535ca8afceaf0bf12b"
                      "881dc200c9833da726e9376c2e32cff7",
               "RFC 4231 TC1", mac1.substr(0, 16) + "...");

        // Test Case 2：key = "Jefe", data = "what do ya want for nothing?"
        const char* key2 = "Jefe";
        const char* data2 = "what do ya want for nothing?";
        std::string mac2 = ComputeHMACSHA256(key2, std::strlen(key2),
                                             data2, std::strlen(data2));
        Expect(mac2 == "5bdcc146bf60754e6a042426089575c7"
                      "5a003f089d2739839dec58b964ec3843",
               "RFC 4231 TC2", mac2.substr(0, 16) + "...");
    }

    // ---- [簽章] SignData/VerifySignedData ----
    printf("-- 資料簽章 --\n");
    {
        auto key = GenerateRandomBytes(32);
        const char* saveData = "{\"level\":5,\"gold\":9999}";
        auto blob = SignData(key.data(), key.size(),
                             saveData, std::strlen(saveData));
        Expect(blob.size() == std::strlen(saveData) + 32,
               "blob = data + 32B MAC");

        std::vector<uint8_t> out;
        Expect(VerifySignedData(key.data(), key.size(), blob, &out),
               "正確金鑰驗證通過");
        Expect(out.size() == std::strlen(saveData) &&
               std::memcmp(out.data(), saveData, out.size()) == 0,
               "取出資料與原文一致");

        // 錯金鑰
        auto wrongKey = GenerateRandomBytes(32);
        Expect(!VerifySignedData(wrongKey.data(), wrongKey.size(), blob, nullptr),
               "錯誤金鑰拒絕");

        // 竄改內容（改 data 區一個位元組）
        auto tampered = blob;
        tampered[5] ^= 0x01;
        Expect(!VerifySignedData(key.data(), key.size(), tampered, nullptr),
               "竄改 data 拒絕");

        // 竄改 MAC 區
        auto tamperedMac = blob;
        tamperedMac.back() ^= 0x01;
        Expect(!VerifySignedData(key.data(), key.size(), tamperedMac, nullptr),
               "竄改 MAC 拒絕");

        // 截斷（短於 32B）
        std::vector<uint8_t> truncated(blob.begin(), blob.begin() + 10);
        Expect(!VerifySignedData(key.data(), key.size(), truncated, nullptr),
               "截斷 blob 拒絕");
    }

    // ---- [檔案] SignFile/VerifySignedFile ----
    printf("-- 檔案簽章 --\n");
    {
        const fs::path dir = fs::temp_directory_path() / "potato_antitamper_test";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir);

        auto key = GenerateRandomBytes(32);
        fs::path save = dir / "save.dat";
        fs::path signedSave = dir / "save.signed";
        {
            std::ofstream f(save, std::ios::binary | std::ios::trunc);
            f << "player_pos=120,88;hp=100;inventory=[sword,potion]";
        }

        Expect(SignFile(save.string(), signedSave.string(),
                        key.data(), key.size()), "SignFile 成功");
        std::vector<uint8_t> content;
        Expect(VerifySignedFile(signedSave.string(), key.data(), key.size(),
                                &content), "VerifySignedFile 通過");
        Expect(std::string(content.begin(), content.end())
                   .find("hp=100") != std::string::npos,
               "解出原始存檔內容");

        // 竄改簽章檔
        {
            std::fstream f(signedSave, std::ios::binary | std::ios::in |
                                       std::ios::out);
            f.seekp(5);
            char c = 0;
            f.read(&c, 1);
            f.seekp(5);
            c ^= 0x55;
            f.write(&c, 1);
        }
        Expect(!VerifySignedFile(signedSave.string(), key.data(), key.size(),
                                 nullptr),
               "竄改後的簽章檔拒絕");

        fs::remove_all(dir, ec);
    }

    // ---- [隨機] GenerateRandomBytes ----
    printf("-- 隨機金鑰 --\n");
    {
        auto k1 = GenerateRandomBytes(32);
        auto k2 = GenerateRandomBytes(32);
        Expect(k1.size() == 32, "金鑰長度正確");
        Expect(k1 != k2, "兩次產出不同");
        Expect(GenerateRandomBytes(0).empty(), "長度 0 回空");
    }

    printf("\n=== 結果: %d PASS / %d FAIL ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
