#include "DeferredShading.h"
#include "GBuffer.h"
#include "ShadowMap.h"
#include "Logging/Logger.h"
#include <glad/glad.h>
#include <algorithm>
#include <string>

namespace Potato {

DeferredLighting::~DeferredLighting() {
    Destroy();
}

bool DeferredLighting::Create() {
    Destroy();
    shader = MakeUnique<Shader>();
    if (!shader->LoadFromSource(VertexShader(), FragmentShader())) {
        LOG_ERROR("DeferredLighting::Create shader 編譯失敗");
        shader.reset();
        return false;
    }
    glGenVertexArrays(1, &vao);
    return true;
}

void DeferredLighting::Destroy() {
    shader.reset();
    if (vao) {
        glDeleteVertexArrays(1, &vao);
        vao = 0;
    }
}

// ============================================================================
// 純 CPU：光源打包（headless 可測）
// ============================================================================

std::vector<float> DeferredLighting::PackDirLights(
    const std::vector<DeferredDirLight>& dirs, const Matrix4& view) {
    const int n = std::min(static_cast<int>(dirs.size()), kMaxDirLights);
    std::vector<float> out(static_cast<size_t>(n) * 6);
    float* dirOut = out.data();
    float* colOut = out.data() + n * 3;
    for (int i = 0; i < n; ++i) {
        const Vector3 d =
            view.TransformVector(dirs[i].direction.Normalized()).Normalized();
        dirOut[i * 3 + 0] = d.x;
        dirOut[i * 3 + 1] = d.y;
        dirOut[i * 3 + 2] = d.z;
        colOut[i * 3 + 0] = dirs[i].color.x * dirs[i].intensity;
        colOut[i * 3 + 1] = dirs[i].color.y * dirs[i].intensity;
        colOut[i * 3 + 2] = dirs[i].color.z * dirs[i].intensity;
    }
    return out;
}

std::vector<float> DeferredLighting::PackPointLights(
    const std::vector<DeferredPointLight>& points, const Matrix4& view) {
    const int n =
        std::min(static_cast<int>(points.size()), kMaxPointLights);
    std::vector<float> out(static_cast<size_t>(n) * 7);
    float* posOut = out.data();
    float* colOut = out.data() + n * 3; // vec4 color+radius
    for (int i = 0; i < n; ++i) {
        const Vector3 p = view.TransformPoint(points[i].position);
        posOut[i * 3 + 0] = p.x;
        posOut[i * 3 + 1] = p.y;
        posOut[i * 3 + 2] = p.z;
        colOut[i * 4 + 0] = points[i].color.x * points[i].intensity;
        colOut[i * 4 + 1] = points[i].color.y * points[i].intensity;
        colOut[i * 4 + 2] = points[i].color.z * points[i].intensity;
        colOut[i * 4 + 3] = points[i].radius;
    }
    return out;
}

int DeferredLighting::FirstShadowCaster(
    const std::vector<DeferredDirLight>& dirs) {
    const int n = std::min(static_cast<int>(dirs.size()), kMaxDirLights);
    for (int i = 0; i < n; ++i) {
        if (dirs[static_cast<size_t>(i)].castShadow) return i;
    }
    return -1;
}

// ============================================================================
// GL pass
// ============================================================================

void DeferredLighting::Render(
    const GBuffer& gbuffer,
    const std::vector<DeferredDirLight>& dirs,
    const std::vector<DeferredPointLight>& points,
    const Matrix4& view,
    uint32 aoTex,
    const ShadowMap* shadow,
    const Matrix4& lightSpace,
    float ambient,
    float shadowBias) {
    if (!shader || !gbuffer.IsValid()) {
        return;
    }
    const GLboolean depthWas = glIsEnabled(GL_DEPTH_TEST);
    glDisable(GL_DEPTH_TEST);

    shader->Bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gbuffer.GetPositionTexture());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, gbuffer.GetNormalTexture());
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, gbuffer.GetAlbedoTexture());
    shader->SetUniformInt("gPosition", 0);
    shader->SetUniformInt("gNormal", 1);
    shader->SetUniformInt("gAlbedo", 2);

    shader->SetUniformInt("useAO", aoTex ? 1 : 0);
    if (aoTex) {
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, aoTex);
        shader->SetUniformInt("aoTex", 3);
    }

    const int caster = FirstShadowCaster(dirs);
    const bool useShadow = shadow && shadow->IsValid() && caster >= 0;
    shader->SetUniformInt("useShadow", useShadow ? 1 : 0);
    if (useShadow) {
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, shadow->GetDepthMap());
        shader->SetUniformInt("shadowMap", 4);
        shader->SetUniformMat4("lightSpace", lightSpace);
        shader->SetUniformVec3("shadowLightDir",
                               dirs[static_cast<size_t>(caster)]
                                   .direction.Normalized());
        shader->SetUniformFloat("shadowBias", shadowBias);
        shader->SetUniformFloat("shadowTexel",
                                1.0f / shadow->GetSize());
        shader->SetUniformMat4("invView", view.Inverse());
    }
    shader->SetUniformInt("shadowIndex", caster);
    shader->SetUniformFloat("ambient", ambient);

    const auto dirPacked = PackDirLights(dirs, view);
    const int numDir =
        std::min(static_cast<int>(dirs.size()), kMaxDirLights);
    shader->SetUniformInt("numDir", numDir);
    if (numDir > 0) {
        glUniform3fv(glGetUniformLocation(shader->GetProgramID(),
                                          "dirDir[0]"),
                     numDir, dirPacked.data());
        glUniform3fv(glGetUniformLocation(shader->GetProgramID(),
                                          "dirColor[0]"),
                     numDir, dirPacked.data() + numDir * 3);
    }

    const auto ptPacked = PackPointLights(points, view);
    const int numPoint =
        std::min(static_cast<int>(points.size()), kMaxPointLights);
    shader->SetUniformInt("numPoint", numPoint);
    if (numPoint > 0) {
        glUniform3fv(glGetUniformLocation(shader->GetProgramID(),
                                          "pointPos[0]"),
                     numPoint, ptPacked.data());
        glUniform4fv(glGetUniformLocation(shader->GetProgramID(),
                                          "pointColorRad[0]"),
                     numPoint, ptPacked.data() + numPoint * 3);
    }

    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (depthWas) {
        glEnable(GL_DEPTH_TEST);
    }
}

