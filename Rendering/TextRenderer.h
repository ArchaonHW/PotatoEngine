#pragma once

#include "Core/CoreTypes.h"
#include "MathUtils/Vector4.h"
#include "Rendering/SpriteBatcher.h"
#include <string>
#include <vector>

namespace Potato {

class FontAtlas;

/**
 * TextRenderer — 位圖文字渲染器（SpriteBatcher 的特化消費端）。
 *
 * Create 把 FontAtlas 的 R8 coverage 展開上傳成 RGBA8 紋理
 * （rgb=白、alpha=coverage——SpriteInstance.color 乘上去即為
 * 可染色文字）。QueueText 產生 SpriteInstance 列表餵
 * SpriteBatcher::Render——同字型同圖集的文字 draw call 恆為 1。
 *
 * 用法：
 *   TextRenderer tr;  tr.Create(font);
 *   auto inst = tr.QueueText(font, "HP:100", 8, 24, 1.0f, {1,1,1,1});
 *   batcher.Render(inst, tr.GetTexture(), ortho);
 *
 * 座標慣例同 SpriteBatcher：像素、左上原點；(x, baselineY) 是
 * 第一行基線左端。QueueText 為純 CPU——headless 可測。
 */
class TextRenderer {
public:
    TextRenderer() = default;
    ~TextRenderer();

    TextRenderer(const TextRenderer&) = delete;
    TextRenderer& operator=(const TextRenderer&) = delete;
    TextRenderer(TextRenderer&& o) noexcept : tex(o.tex) { o.tex = 0; }
    TextRenderer& operator=(TextRenderer&& o) noexcept {
        if (this != &o) { Destroy(); tex = o.tex; o.tex = 0; }
        return *this;
    }

    bool Create(const FontAtlas& atlas);
    void Destroy();
    bool IsValid() const { return tex != 0; }
    uint32 GetTexture() const { return tex; }

    // ---- 純 CPU（headless 可測）----
    // text → SpriteInstance 列表（z 皆為 zOrder）。
    // (x, baselineY)：第一行基線左端的像素座標
    static std::vector<SpriteInstance> QueueText(
        const FontAtlas& font, const std::string& text,
        float x, float baselineY, float scale = 1.0f,
        const Vector4& color = Vector4(1, 1, 1, 1),
        float zOrder = 0.0f);

    // R8 → RGBA(255,255,255,coverage) 展開（供自訂上傳/測試）
    static std::vector<uint8> ExpandToRGBA(const FontAtlas& atlas);

private:
    uint32 tex = 0;
};

} // namespace Potato
