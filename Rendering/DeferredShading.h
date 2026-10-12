#pragma once

#include "Core/CoreTypes.h"
#include "MathUtils/Vector2.h"
#include "MathUtils/Vector3.h"
#include "MathUtils/Matrix4.h"
#include "Rendering/OpenGLRenderer.h" // Shader
#include <vector>

namespace Potato {

class GBuffer;
class ShadowMap;
class PointShadowMap;
class CascadedShadowMap;
class EnvironmentMaps;

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
    bool castShadow = false;    // 僅第一支 castShadow 點光吃 cube shadow map
};

struct DeferredSpotLight {
    Vector3 position;           // 世界空間
    Vector3 direction{0, -1, 0}; // 照射方向（自動正規化）
    Vector3 color{1, 1, 1};
    float intensity = 1.0f;
    float radius = 10.0f;       // 衰減窗口（同點光）
    // 錐形邊界以 cos(半角) 表示（cos 隨角度遞減）：
    // 入射角 < inner 全強度，inner~outer 線性衰減，> outer 歸零。
    // 預設內 12.5°/外 17.5°；innerCos 須 > outerCos
    float innerCos = 0.976296f;
    float outerCos = 0.953717f;
    bool castShadow = false;    // 僅第一支 castShadow 聚光吃 spot shadow map
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
 * 上限：方向光 4、點光 8、聚光 8（GLSL 固定陣列，超過截斷——
 *   Pack* 同步夾；UploadLights 於溢出時 LOG_WARNING 一次）。
 * 聚光 = 點光衰減窗口 × inner/outer 錐形線性衰減。
 * 光照模型：Cook-Torrance GGX PBR + ambient/IBL +
 *   可選 AO 乘法 + 第一支 castShadow 方向光/聚光/點光的 PCF 陰影。
 * 透明物件走 forward pass（DeferredRenderer::BeginTransparencyPass）：
 *   ForwardFragmentShader 與延遲合成共用光源/陰影/BRDF/光源迴圈
 *   GLSL 與 UploadLights() 上傳邏輯，材質改為逐次繪製 uniform；
 *   IBL 環境項亦同——UploadIBL() 兩路共用（forward 無逐像素
 *   AO/材質通道,亦不輸出天空背景）。
 * 背景像素（無法線）：env 非空時採 env cubemap 當天空輸出 HDR，
 *   讓天空走同一條 bloom/tonemap 鏈；否則退回 albedo*ambient。
 */
class DeferredLighting {
public:
    static constexpr int kMaxDirLights = 4;
    static constexpr int kMaxPointLights = 8;
    static constexpr int kMaxSpotLights = 8;

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
    // 聚光 → view-space 打平：[pos3 | dir3 | rgb*intensity+radius4 |
    //   innerCos+outerCos2] × min(n,kMax)（innerCos<outerCos 時對調）
    static std::vector<float> PackSpotLights(
        const std::vector<DeferredSpotLight>& spots, const Matrix4& view);
    // 第一支 castShadow 方向光索引（無 → -1）
    static int FirstShadowCaster(const std::vector<DeferredDirLight>& dirs);
    // 第一支 castShadow 聚光索引（無 → -1）
    static int FirstSpotShadowCaster(
        const std::vector<DeferredSpotLight>& spots);
    // 第一支 castShadow 點光索引（無 → -1）
    static int FirstPointShadowCaster(
        const std::vector<DeferredPointLight>& points);

    // 螢幕 uv∈[0,1] → 世界空間觀察方向（天空採樣用，純 CPU）
    // 與 fragment shader 的 skyCube 分支同一套數學：
    //   clip=(uv*2-1,1,1) → invProj → 除以 w → mat3(invView) → normalize
    static Vector3 SkyDir(const Vector2& uv,
                          const Matrix4& invView,
                          const Matrix4& invProj);

