#pragma once

#include "Core/CoreTypes.h"
#include "MathUtils/Matrix4.h"
#include "MathUtils/Vector3.h"
#include <vector>

namespace Potato {

class SceneGraph;
class SceneNode;
class Camera;
class Mesh;
class Shader;
class Texture;

/**
 * 一筆渲染提交：場景遍歷+剔除後產生的最小繪製單元
 */
struct RenderItem {
    SceneNode* node = nullptr;
    SharedPtr<Mesh> mesh;
    SharedPtr<Shader> shader;
    SharedPtr<Texture> texture;
    Vector3 color = Vector3(1.0f, 1.0f, 1.0f);
    Matrix4 worldMatrix;
};

/**
 * 渲染統計
 */
struct RenderStats {
    int visibleNodes = 0;   // 視錐內且 active 的節點數
    int renderableNodes = 0; // 其中帶 RenderableComponent 的節點數
    int drawCalls = 0;       // 實際送出的 draw call 數
};

/**
 * 場景渲染器
 * 串接:SceneGraph 遍歷 → Frustum Culling → RenderableComponent → GL draw
 * CollectRenderList 階段不需 GL context,可無頭測試
 */
class SceneRenderer {
public:
    SceneRenderer() = default;
    ~SceneRenderer() = default;

    // 階段 1(不需 GL context):遍歷場景、視錐剔除、收集可渲染項目
    std::vector<RenderItem> CollectRenderList(SceneGraph& scene, const Camera& camera);

    // 階段 2(需 GL context):提交渲染清單到 GPU
    RenderStats SubmitRenderList(const std::vector<RenderItem>& items, const Camera& camera);

    // 一步到位：收集 + 提交
    RenderStats Render(SceneGraph& scene, const Camera& camera);
};

} // namespace Potato