// ============================================================================
// 內建 GLSL
// ============================================================================

const char* DeferredLighting::VertexShader() {
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

const char* DeferredLighting::FragmentShader() {
    // 頭尾字串 + ShadowMap::SamplingGLSL() 組合——ShadowFactor 單一正本
    static const std::string src =
        std::string(R"(
#version 330 core
out vec4 FragColor;
in vec2 TexCoords;

uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D gAlbedo;
uniform sampler2D aoTex;
uniform bool useAO;
uniform sampler2D shadowMap;
uniform bool useShadow;

uniform mat4 invView;
uniform mat4 lightSpace;
uniform vec3 shadowLightDir;
uniform float shadowBias;
uniform float shadowTexel;
uniform int shadowIndex;

uniform int numDir;
uniform vec3 dirDir[4];    // view space（已正規化）
uniform vec3 dirColor[4];  // rgb * intensity

uniform int numPoint;
uniform vec3 pointPos[8];      // view space
uniform vec4 pointColorRad[8]; // rgb * intensity, a = radius

uniform float ambient;

)") + ShadowMap::SamplingGLSL() + R"(
void main()
{
    vec3 pos = texture(gPosition, TexCoords).xyz;
    vec3 N = texture(gNormal, TexCoords).xyz;
    vec3 albedo = texture(gAlbedo, TexCoords).rgb;

    // 背景像素（無法線）不吃光照——保留 albedo*ambient 避免死黑
    if (dot(N, N) < 1e-6)
    {
        FragColor = vec4(albedo * ambient, 1.0);
        return;
    }
    N = normalize(N);
    vec3 V = normalize(-pos); // view space：相機在原點

    vec3 Lo = vec3(0.0);
    for (int i = 0; i < numDir; ++i)
    {
        vec3 L = normalize(-dirDir[i]); // 行進方向取反 = 指向光源
        float diff = max(dot(N, L), 0.0);
        vec3 H = normalize(L + V);
        float spec = pow(max(dot(N, H), 0.0), 32.0) * 0.5;
        float sh = 1.0;
        if (useShadow && i == shadowIndex)
        {
            vec3 wPos = (invView * vec4(pos, 1.0)).xyz;
            vec3 wN = normalize(mat3(invView) * N);
            sh = ShadowFactor(wPos, wN, shadowLightDir, lightSpace,
                              shadowMap, shadowBias, shadowTexel);
        }
        Lo += dirColor[i] * (diff * albedo + spec) * sh;
    }
    for (int i = 0; i < numPoint; ++i)
    {
        vec3 toLight = pointPos[i] - pos;
        float d = length(toLight);
        float rad = pointColorRad[i].a;
        if (d >= rad) continue;
        vec3 L = toLight / d;
        float att = 1.0 - d / rad;
        att *= att; // 平滑窗口衰減，半徑外歸零
        float diff = max(dot(N, L), 0.0);
        vec3 H = normalize(L + V);
        float spec = pow(max(dot(N, H), 0.0), 32.0) * 0.5;
        Lo += pointColorRad[i].rgb * (diff * albedo + spec) * att;
    }

    float ao = useAO ? texture(aoTex, TexCoords).r : 1.0;
    vec3 col = (albedo * ambient + Lo) * ao;
    FragColor = vec4(col, 1.0);
}
)";
    return src.c_str();
}

} // namespace Potato
