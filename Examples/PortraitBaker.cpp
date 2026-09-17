// PortraitBaker - 引擎內 2D 分層立繪產生器（取代 tools/generate_roc_cards.py
// 的 PIL 依賴）
//
// 讀取 character card JSON（potato.character_card/1 的 portrait 參數：
// build/head/beard/glasses + faction→軍服色 + rarity→框色），
// CPU 軟體光栅產生分層立繪，輸出 PNG。
//
// 用法:
//   PortraitBaker <card.json> <out.png> [--figure-only] [--size WxH]
//   PortraitBaker --batch <cards_dir> <out_dir> [--figure-only]
//
//   --figure-only  只輸出人物本體（透明背景，512x768），不含卡框/文字/數值條
//   預設           完整卡面（對齊 Python 版 render_card 的版面）
//
// 文字：以 ImGui 的 ImFontAtlas（stb_truetype）在 CPU 側點陣化
//       C:/Windows/Fonts/msjhbd.ttc；找不到字型時跳過文字層仍輸出卡面。

#include "Serialization/JsonParser.h"
#include "Rendering/ImageCodec.h"

#include <imgui.h>
#include <imgui_internal.h>   // ImFontAtlasBuildLegacyPreloadAllGlyphRanges

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using Potato::JsonValue;

namespace {

// ============================================================
// 軟體畫布（RGBA8，不透明覆寫語意——與 PIL 的填色行為一致）
// ============================================================

struct RGBA {
    uint8_t r, g, b, a;
    static RGBA C(int r, int g, int b, int a = 255) {
        return {static_cast<uint8_t>(r), static_cast<uint8_t>(g),
                static_cast<uint8_t>(b), static_cast<uint8_t>(a)};
    }
    RGBA Scale(float f) const {
        return C(static_cast<int>(r * f), static_cast<int>(g * f),
                 static_cast<int>(b * f), a);
    }
};

class Canvas {
public:
    int w = 0, h = 0;
    std::vector<uint8_t> px;   // RGBA

    Canvas() = default;
    Canvas(int width, int height, RGBA bg) { Reset(width, height, bg); }

    void Reset(int width, int height, RGBA bg) {
        w = width; h = height;
        px.assign(static_cast<size_t>(w) * h * 4, 0);
        Fill(bg);
    }

    void Set(int x, int y, RGBA c) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        size_t i = (static_cast<size_t>(y) * w + x) * 4;
        px[i] = c.r; px[i + 1] = c.g; px[i + 2] = c.b; px[i + 3] = c.a;
    }

    // alpha blend 覆寫（文字層用）
    void Blend(int x, int y, RGBA c) {
        if (x < 0 || y < 0 || x >= w || y >= h || c.a == 0) return;
        if (c.a == 255) { Set(x, y, c); return; }
        size_t i = (static_cast<size_t>(y) * w + x) * 4;
        float t = c.a / 255.0f;
        px[i]     = static_cast<uint8_t>(px[i] * (1 - t) + c.r * t);
        px[i + 1] = static_cast<uint8_t>(px[i + 1] * (1 - t) + c.g * t);
        px[i + 2] = static_cast<uint8_t>(px[i + 2] * (1 - t) + c.b * t);
        px[i + 3] = static_cast<uint8_t>(
            std::min(255.0f, px[i + 3] + c.a * (1 - px[i + 3] / 255.0f)));
    }

