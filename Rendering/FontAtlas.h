#pragma once

#include "Core/CoreTypes.h"
#include <string>
#include <vector>

namespace Potato {

/**
 * 單個字形的排版 quad（pen 相對座標）+ 圖集正規化 uv。
 * v=0 為圖集頂端——與 SpriteAtlas/SpriteBatcher 慣例同向。
 */
struct GlyphQuad {
    float x = 0, y = 0;          // quad 左上（相對 pen 原點/基線）
    float w = 0, h = 0;
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
};

/**
 * FontAtlas — TTF 點陣圖字集烘焙（CPU 端，無 GL 依賴）。
 *
 * 經 imstb_truetype（imgui 內嵌 stb_truetype，public domain）把
 * 一個碼位範圍烘焙成 R8 coverage 圖集；紋理上傳由 TextRenderer
 * 負責（展開為 RGBA 白底 + alpha coverage，SpriteInstance.color 染色）。
 *
 * 排版語義：pen 原點 = 文字基線左端；'\n' 換行（pen.y +=
 *   lineHeight*scale）；未知碼位退回 '?'（再缺則略過不佔位）。
 *
 * 全部 API 皆為純 CPU——Bake 需要 TTF 位元組（檔案由呼叫端讀）。
 */
class FontAtlas {
public:
    FontAtlas() = default;

    // 烘焙 ASCII 範圍到 atlasSize² 的 R8 圖集。回傳 false：
    //   資料非合法 TTF、圖集塞不下、參數非法
    bool Bake(const uint8* ttfData, size_t ttfSize, float pixelHeight,
              int firstChar = 32, int charCount = 95,
              int atlasSize = 512);
    // 便捷：讀檔後 Bake
    bool BakeFromFile(const std::string& path, float pixelHeight,
                      int firstChar = 32, int charCount = 95,
                      int atlasSize = 512);

    bool IsValid() const { return !pixels_.empty(); }
    const std::vector<uint8>& GetPixels() const { return pixels_; }
    int GetAtlasSize() const { return atlasSize_; }
    float GetPixelHeight() const { return pixelHeight_; }
    int GetFirstChar() const { return firstChar_; }
    int GetCharCount() const { return charCount_; }

    // 排版：text → quad 列表（pen 相對，y 為基線）；scale 縮放
    std::vector<GlyphQuad> Layout(const std::string& text,
                                  float scale = 1.0f) const;
    // 最長行寬（pen 相對像素）；scale=1 為原始像素
    float MeasureWidth(const std::string& text, float scale = 1.0f) const;
    float GetLineHeight(float scale = 1.0f) const {
        return lineHeight_ * scale;
    }
    // 單碼位 advance（pen 位移）；未知碼位走 '?' fallback
    float GetAdvance(uint32 codepoint, float scale = 1.0f) const;

private:
    int GlyphIndex(uint32 codepoint) const; // -1 = 無（含 fallback）

    std::vector<uint8> pixels_;      // R8 coverage（atlasSize²）
    std::vector<GlyphQuad> glyphs_;  // uv rect + bearing（pen 相對）
    std::vector<float> advances_;    // 每碼位 advance（原始像素）
    int atlasSize_ = 0;
    int firstChar_ = 32;
    int charCount_ = 0;
    float pixelHeight_ = 0;
    float lineHeight_ = 0;
};

} // namespace Potato
