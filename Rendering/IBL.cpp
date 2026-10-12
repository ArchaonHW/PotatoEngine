#include "Rendering/IBL.h"

#include <glad/glad.h>
#include <algorithm>
#include <cmath>

namespace Potato {
namespace IBL {

namespace {
    constexpr float kPI = 3.14159265358979f;
}

float RadicalInverse(uint32 bits) {
    // Van der Corput：位元反轉 → [0,1)
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return static_cast<float>(bits) * 2.3283064365386963e-10f; // /2^32
}

Vector2 Hammersley(uint32 i, uint32 n) {
    return {static_cast<float>(i) / static_cast<float>(n),
            RadicalInverse(i)};
}

Vector3 ImportanceSampleGGX(const Vector2& xi, const Vector3& n,
                            float roughness) {
    const float a = roughness * roughness;
    const float phi = 2.f * kPI * xi.x;
    const float cosTheta =
        std::sqrt((1.f - xi.y) / (1.f + (a * a - 1.f) * xi.y));
    const float sinTheta = std::sqrt(std::max(0.f, 1.f - cosTheta * cosTheta));

    // tangent-space H
    const Vector3 h{sinTheta * std::cos(phi), sinTheta * std::sin(phi),
                    cosTheta};
    // 正交基（|z| 大時換軸防退化）
    const Vector3 up = std::fabs(n.z) < 0.999f ? Vector3{0, 0, 1}
                                               : Vector3{1, 0, 0};
    const Vector3 t = up.Cross(n).Normalized();
    const Vector3 b = n.Cross(t);
    return (t * h.x + b * h.y + n * h.z).Normalized();
}

float GeometrySchlickGGX_IBL(float ndotx, float roughness) {
    const float k = (roughness + 1.f) * (roughness + 1.f) / 8.f;
    return ndotx / (ndotx * (1.f - k) + k);
}

float GeometrySmithIBL(float ndotv, float ndotl, float roughness) {
    return GeometrySchlickGGX_IBL(ndotv, roughness) *
           GeometrySchlickGGX_IBL(ndotl, roughness);
}

Vector2 IntegrateBRDF(float ndotv, float roughness, int samples) {
    ndotv = std::max(ndotv, 1e-4f);
    const Vector3 v{std::sqrt(1.f - ndotv * ndotv), 0.f, ndotv};
    const Vector3 n{0.f, 0.f, 1.f};
    float a = 0.f, b = 0.f;
    for (int i = 0; i < samples; ++i) {
        const Vector2 xi = Hammersley(static_cast<uint32>(i),
                                      static_cast<uint32>(samples));
        const Vector3 h = ImportanceSampleGGX(xi, n, roughness);
        const Vector3 l = h * (2.f * v.Dot(h)) - v; // reflect(-v,h)
        const float ndotl = std::max(l.z, 0.f);
        if (ndotl <= 0.f) continue;
        const float ndoth = std::max(h.z, 0.f);
        const float vdoth = std::max(v.Dot(h), 0.f);
        const float g = GeometrySmithIBL(ndotv, ndotl, roughness);
        const float gVis = g * vdoth / (ndoth * ndotv);
        const float fc = std::pow(1.f - vdoth, 5.f);
        a += (1.f - fc) * gVis;
        b += fc * gVis;
    }
    return {a / samples, b / samples};
}

std::vector<float> BuildBRDFLUT(int size, int samples) {
    std::vector<float> out(static_cast<size_t>(size) * size * 2);
    for (int y = 0; y < size; ++y) {
        const float rough = static_cast<float>(y) / (size - 1);
        for (int x = 0; x < size; ++x) {
            const float ndotv =
                (static_cast<float>(x) + 0.5f) / size; // 取像素中心
            const Vector2 sb = IntegrateBRDF(ndotv, rough, samples);
            out[(static_cast<size_t>(y) * size + x) * 2 + 0] = sb.x;
            out[(static_cast<size_t>(y) * size + x) * 2 + 1] = sb.y;
        }
    }
    return out;
}

Vector3 CubeFaceDir(int face, float u, float v) {
    // GL cubemap texel 映射慣例（u,v∈[-1,1]，v=+1 為 face 頂端）
    switch (face) {
    case 0: return {1.f, -v, -u};   // +X
    case 1: return {-1.f, -v, u};   // -X
    case 2: return {u, 1.f, v};     // +Y
    case 3: return {u, -1.f, -v};   // -Y
    case 4: return {u, -v, 1.f};    // +Z
    default: return {-u, -v, -1.f}; // -Z
    }
}

Vector3 SampleEquirect(const float* rgba, int w, int h,
                       const Vector3& dir) {
    const Vector3 d = dir.Normalized();
    const float u = std::atan2(d.z, d.x) / (2.f * kPI) + 0.5f;
    const float v = 0.5f - std::asin(std::clamp(d.y, -1.f, 1.f)) / kPI;
    // 雙線性（u 環繞、v 夾取）
    const float fx = u * w - 0.5f;
    const float fy = v * h - 0.5f;
    const int x0 = static_cast<int>(std::floor(fx));
    const int y0 = static_cast<int>(std::floor(fy));
    const float tx = fx - x0, ty = fy - y0;
    auto px = [&](int x, int y) -> const float* {
        x = (x % w + w) % w;                       // u 環繞
        y = std::clamp(y, 0, h - 1);               // v 夾取
        return rgba + (static_cast<size_t>(y) * w + x) * 4;
    };
    const float* p00 = px(x0, y0);
    const float* p10 = px(x0 + 1, y0);
    const float* p01 = px(x0, y0 + 1);
    const float* p11 = px(x0 + 1, y0 + 1);
    Vector3 out;
    for (int c = 0; c < 3; ++c) {
        const float a = p00[c] * (1 - tx) + p10[c] * tx;
        const float b = p01[c] * (1 - tx) + p11[c] * tx;
        (&out.x)[c] = a * (1 - ty) + b * ty;
    }
    return out;
}

std::vector<float> EquirectToCubeFace(const float* rgba, int w, int h,
                                      int face, int size) {
    std::vector<float> out(static_cast<size_t>(size) * size * 4);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const float u = 2.f * (x + 0.5f) / size - 1.f;
            const float v = 1.f - 2.f * (y + 0.5f) / size; // 頂端 v=+1
            const Vector3 c =
                SampleEquirect(rgba, w, h, CubeFaceDir(face, u, v));
            float* p = out.data() + (static_cast<size_t>(y) * size + x) * 4;
            p[0] = c.x; p[1] = c.y; p[2] = c.z; p[3] = 1.f;
        }
    return out;
}

std::vector<float> ConvolveIrradianceFace(const float* rgba, int w, int h,
                                          int face, int size,
                                          int thetaSteps, int phiSteps) {
    std::vector<float> out(static_cast<size_t>(size) * size * 4);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const float u = 2.f * (x + 0.5f) / size - 1.f;
            const float v = 1.f - 2.f * (y + 0.5f) / size;
            const Vector3 n = CubeFaceDir(face, u, v).Normalized();
            const Vector3 up = std::fabs(n.z) < 0.999f ? Vector3{0, 0, 1}
                                                       : Vector3{1, 0, 0};
            const Vector3 t = up.Cross(n).Normalized();
            const Vector3 b = n.Cross(t);
            Vector3 sum{0, 0, 0};
            float wsum = 0.f;
            for (int ti = 0; ti < thetaSteps; ++ti) {
                const float theta =
                    (ti + 0.5f) / thetaSteps * (kPI * 0.5f); // 0..π/2
                const float wt = std::cos(theta) * std::sin(theta);
                for (int pi = 0; pi < phiSteps; ++pi) {
                    const float phi =
                        (pi + 0.5f) / phiSteps * (2.f * kPI);
                    const Vector3 dir =
                        (t * (std::cos(phi) * std::sin(theta)) +
                         b * (std::sin(phi) * std::sin(theta)) +
                         n * std::cos(theta))
                            .Normalized();
                    sum += SampleEquirect(rgba, w, h, dir) * wt;
                    wsum += wt;
                }
            }
            const Vector3 irr = sum * (1.f / wsum);
            float* p = out.data() + (static_cast<size_t>(y) * size + x) * 4;
            p[0] = irr.x; p[1] = irr.y; p[2] = irr.z; p[3] = 1.f;
        }
    return out;
}