    void Fill(RGBA c) {
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) Set(x, y, c);
    }

    // 徑向漸層（中心 inner → 邊緣 outer），cy 比例對齊 Python 版
    void RadialGradient(RGBA inner, RGBA outer, float cyRatio) {
        float cx = w * 0.5f, cy = h * cyRatio;
        float maxd = std::hypot(std::max(cx, w - cx), std::max(cy, h - cy));
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                float t = std::min(1.0f, std::hypot(x - cx, y - cy) / maxd);
                Set(x, y, RGBA::C(
                    static_cast<int>(inner.r + (outer.r - inner.r) * t),
                    static_cast<int>(inner.g + (outer.g - inner.g) * t),
                    static_cast<int>(inner.b + (outer.b - inner.b) * t)));
            }
        }
    }

    void FillRect(float x0, float y0, float x1, float y1, RGBA c) {
        int ix0 = static_cast<int>(std::round(std::min(x0, x1)));
        int ix1 = static_cast<int>(std::round(std::max(x0, x1)));
        int iy0 = static_cast<int>(std::round(std::min(y0, y1)));
        int iy1 = static_cast<int>(std::round(std::max(y0, y1)));
        for (int y = iy0; y <= iy1; ++y)
            for (int x = ix0; x <= ix1; ++x) Set(x, y, c);
    }

    void StrokeRect(float x0, float y0, float x1, float y1, RGBA c,
                    int width = 1) {
        for (int k = 0; k < width; ++k) {
            for (int x = static_cast<int>(x0 + k);
                 x <= static_cast<int>(x1 - k); ++x) {
                Set(x, static_cast<int>(y0 + k), c);
                Set(x, static_cast<int>(y1 - k), c);
            }
            for (int y = static_cast<int>(y0 + k);
                 y <= static_cast<int>(y1 - k); ++y) {
                Set(static_cast<int>(x0 + k), y, c);
                Set(static_cast<int>(x1 - k), y, c);
            }
        }
    }

    // 實心橢圓（掃描線）
    void FillEllipse(float cx, float cy, float rx, float ry, RGBA c) {
        if (rx <= 0 || ry <= 0) return;
        int y0 = static_cast<int>(std::floor(cy - ry));
        int y1 = static_cast<int>(std::ceil(cy + ry));
        for (int y = y0; y <= y1; ++y) {
            float t = (y - cy) / ry;
            if (t * t > 1.0f) continue;
            float xr = rx * std::sqrt(1.0f - t * t);
            FillRect(cx - xr, static_cast<float>(y), cx + xr,
                     static_cast<float>(y), c);
        }
    }

    // 半橢圓 chord：bottom=true 對應 PIL chord 0→180（下半）
    void FillHalfEllipse(float cx, float cy, float rx, float ry, RGBA c,
                         bool bottom) {
        int y0 = bottom ? static_cast<int>(std::floor(cy))
                        : static_cast<int>(std::floor(cy - ry));
        int y1 = bottom ? static_cast<int>(std::ceil(cy + ry))
                        : static_cast<int>(std::ceil(cy));
        for (int y = y0; y <= y1; ++y) {
            float t = (y - cy) / ry;
            if (t * t > 1.0f) continue;
            float xr = rx * std::sqrt(1.0f - t * t);
            FillRect(cx - xr, static_cast<float>(y), cx + xr,
                     static_cast<float>(y), c);
        }
    }

    // 實心多邊形（even-odd 掃描線）
    void FillPolygon(const std::vector<std::pair<float, float>>& pts,
                     RGBA c) {
        if (pts.size() < 3) return;
        float yMin = pts[0].second, yMax = pts[0].second;
        for (const auto& p : pts) {
            yMin = std::min(yMin, p.second);
            yMax = std::max(yMax, p.second);
        }
        for (int y = static_cast<int>(std::floor(yMin));
             y <= static_cast<int>(std::ceil(yMax)); ++y) {
            std::vector<float> xs;
            for (size_t i = 0; i < pts.size(); ++i) {
                float x1 = pts[i].first, y1 = pts[i].second;
                float x2 = pts[(i + 1) % pts.size()].first,
                      y2 = pts[(i + 1) % pts.size()].second;
                float yc = y + 0.5f;
                if ((y1 <= yc && y2 > yc) || (y2 <= yc && y1 > yc)) {
                    xs.push_back(x1 + (yc - y1) / (y2 - y1) * (x2 - x1));
                }
            }
            std::sort(xs.begin(), xs.end());
            for (size_t i = 0; i + 1 < xs.size(); i += 2) {
                for (int x = static_cast<int>(std::ceil(xs[i]));
                     x <= static_cast<int>(std::floor(xs[i + 1])); ++x)
                    Set(x, y, c);
            }
        }
    }

    // 粗線（膠囊：線段距離 <= width/2）
    void DrawLine(float x1, float y1, float x2, float y2, RGBA c,
                  int width = 3) {
        float r = width * 0.5f;
        int x0 = static_cast<int>(std::floor(std::min(x1, x2) - r));
        int x3 = static_cast<int>(std::ceil(std::max(x1, x2) + r));
        int y0 = static_cast<int>(std::floor(std::min(y1, y2) - r));
        int y3 = static_cast<int>(std::ceil(std::max(y1, y2) + r));
        float dx = x2 - x1, dy = y2 - y1;
        float len2 = dx * dx + dy * dy;
        for (int y = y0; y <= y3; ++y) {
            for (int x = x0; x <= x3; ++x) {
                float t = (len2 > 0)
                    ? ((x - x1) * dx + (y - y1) * dy) / len2 : 0.0f;
                t = std::clamp(t, 0.0f, 1.0f);
                float ex = x1 + t * dx - x, ey = y1 + t * dy - y;
                if (ex * ex + ey * ey <= r * r) Set(x, y, c);
            }
        }
    }

    void StrokeEllipse(float cx, float cy, float rx, float ry, RGBA c,
                       int width = 2) {
        // 環帶：在外橢圓內且在縮小 width 的內橢圓外
        float irx = rx - width, iry = ry - width;
        for (int y = static_cast<int>(cy - ry);
             y <= static_cast<int>(cy + ry); ++y) {
            for (int x = static_cast<int>(cx - rx);
                 x <= static_cast<int>(cx + rx); ++x) {
                float dx = x - cx, dy = y - cy;
                float outer = (dx * dx) / (rx * rx) + (dy * dy) / (ry * ry);
                if (outer > 1.0f) continue;
                if (irx > 0 && iry > 0) {
                    float inner = (dx * dx) / (irx * irx) +
                                  (dy * dy) / (iry * iry);
                    if (inner <= 1.0f) continue;
                }
                Set(x, y, c);
            }
        }
    }

    // 以圓形 mask 把 src 合成進本畫布（mask 圓外不動）
    void CompositeCircleMasked(const Canvas& src, float cx, float cy,
                               float r) {
        float r2 = r * r;
        int x0 = static_cast<int>(cx - r), x1 = static_cast<int>(cx + r);
        int y0 = static_cast<int>(cy - r), y1 = static_cast<int>(cy + r);
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                float ddx = x - cx, ddy = y - cy;
                if (ddx * ddx + ddy * ddy > r2) continue;
                if (x < 0 || y < 0 || x >= w || y >= h) continue;
                size_t i = (static_cast<size_t>(y) * w + x) * 4;
                size_t si = (static_cast<size_t>(y) * src.w + x) * 4;
                px[i] = src.px[si]; px[i + 1] = src.px[si + 1];
                px[i + 2] = src.px[si + 2]; px[i + 3] = src.px[si + 3];
            }
        }
    }
};

