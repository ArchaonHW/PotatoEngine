#include "AI/RenderAgentTools.h"

#include "Rendering/NeuralGraphics.h"
#include "Rendering/RenderingServer.h"
#include <cmath>
#include <cstdlib>
#include <unordered_map>

namespace Potato {
namespace AI {
namespace RenderTools {

using Params = std::unordered_map<std::string, std::string>;

// ---- 參數解析 helpers（全部失敗→預設值，不 throw）----

static float PF(const Params& p, const char* name,
                float def = 0.0f) {
    auto it = p.find(name);
    return it != p.end() ? (float)std::atof(it->second.c_str())
                         : def;
}
static int PI(const Params& p, const char* name, int def = 0) {
    auto it = p.find(name);
    return it != p.end() ? std::atoi(it->second.c_str()) : def;
}
static std::string PS(const Params& p, const char* name,
                      const std::string& def = "") {
    auto it = p.find(name);
    return it != p.end() ? it->second : def;
}
static bool PB(const Params& p, const char* name,
               bool def = false) {
    auto it = p.find(name);
    if (it == p.end()) return def;
    const std::string& v = it->second;
    return v == "1" || v == "true" || v == "yes" || v == "on";
}
static RID PRID(const Params& p, const char* name) {
    auto it = p.find(name);
    if (it == p.end()) return RID();
    return RID::FromUint64(
        (uint64)std::strtoull(it->second.c_str(), nullptr, 10));
}
// "a,b" 兩浮點（MSVC 無 sscanf 安全替代——strtof 手切）
static bool ParsePair(const std::string& s, float& x,
                      float& y) {
    const size_t comma = s.find(',');
    if (comma == std::string::npos) return false;
    x = (float)std::atof(s.substr(0, comma).c_str());
    y = (float)std::atof(s.substr(comma + 1).c_str());
    return true;
}

static Vector2 PV2(const Params& p, const char* name,
                   const Vector2& def = Vector2(0, 0)) {
    auto it = p.find(name);
    if (it == p.end()) return def;
    float x = 0, y = 0;
    if (!ParsePair(it->second, x, y)) return def;
    return Vector2(x, y);
}
static Color PC(const Params& p, const char* name,
                const Color& def = Color(1, 1, 1, 1)) {
    auto it = p.find(name);
    if (it == p.end()) return def;
    float v[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    const std::string& s = it->second;
    size_t pos = 0;
    int n = 0;
    while (pos <= s.size() && n < 4) {
        const size_t end = s.find(',', pos);
        v[n++] = (float)std::atof(
            s.substr(pos, end == std::string::npos
                             ? std::string::npos
                             : end - pos)
                .c_str());
        if (end == std::string::npos) break;
        pos = end + 1;
    }
    if (n < 3) return def;
    return Color(v[0], v[1], v[2], v[3]);
}
// "x,y;x,y;..." 點列
static std::vector<Vector2> PPoints(const Params& p,
                                    const char* name) {
    std::vector<Vector2> out;
    auto it = p.find(name);
    if (it == p.end()) return out;
    const std::string& s = it->second;
    size_t pos = 0;
    while (pos < s.size()) {
        const size_t end = s.find(';', pos);
        const std::string tok =
            s.substr(pos, end == std::string::npos
                              ? std::string::npos
                              : end - pos);
        float x = 0, y = 0;
        if (ParsePair(tok, x, y)) out.emplace_back(x, y);
        if (end == std::string::npos) break;
        pos = end + 1;
    }
    return out;
}
// "f,f,f,..." latent 向量
static std::vector<float> PFloats(const Params& p,
                                  const char* name) {
    std::vector<float> out;
    auto it = p.find(name);
    if (it == p.end()) return out;
    const std::string& s = it->second;
    size_t pos = 0;
    while (pos < s.size()) {
        size_t end = s.find(',', pos);
        out.push_back((float)std::atof(
            s.substr(pos, end == std::string::npos
                             ? std::string::npos
                             : end - pos)
                .c_str()));
        if (end == std::string::npos) break;
        pos = end + 1;
    }
    return out;
}

static ToolResult Ok(const std::string& out,
                     uint64 rid = 0) {
    ToolResult r;
    r.success = true;
    r.output = out;
    if (rid) r.metadata["rid"] = std::to_string(rid);
    return r;
}
static ToolResult Err(const std::string& msg) {
    ToolResult r;
    r.success = false;
    r.error = msg;
    return r;
}
static bool HasRS(RenderingServer* rs) { return rs != nullptr; }

// ---- neural field 命名註冊表 ----

static std::unordered_map<std::string, Rendering::NeuralFieldImage*>&
FieldRegistry() {
    static std::unordered_map<std::string,
                              Rendering::NeuralFieldImage*>
        reg;
    return reg;
}

void RegisterNeuralField(const std::string& name,
                         Rendering::NeuralFieldImage* field) {
    FieldRegistry()[name] = field;
}

// ---- 工具建構 ----

static Tool MakeTool(const char* name, const char* desc,
                     std::initializer_list<
                         std::pair<const char*, const char*>>
                         params,
                     ToolFunction fn) {
    Tool t;
    t.name = name;
    t.description = desc;
    for (auto& pr : params)
        t.parameters[pr.first] = pr.second;
    t.function = std::move(fn);
    return t;
}

size_t RegisterRenderTools(ToolRegistry& registry,
                           RenderingServer* server) {
    size_t n = 0;
    auto add = [&](Tool t) {
        registry.RegisterTool(std::move(t));
        ++n;
    };

    // ---- viewport / canvas ----
    add(MakeTool("render_viewport_create",
                 "建立 viewport 渲染目標（Godot viewport 對應）",
                 {{"width", "像素寬"}, {"height", "像素高"}},
                 [server](const Params& p) -> ToolResult {
                     if (!HasRS(server)) return Err("no server");
                     RID vp = server->viewport_create();
                     server->viewport_set_size(
                         vp, PI(p, "width", 800),
                         PI(p, "height", 600));
                     return Ok("viewport", vp.GetID());
                 }));

    add(MakeTool(
        "render_canvas_create",
        "建立 2D canvas layer 並掛上 viewport",
        {{"viewport", "viewport rid"}, {"layer", "層序(小=底層)"}},
        [server](const Params& p) -> ToolResult {
            if (!HasRS(server)) return Err("no server");
            RID c = server->canvas_create();
            server->canvas_set_layer(c, PI(p, "layer"));
            RID vp = PRID(p, "viewport");
            if (vp.IsValid())
                server->viewport_attach_canvas(vp, c);
            return Ok("canvas", c.GetID());
        }));

    // ---- canvas item ----
    add(MakeTool(
        "render_canvas_item_create",
        "建立 canvas item（parent 可為 canvas 或 item rid）",
        {{"parent", "父 RID（canvas/item）"}},
        [server](const Params& p) -> ToolResult {
            if (!HasRS(server)) return Err("no server");
            RID it = server->canvas_item_create();
            RID parent = PRID(p, "parent");
            if (parent.IsValid())
                server->canvas_item_set_parent(it, parent);
            return Ok("canvas_item", it.GetID());
        }));

    add(MakeTool(
        "render_canvas_item_add_rect",
        "item 加實心矩形指令",
        {{"item", "item rid"}, {"x", "左"}, {"y", "上"},
         {"w", "寬"}, {"h", "高"}, {"color", "r,g,b,a"}},
        [server](const Params& p) -> ToolResult {
            if (!HasRS(server)) return Err("no server");
            server->canvas_item_add_rect(
                PRID(p, "item"),
                Rect2(PF(p, "x"), PF(p, "y"), PF(p, "w"),
                      PF(p, "h")),
                PC(p, "color"));
            return Ok("rect");
        }));

    add(MakeTool(
        "render_canvas_item_add_line",
        "item 加線段指令",
        {{"item", "item rid"}, {"a", "x0,y0"}, {"b", "x1,y1"},
         {"color", "r,g,b,a"}, {"width", "線寬"}},
        [server](const Params& p) -> ToolResult {
            if (!HasRS(server)) return Err("no server");
            server->canvas_item_add_line(
                PRID(p, "item"), PV2(p, "a"), PV2(p, "b"),
                PC(p, "color"), PF(p, "width", 1.0f));
            return Ok("line");
        }));

    add(MakeTool(
        "render_canvas_item_add_polyline",
        "item 加折線指令（points='x,y;x,y'）",
        {{"item", "item rid"}, {"points", "x,y;x,y;..."},
         {"color", "r,g,b,a"}, {"width", "線寬"},
         {"closed", "1=閉合"}},
        [server](const Params& p) -> ToolResult {
            if (!HasRS(server)) return Err("no server");
            server->canvas_item_add_polyline(
                PRID(p, "item"), PPoints(p, "points"),
                PC(p, "color"), PF(p, "width", 1.0f),
                PB(p, "closed"));
            return Ok("polyline");
        }));

    add(MakeTool(
        "render_canvas_item_add_circle",
        "item 加圓指令",
        {{"item", "item rid"}, {"pos", "x,y"},
         {"radius", "半徑"}, {"color", "r,g,b,a"},
         {"filled", "1=實心"}, {"segments", "分段"}},
        [server](const Params& p) -> ToolResult {
            if (!HasRS(server)) return Err("no server");
            server->canvas_item_add_circle(
                PRID(p, "item"), PV2(p, "pos"),
                PF(p, "radius"), PC(p, "color"),
                PB(p, "filled", true), PI(p, "segments", 32));
            return Ok("circle");
        }));

    add(MakeTool(
        "render_canvas_item_add_polygon",
        "item 加凸多邊形填充（points='x,y;x,y'）",
        {{"item", "item rid"}, {"points", "x,y;x,y;..."},
         {"color", "r,g,b,a"}},
        [server](const Params& p) -> ToolResult {
            if (!HasRS(server)) return Err("no server");
            server->canvas_item_add_polygon(
                PRID(p, "item"), PPoints(p, "points"),
                PC(p, "color"));
            return Ok("polygon");
        }));

    add(MakeTool(
        "render_canvas_item_add_texture_rect",
        "item 加紋理矩形",
        {{"item", "item rid"}, {"x", "左"}, {"y", "上"},
         {"w", "寬"}, {"h", "高"}, {"texture", "texture rid"},
         {"color", "modulate r,g,b,a"}},
        [server](const Params& p) -> ToolResult {
            if (!HasRS(server)) return Err("no server");
            server->canvas_item_add_texture_rect(
                PRID(p, "item"),
                Rect2(PF(p, "x"), PF(p, "y"), PF(p, "w"),
                      PF(p, "h")),
                PRID(p, "texture"),
                Rect2(0, 0, 1, 1), PC(p, "color"));
            return Ok("texture_rect");
        }));

    add(MakeTool(
        "render_canvas_item_add_text",
        "item 加位圖文字（font=font rid）",
        {{"item", "item rid"}, {"pos", "基線左端 x,y"},
         {"text", "字串"}, {"font", "font rid"},
         {"scale", "縮放"}, {"color", "r,g,b,a"}},
        [server](const Params& p) -> ToolResult {
            if (!HasRS(server)) return Err("no server");
            server->canvas_item_add_text(
                PRID(p, "item"), PV2(p, "pos"),
                PS(p, "text"), PRID(p, "font"),
                PF(p, "scale", 1.0f), PC(p, "color"));
            return Ok("text");
        }));

    add(MakeTool(
        "render_canvas_item_set_transform",
        "item TRS 變換（x,y,rotation_deg,sx,sy）",
        {{"item", "item rid"}, {"x", "平移 x"},
         {"y", "平移 y"}, {"rotation_deg", "旋轉角度"},
         {"sx", "縮放 x"}, {"sy", "縮放 y"}},
        [server](const Params& p) -> ToolResult {
            if (!HasRS(server)) return Err("no server");
            constexpr float kD2R = 3.14159265358979f / 180.f;
            server->canvas_item_set_transform(
                PRID(p, "item"),
                Transform2D::TRS(PF(p, "x"), PF(p, "y"),
                                 PF(p, "rotation_deg") * kD2R,
                                 PF(p, "sx", 1.0f),
                                 PF(p, "sy", 1.0f)));
            return Ok("transform");
        }));

    add(MakeTool(
        "render_canvas_item_set_visible",
        "item 可見性", {{"item", "item rid"}, {"visible", "1/0"}},
        [server](const Params& p) -> ToolResult {
            if (!HasRS(server)) return Err("no server");
            server->canvas_item_set_visible(PRID(p, "item"),
                                            PB(p, "visible"));
            return Ok("visible");
        }));

    add(MakeTool(
        "render_canvas_item_set_z_index",
        "item 的 z 排序", {{"item", "item rid"}, {"z", "整數"}},
        [server](const Params& p) -> ToolResult {
            if (!HasRS(server)) return Err("no server");
            server->canvas_item_set_z_index(PRID(p, "item"),
                                            PI(p, "z"));
            return Ok("z");
        }));

    add(MakeTool("render_canvas_item_clear",
                 "清空 item 指令列表",
                 {{"item", "item rid"}},
                 [server](const Params& p) -> ToolResult {
                     if (!HasRS(server)) return Err("no server");
                     server->canvas_item_clear(PRID(p, "item"));
                     return Ok("cleared");
                 }));

    // ---- texture / font ----
    add(MakeTool(
        "render_texture_create",
        "建立單色 RGBA8 紋理",
        {{"w", "寬"}, {"h", "高"}, {"color", "r,g,b,a 0..1"}},
        [server](const Params& p) -> ToolResult {
            if (!HasRS(server)) return Err("no server");
            const int w = PI(p, "w", 8), h = PI(p, "h", 8);
            const Color c = PC(p, "color");
            std::vector<uint8> px((size_t)w * h * 4);
            auto q = [](float v) {
                return (uint8)(v < 0 ? 0
                                     : v > 1 ? 255
                                             : (int)(v * 255.f));
            };
            for (size_t i = 0; i < px.size(); i += 4) {
                px[i] = q(c.r);
                px[i + 1] = q(c.g);
                px[i + 2] = q(c.b);
                px[i + 3] = q(c.a);
            }
            RID t = server->texture_2d_create(w, h, px);
            return t.IsValid() ? Ok("texture", t.GetID())
                               : Err("texture create failed");
        }));

    add(MakeTool(
        "render_texture_generate_neural",
        "以命名 NeuralFieldImage 生成紋理（INR latent→image）",
        {{"field", "註冊名（RegisterNeuralField）"},
         {"latent", "f,f,... latent 向量"},
         {"w", "寬"}, {"h", "高"}},
        [server](const Params& p) -> ToolResult {
            if (!HasRS(server)) return Err("no server");
            auto fit = FieldRegistry().find(PS(p, "field"));
            if (fit == FieldRegistry().end() ||
                !fit->second || !fit->second->Ready()) {
                return Err("unknown/unready neural field");
            }
            Rendering::FImage img = fit->second->Generate(
                PFloats(p, "latent"), PI(p, "w", 32),
                PI(p, "h", 32));
            RID t = server->texture_2d_create_fimage(img);
            return t.IsValid()
                       ? Ok("neural_texture", t.GetID())
                       : Err("texture create failed");
        }));

    add(MakeTool(
        "render_font_create",
        "由 TTF 烘焙字集",
        {{"path", "ttf 路徑"}, {"pixel_height", "像素高"}},
        [server](const Params& p) -> ToolResult {
            if (!HasRS(server)) return Err("no server");
            RID f = server->font_create_from_file(
                PS(p, "path"), PF(p, "pixel_height", 24.0f));
            return f.IsValid() ? Ok("font", f.GetID())
                               : Err("font bake failed");
        }));

    // ---- 3D ----
    add(MakeTool(
        "render_mesh_create_primitive",
        "程序化網格（cube/uvsphere/cylinder/cone/plane/quad）",
        {{"name", "類型"}, {"a", "尺寸1"}, {"b", "尺寸2"}},
        [server](const Params& p) -> ToolResult {
            if (!HasRS(server)) return Err("no server");
            RID m = server->mesh_create_primitive(
                PS(p, "name", "cube"), PF(p, "a", 1.0f),
                PF(p, "b"));
            return m.IsValid() ? Ok("mesh", m.GetID())
                               : Err("unknown primitive");
        }));

    add(MakeTool("render_scenario_create",
                 "建立 3D scenario",
                 {{"background", "r,g,b,a"},
                  {"ambient", "環境光強度"}},
                 [server](const Params& p) -> ToolResult {
                     if (!HasRS(server)) return Err("no server");
                     RID s = server->scenario_create();
                     server->scenario_set_background(
                         s, PC(p, "background",
                               Color(0, 0, 0, 1)));
                     server->scenario_set_ambient(
                         s, PF(p, "ambient", 0.05f));
                     return Ok("scenario", s.GetID());
                 }));

    add(MakeTool(
        "render_instance_create",
        "建立 instance 掛 mesh 進 scenario",
        {{"scenario", "scenario rid"}, {"base", "mesh/light rid"},
         {"x", "平移 x"}, {"y", "平移 y"}, {"z", "平移 z"}},
        [server](const Params& p) -> ToolResult {
            if (!HasRS(server)) return Err("no server");
            RID inst = server->instance_create();
            server->instance_set_base(inst, PRID(p, "base"));
            server->instance_set_transform(
                inst, Matrix4::Translation(PF(p, "x"),
                                           PF(p, "y"),
                                           PF(p, "z")));
            server->instance_set_scenario(inst,
                                          PRID(p, "scenario"));
            return Ok("instance", inst.GetID());
        }));

    add(MakeTool(
        "render_light_create",
        "建立光源 instance（directional/omni/spot）",
        {{"scenario", "scenario rid"},
         {"type", "directional|omni|spot"},
         {"x,y,z", "位置（directional 用朝向 dx,dy,dz）"},
         {"dx,dy,dz", "朝向"}, {"color", "r,g,b,a"},
         {"energy", "強度"}, {"range", "範圍"},
         {"inner,outer", "spot 錐角（度）"},
         {"shadow", "1=投影"}},
        [server](const Params& p) -> ToolResult {
            if (!HasRS(server)) return Err("no server");
            const std::string t = PS(p, "type", "omni");
            LightType lt = t == "directional"
                               ? LightType::Directional
                           : t == "spot" ? LightType::Spot
                                         : LightType::Omni;
            RID light = server->light_create(lt);
            server->light_set_color(light, PC(p, "color"));
            server->light_set_energy(light,
                                     PF(p, "energy", 1.0f));
            server->light_set_range(light,
                                    PF(p, "range", 10.0f));
            if (lt == LightType::Spot) {
                server->light_set_spot_angles(
                    light, PF(p, "inner", 12.5f),
                    PF(p, "outer", 17.5f));
            }
            server->light_set_shadow(light, PB(p, "shadow"));

            // 光源姿態由 instance transform 供給——
            // -Z 軸指向 (dx,dy,dz)，位置為 (x,y,z)
            RID inst = server->instance_create();
            server->instance_set_base(inst, light);
            const Vector3 pos(PF(p, "x"), PF(p, "y"),
                              PF(p, "z"));
            const Vector3 dir(PF(p, "dx", 0.0f),
                              PF(p, "dy", -1.0f),
                              PF(p, "dz", 0.0f));
            const Vector3 tgt = pos + dir.Normalized();
            Matrix4 xf = Matrix4::LookAt(
                             pos, tgt, Vector3(0, 1, 0))
                             .Inverse();
            server->instance_set_transform(inst, xf);
            server->instance_set_scenario(inst,
                                          PRID(p, "scenario"));
            return Ok("light_instance", inst.GetID());
        }));

    add(MakeTool(
        "render_camera_create",
        "建立相機綁 viewport（pos + look 目標）",
        {{"viewport", "viewport rid"},
         {"x,y,z", "位置"}, {"lx,ly,lz", "注視點"},
         {"fov", "度"}, {"znear", "近裁剪"},
         {"zfar", "遠裁剪"}},
        [server](const Params& p) -> ToolResult {
            if (!HasRS(server)) return Err("no server");
            RID cam = server->camera_create();
            server->camera_set_perspective(
                cam, PF(p, "fov", 60.0f), PF(p, "znear", 0.1f),
                PF(p, "zfar", 1000.0f));
            const Vector3 eye(PF(p, "x"), PF(p, "y"),
                              PF(p, "z"));
            const Vector3 at(PF(p, "lx"), PF(p, "ly"),
                             PF(p, "lz"));
            server->camera_set_transform(
                cam,
                Matrix4::LookAt(eye, at, Vector3(0, 1, 0))
                    .Inverse());
            RID vp = PRID(p, "viewport");
            if (vp.IsValid())
                server->viewport_set_camera(vp, cam);
            return Ok("camera", cam.GetID());
        }));

    add(MakeTool(
        "render_viewport_attach_scenario",
        "viewport 綁 scenario",
        {{"viewport", "rid"}, {"scenario", "rid"}},
        [server](const Params& p) -> ToolResult {
            if (!HasRS(server)) return Err("no server");
            server->viewport_attach_scenario(PRID(p, "viewport"),
                                             PRID(p,
                                                  "scenario"));
            return Ok("attached");
        }));

    // ---- frame / generic ----
    add(MakeTool("render_draw_frame",
                 "執行一幀：全部 active viewport 渲染",
                 {},
                 [server](const Params&) -> ToolResult {
                     if (!HasRS(server)) return Err("no server");
                     server->draw_frame();
                     const RenderServerStats s =
                         server->get_stats();
                     char buf[160];
                     std::snprintf(
                         buf, sizeof(buf),
                         "frame=%llu items2d=%d inst3d=%d",
                         (unsigned long long)s.frames,
                         s.canvasItems, s.sceneInstances);
                     return Ok(buf);
                 }));

    add(MakeTool("render_free",
                 "釋放 RID 資源（任意型別自動判別）",
                 {{"rid", "資源 id"}},
                 [server](const Params& p) -> ToolResult {
                     if (!HasRS(server)) return Err("no server");
                     return server->free_rid(PRID(p, "rid"))
                                ? Ok("freed")
                                : Err("unknown rid");
                 }));

    return n;
}

} // namespace RenderTools
} // namespace AI
} // namespace Potato
