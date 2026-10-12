#include "PrimitiveBatcher.h"
#include "Logging/Logger.h"
#include <glad/glad.h>
#include <cmath>

namespace Potato {

namespace {
// normal 的正交基（Frisvad 方法,無分支）——Circle 用
void OrthoBasis(const Vector3& n, Vector3& outU, Vector3& outV) {
    const float a = n.z < -0.999999f ? 0.0f : 1.0f / (1.0f + n.z);
    const float b = -n.x * n.y * a;
    outU = Vector3(1.0f - n.x * n.x * a, b, -n.x).Normalized();
    outV = Vector3(b, 1.0f - n.y * n.y * a, -n.y).Normalized();
}
} // namespace

PrimitiveBatcher::~PrimitiveBatcher() {
    Destroy();
}

bool PrimitiveBatcher::Create() {
    Destroy();
    shader = MakeUnique<Shader>();
    if (!shader->LoadFromSource(VertexShader(), FragmentShader())) {
        LOG_ERROR("PrimitiveBatcher shader 編譯失敗");
        return false;
    }
    // 佈局一次到位：後續 AddVertexBuffer 只換資料,
    // attribute 指標綁同一 VBO 持續有效
    LineVertex dummy{};
    vao.AddVertexBuffer(&dummy, sizeof(dummy), GL_DYNAMIC_DRAW);
    vao.SetVertexAttribute(0, 3, sizeof(LineVertex),
                           offsetof(LineVertex, position));
    vao.SetVertexAttribute(1, 4, sizeof(LineVertex),
                           offsetof(LineVertex, color));
    valid = true;
    return true;
}

void PrimitiveBatcher::Destroy() {
    vao = VertexArray();
    shader.reset();
    valid = false;
}

void PrimitiveBatcher::Clear() {
    lineVerts.clear();
    triVerts.clear();
}

void PrimitiveBatcher::LineStrip(const Vector3* pts, int count,
                                 const Vector4& color) {
    if (!pts || count < 2) return;
    for (int i = 0; i + 1 < count; ++i) {
        Line(pts[i], pts[i + 1], color);
    }
}

void PrimitiveBatcher::Circle(const Vector3& center, float radius,
                              const Vector3& normal,
                              const Vector4& color, int segments) {
    if (radius <= 0.0f || segments < 3) return;
    Vector3 u, v;
    OrthoBasis(normal.Normalized(), u, v);
    Vector3 prev;
    for (int i = 0; i <= segments; ++i) {
        const float a = static_cast<float>(i) / segments * 6.2831853f;
        const Vector3 p = center +
            u * (radius * std::cos(a)) + v * (radius * std::sin(a));
        if (i > 0) Line(prev, p, color);
        prev = p;
    }
}

void PrimitiveBatcher::Box(const AABB& box, const Vector4& color) {
    if (!box.IsValid()) return;
    const Vector3& lo = box.min;
    const Vector3& hi = box.max;
    const Vector3 c[8] = {
        {lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z},
        {lo.x, hi.y, lo.z}, {hi.x, hi.y, lo.z},
        {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z},
        {lo.x, hi.y, hi.z}, {hi.x, hi.y, hi.z},
    };
    // 底面 4 邊 + 頂面 4 邊 + 4 立柱
    Line(c[0], c[1], color); Line(c[1], c[3], color);
    Line(c[3], c[2], color); Line(c[2], c[0], color);
    Line(c[4], c[5], color); Line(c[5], c[7], color);
    Line(c[7], c[6], color); Line(c[6], c[4], color);
    Line(c[0], c[4], color); Line(c[1], c[5], color);
    Line(c[2], c[6], color); Line(c[3], c[7], color);
}

void PrimitiveBatcher::WireSphere(const Vector3& center,
                                  float radius,
                                  const Vector4& color,
                                  int segments) {
    Circle(center, radius, Vector3(1, 0, 0), color, segments);
    Circle(center, radius, Vector3(0, 1, 0), color, segments);
    Circle(center, radius, Vector3(0, 0, 1), color, segments);
}

void PrimitiveBatcher::Grid(float size, int divisions,
                            const Vector4& color) {
    if (size <= 0.0f || divisions < 1) return;
    const float step = size / divisions;
    for (int i = -divisions; i <= divisions; ++i) {
        const float t = i * step;
        Line(Vector3(t, 0, -size), Vector3(t, 0, size), color);
        Line(Vector3(-size, 0, t), Vector3(size, 0, t), color);
    }
}

void PrimitiveBatcher::Axes(float size) {
    Line(Vector3(0, 0, 0), Vector3(size, 0, 0),
         Vector4(1, 0, 0, 1));
    Line(Vector3(0, 0, 0), Vector3(0, size, 0),
         Vector4(0, 1, 0, 1));
    Line(Vector3(0, 0, 0), Vector3(0, 0, size),
         Vector4(0, 0, 1, 1));
}

void PrimitiveBatcher::Rect(float x0, float y0, float x1, float y1,
                            float z, const Vector4& color) {
    Line(Vector3(x0, y0, z), Vector3(x1, y0, z), color);
    Line(Vector3(x1, y0, z), Vector3(x1, y1, z), color);
    Line(Vector3(x1, y1, z), Vector3(x0, y1, z), color);
    Line(Vector3(x0, y1, z), Vector3(x0, y0, z), color);
}

void PrimitiveBatcher::FrustumBox(const Vector3 corners[8],
                                  const Vector4& color) {
    if (!corners) return;
    // 近平面 0-3、遠平面 4-7（索引慣例同
    //   CascadedShadowMap::FrustumSliceCorners）
    for (int i = 0; i < 4; ++i) {
        Line(corners[i], corners[(i + 1) % 4], color);
        Line(corners[4 + i], corners[4 + (i + 1) % 4], color);
        Line(corners[i], corners[4 + i], color);
    }
}

void PrimitiveBatcher::Arrow(const Vector3& from,
                             const Vector3& to,
                             const Vector4& color,
                             float headFrac, int headFins) {
    const Vector3 d = to - from;
    const float len = d.Length();
    if (len < 1e-6f || headFins < 2) return;
    const Vector3 dir = d / len;
    const float headLen = len * headFrac;
    const Vector3 base = to - dir * headLen;
    Vector3 u, v;
    OrthoBasis(dir, u, v);
    Line(from, to, color);
    // 鰭：尖端→錐底邊緣（半徑取頭長 0.4,視覺比例）
    const float r = headLen * 0.4f;
    for (int i = 0; i < headFins; ++i) {
        const float a = static_cast<float>(i) / headFins *
                        6.2831853f;
        const Vector3 rim = base +
            u * (r * std::cos(a)) + v * (r * std::sin(a));
        Line(to, rim, color);
    }
}

void PrimitiveBatcher::WireCylinder(const Vector3& center,
                                    float radius, float height,
                                    const Vector4& color,
                                    int segments) {
    const float h = height * 0.5f;
    Circle(center + Vector3(0, h, 0), radius,
           Vector3(0, 1, 0), color, segments);
    Circle(center - Vector3(0, h, 0), radius,
           Vector3(0, -1, 0), color, segments);
    // 4 母線
    for (int i = 0; i < 4; ++i) {
        const float a = static_cast<float>(i) * 1.5707963f;
        const Vector3 off(radius * std::cos(a), 0,
                          radius * std::sin(a));
        Line(center + off - Vector3(0, h, 0),
             center + off + Vector3(0, h, 0), color);
    }
}

void PrimitiveBatcher::WireCone(const Vector3& apex,
                                float radius, float height,
                                const Vector4& color,
                                int segments) {
    const Vector3 base(apex.x, apex.y - height, apex.z);
    Circle(base, radius, Vector3(0, -1, 0), color, segments);
    for (int i = 0; i < 4; ++i) {
        const float a = static_cast<float>(i) * 1.5707963f;
        const Vector3 off(radius * std::cos(a), 0,
                          radius * std::sin(a));
        Line(apex, base + off, color);
    }
}

void PrimitiveBatcher::Disc(const Vector3& center,
                            float radius, const Vector3& normal,
                            const Vector4& color,
                            int segments) {
    if (radius <= 0.0f || segments < 3) return;
    Vector3 u, v;
    OrthoBasis(normal.Normalized(), u, v);
    for (int i = 0; i < segments; ++i) {
        const float a0 = static_cast<float>(i) / segments *
                         6.2831853f;
        const float a1 = static_cast<float>(i + 1) / segments *
                         6.2831853f;
        const Vector3 p0 = center +
            u * (radius * std::cos(a0)) +
            v * (radius * std::sin(a0));
        const Vector3 p1 = center +
            u * (radius * std::cos(a1)) +
            v * (radius * std::sin(a1));
        Triangle(center, p0, p1, color);
    }
}

void PrimitiveBatcher::Bezier(const Vector3& p0,
                              const Vector3& c0,
                              const Vector3& c1,
                              const Vector3& p1,
                              const Vector4& color,
                              int segments) {
    if (segments < 1) return;
    Vector3 prev = p0;
    for (int i = 1; i <= segments; ++i) {
        const float t = static_cast<float>(i) / segments;
        const float u = 1.0f - t;
        // (1-t)³P0 + 3(1-t)²t C0 + 3(1-t)t² C1 + t³ P1
        const Vector3 p =
            p0 * (u * u * u) + c0 * (3.0f * u * u * t) +
            c1 * (3.0f * u * t * t) + p1 * (t * t * t);
        Line(prev, p, color);
        prev = p;
    }
}

void PrimitiveBatcher::UploadAndDraw(
    const std::vector<LineVertex>& verts, uint32 glMode) {
    if (verts.empty()) return;
    vao.AddVertexBuffer(verts.data(),
                        verts.size() * sizeof(LineVertex),
                        GL_DYNAMIC_DRAW);
    vao.Bind();
    glDrawArrays(glMode, 0, static_cast<int>(verts.size()));
    vao.Unbind();
}

void PrimitiveBatcher::Flush(const Matrix4& viewProj, bool depthTest) {
    if (!valid || (lineVerts.empty() && triVerts.empty())) return;
    const GLboolean depthWas = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blendWas = glIsEnabled(GL_BLEND);
    if (depthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    shader->Bind();
    shader->SetUniformMat4("viewProj", viewProj);
    UploadAndDraw(lineVerts, GL_LINES);
    UploadAndDraw(triVerts, GL_TRIANGLES);
    shader->Unbind();

    if (!blendWas) glDisable(GL_BLEND);
    if (!depthWas) glDisable(GL_DEPTH_TEST);
}

const char* PrimitiveBatcher::VertexShader() {
    return R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;
uniform mat4 viewProj;
out vec4 vColor;
void main()
{
    vColor = aColor;
    gl_Position = viewProj * vec4(aPos, 1.0);
}
)";
}

const char* PrimitiveBatcher::FragmentShader() {
    return R"(
#version 330 core
in vec4 vColor;
out vec4 FragColor;
void main()
{
    FragColor = vColor;
}
)";
}

} // namespace Potato
