#pragma once

#include "Core/CoreTypes.h"
#include "MathUtils/Vector3.h"
#include "MathUtils/Matrix4.h"

namespace Potato {

/**
 * ShadowMap — 方向光陰影貼圖（depth-only FBO + PCF 採樣）。
 *
 * 管線：
 *   1. ComputeLightSpaceMatrix() 由光方向 + 場景包圍球求 light-space
 *      view*proj（正交投影，純 CPU，headless 可測）
 *   2. 深度 pass：Bind() 後用 DepthVertexShader/DepthFragmentShader
 *      畫場景（只需 position 屬性）→ GetDepthMap()
 *   3. 光照 pass：取樣 GetDepthMap()；SamplingGLSL() 提供現成的
 *      ShadowFactor()（3x3 PCF + 邊界 CLAMP_TO_BORDER=1 處理），
 *      以字串拼接嵌進消費端 fragment shader
 *
 * 深度紋理：GL_DEPTH_COMPONENT24 + CLAMP_TO_BORDER(1,1,1,1)——
 * 視錐外採樣回 1.0（最遠）→ 自動無陰影。
 *
 * Create 需 GL context 已建立（同 RenderTarget）；禁拷貝、可 move。
 */
class ShadowMap {
public:
    ShadowMap() = default;
    ~ShadowMap();

    ShadowMap(const ShadowMap&) = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;
    ShadowMap(ShadowMap&& o) noexcept { MoveFrom(o); }
    ShadowMap& operator=(ShadowMap&& o) noexcept {
        if (this != &o) {
            Destroy();
            MoveFrom(o);
        }
        return *this;
    }

    // 建立/重建（size = 深度貼圖邊長，常見 1024/2048）。重入安全。
    bool Create(int size);
    void Destroy();

    bool IsValid() const { return fbo != 0; }
    int GetSize() const { return size; }
    uint32 GetDepthMap() const { return depthTex; }
    uint32 GetFBO() const { return fbo; }

    // 綁定深度 pass FBO + viewport 切成貼圖尺寸
    void Bind() const;
    // 回預設 framebuffer（viewport 由呼叫方依其原尺寸回復）
    void Unbind() const;

    // ---- 純 CPU（headless 可測）----
    // 方向光 light-space 矩陣：光的 view 從包圍球中心逆光方向退 2*radius，
    // 正交範圍 [-radius, radius]，深度涵蓋整顆球。
    // dir：光照方向（光線行進方向，會自動正規化）；
    // center/radius：場景包圍球。
    static Matrix4 ComputeLightSpaceMatrix(const Vector3& dir,
                                           const Vector3& center,
                                           float radius);

    // ---- 內建 GLSL 330 ----
    // 深度 pass shader：uniform model + lightSpaceMatrix，只需 attrib 0=position
    static const char* DepthVertexShader();
    static const char* DepthFragmentShader();
    // 光照端片段：ShadowFactor(fragWorldPos, normal, lightDir,
    //   lightSpace, shadowMap, bias, texelSize) → [0,1]（1=全亮）
    // 供字串拼接嵌進消費端 fragment shader
    static const char* SamplingGLSL();

private:
    void MoveFrom(ShadowMap& o) noexcept {
        fbo = o.fbo; depthTex = o.depthTex; size = o.size;
        o.fbo = o.depthTex = 0;
        o.size = 0;
    }

    uint32 fbo = 0;
    uint32 depthTex = 0;
    int size = 0;
};

} // namespace Potato
