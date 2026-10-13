#pragma once

/**
 * RenderingServerDefault — RenderingServer 的預設實作。
 *
 * 對應 Godot rendering_server_default.*：持有 server 層資源
 * 池（texture/mesh/font/viewport）+ 兩個 culler（canvas/
 * scene）+ 可插拔 rasterizer 後端。
 *
 * 後端注入：
 *   CreateGL()       — RasterizerCanvasGL + RasterizerSceneGL
 *                      （需 GL context）
 *   CreateHeadless() — Dummy 後端，純記錄（測試/agent 模擬）
 *   也可建構後 SetRasterizer* 自訂。
 *
 * 建構即註冊 RenderingServer::GetSingleton()——
 * 同時只應存在一個實例（Godot singleton 語義）。
 */

#include "Core/RID.h"
#include "Rendering/FontAtlas.h"
#include "Rendering/OpenGLRenderer.h" // Mesh/Texture/Vertex
#include "Rendering/RasterizerCanvas.h"
#include "Rendering/RasterizerScene.h"
#include "Rendering/RendererCanvasCull.h"
#include "Rendering/RendererSceneCull.h"
#include "Rendering/RenderingServer.h"
#include <vector>

namespace Potato {

class RasterizerCanvasGL;
class RasterizerSceneGL;

class RenderingServerDefault : public RenderingServer {
public:
    RenderingServerDefault();
    ~RenderingServerDefault() override;

    RenderingServerDefault(const RenderingServerDefault&) = delete;
    RenderingServerDefault& operator=(
        const RenderingServerDefault&) = delete;

    // ---- 工廠（建構即設 singleton）----
    static UniquePtr<RenderingServerDefault> CreateHeadless();
    static UniquePtr<RenderingServerDefault> CreateGL();

    // ---- 後端注入 ----
    void SetRasterizerCanvas(UniquePtr<RasterizerCanvas> r);
    void SetRasterizerScene(UniquePtr<RasterizerScene> r);
    RasterizerCanvas* GetRasterizerCanvas() {
        return rasterizerCanvas.get();
    }
    RasterizerScene* GetRasterizerScene() {
        return rasterizerScene.get();
    }

    // ============================ TEXTURE ===========================
    RID texture_2d_create(int w, int h,
                          const std::vector<uint8>& rgba) override;
    RID texture_2d_create_fimage(
        const Rendering::FImage& img) override;
    void texture_2d_update(RID tex, int w, int h,
                           const std::vector<uint8>& rgba) override;
    bool texture_get_size(RID tex, int& w, int& h) const override;
    // GL 紋理 id（headless/未上傳 → 0）——GL rasterizer 用
    uint32 TextureGLID(RID tex) const;

    // ============================ FONT ==============================
    RID font_create_from_file(const std::string& ttfPath,
                              float pixelHeight) override;
    const FontAtlas* GetFont(RID font) const;

    // ============================ MESH ==============================
    RID mesh_create() override;
    void mesh_set_arrays(
        RID mesh, const std::vector<Vertex>& vertices,
        const std::vector<uint32>& indices) override;
    RID mesh_create_primitive(const std::string& name,
                              float a = 1.0f,
                              float b = 0.0f) override;
    bool mesh_get_aabb(RID mesh, AABB& out) const override;
    const Mesh* GetMesh(RID mesh) const;

    // ============================ CANVAS ============================
    RID canvas_create() override;
    void canvas_set_transform(RID canvas,
                              const Transform2D& t) override;
    void canvas_set_layer(RID canvas, int layer) override;
    void canvas_set_visible(RID canvas, bool v) override;

    // ========================= CANVAS ITEM ==========================
    RID canvas_item_create() override;
    void canvas_item_set_parent(RID item, RID parent) override;
    void canvas_item_set_transform(RID item,
                                   const Transform2D& t) override;
    void canvas_item_set_z_index(RID item, int z) override;
    void canvas_item_set_z_relative(RID item, bool rel) override;
    void canvas_item_set_visible(RID item, bool v) override;
    void canvas_item_set_modulate(RID item, const Color& c) override;
    void canvas_item_clear(RID item) override;