    // 全螢幕光照 pass，輸出到目前綁定 framebuffer（建議 HDRRenderTarget）。
    // gbuffer：GBuffer::GetXxxTexture 自動接 0/1/2 unit
    // view：幾何 pass 用的 view 矩陣（光源轉 view + 陰影還原世界座標）
    // aoTex：AmbientOcclusion::GetAOTexture()，0 = 不乘 AO
    // shadow + lightSpace：ShadowMap 與其矩陣；shadow=nullptr 不取樣。
    //   texelSize 自 GetSize() 推得；只有第一支 castShadow 的方向光受陰影
    // env：EnvironmentMaps 非空 → ambient 換成 IBL（irradiance 漫射 +
    //   prefiltered 鏡面 + BRDF LUT）；matRoughness/Metallic 為全場景
    //   純量——useMaterialChannel=true 時被 GBuffer gMaterial.rg 逐像素
    //   取代（emissive 也來自 gMaterial.b，不吃 AO、直接加到輸出）
    void Render(const GBuffer& gbuffer,
                const std::vector<DeferredDirLight>& dirs,
                const std::vector<DeferredPointLight>& points,
                const std::vector<DeferredSpotLight>& spots,
                const Matrix4& view,
                uint32 aoTex = 0,
                const ShadowMap* shadow = nullptr,
                const Matrix4& lightSpace = Matrix4::Identity(),
                float ambient = 0.05f,
                float shadowBias = 0.005f,
                const EnvironmentMaps* env = nullptr,
                float iblIntensity = 1.0f,
                float matRoughness = 0.6f,
                float matMetallic = 0.0f,
                bool useMaterialChannel = true,
                float emissiveScale = 1.0f,
                const CascadedShadowMap* csm = nullptr,
                const Matrix4* csmLightSpace = nullptr,
                const float* csmSplits = nullptr,
                const Matrix4& projection = Matrix4::Identity(),
                const ShadowMap* spotShadow = nullptr,
                const Matrix4& spotLightSpace = Matrix4::Identity(),
                const PointShadowMap* pointShadow = nullptr,
                float pointShadowFar = 0.0f);
    // csm 三參數為一組（同時非空才生效，優先於單張 shadow）：
    //   csmLightSpace 為長度 csm->GetCascadeCount() 的矩陣陣列、
    //   csmSplits 為 ComputeSplits 的 N-1 個內部邊界
    //   （view-space 正距離，升冪）
    // projection：幾何 pass 的投影矩陣——useIBL 時背景像素依
    //   SkyDir() 採 env cubemap 當天空（不再輸出死黑）；無 env 時不影響
    // spotShadow + spotLightSpace：第一支 castShadow 聚光的透視
    //   shadow map（ComputeSpotLightSpaceMatrix 產出的矩陣）；
    //   spotShadow=nullptr 不取樣；與方向光陰影獨立、可共存
    // pointShadow + pointShadowFar：第一支 castShadow 點光的
    //   cube 深度方塊與其錄製時的 far（= 光源 radius）

    // 光源陣列+三種陰影 uniform/貼圖上傳到已 Bind 的 shader——
    // Render() 與 forward 透明 pass 共用；uniform 名須與本類
    // 內建 shader 一致（kSharedLightUniformsGLSL）。
    static void UploadLights(Shader& target,
        const std::vector<DeferredDirLight>& dirs,
        const std::vector<DeferredPointLight>& points,
        const std::vector<DeferredSpotLight>& spots,
        const Matrix4& view,
        float ambient,
        float shadowBias,
        const ShadowMap* shadow,
        const Matrix4& lightSpace,
        const CascadedShadowMap* csm,
        const Matrix4* csmLightSpace,
        const float* csmSplits,
        const ShadowMap* spotShadow,
        const Matrix4& spotLightSpace,
        const PointShadowMap* pointShadow,
        float pointShadowFar);

    // IBL 環境光 uniform/貼圖上傳到已 Bind 的 shader——
    // Render() 與 forward 透明 pass（BeginTransparencyPass）
    // 共用。env==nullptr 或無效 → useIBL=0 退回 flat ambient。
    // 貼圖單元：irradiance=5、prefilter=6、brdfLUT=7
    // （與光源/陰影的 4/9/11/12 不衝突）
    static void UploadIBL(Shader& target,
                          const EnvironmentMaps* env,
                          float iblIntensity);

    static const char* VertexShader();   // 全螢幕三角
    static const char* FragmentShader(); // 延遲光照（GGX PBR + PCF + AO + IBL）
    // forward 透明 pass shader——頂點同 GBuffer 幾何 pass，
    // 片段與延遲合成共用光源/陰影/PBR 迴圈，alpha 輸出
    static const char* ForwardVertexShader();
    static const char* ForwardFragmentShader();

private:
    UniquePtr<Shader> shader;
    uint32 vao = 0;
};

} // namespace Potato
