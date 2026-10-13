#pragma once

/**
 * RasterizerSceneGL — 3D 場景光柵化的 OpenGL 後端。
 *
 * 把 SceneRenderList 橋接進既有 DeferredRenderer 管線：
 *   items  → QueueGeometry（mesh/transform/albedo）+
 *            視錐外已被 culler 剔除（雙重剔除無害）
 *   lights → DeferredFrameParams 的 dirs/points/spots
 *   陰影   → 第一支 castShadow 方向光走 BeginShadowPass
 *            （內建深度 shader 逐 mesh 繪製；CSM/spot/point
 *            陰影屬 DeferredRenderer 進階路徑，此處 v1 走
 *            單張方向光——其餘旗標保留待擴）
 *
 * Create 需 GL context；Render 內 lazy create + resize 重建。
 */

#include "Rendering/DeferredRenderer.h"
#include "Rendering/RasterizerScene.h"

namespace Potato {

class RasterizerSceneGL : public RasterizerScene {
public:
    bool Create(int w, int h,
                const DeferredRendererSettings& settings =
                    DeferredRendererSettings());
    void Destroy();
    bool IsValid() const { return created; }

    void Render(const SceneRenderList& list, int viewportW,
                int viewportH, uint32 outputFbo) override;

private:
    void EnsureCreated(int w, int h);
    void RenderShadowPass(const SceneRenderList& list);

    DeferredRenderer deferred;
    DeferredRendererSettings settings;
    UniquePtr<Shader> depthShader;
    int width = 0, height = 0;
    bool created = false;
};

} // namespace Potato