// ============================================================
// UTF-8 → codepoint（自給自足，避免 imgui_internal 依賴）
// ============================================================

uint32_t NextCodepoint(const char*& s) {
    auto c = static_cast<unsigned char>(*s++);
    if (c < 0x80) return c;
    if ((c & 0xE0) == 0xC0) {
        uint32_t cp = c & 0x1F;
        cp = (cp << 6) | (static_cast<unsigned char>(*s++) & 0x3F);
        return cp;
    }
    if ((c & 0xF0) == 0xE0) {
        uint32_t cp = c & 0x0F;
        cp = (cp << 6) | (static_cast<unsigned char>(*s++) & 0x3F);
        cp = (cp << 6) | (static_cast<unsigned char>(*s++) & 0x3F);
        return cp;
    }
    uint32_t cp = c & 0x07;
    for (int i = 0; i < 3; ++i)
        cp = (cp << 6) | (static_cast<unsigned char>(*s++) & 0x3F);
    return cp;
}

// ============================================================
// 文字層：ImFontAtlas（stb_truetype）CPU 點陣化
// ============================================================

class TextRenderer {
public:
    // sizes：預先建立的字號集合（每個 size 一個 ImFontBaked）。
    // 1.92+ glyph 惰性載入依賴 frame 迴圈；此處無 frame，改用
    // ImFontAtlasBuildLegacyPreloadAllGlyphRanges 一次烘好全部字形。
    bool Load(const char* fontPath, std::initializer_list<float> sizes) {
        atlas_ = std::make_unique<ImFontAtlas>();
        font_ = atlas_->AddFontFromFileTTF(
            fontPath, 0.0f, nullptr,
            atlas_->GetGlyphRangesChineseFull());
        if (!font_) return false;
        ImFontAtlasBuildLegacyPreloadAllGlyphRanges(atlas_.get());
        for (float s : sizes) {
            ImFontBaked* b = font_->GetFontBaked(s);
            if (b) bakes_[s] = b;
        }
        atlas_->GetTexDataAsRGBA32(&tex_, &tw_, &th_);
        return tex_ != nullptr && !bakes_.empty();
    }
    bool Ready() const { return !bakes_.empty(); }

