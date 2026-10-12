#include "PointShadowMap.h"
#include "Logging/Logger.h"
#include <glad/glad.h>
#include <cstdio>

namespace Potato {

PointShadowMap::~PointShadowMap() {
    Destroy();
}

bool PointShadowMap::Create(int s) {
    if (s <= 0) {
        char msg[96];
        std::snprintf(msg, sizeof(msg),
                      "PointShadowMap::Create 非法尺寸 %d", s);
        LOG_ERROR(msg);
        return false;
    }
    Destroy(); // 重入安全

    glGenFramebuffers(1, &fbo);
    glGenTextures(1, &depthCube);
    glBindTexture(GL_TEXTURE_CUBE_MAP, depthCube);
    for (int i = 0; i < 6; ++i) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0,
                     GL_DEPTH_COMPONENT24, s, s, 0,
                     GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER,
                    GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER,
                    GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S,
                    GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T,
                    GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R,
                    GL_CLAMP_TO_EDGE);
    // 純深度讀取（.r 回傳距離/遠度比值），關閉 compare mode
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_MODE,
                    GL_NONE);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                           GL_TEXTURE_CUBE_MAP_POSITIVE_X, depthCube, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) ==
                    GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ok) {
        LOG_ERROR("PointShadowMap::Create FBO 不完整");
        Destroy();
        return false;
    }
    size = s;
    return true;
}

void PointShadowMap::Destroy() {
    if (depthCube) {
        glDeleteTextures(1, &depthCube);
        depthCube = 0;
    }
    if (fbo) {
        glDeleteFramebuffers(1, &fbo);
        fbo = 0;
    }
    size = 0;
}

void PointShadowMap::BindFace(int i) const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    if (i >= 0 && i < 6) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                               GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
                               depthCube, 0);
    }
    glViewport(0, 0, size, size);
}

void PointShadowMap::Unbind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

std::vector<Matrix4> PointShadowMap::ComputeFaceMatrices(
    const Vector3& pos, float range) {
    // OpenGL cube face 朝向慣例（右目標方向，上向量使各面方位正確）
    static const Vector3 kDir[6] = {
        Vector3(1, 0, 0), Vector3(-1, 0, 0), Vector3(0, 1, 0),
        Vector3(0, -1, 0), Vector3(0, 0, 1), Vector3(0, 0, -1),
    };
    static const Vector3 kUp[6] = {
        Vector3(0, -1, 0), Vector3(0, -1, 0), Vector3(0, 0, 1),
        Vector3(0, 0, -1), Vector3(0, -1, 0), Vector3(0, -1, 0),
    };
    const float far = range > 0.1f ? range : 0.1f;
    const Matrix4 proj =
        Matrix4::Perspective(1.57079632679f, 1.0f, far * 0.01f, far);
    std::vector<Matrix4> out;
    out.reserve(6);
    for (int i = 0; i < 6; ++i) {
        out.push_back(proj * Matrix4::LookAt(pos, pos + kDir[i], kUp[i]));
    }
    return out;
}

const char* PointShadowMap::DepthVertexShader() {
    return R"(
#version 330 core
layout (location = 0) in vec3 aPos;

uniform mat4 lightSpaceMatrix; // 當前面 face 的 proj*view
uniform mat4 model;
out vec3 worldPos;

void main()
{
    vec4 wp = model * vec4(aPos, 1.0);
    worldPos = wp.xyz;
    gl_Position = lightSpaceMatrix * wp;
}
)";
}

const char* PointShadowMap::DepthFragmentShader() {
    return R"(
#version 330 core
in vec3 worldPos;
uniform vec3 lightPos;
uniform float farPlane;

void main()
{
    // 線性距離存進深度值——採樣端直接乘回 farPlane 比對
    gl_FragDepth = length(worldPos - lightPos) / farPlane;
}
)";
}

const char* PointShadowMap::SamplingGLSL() {
    return R"(
// 點光立方體陰影因子（回傳 1=全亮，0=全影）。
// fragWorldPos/normal/lightPos 皆為世界空間；cube 內存 dist/farPlane；
// 3x3 切平面 PCF（沿與 dir 垂直的切向偏移）；bias 同 ShadowFactor：
// [0,1] 深度比例 × farPlane 折世界單位，並按法線坡度縮放。
float PointShadowFactor(vec3 fragWorldPos, vec3 normal, vec3 lightPos,
                        samplerCube cube, float farPlane,
                        float bias, float texelSize)
{
    vec3 dir = fragWorldPos - lightPos;
    float dist = length(dir);
    if (dist > farPlane) return 1.0; // 範圍外不吃陰影
    vec3 d = dir / dist;
    float adjBias =
        max(bias * (1.0 - dot(normal, -d)), bias * 0.1) * farPlane;
    // 建立與 dir 垂直的正交切平面基底
    vec3 ref = abs(d.y) > 0.99 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
    vec3 tx = normalize(cross(ref, d));
    vec3 ty = cross(d, tx);
    // 偏移量以「該距離處的 texel 弧長」近似
    float w = texelSize * dist;
    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            vec3 off = (float(x) * tx + float(y) * ty) * w;
            float closest = texture(cube, dir + off).r * farPlane;
            shadow += (dist - adjBias > closest) ? 1.0 : 0.0;
        }
    }
    return 1.0 - shadow / 9.0;
}
)";
}

} // namespace Potato
