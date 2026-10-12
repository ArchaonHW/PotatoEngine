#pragma once

#include "Core/CoreTypes.h"
#include "MathUtils/Frustum.h"  // AABB
#include "MathUtils/Matrix4.h"
#include "MathUtils/Vector3.h"
#include "MathUtils/Vector4.h"
#include "Rendering/OpenGLRenderer.h" // Shader, VertexArray
#include <vector>

namespace Potato {

/**
 * PrimitiveBatcher — 立即模式線段/三角形批次繪製器。
 *
 * 引擎除錯可視化與程序化 2D/3D 繪製的基礎件：
 * CPU 累積 LineVertex{pos,color} → Flush(viewProj) 一次 draw。
 * 同一套 API 同時服務 3D（傳透視 viewProj）與 2D（傳
 * Orthographic viewProj），無光照、vertex-color 直接輸出。
 *
 * 形狀 helper（全部純 CPU 產生頂點，headless 可測）：
 *   Line/LineStrip、Triangle/Quad（填充,GL_SRC_ALPHA）、
 *   Circle（任意平面）、Box(AABB 12 邊)、WireSphere（3 大圓）、
 *   Grid（XZ 網格）、Axes（RGB=XYZ）、Rect（2D 線框）。
 *
 * Create/Flush 需 GL context；頂點累積隨時可用（lazy-upload
 * 慣例同 Mesh）。批次的典型用法是畫 bounding box / frustum /
 * 導航網格 / 物理形狀等除錯資訊。
 */
struct LineVertex {
    Vector3 position;
    Vector4 color;
};

class PrimitiveBatcher {
public:
    PrimitiveBatcher() = default;
    ~PrimitiveBatcher();
    PrimitiveBatcher(const PrimitiveBatcher&) = delete;
    PrimitiveBatcher& operator=(const PrimitiveBatcher&) = delete;
    PrimitiveBatcher(PrimitiveBatcher&&) = default;
    PrimitiveBatcher& operator=(PrimitiveBatcher&&) = default;

    bool Create();
    void Destroy();
    bool IsValid() const { return valid; }

    // ---- 頂點累積（純 CPU,context 前可用）----
    void Clear();

    void Line(const Vector3& a, const Vector3& b,
              const Vector4& color) {
        lineVerts.push_back({a, color});
        lineVerts.push_back({b, color});
    }
    // 兩端異色（例：深度漸層、速度向量）
    void Line(const Vector3& a, const Vector3& b,
              const Vector4& ca, const Vector4& cb) {
        lineVerts.push_back({a, ca});
        lineVerts.push_back({b, cb});
    }
    void LineStrip(const Vector3* pts, int count,
                   const Vector4& color);

    void Triangle(const Vector3& a, const Vector3& b,
                  const Vector3& c, const Vector4& color) {
        triVerts.push_back({a, color});
        triVerts.push_back({b, color});
        triVerts.push_back({c, color});
    }
    void Quad(const Vector3& a, const Vector3& b,
              const Vector3& c, const Vector3& d,
              const Vector4& color) {
        Triangle(a, b, c, color);
        Triangle(a, c, d, color);
    }

    // ---- 形狀 helper（皆展開成 Line/Triangle）----
    // 圓：以 normal 為法線的平面上的圓（normal 會正規化）
    void Circle(const Vector3& center, float radius,
                const Vector3& normal, const Vector4& color,
                int segments = 32);
    // AABB 12 邊線框——SceneNode/Mesh bounds 直接可視化
    void Box(const AABB& box, const Vector4& color);
    // 線框球：三個正交大圓
    void WireSphere(const Vector3& center, float radius,
                    const Vector4& color, int segments = 24);
    // XZ 平面網格：±size,每格 size/divisions
    void Grid(float size, int divisions, const Vector4& color);
    // 座標軸：X 紅 / Y 綠 / Z 藍,長度 size
    void Axes(float size);
    // 2D 矩形線框（z 由呼叫方給;配 Orthographic viewProj）
    void Rect(float x0, float y0, float x1, float y1, float z,
              const Vector4& color);
    // 3D 線框錐（視錐可視化）：8 角 + 連邊——給世界空間 8 角
    void FrustumBox(const Vector3 corners[8],
                    const Vector4& color);
    // 箭頭：軸線 + headFins 片鰭構成錐形頭——方向/速度/
    // 力向量可視化。headFrac 為頭長佔全長比例
    void Arrow(const Vector3& from, const Vector3& to,
               const Vector4& color, float headFrac = 0.25f,
               int headFins = 4);
    // 線框圓柱：頂底兩圓 + 4 母線,軸向 Y
    void WireCylinder(const Vector3& center, float radius,
                      float height, const Vector4& color,
                      int segments = 24);
    // 線框圓錐：底圓 + 4 母線到頂點,軸向 Y
    void WireCone(const Vector3& apex, float radius,
                  float height, const Vector4& color,
                  int segments = 24);
    // 填充圓（三角扇）——2D 圓盤/範圍圈/雷達掃描;
    // 走 triVerts 佇列,可配 alpha blend
    void Disc(const Vector3& center, float radius,
              const Vector3& normal, const Vector4& color,
              int segments = 32);
    // 三次 Bézier 折線化（segments 段）——曲線/軌跡/
    // 導航路徑可視化
    void Bezier(const Vector3& p0, const Vector3& c0,
                const Vector3& c1, const Vector3& p1,
                const Vector4& color, int segments = 32);

    size_t GetLineVertexCount() const { return lineVerts.size(); }
    size_t GetTriangleVertexCount() const {
        return triVerts.size();
    }

    // 上傳 + 繪製：viewProj 單一變換（無逐物件 model——
    // 頂點本身在世界/螢幕座標）。depthTest=false 用於 HUD/
    // 透視除錯。繪製完不自動 Clear——跨幀保留或呼叫方決定
    void Flush(const Matrix4& viewProj, bool depthTest = true);

    // GLSL 330 源碼（消費端自建 pipeline 可重用）
    static const char* VertexShader();
    static const char* FragmentShader();

private:
    void UploadAndDraw(const std::vector<LineVertex>& verts,
                       uint32 glMode);

    std::vector<LineVertex> lineVerts;
    std::vector<LineVertex> triVerts;

    VertexArray vao;
    UniquePtr<Shader> shader;
    bool valid = false;
};

} // namespace Potato
