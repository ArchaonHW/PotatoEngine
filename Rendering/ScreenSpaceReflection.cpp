#include "ScreenSpaceReflection.h"
#include "Rendering/GBuffer.h"
#include "Logging/Logger.h"
#include <glad/glad.h>
#include <algorithm>
#include <cstdio>

namespace Potato {

ScreenSpaceReflection::~ScreenSpaceReflection() {
    Destroy();
}

std::vector<float> ScreenSpaceReflection::MarchStepTs(int steps,
                                                      float maxDist) {
    std::vector<float> ts;
    if (steps <= 0 || maxDist <= 0.0f) return ts;
    ts.reserve(static_cast<size_t>(steps));
    const float inv = 1.0f / static_cast<float>(steps * steps);
    for (int i = 1; i <= steps; ++i) {
        ts.push_back(maxDist * static_cast<float>(i * i) * inv);
    }
    return ts;
}

bool ScreenSpaceReflection::ViewToUV(const Vector3& viewPos,
                                     const Matrix4& proj, float& outU,
                                     float& outV) {
    const Vector4 clip = proj * Vector4(viewPos.x, viewPos.y,
                                        viewPos.z, 1.0f);
    if (clip.w <= 1e-6f) return false;
    outU = clip.x / clip.w * 0.5f + 0.5f;
    outV = clip.y / clip.w * 0.5f + 0.5f;
    return true;
}

uint32 ScreenSpaceReflection::CreateFloatTarget(int w, int h,
                                                uint32& outTex) {
    uint32 fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(1, &outTex);
    glBindTexture(GL_TEXTURE_2D, outTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA,
                 GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, outTex, 0);
    const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) ==
                    GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ok) {
        glDeleteTextures(1, &outTex);
        glDeleteFramebuffers(1, &fbo);
        outTex = 0;
        return 0;
    }
    return fbo;
}

bool ScreenSpaceReflection::Create(int w, int h, const SSRSettings& s) {
    if (w <= 0 || h <= 0) {
        LOG_ERROR("ScreenSpaceReflection::Create 非法尺寸");
        return false;
    }
    Destroy();
    settings = s;
    settings.maxSteps = std::max(settings.maxSteps, 1);
    settings.binarySteps = std::max(settings.binarySteps, 0);

    ssrFbo = CreateFloatTarget(w, h, ssrTex);
    resolveFbo = CreateFloatTarget(w, h, resolveTex);
    if (!ssrFbo || !resolveFbo) {
        LOG_ERROR("ScreenSpaceReflection::Create FBO 不完整");
        Destroy();
        return false;
    }

    ssrShader = MakeUnique<Shader>();
    if (!ssrShader->LoadFromSource(FullscreenVertexShader(),
                                   SSRFragmentShader())) {
        LOG_ERROR("ScreenSpaceReflection::Create SSR shader 編譯失敗");
        Destroy();
        return false;
    }
    compShader = MakeUnique<Shader>();
    if (!compShader->LoadFromSource(FullscreenVertexShader(),
                                    CompositeFragmentShader())) {
        LOG_ERROR("ScreenSpaceReflection::Create 合成 shader 編譯失敗");
        Destroy();
        return false;
    }

    glGenVertexArrays(1, &vao);
    width = w;
    height = h;
    return true;
}

void ScreenSpaceReflection::Destroy() {
    if (vao) {
        glDeleteVertexArrays(1, &vao);
        vao = 0;
    }
    compShader.reset();
    ssrShader.reset();
    const uint32 texes[2] = {ssrTex, resolveTex};
    for (uint32 t : texes) {
        if (t) glDeleteTextures(1, &t);
    }
    ssrTex = resolveTex = 0;
    const uint32 fbos[2] = {ssrFbo, resolveFbo};
    for (uint32 f : fbos) {
        if (f) glDeleteFramebuffers(1, &f);
    }
    ssrFbo = resolveFbo = 0;
    width = height = 0;
}

void ScreenSpaceReflection::Compute(const GBuffer& gbuffer,
                                    uint32 sceneTex,
                                    const Matrix4& projection) {
    if (!IsValid() || !gbuffer.IsValid() || sceneTex == 0) {
        return;
    }
    const GLboolean depthWas = glIsEnabled(GL_DEPTH_TEST);
    glDisable(GL_DEPTH_TEST);

    glBindFramebuffer(GL_FRAMEBUFFER, ssrFbo);
    glViewport(0, 0, width, height);
    glBindVertexArray(vao);
    ssrShader->Bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gbuffer.GetPositionTexture());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, gbuffer.GetNormalTexture());
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, gbuffer.GetAlbedoTexture());
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, gbuffer.GetMaterialTexture());
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, sceneTex);
    ssrShader->SetUniformInt("gPosition", 0);
    ssrShader->SetUniformInt("gNormal", 1);
    ssrShader->SetUniformInt("gAlbedo", 2);
    ssrShader->SetUniformInt("gMaterial", 3);
    ssrShader->SetUniformInt("sceneTex", 4);
    ssrShader->SetUniformMat4("projection", projection);
    ssrShader->SetUniformInt("maxSteps", settings.maxSteps);
    ssrShader->SetUniformFloat("maxDist", settings.maxDistance);
    ssrShader->SetUniformInt("binarySteps", settings.binarySteps);
    ssrShader->SetUniformFloat("thickness", settings.thickness);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (depthWas) glEnable(GL_DEPTH_TEST);
}

