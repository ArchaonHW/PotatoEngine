#pragma once

/**
 * RenderingServer — 引擎渲染的統一 server 介面（singleton）。
 *
 * 對應 Godot servers/rendering/rendering_server.h：
 * 所有資源以 RID 不透明句柄操作、API 全 snake_case、
 * 參數全為 POD——可被 AI agent 工具、指令佇列、
 * 序列化層直接驅動，無需碰觸 GL 物件。
 *
 * 分層（對應 Godot）：
 *   RenderingServer          ← 本類：抽象 API + singleton
 *   RenderingServerDefault   ← 實作：持有 RID owners +
 *       RendererCanvasCull / RendererSceneCull（剔除層）+
 *       Rasterizer{Canvas,Scene}（後端可換：GL / Dummy）
 *
 * 使用：
 *   RenderingServer* rs = RenderingServer::GetSingleton();
 *   RID item = rs->canvas_item_create();
 *   rs->canvas_item_add_rect(item, {0,0,100,50}, {1,0,0,1});
 *   rs->draw_frame();
 *
 * 全部 API headless 安全（Dummy rasterizer 下不碰 GL）。
 */

#include "Core/CoreTypes.h"
#include "Core/RID.h"
#include "MathUtils/Matrix4.h"
#include "MathUtils/Vector2.h"
#include "MathUtils/Vector3.h"
#include "Rendering/RenderTypes.h"
#include "Rendering/RendererSceneCull.h" // LightType
#include "Rendering/OpenGLRenderer.h"  // Vertex
#include <string>
#include <vector>

namespace Potato {

namespace Rendering { struct FImage; }

struct RenderServerStats {
    int canvasItems = 0;   // 本幀 2D 繪製指令數
    int sceneInstances = 0; // 本幀 3D 可見 instance 數
    int drawCalls = 0;
    uint64 frames = 0;
};

class RenderingServer {
public:
    virtual ~RenderingServer() = default;

    static RenderingServer* GetSingleton() { return singleton; }

    // ============================ TEXTURE ============================
    // RGBA8 建立；headless 下僅記錄像素
    virtual RID texture_2d_create(int w, int h,
                                  const std::vector<uint8>& rgba) = 0;
    // NeuralGraphics FImage 直送（agent 生成紋理橋接）
    virtual RID texture_2d_create_fimage(
        const Rendering::FImage& img) = 0;
    virtual void texture_2d_update(RID tex, int w, int h,
                                   const std::vector<uint8>& rgba) = 0;
    virtual bool texture_get_size(RID tex, int& w, int& h) const = 0;

    // ============================ FONT ==============================
    virtual RID font_create_from_file(const std::string& ttfPath,
                                      float pixelHeight) = 0;

    // ============================ MESH ==============================
    virtual RID mesh_create() = 0;
    virtual void mesh_set_arrays(
        RID mesh, const std::vector<Vertex>& vertices,
        const std::vector<uint32>& indices) = 0;
    // 程序化基本體："cube","uvsphere","cylinder","cone",
    //   "plane","quad"（PrimitiveMeshes::*；a/b 為尺寸參數）
    virtual RID mesh_create_primitive(const std::string& name,
                                      float a = 1.0f,
                                      float b = 0.0f) = 0;
    virtual bool mesh_get_aabb(RID mesh, AABB& out) const = 0;

    // ============================ CANVAS ============================
    virtual RID canvas_create() = 0;
    virtual void canvas_set_transform(RID canvas,
                                      const Transform2D& t) = 0;
    virtual void canvas_set_layer(RID canvas, int layer) = 0;
    virtual void canvas_set_visible(RID canvas, bool v) = 0;

    // ========================= CANVAS ITEM ==========================
    virtual RID canvas_item_create() = 0;
    virtual void canvas_item_set_parent(RID item, RID parent) = 0;
    virtual void canvas_item_set_transform(
        RID item, const Transform2D& t) = 0;
    virtual void canvas_item_set_z_index(RID item, int z) = 0;
    virtual void canvas_item_set_z_relative(RID item,
                                            bool rel) = 0;
    virtual void canvas_item_set_visible(RID item, bool v) = 0;
    virtual void canvas_item_set_modulate(RID item,
                                          const Color& c) = 0;
    virtual void canvas_item_clear(RID item) = 0;

