// RenderPipelineTest - E1-1 渲染管線串接驗證(無頭,不需 GL context)
// 覆蓋:SceneGraph 遍歷 → Frustum Culling → RenderableComponent → RenderItem 收集
// 驗證內容:可見節點入列、剔除生效、visible=false 略過、boundingRadius 由組件同步、
//           worldMatrix 正確、無 renderable 節點不產生 draw item

#include "Scene/SceneNode.h"
#include "Rendering/SceneRenderer.h"
#include "Rendering/RenderableComponent.h"
#include "Rendering/OpenGLRenderer.h" // Mesh(GL 惰性建立,可安全建構)
#include "Rendering/Camera.h"
#include "MathUtils/Vector3.h"

#include <cstdio>
#include <cmath>

using namespace Potato;

static int g_pass = 0;
static int g_fail = 0;

static void Check(bool ok, const char* name, float value = 0.0f, float expected = 0.0f) {
    if (ok) {
        g_pass++;
        printf("  [PASS] %s\n", name);
    } else {
        g_fail++;
        printf("  [FAIL] %s  (got %.4f, expected %.4f)\n", name, value, expected);
    }
}

int main() {
    printf("=== Render Pipeline Tests ===\n\n");

    SceneGraph scene;
    auto root = MakeShared<SceneNode>("root");
    scene.SetRootNode(root);

    // 前方可見節點(帶 mesh + 顏色)
    auto front = MakeShared<SceneNode>("front");
    {
        auto rc = MakeShared<RenderableComponent>();
        rc->mesh = MakeShared<Mesh>(); // GL 物件惰性建立,無 context 也可建構
        rc->color = Vector3(1.0f, 0.0f, 0.0f);
        rc->boundingRadius = 1.0f;
        front->SetRenderable(rc);
    }
    front->SetLocalPosition(Vector3(0, 0, -10));
    root->AddChild(front);

    // 後方節點(應被視錐剔除)
    auto behind = MakeShared<SceneNode>("behind");
    {
        auto rc = MakeShared<RenderableComponent>();
        rc->mesh = MakeShared<Mesh>();
        rc->boundingRadius = 1.0f;
        behind->SetRenderable(rc);
    }
    behind->SetLocalPosition(Vector3(0, 0, 10));
    root->AddChild(behind);

    // 可見但 visible=false(應略過)
    auto hidden = MakeShared<SceneNode>("hidden");
    {
        auto rc = MakeShared<RenderableComponent>();
        rc->mesh = MakeShared<Mesh>();
        rc->visible = false;
        rc->boundingRadius = 1.0f;
        hidden->SetRenderable(rc);
    }
    hidden->SetLocalPosition(Vector3(2, 0, -10));
    root->AddChild(hidden);

    // 可見但無 mesh(應略過,不產生 draw item)
    auto noMesh = MakeShared<SceneNode>("noMesh");
    {
        auto rc = MakeShared<RenderableComponent>();
        rc->boundingRadius = 1.0f;
        noMesh->SetRenderable(rc);
    }
    noMesh->SetLocalPosition(Vector3(-2, 0, -10));
    root->AddChild(noMesh);

    // 純組織節點(無 renderable,不設半徑):不參與剔除也不入列
    auto group = MakeShared<SceneNode>("group");
    group->SetLocalPosition(Vector3(0, 5, -10));
    root->AddChild(group);

    Camera cam;
    cam.SetPosition(Vector3(0, 0, 0));
    cam.SetTarget(Vector3(0, 0, -1));
    cam.SetPerspective(60.0f * 3.14159265f / 180.0f, 16.0f / 9.0f, 0.1f, 100.0f);

    SceneRenderer renderer;
    auto items = renderer.CollectRenderList(scene, cam);

    // 只有 front 進入 render list
    Check(items.size() == 1, "render list 只有 1 項", static_cast<float>(items.size()), 1.0f);
    if (!items.empty()) {
        Check(items[0].node == front.get(), "入列的是前方節點");
        Check(items[0].mesh != nullptr, "RenderItem 帶 mesh");
        Check(items[0].color.x == 1.0f && items[0].color.y == 0.0f,
              "RenderItem 顏色正確");
        // worldMatrix 平移分量應為 (0,0,-10)
        Vector3 wp = front->GetWorldPosition();
        Check(std::fabs(wp.z + 10.0f) < 1e-4f, "節點世界位置正確", wp.z, -10.0f);
    }

    // SetRenderable 同步 boundingRadius:behind 節點半徑應已被設為 1.0
    Check(behind->GetBoundingRadius() == 1.0f, "SetRenderable 同步包圍半徑");
    // 無 renderable 的 group 節點不設半徑(不參與剔除)
    Check(group->GetBoundingRadius() < 0.0f, "組織節點無包圍半徑");

    // 階層剔除:在 behind 下掛一個理論上可見的子節點,父被剔除時不進列
    auto behindChild = MakeShared<SceneNode>("behindChild");
    {
        auto rc = MakeShared<RenderableComponent>();
        rc->mesh = MakeShared<Mesh>();
        rc->boundingRadius = 1.0f;
        behindChild->SetRenderable(rc);
    }
    behindChild->SetLocalPosition(Vector3(0, 0, -20)); // world z = -10,理論可見
    behind->AddChild(behindChild);

    items = renderer.CollectRenderList(scene, cam);
    Check(items.size() == 1, "父被剔除時子節點不入列", static_cast<float>(items.size()), 1.0f);

    printf("\n=== 結果: %d PASS, %d FAIL ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