    float TextWidth(const std::string& utf8, float size) const {
        ImFontBaked* baked = Baked(size);
        if (!baked) return 0.0f;
        float w = 0.0f;
        const char* p = utf8.c_str();
        while (*p) {
            uint32_t cp = NextCodepoint(p);
            const ImFontGlyph* g = baked->FindGlyphNoFallback((ImWchar)cp);
            w += g ? g->AdvanceX : 0.0f;
        }
        return w;
    }

    // 水平置中繪製；y 為字形頂端
    void DrawCentered(Canvas& cv, float cx, float y, const std::string& utf8,
                      float size, RGBA color, float spacing = 0.0f) const {
        ImFontBaked* baked = Baked(size);
        if (!baked || !tex_) return;
        float w = TextWidth(utf8, size);
        int chars = 0;
        { const char* q = utf8.c_str(); while (*q) { NextCodepoint(q); ++chars; } }
        w += spacing * (chars - 1);
        float penX = cx - w * 0.5f;
        const char* p = utf8.c_str();
        while (*p) {
            uint32_t cp = NextCodepoint(p);
            const ImFontGlyph* g = baked->FindGlyphNoFallback((ImWchar)cp);
            if (!g || !g->Visible) { if (g) penX += g->AdvanceX + spacing; continue; }
            int gx0 = static_cast<int>(penX + g->X0);
            int gy0 = static_cast<int>(y + g->Y0);
            int gw = static_cast<int>(g->X1 - g->X0);
            int gh = static_cast<int>(g->Y1 - g->Y0);
            if (gw <= 0 || gh <= 0) {
                penX += g->AdvanceX + spacing;
                continue;
            }
            // oversample_h=2（≤36px 字號）時貼圖寬為版面寬兩倍：
            // 以 quad→UV 比例做最近鄰採樣，不可 1:1 texel 對映
            const float du = (g->U1 - g->U0) / static_cast<float>(gw);
            const float dv = (g->V1 - g->V0) / static_cast<float>(gh);
            for (int gy = 0; gy < gh; ++gy) {
                int tv = static_cast<int>((g->V0 + (gy + 0.5f) * dv) * th_);
                for (int gx = 0; gx < gw; ++gx) {
                    int tu = static_cast<int>((g->U0 + (gx + 0.5f) * du) * tw_);
                    if (tu < 0 || tv < 0 || tu >= tw_ || tv >= th_) continue;
                    uint8_t a = tex_[(tv * tw_ + tu) * 4 + 3];
                    if (a == 0) continue;
                    cv.Blend(gx0 + gx, gy0 + gy,
                             RGBA::C(color.r, color.g, color.b, a));
                }
            }
            penX += g->AdvanceX + spacing;
        }
    }

private:
    ImFontBaked* Baked(float size) const {
        auto it = bakes_.find(size);
        return it != bakes_.end() ? it->second : nullptr;
    }

    std::unique_ptr<ImFontAtlas> atlas_;
    ImFont* font_ = nullptr;
    std::unordered_map<float, ImFontBaked*> bakes_;
    unsigned char* tex_ = nullptr;
    int tw_ = 0, th_ = 0;
};

// ============================================================
// 立繪參數與配色（對齊 generate_roc_cards.py）
// ============================================================

const RGBA kSkin      = RGBA::C(214, 178, 148);
const RGBA kSkinDark  = RGBA::C(188, 150, 120);
const RGBA kCapColor  = RGBA::C(58, 56, 48);
const RGBA kCapBand   = RGBA::C(40, 38, 32);

