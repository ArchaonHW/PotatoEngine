#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Potato {
namespace Media {

// ---- 離屏影格擷取（AD-3：唯一 top-down 語意）----
// 收斂 4 處複製貼上的 hidden-window + FBO + readback + flip 模式。
// GL 程式碼集中此檔；僅由 GL 工具 exe 連結（AD-1），
// Media/ 其他成員不含 GL。
//
// 語意不變式：
//   - Grab() 永遠回傳 top-down RGBA8（列 0 = 畫面頂端）——
//     修掉 VideoDataDemo 未翻轉 PNG vs top-down bbox 的缺陷
//   - 無 GL 環境 → Begin() 回 false（呼叫端走 [SKIP] exit-0）
//   - End() 釋放 context；RAII 解構也會清

class HeadlessCapture {
public:
    HeadlessCapture() = default;
    ~HeadlessCapture();
    HeadlessCapture(const HeadlessCapture&) = delete;
    HeadlessCapture& operator=(const HeadlessCapture&) = delete;

    // 建 hidden GLFW window + 載 GL + 建 FBO。失敗回 false。
    bool Begin(int width, int height);

    // 呼叫端在 callback 內下 GL 繪製指令；回傳 top-down RGBA 影格。
    // draw 回傳 false 視為該幀放棄（Grab 回空 vector）。
    std::vector<uint8_t> Grab(const std::function<bool()>& draw);

    // 便利函式：Grab → PNG 檔（走 ImageCodec）。失敗回 false。
    bool GrabToFile(const std::function<bool()>& draw,
                    const std::string& pngPath);

    void End();
    bool Ready() const { return ready_; }
    int Width() const { return width_; }
    int Height() const { return height_; }

private:
    bool ready_ = false;
    int width_ = 0;
    int height_ = 0;
    void* window_ = nullptr;   // GLFWwindow*（不透過標頭洩漏 GLFW 型別）
    unsigned fbo_ = 0;
    unsigned colorTex_ = 0;
};

} // namespace Media
} // namespace Potato
