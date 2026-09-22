#include "Serialization/SignedSaveFile.h"
#include "Security/SecuritySystem.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>

namespace Potato {

namespace fs = std::filesystem;

namespace {

// 讀整個檔案成 byte vector;失敗回空
std::vector<uint8_t> ReadWholeFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    const auto size = f.tellg();
    if (size <= 0) return {};
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(buf.data()), size);
    if (!f) return {};
    return buf;
}

// 組含檔頭的受簽區域：magic || version || payload
std::vector<uint8_t> BuildSignedRegion(const std::string& payload) {
    std::vector<uint8_t> region;
    region.reserve(5 + payload.size());
    region.insert(region.end(), SignedSaveFile::kMagic,
                  SignedSaveFile::kMagic + 4);
    region.push_back(SignedSaveFile::kFormatVersion);
    region.insert(region.end(), payload.begin(), payload.end());
    return region;
}

// 驗簽並取出 payload;失敗回 false(out 不動)
bool Unpack(const std::string& filePath, const void* key, size_t keyLen,
            std::vector<uint8_t>* payloadOut) {
    const auto blob = ReadWholeFile(filePath);
    if (blob.size() < 5 + 32) return false;  // 至少檔頭 + MAC

    std::vector<uint8_t> region;
    if (!Security::VerifySignedData(key, keyLen, blob, &region)) return false;
    if (region.size() < 5) return false;
    if (!std::equal(SignedSaveFile::kMagic, SignedSaveFile::kMagic + 4,
                    region.begin())) return false;
    // 高版本檔拒讀（向前相容由呼叫端自行處理）
    if (region[4] > SignedSaveFile::kFormatVersion) return false;

    if (payloadOut) {
        payloadOut->assign(region.begin() + 5, region.end());
    }
    return true;
}

} // anonymous namespace

bool SignedSaveFile::Write(const std::string& filePath,
                           const ISerializable& object,
                           const void* key, size_t keyLen) {
    const std::string payload = object.Serialize();
    const auto region = BuildSignedRegion(payload);
    const auto blob = Security::SignData(key, keyLen,
                                         region.data(), region.size());
    if (blob.empty()) return false;

    // .tmp + rename：寫到一半當機不覆蓋原本的好檔
    const fs::path dest(filePath);
    const fs::path tmp = dest.string() + ".tmp";
    std::error_code ec;
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(reinterpret_cast<const char*>(blob.data()),
                static_cast<std::streamsize>(blob.size()));
        f.flush();
        if (!f) return false;
    }
    // std::filesystem::rename 在目標已存在時失敗（Windows）——先刪再搬，
    // 視窗內當機最多損失新檔，舊檔已由簽章保護可偵測
    fs::remove(dest, ec);
    fs::rename(tmp, dest, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

bool SignedSaveFile::Read(const std::string& filePath,
                          ISerializable& object,
                          const void* key, size_t keyLen) {
    std::vector<uint8_t> payload;
    if (!Unpack(filePath, key, keyLen, &payload)) return false;
    return object.Deserialize(
        std::string(reinterpret_cast<const char*>(payload.data()),
                    payload.size()));
}

bool SignedSaveFile::Verify(const std::string& filePath,
                            const void* key, size_t keyLen) {
    return Unpack(filePath, key, keyLen, nullptr);
}

} // namespace Potato
