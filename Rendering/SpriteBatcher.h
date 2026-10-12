#pragma once

#include "Core/CoreTypes.h"
#include "MathUtils/Vector2.h"
#include "MathUtils/Vector3.h"
#include "MathUtils/Matrix4.h"
#include "Rendering/OpenGLRenderer.h" // Shader
#include "Rendering/SpriteAtlas.h"  // SpriteFrame
#include <vector>

namespace Potato {

/**
 * 單一精靈實例（GPU instancing 的 per-instance 屬性）。
 * 全 float 布局——直接 memcpy 進 instance VBO。
 */
struct SpriteInstance {
    float x = 0, y = 0;           // 中心位置（像素座標，左上原點）
    float w = 0, h = 0;           // 尺寸（像素）
    float u0 = 0, v0 = 0;         // uv 左上（SpriteAtlas v=0 為頂端）
    float u1 = 1, v1 = 1;         // uv 右下
    float r = 1, g = 1, b = 1, a = 1; // 色調乘數
    float rotCos = 1, rotSin = 0; // 旋轉（預算 cos/sin，shader 免三角）
    float z = 0;                  // 排序鍵（SortByZ；不上 GPU）

    // 從 SpriteFrame 填 uv（frame==nullptr 時 uv 全 0..1 退化為整圖）
    void SetUV(const SpriteFrame* frame) {
        if (frame) { u0 = frame->u0; v0 = frame->v0;
                     u1 = frame->u1; v1 = frame->v1; }
    }
};

/**
 * SpriteBatcher — GPU instanced 精靈批次渲染器。
 *
 * 單一 VAO：unit quad（attrib 0/1）+ instance VBO（attrib 2..7，
 * glVertexAttribDivisor=1）。Render() 一次 glDrawElementsInstanced
 * 畫完整批——同圖集精靈的 draw call 恆為 1。
 *
 * 用途：RTS 單位圖示/建築/粒子 billboard——接 SpriteAtlas::Find()
 * 回傳的 SpriteFrame 直接填 uv（SetUV）。
 *
 * 混合：呼叫前自行 EnableBlending + SetBlendMode(Alpha)（引擎慣例
 *   狀態由呼叫端管，batcher 不偷改 GL 狀態）。
 *
 * 投影：呼叫端給 Matrix4::Orthographic(0, w, h, 0, -1, 1)
 *   = 像素座標左上原點，與 SpriteAtlas uv 同向。
 *
 * Create 需 GL context；禁拷貝、可 move。
 */
class SpriteBatcher {
public:
    SpriteBatcher() = default;
    ~SpriteBatcher();

    SpriteBatcher(const SpriteBatcher&) = delete;
    SpriteBatcher& operator=(const SpriteBatcher&) = delete;
    SpriteBatcher(SpriteBatcher&& o) noexcept { MoveFrom(o); }
    SpriteBatcher& operator=(SpriteBatcher&& o) noexcept {
        if (this != &o) {
            Destroy();
            MoveFrom(o);
        }
        return *this;
    }

    bool Create();
    void Destroy();
    bool IsValid() const { return vao != 0; }

    // ---- 純 CPU（headless 可測）----
    // 依 z 穩定排序（畫家演算法：z 小先畫 = 在底層）
    static void SortByZ(std::vector<SpriteInstance>& instances);
    // 打平成 float 陣列（每實例 14 float，含 padding 對齊 vec4）
    static std::vector<float> PackInstances(
        const std::vector<SpriteInstance>& instances);

    // ---- GL 路徑 ----
    // 一次 instanced draw 畫完整批。texture 為圖集 GL id。
    // instances 為空 → 早退（不發 draw call）。
    void Render(const std::vector<SpriteInstance>& instances,
                uint32 texture, const Matrix4& projection);

    static const char* VertexShader();
    static const char* FragmentShader();

private:
    void MoveFrom(SpriteBatcher& o) noexcept;

    uint32 vao = 0;
    uint32 quadVbo = 0;    // unit quad 頂點（4 verts × pos2+uv2）
    uint32 quadEbo = 0;    // 6 indices
    uint32 instVbo = 0;    // per-instance 屬性（GL_STREAM_DRAW）
    UniquePtr<Shader> shader;
};

} // namespace Potato
