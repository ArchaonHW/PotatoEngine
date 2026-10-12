#include "TextRenderer.h"
#include "FontAtlas.h"
#include "Logging/Logger.h"
#include <glad/glad.h>

namespace Potato {

TextRenderer::~TextRenderer() { Destroy(); }

bool TextRenderer::Create(const FontAtlas& atlas) {
    if (!atlas.IsValid()) {
        LOG_ERROR("TextRenderer::Create atlas 無效");
        return false;
    }
    Destroy();
    const auto rgba = ExpandToRGBA(atlas);
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                 atlas.GetAtlasSize(), atlas.GetAtlasSize(), 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    // 文字圖集：LINEAR 放大平滑；CLAMP 防邊緣滲色
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return true;
}

void TextRenderer::Destroy() {
    if (tex) {
        glDeleteTextures(1, &tex);
        tex = 0;
    }
}

std::vector<uint8> TextRenderer::ExpandToRGBA(const FontAtlas& atlas) {
    const size_t n = atlas.GetPixels().size();
    std::vector<uint8> rgba(n * 4);
    for (size_t i = 0; i < n; ++i) {
        rgba[i * 4 + 0] = 255;
        rgba[i * 4 + 1] = 255;
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = atlas.GetPixels()[i];
    }
    return rgba;
}

std::vector<SpriteInstance> TextRenderer::QueueText(
    const FontAtlas& font, const std::string& text,
    float x, float baselineY, float scale,
    const Vector4& color, float zOrder) {
    std::vector<SpriteInstance> out;
    const auto quads = font.Layout(text, scale);
    out.reserve(quads.size());
    for (const GlyphQuad& q : quads) {
        if (q.w <= 0.f || q.h <= 0.f) continue; // 空白字形無 quad
        SpriteInstance inst;
        inst.x = x + q.x + q.w * 0.5f;
        inst.y = baselineY + q.y + q.h * 0.5f;
        inst.w = q.w;
        inst.h = q.h;
        inst.u0 = q.u0; inst.v0 = q.v0;
        inst.u1 = q.u1; inst.v1 = q.v1;
        inst.r = color.x; inst.g = color.y;
        inst.b = color.z; inst.a = color.w;
        inst.z = zOrder;
        out.push_back(inst);
    }
    return out;
}

} // namespace Potato
