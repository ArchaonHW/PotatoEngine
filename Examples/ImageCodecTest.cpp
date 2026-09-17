// ImageCodecTest - PNG 編解碼無頭測試
// 覆蓋：encode→decode 像素往返、畸形輸入拒絕、解壓炸彈上限、尺寸上限

#include "Rendering/ImageCodec.h"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace Potato;

static int g_pass = 0;
static int g_fail = 0;

static void Check(bool ok, const char* name) {
    if (ok) { g_pass++; printf("  [PASS] %s\n", name); }
    else    { g_fail++; printf("  [FAIL] %s\n", name); }
}

int main() {
    printf("=== ImageCodec Tests ===\n\n");

    // [1] encode→decode 像素往返
    {
        const int W = 8, H = 4;
        std::vector<uint8> src(static_cast<size_t>(W) * H * 4);
        for (size_t i = 0; i < src.size(); i += 4) {
            src[i] = (uint8)(i & 0xFF);
            src[i + 1] = (uint8)((i * 3) & 0xFF);
            src[i + 2] = (uint8)((i * 7) & 0xFF);
            src[i + 3] = 200;
        }
        std::vector<uint8> png;
        std::string err;
        Check(ImageCodec::EncodePNG(W, H, src.data(), png, &err),
              "EncodePNG 成功");

        std::vector<uint8> dec;
        int dw = 0, dh = 0;
        Check(ImageCodec::DecodePNG(png.data(), png.size(), dec, dw, dh, &err),
              "DecodePNG 成功");
        Check(dw == W && dh == H, "尺寸往返一致");
        Check(dec == src, "像素往返一致");
    }

    // [2] 畸形輸入拒絕
    {
        std::vector<uint8> dec; int dw, dh; std::string err;
        uint8 garbage[64] = {};
        Check(!ImageCodec::DecodePNG(garbage, sizeof(garbage), dec, dw, dh, &err),
              "垃圾資料被拒絕");

        // PNG magic 但截斷
        const uint8 truncated[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        Check(!ImageCodec::DecodePNG(truncated, sizeof(truncated), dec, dw, dh, &err),
              "截斷 PNG 被拒絕");
    }

    // [3] 尺寸上限（encode 端）
    {
        std::vector<uint8> png; std::string err;
        uint8 px[4] = {0};
        Check(!ImageCodec::EncodePNG(20000, 20000, px, png, &err),
              "超上限尺寸被拒絕");
        Check(!ImageCodec::EncodePNG(0, 10, px, png, &err),
              "零寬度被拒絕");
        Check(!ImageCodec::EncodePNG(10, 10, nullptr, png, &err),
              "null 像素被拒絕");
    }

    // [4] IDAT 解壓上限：篡改 IHDR 宣告大尺寸但資料短小
    //     （inflate 收到 expect 上限應快速失敗而非撐大 raw）
    {
        // 造一個 1x1 合法 PNG，再把 IHDR 的 w/h 欄位改大
        std::vector<uint8> png;
        std::string err;
        uint8 px[4] = {255, 0, 0, 255};
        ImageCodec::EncodePNG(1, 1, px, png, &err);
        Check(png.size() > 40, "1x1 PNG 已產生");
        // IHDR payload 從 offset 16 開始：width(4) height(4)
        png[16] = 0x00; png[17] = 0x00; png[18] = 0x40; png[19] = 0x00; // w=16384
        png[20] = 0x00; png[21] = 0x00; png[22] = 0x40; png[23] = 0x00; // h=16384
        std::vector<uint8> dec; int dw, dh;
        Check(!ImageCodec::DecodePNG(png.data(), png.size(), dec, dw, dh, &err),
              "IHDR 尺寸與資料不符被拒絕");
    }

    printf("\n=== 結果: %d PASS, %d FAIL ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
