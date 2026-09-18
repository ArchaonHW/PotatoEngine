#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace Potato {
namespace Media {

// ---- ffmpeg 外部行程編碼器（AD-4）----
// 全專案唯一的 ffmpeg CLI 介面：_popen rawvideo stdin pipe。
// ffmpeg 不在 PATH → Open 失敗 → 呼叫端降級 PNG-only，不算錯。
// 用法：
//   VideoEncoder enc;
//   if (enc.Open("out.mp4", 640, 360, 12, PixelFormat::RGBA)) {
//       enc.WriteFrame(rgba.data(), rgba.size());
//       enc.Close();
//   }

enum class PixelFormat {
    RGBA, // 4 bytes/px（capture 段標準）
    RGB,  // 3 bytes/px（舊工具慣用）
};

// 編碼品質：crf 越低畫質越好（x264:15~18 高品質,23 以下肉眼無損）;
// preset 越慢同碼率畫質越好（medium → slow 約 +0.3dB,檔案更小）
struct EncodeQuality {
    int crf = 20;
    const char* preset = "medium";

    static EncodeQuality High()  { return {16, "slow"}; }
    static EncodeQuality Draft() { return {23, "fast"}; }
};

class VideoEncoder {
public:
    VideoEncoder() = default;
    ~VideoEncoder() { Close(); }
    VideoEncoder(const VideoEncoder&) = delete;
    VideoEncoder& operator=(const VideoEncoder&) = delete;

    // 啟動 ffmpeg 子行程；失敗（無 ffmpeg/管道建立失敗）回 false。
    // logPath 非空時把 ffmpeg stderr 導到該檔。
    bool Open(const std::string& outPath, int width, int height, int fps,
              PixelFormat fmt = PixelFormat::RGBA,
              const std::string& logPath = "",
              const EncodeQuality& quality = {});

    // 餵一影格；bytes 必須 = w*h*channels。pipe 已斷回 false。
    bool WriteFrame(const void* pixels, size_t bytes);

    // 關管等 ffmpeg 收尾；回傳子行程 exit status（0 = 成功）。
    int Close();

    bool IsOpen() const { return pipe_ != nullptr; }

    // 組 ffmpeg 命令列（公開是為了測試可驗證參數而不需真跑 ffmpeg）
    static std::string BuildCommand(const std::string& outPath, int w, int h,
                                    int fps, PixelFormat fmt,
                                    const std::string& logPath,
                                    const EncodeQuality& quality = {});

private:
    FILE* pipe_ = nullptr;
};

} // namespace Media
} // namespace Potato
