#include "DeferredRenderer.h"
#include "Logging/Logger.h"
#include <glad/glad.h>
#include <algorithm>

namespace Potato {

DeferredRenderer::~DeferredRenderer() {
    Destroy();
}

bool DeferredRenderer::Create(int w, int h,
                              const DeferredRendererSettings& s) {
    if (w <= 0 || h <= 0) {
        LOG_ERROR("DeferredRenderer::Create 非法尺寸");
        return false;
    }
    Destroy();
    settings = s;

    if (!gbuffer.Create(w, h)) return false;
    if (settings.enableAO && !ao.Create(w, h, settings.ao)) {
        Destroy();
        return false;
    }
    if (settings.enableShadows) {
        const bool ok = settings.useCascades
            ? csm.Create(settings.shadowMapSize, settings.cascadeCount)
            : shadowMap.Create(settings.shadowMapSize);
        if (!ok || !spotShadowMap.Create(settings.spotShadowMapSize) ||
            !pointShadowMap.Create(settings.pointShadowMapSize)) {
            Destroy();
            return false;
        }
    }
    if (!lighting.Create()) {
        Destroy();
        return false;
    }
    // forward 透明 pass shader（光源/陰影與延遲合成共用）
    forwardShader = MakeUnique<Shader>();
    if (!forwardShader->LoadFromSource(
            DeferredLighting::ForwardVertexShader(),
            DeferredLighting::ForwardFragmentShader())) {
        LOG_ERROR("DeferredRenderer forward shader 編譯失敗");
        Destroy();
        return false;
    }
    if (!hdr.Create(w, h)) {
        Destroy();
        return false;
    }
    // 不透明佇列用的內建 GBuffer shader（DrawGeometryQueue）
    geometryShader = MakeUnique<Shader>();
    if (!geometryShader->LoadFromSource(GBuffer::VertexShader(),
                                        GBuffer::FragmentShader())) {
        LOG_ERROR("DeferredRenderer geometry shader 編譯失敗");
        Destroy();
        return false;
    }
    if (settings.enableBloom && !bloom.Create(w, h, settings.bloom)) {
        Destroy();
        return false;
    }
    if (settings.enableSSR && !ssr.Create(w, h, settings.ssr)) {
        Destroy();
        return false;
    }
    if (!toneMap.Create()) {
        Destroy();
        return false;
    }
    if (settings.enableFXAA) {
        if (!ldr.Create(w, h) || !fxaa.Create()) {
            LOG_ERROR("DeferredRenderer FXAA 路徑建立失敗");
            Destroy();
            return false;
        }
    }

    width = w;
    height = h;
    return true;
}

void DeferredRenderer::Destroy() {
    inTransparencyPass = false;
    forwardShader.reset();
    geometryShader.reset();
    fxaa.Destroy();
    ldr.Destroy();
    toneMap.Destroy();
    ssr.Destroy();
    bloom.Destroy();
    hdr.Destroy();
    lighting.Destroy();
    csm.Destroy();
    pointShadowMap.Destroy();
    spotShadowMap.Destroy();
    shadowMap.Destroy();
    ao.Destroy();
    gbuffer.Destroy();
    shadowRendered = false;
    spotShadowRendered = false;
    pointShadowRendered = false;
    activeShadowPass = ShadowPass::None;
    width = height = 0;
}

Matrix4 DeferredRenderer::BeginShadowPass(const Vector3& dir,
                                        const Vector3& center,
                                        float radius) {
    lightSpace = ShadowMap::ComputeLightSpaceMatrix(dir, center, radius);
    shadowMap.Bind();
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    shadowRendered = true;
    activeShadowPass = ShadowPass::Dir;
    return lightSpace;
}

std::vector<Matrix4> DeferredRenderer::BeginCascadedShadowPass(
    const Vector3& dir, const Matrix4& view, const Matrix4& proj,
    float nearZ, float farZ) {
    csmLightSpace.clear();
    csmSplits.clear();
    if (!settings.enableShadows || !settings.useCascades ||
        !csm.IsValid()) {
        shadowRendered = false;
        activeShadowPass = ShadowPass::None;
        return csmLightSpace;
    }
    activeShadowPass = ShadowPass::CSM;
    const auto edges = CascadedShadowMap::ComputeSplits(
        nearZ, farZ, csm.GetCascadeCount(), settings.cascadeLambda);
    for (int i = 0; i < csm.GetCascadeCount(); ++i) {
        const auto corners = CascadedShadowMap::FrustumSliceCorners(
            view, proj, edges[i], edges[i + 1]);
        csmLightSpace.push_back(
            CascadedShadowMap::ComputeSliceLightSpace(
                dir, corners.data(), csm.GetSize()));
    }
    csmSplits.assign(edges.begin() + 1, edges.end() - 1);
    shadowRendered = true;
    return csmLightSpace;
}

void DeferredRenderer::BindCascade(int i) {
    csm.BindCascade(i);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
}

Matrix4 DeferredRenderer::BeginSpotShadowPass(
    const DeferredSpotLight& spot) {
    spotLightSpace = ShadowMap::ComputeSpotLightSpaceMatrix(
        spot.position, spot.direction, spot.outerCos, spot.radius);
    spotShadowMap.Bind();
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    spotShadowRendered = true;
    activeShadowPass = ShadowPass::Spot;
    return spotLightSpace;
}

std::vector<Matrix4> DeferredRenderer::BeginPointShadowPass(
    const DeferredPointLight& point) {
    const auto mats = PointShadowMap::ComputeFaceMatrices(
        point.position, point.radius);
    pointShadowFar = point.radius;
    pointShadowRendered = true;
    activeShadowPass = ShadowPass::Point;
    return mats;
}

void DeferredRenderer::BindPointShadowFace(int i) {
    pointShadowMap.BindFace(i);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
}

void DeferredRenderer::EndShadowPass() {
    switch (activeShadowPass) {
        case ShadowPass::CSM:   csm.Unbind(); break;
        case ShadowPass::Spot:  spotShadowMap.Unbind(); break;
        case ShadowPass::Point: pointShadowMap.Unbind(); break;
        case ShadowPass::Dir:   shadowMap.Unbind(); break;
        case ShadowPass::None:  break;
    }
    activeShadowPass = ShadowPass::None;
}

void DeferredRenderer::BeginGeometry() {
    gbuffer.Bind();
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
}

void DeferredRenderer::EndGeometry() {
    gbuffer.Unbind();
}

void DeferredRenderer::RenderFrame(const DeferredFrameParams& p,
                                   uint32 outputFbo) {
    if (!IsValid()) {
        return;
    }

    // AO：由 GBuffer 算遮蔽貼圖
    uint32 aoTex = 0;
    if (settings.enableAO && ao.IsValid()) {
        ao.Compute(gbuffer, p.projection);
        aoTex = ao.GetAOTexture();
    }

    // 光照：延遲合成 → HDR
    const ResolvedShadows sh = resolveShadows();
    hdr.Bind();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    lighting.Render(gbuffer, p.dirs, p.points, p.spots, p.view, aoTex,
                    sh.dir,
                    lightSpace, p.ambient, p.shadowBias,
                    p.environment, p.iblIntensity,
                    p.matRoughness, p.matMetallic,
                    p.useMaterialChannel, p.emissiveScale,
                    sh.csm,
                    sh.csm ? csmLightSpace.data() : nullptr,
                    sh.csm ? csmSplits.data() : nullptr,
                    p.projection, sh.spot, spotLightSpace,
                    sh.point, pointShadowFar);
    hdr.Unbind();

    // 透明物件：forward pass 疊進 HDR（深度對 GBuffer、不寫深度、
    // alpha blend）——須在 SSR/bloom 之前讓反射與亮部也吃到。
    // 佇列先按 view 深度排序畫，drawTransparent 回呼接著執行
    if ((p.drawTransparent || !transparentQueue.empty()) &&
        BeginTransparencyPass(p)) {
        const auto order = SortBackToFront(transparentQueue, p.view);
        for (const size_t i : order) {
            DrawTransparent(*transparentQueue[i].mesh,
                            transparentQueue[i].model,
                            transparentQueue[i].material);
        }
        if (p.drawTransparent) {
            p.drawTransparent(*this);
        }
        EndTransparencyPass();
    }

    // SSR：光照 HDR 上 raymarch 反射，合成進 scene 後才抽 bloom
    uint32 sceneTex = hdr.GetColorTexture();
    if (settings.enableSSR && ssr.IsValid()) {
        ssr.Compute(gbuffer, sceneTex, p.projection);
        ssr.Apply(sceneTex);
        sceneTex = ssr.GetResolvedTexture();
    }

    // Bloom 亮部提取 + blur
    uint32 bloomTex = 0;
    if (settings.enableBloom && bloom.IsValid()) {
        bloom.Extract(sceneTex);
        bloomTex = bloom.GetTexture();
    }

    // 最終 tone map → outputFbo；enableFXAA 時先進 RGBA8
    // 中介（ldr）再 FXAA 平滑邊緣輸出
    if (settings.enableFXAA && fxaa.IsValid() && ldr.IsValid()) {
        glBindFramebuffer(GL_FRAMEBUFFER, ldr.GetFBO());
        glViewport(0, 0, width, height);
        toneMap.Apply(sceneTex, bloomTex, p.exposure,
                      p.bloomStrength);
        glBindFramebuffer(GL_FRAMEBUFFER, outputFbo);
        glViewport(0, 0, width, height);
        fxaa.Apply(ldr.GetColorTexture(), width, height,
                   settings.fxaa);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, outputFbo);
        glViewport(0, 0, width, height);
        toneMap.Apply(sceneTex, bloomTex, p.exposure,
                      p.bloomStrength);
    }
}

