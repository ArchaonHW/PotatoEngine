#include "GBuffer.h"
#include "Logging/Logger.h"
#include <glad/glad.h>
#include <cstdio>

namespace Potato {

GBuffer::~GBuffer() {
    Destroy();
}

bool GBuffer::Create(int w, int h) {
    if (w <= 0 || h <= 0) {
        char msg[96];
        std::snprintf(msg, sizeof(msg), "GBuffer::Create 非法尺寸 %dx%d", w, h);
        LOG_ERROR(msg);
        return false;
    }
    Destroy(); // 重入安全：resize 直接 Create 新尺寸

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    // view-space position/normal 需要 float 精度（負值、單位化）→ RGBA16F
    auto makeAttachment = [w, h](GLenum internal, GLenum format, uint32& tex,
                                 int index) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internal), w, h, 0,
                     format, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        // 螢幕空間技術按像素對應採樣——夾取避免鄰邊滲色
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER,
                               static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + index),
                               GL_TEXTURE_2D, tex, 0);
    };

    makeAttachment(GL_RGBA16F, GL_RGBA, positionTex, 0);
    makeAttachment(GL_RGBA16F, GL_RGBA, normalTex, 1);

    glGenTextures(1, &albedoTex);
    glBindTexture(GL_TEXTURE_2D, albedoTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2,
                           GL_TEXTURE_2D, albedoTex, 0);

    const GLenum drawBuffers[3] = {
        GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
    glDrawBuffers(3, drawBuffers);

    glGenRenderbuffers(1, &depthRb);
    glBindRenderbuffer(GL_RENDERBUFFER, depthRb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                              GL_RENDERBUFFER, depthRb);

    const bool ok =
        glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ok) {
        char msg[96];
        std::snprintf(msg, sizeof(msg), "GBuffer::Create FBO 不完整 %dx%d", w, h);
        LOG_ERROR(msg);
        Destroy();
        return false;
    }
    width = w;
    height = h;
    return true;
}

void GBuffer::Destroy() {
    if (depthRb) {
        glDeleteRenderbuffers(1, &depthRb);
        depthRb = 0;
    }
    const uint32 texes[3] = {positionTex, normalTex, albedoTex};
    for (uint32 t : texes) {
        if (t) glDeleteTextures(1, &t);
    }
    positionTex = normalTex = albedoTex = 0;
    if (fbo) {
        glDeleteFramebuffers(1, &fbo);
        fbo = 0;
    }
    width = height = 0;
}

void GBuffer::Bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width, height);
}

void GBuffer::Unbind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

const char* GBuffer::VertexShader() {
    return R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec3 FragPos;    // view-space 位置
out vec3 Normal;     // view-space 法線
out vec2 TexCoords;

void main()
{
    vec4 viewPos = view * model * vec4(aPos, 1.0);
    FragPos = viewPos.xyz;
    // 逆轉置保非等比縮放下法線方向正確（GLSL 330 內建 inverse/transpose）
    Normal = mat3(transpose(inverse(view * model))) * aNormal;
    TexCoords = aTexCoord;
    gl_Position = projection * viewPos;
}
)";
}

const char* GBuffer::FragmentShader() {
    return R"(
#version 330 core
layout (location = 0) out vec4 gPosition;
layout (location = 1) out vec4 gNormal;
layout (location = 2) out vec4 gAlbedo;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;

uniform vec3 albedo;
uniform bool useTexture;
uniform sampler2D albedoMap;

void main()
{
    gPosition = vec4(FragPos, 1.0);
    gNormal = vec4(normalize(Normal), 1.0);
    gAlbedo = useTexture ? texture(albedoMap, TexCoords)
                       : vec4(albedo, 1.0);
}
)";
}

} // namespace Potato
