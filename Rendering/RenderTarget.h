#pragma once

#include "Core/CoreTypes.h"
#include <vector>

namespace Potato {

/**
 * 離屏渲染目標（FBO）：RGBA8 color texture + depth24/stencil8 renderbuffer。
 *
 * 取代各工具手寫的 glGenFramebuffers 管線（PortraitRenderer /
 * SynthDataDemo / VideoDataDemo 三處重複實作的共用基建）。
 * 用途：離屏截圖、訓練資料產出、後處理鏈、多通道（MRT 前置件）。
 *
 * 使用：
 *   RenderTarget rt;
 *   if (!rt.Create(640, 360)) return;   // 需 GL context 已建立
 *   rt.Bind();                          // 綁 FBO + viewport 切目標尺寸
 *   ...渲染...
 *   rt.ReadColor(rgba);                 // W*H*4 讀回
 *   rt.Unbind();                        // 回預設 framebuffer
 *
 * GL id 唯一所有權（同 Texture/Mesh 慣例）：禁拷貝、可 move。
 */
class RenderTarget {
public:
    RenderTarget() = default;
    ~RenderTarget();

    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;
    RenderTarget(RenderTarget&& o) noexcept { MoveFrom(o); }
    RenderTarget& operator=(RenderTarget&& o) noexcept {
        if (this != &o) {
            Destroy();
            MoveFrom(o);
        }
        return *this;
    }

    // 建立/重建；尺寸非法或 GL 失敗回 false（內部狀態清空）。
    // 已持舊資源時先 Destroy 再建——Create 可安全重入用於 resize
    bool Create(int w, int h);
    void Destroy();

    bool IsValid() const { return fbo != 0; }
    int GetWidth() const { return width; }
    int GetHeight() const { return height; }
    // color attachment 的 GL texture id（後處理採樣/跨 pass 輸入）
    uint32 GetColorTexture() const { return colorTex; }
    uint32 GetDepthRenderbuffer() const { return depthRb; }

    // 綁定 FBO 並把 viewport 切到目標尺寸
    void Bind() const;
    // 回預設 framebuffer（viewport 由呼叫方依其原尺寸回復——
    // FBO 無法知道綁定前的 viewport 是什麼）
    void Unbind() const;

    // 像素讀回（Bind 狀態下呼叫；輸出 resize 成 W*H*4 / W*H）。
    // ReadDepth 是原始深度 [0,1]；線性化由消費端依投影決定
    bool ReadColor(std::vector<unsigned char>& outRGBA) const;
    bool ReadDepth(std::vector<float>& outDepth) const;

private:
    void MoveFrom(RenderTarget& o) noexcept {
        fbo = o.fbo; colorTex = o.colorTex; depthRb = o.depthRb;
        width = o.width; height = o.height;
        o.fbo = o.colorTex = o.depthRb = 0;
        o.width = o.height = 0;
    }

    uint32 fbo = 0;
    uint32 colorTex = 0;
    uint32 depthRb = 0;
    int width = 0;
    int height = 0;
};

} // namespace Potato