RGBA UniformColor(const std::string& faction) {
    // UTF-8 字面值比對（與 Python UNIFORM map 相同）
    static const std::pair<const char*, RGBA> table[] = {
        {"國民政府", RGBA::C(96, 88, 62)},  {"中央軍", RGBA::C(96, 88, 62)},
        {"東北軍",   RGBA::C(70, 78, 62)},  {"桂系",   RGBA::C(82, 90, 70)},
        {"西北軍",   RGBA::C(76, 76, 84)},  {"晉綏軍", RGBA::C(88, 84, 76)},
        {"遠征軍",   RGBA::C(104, 96, 68)},
    };
    for (const auto& kv : table)
        if (faction == kv.first) return kv.second;
    return RGBA::C(90, 84, 66);
}

RGBA RarityColor(const std::string& rarity) {
    if (rarity == "legendary") return RGBA::C(212, 175, 55);
    return RGBA::C(148, 88, 214);   // epic / 其他
}

RGBA RarityGlow(const std::string& rarity) {
    if (rarity == "legendary") return RGBA::C(255, 223, 120);
    return RGBA::C(190, 140, 255);
}

// ============================================================
// 分層立繪（移植 draw_portrait：軍服 → 頸 → 頭 → 帽/髮 → 五官 → 鬍 → 鏡）
// ============================================================