DeferredRenderer::ResolvedShadows DeferredRenderer::resolveShadows()
    const {
    ResolvedShadows r;
    const bool shadowsOn = settings.enableShadows && shadowRendered;
    r.csm = (shadowsOn && settings.useCascades) ? &csm : nullptr;
    r.dir = (shadowsOn && !settings.useCascades) ? &shadowMap
                                               : nullptr;
    r.spot = (settings.enableShadows && spotShadowRendered)
                 ? &spotShadowMap
                 : nullptr;
    r.point = (settings.enableShadows && pointShadowRendered)
                  ? &pointShadowMap
                  : nullptr;
    return r;
}

bool DeferredRenderer::BeginTransparencyPass(
    const DeferredFrameParams& p) {
    if (!IsValid() || !forwardShader || inTransparencyPass) {
        return false;
    }
    // GBuffer 深度 blit 進 HDR depth——透明物件受不透明幾何
    // 遮蔽（深度測試對齊不透明場景）
    glBindFramebuffer(GL_READ_FRAMEBUFFER, gbuffer.GetFBO());
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, hdr.GetFBO());
    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height,
                      GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

    hdr.Bind();
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE); // 透明物件不寫深度（避免互相遮蔽）
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    forwardShader->Bind();
    forwardShader->SetUniformMat4("view", p.view);
    forwardShader->SetUniformMat4("projection", p.projection);
    forwardShader->SetUniformFloat("emissiveScale", p.emissiveScale);
    const ResolvedShadows sh = resolveShadows();
    DeferredLighting::UploadLights(
        *forwardShader, p.dirs, p.points, p.spots, p.view, p.ambient,
        p.shadowBias, sh.dir, lightSpace, sh.csm,
        sh.csm ? csmLightSpace.data() : nullptr,
        sh.csm ? csmSplits.data() : nullptr,
        sh.spot, spotLightSpace, sh.point, pointShadowFar);
    // 透明物件同吃 IBL 環境光（irradiance 漫射 + prefilter 鏡面
    // + BRDF LUT），與延遲合成同一套 split-sum 近似
    DeferredLighting::UploadIBL(*forwardShader, p.environment,
                                p.iblIntensity);
    inTransparencyPass = true;
    return true;
}

