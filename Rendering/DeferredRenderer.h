#pragma once

#include "Core/CoreTypes.h"
#include "MathUtils/Vector3.h"
#include "MathUtils/Matrix4.h"
#include "Rendering/AmbientOcclusion.h"
#include "Rendering/DeferredShading.h"
#include "Rendering/GBuffer.h"
#include "Rendering/PostProcess.h"
#include "Rendering/PointShadowMap.h"
#include "Rendering/RenderTarget.h"
#include "Rendering/ScreenSpaceReflection.h"
#include "Rendering/ShadowMap.h"
#include "Rendering/CascadedShadowMap.h"
#include <functional>
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
    // CSM：useCascades=true 時以級聯深度圖取代單張 shadowMap
    // （大場景用；深度 pass 改走 BeginCascadedShadowPass）
    bool useCascades = false;
    int cascadeCount = 4;              // 1..CascadedShadowMap::kMaxCascades
    float cascadeLambda = 0.6f;        // 對數×均勻混合權重
    int spotShadowMapSize = 1024;      // 聚光陰影貼圖邊長
    int pointShadowMapSize = 512;      // 點光 cube 每面邊長
    // SSR：光照合成後在 HDR 上 raymarch 反射，bloom 前疊入
    bool enableSSR = false;
    SSRSettings ssr;
    // FXAA：tone map 輸出先進 RGBA8 中介 target,再 FXAA
    // 到 outputFbo——延遲管線不吃 MSAA（GBuffer 無法多採樣化）
    bool enableFXAA = false;
    FxaaSettings fxaa;
};

class EnvironmentMaps;
class DeferredRenderer;

// 透明物件逐次繪製材質（forward pass——透明物件不寫 GBuffer）
struct TransparentMaterial {
    Vector3 albedo{1.0f, 1.0f, 1.0f};
    float alpha = 0.5f;          // src 權重（GL_SRC_ALPHA blend）
    float metallic = 0.0f;
    float roughness = 0.5f;
    float emissive = 0.0f;       // 乘 emissiveScale 的自發光
};

// QueueTransparent 佇列元素——mesh 指標須存活至 RenderFrame
struct TransparentDraw {
    const Mesh* mesh = nullptr;
    Matrix4 model = Matrix4::Identity();
    TransparentMaterial material;
};

// QueueGeometry 佇列元素——不透明幾何（GBuffer pass,平面材質）
struct OpaqueDraw {
    const Mesh* mesh = nullptr;
    Matrix4 model = Matrix4::Identity();
    Vector3 albedo{0.9f, 0.9f, 0.9f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    float emissive = 0.0f;
};

struct DeferredFrameParams {
    Matrix4 view;
    Matrix4 projection;
    std::vector<DeferredDirLight> dirs;
    std::vector<DeferredPointLight> points;
    std::vector<DeferredSpotLight> spots;
    float ambient = 0.05f;
    float exposure = 1.0f;
    float bloomStrength = 1.0f;
    float shadowBias = 0.005f;
    // IBL：非空時光照 ambient 換成環境光（irradiance+prefilter+LUT）
    const EnvironmentMaps* environment = nullptr;
    float iblIntensity = 1.0f;
    float matRoughness = 0.6f;
    float matMetallic = 0.0f;
    // per-pixel 材質通道（GBuffer attachment 3）；false = 退回純量
    bool useMaterialChannel = true;
    float emissiveScale = 1.0f;
    // 透明物件：非空時 RenderFrame 在延遲光照之後、SSR/bloom
    // 之前自動跑 forward pass——callback 內呼叫傳入 renderer 的
    // DrawTransparent()（深度測試對 GBuffer 深度、不寫深度、
    // alpha blend、沿用同組光源與陰影）。與 QueueTransparent
    // 佇列可並用：佇列先按深度排序畫完，callback 接著執行
    std::function<void(DeferredRenderer&)> drawTransparent;
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
    // CSM 深度 pass：回傳每級的 light-space 矩陣（供深度 shader
    // 的 lightSpaceMatrix uniform）；逐級呼叫 BindCascade(i) 後
    // 用對應矩陣畫場景，全部畫完呼叫 EndShadowPass。
    // nearZ/farZ 為相機 view-space 正距離（與 projection 對應）
    std::vector<Matrix4> BeginCascadedShadowPass(
        const Vector3& dir, const Matrix4& view, const Matrix4& proj,
        float nearZ, float farZ);
    void BindCascade(int i);
    // 聚光深度 pass：綁 spot shadow FBO 並回傳透視 light-space
    // 矩陣（供深度 shader 與 RenderFrame 採樣共用）；
    // spot 用第一支 castShadow 聚光
    Matrix4 BeginSpotShadowPass(const DeferredSpotLight& spot);
    // 點光深度 pass：回傳六面 light-space 矩陣，逐面
    //   BindPointShadowFace(i) 後以 matrices[i] 畫場景，
    //   六面畫完呼叫 EndShadowPass；point 用第一支 castShadow 點光
    std::vector<Matrix4> BeginPointShadowPass(
        const DeferredPointLight& point);
    void BindPointShadowFace(int i);
    void EndShadowPass();

    // ---- Pass 1b：幾何 ----
    // 綁 GBuffer + 清 color/depth + 開深度測試；結束呼叫 EndGeometry
    void BeginGeometry();
    void EndGeometry();

    // ---- Pass 2-4：AO → 光照 → HDR → bloom → tone map ----
    // 輸出 LDR 到 outputFbo（0 = 預設 framebuffer），viewport 切全尺寸
    void RenderFrame(const DeferredFrameParams& params, uint32 outputFbo = 0);

