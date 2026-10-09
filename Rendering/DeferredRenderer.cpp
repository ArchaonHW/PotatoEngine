#include "DeferredRenderer.h"
#include "Logging/Logger.h"
#include <glad/glad.h>

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
    if (settings.enableShadows &&
        !shadowMap.Create(settings.shadowMapSize)) {
        Destroy();
        return false;
    }
    if (!lighting.Create()) {
        Destroy();
        return false;
    }
    if (!hdr.Create(w, h)) {
        Destroy();
        return false;
    }
    if (settings.enableBloom && !bloom.Create(w, h, settings.bloom)) {
        Destroy();
        return false;
    }
    if (!toneMap.Create()) {
        Destroy();
        return false;
    }

    width = w;
    height = h;
    return true;
}

void DeferredRenderer::Destroy() {
    toneMap.Destroy();
    bloom.Destroy();
    hdr.Destroy();
    lighting.Destroy();
    shadowMap.Destroy();
    ao.Destroy();
    gbuffer.Destroy();
    shadowRendered = false;
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
    return lightSpace;
}

void DeferredRenderer::EndShadowPass() {
    shadowMap.Unbind();
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
    const ShadowMap* shadow =
        (settings.enableShadows && shadowRendered) ? &shadowMap : nullptr;
    hdr.Bind();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    lighting.Render(gbuffer, p.dirs, p.points, p.view, aoTex, shadow,
                    lightSpace, p.ambient, p.shadowBias);
    hdr.Unbind();

    // Bloom 亮部提取 + blur
    uint32 bloomTex = 0;
    if (settings.enableBloom && bloom.IsValid()) {
        bloom.Extract(hdr.GetColorTexture());
        bloomTex = bloom.GetTexture();
    }

    // 最終 tone map → outputFbo
    glBindFramebuffer(GL_FRAMEBUFFER, outputFbo);
    glViewport(0, 0, width, height);
    toneMap.Apply(hdr.GetColorTexture(), bloomTex, p.exposure,
                  p.bloomStrength);
}

} // namespace Potato
