#include "Rendering/RendererCanvasCull.h"

#include <algorithm>
#include <cmath>

namespace Potato {

// ---- Canvas ----

RID RendererCanvasCull::CanvasAllocate() {
    return canvasOwner.AllocateRID();
}
void RendererCanvasCull::CanvasInitialize(RID rid) {
    canvasOwner.InitializeRID(rid, Canvas{});
}
void RendererCanvasCull::CanvasSetTransform(RID rid,
                                            const Transform2D& t) {
    if (Canvas* c = canvasOwner.GetOrNull(rid)) c->transform = t;
}
void RendererCanvasCull::CanvasSetLayer(RID rid, int layer) {
    if (Canvas* c = canvasOwner.GetOrNull(rid)) c->layer = layer;
}
void RendererCanvasCull::CanvasSetVisible(RID rid, bool v) {
    if (Canvas* c = canvasOwner.GetOrNull(rid)) c->visible = v;
}
void RendererCanvasCull::CanvasFree(RID rid) {
    if (Canvas* c = canvasOwner.GetOrNull(rid)) {
        // 掛在本 canvas 的根 item 一併釋放（孤兒不留）
        std::vector<RID> roots = c->items;
        for (RID r : roots) ItemFree(r);
    }
    canvasOwner.Free(rid);
}

// ---- CanvasItem ----

RID RendererCanvasCull::ItemAllocate() {
    return itemOwner.AllocateRID();
}
void RendererCanvasCull::ItemInitialize(RID rid) {
    CanvasItem it;
    it.sortKey = sortCounter++;
    itemOwner.InitializeRID(rid, it);
}

bool RendererCanvasCull::ItemSetParent(RID item, RID parent) {
    CanvasItem* it = itemOwner.GetOrNull(item);
    if (!it) return false;

    const bool parentIsCanvas = canvasOwner.Owns(parent);
    if (parent.IsValid() && !parentIsCanvas &&
        !itemOwner.Owns(parent)) {
        return false; // parent 不是 canvas 也不是 item
    }

    // 環檢測：parent 沿祖先鏈回走不得碰到 item 本身
    for (RID p = parent; p.IsValid() && !canvasOwner.Owns(p);) {
        if (p == item) return false;
        CanvasItem* pi = itemOwner.GetOrNull(p);
        if (!pi) break;
        p = pi->parent;
    }

    // 從舊 parent 脫鉤
    if (it->parent.IsValid()) {
        std::vector<RID>* siblings = nullptr;
        if (it->parentIsCanvas) {
            if (Canvas* c = canvasOwner.GetOrNull(it->parent))
                siblings = &c->items;
        } else if (CanvasItem* pi =
                       itemOwner.GetOrNull(it->parent)) {
            siblings = &pi->children;
        }
        if (siblings) {
            siblings->erase(
                std::remove(siblings->begin(), siblings->end(), item),
                siblings->end());
        }
    }

    it->parent = parent;
    it->parentIsCanvas = parentIsCanvas;

    if (parent.IsValid()) {
        if (parentIsCanvas) {
            canvasOwner.GetOrNull(parent)->items.push_back(item);
        } else {
            itemOwner.GetOrNull(parent)->children.push_back(item);
        }
    }
    return true;
}

void RendererCanvasCull::ItemSetTransform(RID item,
                                          const Transform2D& t) {
    if (CanvasItem* it = itemOwner.GetOrNull(item))
        it->transform = t;
}
void RendererCanvasCull::ItemSetZIndex(RID item, int z) {
    if (CanvasItem* it = itemOwner.GetOrNull(item))
        it->zIndex = z;
}
void RendererCanvasCull::ItemSetZRelative(RID item, bool rel) {
    if (CanvasItem* it = itemOwner.GetOrNull(item))
        it->zRelative = rel;
}
void RendererCanvasCull::ItemSetVisible(RID item, bool v) {
    if (CanvasItem* it = itemOwner.GetOrNull(item))
        it->visible = v;
}
void RendererCanvasCull::ItemSetModulate(RID item, const Color& c) {
    if (CanvasItem* it = itemOwner.GetOrNull(item))
        it->modulate = c;
}
void RendererCanvasCull::ItemClear(RID item) {
    if (CanvasItem* it = itemOwner.GetOrNull(item)) {
        it->commands.clear();
        it->boundsValid = false;
        it->bounds = Rect2{};
    }
}

bool RendererCanvasCull::CommandBounds(const CanvasCommand& cmd,
                                       Rect2& out) const {
    struct Visitor {
        const RendererCanvasCull* self;
        Rect2& out;
        bool ok = false;

        void operator()(const CanvasCmd::Rect& c) {
            out = c.rect; ok = true;
        }
        void operator()(const CanvasCmd::Line& c) {
            out = Rect2(c.a, Vector2(0, 0))
                      .Union(Rect2(c.b, Vector2(0, 0)))
                      .Grow(c.width * 0.5f);
            ok = true;
        }
        void operator()(const CanvasCmd::PolyLine& c) {
            if (c.points.empty()) return;
            Rect2 r(c.points[0], Vector2(0, 0));
            for (const auto& p : c.points)
                r = r.Union(Rect2(p, Vector2(0, 0)));
            out = r.Grow(c.width * 0.5f);
            ok = true;
        }
        void operator()(const CanvasCmd::Circle& c) {
            out = Rect2(c.pos.x - c.radius, c.pos.y - c.radius,
                        c.radius * 2.0f, c.radius * 2.0f);
            ok = true;
        }
        void operator()(const CanvasCmd::Polygon& c) {
            if (c.points.empty()) return;
            Rect2 r(c.points[0], Vector2(0, 0));
            for (const auto& p : c.points)
                r = r.Union(Rect2(p, Vector2(0, 0)));
            out = r;
            ok = true;
        }
        void operator()(const CanvasCmd::TextureRect& c) {
            out = c.rect; ok = true;
        }
        void operator()(const CanvasCmd::Text& c) {
            float w = 0, h = 0;
            if (self->textMeasurer &&
                self->textMeasurer(c.font, c.text, c.scale, w, h)) {
                out = Rect2(c.pos, Vector2(w, h));
                ok = true;
            }
            // 無法測量 → 無界（item 永不剔除，保守）
        }
        void operator()(const CanvasCmd::Transform&) {
            // 變換指令不貢獻 bounds
        }
    };
    Visitor v{this, out};
    std::visit(v, cmd);
    return v.ok;
}

void RendererCanvasCull::UpdateBounds(CanvasItem& item) {
    item.boundsValid = false;
    for (const auto& cmd : item.commands) {
        Rect2 b;
        if (!CommandBounds(cmd, b)) {
            item.boundsValid = false; // 含無界指令 → 永不剔除
            return;
        }
        item.bounds = item.boundsValid ? item.bounds.Union(b) : b;
        item.boundsValid = true;
    }
}

void RendererCanvasCull::ItemAddCommand(RID item, CanvasCommand cmd) {
    if (CanvasItem* it = itemOwner.GetOrNull(item)) {
        it->commands.push_back(std::move(cmd));
        UpdateBounds(*it);
    }
}

void RendererCanvasCull::ItemFree(RID rid) {
    if (CanvasItem* it = itemOwner.GetOrNull(rid)) {
        // 脫鉤 parent
        if (it->parent.IsValid()) {
            std::vector<RID>* siblings = nullptr;
            if (it->parentIsCanvas) {
                if (Canvas* c = canvasOwner.GetOrNull(it->parent))
                    siblings = &c->items;
            } else if (CanvasItem* pi =
                           itemOwner.GetOrNull(it->parent)) {
                siblings = &pi->children;
            }
            if (siblings) {
                siblings->erase(std::remove(siblings->begin(),
                                            siblings->end(), rid),
                                siblings->end());
            }
        }
        // 子樹連鎖釋放
        std::vector<RID> kids = it->children;
        for (RID k : kids) ItemFree(k);
    }
    itemOwner.Free(rid);
}

bool RendererCanvasCull::FreeAny(RID rid) {
    if (itemOwner.Owns(rid)) {
        ItemFree(rid);
        return true;
    }
    if (canvasOwner.Owns(rid)) {
        CanvasFree(rid);
        return true;
    }
    return false;
}

// ---- 剔除+排序 ----

void RendererCanvasCull::CollectItem(
    RID rid, const Transform2D& parentXform,
    const Color& parentModulate, int parentZ,
    const Rect2& viewRect, CanvasRenderList& out) {
    CanvasItem* it = itemOwner.GetOrNull(rid);
    if (!it || !it->visible) return; // 不可見 → 整棵子樹跳過

    const Transform2D global = parentXform * it->transform;
    const Color modulate = parentModulate * it->modulate;
    const int effZ = it->zRelative ? parentZ + it->zIndex
                                   : it->zIndex;

    // item 粒度剔除：本地 bounds 經全域變換後與 viewport 交集
    if (it->boundsValid &&
        !viewRect.Intersects(global.Xform(it->bounds))) {
        return;
    }

    // 展開指令：Transform 指令疊加進後續指令的 xform
    Transform2D cmdXform = Transform2D::Identity();
    for (const auto& cmd : it->commands) {
        if (const auto* tf =
                std::get_if<CanvasCmd::Transform>(&cmd)) {
            cmdXform = cmdXform * tf->xform;
            continue;
        }
        CanvasRenderItem ri;
        ri.cmd = &cmd;
        ri.xform = global * cmdXform;
        ri.modulate = modulate;
        ri.z = effZ;
        ri.item = rid;
        out.items.push_back(ri);
    }

    // 子 item 同層攤平——z 排序在最終列表做
    for (RID child : it->children) {
        CollectItem(child, global, modulate, effZ, viewRect, out);
    }
}

int RendererCanvasCull::RenderCanvases(
    const std::vector<RID>& canvases, const Rect2& viewRect,
    RasterizerCanvas& rasterizer) {
    // canvas 依 layer 排序（Godot：layer 小的先畫=底層）
    std::vector<const Canvas*> sorted;
    sorted.reserve(canvases.size());
    for (RID rid : canvases) {
        if (const Canvas* c = canvasOwner.GetOrNull(rid))
            if (c->visible) sorted.push_back(c);
    }
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const Canvas* a, const Canvas* b) {
                         return a->layer < b->layer;
                     });

    // 逐 canvas 收集 → z/sortKey 穩定排序 → 送 rasterizer
    std::vector<CanvasRenderList> lists;
    lists.reserve(sorted.size());
    for (const Canvas* c : sorted) {
        CanvasRenderList list;
        list.layer = c->layer;
        list.canvasTransform = c->transform;
        for (RID root : c->items) {
            CollectItem(root, c->transform,
                        Color(1, 1, 1, 1), 0, viewRect, list);
        }
        // 同 z 以建立序穩定排序（sortKey 存在每條 item 上——
        // 這裡展開後以蒐集順序近似：DFS 順序已含父子序，
        // stable_sort 保證同 z 維持 DFS 序）
        std::stable_sort(list.items.begin(), list.items.end(),
                         [](const CanvasRenderItem& a,
                            const CanvasRenderItem& b) {
                             return a.z < b.z;
                         });
        lists.push_back(std::move(list));
    }
    int total = 0;
    for (const auto& l : lists) total += (int)l.items.size();
    rasterizer.Render(lists, Matrix4::Orthographic(
                                 viewRect.pos.x, viewRect.Right(),
                                 viewRect.Bottom(), viewRect.pos.y,
                                 -1.0f, 1.0f));
    return total;
}

} // namespace Potato
