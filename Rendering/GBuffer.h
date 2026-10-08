#pragma once

#include "Core/CoreTypes.h"

namespace Potato {

/**
 * G-Buffer（MRT FBO）：view-space 幾何資訊三通道 + depth24/stencil8。
 *
 * attachment 0 gPosition：GL_RGBA16F，view-space 位置（xyz）
 * attachment 1 gNormal  ：GL_RGBA16F，view-space 法線（xyz）
 * attachment 2 gAlbedo  ：GL_RGBA8，材質反照率
 *
 * 用途：SSAO（AmbientOcclusion）、延遲光照、SSR 等螢幕空間技術
 * 的共用地基建——比 RenderTarget 多兩張 attachment 並設好 draw buffers。
 *
 * 幾何 pass 用內建 VertexShader()/FragmentShader()：
 *   - 頂點屬性對齊 Mesh 標準佈局（0=position, 1=normal, 2=texCoord）
 *   - uniform：model/view/projection（mat4）、albedo（vec3）、
 *     useTexture（bool）、albedoMap（sampler2D）
 *   - 法線在 shader 內以 transpose(inverse(view*model)) 變換，
 *     非等比縮放亦正確，不需額外 normalMatrix uniform
 *
 * 使用：
 *   GBuffer gb;
 *   if (!gb.Create(w, h)) return;   // 需 GL context 已建立（同 RenderTarget）
 *   gb.Bind();
 *   ...用 gb 內建 shader 畫場景...
 *   gb.Unbind();
 *   ssao.Compute(gb, proj);
 *
 * GL id 唯一所有權（同 RenderTarget 慣例）：禁拷貝、可 move。
 */
class GBuffer {
public:
    GBuffer() = default;
    ~GBuffer();

    GBuffer(const GBuffer&) = delete;
    GBuffer& operator=(const GBuffer&) = delete;
    GBuffer(GBuffer&& o) noexcept { MoveFrom(o); }
    GBuffer& operator=(GBuffer&& o) noexcept {
        if (this != &o) {
            Destroy();
            MoveFrom(o);
        }
        return *this;
    }

    // 建立/重建；尺寸非法或 FBO 不完整回 false。重入安全（先 Destroy）。
    bool Create(int w, int h);
    void Destroy();

    bool IsValid() const { return fbo != 0; }
    int GetWidth() const { return width; }
    int GetHeight() const { return height; }

    uint32 GetPositionTexture() const { return positionTex; }
    uint32 GetNormalTexture() const { return normalTex; }
    uint32 GetAlbedoTexture() const { return albedoTex; }
    uint32 GetFBO() const { return fbo; }

    // 綁定 FBO 並把 viewport 切到目標尺寸
    void Bind() const;
    // 回預設 framebuffer（viewport 由呼叫方依其原尺寸回復）
    void Unbind() const;

    // 幾何 pass GLSL 330 源碼（供 Shader::LoadFromSource）
    static const char* VertexShader();
    static const char* FragmentShader();

private:
    void MoveFrom(GBuffer& o) noexcept {
        fbo = o.fbo; positionTex = o.positionTex; normalTex = o.normalTex;
        albedoTex = o.albedoTex; depthRb = o.depthRb;
        width = o.width; height = o.height;
        o.fbo = o.positionTex = o.normalTex = o.albedoTex = o.depthRb = 0;
        o.width = o.height = 0;
    }

    uint32 fbo = 0;
    uint32 positionTex = 0;
    uint32 normalTex = 0;
    uint32 albedoTex = 0;
    uint32 depthRb = 0;
    int width = 0;
    int height = 0;
};

} // namespace Potato
