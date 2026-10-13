#pragma once

/**
 * RenderTypes — RenderingServer 層共用的 POD 值型別。
 *
 * 對應 Godot 的 Color/Rect2/Transform2D——server API 的
 * 參數一律走這些 POD（而非引擎物件指標），保持可序列化、
 * headless 可用、agent 工具可直接填值。
 */

#include "Core/CoreTypes.h"
#include "MathUtils/Vector2.h"
#include <cmath>

namespace Potato {

/**
 * RGBA 顏色（float 0..1，未夾取——允許 HDR >1）。
 */
struct Color {
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;

    Color() = default;
    Color(float r_, float g_, float b_, float a_ = 1.0f)
        : r(r_), g(g_), b(b_), a(a_) {}

    Color operator*(const Color& o) const {
        return Color(r * o.r, g * o.g, b * o.b, a * o.a);
    }
    Color operator*(float s) const {
        return Color(r * s, g * s, b * s, a * s);
    }
};

/**
 * 2D 軸對齊矩形（pos = 左上,沿用引擎 2D 左上原點慣例）。
 */
struct Rect2 {
    Vector2 pos;
    Vector2 size;

    Rect2() = default;
    Rect2(float x, float y, float w, float h) : pos(x, y), size(w, h) {}
    Rect2(const Vector2& p, const Vector2& s) : pos(p), size(s) {}

    float Right() const { return pos.x + size.x; }
    float Bottom() const { return pos.y + size.y; }
    bool IsValid() const { return size.x >= 0.0f && size.y >= 0.0f; }

    bool Intersects(const Rect2& o) const {
        return pos.x < o.Right() && Right() > o.pos.x &&
               pos.y < o.Bottom() && Bottom() > o.pos.y;
    }
    bool Contains(const Vector2& p) const {
        return p.x >= pos.x && p.x < Right() &&
               p.y >= pos.y && p.y < Bottom();
    }

    // 吸收一點/一矩形的最小外接（自 GrowthBounds 語義）
    Rect2 Union(const Rect2& o) const {
        if (!IsValid()) return o;
        if (!o.IsValid()) return *this;
        const float x0 = pos.x < o.pos.x ? pos.x : o.pos.x;
        const float y0 = pos.y < o.pos.y ? pos.y : o.pos.y;
        const float x1 = Right() > o.Right() ? Right() : o.Right();
        const float y1 = Bottom() > o.Bottom() ? Bottom() : o.Bottom();
        return Rect2(x0, y0, x1 - x0, y1 - y0);
    }

    Rect2 Grow(float by) const {
        return Rect2(pos.x - by, pos.y - by,
                     size.x + by * 2.0f, size.y + by * 2.0f);
    }
};

/**
 * Transform2D — 2D 仿射變換（2x3）。
 *
 * 佈局同 Godot：columns[0]/[1] 為基向量、columns[2] 為原點。
 * 點變換：p' = columns[0]*x + columns[1]*y + columns[2]。
 * 組合 a*b 為「先 b 後 a」（b 的點先變換再進 a 座標系）。
 */
struct Transform2D {
    Vector2 columns[3]; // [xAxis, yAxis, origin]

    Transform2D() {
        columns[0] = Vector2(1.0f, 0.0f);
        columns[1] = Vector2(0.0f, 1.0f);
        columns[2] = Vector2(0.0f, 0.0f);
    }
    Transform2D(float xx, float xy, float yx, float yy,
                float ox, float oy) {
        columns[0] = Vector2(xx, xy);
        columns[1] = Vector2(yx, yy);
        columns[2] = Vector2(ox, oy);
    }

    static Transform2D Identity() { return Transform2D(); }
    static Transform2D Translation(float x, float y) {
        Transform2D t;
        t.columns[2] = Vector2(x, y);
        return t;
    }
    static Transform2D Rotation(float radians) {
        Transform2D t;
        const float c = std::cos(radians), s = std::sin(radians);
        t.columns[0] = Vector2(c, s);
        t.columns[1] = Vector2(-s, c);
        return t;
    }
    static Transform2D Scale(float sx, float sy) {
        Transform2D t;
        t.columns[0] = Vector2(sx, 0.0f);
        t.columns[1] = Vector2(0.0f, sy);
        return t;
    }
    // TRS 便捷建構
    static Transform2D TRS(float x, float y, float radians,
                           float sx = 1.0f, float sy = 1.0f) {
        const float c = std::cos(radians), s = std::sin(radians);
        Transform2D t;
        t.columns[0] = Vector2(c * sx, s * sx);
        t.columns[1] = Vector2(-s * sy, c * sy);
        t.columns[2] = Vector2(x, y);
        return t;
    }

    Vector2 Xform(const Vector2& p) const {
        return columns[0] * p.x + columns[1] * p.y + columns[2];
    }
    Vector2 XformInv(const Vector2& p) const {
        return AffineInverse().Xform(p);
    }
    // 矩形 4 角保守包圍
    Rect2 Xform(const Rect2& r) const {
        const Vector2 c[4] = {
            Xform(r.pos),
            Xform(Vector2(r.Right(), r.pos.y)),
            Xform(Vector2(r.pos.x, r.Bottom())),
            Xform(Vector2(r.Right(), r.Bottom()))};
        float x0 = c[0].x, y0 = c[0].y, x1 = c[0].x, y1 = c[0].y;
        for (int i = 1; i < 4; ++i) {
            x0 = c[i].x < x0 ? c[i].x : x0;
            y0 = c[i].y < y0 ? c[i].y : y0;
            x1 = c[i].x > x1 ? c[i].x : x1;
            y1 = c[i].y > y1 ? c[i].y : y1;
        }
        return Rect2(x0, y0, x1 - x0, y1 - y0);
    }

    Transform2D operator*(const Transform2D& o) const {
        Transform2D t;
        // (a*b) 基向量 = a 線性部 * b 基向量（不含 a 平移）
        t.columns[0] = columns[0] * o.columns[0].x +
                       columns[1] * o.columns[0].y;
        t.columns[1] = columns[0] * o.columns[1].x +
                       columns[1] * o.columns[1].y;
        t.columns[2] = Xform(o.columns[2]);
        return t;
    }

    Transform2D AffineInverse() const {
        // 2x2 線性部 [a b; c d] 的逆 + 平移回代
        const float a = columns[0].x, b = columns[1].x;
        const float c = columns[0].y, d = columns[1].y;
        const float det = a * d - b * c;
        if (std::fabs(det) < 1e-12f) return Identity();
        const float inv = 1.0f / det;
        Transform2D t;
        t.columns[0] = Vector2(d * inv, -c * inv);
        t.columns[1] = Vector2(-b * inv, a * inv);
        t.columns[2] = Vector2(
            -(t.columns[0].x * columns[2].x +
              t.columns[1].x * columns[2].y),
            -(t.columns[0].y * columns[2].x +
              t.columns[1].y * columns[2].y));
        return t;
    }

    Vector2 GetOrigin() const { return columns[2]; }
    float GetRotation() const {
        return std::atan2(columns[0].y, columns[0].x);
    }
};

} // namespace Potato