void DrawPortrait(Canvas& img, const JsonValue& card,
                  float cx, float cy, float r) {
    const JsonValue& p = card["portrait"];
    std::string build = p["build"].AsString("normal");
    std::string head = p["head"].AsString("bald");
    std::string beard = p["beard"].AsString("none");
    bool glasses = p["glasses"].AsBool(false);

    RGBA frame = RarityColor(card["rarity"].AsString());
    RGBA uniform = UniformColor(card["faction"].AsString());
    bool burly = (build == "burly");
    bool slim = (build == "slim");

    // --- 肩/軍服 ---
    float sh_half = burly ? 118.0f : (slim ? 96.0f : 106.0f);
    float shoulder_y = cy + r * 0.62f;
    float neck_y = cy + r * 0.28f;
    img.FillPolygon({{cx - sh_half, cy + r + 10},
                     {cx - sh_half * 0.92f, shoulder_y},
                     {cx - r * 0.30f, neck_y},
                     {cx + r * 0.30f, neck_y},
                     {cx + sh_half * 0.92f, shoulder_y},
                     {cx + sh_half, cy + r + 10}}, uniform);
    // 暗部
    img.FillPolygon({{cx + r * 0.10f, neck_y + 8},
                     {cx + sh_half * 0.92f, shoulder_y},
                     {cx + sh_half, cy + r + 10},
                     {cx + r * 0.10f, cy + r + 10}}, uniform.Scale(0.78f));
    // 翻領
    float collar_y = neck_y + 6;
    img.FillPolygon({{cx - r * 0.30f, neck_y},
                     {cx - r * 0.06f, collar_y + 26},
                     {cx - r * 0.30f, collar_y + 40}}, uniform.Scale(0.55f));
    img.FillPolygon({{cx + r * 0.30f, neck_y},
                     {cx + r * 0.06f, collar_y + 26},
                     {cx + r * 0.30f, collar_y + 40}}, uniform.Scale(0.55f));
    // 領章（稀有度色）
    img.FillRect(cx - r * 0.26f, collar_y + 8, cx - r * 0.14f,
                 collar_y + 18, frame);
    img.FillRect(cx + r * 0.14f, collar_y + 8, cx + r * 0.26f,
                 collar_y + 18, frame);
    // 排扣
    for (int i = 0; i < 3; ++i)
        img.FillEllipse(cx, collar_y + 34 + i * 18 + 4, 4, 4,
                        uniform.Scale(0.5f));
    // 肩章
    for (int sx : {-1, 1}) {
        float ex = cx + sx * (sh_half * 0.62f);
        float ey = shoulder_y + 8;
        img.FillPolygon({{ex - sx * 26, ey + 16}, {ex + sx * 8, ey - 4},
                         {ex + sx * 22, ey + 4}, {ex - sx * 12, ey + 24}},
                        frame);
    }

    // --- 頸 ---
    img.FillRect(cx - r * 0.13f, cy + r * 0.16f, cx + r * 0.13f,
                 neck_y + 10, kSkinDark);

    // --- 頭 ---
    float hw = (burly ? 46.0f : (slim ? 36.0f : 41.0f)) * (r / 120.0f);
    float head_top = cy - r * 0.52f;
    float head_bot = cy + r * 0.22f;
    img.FillEllipse(cx, (head_top + head_bot) * 0.5f, hw,
                    (head_bot - head_top) * 0.5f, kSkin);
    // 下巴陰影（下半 chord）
    float chordTop = head_top + (head_bot - head_top) * 0.55f;
    img.FillHalfEllipse(cx, (chordTop + head_bot + 6) * 0.5f, hw,
                        (head_bot + 6 - chordTop) * 0.5f, kSkinDark, true);
    // 耳朵
    for (int sx : {-1, 1}) {
        float ex = cx + sx * hw;
        float ey = cy - r * 0.08f;
        img.FillEllipse(ex, ey + r * 0.07f, 6, r * 0.07f, kSkin);
    }

    // --- 帽 / 光頭 ---
    if (head == "cap") {
        float crown_top = head_top - r * 0.30f;
        // 帽冠：上半橢圓
        img.FillHalfEllipse(cx, head_top + r * 0.16f, hw + 8,
                            head_top + r * 0.16f - crown_top, kCapColor,
                            false);
        // 帽舌
        float brim_y = head_top + r * 0.10f;
        img.FillEllipse(cx, brim_y + r * 0.08f, hw + 14, r * 0.08f, kCapBand);
        // 帽徽（白日章：白圈+藍心）
        float bd = r * 0.09f;
        float badge_y = crown_top + r * 0.10f + bd;
        img.FillEllipse(cx, badge_y, bd, bd, RGBA::C(228, 226, 220));
        img.FillEllipse(cx, badge_y, bd * 0.55f, bd * 0.55f,
                        RGBA::C(70, 90, 150));
    } else {
        // 光頭：剃髮青影（上半 chord）
        float t0 = head_top - 4, t1 = cy - r * 0.05f;
        img.FillHalfEllipse(cx, (t0 + t1) * 0.5f, hw, (t1 - t0) * 0.5f,
                            kSkinDark.Scale(0.92f), false);
    }

    // --- 五官 ---
    float eye_y = cy - r * 0.10f;
    float eye_dx = hw * 0.42f;
    for (int sx : {-1, 1}) {
        float ex = cx + sx * eye_dx;
        img.DrawLine(ex - hw * 0.22f, eye_y - r * 0.075f,
                     ex + hw * 0.20f, eye_y - r * 0.09f,
                     RGBA::C(60, 46, 38), 3);
        img.DrawLine(ex - hw * 0.18f, eye_y, ex + hw * 0.16f, eye_y,
                     RGBA::C(40, 32, 26), 3);
    }
    img.DrawLine(cx, eye_y + r * 0.04f, cx - r * 0.02f, cy + r * 0.10f,
                 kSkinDark, 3);
    float mouth_y = cy + r * 0.15f;
    img.DrawLine(cx - hw * 0.20f, mouth_y, cx + hw * 0.20f, mouth_y,
                 RGBA::C(120, 80, 66), 3);

    // --- 鬍 ---
    if (beard == "mustache") {
        img.DrawLine(cx - hw * 0.30f, cy + r * 0.115f,
                     cx + hw * 0.30f, cy + r * 0.115f,
                     RGBA::C(52, 40, 32), 4);
    } else if (beard == "goatee") {
        img.DrawLine(cx - hw * 0.30f, cy + r * 0.115f,
                     cx + hw * 0.30f, cy + r * 0.115f,
                     RGBA::C(52, 40, 32), 4);
        img.FillPolygon({{cx - hw * 0.16f, cy + r * 0.18f},
                         {cx + hw * 0.16f, cy + r * 0.18f},
                         {cx, cy + r * 0.30f}}, RGBA::C(66, 52, 42));
    } else if (beard == "full") {
        float t0 = cy - r * 0.02f, t1 = head_bot + 8;
        img.FillHalfEllipse(cx, (t0 + t1) * 0.5f, hw * 0.8f, (t1 - t0) * 0.5f,
                            RGBA::C(66, 52, 42), true);
    }

    // --- 眼鏡 ---
    if (glasses) {
        float gr = hw * 0.30f;
        for (int sx : {-1, 1}) {
            float ex = cx + sx * eye_dx;
            img.StrokeEllipse(ex, eye_y, gr, gr, RGBA::C(200, 198, 190), 2);
        }
        img.DrawLine(cx - eye_dx + gr, eye_y, cx + eye_dx - gr, eye_y,
                     RGBA::C(200, 198, 190), 2);
    }
}

