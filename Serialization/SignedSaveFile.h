#pragma once

/**
 * SignedSaveFile - 簽章存檔包裝
 *
 * 把 ISerializable 序列化與 Security::SignData/VerifySignedData 接成單一呼叫：
 * 寫檔自動附 HMAC-SHA256，讀檔先驗簽再反序列化。
 *
 * 檔案格式：
 *   magic "PSVF"(4B) || version(1B) || payload || HMAC-SHA256(32B)
 * MAC 涵蓋 magic+version+payload——改檔頭、降版本、動內容都會驗簽失敗。
 *
 * 金鑰保管責任在呼叫端（Security::GenerateRandomBytes(32) 首啟產生、
 * 存於使用者目錄；金鑰跟存檔放一起就失去意義）。
 *
 * 誠實標註：HMAC 保完整性不保機密——存檔內容仍可被讀取，
 * 需要加密請在序列化層另加；金鑰被逆向取出後簽章即失效。
 */

#include "Serialization/Serialization.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace Potato {

class SignedSaveFile {
public:
    static constexpr uint8_t kFormatVersion = 1;
    static constexpr char kMagic[4] = {'P', 'S', 'V', 'F'};

    // 序列化 object → 加檔頭 → 附 HMAC → 寫入 filePath。
    // 經 .tmp + rename 替換，當機不留半個檔（殘檔反正也過不了簽章）。
    static bool Write(const std::string& filePath,
                      const ISerializable& object,
                      const void* key, size_t keyLen);

    // 驗證 magic/version/HMAC → 反序列化進 object。
    // 任一關卡失敗回 false 且 object 不被觸碰。
    static bool Read(const std::string& filePath,
                     ISerializable& object,
                     const void* key, size_t keyLen);

    // 只驗證檔頭與簽章（例：列出存檔槽前先篩損毀檔）
    static bool Verify(const std::string& filePath,
                       const void* key, size_t keyLen);
};

} // namespace Potato
