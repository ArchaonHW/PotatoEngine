/**
 * SecurityDataTest - Security 模組「資料防護層」測試
 *
 * 覆蓋拆倉後新增的兩塊（headless,純引擎層）：
 *
 *   ProtectedValue<T> — 記憶體數值混淆容器
 *     [正確性] 建構/Set/Get/運算子(+ = ++ += -=)
 *     [偵測]   外部直接 patch 物件位元組 → IsTampered() latch
 *     [混淆]   明文值不出現在物件儲存位元組中（連續記憶體掃描失效）
 *     [重配]   每次 Set 換 key：同值兩次寫入的 cipher bytes 不同
 *
 *   SignData / VerifySignedData / SignFile / VerifySignedFile
 *     [正確性] 簽章→驗證→取回原始資料
 *     [偵測]   blob 任一位元組被翻轉 → 驗證失敗
 *     [偵測]   錯誤金鑰 / 截短 blob → 驗證失敗
 *     [檔案]   SignFile→VerifySignedFile 往返一致
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

} // anonymous namespace

int main() {
    printf("=== SecurityDataTest 資料防護層測試 ===\n\n");

    // ---- ProtectedValue: 正確性 ----
    printf("-- ProtectedValue 正確性 --\n");
    {
        ProtectedValue<int> score(100);
        Expect(score.Get() == 100, "建構值正確");
        score += 55;
        Expect(score.Get() == 155, "+= 正確");
        ++score;
        Expect(score.Get() == 156, "++ 正確");
        score = 42;
        int s = score;  // operator T()
        Expect(s == 42, "賦值 + 隱式轉型正確");

        ProtectedValue<float> hp(100.0f);
        hp -= 30.5f;
        Expect(hp.Get() > 69.4f && hp.Get() < 69.6f, "float -= 正確");
    }

    // ---- ProtectedValue: 外部竄改偵測 ----
    printf("-- ProtectedValue 竄改偵測 --\n");
    {
        ProtectedValue<int> ammo(30);
        Expect(!ammo.IsTampered(), "初始未竄改");

        // 模擬 Cheat Engine 直接 patch：翻轉物件第一個儲存位元組(cipher_)
        uint8_t* raw = reinterpret_cast<uint8_t*>(&ammo);
        raw[0] ^= 0xFF;
        ammo.Get();  // Get() 內部驗 seal
        Expect(ammo.IsTampered(), "外部 patch cipher → IsTampered");

        ammo.ResetTampered();
        ammo.Set(999);  // 正常 Set 之後 seal 重建
        ammo.Get();
        Expect(!ammo.IsTampered(), "ResetTampered + Set 後清除");
    }

    // ---- ProtectedValue: 明文不落記憶體 ----
    printf("-- ProtectedValue 混淆 --\n");
    {
        ProtectedValue<int> v(0xDEADBEEF);
        // 在物件佔用的儲存位元組中搜尋明文 0xDEADBEEF（little-endian）
        const uint8_t* raw = reinterpret_cast<const uint8_t*>(&v);
        const uint8_t needle[4] = {0xEF, 0xBE, 0xAD, 0xDE};
        bool foundPlain = false;
        for (size_t i = 0; i + 4 <= sizeof(v); ++i) {
            if (std::memcmp(raw + i, needle, 4) == 0) foundPlain = true;
        }
        Expect(!foundPlain, "明文 0xDEADBEEF 不存在於物件位元組");

        // 同值兩次寫入,儲存位元組應不同（Rekey 生效）
        ProtectedValue<int> a(777);
        std::vector<uint8_t> first(
            reinterpret_cast<const uint8_t*>(&a),
            reinterpret_cast<const uint8_t*>(&a) + sizeof(a));
        a.Set(777);  // 同值重寫 → 換 key
        std::vector<uint8_t> second(
            reinterpret_cast<const uint8_t*>(&a),
            reinterpret_cast<const uint8_t*>(&a) + sizeof(a));
        Expect(first != second && a.Get() == 777,
               "同值重寫換 key(cipher 不同、值仍正確)");
    }

    // ---- SignData / VerifySignedData ----
    printf("-- SignData 簽章 --\n");
    {
        uint8_t key[32];
        for (int i = 0; i < 32; ++i) key[i] = static_cast<uint8_t>(i * 7);
        const char* save = "{\"level\":5,\"exp\":12345}";
        std::vector<uint8_t> blob = SignData(key, sizeof(key),
                                             save, std::strlen(save));
        Expect(blob.size() == std::strlen(save) + 32,
               "blob = 原文 + 32B MAC");

        std::vector<uint8_t> out;
        Expect(VerifySignedData(key, sizeof(key), blob, &out) &&
               out.size() == std::strlen(save) &&
               std::memcmp(out.data(), save, out.size()) == 0,
               "驗證通過且取回原文");

        // 竄改任一位元組 → 失敗
        blob[3] ^= 0x01;
        Expect(!VerifySignedData(key, sizeof(key), blob),
               "blob 竄改 → 驗證失敗");

        // 錯誤金鑰 → 失敗
        uint8_t wrongKey[32] = {};
        std::vector<uint8_t> blob2 = SignData(key, sizeof(key),
                                              save, std::strlen(save));
        Expect(!VerifySignedData(wrongKey, sizeof(wrongKey), blob2),
               "錯誤金鑰 → 驗證失敗");

        // 截短 blob(<32B)→ 失敗
        std::vector<uint8_t> short_(10, 0);
        Expect(!VerifySignedData(key, sizeof(key), short_),
               "短 blob → 驗證失敗");
    }

    // ---- SignFile / VerifySignedFile ----
    printf("-- SignFile 檔案簽章 --\n");
    {
        const fs::path dir = fs::temp_directory_path() / "potato_signdata_test";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir);

        fs::path src = dir / "save.dat";
        fs::path sig = dir / "save.sig";
        {
            std::ofstream f(src, std::ios::binary);
            f << "LEVEL=99;GOLD=42000;INVENTORY=sword,shield";
        }
        uint8_t key[32];
        for (int i = 0; i < 32; ++i) key[i] = static_cast<uint8_t>(0xA5 ^ i);

        Expect(SignFile(src.string(), sig.string(), key, sizeof(key)),
               "SignFile 成功");
        std::vector<uint8_t> recovered;
        Expect(VerifySignedFile(sig.string(), key, sizeof(key), &recovered) &&
               std::string(recovered.begin(), recovered.end())
                       .find("GOLD=42000") != std::string::npos,
               "VerifySignedFile 取回內容");

        // 竄改簽章檔 → 失敗
        {
            std::fstream f(sig, std::ios::binary | std::ios::in | std::ios::out);
            f.seekp(0);
            char c = 0;
            f.read(&c, 1);
            f.seekp(0);
            c ^= 0x55;
            f.write(&c, 1);
        }
        Expect(!VerifySignedFile(sig.string(), key, sizeof(key)),
               "簽章檔竄改 → 驗證失敗");

        fs::remove_all(dir, ec);
    }

    // ---- GenerateRandomBytes ----
    printf("-- GenerateRandomBytes --\n");
    {
        auto a = GenerateRandomBytes(32);
        auto b = GenerateRandomBytes(32);
        Expect(a.size() == 32 && b.size() == 32, "長度正確");
        Expect(a != b, "兩次產生結果不同");
        auto empty = GenerateRandomBytes(0);
        Expect(empty.empty(), "len=0 回空");
    }

    printf("\n=== 結果: %d PASS / %d FAIL ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