void DeferredRenderer::DrawTransparent(
    const Mesh& mesh, const Matrix4& model,
    const TransparentMaterial& material) {
    if (!inTransparencyPass || !forwardShader) {
        return;
    }
    forwardShader->SetUniformMat4("model", model);
    forwardShader->SetUniformVec3("materialAlbedo", material.albedo);
    forwardShader->SetUniformFloat("materialAlpha", material.alpha);
    forwardShader->SetUniformFloat("materialMetallic",
                                   material.metallic);
    forwardShader->SetUniformFloat("materialRoughness",
                                   material.roughness);
    forwardShader->SetUniformFloat("materialEmissive",
                                   material.emissive);
    mesh.Draw();
}

std::vector<size_t> DeferredRenderer::SortBackToFront(
    const std::vector<TransparentDraw>& draws, const Matrix4& view) {
    std::vector<size_t> order(draws.size());
    std::vector<float> depth(draws.size());
    for (size_t i = 0; i < draws.size(); ++i) {
        order[i] = i;
        // model 平移 → view space（-Z 前方；越小越遠）
        depth[i] =
            view.TransformPoint(
                    draws[i].model.TransformPoint(
                        Vector3(0.0f, 0.0f, 0.0f)))
                .z;
    }
    std::sort(order.begin(), order.end(),
              [&](const size_t a, const size_t b) {
                  return depth[a] < depth[b];
              });
    return order;
}

