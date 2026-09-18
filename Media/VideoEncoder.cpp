#include "VideoEncoder.h"

#include <cstdio>

#ifdef _WIN32
#define POPEN _popen
#define PCLOSE _pclose
#else
#define POPEN popen
#define PCLOSE pclose
#endif

namespace Potato {
namespace Media {

// 沿用現行工具的參數集（VideoDataDemo/BattleRenderDemo 驗證過的組合）：
// rawvideo stdin → vflip（glReadPixels 是 bottom-up）→ libx264 yuv420p
std::string VideoEncoder::BuildCommand(const std::string& outPath, int w, int h,
                                       int fps, PixelFormat fmt,
                                       const std::string& logPath) {
    char buf[1024];
    const char* pix = (fmt == PixelFormat::RGBA) ? "rgba" : "rgb24";
    std::string cmd;
    std::snprintf(buf, sizeof(buf),
                  "ffmpeg -y -f rawvideo -pix_fmt %s -s %dx%d -r %d -i - "
                  "-vf vflip -an -c:v libx264 -pix_fmt yuv420p -crf 20 "
                  "-movflags +faststart \"%s\"",
                  pix, w, h, fps, outPath.c_str());
    cmd = buf;
    if (!logPath.empty()) {
        cmd += " 2> \"" + logPath + "\"";
    }
    return cmd;
}

bool VideoEncoder::Open(const std::string& outPath, int width, int height,
                        int fps, PixelFormat fmt,
                        const std::string& logPath) {
    if (pipe_ || width <= 0 || height <= 0 || fps <= 0 || outPath.empty()) {
        return false;
    }
    const std::string cmd =
        BuildCommand(outPath, width, height, fps, fmt, logPath);
    pipe_ = POPEN(cmd.c_str(), "wb");
    return pipe_ != nullptr;
}

bool VideoEncoder::WriteFrame(const void* pixels, size_t bytes) {
    if (!pipe_ || !pixels || bytes == 0) return false;
    return std::fwrite(pixels, 1, bytes, pipe_) == bytes;
}

int VideoEncoder::Close() {
    if (!pipe_) return -1;
    FILE* p = pipe_;
    pipe_ = nullptr;
    return PCLOSE(p);
}

} // namespace Media
} // namespace Potato
