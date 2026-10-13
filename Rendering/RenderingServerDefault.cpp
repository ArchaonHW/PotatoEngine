#include "Rendering/RenderingServerDefault.h"

#include "Rendering/NeuralGraphics.h"      // FImage/FImageToRGBA8
#include "Rendering/PrimitiveMeshes.h"
#include "Rendering/RasterizerCanvasGL.h"
#include "Rendering/RasterizerSceneGL.h"
#include <algorithm>

namespace Potato {

RenderingServer* RenderingServer::singleton = nullptr;

RenderingServerDefault::RenderingServerDefault() {
    SetSingleton(this);
    // text 指令的 bounds 需要 font 測量——server 持有 font
    // 池，注入給 culler（未命中的 font RID → 無界不剔除）
    canvasCull.SetTextMeasurer(
        [this](RID font, const std::string& text, float scale,
               float& w, float& h) -> bool {
            const FontAtlas* f = GetFont(font);
            if (!f || !f->IsValid()) return false;
            w = f->MeasureWidth(text, scale);
            h = f->GetLineHeight(scale);
            return true;
        });
}

RenderingServerDefault::~RenderingServerDefault() {
    if (GetSingleton() == this) SetSingleton(nullptr);
}

UniquePtr<RenderingServerDefault>
RenderingServerDefault::CreateHeadless() {
    auto s = MakeUnique<RenderingServerDefault>();
    s->SetRasterizerCanvas(MakeUnique<RasterizerCanvasDummy>());
    s->SetRasterizerScene(MakeUnique<RasterizerSceneDummy>());
    return s;
}

UniquePtr<RenderingServerDefault> RenderingServerDefault::CreateGL() {
    auto s = MakeUnique<RenderingServerDefault>();
    auto canvas = MakeUnique<RasterizerCanvasGL>();
    RenderingServerDefault* self = s.get();
    canvas->SetResolvers(
        [self](RID t) { return self->TextureGLID(t); },
        [self](RID f) { return self->GetFont(f); });
    s->SetRasterizerCanvas(std::move(canvas));
    s->SetRasterizerScene(MakeUnique<RasterizerSceneGL>());
    return s;
}

void RenderingServerDefault::SetRasterizerCanvas(
    UniquePtr<RasterizerCanvas> r) {
    rasterizerCanvas = std::move(r);
}
void RenderingServerDefault::SetRasterizerScene(
    UniquePtr<RasterizerScene> r) {
    rasterizerScene = std::move(r);
}

// ============================ TEXTURE ============================

RID RenderingServerDefault::texture_2d_create(
    int w, int h, const std::vector<uint8>& rgba) {
    if (w <= 0 || h <= 0 ||
        rgba.size() != (size_t)w * (size_t)h * 4) {
        return RID();
    }
    TextureRec rec;
    rec.tex = MakeShared<Texture>();
    // LoadFromMemory 採 lazy-upload——headless 僅暫存像素
    rec.tex->LoadFromMemory(rgba.data(), w, h, 4);
    rec.w = w;
    rec.h = h;
    rec.pixels = rgba;
    return textureOwner.MakeRID(rec);
}

RID RenderingServerDefault::texture_2d_create_fimage(
    const Rendering::FImage& img) {
    std::vector<uint8> rgba;
    Rendering::FImageToRGBA8(img, rgba);
    return texture_2d_create(img.w, img.h, rgba);
}

void RenderingServerDefault::texture_2d_update(
    RID tex, int w, int h, const std::vector<uint8>& rgba) {
    TextureRec* rec = textureOwner.GetOrNull(tex);
    if (!rec || w <= 0 || h <= 0 ||
        rgba.size() != (size_t)w * (size_t)h * 4) {
        return;
    }
    rec->tex = MakeShared<Texture>();
    rec->tex->LoadFromMemory(rgba.data(), w, h, 4);
    rec->w = w;
    rec->h = h;
    rec->pixels = rgba;
}

bool RenderingServerDefault::texture_get_size(RID tex, int& w,
                                              int& h) const {
    const TextureRec* rec = textureOwner.GetOrNull(tex);
    if (!rec) return false;
    w = rec->w;
    h = rec->h;
    return true;
}

uint32 RenderingServerDefault::TextureGLID(RID tex) const {
    const TextureRec* rec = textureOwner.GetOrNull(tex);
    return rec && rec->tex ? rec->tex->GetTextureID() : 0;
}

// ============================ FONT ==============================

RID RenderingServerDefault::font_create_from_file(
    const std::string& ttfPath, float pixelHeight) {
    FontRec rec;
    if (!rec.atlas.BakeFromFile(ttfPath, pixelHeight)) {
        return RID();
    }
    return fontOwner.MakeRID(std::move(rec));
}

const FontAtlas* RenderingServerDefault::GetFont(RID font) const {
    const FontRec* rec = fontOwner.GetOrNull(font);
    return rec ? &rec->atlas : nullptr;
}

// ============================ MESH ==============================

RID RenderingServerDefault::mesh_create() {
    MeshRec rec;
    rec.mesh = MakeShared<Mesh>();
    return meshOwner.MakeRID(rec);
}

void RenderingServerDefault::mesh_set_arrays(
    RID mesh, const std::vector<Vertex>& vertices,
    const std::vector<uint32>& indices) {
    if (MeshRec* rec = meshOwner.GetOrNull(mesh)) {
        rec->mesh->SetVertices(vertices);
        rec->mesh->SetIndices(indices);
    }
}

RID RenderingServerDefault::mesh_create_primitive(
    const std::string& name, float a, float b) {
    Mesh m;
    if (name == "cube") {
        m = PrimitiveMeshes::MakeCube(a);
    } else if (name == "uvsphere") {
        m = PrimitiveMeshes::MakeUVSphere(a);
    } else if (name == "cylinder") {
        m = PrimitiveMeshes::MakeCylinder(a, b > 0 ? b : a * 2.0f);
    } else if (name == "cone") {
        m = PrimitiveMeshes::MakeCone(a, b > 0 ? b : a * 2.0f);
    } else if (name == "plane") {
        m = PrimitiveMeshes::MakePlane(a, b > 0 ? b : a);
    } else if (name == "quad") {
        m = PrimitiveMeshes::MakeQuad(a, b > 0 ? b : a);
    } else {
        return RID();
    }
    MeshRec rec;
    rec.mesh = MakeShared<Mesh>(std::move(m));
    return meshOwner.MakeRID(rec);
}

bool RenderingServerDefault::mesh_get_aabb(RID mesh,
                                           AABB& out) const {
    const Mesh* m = GetMesh(mesh);
    if (!m || !m->HasBounds()) return false;
    out = m->GetLocalAABB();
    return true;
}

const Mesh* RenderingServerDefault::GetMesh(RID mesh) const {
    const MeshRec* rec = meshOwner.GetOrNull(mesh);
    return rec ? rec->mesh.get() : nullptr;
}

// ============================ CANVAS ============================

RID RenderingServerDefault::canvas_create() {
    RID rid = canvasCull.CanvasAllocate();
    canvasCull.CanvasInitialize(rid);
    return rid;
}
void RenderingServerDefault::canvas_set_transform(
    RID canvas, const Transform2D& t) {
    canvasCull.CanvasSetTransform(canvas, t);
}
void RenderingServerDefault::canvas_set_layer(RID canvas,
                                              int layer) {
    canvasCull.CanvasSetLayer(canvas, layer);
}
void RenderingServerDefault::canvas_set_visible(RID canvas,
                                                bool v) {
    canvasCull.CanvasSetVisible(canvas, v);
}

// ========================= CANVAS ITEM ==========================

RID RenderingServerDefault::canvas_item_create() {
    RID rid = canvasCull.ItemAllocate();
    canvasCull.ItemInitialize(rid);
    return rid;
}
void RenderingServerDefault::canvas_item_set_parent(RID item,
                                                    RID parent) {
    canvasCull.ItemSetParent(item, parent);
}
void RenderingServerDefault::canvas_item_set_transform(
    RID item, const Transform2D& t) {
    canvasCull.ItemSetTransform(item, t);
}
void RenderingServerDefault::canvas_item_set_z_index(RID item,
                                                     int z) {
    canvasCull.ItemSetZIndex(item, z);
}
void RenderingServerDefault::canvas_item_set_z_relative(
    RID item, bool rel) {
    canvasCull.ItemSetZRelative(item, rel);
}
void RenderingServerDefault::canvas_item_set_visible(RID item,
                                                     bool v) {
    canvasCull.ItemSetVisible(item, v);
}
void RenderingServerDefault::canvas_item_set_modulate(
    RID item, const Color& c) {
    canvasCull.ItemSetModulate(item, c);
}
void RenderingServerDefault::canvas_item_clear(RID item) {
    canvasCull.ItemClear(item);
}

void RenderingServerDefault::canvas_item_add_rect(
    RID item, const Rect2& r, const Color& c) {
    canvasCull.ItemAddCommand(item, CanvasCmd::Rect{r, c});
}
void RenderingServerDefault::canvas_item_add_line(
    RID item, const Vector2& a, const Vector2& b,
    const Color& c, float width) {
    canvasCull.ItemAddCommand(item,
                              CanvasCmd::Line{a, b, c, width});
}
void RenderingServerDefault::canvas_item_add_polyline(
    RID item, const std::vector<Vector2>& pts, const Color& c,
    float width, bool closed) {
    CanvasCmd::PolyLine p;
    p.points = pts;
    p.color = c;
    p.width = width;
    p.closed = closed;
    canvasCull.ItemAddCommand(item, std::move(p));
}
void RenderingServerDefault::canvas_item_add_circle(
    RID item, const Vector2& pos, float radius, const Color& c,
    bool filled, int segments) {
    CanvasCmd::Circle cmd;
    cmd.pos = pos;
    cmd.radius = radius;
    cmd.color = c;
    cmd.filled = filled;
    cmd.segments = segments;
    canvasCull.ItemAddCommand(item, cmd);
}
void RenderingServerDefault::canvas_item_add_polygon(
    RID item, const std::vector<Vector2>& pts, const Color& c) {
    CanvasCmd::Polygon p;
    p.points = pts;
    p.color = c;
    canvasCull.ItemAddCommand(item, std::move(p));
}
void RenderingServerDefault::canvas_item_add_texture_rect(
    RID item, const Rect2& r, RID texture, const Rect2& uv,
    const Color& modulate) {
    CanvasCmd::TextureRect t;
    t.rect = r;
    t.texture = texture;
    t.uv = uv;
    t.modulate = modulate;
    canvasCull.ItemAddCommand(item, t);
}
void RenderingServerDefault::canvas_item_add_text(
    RID item, const Vector2& baselinePos, const std::string& text,
    RID font, float scale, const Color& c) {
    CanvasCmd::Text t;
    t.pos = baselinePos;
    t.text = text;
    t.font = font;
    t.scale = scale;
    t.color = c;
    canvasCull.ItemAddCommand(item, std::move(t));
}

// ========================== SCENARIO ============================

RID RenderingServerDefault::scenario_create() {
    RID rid = sceneCull.ScenarioAllocate();
    sceneCull.ScenarioInitialize(rid);
    return rid;
}
void RenderingServerDefault::scenario_set_background(
    RID scenario, const Color& c) {
    sceneCull.ScenarioSetBackground(scenario, c);
}
void RenderingServerDefault::scenario_set_ambient(RID scenario,
                                                  float ambient) {
    sceneCull.ScenarioSetAmbient(scenario, ambient);
}

// ========================== INSTANCE ============================

RID RenderingServerDefault::instance_create() {
    RID rid = sceneCull.InstanceAllocate();
    sceneCull.InstanceInitialize(rid);
    return rid;
}
void RenderingServerDefault::instance_set_base(RID inst,
                                               RID base) {
    // light RID → Light；其他合法 RID → Mesh（解析失敗渲染時跳過）
    if (base.IsValid() && sceneCull.LightOwns(base)) {
        sceneCull.InstanceSetBase(inst, base);
    } else if (base.IsNull() || meshOwner.Owns(base)) {
        sceneCull.InstanceSetBase(inst, base);
    } else {
        sceneCull.InstanceSetBase(inst, RID()); // 未知 RID → None
    }
}
void RenderingServerDefault::instance_set_scenario(RID inst,
                                                   RID scenario) {
    sceneCull.InstanceSetScenario(inst, scenario);
}
void RenderingServerDefault::instance_set_transform(
    RID inst, const Matrix4& t) {
    sceneCull.InstanceSetTransform(inst, t);
}
void RenderingServerDefault::instance_set_visible(RID inst,
                                                  bool v) {
    sceneCull.InstanceSetVisible(inst, v);
}
void RenderingServerDefault::instance_set_albedo(
    RID inst, const Color& c) {
    sceneCull.InstanceSetAlbedo(inst, c);
}
void RenderingServerDefault::instance_set_cast_shadow(RID inst,
                                                      bool s) {
    sceneCull.InstanceSetCastShadow(inst, s);
}

// =========================== CAMERA =============================

RID RenderingServerDefault::camera_create() {
    RID rid = sceneCull.CameraAllocate();
    sceneCull.CameraInitialize(rid);
    return rid;
}
void RenderingServerDefault::camera_set_transform(
    RID cam, const Matrix4& t) {
    sceneCull.CameraSetTransform(cam, t);
}
void RenderingServerDefault::camera_set_perspective(
    RID cam, float fovDeg, float znear, float zfar) {
    sceneCull.CameraSetPerspective(cam, fovDeg, znear, zfar);
}
void RenderingServerDefault::camera_set_orthogonal(
    RID cam, float size, float znear, float zfar) {
    sceneCull.CameraSetOrthogonal(cam, size, znear, zfar);
}

// =========================== LIGHT ==============================

RID RenderingServerDefault::light_create(LightType type) {
    RID rid = sceneCull.LightAllocate();
    sceneCull.LightInitialize(rid, type);
    return rid;
}
void RenderingServerDefault::light_set_color(RID light,
                                             const Color& c) {
    sceneCull.LightSetColor(light, c);
}
void RenderingServerDefault::light_set_energy(RID light,
                                              float e) {
    sceneCull.LightSetEnergy(light, e);
}
void RenderingServerDefault::light_set_range(RID light,
                                             float r) {
    sceneCull.LightSetRange(light, r);
}
void RenderingServerDefault::light_set_spot_angles(
    RID light, float innerDeg, float outerDeg) {
    sceneCull.LightSetSpotAngles(light, innerDeg, outerDeg);
}
void RenderingServerDefault::light_set_shadow(RID light, bool s) {
    sceneCull.LightSetShadow(light, s);
}

// ========================== VIEWPORT ============================

RID RenderingServerDefault::viewport_create() {
    return viewportOwner.MakeRID(Viewport{});
}
void RenderingServerDefault::viewport_set_size(RID vp, int w,
                                               int h) {
    if (Viewport* v = viewportOwner.GetOrNull(vp)) {
        v->w = w;
        v->h = h;
    }
}
void RenderingServerDefault::viewport_attach_canvas(RID vp,
                                                    RID canvas) {
    Viewport* v = viewportOwner.GetOrNull(vp);
    if (!v || !canvasCull.CanvasOwns(canvas)) return;
    if (std::find(v->canvases.begin(), v->canvases.end(),
                  canvas) == v->canvases.end()) {
        v->canvases.push_back(canvas);
    }
}
void RenderingServerDefault::viewport_detach_canvas(
    RID vp, RID canvas) {
    if (Viewport* v = viewportOwner.GetOrNull(vp)) {
        v->canvases.erase(std::remove(v->canvases.begin(),
                                      v->canvases.end(), canvas),
                          v->canvases.end());
    }
}
void RenderingServerDefault::viewport_attach_scenario(
    RID vp, RID scenario) {
    if (Viewport* v = viewportOwner.GetOrNull(vp))
        v->scenario = scenario;
}
void RenderingServerDefault::viewport_set_camera(RID vp,
                                                 RID camera) {
    if (Viewport* v = viewportOwner.GetOrNull(vp))
        v->camera = camera;
}
void RenderingServerDefault::viewport_set_active(RID vp,
                                                 bool active) {
    if (Viewport* v = viewportOwner.GetOrNull(vp))
        v->active = active;
}
void RenderingServerDefault::viewport_set_output_fbo(
    RID vp, uint32 fbo) {
    if (Viewport* v = viewportOwner.GetOrNull(vp))
        v->outputFbo = fbo;
}

// ========================== GENERIC =============================

bool RenderingServerDefault::free_rid(RID rid) {
    if (rid.IsNull()) return false;
    if (textureOwner.Owns(rid)) {
        textureOwner.Free(rid);
        return true;
    }
    if (meshOwner.Owns(rid)) {
        meshOwner.Free(rid);
        return true;
    }
    if (fontOwner.Owns(rid)) {
        fontOwner.Free(rid);
        return true;
    }
    if (viewportOwner.Owns(rid)) {
        viewportOwner.Free(rid);
        return true;
    }
    if (canvasCull.FreeAny(rid)) return true;
    if (sceneCull.FreeAny(rid)) return true;
    return false;
}

void RenderingServerDefault::draw_frame() {
    stats = RenderServerStats{};
    for (RID vpRID : viewportOwner.GetOwnedList()) {
        Viewport* vp = viewportOwner.GetOrNull(vpRID);
        if (!vp || !vp->active || vp->w <= 0 || vp->h <= 0)
            continue;

        // 3D 場景先（Godot：world 在 canvas layer 之下）
        if (rasterizerScene && vp->scenario.IsValid() &&
            vp->camera.IsValid()) {
            const float aspect =
                vp->h > 0 ? (float)vp->w / (float)vp->h : 1.0f;
            SceneRenderList list = sceneCull.CullScenario(
                vp->scenario, vp->camera, aspect,
                [this](RID m) { return GetMesh(m); });
            stats.sceneInstances += (int)list.items.size();
            rasterizerScene->Render(list, vp->w, vp->h,
                                    vp->outputFbo);
        }

        // 2D canvas 疊於其上
        if (rasterizerCanvas && !vp->canvases.empty()) {
            stats.canvasItems += canvasCull.RenderCanvases(
                vp->canvases,
                Rect2(0, 0, (float)vp->w, (float)vp->h),
                *rasterizerCanvas);
        }
    }
    stats.frames++;
}

} // namespace Potato