std::vector<size_t> DeferredRenderer::CullGeometryQueue(
    const std::vector<OpaqueDraw>& draws,
    const Matrix4& viewProj) {
    const Frustum frustum = Frustum::FromMatrix(viewProj);
    std::vector<size_t> kept;
    kept.reserve(draws.size());
    for (size_t i = 0; i < draws.size(); ++i) {
        const OpaqueDraw& d = draws[i];
        if (!d.mesh || !d.mesh->HasBounds()) {
            continue; // 無從剔除也不該畫——直接跳過
        }
        const AABB world =
            d.mesh->GetLocalAABB().Transformed(d.model);
        if (frustum.ContainsAABB(world)) {
            kept.push_back(i);
        }
    }
    return kept;
}

size_t DeferredRenderer::DrawGeometryQueue(
    const Matrix4& view, const Matrix4& projection) {
    if (!geometryShader) {
        return 0;
    }
    const auto kept =
        CullGeometryQueue(geometryQueue, projection * view);
    if (kept.empty()) {
        return 0;
    }
    geometryShader->Bind();
    geometryShader->SetUniformMat4("view", view);
    geometryShader->SetUniformMat4("projection", projection);
    geometryShader->SetUniformInt("useTexture", 0);
    for (const size_t i : kept) {
        const OpaqueDraw& d = geometryQueue[i];
        geometryShader->SetUniformMat4("model", d.model);
        geometryShader->SetUniformVec3("albedo", d.albedo);
        geometryShader->SetUniformFloat("materialMetallic",
                                       d.metallic);
        geometryShader->SetUniformFloat("materialRoughness",
                                       d.roughness);
        geometryShader->SetUniformFloat("materialEmissive",
                                       d.emissive);
        d.mesh->Draw();
    }
    geometryShader->Unbind();
    return kept.size();
}

void DeferredRenderer::EndTransparencyPass() {
    if (!inTransparencyPass) {
        return;
    }
    // 解綁陰影/IBL 貼圖單元（與 UploadLights/UploadIBL 綁定的
    // 4/5/6/7/9/11/12 對應）
    glActiveTexture(GL_TEXTURE12);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    glActiveTexture(GL_TEXTURE11);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    hdr.Unbind();
    inTransparencyPass = false;
}

} // namespace Potato
