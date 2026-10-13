#include "Rendering/RendererSceneCull.h"

#include "Rendering/OpenGLRenderer.h" // Mesh::HasBounds/GetLocalAABB
#include <algorithm>
#include <cmath>

namespace Potato {

// ---- Camera ----

Matrix4 RendererSceneCull::Camera::GetProjection(float aspect) const {
    if (proj == Projection::Perspective) {
        // Matrix4::Perspective 的 fov 參數為弧度
        constexpr float kD2R = 3.14159265358979f / 180.0f;
        return Matrix4::Perspective(fovDeg * kD2R, aspect,
                                    znear, zfar);
    }
    // orthogonal：size 為垂直全高（Godot camera_set_orthogonal
    // 的 size 語義）
    const float halfH = size * 0.5f;
    const float halfW = halfH * aspect;
    return Matrix4::Orthographic(-halfW, halfW, -halfH, halfH,
                                 znear, zfar);
}

// ---- Scenario ----

RID RendererSceneCull::ScenarioAllocate() {
    return scenarioOwner.AllocateRID();
}
void RendererSceneCull::ScenarioInitialize(RID rid) {
    scenarioOwner.InitializeRID(rid, Scenario{});
}
void RendererSceneCull::ScenarioSetBackground(RID rid,
                                              const Color& c) {
    if (Scenario* s = scenarioOwner.GetOrNull(rid))
        s->background = c;
}
void RendererSceneCull::ScenarioSetAmbient(RID rid, float a) {
    if (Scenario* s = scenarioOwner.GetOrNull(rid))
        s->ambient = a;
}
void RendererSceneCull::ScenarioFree(RID rid) {
    if (Scenario* s = scenarioOwner.GetOrNull(rid)) {
        std::vector<RID> insts = s->instances;
        for (RID i : insts) {
            if (Instance* inst = instanceOwner.GetOrNull(i))
                inst->scenario = RID();
        }
        s->instances.clear();
    }
    scenarioOwner.Free(rid);
}

// ---- Instance ----

RID RendererSceneCull::InstanceAllocate() {
    return instanceOwner.AllocateRID();
}
void RendererSceneCull::InstanceInitialize(RID rid) {
    instanceOwner.InitializeRID(rid, Instance{});
}

bool RendererSceneCull::InstanceSetBase(RID inst, RID base) {
    Instance* i = instanceOwner.GetOrNull(inst);
    if (!i) return false;
    if (base.IsNull()) {
        i->baseType = Instance::BaseType::None;
        i->base = RID();
        return true;
    }
    if (lightOwner.Owns(base)) {
        i->baseType = Instance::BaseType::Light;
        i->base = base;
        return true;
    }
    // mesh 有效性由 server 端保證（meshResolver 注入點在
    // CullScenario——這裡只記 base，渲染時解析失敗即跳過）
    i->baseType = Instance::BaseType::Mesh;
    i->base = base;
    return true;
}

bool RendererSceneCull::InstanceSetScenario(RID inst,
                                            RID scenario) {
    Instance* i = instanceOwner.GetOrNull(inst);
    if (!i) return false;
    if (scenario.IsValid() && !scenarioOwner.Owns(scenario))
        return false;

    // 從舊 scenario 脫鉤（線性搜尋自身 RID）
    if (Scenario* old = scenarioOwner.GetOrNull(i->scenario)) {
        old->instances.erase(std::remove(old->instances.begin(),
                                         old->instances.end(),
                                         inst),
                             old->instances.end());
    }
    i->scenario = scenario;
    if (Scenario* s = scenarioOwner.GetOrNull(scenario)) {
        s->instances.push_back(inst);
    }
    return true;
}

void RendererSceneCull::InstanceSetTransform(RID inst,
                                             const Matrix4& t) {
    if (Instance* i = instanceOwner.GetOrNull(inst))
        i->transform = t;
}
void RendererSceneCull::InstanceSetVisible(RID inst, bool v) {
    if (Instance* i = instanceOwner.GetOrNull(inst))
        i->visible = v;
}
void RendererSceneCull::InstanceSetAlbedo(RID inst,
                                          const Color& c) {
    if (Instance* i = instanceOwner.GetOrNull(inst))
        i->albedo = c;
}
void RendererSceneCull::InstanceSetCastShadow(RID inst, bool s) {
    if (Instance* i = instanceOwner.GetOrNull(inst))
        i->castShadow = s;
}
void RendererSceneCull::InstanceFree(RID rid) {
    if (Instance* i = instanceOwner.GetOrNull(rid)) {
        if (Scenario* s = scenarioOwner.GetOrNull(i->scenario)) {
            s->instances.erase(
                std::remove(s->instances.begin(),
                            s->instances.end(), rid),
                s->instances.end());
        }
    }
    instanceOwner.Free(rid);
}

// ---- Camera ----

RID RendererSceneCull::CameraAllocate() {
    return cameraOwner.AllocateRID();
}
void RendererSceneCull::CameraInitialize(RID rid) {
    cameraOwner.InitializeRID(rid, Camera{});
}
void RendererSceneCull::CameraSetTransform(RID rid,
                                           const Matrix4& t) {
    if (Camera* c = cameraOwner.GetOrNull(rid)) c->transform = t;
}
void RendererSceneCull::CameraSetPerspective(RID rid,
                                             float fovDeg,
                                             float znear,
                                             float zfar) {
    if (Camera* c = cameraOwner.GetOrNull(rid)) {
        c->proj = Camera::Projection::Perspective;
        c->fovDeg = fovDeg;
        c->znear = znear;
        c->zfar = zfar;
    }
}
void RendererSceneCull::CameraSetOrthogonal(RID rid, float size,
                                            float znear,
                                            float zfar) {
    if (Camera* c = cameraOwner.GetOrNull(rid)) {
        c->proj = Camera::Projection::Orthogonal;
        c->size = size;
        c->znear = znear;
        c->zfar = zfar;
    }
}

// ---- Light ----

RID RendererSceneCull::LightAllocate() {
    return lightOwner.AllocateRID();
}
void RendererSceneCull::LightInitialize(RID rid, LightType type) {
    Light l;
    l.type = type;
    lightOwner.InitializeRID(rid, l);
}
void RendererSceneCull::LightSetColor(RID rid, const Color& c) {
    if (Light* l = lightOwner.GetOrNull(rid)) l->color = c;
}
void RendererSceneCull::LightSetEnergy(RID rid, float e) {
    if (Light* l = lightOwner.GetOrNull(rid)) l->energy = e;
}
void RendererSceneCull::LightSetRange(RID rid, float r) {
    if (Light* l = lightOwner.GetOrNull(rid)) l->range = r;
}
void RendererSceneCull::LightSetSpotAngles(RID rid, float innerDeg,
                                           float outerDeg) {
    if (Light* l = lightOwner.GetOrNull(rid)) {
        l->innerDeg = innerDeg;
        l->outerDeg = outerDeg;
    }
}
void RendererSceneCull::LightSetShadow(RID rid, bool s) {
    if (Light* l = lightOwner.GetOrNull(rid)) l->castShadow = s;
}
void RendererSceneCull::LightFree(RID rid) {
    lightOwner.Free(rid);
}

// ---- 剔除 ----

SceneRenderList RendererSceneCull::CullScenario(
    RID scenario, RID camera, float aspect,
    const std::function<const Mesh*(RID)>& meshResolver) const {
    SceneRenderList out;

    const Scenario* sc = scenarioOwner.GetOrNull(scenario);
    const Camera* cam = cameraOwner.GetOrNull(camera);
    if (!sc || !cam) return out;

    out.view = cam->GetView();
    out.projection = cam->GetProjection(aspect);
    out.background = sc->background;
    out.ambient = sc->ambient;

    const Matrix4 viewProj = out.projection * out.view;
    const Frustum frustum = Frustum::FromMatrix(viewProj);

    for (RID instRID : sc->instances) {
        const Instance* inst = instanceOwner.GetOrNull(instRID);
        if (!inst || !inst->visible) continue;

        if (inst->baseType == Instance::BaseType::Mesh) {
            const Mesh* mesh =
                meshResolver ? meshResolver(inst->base) : nullptr;
            if (!mesh || !mesh->HasBounds()) continue;

            // 保守世界 AABB（Transformed 8 角）——
            // 與 DeferredRenderer::CullGeometryQueue 同策略
            if (!frustum.ContainsAABB(
                    mesh->GetLocalAABB().Transformed(
                        inst->transform))) {
                continue;
            }
            SceneRenderItem item;
            item.instance = instRID;
            item.mesh = mesh;
            item.transform = inst->transform;
            item.albedo = inst->albedo;
            item.castShadow = inst->castShadow;
            out.items.push_back(item);

        } else if (inst->baseType == Instance::BaseType::Light) {
            const Light* l = lightOwner.GetOrNull(inst->base);
            if (!l) continue;

            // column-major：m[12..14] 平移、-Z 軸為朝向
            const Vector3 origin(inst->transform.m[12],
                                 inst->transform.m[13],
                                 inst->transform.m[14]);
            const Vector3 forward(-inst->transform.m[8],
                                  -inst->transform.m[9],
                                  -inst->transform.m[10]);
            const Vector3 lc(l->color.r, l->color.g, l->color.b);

            switch (l->type) {
            case LightType::Directional: {
                DeferredDirLight d;
                d.direction = forward.Normalized();
                d.color = lc;
                d.intensity = l->energy;
                d.castShadow = l->castShadow;
                out.dirs.push_back(d);
                break;
            }
            case LightType::Omni: {
                DeferredPointLight p;
                p.position = origin;
                p.color = lc;
                p.intensity = l->energy;
                p.radius = l->range;
                p.castShadow = l->castShadow;
                out.points.push_back(p);
                break;
            }
            case LightType::Spot: {
                DeferredSpotLight s;
                s.position = origin;
                s.direction = forward.Normalized();
                s.color = lc;
                s.intensity = l->energy;
                s.radius = l->range;
                // 錐參數存 cos 半角；inner>outer 自動對調
                float in = l->innerDeg, outd = l->outerDeg;
                if (in > outd) std::swap(in, outd);
                constexpr float kDeg2Rad =
                    3.14159265358979f / 180.0f;
                s.innerCos = std::cos(in * kDeg2Rad);
                s.outerCos = std::cos(outd * kDeg2Rad);
                s.castShadow = l->castShadow;
                out.spots.push_back(s);
                break;
            }
            }
        }
    }
    return out;
}

bool RendererSceneCull::FreeAny(RID rid) {
    if (instanceOwner.Owns(rid)) {
        InstanceFree(rid);
        return true;
    }
    if (scenarioOwner.Owns(rid)) {
        ScenarioFree(rid);
        return true;
    }
    if (cameraOwner.Owns(rid)) {
        cameraOwner.Free(rid);
        return true;
    }
    if (lightOwner.Owns(rid)) {
        lightOwner.Free(rid);
        return true;
    }
    return false;
}

} // namespace Potato
