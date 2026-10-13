#pragma once

/**
 * RasterizerCanvasGL — canvas 光柵化的 OpenGL 後端。
 *
 * 把 CanvasRenderItem 指令流映射到既有 2D 基建：
 *   Rect/Polygon/Circle(filled) → PrimitiveBatcher 三角形
 *   Line/PolyLine/Circle(線框)  → PrimitiveBatcher 線段
 *     （width>1 展開成 quad——GL_LINES 無粗線）
 *   TextureRect                 → SpriteBatcher instanced
 *   Text                        → TextRenderer::QueueText
 *     （font RID → FontAtlas → 每字型一張 coverage 紋理）
 *
 * 順序保真：依列表序繪製——遇到 sprite 類指令前先 flush
 * 累積的 prims，連續同紋理 sprite 併批。
 *
 * Create 需 GL context；resolver 由 server 注入
 * （RID → GL 紋理 id / FontAtlas）。
 */

#include "Rendering/PrimitiveBatcher.h"
#include "Rendering/RasterizerCanvas.h"
#include "Rendering/SpriteBatcher.h"
#include "Rendering/TextRenderer.h"
#include <functional>
#include <unordered_map>

namespace Potato {

class FontAtlas;

class RasterizerCanvasGL : public RasterizerCanvas {
public:
    using TextureResolver = std::function<uint32(RID)>;
    using FontResolver = std::function<const FontAtlas*(RID)>;

    void SetResolvers(TextureResolver tex, FontResolver font) {
        textureResolver = std::move(tex);
        fontResolver = std::move(font);
    }

    bool Create(); // 需 GL context
    void Destroy();
    bool IsValid() const { return created; }

    void Render(const std::vector<CanvasRenderList>& lists,
                const Matrix4& viewProj) override;

private:
    void FlushPrims(const Matrix4& viewProj);
    void FlushSprites(const Matrix4& viewProj);
    // font RID → 上傳好的 coverage 紋理（lazy Create）
    uint32 FontTexture(RID font);
    const FontAtlas* ResolveFont(RID font) const {
        return fontResolver ? fontResolver(font) : nullptr;
    }

    // 指令 → 批次
    void EmitRect(const CanvasRenderItem& it,
                  const CanvasCmd::Rect& c);
    void EmitLine(const CanvasRenderItem& it,
                  const CanvasCmd::Line& c);
    void EmitPolyLine(const CanvasRenderItem& it,
                      const CanvasCmd::PolyLine& c);
    void EmitCircle(const CanvasRenderItem& it,
                    const CanvasCmd::Circle& c);
    void EmitPolygon(const CanvasRenderItem& it,
                     const CanvasCmd::Polygon& c);
    void EmitTextureRect(const Matrix4& viewProj,
                         const CanvasRenderItem& it,
                         const CanvasCmd::TextureRect& c);
    void EmitText(const Matrix4& viewProj,
                  const CanvasRenderItem& it,
                  const CanvasCmd::Text& c);

    // 粗線展開成填充 quad
    void ThickLine(const Vector2& a, const Vector2& b,
                   float width, const Color& c);

    TextureResolver textureResolver;
    FontResolver fontResolver;

    PrimitiveBatcher prims;
    SpriteBatcher sprites;
    std::unordered_map<uint64, UniquePtr<TextRenderer>> fontTex;
    const FontAtlas* fontPending = nullptr; // FontTexture 暫存

    // 連續同紋理 sprite 併批
    std::vector<SpriteInstance> spriteBatch;
    uint32 spriteBatchTex = 0;

    bool created = false;
};

} // namespace Potato
