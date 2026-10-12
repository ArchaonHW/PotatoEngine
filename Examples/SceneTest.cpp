// SceneTest - 場景圖 + 視錐剔除測試

#include "Scene/SceneNode.h"
#include "MathUtils/Frustum.h"
#include "Rendering/Camera.h"
#include "Rendering/RenderableComponent.h"
#include "Rendering/OpenGLRenderer.h" // Mesh/Vertex（headless 可建構）
#include <cstdio>
#include <cmath>
#include <vector>

using namespace Potato;

static int g_pass = 0;
static int g_fail = 0;

static void Check(bool ok, const char* name) {
    if (ok) { g_pass++; printf("  [PASS] %s\n", name); }
    else    { g_fail++; printf("  [FAIL] %s\n", name); }
}

int main() {
    printf("=== Scene Graph / Frustum Culling Tests ===\n\n");

    SceneGraph scene;
    auto root = scene.GetRootNode(); // "Root",boundingRadius = -1(組織節點)

    // 建三個可渲染節點:前方(可見)、後方(剔除)、側面(剔除)
    auto visible = MakeShared<SceneNode>("visible");
    visible->SetBoundingRadius(1.0f);
    visible->SetLocalPosition(Vector3(0, 0, -10));
    root->AddChild(visible);

    auto behind = MakeShared<SceneNode>("behind");
    behind->SetBoundingRadius(1.0f);
    behind->SetLocalPosition(Vector3(0, 0, 10)); // 相機後方
    root->AddChild(behind);

    auto side = MakeShared<SceneNode>("side");
    side->SetBoundingRadius(1.0f);
    side->SetLocalPosition(Vector3(60, 0, -10)); // fov 外
    root->AddChild(side);

    // 階層剔除:behind 下掛一個「單獨判定可見」的子節點
    // (world z = -10 在視錐內),但父節點被剔除時子樹不會被評估
    auto hiddenChild = MakeShared<SceneNode>("hiddenChild");
    hiddenChild->SetBoundingRadius(1.0f);
    hiddenChild->SetLocalPosition(Vector3(0, 0, -20)); // world z = -10
    behind->AddChild(hiddenChild);

    Matrix4 proj = Matrix4::Perspective(60.0f * 3.14159265f / 180.0f, 16.0f / 9.0f, 0.1f, 100.0f);
    Matrix4 view = Matrix4::LookAt(Vector3(0, 0, 0), Vector3(0, 0, -1), Vector3(0, 1, 0));
    Frustum frustum = Frustum::FromMatrix(proj * view);

    auto vis = scene.CollectVisibleNodes(frustum);
    auto has = [&](const char* n) {
        for (auto* node : vis) if (node->GetName() == n) return true;
        return false;
    };

    Check(vis.size() == 1, "只有 1 個節點可見");
    Check(has("visible"), "前方節點可見");
    Check(!has("behind"), "後方節點被剔除");
    Check(!has("side"), "側面節點被剔除");
    Check(!has("hiddenChild"), "父被剔除時子樹不評估(階層剔除)");

    // 世界縮放影響包圍球:縮放 100 倍的小物件在遠處仍應可見
    auto scaled = MakeShared<SceneNode>("scaled");
    scaled->SetBoundingRadius(1.0f);
    scaled->SetLocalPosition(Vector3(5, 0, -50));
    scaled->SetLocalScale(Vector3(50, 50, 50)); // 世界半徑 50,覆蓋視錐
    root->AddChild(scaled);
    vis = scene.CollectVisibleNodes(frustum);
    Check(has("scaled"), "世界縮放正確放大包圍球");

    // active=false 的節點不參與
    side->SetActive(true);
    visible->SetActive(false);
    vis = scene.CollectVisibleNodes(frustum);
    Check(!has("visible"), "inactive 節點不收集");

    // 旋轉 parent：子節點世界位置須經父矩陣完整變換
    // (舊 bug：worldPosition = parentPos + localPos 直接相加,忽略旋轉)
    {
        auto parent = MakeShared<SceneNode>("rotParent");
        parent->SetLocalPosition(Vector3(10, 0, 0));
        parent->SetLocalRotation(
            Quaternion::FromAxisAngle(Vector3(0, 1, 0), 3.14159265f / 2.0f)); // 繞 Y 轉 90°

        auto child = MakeShared<SceneNode>("rotChild");
        child->SetLocalPosition(Vector3(1, 0, 0)); // 父節點局部 +X
        parent->AddChild(child);

        // R_y(90°)·(1,0,0) = (0,0,-1);加父平移 (10,0,0) → 世界 (10,0,-1)
        Vector3 wp = child->GetWorldPosition();
        Check(std::fabs(wp.x - 10.0f) < 1e-4f &&
              std::fabs(wp.z + 1.0f) < 1e-4f,
              "旋轉父節點下子節點世界位置正確(10,0,-1)");
    }

    // SetRenderable 自動推導：組件未指定半徑時從 mesh 頂點算
    // （|localCenter|+radius 保守包住偏離節點原點的 mesh）
    {
        auto mesh = MakeShared<Mesh>();
        std::vector<Vertex> v(8); // 盒心 (2,0,0)、半邊 1
        for (int i = 0; i < 8; ++i) {
            v[i].position = Vector3(2.0f + ((i & 1) ? 1.0f : -1.0f),
                                    (i & 2) ? 1.0f : -1.0f,
                                    (i & 4) ? 1.0f : -1.0f);
        }
        mesh->SetVertices(v);

        auto rc = MakeShared<RenderableComponent>();
        rc->mesh = mesh; // boundingRadius 預設 -1 → 走自動推導
        auto node = MakeShared<SceneNode>("autoBound");
        node->SetRenderable(rc);
        // 推導值 = |(2,0,0)| + √3 ≈ 3.732
        Check(std::fabs(node->GetBoundingRadius() -
                        (2.0f + std::sqrt(3.0f))) < 1e-4f,
              "SetRenderable 由 mesh 自動推導 boundingRadius");

        // 組件明確給半徑時仍以手動值優先
        auto rc2 = MakeShared<RenderableComponent>();
        rc2->mesh = mesh;
        rc2->boundingRadius = 7.5f;
        auto node2 = MakeShared<SceneNode>("manualBound");
        node2->SetRenderable(rc2);
        Check(std::fabs(node2->GetBoundingRadius() - 7.5f) < 1e-6f,
              "手動 boundingRadius 優先於自動推導");
    }

    // Camera::SetViewport 拒絕非法尺寸（width<=0 不得污染 aspect）
    {
        Camera cam;
        cam.SetPerspective(60.0f, 16.0f / 9.0f, 0.1f, 100.0f);
        float goodAspect = cam.GetAspectRatio();

        cam.SetViewport(0, 0, 0, 720);  // width=0
        Check(std::fabs(cam.GetAspectRatio() - goodAspect) < 1e-6f,
              "SetViewport width=0 不改變 aspect");

        cam.SetViewport(0, 0, 800, 600);
        Check(std::fabs(cam.GetAspectRatio() - 800.0f / 600.0f) < 1e-4f,
              "SetViewport 800x600 更新 aspect");
    }

    printf("\n=== 結果: %d PASS, %d FAIL ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
