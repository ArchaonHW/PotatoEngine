#pragma once

/**
 * RendererSceneCull — 3D 場景的資源持有與剔除層。
 *
 * 對應 Godot servers/rendering/renderer_scene_cull.*：
 * 持有 Scenario / Instance / Camera / Light 的 RID_Alloc 池。
 * Instance 以 base RID 指涉 mesh 或 light（Godot 的
 * instance_set_base 語義）；Scenario 為 instance 的扁平集合
 * （Godot 用 octree 動態分區——此處簡化為線性掃描+
 * 視錐剔除，場景規模達瓶頸再換加速結構）。
 *
 * 光源姿態遵循 Godot：position/direction 由 instance
 * transform 供給（-Z 為照射方向），Light 本體只存
 * color/energy/range/錐角/陰影旗標。
 *
 * CullScenario 為純 CPU（Frustum::ContainsAABB），
 * headless 可測。
 */

#include "Core/CoreTypes.h"
#include "Core/RID.h"
#include "MathUtils/Frustum.h"
#include "MathUtils/Matrix4.h"
#include "MathUtils/Vector3.h"
#include "Rendering/RasterizerScene.h"
#include "Rendering/RenderTypes.h"
#include <functional>
#include <vector>

namespace Potato {

class Mesh;

// Godot RS::LightType 對應
enum class LightType {
    Directional, // LIGHT_DIRECTIONAL
    Omni,        // LIGHT_OMNI（點光）
    Spot         // LIGHT_SPOT
};

class RendererSceneCull {
public:
    struct Scenario {
        std::vector<RID> instances;
        Color background{0.0f, 0.0f, 0.0f, 1.0f};
        float ambient = 0.05f;
    };

    struct Camera {
        Matrix4 transform = Matrix4::Identity();
        enum class Projection { Perspective, Orthogonal };
        Projection proj = Projection::Perspective;
        float fovDeg = 60.0f;   // perspective
        float size = 1.0f;      // orthogonal：垂直半幅×2
        float znear = 0.1f;
        float zfar = 1000.0f;

        Matrix4 GetView() const { return transform.Inverse(); }
        Matrix4 GetProjection(float aspect) const;
    };

    struct Light {
        LightType type = LightType::Omni;
        Color color{1.0f, 1.0f, 1.0f, 1.0f};
        float energy = 1.0f;
        float range = 10.0f;                    // omni/spot
        float innerDeg = 12.5f, outerDeg = 17.5f; // spot
        bool castShadow = false;
    };

    struct Instance {
        enum class BaseType { None, Mesh, Light };
        BaseType baseType = BaseType::None;
        RID base;                            // mesh 或 light RID
        RID scenario;
        Matrix4 transform = Matrix4::Identity();
        bool visible = true;
        bool castShadow = true;
        Color albedo{0.9f, 0.9f, 0.9f, 1.0f};
    };

    // ---- Scenario ----
    RID ScenarioAllocate();
    void ScenarioInitialize(RID rid);
    void ScenarioSetBackground(RID rid, const Color& c);
    void ScenarioSetAmbient(RID rid, float ambient);
    Scenario* GetScenario(RID rid) {
        return scenarioOwner.GetOrNull(rid);
    }
    bool ScenarioOwns(RID rid) const {
        return scenarioOwner.Owns(rid);
    }
    void ScenarioFree(RID rid);

    // ---- Instance ----
    RID InstanceAllocate();
    void InstanceInitialize(RID rid);
    // base 指涉哪種資源由 meshResolver/lightOwner 判定
    bool InstanceSetBase(RID inst, RID base);
    bool InstanceSetScenario(RID inst, RID scenario);
    void InstanceSetTransform(RID inst, const Matrix4& t);
    void InstanceSetVisible(RID inst, bool v);
    void InstanceSetAlbedo(RID inst, const Color& c);
    void InstanceSetCastShadow(RID inst, bool s);
    Instance* GetInstance(RID rid) {
        return instanceOwner.GetOrNull(rid);
    }
    bool InstanceOwns(RID rid) const {
        return instanceOwner.Owns(rid);
    }
    void InstanceFree(RID rid);

    // ---- Camera ----
    RID CameraAllocate();
    void CameraInitialize(RID rid);
    void CameraSetTransform(RID rid, const Matrix4& t);
    void CameraSetPerspective(RID rid, float fovDeg,
                              float znear, float zfar);
    void CameraSetOrthogonal(RID rid, float size,
                             float znear, float zfar);
    Camera* GetCamera(RID rid) { return cameraOwner.GetOrNull(rid); }
    bool CameraOwns(RID rid) const { return cameraOwner.Owns(rid); }
    void CameraFree(RID rid) { cameraOwner.Free(rid); }

    // ---- Light ----
    RID LightAllocate();
    void LightInitialize(RID rid, LightType type);
    void LightSetColor(RID rid, const Color& c);
    void LightSetEnergy(RID rid, float e);
    void LightSetRange(RID rid, float r);
    void LightSetSpotAngles(RID rid, float innerDeg,
                            float outerDeg);
    void LightSetShadow(RID rid, bool s);
    Light* GetLight(RID rid) { return lightOwner.GetOrNull(rid); }
    bool LightOwns(RID rid) const { return lightOwner.Owns(rid); }
    void LightFree(RID rid);

    // ---- 剔除 ----
    // meshResolver：RID → const Mesh*（server 端注入；
    // nullptr base/無 bounds 的 instance 跳過不畫）。
    // 回傳剔除後的 SceneRenderList——3D 光源不受視錐剔除
    // （Godot 有 light culler；此處數量上限本就由 deferred
    // 管線管）。
    SceneRenderList CullScenario(
        RID scenario, RID camera, float aspect,
        const std::function<const Mesh*(RID)>& meshResolver)
        const;

    bool OwnsAny(RID rid) const {
        return scenarioOwner.Owns(rid) || instanceOwner.Owns(rid) ||
               cameraOwner.Owns(rid) || lightOwner.Owns(rid);
    }
    bool FreeAny(RID rid);

private:
    RID_Alloc<Scenario> scenarioOwner{16, "scenario"};
    RID_Alloc<Instance> instanceOwner{1024, "instance"};
    RID_Alloc<Camera> cameraOwner{16, "camera"};
    RID_Alloc<Light> lightOwner{64, "light"};
};

} // namespace Potato