std::vector<float> PrefilterFace(const float* rgba, int w, int h,
                                 int face, int size, float roughness,
                                 int samples) {
    std::vector<float> out(static_cast<size_t>(size) * size * 4);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const float u = 2.f * (x + 0.5f) / size - 1.f;
            const float v = 1.f - 2.f * (y + 0.5f) / size;
            const Vector3 n = CubeFaceDir(face, u, v).Normalized();
            // split-sum 近似：N = V = R
            Vector3 sum{0, 0, 0};
            float wsum = 0.f;
            for (int i = 0; i < samples; ++i) {
                const Vector2 xi = Hammersley(static_cast<uint32>(i),
                                              static_cast<uint32>(samples));
                const Vector3 hv =
                    ImportanceSampleGGX(xi, n, roughness);
                const Vector3 l = hv * (2.f * n.Dot(hv)) - n;
                const float ndotl = std::max(n.Dot(l), 0.f);
                if (ndotl <= 0.f) continue;
                sum += SampleEquirect(rgba, w, h, l) * ndotl;
                wsum += ndotl;
            }
            const Vector3 col =
                wsum > 0.f ? sum * (1.f / wsum) : Vector3{0, 0, 0};
            float* p = out.data() + (static_cast<size_t>(y) * size + x) * 4;
            p[0] = col.x; p[1] = col.y; p[2] = col.z; p[3] = 1.f;
        }
    return out;
}

