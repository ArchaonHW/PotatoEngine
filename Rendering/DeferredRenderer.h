#pragma once

#include "Core/CoreTypes.h"
#include "MathUtils/Vector3.h"
#include "MathUtils/Matrix4.h"
#include "Rendering/AmbientOcclusion.h"
#include "Rendering/DeferredShading.h"
#include "Rendering/GBuffer.h"
#include "Rendering/PostProcess.h"
#include "Rendering/ShadowMap.h"
#include <vector>

namespace Potato {

/**
 * DeferredRenderer — 延遲渲染管線總成（facade）。
 *
 * 把 GBuffer/SSAO/ShadowMap/DeferredLighting/HDR/Bloom/ToneMapper
 * 串成一條呼叫：
 *
 *   DeferredRenderer dr;
 *   dr.Create(w, h);
 *
 *   // 每幀
 *   Matrix4 ls = dr.BeginShadowPass(lightDir, sceneCenter, sceneRadius);
 *   ...用 ShadowMap::Depth*Shader 畫場景（depth pass）...
 *   dr.EndShadowPass();
 *
 *   dr.BeginGeometry();
 *   ...用 GBuffer::VertexShader/FragmentShader 畫場景（幾何 pass）...
 *   dr.EndGeometry();
 *
 *   dr.RenderFrame(frame);   // AO → 光照(+陰影) → HDR → bloom → ACES
 *
 * RenderFrame 輸出到 outputFbo（0 = 預設 framebuffer 螢幕）。
 * 全程 GL 依賴——Create 需 context 已建立。
 */
struct DeferredRendererSettings {
    SSAOSettings ao;
    BloomSettings bloom;
    int shadowMapSize = 2048;
    bool enableAO = true;
    bool enableShadows = true;
    bool enableBloom = true;
};

struct DeferredFrameParams {
    Matrix4 view;
    Matrix4 projection;
    std::vector<DeferredDirLight> dirs;
    std::vector<DeferredPointLight> points;
    float ambient = 0.05f;
    float exposure = 1.0f;
    float bloomStrength = 1.0f;
    float shadowBias = 0.005f;
};

class DeferredRenderer {
public:
    DeferredRenderer() = default;
    ~DeferredRenderer();

    DeferredRenderer(const DeferredRenderer&) = delete;
    DeferredRenderer& operator=(const DeferredRenderer&) = delete;
    // 成員全為 GL 資源且部分不可 move（UniquePtr 包 Shader），
    // facade 同樣禁拷貝、可 move
    DeferredRenderer(DeferredRenderer&&) = default;
    DeferredRenderer& operator=(DeferredRenderer&&) = default;

    bool Create(int w, int h,
                const DeferredRendererSettings& settings =
                    DeferredRendererSettings());
    void Destroy();
    bool IsValid() const { return lighting.IsValid(); }
    int GetWidth() const { return width; }
    int GetHeight() const { return height; }
    const DeferredRendererSettings& GetSettings() const { return settings; }

    // ---- Pass 1a：陰影深度（可省略 → 陰影關閉）----
    // 綁 shadow map FBO 並回傳 light-space 矩陣（給深度 shader 的
    // lightSpaceMatrix uniform 與後續 RenderFrame 採樣共用）。
    // dir：第一支 castShadow 方向光的行進方向
    Matrix4 BeginShadowPass(const Vector3& dir, const Vector3& center,
                            float radius);
    void EndShadowPass();

    // ---- Pass 1b：幾何 ----
    // 綁 GBuffer + 清 color/depth + 開深度測試；結束呼叫 EndGeometry
    void BeginGeometry();
    void EndGeometry();

    // ---- Pass 2-4：AO → 光照 → HDR → bloom → tone map ----
    // 輸出 LDR 到 outputFbo（0 = 預設 framebuffer），viewport 切全尺寸
    void RenderFrame(const DeferredFrameParams& params, uint32 outputFbo = 0);

    // 子系統存取（除錯/自訂合成）
    GBuffer& GetGBuffer() { return gbuffer; }
    AmbientOcclusion& GetSSAO() { return ao; }
    ShadowMap& GetShadowMap() { return shadowMap; }
    Bloom& GetBloom() { return bloom; }
    HDRRenderTarget& GetHDRTarget() { return hdr; }

private:
    DeferredRendererSettings settings;
    Matrix4 lightSpace = Matrix4::Identity();
    bool shadowRendered = false;

    GBuffer gbuffer;
    AmbientOcclusion ao;
    ShadowMap shadowMap;
    DeferredLighting lighting;
    HDRRenderTarget hdr;
    Bloom bloom;
    ToneMapper toneMap;

    int width = 0;
    int height = 0;
};

} // namespace Potato
