#include "ShadowMap.h"
#include "Logging/Logger.h"
#include <glad/glad.h>
#include <cmath>
#include <cstdio>

namespace Potato {

ShadowMap::~ShadowMap() {
    Destroy();
}

bool ShadowMap::Create(int s) {
    if (s <= 0) {
        char msg[96];
        std::snprintf(msg, sizeof(msg), "ShadowMap::Create 非法尺寸 %d", s);
        LOG_ERROR(msg);
        return false;
    }
    Destroy(); // 重入安全

    glGenFramebuffers(1, &fbo);

    glGenTextures(1, &depthTex);
    glBindTexture(GL_TEXTURE_2D, depthTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, s, s, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    // 視錐外採樣回邊界深度 1.0（最遠）→ 視錐外無陰影
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float border[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    // 純深度讀取（.r 回傳深度），關閉硬體 compare mode
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                           GL_TEXTURE_2D, depthTex, 0);
    // 無 color attachment：明確關閉 draw/read buffer，core profile 必要
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);

    const bool ok =
        glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ok) {
        LOG_ERROR("ShadowMap::Create FBO 不完整");
        Destroy();
        return false;
    }
    size = s;
    return true;
}

void ShadowMap::Destroy() {
    if (depthTex) {
        glDeleteTextures(1, &depthTex);
        depthTex = 0;
    }
    if (fbo) {
        glDeleteFramebuffers(1, &fbo);
        fbo = 0;
    }
    size = 0;
}

void ShadowMap::Bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, size, size);
}

void ShadowMap::Unbind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

Matrix4 ShadowMap::ComputeLightSpaceMatrix(const Vector3& dir,
                                           const Vector3& center,
                                           float radius) {
    const Vector3 d = dir.Normalized();
    // 近乎垂直的光（dir ∥ up）改用 -Z 為參考上向量避免退化
    Vector3 up(0.0f, 1.0f, 0.0f);
    if (std::fabs(d.y) > 0.999f) {
        up = Vector3(0.0f, 0.0f, -1.0f);
    }
    const Vector3 eye = center - d * (radius * 2.0f);
    const Matrix4 view = Matrix4::LookAt(eye, center, up);
    // 包圍球半徑 r、光源退 2r → 場景深度落在 [r, 3r]；
    // near=0.1/far=4r 留有餘量
    return Matrix4::Orthographic(-radius, radius, -radius, radius,
                                 0.1f, radius * 4.0f) * view;
}

const char* ShadowMap::DepthVertexShader() {
    return R"(
#version 330 core
layout (location = 0) in vec3 aPos;

uniform mat4 lightSpaceMatrix;
uniform mat4 model;

void main()
{
    gl_Position = lightSpaceMatrix * model * vec4(aPos, 1.0);
}
)";
}

const char* ShadowMap::DepthFragmentShader() {
    return R"(
#version 330 core
void main()
{
    // depth-only pass：不寫顏色，gl_FragDepth 預設即可
}
)";
}

const char* ShadowMap::SamplingGLSL() {
    return R"(
// 3x3 PCF 陰影因子（回傳 1=全亮，0=全影）。
// fragWorldPos：世界空間片段位置；normal：世界法線；
// lightDir：光線行進方向（已正規化）；lightSpace：ComputeLightSpaceMatrix
// 產出的矩陣；bias：深度偏差（抗自遮蔽 acne）；texelSize = 1/貼圖邊長。
float ShadowFactor(vec3 fragWorldPos, vec3 normal, vec3 lightDir,
                   mat4 lightSpace, sampler2D shadowMap,
                   float bias, float texelSize)
{
    vec4 lsPos = lightSpace * vec4(fragWorldPos, 1.0);
    vec3 ndc = lsPos.xyz / lsPos.w * 0.5 + 0.5;
    if (ndc.z > 1.0) return 1.0; // 超出 far plane → 不吃陰影

    // 坡度調整 bias：光越切線（掠射）自遮蔽越嚴重
    float adjBias = max(bias * (1.0 - dot(normal, -lightDir)), bias * 0.1);

    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            float d = texture(shadowMap,
                              ndc.xy + vec2(float(x), float(y)) * texelSize).r;
            shadow += (ndc.z - adjBias > d) ? 1.0 : 0.0;
        }
    }
    return 1.0 - shadow / 9.0;
}
)";
}

} // namespace Potato
