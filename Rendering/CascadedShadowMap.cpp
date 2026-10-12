#include "CascadedShadowMap.h"
#include "Logging/Logger.h"

#include <glad/glad.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Potato {

CascadedShadowMap::~CascadedShadowMap() { Destroy(); }

bool CascadedShadowMap::Create(int s, int n) {
    if (s <= 0 || n < 1 || n > kMaxCascades) {
        char msg[96];
        std::snprintf(msg, sizeof(msg),
                      "CascadedShadowMap::Create 非法參數 size=%d n=%d",
                      s, n);
        LOG_ERROR(msg);
        return false;
    }
    Destroy();

    glGenFramebuffers(1, &fbo);

    glGenTextures(1, &depthTex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, depthTex);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT24,
                 s, s, n, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S,
                    GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T,
                    GL_CLAMP_TO_BORDER);
    const float border[4] = {1.f, 1.f, 1.f, 1.f};
    glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, border);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              depthTex, 0, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    const bool ok =
        glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ok) {
        LOG_ERROR("CascadedShadowMap::Create FBO 不完整");
        Destroy();
        return false;
    }
    size = s;
    cascades = n;
    return true;
}

void CascadedShadowMap::Destroy() {
    if (depthTex) {
        glDeleteTextures(1, &depthTex);
        depthTex = 0;
    }
    if (fbo) {
        glDeleteFramebuffers(1, &fbo);
        fbo = 0;
    }
    size = cascades = 0;
}

void CascadedShadowMap::BindCascade(int i) const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    // 逐層重綁（layered attachment 切換，GL3.0+）
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              depthTex, 0, std::clamp(i, 0, cascades - 1));
    glViewport(0, 0, size, size);
}

void CascadedShadowMap::Unbind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ============================================================================
// CPU：切片與 light-space
// ============================================================================

std::vector<float> CascadedShadowMap::ComputeSplits(float nearZ,
                                                    float farZ,
                                                    int cascades,
                                                    float lambda) {
    std::vector<float> edges;
    edges.push_back(nearZ);
    for (int i = 1; i < cascades; ++i) {
        const float t = static_cast<float>(i) / cascades;
        const float logSplit = nearZ * std::pow(farZ / nearZ, t);
        const float linSplit = nearZ + (farZ - nearZ) * t;
        edges.push_back(logSplit * lambda + linSplit * (1.f - lambda));
    }
    edges.push_back(farZ);
    return edges;
}

float CascadedShadowMap::ViewDistToNdcZ(const Matrix4& proj, float dist) {
    const Vector4 p = proj * Vector4(0.f, 0.f, -dist, 1.f);
    return p.z / p.w;
}

std::vector<Vector3> CascadedShadowMap::FrustumSliceCorners(
    const Matrix4& view, const Matrix4& proj,
    float distNear, float distFar) {
    const Matrix4 invVP = (proj * view).Inverse();
    const float zN = ViewDistToNdcZ(proj, distNear);
    const float zF = ViewDistToNdcZ(proj, distFar);
    std::vector<Vector3> out;
    out.reserve(8);
    for (float z : {zN, zF})
        for (float y : {-1.f, 1.f})
            for (float x : {-1.f, 1.f}) {
                const Vector4 p = invVP * Vector4(x, y, z, 1.f);
                out.emplace_back(p.x / p.w, p.y / p.w, p.z / p.w);
            }
    return out;
}

Matrix4 CascadedShadowMap::ComputeSliceLightSpace(const Vector3& dir,
                                                  const Vector3 corners[8],
                                                  int mapSize) {
    const Vector3 d = dir.Normalized();
    Vector3 up(0.f, 1.f, 0.f);
    if (std::fabs(d.y) > 0.999f) up = Vector3(0.f, 0.f, -1.f);

    // 光視圖以切片質心為中心，眼點沿 -dir 退後
    Vector3 centroid(0, 0, 0);
    for (int i = 0; i < 8; ++i) centroid += corners[i];
    centroid = centroid * (1.f / 8.f);
    const Matrix4 lv = Matrix4::LookAt(centroid - d * 100.f, centroid, up);

    // 緊密包圍：角點轉光視圖取 min/max
    float minX = 1e30f, maxX = -1e30f;
    float minY = 1e30f, maxY = -1e30f;
    float minZ = 1e30f, maxZ = -1e30f;
    for (int i = 0; i < 8; ++i) {
        const Vector3 p = lv.TransformPoint(corners[i]);
        minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
        minZ = std::min(minZ, p.z); maxZ = std::max(maxZ, p.z);
    }

    // texel snap：XY 範圍對齊像素格避免邊緣閃爍
    if (mapSize > 0) {
        const float texel = (maxX - minX) / mapSize;
        if (texel > 0.f) {
            minX = std::floor(minX / texel) * texel;
            maxX = std::floor(maxX / texel + 1.f) * texel;
            minY = std::floor(minY / texel) * texel;
            maxY = std::floor(maxY / texel + 1.f) * texel;
        }
    }

    // 光視圖 -Z 朝前；切片遮擋物可能在切片外 → near 往前擴、
    // far 留餘量。z 在 [-100-深, -100+深] 區間（眼點退 100）
    const float range = std::max(1.f, maxZ - minZ);
    const float nearZ = std::max(0.1f, -maxZ - range);
    const float farZ = -minZ + range;
    return Matrix4::Orthographic(minX, maxX, minY, maxY, nearZ, farZ) * lv;
}

const char* CascadedShadowMap::SamplingGLSL() {
    return R"(
// CSM 3x3 PCF：sampler2DArray + layer（級聯索引由呼叫端依視深選擇）
float ShadowFactorCSM(vec3 fragWorldPos, vec3 normal, vec3 lightDir,
                      mat4 lightSpace, sampler2DArray shadowMapArr,
                      int layer, float bias, float texelSize)
{
    vec4 lsPos = lightSpace * vec4(fragWorldPos, 1.0);
    vec3 ndc = lsPos.xyz / lsPos.w * 0.5 + 0.5;
    if (ndc.z > 1.0) return 1.0;

    float adjBias = max(bias * (1.0 - dot(normal, -lightDir)), bias * 0.1);

    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            float d = texture(shadowMapArr,
                              vec3(ndc.xy +
                                   vec2(float(x), float(y)) * texelSize,
                                   float(layer))).r;
            shadow += (ndc.z - adjBias > d) ? 1.0 : 0.0;
        }
    }
    return 1.0 - shadow / 9.0;
}
)";
}

} // namespace Potato
