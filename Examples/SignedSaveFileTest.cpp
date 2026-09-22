/**
 * SignedSaveFileTest - 簽章存檔包裝契約測試（headless）
 *
 * 覆蓋：
 *   [回圈]   Write → Read 欄位一致;.tmp 無殘留
 *   [竄改]   payload/MAC/檔頭任一位置翻轉 → Read 失敗
 *   [金鑰]   錯金鑰驗簽失敗
 *   [截斷]   截短檔案失敗;空檔失敗
 *   [版本]   高版本檔拒讀（即使 MAC 正確）
 *   [Verify] 只驗不讀 API
 */

#include "Serialization/SignedSaveFile.h"
#include "Security/SecuritySystem.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace Potato;
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

std::vector<uint8_t> ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f),
            std::istreambuf_iterator<char>()};
}

void WriteFile(const std::string& path, const std::vector<uint8_t>& buf) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(buf.data()),
            static_cast<std::streamsize>(buf.size()));
}

SerializableTypes::GameStateData MakeState() {
    SerializableTypes::GameStateData s;
    s.levelName = "chapter3/fortress";
    s.playTime = 1234.5f;
    s.score = 9876;
    s.activeObjects = {"player", "squad_01", "squad_02"};
    return s;
}

bool StateEquals(const SerializableTypes::GameStateData& a,
                 const SerializableTypes::GameStateData& b) {
    return a.levelName == b.levelName &&
           a.playTime == b.playTime &&
           a.score == b.score &&
           a.activeObjects == b.activeObjects;
}

} // anonymous namespace

int main() {
    printf("=== SignedSaveFile 測試 ===\n\n");

    const auto key = Security::GenerateRandomBytes(32);
    Expect(key.size() == 32, "金鑰產生 32B");
    const auto wrongKey = Security::GenerateRandomBytes(32);

    const fs::path dir = fs::temp_directory_path() / "potato_signed_save_test";
    fs::create_directories(dir);
    const std::string path = (dir / "slot1.psave").string();

    // ---- [回圈] ----
    printf("-- 寫讀回圈 --\n");
    const auto orig = MakeState();
    Expect(SignedSaveFile::Write(path, orig, key.data(), key.size()),
           "Write 成功");
    SerializableTypes::GameStateData back;
    Expect(SignedSaveFile::Read(path, back, key.data(), key.size()),
           "Read 成功");
    Expect(StateEquals(orig, back), "欄位一致",
           back.levelName + " score=" + std::to_string(back.score));
    Expect(!fs::exists(path + ".tmp"), ".tmp 無殘留");

    // ---- [Verify] ----
    printf("-- Verify --\n");
    Expect(SignedSaveFile::Verify(path, key.data(), key.size()),
           "Verify 正檔通過");

    // ---- [竄改] ----
    printf("-- 竄改偵測 --\n");
    const auto good = ReadFile(path);
    Expect(good.size() > 40, "檔案尺寸合理",
           std::to_string(good.size()) + "B");

    auto flipAt = [&](size_t pos, const char* name) {
        auto bad = good;
        bad[pos] ^= 0xFF;
        const std::string p = (dir / "tampered.psave").string();
        WriteFile(p, bad);
        SerializableTypes::GameStateData out;
        Expect(!SignedSaveFile::Read(p, out, key.data(), key.size()), name);
    };
    flipAt(10, "翻轉 payload 位元組 → 拒讀");        // payload 中段
    flipAt(good.size() - 5, "翻轉 MAC 位元組 → 拒讀");
    flipAt(0, "翻轉 magic 位元組 → 拒讀");
    flipAt(4, "翻轉 version 位元組 → 拒讀");

    // ---- [金鑰] ----
    printf("-- 金鑰 --\n");
    {
        SerializableTypes::GameStateData out;
        Expect(!SignedSaveFile::Read(path, out, wrongKey.data(),
                                     wrongKey.size()),
               "錯金鑰拒讀");
        Expect(!SignedSaveFile::Verify(path, wrongKey.data(),
                                       wrongKey.size()),
               "錯金鑰 Verify 失敗");
    }

    // ---- [截斷] ----
    printf("-- 截斷 --\n");
    {
        const std::string p = (dir / "trunc.psave").string();
        WriteFile(p, {good.begin(), good.end() - 10});
        SerializableTypes::GameStateData out;
        Expect(!SignedSaveFile::Read(p, out, key.data(), key.size()),
               "截短 10B 拒讀");
        WriteFile(p, {});
        Expect(!SignedSaveFile::Read(p, out, key.data(), key.size()),
               "空檔拒讀");
    }

    // ---- [版本] ----
    printf("-- 版本 --\n");
    {
        // 合法簽章但版本=2:MAC 正確仍應拒讀
        std::vector<uint8_t> region = {'P', 'S', 'V', 'F',
                                       SignedSaveFile::kFormatVersion + 1};
        region.push_back('{'); region.push_back('}');
        const auto blob = Security::SignData(key.data(), key.size(),
                                             region.data(), region.size());
        const std::string p = (dir / "v2.psave").string();
        WriteFile(p, blob);
        SerializableTypes::GameStateData out;
        Expect(!SignedSaveFile::Read(p, out, key.data(), key.size()),
               "高版本檔拒讀(MAC 正確亦然)");
        Expect(!SignedSaveFile::Verify(p, key.data(), key.size()),
               "高版本 Verify 失敗");
    }

    // ---- 不存在檔 ----
    {
        SerializableTypes::GameStateData out;
        Expect(!SignedSaveFile::Read((dir / "missing.psave").string(),
                                     out, key.data(), key.size()),
               "不存在檔拒讀");
    }

    std::error_code ec;
    fs::remove_all(dir, ec);  // 清理

    printf("\n=== 結果: %d PASS / %d FAIL ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
