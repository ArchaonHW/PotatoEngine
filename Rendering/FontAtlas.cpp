#include "FontAtlas.h"
#include "Logging/Logger.h"

// imstb_truetype：imgui submodule 內嵌的 stb_truetype（public domain）。
// STBTT_STATIC 讓符號不匯出——避免與連結 imgui 的下游 target
// 產生重複符號（imgui_draw.cpp 也編譯一份）。
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "imstb_truetype.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace Potato {

bool FontAtlas::Bake(const uint8* ttfData, size_t ttfSize,
                     float pixelHeight, int firstChar, int charCount,
                     int atlasSize) {
    if (!ttfData || ttfSize == 0 || pixelHeight <= 0.f ||
        atlasSize <= 0 || charCount <= 0 || firstChar < 0) {
        LOG_ERROR("FontAtlas::Bake 非法參數");
        return false;
    }
    const int fontOff = stbtt_GetFontOffsetForIndex(ttfData, 0);
    if (fontOff < 0) {
        LOG_ERROR("FontAtlas::Bake 非合法 TTF 資料");
        return false;
    }

    pixels_.assign(static_cast<size_t>(atlasSize) * atlasSize, 0);
    std::vector<stbtt_bakedchar> baked(static_cast<size_t>(charCount));
    const int packed = stbtt_BakeFontBitmap(
        ttfData, fontOff, pixelHeight, pixels_.data(), atlasSize, atlasSize,
        firstChar, charCount, baked.data());
    if (packed <= 0) {
        LOG_ERROR("FontAtlas::Bake 圖集塞不下（加大 atlasSize）");
        pixels_.clear();
        return false;
    }

    const float inv = 1.0f / atlasSize;
    glyphs_.assign(static_cast<size_t>(charCount), GlyphQuad{});
    advances_.assign(static_cast<size_t>(charCount), 0.0f);
    for (int i = 0; i < charCount; ++i) {
        const stbtt_bakedchar& c = baked[static_cast<size_t>(i)];
        GlyphQuad& q = glyphs_[static_cast<size_t>(i)];
        q.w = static_cast<float>(c.x1 - c.x0);
        q.h = static_cast<float>(c.y1 - c.y0);
        q.x = c.xoff;
        q.y = c.yoff;
        q.u0 = c.x0 * inv;
        q.v0 = c.y0 * inv;
        q.u1 = c.x1 * inv;
        q.v1 = c.y1 * inv;
        advances_[static_cast<size_t>(i)] = c.xadvance;
    }

    atlasSize_ = atlasSize;
    firstChar_ = firstChar;
    charCount_ = charCount;
    pixelHeight_ = pixelHeight;
    // 行高：像素高度（含上下沿餘量的實務近似）
    lineHeight_ = pixelHeight;
    return true;
}

bool FontAtlas::BakeFromFile(const std::string& path, float pixelHeight,
                             int firstChar, int charCount, int atlasSize) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        LOG_ERROR("FontAtlas::BakeFromFile 無法開啟 " + path);
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string data = ss.str();
    return Bake(reinterpret_cast<const uint8*>(data.data()),
                data.size(), pixelHeight, firstChar, charCount,
                atlasSize);
}

int FontAtlas::GlyphIndex(uint32 codepoint) const {
    if (codepoint >= static_cast<uint32>(firstChar_) &&
        codepoint < static_cast<uint32>(firstChar_ + charCount_)) {
        return static_cast<int>(codepoint) - firstChar_;
    }
    // '?' fallback
    const uint32 q = '?';
    if (q >= static_cast<uint32>(firstChar_) &&
        q < static_cast<uint32>(firstChar_ + charCount_)) {
        return static_cast<int>(q) - firstChar_;
    }
    return -1;
}

float FontAtlas::GetAdvance(uint32 codepoint, float scale) const {
    const int i = GlyphIndex(codepoint);
    if (i < 0) return 0.0f;
    return advances_[static_cast<size_t>(i)] * scale;
}

// UTF-8 序列 → codepoint；非法序列回 0xFFFD 並前進 1 byte
static uint32 NextCodepoint(const std::string& s, size_t& i) {
    const auto c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) { ++i; return c; }
    const size_t n = s.size();
    if ((c & 0xE0) == 0xC0 && i + 1 < n) {
        const uint32 cp = ((c & 0x1F) << 6) | (s[i + 1] & 0x3F);
        i += 2; return cp;
    }
    if ((c & 0xF0) == 0xE0 && i + 2 < n) {
        const uint32 cp = ((c & 0x0F) << 12) |
            ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6) |
            (static_cast<unsigned char>(s[i + 2]) & 0x3F);
        i += 3; return cp;
    }
    if ((c & 0xF8) == 0xF0 && i + 3 < n) {
        const uint32 cp = ((c & 0x07) << 18) |
            ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 12) |
            ((static_cast<unsigned char>(s[i + 2]) & 0x3F) << 6) |
            (static_cast<unsigned char>(s[i + 3]) & 0x3F);
        i += 4; return cp;
    }
    ++i; return 0xFFFD;
}

std::vector<GlyphQuad> FontAtlas::Layout(const std::string& text,
                                         float scale) const {
    std::vector<GlyphQuad> out;
    if (!IsValid()) return out;
    out.reserve(text.size());

    float penX = 0.0f, penY = 0.0f; // penY 為基線（換行累加行高）
    for (size_t i = 0; i < text.size();) {
        if (text[i] == '\n') {
            penX = 0.0f;
            penY += lineHeight_ * scale;
            ++i;
            continue;
        }
        const uint32 cp = NextCodepoint(text, i);
        const int gi = GlyphIndex(cp);
        if (gi < 0) continue;
        const GlyphQuad& g = glyphs_[static_cast<size_t>(gi)];
        GlyphQuad q = g;
        q.x = penX + g.x * scale;
        q.y = penY + g.y * scale;
        q.w = g.w * scale;
        q.h = g.h * scale;
        out.push_back(q);
        penX += advances_[static_cast<size_t>(gi)] * scale;
    }
    return out;
}

float FontAtlas::MeasureWidth(const std::string& text,
                              float scale) const {
    if (!IsValid()) return 0.0f;
    float penX = 0.0f, maxW = 0.0f;
    for (size_t i = 0; i < text.size();) {
        if (text[i] == '\n') {
            maxW = std::max(maxW, penX);
            penX = 0.0f;
            ++i;
            continue;
        }
        const uint32 cp = NextCodepoint(text, i);
        const int gi = GlyphIndex(cp);
        if (gi < 0) continue;
        penX += advances_[static_cast<size_t>(gi)] * scale;
    }
    return std::max(maxW, penX);
}

} // namespace Potato