std::vector<float> ProceduralSkyEquirect(int w, int h,
                                         const Vector3& sunDir,
                                         float sunIntensity) {
    const Vector3 sun = sunDir.Normalized();
    std::vector<float> out(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        const float v = (static_cast<float>(y) + 0.5f) / h; // 0 頂
        const float lat = kPI * (0.5f - v);                 // +π/2 北極
        const float sy = std::sin(lat);
        const float sr = std::cos(lat);
        for (int x = 0; x < w; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / w;
            const float lon = (u - 0.5f) * 2.f * kPI;
            const Vector3 dir{std::cos(lon) * sr, sy,
                              std::sin(lon) * sr};
            // 地平線暖灰 → 天頂藍（y=0 附近再壓暗作地面側）
            const float t = std::clamp(dir.y * 0.5f + 0.5f, 0.f, 1.f);
            Vector3 col{
                0.45f * (1 - t) + 0.15f * t,   // R
                0.42f * (1 - t) + 0.30f * t,   // G
                0.38f * (1 - t) + 0.60f * t    // B
            };
            if (dir.y < 0.f) // 地平線下：深地色
                col = {0.12f, 0.10f, 0.08f};
            // 太陽盤：角距 <cosThreshold 全強度，外圈暈
            const float cosSun = dir.Dot(sun);
            if (cosSun > 0.999f) {
                col = {sunIntensity, sunIntensity * 0.95f,
                       sunIntensity * 0.85f};
            } else if (cosSun > 0.98f) { // 暈圈
                const float glow = (cosSun - 0.98f) / (0.999f - 0.98f);
                col += Vector3{1.5f, 1.2f, 0.8f} * glow;
            }
            float* p = out.data() + (static_cast<size_t>(y) * w + x) * 4;
            p[0] = col.x; p[1] = col.y; p[2] = col.z; p[3] = 1.f;
        }
    }
    return out;
}

} // namespace IBL

// ============================================================================
// EnvironmentMaps — GL 持有端（上傳 CPU 產物）
// ============================================================================

EnvironmentMaps::~EnvironmentMaps() { Destroy(); }