void ScreenSpaceReflection::Apply(uint32 sceneTex) {
    if (!IsValid() || sceneTex == 0) {
        return;
    }
    const GLboolean depthWas = glIsEnabled(GL_DEPTH_TEST);
    glDisable(GL_DEPTH_TEST);

    glBindFramebuffer(GL_FRAMEBUFFER, resolveFbo);
    glViewport(0, 0, width, height);
    glBindVertexArray(vao);
    compShader->Bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneTex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, ssrTex);
    compShader->SetUniformInt("sceneTex", 0);
    compShader->SetUniformInt("ssrTex", 1);
    compShader->SetUniformFloat("intensity", settings.intensity);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (depthWas) glEnable(GL_DEPTH_TEST);
}

void ScreenSpaceReflection::MoveFrom(ScreenSpaceReflection& o) noexcept {
    ssrFbo = o.ssrFbo; ssrTex = o.ssrTex;
    resolveFbo = o.resolveFbo; resolveTex = o.resolveTex;
    vao = o.vao;
    ssrShader = std::move(o.ssrShader);
    compShader = std::move(o.compShader);
    settings = o.settings;
    width = o.width; height = o.height;
    o.ssrFbo = o.ssrTex = o.resolveFbo = o.resolveTex = o.vao = 0;
    o.width = o.height = 0;
}

const char* ScreenSpaceReflection::FullscreenVertexShader() {
    return R"(
#version 330 core
out vec2 TexCoords;
void main()
{
    vec2 v = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    TexCoords = v;
    gl_Position = vec4(v.x * 2.0 - 1.0, v.y * 2.0 - 1.0, 0.0, 1.0);
}
)";
}

const char* ScreenSpaceReflection::SSRFragmentShader() {
    return R"(
#version 330 core
out vec4 FragColor;
in vec2 TexCoords;

uniform sampler2D gPosition; // view-space
uniform sampler2D gNormal;
uniform sampler2D gAlbedo;
uniform sampler2D gMaterial; // r=metallic g=roughness
uniform sampler2D sceneTex;  // 已著色 HDR——命中處的反射色來源
uniform mat4 projection;
uniform int maxSteps;
uniform float maxDist;
uniform int binarySteps;
uniform float thickness;

vec2 ProjUV(vec3 v)
{
    vec4 c = projection * vec4(v, 1.0);
    return c.xy / c.w * 0.5 + 0.5;
}

void main()
{
    vec3 pos = texture(gPosition, TexCoords).xyz;
    vec3 N = texture(gNormal, TexCoords).xyz;
    if (dot(N, N) < 1e-6) { FragColor = vec4(0.0); return; }
    N = normalize(N);
    vec3 albedo = texture(gAlbedo, TexCoords).rgb;
    vec3 mat3v = texture(gMaterial, TexCoords).rgb;

    vec3 V = normalize(-pos);
    vec3 R = reflect(-V, N);
    float NoV = max(dot(N, V), 0.0);
    vec3 F0 = mix(vec3(0.04), albedo, mat3v.r);
    vec3 F = F0 + (1.0 - F0) * pow(1.0 - NoV, 5.0);
    // 粗糙面螢幕空間反射不可靠（微表面散射模糊）
    float w = 1.0 - mat3v.g;

    // 線性掃描找穿越段：q.z < sp.z 表示射線走到表面後方
    // （view space 朝 -z，越負越遠）
    float lo = 0.0, hi = 0.0;
    float prevT = 0.0;
    bool crossed = false;
    for (int i = 1; i <= maxSteps; ++i)
    {
        float t = maxDist * float(i * i)
                  / float(maxSteps * maxSteps);
        vec3 q = pos + R * t;
        vec2 suv = ProjUV(q);
        if (suv.x < 0.0 || suv.x > 1.0 ||
            suv.y < 0.0 || suv.y > 1.0) break;
        vec3 sp = texture(gPosition, suv).xyz;
        if (dot(sp, sp) < 1e-6) { prevT = t; continue; } // 背景
        if (q.z < sp.z) { lo = prevT; hi = t; crossed = true; break; }
        prevT = t;
    }

    vec3 hit = vec3(0.0);
    if (crossed)
    {
        for (int b = 0; b < binarySteps; ++b)
        {
            float m = 0.5 * (lo + hi);
            vec3 q = pos + R * m;
            vec3 sp = texture(gPosition, ProjUV(q)).xyz;
            if (q.z < sp.z) hi = m; else lo = m;
        }
        vec3 q = pos + R * hi;
        vec2 suv = ProjUV(q);
        vec3 sp = texture(gPosition, suv).xyz;
        if (dot(sp, sp) > 1e-6 && sp.z - q.z < thickness)
        {
            float edge = smoothstep(0.0, 0.08, suv.x)
                       * smoothstep(0.0, 0.08, suv.y)
                       * smoothstep(0.0, 0.08, 1.0 - suv.x)
                       * smoothstep(0.0, 0.08, 1.0 - suv.y);
            hit = texture(sceneTex, suv).rgb * F * w * edge;
        }
    }
    FragColor = vec4(hit, 1.0);
}
)";
}

const char* ScreenSpaceReflection::CompositeFragmentShader() {
    return R"(
#version 330 core
out vec4 FragColor;
in vec2 TexCoords;

uniform sampler2D sceneTex;
uniform sampler2D ssrTex; // 已加權（Fresnel×roughness×fade）反射量
uniform float intensity;

void main()
{
    vec3 scene = texture(sceneTex, TexCoords).rgb;
    vec3 refl = texture(ssrTex, TexCoords).rgb;
    FragColor = vec4(scene + refl * intensity, 1.0);
}
)";
}

} // namespace Potato