    virtual void canvas_item_add_rect(RID item, const Rect2& r,
                                      const Color& c) = 0;
    virtual void canvas_item_add_line(RID item, const Vector2& a,
                                      const Vector2& b,
                                      const Color& c,
                                      float width = 1.0f) = 0;
    virtual void canvas_item_add_polyline(
        RID item, const std::vector<Vector2>& pts,
        const Color& c, float width = 1.0f,
        bool closed = false) = 0;
    virtual void canvas_item_add_circle(RID item,
                                        const Vector2& pos,
                                        float radius,
                                        const Color& c,
                                        bool filled = true,
                                        int segments = 32) = 0;
    virtual void canvas_item_add_polygon(
        RID item, const std::vector<Vector2>& pts,
        const Color& c) = 0;
    virtual void canvas_item_add_texture_rect(
        RID item, const Rect2& r, RID texture, const Rect2& uv,
        const Color& modulate) = 0;
    virtual void canvas_item_add_text(RID item,
                                      const Vector2& baselinePos,
                                      const std::string& text,
                                      RID font, float scale,
                                      const Color& c) = 0;

    // ========================== SCENARIO ============================
    virtual RID scenario_create() = 0;
    virtual void scenario_set_background(RID scenario,
                                         const Color& c) = 0;
    virtual void scenario_set_ambient(RID scenario,
                                      float ambient) = 0;

    // ========================== INSTANCE ============================
    virtual RID instance_create() = 0;
    virtual void instance_set_base(RID inst, RID base) = 0;
    virtual void instance_set_scenario(RID inst, RID scenario) = 0;
    virtual void instance_set_transform(RID inst,
                                        const Matrix4& t) = 0;
    virtual void instance_set_visible(RID inst, bool v) = 0;
    virtual void instance_set_albedo(RID inst, const Color& c) = 0;
    virtual void instance_set_cast_shadow(RID inst, bool s) = 0;

    // =========================== CAMERA =============================
    virtual RID camera_create() = 0;
    virtual void camera_set_transform(RID cam,
                                      const Matrix4& t) = 0;
    virtual void camera_set_perspective(RID cam, float fovDeg,
                                        float znear,
                                        float zfar) = 0;
    virtual void camera_set_orthogonal(RID cam, float size,
                                       float znear,
                                       float zfar) = 0;

    // =========================== LIGHT ==============================
    virtual RID light_create(LightType type) = 0;
    virtual void light_set_color(RID light, const Color& c) = 0;
    virtual void light_set_energy(RID light, float e) = 0;
    virtual void light_set_range(RID light, float r) = 0;
    virtual void light_set_spot_angles(RID light, float innerDeg,
                                       float outerDeg) = 0;
    virtual void light_set_shadow(RID light, bool s) = 0;

    // ========================== VIEWPORT ============================
    // viewport = 渲染目標（Godot viewport 對應）；draw_frame
    // 逐一渲染 active viewport：scenario(3D) → canvases(2D)
    virtual RID viewport_create() = 0;
    virtual void viewport_set_size(RID vp, int w, int h) = 0;
    virtual void viewport_attach_canvas(RID vp, RID canvas) = 0;
    virtual void viewport_detach_canvas(RID vp, RID canvas) = 0;
    virtual void viewport_attach_scenario(RID vp,
                                          RID scenario) = 0;
    virtual void viewport_set_camera(RID vp, RID camera) = 0;
    virtual void viewport_set_active(RID vp, bool active) = 0;
    virtual void viewport_set_output_fbo(RID vp, uint32 fbo) = 0;

    // ========================== GENERIC =============================
    virtual bool free_rid(RID rid) = 0;
    virtual void draw_frame() = 0;
    virtual RenderServerStats get_stats() const = 0;

protected:
    static void SetSingleton(RenderingServer* s) {
        singleton = s;
    }

private:
    static RenderingServer* singleton;
};

} // namespace Potato
