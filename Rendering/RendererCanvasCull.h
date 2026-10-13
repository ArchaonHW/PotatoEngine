#pragma once

/**
 * RendererCanvasCull — 2D canvas 的資源持有與剔除層。
 *
 * 對應 Godot servers/rendering/renderer_canvas_cull.*：
 * 持有 Canvas（layer）與 CanvasItem 的 RID_Alloc 池；
 * item 以 parent RID 組成樹（parent 可為 canvas 或
 * item），指令存於 item 的 command list。
 *
 * RenderCanvases 把掛在 viewport 的 canvases 剔除+排序成
 * CanvasRenderList 交給 RasterizerCanvas——cull/排序全純
 * CPU，headless 可測。
 *
 * z-order 語義（Godot 同款）：item.zIndex 預設相對父層
 * （zRelative=true → effectiveZ = 父層 + zIndex）；同 z
 * 以建立序穩定排序。visible=false 整棵子樹剔除。
 * modulate 沿樹相乘傳遞。
 */

#include "Core/CoreTypes.h"
#include "Core/RID.h"
#include "Rendering/RenderTypes.h"
#include "Rendering/RasterizerCanvas.h"
#include <functional>
#include <vector>

namespace Potato {

class RendererCanvasCull {
public:
    struct Canvas {
        Transform2D transform;
        int layer = 0;
        bool visible = true;
        std::vector<RID> items; // 直接掛本 canvas 的根 item（建立序）
    };

    struct CanvasItem {
        RID parent;             // Canvas 或 CanvasItem 的 RID
        bool parentIsCanvas = false;
        std::vector<RID> children;
        Transform2D transform;
        int zIndex = 0;
        bool zRelative = true;
        bool visible = true;
        Color modulate{1.0f, 1.0f, 1.0f, 1.0f};
        std::vector<CanvasCommand> commands;
        Rect2 bounds;           // 本地空間外接（含全部指令）
        bool boundsValid = false;
        int sortKey = 0;        // 建立序——同 z 穩定排序鍵
    };

    // ---- Canvas ----
    RID CanvasAllocate();
    void CanvasInitialize(RID rid);
    void CanvasSetTransform(RID rid, const Transform2D& t);
    void CanvasSetLayer(RID rid, int layer);
    void CanvasSetVisible(RID rid, bool v);
    Canvas* GetCanvas(RID rid) { return canvasOwner.GetOrNull(rid); }
    bool CanvasOwns(RID rid) const { return canvasOwner.Owns(rid); }
    void CanvasFree(RID rid);

    // ---- CanvasItem ----
    RID ItemAllocate();
    void ItemInitialize(RID rid);
    // parent 可為 canvas 或 item（自動判別）；含環檢測
    bool ItemSetParent(RID item, RID parent);
    void ItemSetTransform(RID item, const Transform2D& t);
    void ItemSetZIndex(RID item, int z);
    void ItemSetZRelative(RID item, bool rel);
    void ItemSetVisible(RID item, bool v);
    void ItemSetModulate(RID item, const Color& c);
    void ItemClear(RID item);
    // 加入指令並更新本地 bounds（Text 需 textMeasurer 才有界）
    void ItemAddCommand(RID item, CanvasCommand cmd);
    CanvasItem* GetItem(RID rid) { return itemOwner.GetOrNull(rid); }
    bool ItemOwns(RID rid) const { return itemOwner.Owns(rid); }
    void ItemFree(RID rid);

    // 文字測量注入（server 端把 font RID → FontAtlas 解析
    // 接上後 text 指令才有界；未接時 text item 永不剔除）
    using TextMeasurer =
        std::function<bool(RID font, const std::string& text,
                           float scale, float& w, float& h)>;
    void SetTextMeasurer(TextMeasurer m) { textMeasurer = m; }

    // ---- 剔除+排序 ----
    // 把 canvases（viewport 掛的集合）剔除成按 layer 排序的
    // render lists，丟給 rasterizer。viewRect 為螢幕像素矩形。
    // 回傳送出的繪製指令總數（統計用）。
    int RenderCanvases(const std::vector<RID>& canvases,
                       const Rect2& viewRect,
                       RasterizerCanvas& rasterizer);

    bool OwnsAny(RID rid) const {
        return canvasOwner.Owns(rid) || itemOwner.Owns(rid);
    }
    bool FreeAny(RID rid);

private:
    void CollectItem(RID rid, const Transform2D& parentXform,
                     const Color& parentModulate, int parentZ,
                     const Rect2& viewRect,
                     CanvasRenderList& out);

    // 指令本地 bounds（無法決定 → 回傳 false）
    bool CommandBounds(const CanvasCommand& cmd, Rect2& out) const;
    void UpdateBounds(CanvasItem& item);

    RID_Alloc<Canvas> canvasOwner{64, "canvas"};
    RID_Alloc<CanvasItem> itemOwner{256, "canvas_item"};
    TextMeasurer textMeasurer;
    int sortCounter = 0;
};

} // namespace Potato
