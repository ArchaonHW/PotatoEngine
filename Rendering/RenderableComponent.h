#pragma once

#include "Core/CoreTypes.h"
#include "MathUtils/Vector3.h"

namespace Potato {

// 前向宣告：避免 RenderableComponent 拖入 GL 相依頭文件
class Mesh;
class Shader;
class Texture;

/**
 * 可渲染組件
 * 掛在 SceneNode 上，由 SceneRenderer 在視錐剔除後提交繪製
 * 所有資源為 SharedPtr；GL 物件由 Mesh/Texture 內部惰性建立
 */
class RenderableComponent {
public:
    RenderableComponent() = default;
    explicit RenderableComponent(SharedPtr<Mesh> mesh_) : mesh(std::move(mesh_)) {}

    // 渲染資源
    SharedPtr<Mesh> mesh;
    SharedPtr<Shader> shader;      // 可為空：SceneRenderer 用預設 shader
    SharedPtr<Texture> texture;    // 可為空

    // 材質參數
    Vector3 color = Vector3(1.0f, 1.0f, 1.0f);
    bool visible = true;

    // 本地空間包圍球半徑；SetRenderable 時寫回節點供 Frustum Culling 使用
    // < 0 表示不覆寫節點既有半徑
    float boundingRadius = -1.0f;
};

} // namespace Potato
