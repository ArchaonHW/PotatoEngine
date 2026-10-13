#pragma once

/**
 * RasterizerCanvas — 2D canvas 光柵化介面 + Dummy 後端。
 *
 * 對應 Godot 的 renderer_canvas_render.h / dummy/
 * rasterizer_canvas_dummy.h：RendererCanvasCull 負責
 * item 樹、剔除與 z-order，產出平坦的 CanvasRenderItem
 * 列表；RasterizerCanvas 只管「拿到列表畫出來」——
 * GL 後端（RasterizerCanvasGL）與 headless Dummy 可互換。
 *
 * 指令集（CanvasCommand variant）對應 Godot 的
 * Item::Command* 子類：Rect/Line/PolyLine/Circle/Polygon/
 * TextureRect/Text/Transform——全部為 POD，可序列化。
 */

#include "Core/CoreTypes.h"
#include "Core/RID.h"
#include "MathUtils/Vector2.h"
#include "MathUtils/Matrix4.h"
#include "Rendering/RenderTypes.h"
#include <string>
#include <variant>
#include <vector>

namespace Potato {

// ---- Canvas 繪圖指令（全 POD，無指標）----
namespace CanvasCmd {

// 實心矩形（Godot canvas_item_add_rect）
struct Rect {
    Rect2 rect;
    Color color;
};

// 單線段（width>1 時後端以 quad 展開）
struct Line {
    Vector2 a, b;
    Color color;
    float width = 1.0f;
};

// 折線/閉合線框
struct PolyLine {
    std::vector<Vector2> points;
    Color color;
    float width = 1.0f;
    bool closed = false;
};

// 圓（filled=實心扇形；否則線框）
struct Circle {
    Vector2 pos;
    float radius = 0.0f;
    Color color;
    bool filled = true;
    int segments = 32;
};

// 凸多邊形實心填充（triangle fan）
struct Polygon {
    std::vector<Vector2> points;
    Color color;
};

// 紋理矩形（SpriteBatcher 路徑；uv 為正規化子區域）
struct TextureRect {
    Rect2 rect;
    RID texture;
    Rect2 uv{Vector2(0.0f, 0.0f), Vector2(1.0f, 1.0f)};
    Color modulate;
};

// 位圖文字（font RID → FontAtlas；pos 為基線左端）
struct Text {
    Vector2 pos;
    std::string text;
    RID font;
    float scale = 1.0f;
    Color color;
};

// 疊加變換：影響同 item 內之後的所有指令
// （Godot Item::CommandTransform）
struct Transform {
    Transform2D xform;
};

} // namespace CanvasCmd

using CanvasCommand = std::variant<
    CanvasCmd::Rect,
    CanvasCmd::Line,
    CanvasCmd::PolyLine,
    CanvasCmd::Circle,
    CanvasCmd::Polygon,
    CanvasCmd::TextureRect,
    CanvasCmd::Text,
    CanvasCmd::Transform>;

/**
 * 剔除+排序後的平坦繪製單元——rasterizer 唯一需要理解的格式。
 * xform 已含 canvas→item→command 的全鏈路變換；
 * modulate 已含父鏈乘積。
 */
struct CanvasRenderItem {
    const CanvasCommand* cmd = nullptr; // 指向 culler 持有的儲存，渲染期有效
    Transform2D xform;
    Color modulate;
    int z = 0;
    RID item;
};

// 單一 canvas layer 的一幀輸出
struct CanvasRenderList {
    int layer = 0;
    Transform2D canvasTransform;
    std::vector<CanvasRenderItem> items;
};

/**
 * RasterizerCanvas — 2D 光柵化後端介面。
 * Render 把一堆 canvas layer 依序畫出（lists 已按 layer 排序）。
 * viewProj：呼叫端給 Orthographic(0,w,h,0,-1,1)。
 */
class RasterizerCanvas {
public:
    virtual ~RasterizerCanvas() = default;
    virtual void Render(const std::vector<CanvasRenderList>& lists,
                        const Matrix4& viewProj) = 0;
};

/**
 * RasterizerCanvasDummy — headless 後端。
 * 不產生像素，只記錄收到的指令（測試/agent 模擬執行用）。
 */
class RasterizerCanvasDummy : public RasterizerCanvas {
public:
    void Render(const std::vector<CanvasRenderList>& lists,
                const Matrix4& /*viewProj*/) override {
        lastLists = lists;
        frameCount++;
    }

    const std::vector<CanvasRenderList>& GetLastFrame() const {
        return lastLists;
    }
    size_t GetLastItemCount() const {
        size_t n = 0;
        for (const auto& l : lastLists) n += l.items.size();
        return n;
    }
    uint64 GetFrameCount() const { return frameCount; }

private:
    std::vector<CanvasRenderList> lastLists; // 複製：cmd 指標保留原址語義
    uint64 frameCount = 0;
};

} // namespace Potato
