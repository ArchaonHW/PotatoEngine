#include "PrimitiveMeshes.h"
#include <cmath>
#include <vector>

namespace Potato {
namespace PrimitiveMeshes {

namespace {
constexpr float kPi = 3.14159265358979323846f;

// 由法線挑一個正交切線（Frisvad,無分支）填 tangent；
// bitangent = normal × tangent——normal mapping 路徑直接用
void FillTangent(Vertex& v) {
    const Vector3& n = v.normal;
    const float a = n.z < -0.999999f ? 0.0f : 1.0f / (1.0f + n.z);
    const float b = -n.x * n.y * a;
    v.tangent =
        Vector3(1.0f - n.x * n.x * a, b, -n.x).Normalized();
    v.bitangent = n.Cross(v.tangent);
}

Mesh FromData(const std::vector<Vertex>& verts,
              const std::vector<uint32>& indices) {
    Mesh m;
    m.SetVertices(verts);
    m.SetIndices(indices);
    return m;
}

// 單面 quad（給 4 角、法線、uv 矩形）——cube/plane/quad 共用
void AppendQuad(std::vector<Vertex>& verts,
                std::vector<uint32>& indices, const Vector3& a,
                const Vector3& b, const Vector3& c,
                const Vector3& d, const Vector3& normal) {
    const uint32 base = static_cast<uint32>(verts.size());
    const Vector2 uvs[4] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    const Vector3 pts[4] = {a, b, c, d};
    for (int i = 0; i < 4; ++i) {
        Vertex v;
        v.position = pts[i];
        v.normal = normal;
        v.texCoord = uvs[i];
        FillTangent(v);
        verts.push_back(v);
    }
    indices.insert(indices.end(), {base, base + 1, base + 2,
                                   base, base + 2, base + 3});
}
} // namespace

Mesh MakeCube(float size) {
    const float h = size * 0.5f;
    std::vector<Vertex> verts;
    std::vector<uint32> indices;
    const Vector3 lo(-h, -h, -h), hi(h, h, h);
    // +Y
    AppendQuad(verts, indices,
               {lo.x, hi.y, hi.z}, {hi.x, hi.y, hi.z},
               {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
               {0, 1, 0});
    // -Y
    AppendQuad(verts, indices,
               {lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z},
               {hi.x, lo.y, hi.z}, {lo.x, lo.y, hi.z},
               {0, -1, 0});
    // +Z
    AppendQuad(verts, indices,
               {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z},
               {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z},
               {0, 0, 1});
    // -Z
    AppendQuad(verts, indices,
               {hi.x, lo.y, lo.z}, {lo.x, lo.y, lo.z},
               {lo.x, hi.y, lo.z}, {hi.x, hi.y, lo.z},
               {0, 0, -1});
    // +X
    AppendQuad(verts, indices,
               {hi.x, lo.y, hi.z}, {hi.x, lo.y, lo.z},
               {hi.x, hi.y, lo.z}, {hi.x, hi.y, hi.z},
               {1, 0, 0});
    // -X
    AppendQuad(verts, indices,
               {lo.x, lo.y, lo.z}, {lo.x, lo.y, hi.z},
               {lo.x, hi.y, hi.z}, {lo.x, hi.y, lo.z},
               {-1, 0, 0});
    return FromData(verts, indices);
}

Mesh MakeUVSphere(float radius, int sectors, int stacks) {
    std::vector<Vertex> verts;
    std::vector<uint32> indices;
    if (sectors < 3 || stacks < 2 || radius <= 0.0f) {
        return FromData(verts, indices);
    }
    for (int i = 0; i <= stacks; ++i) {
        const float phi = kPi * i / stacks; // 0(北)→π(南)
        const float sp = std::sin(phi), cp = std::cos(phi);
        for (int j = 0; j <= sectors; ++j) {
            const float theta = 2.0f * kPi * j / sectors;
            const float st = std::sin(theta), ct = std::cos(theta);
            Vertex v;
            v.normal = Vector3(sp * ct, cp, sp * st);
            v.position = v.normal * radius;
            v.texCoord = Vector2(
                static_cast<float>(j) / sectors,
                static_cast<float>(i) / stacks);
            FillTangent(v);
            verts.push_back(v);
        }
    }
    const int row = sectors + 1;
    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < sectors; ++j) {
            const uint32 a = i * row + j;
            const uint32 b = a + row;
            // 外法線 CCW
            indices.insert(indices.end(),
                           {a, a + 1, b, a + 1, b + 1, b});
        }
    }
    return FromData(verts, indices);
}

// 圓柱/圓錐共用的側面 + 蓋生成
namespace {
void AppendLateral(std::vector<Vertex>& verts,
                   std::vector<uint32>& indices, float r0, float r1,
                   float y0, float y1, int sectors) {
    // 側面法線：錐面沿母線傾斜（dr/dh 坡度的法向）
    const float dr = r1 - r0;
    for (int j = 0; j <= sectors; ++j) {
        const float theta = 2.0f * kPi * j / sectors;
        const float st = std::sin(theta), ct = std::cos(theta);
        // 側面外法線 ∝ (dh·cosθ, -dr, dh·sinθ)
        // （dp/dt × dp/dθ；圓柱 dr=0 退化為徑向）
        const float dh = y1 - y0;
        const Vector3 n =
            Vector3(ct * dh, -dr, st * dh).Normalized();
        Vertex vb, vt;
        vb.position = Vector3(r0 * ct, y0, r0 * st);
        vt.position = Vector3(r1 * ct, y1, r1 * st);
        vb.normal = vt.normal = n;
        vb.texCoord =
            Vector2(static_cast<float>(j) / sectors, 0.0f);
        vt.texCoord =
            Vector2(static_cast<float>(j) / sectors, 1.0f);
        FillTangent(vb);
        FillTangent(vt);
        const uint32 base = static_cast<uint32>(verts.size());
        verts.push_back(vb);
        verts.push_back(vt);
        if (j < sectors) {
            // 外法線 CCW：(b_j, t_j, t_j+1) + (b_j, t_j+1, b_j+1)
            indices.insert(indices.end(), {base, base + 1,
                                           base + 3, base,
                                           base + 3, base + 2});
        }
    }
}

void AppendCap(std::vector<Vertex>& verts,
               std::vector<uint32>& indices, float radius,
               float y, bool up, int sectors) {
    if (radius <= 0.0f) return;
    const float sgn = up ? 1.0f : -1.0f;
    const uint32 center = static_cast<uint32>(verts.size());
    Vertex vc;
    vc.position = Vector3(0, y, 0);
    vc.normal = Vector3(0, sgn, 0);
    vc.texCoord = Vector2(0.5f, 0.5f);
    FillTangent(vc);
    verts.push_back(vc);
    for (int j = 0; j <= sectors; ++j) {
        const float theta = 2.0f * kPi * j / sectors;
        const float st = std::sin(theta), ct = std::cos(theta);
        Vertex v;
        v.position = Vector3(radius * ct, y, radius * st);
        v.normal = Vector3(0, sgn, 0);
        v.texCoord = Vector2(0.5f + 0.5f * ct,
                             0.5f + 0.5f * st * sgn);
        FillTangent(v);
        verts.push_back(v);
        if (j < sectors) {
            const uint32 c0 = center + 1 + j;
            // 上蓋需 +Y 外法線→(center, c0+1, c0)；
            // 下蓋 -Y 反之
            if (up) {
                indices.insert(indices.end(),
                               {center, c0 + 1, c0});
            } else {
                indices.insert(indices.end(),
                               {center, c0, c0 + 1});
            }
        }
    }
}
} // namespace

Mesh MakeCylinder(float radius, float height, int sectors) {
    std::vector<Vertex> verts;
    std::vector<uint32> indices;
    if (sectors < 3 || radius <= 0.0f || height <= 0.0f) {
        return FromData(verts, indices);
    }
    const float h = height * 0.5f;
    AppendLateral(verts, indices, radius, radius, -h, h,
                  sectors);
    AppendCap(verts, indices, radius, h, true, sectors);
    AppendCap(verts, indices, radius, -h, false, sectors);
    return FromData(verts, indices);
}

Mesh MakeCone(float radius, float height, int sectors) {
    std::vector<Vertex> verts;
    std::vector<uint32> indices;
    if (sectors < 3 || radius <= 0.0f || height <= 0.0f) {
        return FromData(verts, indices);
    }
    const float h = height * 0.5f;
    AppendLateral(verts, indices, radius, 0.0f, -h, h,
                  sectors);
    AppendCap(verts, indices, radius, -h, false, sectors);
    return FromData(verts, indices);
}

Mesh MakePlane(float width, float depth, int gridX,
               int gridZ) {
    std::vector<Vertex> verts;
    std::vector<uint32> indices;
    if (width <= 0.0f || depth <= 0.0f || gridX < 1 ||
        gridZ < 1) {
        return FromData(verts, indices);
    }
    for (int i = 0; i <= gridZ; ++i) {
        for (int j = 0; j <= gridX; ++j) {
            Vertex v;
            v.position = Vector3(
                (static_cast<float>(j) / gridX - 0.5f) * width,
                0.0f,
                (static_cast<float>(i) / gridZ - 0.5f) * depth);
            v.normal = Vector3(0, 1, 0);
            v.texCoord = Vector2(
                static_cast<float>(j) / gridX,
                static_cast<float>(i) / gridZ);
            FillTangent(v);
            verts.push_back(v);
        }
    }
    const int row = gridX + 1;
    for (int i = 0; i < gridZ; ++i) {
        for (int j = 0; j < gridX; ++j) {
            const uint32 a = i * row + j;
            const uint32 b = a + row;
            indices.insert(indices.end(),
                           {a, b, a + 1, a + 1, b, b + 1});
        }
    }
    return FromData(verts, indices);
}

Mesh MakeQuad(float width, float height) {
    std::vector<Vertex> verts;
    std::vector<uint32> indices;
    const float hw = width * 0.5f, hh = height * 0.5f;
    AppendQuad(verts, indices,
               {-hw, -hh, 0}, {hw, -hh, 0},
               {hw, hh, 0}, {-hw, hh, 0},
               {0, 0, 1});
    return FromData(verts, indices);
}

Mesh MakeTorus(float majorRadius, float minorRadius,
               int majorSegments, int minorSegments) {
    std::vector<Vertex> verts;
    std::vector<uint32> indices;
    if (majorRadius <= 0.0f || minorRadius <= 0.0f ||
        majorSegments < 3 || minorSegments < 3) {
        return FromData(verts, indices);
    }
    // p(u,v)=((R+r·cosv)cosu, r·sinv, (R+r·cosv)sinu)
    // 法線=管心指向表面：(cosv·cosu, sinv, cosv·sinu)
    for (int i = 0; i <= majorSegments; ++i) {
        const float u = 2.0f * kPi * i / majorSegments;
        const float cu = std::cos(u), su = std::sin(u);
        for (int j = 0; j <= minorSegments; ++j) {
            const float v = 2.0f * kPi * j / minorSegments;
            const float cv = std::cos(v), sv = std::sin(v);
            Vertex vert;
            vert.normal =
                Vector3(cv * cu, sv, cv * su);
            vert.position =
                Vector3((majorRadius + minorRadius * cv) * cu,
                        minorRadius * sv,
                        (majorRadius + minorRadius * cv) * su);
            vert.texCoord = Vector2(
                static_cast<float>(i) / majorSegments,
                static_cast<float>(j) / minorSegments);
            FillTangent(vert);
            verts.push_back(vert);
        }
    }
    const int row = minorSegments + 1;
    for (int i = 0; i < majorSegments; ++i) {
        for (int j = 0; j < minorSegments; ++j) {
            const uint32 a = i * row + j;
            const uint32 b = a + row;
            // 外法線 CCW（同球面繞序）
            indices.insert(indices.end(),
                           {a, a + 1, b, a + 1, b + 1, b});
        }
    }
    return FromData(verts, indices);
}

Mesh MakeCapsule(float radius, float height,
                 int sectors, int hemiStacks) {
    std::vector<Vertex> verts;
    std::vector<uint32> indices;
    const float halfH = height * 0.5f;
    if (radius <= 0.0f || height < 2.0f * radius - 1e-5f ||
        sectors < 3 || hemiStacks < 2) {
        return FromData(verts, indices);
    }
    // lathe profile：南極→下帽→柱段→上帽→北極。
    // capY<FLT_MAX 表示該環屬帽（法線=帽心方向）,
    // 否則柱段（法線純徑向）
    struct Ring { float rad, y, capY; };
    const float yBot = -(halfH - radius);
    const float yTop = halfH - radius;
    std::vector<Ring> prof = {{0.0f, -halfH, yBot}};
    for (int j = 1; j <= hemiStacks; ++j) {
        const float phi = -kPi * 0.5f +
            (kPi * 0.5f) * j / hemiStacks;
        prof.push_back({radius * std::cos(phi),
                        yBot + radius * std::sin(phi), yBot});
    }
    prof.push_back({radius, yTop, 1e30f}); // 柱段環
    for (int j = 1; j <= hemiStacks; ++j) {
        const float phi = (kPi * 0.5f) * j / hemiStacks;
        prof.push_back({radius * std::cos(phi),
                        yTop + radius * std::sin(phi), yTop});
    }
    const int row = sectors + 1;
    for (const Ring& r : prof) {
        for (int j = 0; j <= sectors; ++j) {
            const float th = 2.0f * kPi * j / sectors;
            const float ct = std::cos(th), st = std::sin(th);
            Vertex v;
            v.position = Vector3(r.rad * ct, r.y, r.rad * st);
            if (r.capY < 1e29f) {
                v.normal = Vector3(r.rad * ct / radius,
                                   (r.y - r.capY) / radius,
                                   r.rad * st / radius);
            } else {
                v.normal = Vector3(ct, 0.0f, st);
            }
            v.texCoord = Vector2(
                static_cast<float>(j) / sectors,
                (r.y + halfH) / height);
            FillTangent(v);
            verts.push_back(v);
        }
    }
    const int rings = static_cast<int>(prof.size());
    for (int i = 0; i + 1 < rings; ++i) {
        for (int j = 0; j < sectors; ++j) {
            const uint32 a = i * row + j;
            const uint32 b = a + row;
            // south→north lathe：外法線 CCW（與球面反向）
            indices.insert(indices.end(),
                           {a, b, a + 1, a + 1, b, b + 1});
        }
    }
    return FromData(verts, indices);
}

} // namespace PrimitiveMeshes
} // namespace Potato