    // ---- 透明 forward pass（手動路徑；一般用 params.drawTransparent）----
    // Begin：GBuffer 深度 blit 進 HDR depth，綁 HDR FBO，
    //   depth test 開/寫關 + alpha blend，forward shader 上傳
    //   光源/陰影 uniforms（與延遲合成同一套）。
    //   回傳 false = 未建立/已在 pass 中。
    bool BeginTransparencyPass(const DeferredFrameParams& params);
    // 畫一個透明網格（呼叫方負責 back-to-front 排序）
    void DrawTransparent(const Mesh& mesh, const Matrix4& model,
                         const TransparentMaterial& material);
    // 還原 depth mask/blend、解綁 HDR 與陰影貼圖
    void EndTransparencyPass();

    // ---- 透明佇列（自動排序路徑）----
    // 排入透明 draw——RenderFrame 時按 view-space 深度降冪
    // （遠→近，正確 alpha 疊加序）自動排序繪製，免除呼叫方
    // 排序負擔。mesh 指標須存活至 RenderFrame；佇列跨幀保留，
    // ClearTransparentQueue() 清空。
    void QueueTransparent(const Mesh& mesh, const Matrix4& model,
                          const TransparentMaterial& material) {
        transparentQueue.push_back({&mesh, model, material});
    }
    void ClearTransparentQueue() { transparentQueue.clear(); }
    size_t GetTransparentCount() const {
        return transparentQueue.size();
    }

    // 排序鍵（純 CPU，headless 可測）：view*(model 平移) 的
    // view-space z（-Z 為前方，越小越遠）→ 回傳遠→近索引序
    static std::vector<size_t> SortBackToFront(
        const std::vector<TransparentDraw>& draws,
        const Matrix4& view);

    // ---- 不透明佇列（自動視錐剔除路徑）----
    // 排入不透明 draw——BeginGeometry 後呼叫
    // DrawGeometryQueue(view, projection)：先以
    // CullGeometryQueue 剔除視錐外物件，再以內建 GBuffer
    // shader 逐次繪製（平面材質、不綁 albedoMap）。
    // mesh 指標須存活至繪製；佇列跨幀保留。
    void QueueGeometry(const OpaqueDraw& draw) {
        geometryQueue.push_back(draw);
    }
    void ClearGeometryQueue() { geometryQueue.clear(); }
    size_t GetGeometryQueueSize() const {
        return geometryQueue.size();
    }

    // 剔除（純 CPU，headless 可測）：viewProj 提取 Frustum，
    // mesh 本地 AABB 經 model 變換後全在視錐外才剔除；
    // mesh==null 或無 bounds 的 draw 跳過不畫。回傳保留索引序
    static std::vector<size_t> CullGeometryQueue(
        const std::vector<OpaqueDraw>& draws,
        const Matrix4& viewProj);

    // 在 BeginGeometry/EndGeometry 之間呼叫：剔除後逐 draw
    // 上傳 model/albedo/材質 uniforms 並繪製，回傳實際繪製數
    size_t DrawGeometryQueue(const Matrix4& view,
                             const Matrix4& projection);

    // 子系統存取（除錯/自訂合成）
    GBuffer& GetGBuffer() { return gbuffer; }
    AmbientOcclusion& GetSSAO() { return ao; }
    ShadowMap& GetShadowMap() { return shadowMap; }
    ShadowMap& GetSpotShadowMap() { return spotShadowMap; }
    PointShadowMap& GetPointShadowMap() { return pointShadowMap; }
    CascadedShadowMap& GetCascadedShadowMap() { return csm; }
    Bloom& GetBloom() { return bloom; }
    HDRRenderTarget& GetHDRTarget() { return hdr; }

private:
    // RenderFrame 與 BeginTransparencyPass 共用的陰影指標解析
    struct ResolvedShadows {
        const ShadowMap* dir = nullptr;        // 單張方向光（非 CSM）
        const CascadedShadowMap* csm = nullptr;
        const ShadowMap* spot = nullptr;
        const PointShadowMap* point = nullptr;
    };
    ResolvedShadows resolveShadows() const;

    DeferredRendererSettings settings;
    Matrix4 lightSpace = Matrix4::Identity();
    bool shadowRendered = false;
    Matrix4 spotLightSpace = Matrix4::Identity();
    bool spotShadowRendered = false;
    float pointShadowFar = 0.0f;
    bool pointShadowRendered = false;
    // 目前綁定的深度 pass（EndShadowPass 依此 unbind 正確 FBO）
    enum class ShadowPass { None, Dir, CSM, Spot, Point };
    ShadowPass activeShadowPass = ShadowPass::None;
    std::vector<Matrix4> csmLightSpace;
    std::vector<float> csmSplits; // N-1 個內部邊界（view-space 距離）

    GBuffer gbuffer;
    AmbientOcclusion ao;
    ShadowMap shadowMap;
    ShadowMap spotShadowMap;
    PointShadowMap pointShadowMap;
    ScreenSpaceReflection ssr;
    CascadedShadowMap csm;
    DeferredLighting lighting;
    HDRRenderTarget hdr;
    Bloom bloom;
    ToneMapper toneMap;
    // FXAA 路徑：tone map → ldr(RGBA8) → fxaa → outputFbo
    RenderTarget ldr;
    FxaaFilter fxaa;
    // forward 透明 pass（光源/陰影 GLSL 與延遲合成共用）
    UniquePtr<Shader> forwardShader;
    bool inTransparencyPass = false;
    std::vector<TransparentDraw> transparentQueue;
    // 不透明佇列 + 內建 GBuffer shader（DrawGeometryQueue 用）
    UniquePtr<Shader> geometryShader;
    std::vector<OpaqueDraw> geometryQueue;

    int width = 0;
    int height = 0;
};

} // namespace Potato