void EnvironmentMaps::MoveFrom(EnvironmentMaps& o) noexcept {
    envCube = o.envCube; irrCube = o.irrCube;
    preCube = o.preCube; brdfLUT = o.brdfLUT;
    cubeSize = o.cubeSize;
    o.envCube = o.irrCube = o.preCube = o.brdfLUT = 0;
    o.cubeSize = 0;
}

bool EnvironmentMaps::Create(int size) {
    Destroy();
    cubeSize = size;
    glGenTextures(1, &envCube);
    glGenTextures(1, &irrCube);
    glGenTextures(1, &preCube);
    glGenTextures(1, &brdfLUT);
    if (!envCube || !irrCube || !preCube || !brdfLUT) {
        Destroy();
        return false;
    }
    // 佔位分配；Generate 才填資料
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCube);
    for (int f = 0; f < 6; ++f)
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, GL_RGBA16F,
                     size, size, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_CUBE_MAP, irrCube);
    for (int f = 0; f < 6; ++f)
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, GL_RGBA16F,
                     32, 32, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_CUBE_MAP, preCube);
    int sz = 64;
    for (int mip = 0; mip < kMaxPrefilterMips; ++mip, sz >>= 1)
        for (int f = 0; f < 6; ++f)
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, mip,
                         GL_RGBA16F, sz, sz, 0, GL_RGBA, GL_FLOAT,
                         nullptr);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER,
                    GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL,
                    kMaxPrefilterMips - 1);

    glBindTexture(GL_TEXTURE_2D, brdfLUT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void EnvironmentMaps::Destroy() {
    uint32 ids[4] = {envCube, irrCube, preCube, brdfLUT};
    glDeleteTextures(4, ids);
    envCube = irrCube = preCube = brdfLUT = 0;
    cubeSize = 0;
}

void EnvironmentMaps::UploadCubeFace(uint32 tex, int face, int size,
                                     const float* rgba) {
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex);
    glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, 0, 0,
                    size, size, GL_RGBA, GL_FLOAT, rgba);
}

void EnvironmentMaps::UploadCubeFaceMip(uint32 tex, int face, int size,
                                        int level, const float* rgba) {
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex);
    glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, level, 0, 0,
                    size, size, GL_RGBA, GL_FLOAT, rgba);
}

bool EnvironmentMaps::Generate(const float* eq, int w, int h,
                               int lutSize, int lutSamples) {
    if (!envCube || !eq || w <= 0 || h <= 0)
        return false;

    // 1. env cubemap（skybox 可直接採樣）
    for (int f = 0; f < 6; ++f) {
        const auto face = IBL::EquirectToCubeFace(eq, w, h, f, cubeSize);
        UploadCubeFace(envCube, f, cubeSize, face.data());
    }

    // 2. irradiance（32² 半球卷積）
    for (int f = 0; f < 6; ++f) {
        const auto face = IBL::ConvolveIrradianceFace(eq, w, h, f, 32);
        UploadCubeFace(irrCube, f, 32, face.data());
    }

    // 3. prefilter（64² base，roughness = mip/(mips-1)）
    int sz = 64;
    for (int mip = 0; mip < kMaxPrefilterMips; ++mip, sz >>= 1) {
        const float rough =
            static_cast<float>(mip) / (kMaxPrefilterMips - 1);
        for (int f = 0; f < 6; ++f) {
            const auto face =
                IBL::PrefilterFace(eq, w, h, f, sz, rough);
            UploadCubeFaceMip(preCube, f, sz, mip, face.data());
        }
    }

    // 4. BRDF LUT（CPU 積分 → RG32F）
    const auto lut = IBL::BuildBRDFLUT(lutSize, lutSamples);
    glBindTexture(GL_TEXTURE_2D, brdfLUT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, lutSize, lutSize, 0,
                 GL_RG, GL_FLOAT, lut.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    return glGetError() == GL_NO_ERROR;
}

} // namespace Potato
