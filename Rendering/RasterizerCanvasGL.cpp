#include "Rendering/RasterizerCanvasGL.h"

#include "Rendering/FontAtlas.h"
#include <cmath>

namespace Potato {

static Vector4 ToVec4(const Color& c) {
    return Vector4(c.r, c.g, c.b, c.a);
}

bool RasterizerCanvasGL::Create() {
    if (created) return true;
    if (!prims.Create() || !sprites.Create()) return false;
    created = true;
    return true;
}

void RasterizerCanvasGL::Destroy() {
    prims.Destroy();
    sprites.Destroy();
    fontTex.clear();
    created = false;
}

void RasterizerCanvasGL::FlushPrims(const Matrix4& viewProj) {
    // 2D 畫家演算法：z-order 已由 culler 排好,關深度測試
    if (prims.GetLineVertexCount() > 0 ||
        prims.GetTriangleVertexCount() > 0) {
        prims.Flush(viewProj, false);
        prims.Clear();
    }
}

void RasterizerCanvasGL::FlushSprites(const Matrix4& viewProj) {
    if (!spriteBatch.empty() && spriteBatchTex != 0) {
        sprites.Render(spriteBatch, spriteBatchTex, viewProj);
    }
    spriteBatch.clear();
    spriteBatchTex = 0;
}

uint32 RasterizerCanvasGL::FontTexture(RID font) {
    const uint64 key = font.GetID();
    auto it = fontTex.find(key);
    if (it != fontTex.end()) return it->second->GetTexture();

    const FontAtlas* atlas = ResolveFont(font);
    if (!atlas || !atlas->IsValid()) return 0;
    auto tr = MakeUnique<TextRenderer>();
    if (!tr->Create(*atlas)) return 0;
    const uint32 id = tr->GetTexture();
    fontTex[key] = std::move(tr);
    return id;
}

// ---- 指令展開 ----

void RasterizerCanvasGL::ThickLine(const Vector2& a,
                                   const Vector2& b,
                                   float width,
                                   const Color& c) {
    const Vector2 d = (b - a);
    const float len = d.Length();
    if (len < 1e-6f) return;
    const Vector2 n = Vector2(-d.y, d.x) * (width * 0.5f / len);
    const Vector3 p0(a.x + n.x, a.y + n.y, 0.0f);
    const Vector3 p1(b.x + n.x, b.y + n.y, 0.0f);
    const Vector3 p2(b.x - n.x, b.y - n.y, 0.0f);
    const Vector3 p3(a.x - n.x, a.y - n.y, 0.0f);
    prims.Quad(p0, p1, p2, p3, ToVec4(c));
}

void RasterizerCanvasGL::EmitRect(const CanvasRenderItem& it,
                                  const CanvasCmd::Rect& c) {
    const Color col = c.color * it.modulate;
    const Vector2 p0 = it.xform.Xform(c.rect.pos);
    const Vector2 p1 =
        it.xform.Xform(Vector2(c.rect.Right(), c.rect.pos.y));
    const Vector2 p2 =
        it.xform.Xform(Vector2(c.rect.Right(), c.rect.Bottom()));
    const Vector2 p3 =
        it.xform.Xform(Vector2(c.rect.pos.x, c.rect.Bottom()));
    prims.Quad(Vector3(p0.x, p0.y, 0.0f),
               Vector3(p1.x, p1.y, 0.0f),
               Vector3(p2.x, p2.y, 0.0f),
               Vector3(p3.x, p3.y, 0.0f), ToVec4(col));
}

void RasterizerCanvasGL::EmitLine(const CanvasRenderItem& it,
                                  const CanvasCmd::Line& c) {
    const Color col = c.color * it.modulate;
    const Vector2 a = it.xform.Xform(c.a);
    const Vector2 b = it.xform.Xform(c.b);
    if (c.width > 1.0f) {
        ThickLine(a, b, c.width, col);
    } else {
        prims.Line(Vector3(a.x, a.y, 0.0f),
                   Vector3(b.x, b.y, 0.0f), ToVec4(col));
    }
}

void RasterizerCanvasGL::EmitPolyLine(
    const CanvasRenderItem& it, const CanvasCmd::PolyLine& c) {
    const Color col = c.color * it.modulate;
    const size_t n = c.points.size();
    if (n < 2) return;
    const size_t segs = c.closed ? n : n - 1;
    for (size_t i = 0; i < segs; ++i) {
        const Vector2 a = it.xform.Xform(c.points[i]);
        const Vector2 b =
            it.xform.Xform(c.points[(i + 1) % n]);
        if (c.width > 1.0f) {
            ThickLine(a, b, c.width, col);
        } else {
            prims.Line(Vector3(a.x, a.y, 0.0f),
                       Vector3(b.x, b.y, 0.0f), ToVec4(col));
        }
    }
}

void RasterizerCanvasGL::EmitCircle(
    const CanvasRenderItem& it, const CanvasCmd::Circle& c) {
    const Color col = c.color * it.modulate;
    const int segs = c.segments < 3 ? 3 : c.segments;
    const Vector2 center = it.xform.Xform(c.pos);
    // 半徑取 xform x 軸長度（等比近似——非均勻縮放取較大者）
    const float sx = it.xform.columns[0].Length();
    const float sy = it.xform.columns[1].Length();
    const float r = c.radius * (sx > sy ? sx : sy);
    constexpr float kTau = 6.28318530717959f;

    if (c.filled) {
        for (int i = 0; i < segs; ++i) {
            const float a0 = kTau * i / segs;
            const float a1 = kTau * (i + 1) / segs;
            prims.Triangle(
                Vector3(center.x, center.y, 0.0f),
                Vector3(center.x + r * std::cos(a0),
                        center.y + r * std::sin(a0), 0.0f),
                Vector3(center.x + r * std::cos(a1),
                        center.y + r * std::sin(a1), 0.0f),
                ToVec4(col));
        }
    } else {
        for (int i = 0; i < segs; ++i) {
            const float a0 = kTau * i / segs;
            const float a1 = kTau * (i + 1) / segs;
            prims.Line(
                Vector3(center.x + r * std::cos(a0),
                        center.y + r * std::sin(a0), 0.0f),
                Vector3(center.x + r * std::cos(a1),
                        center.y + r * std::sin(a1), 0.0f),
                ToVec4(col));
        }
    }
}

void RasterizerCanvasGL::EmitPolygon(
    const CanvasRenderItem& it, const CanvasCmd::Polygon& c) {
    const Color col = c.color * it.modulate;
    if (c.points.size() < 3) return;
    const Vector2 p0 = it.xform.Xform(c.points[0]);
    for (size_t i = 1; i + 1 < c.points.size(); ++i) {
        const Vector2 p1 = it.xform.Xform(c.points[i]);
        const Vector2 p2 = it.xform.Xform(c.points[i + 1]);
        prims.Triangle(Vector3(p0.x, p0.y, 0.0f),
                       Vector3(p1.x, p1.y, 0.0f),
                       Vector3(p2.x, p2.y, 0.0f), ToVec4(col));
    }
}

void RasterizerCanvasGL::EmitTextureRect(
    const Matrix4& viewProj, const CanvasRenderItem& it,
    const CanvasCmd::TextureRect& c) {
    const uint32 tex =
        textureResolver ? textureResolver(c.texture) : 0;
    if (tex == 0) return;
    FlushPrims(viewProj);
    if (tex != spriteBatchTex) FlushSprites(viewProj);
    spriteBatchTex = tex;

    // 變換後近似：中心 + 軸長度 + 旋轉
    const Vector2 center =
        it.xform.Xform(Vector2(c.rect.pos.x + c.rect.size.x * 0.5f,
                               c.rect.pos.y +
                                   c.rect.size.y * 0.5f));
    SpriteInstance inst;
    inst.x = center.x;
    inst.y = center.y;
    inst.w = c.rect.size.x * it.xform.columns[0].Length();
    inst.h = c.rect.size.y * it.xform.columns[1].Length();
    inst.u0 = c.uv.pos.x;
    inst.v0 = c.uv.pos.y;
    inst.u1 = c.uv.Right();
    inst.v1 = c.uv.Bottom();
    const Color col = c.modulate * it.modulate;
    inst.r = col.r;
    inst.g = col.g;
    inst.b = col.b;
    inst.a = col.a;
    const float rot = it.xform.GetRotation();
    inst.rotCos = std::cos(rot);
    inst.rotSin = std::sin(rot);
    spriteBatch.push_back(inst);
}

void RasterizerCanvasGL::EmitText(const Matrix4& viewProj,
                                  const CanvasRenderItem& it,
                                  const CanvasCmd::Text& c) {
    const FontAtlas* atlas = ResolveFont(c.font);
    const uint32 tex = FontTexture(c.font);
    if (!atlas || tex == 0) return;
    FlushPrims(viewProj);
    if (tex != spriteBatchTex) FlushSprites(viewProj);
    spriteBatchTex = tex;

    const Vector2 pos = it.xform.Xform(c.pos);
    const Color col = c.color * it.modulate;
    auto insts = TextRenderer::QueueText(
        *atlas, c.text, pos.x, pos.y, c.scale, ToVec4(col),
        (float)it.z);
    spriteBatch.insert(spriteBatch.end(), insts.begin(),
                       insts.end());
}

void RasterizerCanvasGL::Render(
    const std::vector<CanvasRenderList>& lists,
    const Matrix4& viewProj) {
    if (!created && !Create()) return; // 無 GL context → 略過

    for (const auto& list : lists) {
        for (const auto& item : list.items) {
            struct Visitor {
                RasterizerCanvasGL* self;
                const CanvasRenderItem& it;
                const Matrix4& vp;
                void operator()(const CanvasCmd::Rect& c) {
                    self->EmitRect(it, c);
                }
                void operator()(const CanvasCmd::Line& c) {
                    self->EmitLine(it, c);
                }
                void operator()(const CanvasCmd::PolyLine& c) {
                    self->EmitPolyLine(it, c);
                }
                void operator()(const CanvasCmd::Circle& c) {
                    self->EmitCircle(it, c);
                }
                void operator()(const CanvasCmd::Polygon& c) {
                    self->EmitPolygon(it, c);
                }
                void operator()(const CanvasCmd::TextureRect& c) {
                    self->EmitTextureRect(vp, it, c);
                }
                void operator()(const CanvasCmd::Text& c) {
                    self->EmitText(vp, it, c);
                }
                void operator()(const CanvasCmd::Transform&) {}
            } v{this, item, viewProj};
            std::visit(v, *item.cmd);
        }
    }
    FlushPrims(viewProj);
    FlushSprites(viewProj);
}

} // namespace Potato
