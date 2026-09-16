#include "Rendering/SceneRenderer.h"
#include "Rendering/RenderableComponent.h"
#include "Rendering/OpenGLRenderer.h" // Mesh / Shader / Texture
#include "Rendering/Camera.h"
#include "Scene/SceneNode.h"

#include <algorithm>

namespace Potato {

std::vector<RenderItem> SceneRenderer::CollectRenderList(SceneGraph& scene, const Camera& camera) {
    std::vector<RenderItem> items;

    // 階層式視錐剔除:回傳 boundingRadius >= 0 且 active 的節點
    std::vector<SceneNode*> visible = scene.CollectVisibleNodes(camera.GetFrustum());
    items.reserve(visible.size());

    for (SceneNode* node : visible) {
        RenderableComponent* rc = node->GetRenderable();
        if (!rc || !rc->visible || !rc->mesh) {
            continue;
        }

        RenderItem item;
        item.node = node;
        item.mesh = rc->mesh;
        item.shader = rc->shader;
        item.texture = rc->texture;
        item.color = rc->color;
        item.worldMatrix = node->GetWorldMatrix();
        items.push_back(item);
    }

    // 依 shader 分組減少切換(無 shader 的排最後,由 Submit 用預設 shader)
    std::stable_sort(items.begin(), items.end(),
        [](const RenderItem& a, const RenderItem& b) {
            return a.shader.get() < b.shader.get();
        });

    return items;
}

RenderStats SceneRenderer::SubmitRenderList(const std::vector<RenderItem>& items, const Camera& camera) {
    RenderStats stats;
    stats.renderableNodes = static_cast<int>(items.size());

    const Matrix4 view = camera.GetViewMatrix();
    const Matrix4 proj = camera.GetProjectionMatrix();

    Shader* boundShader = nullptr;
    for (const RenderItem& item : items) {
        Shader* shader = item.shader.get();
        if (!shader) {
            continue; // 無 shader 且無預設管線:略過(預設 shader 由呼叫方掛到 renderable)
        }
        if (shader != boundShader) {
            shader->Bind();
            shader->SetUniformMat4("view", view);
            shader->SetUniformMat4("projection", proj);
            boundShader = shader;
        }
        shader->SetUniformMat4("model", item.worldMatrix);
        shader->SetUniformVec3("uColor", item.color);
        if (item.texture) {
            item.texture->Bind(0);
            shader->SetUniformInt("texture1", 0);
        }
        item.mesh->Draw();
        stats.drawCalls++;
    }
    if (boundShader) {
        boundShader->Unbind();
    }

    return stats;
}

RenderStats SceneRenderer::Render(SceneGraph& scene, const Camera& camera) {
    // 先算可見節點數(含無 renderable 的)
    std::vector<SceneNode*> visible = scene.CollectVisibleNodes(camera.GetFrustum());
    std::vector<RenderItem> items = CollectRenderList(scene, camera);

    RenderStats stats = SubmitRenderList(items, camera);
    stats.visibleNodes = static_cast<int>(visible.size());
    return stats;
}

} // namespace Potato
