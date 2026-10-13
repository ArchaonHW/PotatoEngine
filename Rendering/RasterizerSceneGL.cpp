#include "Rendering/RasterizerSceneGL.h"

#include "Rendering/OpenGLRenderer.h" // Mesh::Bind/Draw
#include "Rendering/ShadowMap.h"      // DepthVertexShader
#include <cfloat>

namespace Potato {

bool RasterizerSceneGL::Create(
    int w, int h, const DeferredRendererSettings& s) {
    Destroy();
    settings = s;
    if (!deferred.Create(w, h, settings)) return false;
    depthShader = MakeUnique<Shader>();
    if (!depthShader->LoadFromSource(
            ShadowMap::DepthVertexShader(),
            ShadowMap::DepthFragmentShader())) {
        deferred.Destroy();
        depthShader.reset();
        return false;
    }
    width = w;
    height = h;
    created = true;
    return true;
}

void RasterizerSceneGL::Destroy() {
    deferred.Destroy();
    depthShader.reset();
    width = height = 0;
    created = false;
}

void RasterizerSceneGL::EnsureCreated(int w, int h) {
    if (!created) {
        Create(w, h, settings);
    } else if (w != width || h != height) {
        Create(w, h, settings); // 重建（GBuffer 尺寸綁定）
    }
}

void RasterizerSceneGL::RenderShadowPass(
    const SceneRenderList& list) {
    if (!settings.enableShadows || !depthShader) return;

    // 第一支 castShadow 方向光
    const DeferredDirLight* key = nullptr;
    for (const auto& d : list.dirs) {
        if (d.castShadow) {
            key = &d;
            break;
        }
    }
    if (!key) return;

    // 場景包圍球：所有 mesh 世界 AABB 中心併集
    Vector3 cmin(FLT_MAX, FLT_MAX, FLT_MAX);
    Vector3 cmax(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    bool any = false;
    for (const auto& it : list.items) {
        if (!it.castShadow || !it.mesh ||
            !it.mesh->HasBounds())
            continue;
        const AABB wb =
            it.mesh->GetLocalAABB().Transformed(it.transform);
        cmin.x = wb.min.x < cmin.x ? wb.min.x : cmin.x;
        cmin.y = wb.min.y < cmin.y ? wb.min.y : cmin.y;
        cmin.z = wb.min.z < cmin.z ? wb.min.z : cmin.z;
        cmax.x = wb.max.x > cmax.x ? wb.max.x : cmax.x;
        cmax.y = wb.max.y > cmax.y ? wb.max.y : cmax.y;
        cmax.z = wb.max.z > cmax.z ? wb.max.z : cmax.z;
        any = true;
    }
    if (!any) return;

    const Vector3 center = (cmin + cmax) * 0.5f;
    float radius = 0.0f;
    for (const auto& it : list.items) {
        if (!it.castShadow || !it.mesh) continue;
        const AABB wb =
            it.mesh->GetLocalAABB().Transformed(it.transform);
        for (int i = 0; i < 8; ++i) {
            const Vector3 corner(
                (i & 1) ? wb.max.x : wb.min.x,
                (i & 2) ? wb.max.y : wb.min.y,
                (i & 4) ? wb.max.z : wb.min.z);
            const float d = (corner - center).Length();
            if (d > radius) radius = d;
        }
    }
    if (radius < 1e-3f) radius = 1.0f;

    const Matrix4 lightSpace = deferred.BeginShadowPass(
        key->direction, center, radius);
    depthShader->Bind();
    depthShader->SetUniformMat4("lightSpaceMatrix", lightSpace);
    for (const auto& it : list.items) {
        if (!it.castShadow || !it.mesh) continue;
        depthShader->SetUniformMat4("model", it.transform);
        it.mesh->Bind();
        it.mesh->Draw();
    }
    depthShader->Unbind();
    deferred.EndShadowPass();
}

void RasterizerSceneGL::Render(const SceneRenderList& list,
                               int viewportW, int viewportH,
                               uint32 outputFbo) {
    if (viewportW <= 0 || viewportH <= 0) return;
    EnsureCreated(viewportW, viewportH);
    if (!created) return; // 無 GL context → 略過

    RenderShadowPass(list);

    // 幾何 pass：不透明佇列路徑
    deferred.ClearGeometryQueue();
    for (const auto& it : list.items) {
        if (!it.mesh) continue;
        OpaqueDraw d;
        d.mesh = it.mesh;
        d.model = it.transform;
        d.albedo = Vector3(it.albedo.r, it.albedo.g,
                           it.albedo.b);
        deferred.QueueGeometry(d);
    }
    deferred.BeginGeometry();
    deferred.DrawGeometryQueue(list.view, list.projection);
    deferred.EndGeometry();

    // 光照 → HDR → bloom → tonemap → outputFbo
    DeferredFrameParams params;
    params.view = list.view;
    params.projection = list.projection;
    params.dirs = list.dirs;
    params.points = list.points;
    params.spots = list.spots;
    params.ambient = list.ambient;
    deferred.RenderFrame(params, outputFbo);
}

} // namespace Potato