    void canvas_item_add_rect(RID item, const Rect2& r,
                              const Color& c) override;
    void canvas_item_add_line(RID item, const Vector2& a,
                              const Vector2& b, const Color& c,
                              float width) override;
    void canvas_item_add_polyline(
        RID item, const std::vector<Vector2>& pts,
        const Color& c, float width, bool closed) override;
    void canvas_item_add_circle(RID item, const Vector2& pos,
                                float radius, const Color& c,
                                bool filled, int segments) override;
    void canvas_item_add_polygon(
        RID item, const std::vector<Vector2>& pts,
        const Color& c) override;
    void canvas_item_add_texture_rect(
        RID item, const Rect2& r, RID texture, const Rect2& uv,
        const Color& modulate) override;
    void canvas_item_add_text(RID item, const Vector2& baselinePos,
                              const std::string& text, RID font,
                              float scale, const Color& c) override;

    // ========================== SCENARIO ============================
    RID scenario_create() override;
    void scenario_set_background(RID scenario,
                                 const Color& c) override;
    void scenario_set_ambient(RID scenario, float ambient) override;

    // ========================== INSTANCE ============================
    RID instance_create() override;
    void instance_set_base(RID inst, RID base) override;
    void instance_set_scenario(RID inst, RID scenario) override;
    void instance_set_transform(RID inst,
                                const Matrix4& t) override;
    void instance_set_visible(RID inst, bool v) override;
    void instance_set_albedo(RID inst, const Color& c) override;
    void instance_set_cast_shadow(RID inst, bool s) override;

    // =========================== CAMERA =============================
    RID camera_create() override;
    void camera_set_transform(RID cam, const Matrix4& t) override;
    void camera_set_perspective(RID cam, float fovDeg,
                                float znear, float zfar) override;
    void camera_set_orthogonal(RID cam, float size,
                               float znear, float zfar) override;

    // =========================== LIGHT ==============================
    RID light_create(LightType type) override;
    void light_set_color(RID light, const Color& c) override;
    void light_set_energy(RID light, float e) override;
    void light_set_range(RID light, float r) override;
    void light_set_spot_angles(RID light, float innerDeg,
                               float outerDeg) override;
    void light_set_shadow(RID light, bool s) override;

    // ========================== VIEWPORT ============================
    RID viewport_create() override;
    void viewport_set_size(RID vp, int w, int h) override;
    void viewport_attach_canvas(RID vp, RID canvas) override;
    void viewport_detach_canvas(RID vp, RID canvas) override;
    void viewport_attach_scenario(RID vp, RID scenario) override;
    void viewport_set_camera(RID vp, RID camera) override;
    void viewport_set_active(RID vp, bool active) override;
    void viewport_set_output_fbo(RID vp, uint32 fbo) override;

    // ========================== GENERIC =============================
    bool free_rid(RID rid) override;
    void draw_frame() override;
    RenderServerStats get_stats() const override { return stats; }

    // 內部存取（GL rasterizer 解析 RID → 資源用）
    RendererCanvasCull& CanvasCull() { return canvasCull; }
    RendererSceneCull& SceneCull() { return sceneCull; }

private:
    struct TextureRec {
        SharedPtr<Texture> tex;
        int w = 0, h = 0;
        std::vector<uint8> pixels; // headless 亦保留（update/get）
    };
    struct MeshRec {
        SharedPtr<Mesh> mesh;
    };
    struct FontRec {
        FontAtlas atlas;
    };
    struct Viewport {
        int w = 0, h = 0;
        RID scenario;
        RID camera;
        std::vector<RID> canvases;
        bool active = true;
        uint32 outputFbo = 0;
    };

    RID_Alloc<TextureRec> textureOwner{256, "texture"};
    RID_Alloc<MeshRec> meshOwner{256, "mesh"};
    RID_Alloc<FontRec> fontOwner{16, "font"};
    RID_Alloc<Viewport> viewportOwner{8, "viewport"};

    RendererCanvasCull canvasCull;
    RendererSceneCull sceneCull;

    UniquePtr<RasterizerCanvas> rasterizerCanvas;
    UniquePtr<RasterizerScene> rasterizerScene;

    RenderServerStats stats;
};

} // namespace Potato
