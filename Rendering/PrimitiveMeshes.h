#pragma once

#include "Rendering/OpenGLRenderer.h" // Mesh, Vertex

namespace Potato {

/**
 * PrimitiveMeshes — 程序化 3D 基本網格生成。
 *
 * 產生帶 position/normal/uv/tangent/bitangent 的標準 Vertex
 * 資料,直接進 Mesh → 可餵 GBuffer 延遲管線與 forward pass。
 * 生成是純 CPU 的（SetVertices lazy-upload），headless 可測
 * 頂點/索引/法線/bounds 正確性。
 *
 * 所有網格以原點為中心（plane 在 XZ 面 +Y 朝向,quad 在 XY
 * 面 +Z 朝向——螢幕/UI/背景板慣例）。
 */
namespace PrimitiveMeshes {

// 立方體：邊長 size,24 頂點（每面獨立法線）36 索引
Mesh MakeCube(float size = 1.0f);

// UV 球：radius,sectors×stacks 經緯分段
// 頂點 (stacks+1)×(sectors+1),法線=位置/半徑
Mesh MakeUVSphere(float radius = 0.5f, int sectors = 24,
                  int stacks = 16);

// 圓柱：側面 + 上下蓋,軸向 Y,半徑 radius 高 height
Mesh MakeCylinder(float radius = 0.5f, float height = 1.0f,
                  int sectors = 24);

// 圓錐：側面 + 底蓋,軸向 Y,底半徑 radius 高 height
Mesh MakeCone(float radius = 0.5f, float height = 1.0f,
              int sectors = 24);

// 水平面：XZ 面 +Y 法線,w×d 居中——地板/水面常用
Mesh MakePlane(float width = 1.0f, float depth = 1.0f,
               int gridX = 1, int gridZ = 1);

// 正面 quad：XY 面 +Z 法線,w×h 居中——UI/公告板
Mesh MakeQuad(float width = 1.0f, float height = 1.0f);

// 環面：XZ 面躺平（軸向 Y）,majorRadius 環心半徑、
// minorRadius 管半徑——碰撞體/甜甜圈/裝飾
Mesh MakeTorus(float majorRadius = 0.7f,
               float minorRadius = 0.3f,
               int majorSegments = 32,
               int minorSegments = 16);

// 膠囊：軸向 Y 圓柱段 + 上下半球,總高 height（含帽）——
// 角色碰撞體/物理形狀標配
Mesh MakeCapsule(float radius = 0.5f, float height = 2.0f,
                 int sectors = 24, int hemiStacks = 8);

} // namespace PrimitiveMeshes
} // namespace Potato
