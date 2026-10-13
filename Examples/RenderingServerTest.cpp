// RenderingServerTest — Godot 式 RenderingServer 架構測試（全 headless）
// 覆蓋：RID_Alloc 代際驗證、Transform2D 代數、canvas item
//   樹/z-order/剔除、scene culler 視錐剔除+光源打包、
//   Dummy rasterizer 指令流、Agent 工具端到端

#include "Core/RID.h"
#include "Rendering/RenderTypes.h"
#include "Rendering/RasterizerCanvas.h"
#include "Rendering/RasterizerScene.h"
#include "Rendering/RenderingServerDefault.h"
#include "AI/RenderAgentTools.h"
#include "AI/ToolFramework.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace Potato;

static int g_pass = 0;
static int g_fail = 0;

static void Check(bool ok, const char* name) {
    if (ok) { g_pass++; printf("  [PASS] %s\n", name); }
    else    { g_fail++; printf("  [FAIL] %s\n", name); }
}

int main() {
    printf("=== Rendering Server Tests ===\n\n");

    // [1] RID / RID_Alloc 代際語義（Godot rid_owner 同規格）
    {
        RID_Alloc<int> alloc(4, "test");
        RID a = alloc.MakeRID(42);
        Check(a.IsValid(), "RID alloc → valid");
        Check(alloc.GetOrNull(a) && *alloc.GetOrNull(a) == 42,
              "GetOrNull 取回資料");

        alloc.Free(a);
        Check(alloc.GetOrNull(a) == nullptr,
              "free 後 GetOrNull → null");

        RID b = alloc.MakeRID(7);
        Check(b.IsValid() && b != a,
              "slot 重用但 validator 不同");
        Check(*alloc.GetOrNull(b) == 7, "新 RID 資料正確");
        Check(alloc.GetAllocCount() == 1, "alloc 計數正確");

        RID stale = a;
        Check(alloc.GetOrNull(stale) == nullptr,
              "舊 RID 被代際檔下（無懸垂）");
    }

    // [2] Transform2D 代數
    {
        const Transform2D t = Transform2D::TRS(10, 20, 0.0f,
                                             2.0f, 2.0f);
        const Vector2 p = t.Xform(Vector2(1, 1));
        Check(std::fabs(p.x - 12.0f) < 1e-4f &&
                  std::fabs(p.y - 22.0f) < 1e-4f,
              "TRS 平移+縮放");

        const Transform2D rot = Transform2D::Rotation(
            3.14159265f / 2.0f); // 90°
        const Vector2 r = rot.Xform(Vector2(1, 0));
        Check(std::fabs(r.x) < 1e-4f &&
                  std::fabs(r.y - 1.0f) < 1e-4f,
              "Rotation(90°) x→y");

        const Transform2D inv = t.AffineInverse();
        const Vector2 back = inv.Xform(p);
        Check(std::fabs(back.x - 1.0f) < 1e-4f &&
                  std::fabs(back.y - 1.0f) < 1e-4f,
              "AffineInverse 往返");
    }

    // [3] RenderingServer headless 建構 + singleton
    auto rs = RenderingServerDefault::CreateHeadless();
    Check(RenderingServer::GetSingleton() == rs.get(),
          "singleton 已註冊");
    auto* canvasRast = static_cast<RasterizerCanvasDummy*>(
        rs->GetRasterizerCanvas());
    auto* sceneRast = static_cast<RasterizerSceneDummy*>(
        rs->GetRasterizerScene());
    Check(canvasRast && sceneRast, "Dummy 後端已接上");

    // [4] Canvas item 樹：parent/z-order/剔除
    {
        RID vp = rs->viewport_create();
        rs->viewport_set_size(vp, 800, 600);
        RID canvas = rs->canvas_create();
        rs->viewport_attach_canvas(vp, canvas);

        RID itemA = rs->canvas_item_create();
        rs->canvas_item_set_parent(itemA, canvas);
        rs->canvas_item_add_rect(itemA, Rect2(10, 10, 50, 50),
                                 Color(1, 0, 0, 1));

        RID itemB = rs->canvas_item_create();
        rs->canvas_item_set_parent(itemB, canvas);
        rs->canvas_item_add_rect(itemB, Rect2(20, 20, 50, 50),
                                 Color(0, 1, 0, 1));
        rs->canvas_item_set_z_index(itemB, -1); // B 在 A 下層

        // 螢幕外 item 應被剔除
        RID itemFar = rs->canvas_item_create();
        rs->canvas_item_set_parent(itemFar, canvas);
        rs->canvas_item_add_rect(
            itemFar, Rect2(10000, 10000, 10, 10),
            Color(1, 1, 1, 1));

        // 不可見 item 剔除
        RID itemHidden = rs->canvas_item_create();
        rs->canvas_item_set_parent(itemHidden, canvas);
        rs->canvas_item_add_rect(
            itemHidden, Rect2(0, 0, 10, 10), Color(1, 1, 1, 1));
        rs->canvas_item_set_visible(itemHidden, false);

        rs->draw_frame();
        Check(canvasRast->GetLastItemCount() == 2,
              "可見 2 指令（螢幕外+隱藏剔除）");
        Check(rs->get_stats().canvasItems == 2,
              "stats.canvasItems = 2");

        // z-order：B(z=-1) 先畫（在底層）
        const auto& lists = canvasRast->GetLastFrame();
        if (lists.size() == 1 && lists[0].items.size() == 2) {
            Check(std::get_if<CanvasCmd::Rect>(
                      lists[0].items[0].cmd) &&
                      std::get_if<CanvasCmd::Rect>(
                          lists[0].items[0].cmd)->color.g == 1.0f,
                  "z=-1 item 先畫（底層）");
        } else {
            Check(false, "canvas list 結構異常");
        }

        // 子 item transform 疊加
        RID child = rs->canvas_item_create();
        rs->canvas_item_set_parent(child, itemA);
        rs->canvas_item_set_transform(
            itemA, Transform2D::Translation(100, 0));
        rs->canvas_item_add_rect(child, Rect2(0, 0, 10, 10),
                                 Color(0, 0, 1, 1));
        rs->draw_frame();
        const auto& l2 = canvasRast->GetLastFrame();
        bool childShifted = false;
        for (const auto& li : l2[0].items) {
            if (const auto* rc =
                    std::get_if<CanvasCmd::Rect>(li.cmd)) {
                if (rc->color.b == 1.0f) {
                    const Vector2 tp =
                        li.xform.Xform(rc->rect.pos);
                    childShifted = std::fabs(tp.x - 100.0f) < 0.5f;
                }
            }
        }
        Check(childShifted, "子 item 繼承父 transform");

        // clear 後無指令
        rs->canvas_item_clear(itemA);
        rs->canvas_item_clear(itemB);
        rs->canvas_item_clear(child);
        rs->draw_frame();
        Check(canvasRast->GetLastItemCount() == 0,
              "clear 後 0 指令");
    }

    // [5] Scenario/camera/instance/light 3D 路徑
    {
        RID vp = rs->viewport_create();
        rs->viewport_set_size(vp, 640, 480);

        RID scenario = rs->scenario_create();
        rs->viewport_attach_scenario(vp, scenario);

        RID cam = rs->camera_create();
        rs->camera_set_perspective(cam, 60.0f, 0.1f, 100.0f);
        // 原點向 -Z 看（LookAt view→inverse = cam transform）
        rs->camera_set_transform(
            cam, Matrix4::LookAt(Vector3(0, 0, 5),
                                 Vector3(0, 0, 0),
                                 Vector3(0, 1, 0))
                     .Inverse());
        rs->viewport_set_camera(vp, cam);

        RID mesh = rs->mesh_create_primitive("cube", 1.0f);
        Check(mesh.IsValid(), "mesh_create_primitive cube");

        // 相機正前方（可見）
        RID inst1 = rs->instance_create();
        rs->instance_set_base(inst1, mesh);
        rs->instance_set_transform(
            inst1, Matrix4::Translation(0, 0, 0));
        rs->instance_set_scenario(inst1, scenario);

        // 相機後方（剔除）
        RID inst2 = rs->instance_create();
        rs->instance_set_base(inst2, mesh);
        rs->instance_set_transform(
            inst2, Matrix4::Translation(0, 0, 50));
        rs->instance_set_scenario(inst2, scenario);

        // 方向光（-Z 照射）+ 點光
        RID dl = rs->light_create(LightType::Directional);
        rs->light_set_energy(dl, 2.0f);
        rs->light_set_shadow(dl, true);
        RID dli = rs->instance_create();
        rs->instance_set_base(dli, dl);
        rs->instance_set_transform(
            dli, Matrix4::LookAt(Vector3(0, 0, 0),
                                 Vector3(0, -1, -1),
                                 Vector3(0, 1, 0))
                     .Inverse());
        rs->instance_set_scenario(dli, scenario);

        RID pl = rs->light_create(LightType::Omni);
        rs->light_set_range(pl, 25.0f);
        RID pli = rs->instance_create();
        rs->instance_set_base(pli, pl);
        rs->instance_set_transform(
            pli, Matrix4::Translation(1, 2, 3));
        rs->instance_set_scenario(pli, scenario);

        rs->draw_frame();
        const SceneRenderList& sl = sceneRast->GetLastFrame();
        Check(sl.items.size() == 1,
              "視錐剔除：1/2 mesh 可見");
        Check(sl.dirs.size() == 1 && sl.points.size() == 1,
              "光源打包 1 dir + 1 omni");
        Check(sl.dirs[0].intensity == 2.0f &&
                  sl.dirs[0].castShadow,
              "dir light energy/shadow 透傳");
        Check(std::fabs(sl.points[0].radius - 25.0f) < 1e-4f &&
                  std::fabs(sl.points[0].position.y - 2.0f) <
                      1e-4f,
              "omni range/位置（instance transform）透傳");
        Check(rs->get_stats().sceneInstances == 1,
              "stats.sceneInstances = 1");
    }

    // [6] Agent 工具端到端
    {
        AI::ToolRegistry registry;
        const size_t n =
            AI::RenderTools::RegisterRenderTools(registry,
                                                 rs.get());
        Check(n >= 15, "註冊 >=15 支 render_* 工具");

        AI::ToolResult r = registry.ExecuteTool(
            "render_viewport_create",
            {{"width", "320"}, {"height", "240"}});
        Check(r.success && r.metadata.count("rid"),
              "tool: viewport_create");
        const uint64 vpId =
            std::stoull(r.metadata["rid"]);

        r = registry.ExecuteTool(
            "render_canvas_create",
            {{"viewport", std::to_string(vpId)}, {"layer", "0"}});
        Check(r.success, "tool: canvas_create");
        const std::string canvasId = r.metadata["rid"];

        r = registry.ExecuteTool(
            "render_canvas_item_create",
            {{"parent", canvasId}});
        Check(r.success, "tool: item_create");
        const std::string itemId = r.metadata["rid"];

        r = registry.ExecuteTool(
            "render_canvas_item_add_rect",
            {{"item", itemId}, {"x", "5"}, {"y", "5"},
             {"w", "30"}, {"h", "30"},
             {"color", "0.2,0.6,1.0,1.0"}});
        Check(r.success, "tool: add_rect");

        r = registry.ExecuteTool("render_draw_frame", {});
        Check(r.success &&
                  r.output.find("items2d=1") != std::string::npos,
              "tool: draw_frame 回報 1 指令");

        r = registry.ExecuteTool("render_free",
                                 {{"rid", itemId}});
        Check(r.success, "tool: free_rid");
        r = registry.ExecuteTool("render_free",
                                 {{"rid", "999999999"}});
        Check(!r.success, "tool: 非法 rid 回錯誤");
    }

    // [7] 紋理 + 單色產生
    {
        std::vector<uint8> px(4 * 4 * 4, 255);
        RID tex = rs->texture_2d_create(4, 4, px);
        int w = 0, h = 0;
        Check(tex.IsValid() &&
                  rs->texture_get_size(tex, w, h) && w == 4 &&
                  h == 4,
              "texture_2d_create + get_size");
        Check(rs->free_rid(tex), "free_rid texture");
    }

    printf("\n=== 結果 ===\n  PASS: %d\n  FAIL: %d\n",
           g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