// ============================================================
// 完整卡面（移植 render_card；文字用 TextRenderer，無字型則略過）
// ============================================================

constexpr int kCardW = 512, kCardH = 768;

void RenderCard(Canvas& img, const JsonValue& card,
                const TextRenderer* titleFont, const TextRenderer* labelFont,
                const TextRenderer* smallFont, const TextRenderer* numFont,
                bool figureOnly) {
    std::string rarity = card["rarity"].AsString();
    RGBA frame = RarityColor(rarity);
    RGBA glow = RarityGlow(rarity);

    float pcx = kCardW / 2.0f, pcy = 236.0f, pr = 150.0f;

    if (figureOnly) {
        // 人物本體：透明底、只畫立繪（供 UI 疊圖/資料集用）
        DrawPortrait(img, card, pcx, kCardH * 0.38f, pr);
        return;
    }

    img.RadialGradient(RGBA::C(48, 44, 58), RGBA::C(14, 12, 18), 0.30f);

    // 外框三層 + 角珠
    img.StrokeRect(6, 6, kCardW - 7, kCardH - 7, glow, 2);
    img.StrokeRect(12, 12, kCardW - 13, kCardH - 13, frame, 5);
    img.StrokeRect(22, 22, kCardW - 23, kCardH - 23, frame, 1);
    for (auto [ax, ay] : {std::pair{12.0f, 12.0f},
                          {kCardW - 13.0f, 12.0f},
                          {12.0f, kCardH - 13.0f},
                          {kCardW - 13.0f, kCardH - 13.0f}}) {
        img.FillEllipse(ax, ay, 7, 7, frame);
    }

    // 稀有度標籤
    if (labelFont && labelFont->Ready()) {
        labelFont->DrawCentered(
            img, kCardW / 2.0f, 34,
            rarity == "legendary" ? "傳  奇" : "史  詩", frame, 8);
    }

    // --- 立繪（暫存畫布 + 圓形 mask）---
    {
        Canvas layer(kCardW, kCardH, RGBA::C(10, 9, 14));
        layer.FillEllipse(pcx, pcy, pr, pr, RGBA::C(30, 28, 38));
        layer.FillEllipse(pcx, pcy, pr * 0.7f, pr * 0.7f,
                          RGBA::C(40, 38, 48));
        DrawPortrait(layer, card, pcx, pcy, pr);
        img.CompositeCircleMasked(layer, pcx, pcy, pr);
    }
    img.StrokeEllipse(pcx, pcy, pr + 4, pr + 4, glow, 2);
    img.StrokeEllipse(pcx, pcy, pr, pr, frame, 4);

    // 姓名與稱號
    if (titleFont && titleFont->Ready()) {
        titleFont->DrawCentered(img, kCardW / 2.0f, 412,
                                card["name"].AsString(),
                                RGBA::C(240, 236, 228), 6);
    }
    if (labelFont && labelFont->Ready()) {
        std::string sub = "字 " + card["courtesyName"].AsString() +
                          " · " + card["epithet"].AsString();
        labelFont->DrawCentered(img, kCardW / 2.0f, 486, sub, frame);
        labelFont->DrawCentered(img, kCardW / 2.0f, 518,
                                card["faction"].AsString(),
                                RGBA::C(150, 145, 140));
    }

    // 數值條
    const JsonValue& stats = card["stats"];
    const char* keys[] = {"command", "attack", "defense", "mobility",
                          "moraleAura"};
    const char* names[] = {"指揮", "攻擊", "防禦", "機動", "威儀"};
    float y = 562;
    for (int i = 0; i < 5; ++i) {
        int v = stats[keys[i]].AsInt(0);
        if (smallFont && smallFont->Ready()) {
            smallFont->DrawCentered(
                img, 48 + smallFont->TextWidth(names[i]) * 0.5f,
                y, names[i], RGBA::C(200, 196, 190));
        }
        img.StrokeRect(116, y + 4, 116 + 340, y + 22,
                       RGBA::C(90, 86, 96), 1);
        img.FillRect(116, y + 4, 116 + 340.0f * v / 100.0f, y + 22, frame);
        if (numFont && numFont->Ready()) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%d", v);
            numFont->DrawCentered(
                img, 466 + numFont->TextWidth(buf) * 0.5f,
                y, buf, RGBA::C(230, 226, 220));
        }
        y += 30;
    }

    if (labelFont && labelFont->Ready()) {
        labelFont->DrawCentered(img, kCardW / 2.0f, kCardH - 42,
                                card["historicalBattle"].AsString(),
                                RGBA::C(170, 165, 158));
    }
}

