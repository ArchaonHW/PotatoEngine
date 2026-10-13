#pragma once

/**
 * RasterizerScene — 3D 場景光柵化介面 + Dummy 後端。
 *
 * 對應 Godot renderer_scene_render / dummy/rasterizer_scene_dummy：
 * RendererSceneCull 產出剔除後的 SceneRenderList（instance 指標
 * 陣列 + 打包好的 deferred 光源 + 相機矩陣），rasterizer 只管畫。
 *
 * GL 後端 RasterizerSceneGL 橋接既有 DeferredRenderer 佇列——
 * RenderingServer 層不改動既有管線，只做資源化與剔除前置。
 */

#include "Core/CoreTypes.h"
#include "Core/RID.h"
#include "MathUtils/Matrix4.h"
#include "MathUtils/Vector3.h"
#include "Rendering/DeferredShading.h" // DeferredDirLight/Point/Spot
#include "Rendering/RenderTypes.h"
#include <vector>

namespace Potato {

class Mesh;

// 剔除後的 3D 繪製單元（mesh 指標渲染期間須存活）
struct SceneRenderItem {
    RID instance;
    const Mesh* mesh = nullptr;
    Matrix4 transform;
    Color albedo{0.9f, 0.9f, 0.9f, 1.0f};
    bool castShadow = true;
};

// 一個 scenario × camera 的幀輸入
struct SceneRenderList {
    std::vector<SceneRenderItem> items;
    std::vector<DeferredDirLight> dirs;
    std::vector<DeferredPointLight> points;
    std::vector<DeferredSpotLight> spots;
    Matrix4 view;
    Matrix4 projection;
    Color background{0.0f, 0.0f, 0.0f, 1.0f};
    float ambient = 0.05f;
};

class RasterizerScene {
public:
    virtual ~RasterizerScene() = default;
    virtual void Render(const SceneRenderList& list,
                        int viewportW, int viewportH,
                        uint32 outputFbo = 0) = 0;
};

// headless 後端：只記錄（測試/agent 模擬執行用）
class RasterizerSceneDummy : public RasterizerScene {
public:
    void Render(const SceneRenderList& list, int w, int h,
                uint32 /*outputFbo*/) override {
        lastList = list;
        lastW = w;
        lastH = h;
        frameCount++;
    }

    const SceneRenderList& GetLastFrame() const { return lastList; }
    uint64 GetFrameCount() const { return frameCount; }

private:
    SceneRenderList lastList;
    int lastW = 0, lastH = 0;
    uint64 frameCount = 0;
};

} // namespace Potato
