#pragma once

#include "Core/CoreTypes.h"

#include <string>
#include <vector>

namespace Potato {
namespace ImageCodec {

/**
 * PNG 解碼（零第三方依賴）
 *
 * 支援：8-bit color type 0(gray)/2(RGB)/3(palette)/4(gray+alpha)/6(RGBA)、
 *       非交錯（interlace=0）、tRNS 透明色/調色盤 alpha。
 * 不支援：Adam7 交錯、16-bit、JPEG。
 * 輸出固定 RGBA8，列序為圖像由上至下（PNG 原序）。
 */
bool DecodePNG(const uint8* data, size_t size,
               std::vector<uint8>& outRGBA, int& outW, int& outH,
               std::string* err = nullptr);

/**
 * 依 magic 自動分派解碼：PNG → DecodePNG；
 * JPEG 等其他格式回傳 false 並在 err 說明。
 */
bool DecodeImage(const uint8* data, size_t size,
                 std::vector<uint8>& outRGBA, int& outW, int& outH,
                 std::string* err = nullptr);

/**
 * PNG 編碼：RGBA8 輸入。zlib 用 stored blocks（不壓縮，零依賴）。
 * rgba 為頂端列優先（top-down）排列。
 */
bool EncodePNG(int w, int h, const uint8* rgba,
               std::vector<uint8>& outPNG, std::string* err = nullptr);

/** 便利函式：編碼並寫檔（建立失敗/無法寫入回 false）。 */
bool WritePNGFile(const std::string& path, int w, int h, const uint8* rgba,
                  std::string* err = nullptr);

} // namespace ImageCodec
} // namespace Potato