std::string ReadTextFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void PrintUsage() {
    printf("用法: PortraitBaker <card.json> <out.png> [--figure-only]\n");
    printf("      PortraitBaker --batch <cards_dir> <out_dir> [--figure-only]\n");
    printf("  字型: C:/Windows/Fonts/msjhbd.ttc（缺檔時跳過文字層）\n");
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) { PrintUsage(); return 1; }

    bool batch = false, figureOnly = false;
    int argi = 1;
    if (std::strcmp(argv[argi], "--batch") == 0) { batch = true; ++argi; }
    if (argi + 1 >= argc) { PrintUsage(); return 1; }
    std::string inPath = argv[argi], outPath = argv[argi + 1];
    for (int i = argi + 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--figure-only") == 0) figureOnly = true;
    }

    // 字型（四種字號，對齊 Python 版 font() 用法）
    const char* kFontBold = "C:/Windows/Fonts/msjhbd.ttc";
    const char* kFontReg = "C:/Windows/Fonts/msjh.ttc";
    TextRenderer titleFont, labelFont, smallFont, numFont;
    bool fontOk = false;
    {
        std::ifstream test(kFontBold);
        fontOk = test.good();
    }
    if (fontOk) {
        titleFont.Load(kFontBold, 58);
        labelFont.Load(kFontReg, 23);
        smallFont.Load(kFontReg, 20);
        numFont.Load(kFontBold, 18);
    } else {
        printf("[警告] 找不到字型 %s，跳過文字層\n", kFontBold);
    }

    // 收集輸入
    std::vector<std::filesystem::path> cards;
    std::error_code ec;
    if (batch) {
        for (const auto& e :
             std::filesystem::directory_iterator(inPath, ec)) {
            if (e.path().extension() == ".json") cards.push_back(e.path());
        }
        std::sort(cards.begin(), cards.end());
        std::filesystem::create_directories(outPath, ec);
    } else {
        cards.push_back(inPath);
    }
    if (cards.empty()) { printf("找不到卡片 JSON\n"); return 1; }

    int fails = 0;
    for (const auto& path : cards) {
        std::string text = ReadTextFile(path.string());
        JsonValue card = JsonValue::Parse(text);
        if (card.IsNull()) {
            printf("[FAIL] JSON 解析失敗: %s\n", path.string().c_str());
            ++fails;
            continue;
        }
        Canvas img(kCardW, kCardH,
                   figureOnly ? RGBA::C(0, 0, 0, 0) : RGBA::C(14, 12, 18));
        RenderCard(img, card,
                   fontOk ? &titleFont : nullptr,
                   fontOk ? &labelFont : nullptr,
                   fontOk ? &smallFont : nullptr,
                   fontOk ? &numFont : nullptr,
                   figureOnly);

        std::string out = batch
            ? outPath + "/" + path.stem().string() +
                  (figureOnly ? "_figure.png" : ".png")
            : outPath;
        std::string err;
        if (!Potato::ImageCodec::WritePNGFile(out, kCardW, kCardH,
                                             img.px.data(), &err)) {
            printf("[FAIL] PNG 寫出失敗 %s: %s\n", out.c_str(), err.c_str());
            ++fails;
            continue;
        }
        printf("[OK] %s -> %s\n", card["name"].AsString().c_str(),
               out.c_str());
    }

    printf("\n完成: %zu 張（失敗 %d）\n", cards.size(), fails);
    return fails ? 1 : 0;
}
