#pragma once

#include "Core/CoreTypes.h"
#include "MathUtils/Vector3.h"
#include "MathUtils/Matrix4.h"
#include "Rendering/OpenGLRenderer.h" // Shader
#include <vector>

namespace Potato {

class GBuffer;
class ShadowMap;

/**
 * 延遲光照的光源描述（世界空間輸入，內部轉 view space）。
 */
struct DeferredDirLight {
    Vector3 direction;          // 光線行進方向（自動正規化）
    Vector3 color{1, 1, 1};
    float intensity = 1.0f;
    bool castShadow = false;    // 僅第一支 castShadow 的方向光吃 shadow map
};

struct DeferredPointLight {
    Vector3 position;           // 世界空間
    Vector3 color{1, 1, 1};
    float intensity = 1.0f;
    float radius = 10.0f;       // 衰減窗口（(1-d/r)^2，r 外歸零）
};

/**
 * DeferredLighting — 延遲光照合成 pass。
 *
 * 管線位置：GBuffer（position/normal/albedo）→ 本類全螢幕 pass
 *   → HDR 目標（之後接 Bloom/ToneMapper）。
 * GBuffer 為 view space，光源世界空間描述、上傳前由 Pack*
 * 轉 view space（純 CPU，headless 可測）；陰影採樣需要世界座標，
 * shader 內以 invView 把 gPosition 還原。
 *
 * 上限：方向光 4、點光 8（GLSL 固定陣列，超過截斷——Pack* 同步夾）。
 * 光照模型：Blinn-Phong（與 LightingCalculator 同族）+ ambient +
 *   可選 AO 乘法 + 第一支 castShadow 方向光的 PCF 陰影。
 */
class DeferredLighting {
public:
    static constexpr int kMaxDirLights = 4;
    static constexpr int kMaxPointLights = 8;

    DeferredLighting() = default;
    ~DeferredLighting();

    DeferredLighting(const DeferredLighting&) = delete;
    DeferredLighting& operator=(const DeferredLighting&) = delete;
    DeferredLighting(DeferredLighting&& o) noexcept
        : shader(std::move(o.shader)), vao(o.vao) {
        o.vao = 0;
    }
    DeferredLighting& operator=(DeferredLighting&& o) noexcept {
        if (this != &o) {
            Destroy();
            shader = std::move(o.shader);
            vao = o.vao;
            o.vao = 0;
        }
        return *this;
    }

    bool Create();
    void Destroy();
    bool IsValid() const { return shader != nullptr; }

    // ---- 純 CPU（headless 可測）----
    // 方向光 → view-space 打平：[dir.xyz(已正規化), rgb*intensity] × min(n,kMax)
    static std::vector<float> PackDirLights(
        const std::vector<DeferredDirLight>& dirs, const Matrix4& view);
    // 點光 → view-space 打平：[pos.xyz(view), rgb*intensity, radius] × min(n,kMax)
    static std::vector<float> PackPointLights(
        const std::vector<DeferredPointLight>& points, const Matrix4& view);
    // 第一支 castShadow 方向光索引（無 → -1）
    static int FirstShadowCaster(const std::vector<DeferredDirLight>& dirs);

    // 全螢幕光照 pass，輸出到目前綁定 framebuffer（建議 HDRRenderTarget）。
    // gbuffer：GBuffer::GetXxxTexture 自動接 0/1/2 unit
    // view：幾何 pass 用的 view 矩陣（光源轉 view + 陰影還原世界座標）
    // aoTex：AmbientOcclusion::GetAOTexture()，0 = 不乘 AO
    // shadow + lightSpace：ShadowMap 與其矩陣；shadow=nullptr 不取樣。
    //   texelSize 自 GetSize() 推得；只有第一支 castShadow 的方向光受陰影
    void Render(const GBuffer& gbuffer,
                const std::vector<DeferredDirLight>& dirs,
                const std::vector<DeferredPointLight>& points,
                const Matrix4& view,
                uint32 aoTex = 0,
                const ShadowMap* shadow = nullptr,
                const Matrix4& lightSpace = Matrix4::Identity(),
                float ambient = 0.05f,
                float shadowBias = 0.005f);

    static const char* VertexShader();   // 全螢幕三角
    static const char* FragmentShader(); // 延遲光照（Blinn-Phong + PCF + AO）

private:
    UniquePtr<Shader> shader;
    uint32 vao = 0;
};

} // namespace Potato
