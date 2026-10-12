#include "DeferredShading.h"
#include "CascadedShadowMap.h"
#include "GBuffer.h"
#include "IBL.h"
#include "PointShadowMap.h"
#include "ShadowMap.h"
#include "Logging/Logger.h"
#include <glad/glad.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>

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

std::vector<float> DeferredLighting::PackSpotLights(
    const std::vector<DeferredSpotLight>& spots, const Matrix4& view) {
    const int n =
        std::min(static_cast<int>(spots.size()), kMaxSpotLights);
    std::vector<float> out(static_cast<size_t>(n) * 12);
    float* posOut = out.data();
    float* dirOut = out.data() + n * 3;
    float* colOut = out.data() + n * 6;  // vec4 color+radius
    float* coneOut = out.data() + n * 10; // vec2 inner/outer cos
    for (int i = 0; i < n; ++i) {
        const Vector3 p = view.TransformPoint(spots[i].position);
        const Vector3 d =
            view.TransformVector(spots[i].direction.Normalized())
                .Normalized();
        float inner = spots[i].innerCos;
        float outer = spots[i].outerCos;
        if (inner < outer) std::swap(inner, outer); // cos 越大錐越窄
        posOut[i * 3 + 0] = p.x;
        posOut[i * 3 + 1] = p.y;
        posOut[i * 3 + 2] = p.z;
        dirOut[i * 3 + 0] = d.x;
        dirOut[i * 3 + 1] = d.y;
        dirOut[i * 3 + 2] = d.z;
        colOut[i * 4 + 0] = spots[i].color.x * spots[i].intensity;
        colOut[i * 4 + 1] = spots[i].color.y * spots[i].intensity;
        colOut[i * 4 + 2] = spots[i].color.z * spots[i].intensity;
        colOut[i * 4 + 3] = spots[i].radius;
        coneOut[i * 2 + 0] = inner;
        coneOut[i * 2 + 1] = outer;
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

int DeferredLighting::FirstSpotShadowCaster(
    const std::vector<DeferredSpotLight>& spots) {
    const int n =
        std::min(static_cast<int>(spots.size()), kMaxSpotLights);
    for (int i = 0; i < n; ++i) {
        if (spots[static_cast<size_t>(i)].castShadow) return i;
    }
    return -1;
}

int DeferredLighting::FirstPointShadowCaster(
    const std::vector<DeferredPointLight>& points) {
    const int n =
        std::min(static_cast<int>(points.size()), kMaxPointLights);
    for (int i = 0; i < n; ++i) {
        if (points[static_cast<size_t>(i)].castShadow) return i;
    }
    return -1;
}

Vector3 DeferredLighting::SkyDir(const Vector2& uv,
                                 const Matrix4& invView,
                                 const Matrix4& invProj) {
    const Vector4 clip(uv.x * 2.0f - 1.0f, uv.y * 2.0f - 1.0f, 1.0f, 1.0f);
    const Vector4 v = invProj * clip;
    Vector3 vs(v.x, v.y, v.z);
    if (std::fabs(v.w) > 1e-8f) {
        vs = vs * (1.0f / v.w);
    }
    // TransformVector 平移分量為 0——天空方向與相機位置無關
    return invView.TransformVector(vs).Normalized();
}

// ============================================================================
// GL pass
// ============================================================================

void DeferredLighting::Render(
    const GBuffer& gbuffer,
    const std::vector<DeferredDirLight>& dirs,
    const std::vector<DeferredPointLight>& points,
    const std::vector<DeferredSpotLight>& spots,
    const Matrix4& view,
    uint32 aoTex,
    const ShadowMap* shadow,
    const Matrix4& lightSpace,
    float ambient,
    float shadowBias,
    const EnvironmentMaps* env,
    float iblIntensity,
    float matRoughness,
    float matMetallic,
    bool useMaterialChannel,
    float emissiveScale,
    const CascadedShadowMap* csm,
    const Matrix4* csmLightSpace,
    const float* csmSplits,
    const Matrix4& projection,
    const ShadowMap* spotShadow,
    const Matrix4& spotLightSpace,
    const PointShadowMap* pointShadow,
    float pointShadowFar) {
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

    // attachment 3：per-pixel PBR 參數（r=metal, g=rough, b=emissive）
    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, gbuffer.GetMaterialTexture());
    shader->SetUniformInt("gMaterial", 8);
    shader->SetUniformInt("useMatParams", useMaterialChannel ? 1 : 0);
    shader->SetUniformFloat("emissiveScale", emissiveScale);

    shader->SetUniformInt("useAO", aoTex ? 1 : 0);
    if (aoTex) {
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, aoTex);
        shader->SetUniformInt("aoTex", 3);
    }

    // IBL：ambient 換成環境光（irradiance + prefiltered + BRDF LUT）
    UploadIBL(*shader, env, iblIntensity);
    const bool useIBL = env && env->IsValid();
    // 全域純量材質——useMatParams=false 時的備援，與 IBL 無關
    shader->SetUniformFloat("matRoughness", matRoughness);
    shader->SetUniformFloat("matMetallic", matMetallic);
    if (useIBL) {
        // 天空：背景像素採 env cubemap（方向由 invProj+invView 還原）
        glActiveTexture(GL_TEXTURE10);
        glBindTexture(GL_TEXTURE_CUBE_MAP, env->GetEnvCubemap());
        shader->SetUniformInt("skyCube", 10);
        shader->SetUniformMat4("invProj", projection.Inverse());
    }

    UploadLights(*shader, dirs, points, spots, view, ambient,
                 shadowBias, shadow, lightSpace, csm, csmLightSpace,
                 csmSplits, spotShadow, spotLightSpace, pointShadow,
                 pointShadowFar);

    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    glActiveTexture(GL_TEXTURE12);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    glActiveTexture(GL_TEXTURE11);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE10);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
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

void DeferredLighting::UploadIBL(Shader& target,
                                 const EnvironmentMaps* env,
                                 float iblIntensity) {
    const bool useIBL = env && env->IsValid();
    target.SetUniformInt("useIBL", useIBL ? 1 : 0);
    if (!useIBL) {
        return;
    }
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_CUBE_MAP, env->GetIrradianceMap());
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_CUBE_MAP, env->GetPrefilterMap());
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D, env->GetBRDFLUT());
    target.SetUniformInt("irradianceMap", 5);
    target.SetUniformInt("prefilterMap", 6);
    target.SetUniformInt("brdfLUT", 7);
    target.SetUniformFloat("iblIntensity", iblIntensity);
    glActiveTexture(GL_TEXTURE0);
}

void DeferredLighting::UploadLights(
    Shader& target,
    const std::vector<DeferredDirLight>& dirs,
    const std::vector<DeferredPointLight>& points,
    const std::vector<DeferredSpotLight>& spots,
    const Matrix4& view,
    float ambient,
    float shadowBias,
    const ShadowMap* shadow,
    const Matrix4& lightSpace,
    const CascadedShadowMap* csm,
    const Matrix4* csmLightSpace,
    const float* csmSplits,
    const ShadowMap* spotShadow,
    const Matrix4& spotLightSpace,
    const PointShadowMap* pointShadow,
    float pointShadowFar) {
    Shader* const shader = &target;
    const int caster = FirstShadowCaster(dirs);
    const bool useCSM =
        csm && csm->IsValid() && csmLightSpace && csmSplits &&
        caster >= 0;
    // CSM 優先於單張 shadow map（大場景路徑）
    const bool useShadow =
        !useCSM && shadow && shadow->IsValid() && caster >= 0;
    shader->SetUniformInt("useCSM", useCSM ? 1 : 0);
    shader->SetUniformInt("useShadow", useShadow ? 1 : 0);
    if (useCSM) {
        glActiveTexture(GL_TEXTURE9);
        glBindTexture(GL_TEXTURE_2D_ARRAY, csm->GetDepthArray());
        shader->SetUniformInt("csmMap", 9);
        shader->SetUniformInt("csmCount", csm->GetCascadeCount());
        shader->SetUniformFloat("csmTexel", 1.0f / csm->GetSize());
        shader->SetUniformVec3("shadowLightDir",
                               dirs[static_cast<size_t>(caster)]
                                   .direction.Normalized());
        shader->SetUniformFloat("shadowBias", shadowBias);
        // 內部邊界升冪寫入 vec4 前 N-1 欄
        float splits[4] = {1e30f, 1e30f, 1e30f, 1e30f};
        for (int i = 0; i < csm->GetCascadeCount() - 1; ++i)
            splits[i] = csmSplits[i];
        glUniform4fv(
            glGetUniformLocation(shader->GetProgramID(), "csmSplits"),
            1, splits);
        glUniformMatrix4fv(
            glGetUniformLocation(shader->GetProgramID(),
                                 "csmLightSpace[0]"),
            csm->GetCascadeCount(), false,
            csmLightSpace[0].Data());
    }
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
    }

    // 聚光陰影：獨立於方向光 shadow/CSM，第一支 castShadow 聚光生效
    const int spotCaster = FirstSpotShadowCaster(spots);
    const bool useSpotShadow =
        spotShadow && spotShadow->IsValid() && spotCaster >= 0;
    shader->SetUniformInt("useSpotShadow", useSpotShadow ? 1 : 0);
    shader->SetUniformInt("spotShadowIndex", spotCaster);
    if (useSpotShadow) {
        glActiveTexture(GL_TEXTURE11);
        glBindTexture(GL_TEXTURE_2D, spotShadow->GetDepthMap());
        shader->SetUniformInt("spotShadowMap", 11);
        shader->SetUniformMat4("spotLightSpace", spotLightSpace);
        shader->SetUniformFloat("spotShadowTexel",
                                1.0f / spotShadow->GetSize());
    }

    // 點光陰影：cube 深度方塊，第一支 castShadow 點光生效
    const int pointCaster = FirstPointShadowCaster(points);
    const bool usePointShadow = pointShadow && pointShadow->IsValid() &&
                                pointCaster >= 0 && pointShadowFar > 0.0f;
    shader->SetUniformInt("usePointShadow", usePointShadow ? 1 : 0);
    shader->SetUniformInt("pointShadowIndex", pointCaster);
    if (usePointShadow) {
        glActiveTexture(GL_TEXTURE12);
        glBindTexture(GL_TEXTURE_CUBE_MAP, pointShadow->GetDepthCube());
        shader->SetUniformInt("pointShadowCube", 12);
        shader->SetUniformFloat("pointShadowFar", pointShadowFar);
        shader->SetUniformFloat("pointShadowTexel",
                                1.0f / pointShadow->GetSize());
    }

    // 陰影/IBL/天空都需把 view-space 還原世界座標——一律上傳
    shader->SetUniformMat4("invView", view.Inverse());
    shader->SetUniformInt("shadowIndex", caster);
    shader->SetUniformFloat("ambient", ambient);

    // 光源超上限時靜默截斷易漏光——每類告警一次
    const auto warnOverflow = [](const char* kind, size_t n,
                                 int cap) {
        if (n > static_cast<size_t>(cap)) {
            static std::unordered_map<const char*, bool> warned;
            if (!warned[kind]) {
                warned[kind] = true;
                LOG_WARNING("DeferredLighting: " +
                            std::string(kind) + " 數量 " +
                            std::to_string(n) + " 超過上限 " +
                            std::to_string(cap) + "，多出的被截斷");
            }
        }
    };
    warnOverflow("方向光", dirs.size(), kMaxDirLights);
    warnOverflow("點光", points.size(), kMaxPointLights);
    warnOverflow("聚光", spots.size(), kMaxSpotLights);

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

    const auto spPacked = PackSpotLights(spots, view);
    const int numSpot =
        std::min(static_cast<int>(spots.size()), kMaxSpotLights);
    shader->SetUniformInt("numSpot", numSpot);
    if (numSpot > 0) {
        const GLuint prog = shader->GetProgramID();
        glUniform3fv(glGetUniformLocation(prog, "spotPos[0]"),
                     numSpot, spPacked.data());
        glUniform3fv(glGetUniformLocation(prog, "spotDir[0]"),
                     numSpot, spPacked.data() + numSpot * 3);
        glUniform4fv(glGetUniformLocation(prog, "spotColorRad[0]"),
                     numSpot, spPacked.data() + numSpot * 6);
        glUniform2fv(glGetUniformLocation(prog, "spotCone[0]"),
                     numSpot, spPacked.data() + numSpot * 10);
    }
}

// ============================================================================
// 內建 GLSL
// ============================================================================

namespace {

// deferred 合成與 forward 透明 pass 共用的光源+陰影 uniform 宣告
const char* kSharedLightUniformsGLSL = R"(
uniform sampler2D shadowMap;
uniform bool useShadow;
uniform bool useCSM;
uniform sampler2DArray csmMap;
uniform mat4 csmLightSpace[4];
uniform vec4 csmSplits;
uniform int csmCount;
uniform float csmTexel;
uniform mat4 invView;
uniform mat4 lightSpace;
uniform vec3 shadowLightDir;
uniform float shadowBias;
uniform float shadowTexel;
uniform int shadowIndex;
uniform int numDir;
uniform vec3 dirDir[4];
uniform vec3 dirColor[4];
uniform int numPoint;
uniform vec3 pointPos[8];
uniform vec4 pointColorRad[8];
uniform int numSpot;
uniform vec3 spotPos[8];
uniform vec3 spotDir[8];
uniform vec4 spotColorRad[8];
uniform vec2 spotCone[8];
uniform sampler2D spotShadowMap;
uniform bool useSpotShadow;
uniform mat4 spotLightSpace;
uniform float spotShadowTexel;
uniform int spotShadowIndex;
uniform samplerCube pointShadowCube;
uniform bool usePointShadow;
uniform float pointShadowFar;
uniform float pointShadowTexel;
uniform int pointShadowIndex;
uniform float ambient;
uniform float emissiveScale;
)";

// Cook-Torrance GGX 直接光 helpers——deferred 與 forward 共用
const char* kPbrGLSL = R"(
const float PBR_PI = 3.14159265359;
float D_GGX(float NdotH, float rough)
{
    float a = rough * rough;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(PBR_PI * d * d, 1e-6);
}
float G_SchlickPBR(float NdotX, float k)
{
    return NdotX / (NdotX * (1.0 - k) + k);
}
float G_SmithPBR(float NdotV, float NdotL, float rough)
{
    float r = rough + 1.0;
    float k = r * r / 8.0; // 直接光重映射
    return G_SchlickPBR(NdotV, k) * G_SchlickPBR(NdotL, k);
}
vec3 F_SchlickPBR(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0),
                                 5.0);
}
// 回傳 kD*albedo/π + DFG——呼叫端乘 radiance * NdotL
vec3 PBR_BRDF(vec3 N, vec3 V, vec3 L, vec3 albedo, float rough,
              float metallic, vec3 F0)
{
    vec3 H = normalize(V + L);
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float D = D_GGX(max(dot(N, H), 0.0), rough);
    vec3 F = F_SchlickPBR(max(dot(H, V), 0.0), F0);
    float G = G_SmithPBR(NdotV, NdotL, rough);
    vec3 spec = D * F * G / max(4.0 * NdotV * NdotL, 1e-4);
    vec3 kD = (1.0 - F) * (1.0 - metallic);
    return kD * albedo / PBR_PI + spec;
}
)";

// 三光源迴圈——deferred 合成與 forward 透明共用。呼叫端需在
// 作用域內定義：pos/N/V/albedo/roughness/metallic/F0/Lo
const char* kLightLoopsGLSL = R"(
    for (int i = 0; i < numDir; ++i)
    {
        vec3 L = normalize(-dirDir[i]); // 行進方向取反 = 指向光源
        float diff = max(dot(N, L), 0.0);
        float sh = 1.0;
        if ((useShadow || useCSM) && i == shadowIndex)
        {
            vec3 wPos = (invView * vec4(pos, 1.0)).xyz;
            vec3 wN = normalize(mat3(invView) * N);
            if (useCSM)
            {
                // 以視深選級：越過第 k 個邊界 → 第 k+1 層
                float vd = -pos.z;
                int layer = 0;
                for (int c = 0; c < csmCount - 1; ++c)
                    if (vd > csmSplits[c]) ++layer;
                sh = ShadowFactorCSM(wPos, wN, shadowLightDir,
                                     csmLightSpace[layer], csmMap,
                                     layer, shadowBias, csmTexel);
            }
            else
            {
                sh = ShadowFactor(wPos, wN, shadowLightDir, lightSpace,
                                  shadowMap, shadowBias, shadowTexel);
            }
        }
        Lo += dirColor[i]
              * PBR_BRDF(N, V, L, albedo, roughness, metallic, F0)
              * diff * sh;
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
        float sh = 1.0;
        if (usePointShadow && i == pointShadowIndex)
        {
            vec3 wPos = (invView * vec4(pos, 1.0)).xyz;
            vec3 wN = normalize(mat3(invView) * N);
            vec3 wLightPos = (invView * vec4(pointPos[i], 1.0)).xyz;
            sh = PointShadowFactor(wPos, wN, wLightPos,
                                   pointShadowCube, pointShadowFar,
                                   shadowBias, pointShadowTexel);
        }
        Lo += pointColorRad[i].rgb
              * PBR_BRDF(N, V, L, albedo, roughness, metallic, F0)
              * diff * att * sh;
    }
    for (int i = 0; i < numSpot; ++i)
    {
        vec3 toLight = spotPos[i] - pos;
        float d = length(toLight);
        float rad = spotColorRad[i].a;
        if (d >= rad) continue;
        vec3 L = toLight / d;
        // 光源→像素方向與照射方向的夾角餘弦
        float theta = dot(-L, normalize(spotDir[i]));
        vec2 cone = spotCone[i];
        if (theta < cone.y) continue; // 外錐之外
        // inner 內全強度、inner~outer 線性衰減
        float coneAtt = clamp(
            (theta - cone.y) / max(cone.x - cone.y, 1e-5), 0.0, 1.0);
        float att = 1.0 - d / rad;
        att *= att;
        float diff = max(dot(N, L), 0.0);
        float sh = 1.0;
        if (useSpotShadow && i == spotShadowIndex)
        {
            // 逐片段光線行進方向（聚光是點狀光源）
            vec3 wPos = (invView * vec4(pos, 1.0)).xyz;
            vec3 wN = normalize(mat3(invView) * N);
            vec3 wSpotPos = (invView * vec4(spotPos[i], 1.0)).xyz;
            vec3 spotLDir = normalize(wPos - wSpotPos);
            sh = ShadowFactor(wPos, wN, spotLDir, spotLightSpace,
                              spotShadowMap, shadowBias,
                              spotShadowTexel);
        }
        Lo += spotColorRad[i].rgb
              * PBR_BRDF(N, V, L, albedo, roughness, metallic, F0)
              * diff * att * coneAtt * sh;
    }
)";

// IBL 環境光 uniform 宣告——deferred 合成與 forward 透明共用
// （deferred 另有 skyCube/invProj/matRoughness/matMetallic 專屬項）
const char* kIblUniformsGLSL = R"(
uniform bool useIBL;
uniform samplerCube irradianceMap;
uniform samplerCube prefilterMap;
uniform sampler2D brdfLUT;
uniform float iblIntensity;
#define IBL_MAX_LOD 4.0
)";

// IBL 環境項（split-sum 近似）：useIBL 時 irradiance 漫射 +
// prefiltered 鏡面 + BRDF LUT，否則退回 flat ambient。
// deferred 與 forward 透明 pass 同一正本——呼叫端作用域內需有
//   N/V/albedo/roughness/metallic/F0/ambient
// （forward 無 AO；deferred 端於 amb 後再乘 ao）
const char* kIblAmbientGLSL = R"(
    vec3 amb;
    if (useIBL)
    {
        vec3 wN = normalize(mat3(invView) * N);
        vec3 wV = normalize(mat3(invView) * V);
        vec3 wR = reflect(-wV, wN);
        float NoV = max(dot(N, V), 0.0);

        vec3 F = F0 + (max(vec3(1.0 - roughness), F0) - F0)
                   * pow(1.0 - NoV, 5.0);
        vec3 kD = (1.0 - F) * (1.0 - metallic);

        vec3 diffuse = texture(irradianceMap, wN).rgb * albedo;
        vec3 prefiltered =
            textureLod(prefilterMap, wR, roughness * IBL_MAX_LOD).rgb;
        vec2 brdf = texture(brdfLUT, vec2(NoV, roughness)).rg;
        vec3 specular = prefiltered * (F * brdf.x + brdf.y);
        amb = (kD * diffuse + specular) * iblIntensity;
    }
    else
    {
        amb = albedo * ambient;
    }
)";

} // anonymous namespace

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
    // 頭尾字串 + 共享區塊 + ShadowMap::SamplingGLSL() 組合——
    // 光源/陰影/PBR/光源迴圈與 ForwardFragmentShader 同一正本
    static const std::string src =
        std::string(R"(
#version 330 core
out vec4 FragColor;
in vec2 TexCoords;

uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D gAlbedo;
uniform sampler2D gMaterial;   // r=metallic g=roughness b=emissive
uniform bool useMatParams;
uniform sampler2D aoTex;
uniform bool useAO;
)") + kSharedLightUniformsGLSL + R"(
uniform mat4 invProj;
uniform samplerCube skyCube;  // 環境 cubemap——useIBL 時背景輸出天空
uniform float matRoughness;
uniform float matMetallic;
)" + kIblUniformsGLSL + ShadowMap::SamplingGLSL() +
        CascadedShadowMap::SamplingGLSL() +
        PointShadowMap::SamplingGLSL() + kPbrGLSL + R"(
void main()
{
    vec3 pos = texture(gPosition, TexCoords).xyz;
    vec3 N = texture(gNormal, TexCoords).xyz;
    vec3 albedo = texture(gAlbedo, TexCoords).rgb;

    // 背景像素（無法線）：useIBL 時採 env cubemap 當天空
    // （與 SkyDir() 同套數學），走 HDR→bloom→tonemap 同一條鏈
    if (dot(N, N) < 1e-6)
    {
        if (useIBL)
        {
            vec4 clip = vec4(TexCoords * 2.0 - 1.0, 1.0, 1.0);
            vec4 vpos = invProj * clip;
            vec3 dir = normalize(mat3(invView) * (vpos.xyz / vpos.w));
            FragColor = vec4(texture(skyCube, dir).rgb * iblIntensity,
                             1.0);
            return;
        }
        FragColor = vec4(albedo * ambient, 1.0);
        return;
    }
    N = normalize(N);
    vec3 V = normalize(-pos); // view space：相機在原點

    // PBR 參數：gMaterial 通道優先，useMatParams=false 時用全域預設
    vec3 mat3v = texture(gMaterial, TexCoords).rgb;
    float roughness = useMatParams ? max(mat3v.g, 0.05)
                                   : matRoughness;
    float metallic  = useMatParams ? mat3v.r : matMetallic;
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 Lo = vec3(0.0);
)" + kLightLoopsGLSL + kIblAmbientGLSL + R"(

    float ao = useAO ? texture(aoTex, TexCoords).r : 1.0;
    vec3 col = (amb + Lo) * ao;
    // 自發光：不吃光照也不吃 AO（燈火/霓虹在暗處仍亮）
    col += albedo * mat3v.b * emissiveScale;
    FragColor = vec4(col, 1.0);
}
)";
    return src.c_str();
}

const char* DeferredLighting::ForwardVertexShader() {
    // 與 GBuffer 幾何 pass 同一支——輸出 view-space FragPos/Normal
    return GBuffer::VertexShader();
}

const char* DeferredLighting::ForwardFragmentShader() {
    // 透明物件 forward PBR：光源 uniform/陰影採樣/BRDF/光源迴圈
    // 全部與延遲合成同一正本（kSharedLightUniformsGLSL +
    // kPbrGLSL + kLightLoopsGLSL），差異只在資料來源——
    // 透明物件不寫 GBuffer，材質走逐次繪製 uniform。
    static const std::string src =
        std::string(R"(
#version 330 core
out vec4 FragColor;
in vec3 FragPos;
in vec3 Normal;

// 逐次繪製材質（albedo rgb + alpha 不透明度 + PBR 純量）
uniform vec3 materialAlbedo;
uniform float materialAlpha;
uniform float materialMetallic;
uniform float materialRoughness;
uniform float materialEmissive;
)") + kSharedLightUniformsGLSL + kIblUniformsGLSL +
        ShadowMap::SamplingGLSL() +
        CascadedShadowMap::SamplingGLSL() +
        PointShadowMap::SamplingGLSL() + kPbrGLSL + R"(
void main()
{
    vec3 pos = FragPos;
    vec3 N = normalize(Normal);
    vec3 V = normalize(-pos); // view space：相機在原點
    vec3 albedo = materialAlbedo;
    float roughness = max(materialRoughness, 0.05);
    float metallic = materialMetallic;
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 Lo = vec3(0.0);
)" + kLightLoopsGLSL + kIblAmbientGLSL + R"(

    // 環境項 + 直接光 + 自發光（IBL 由共享段處理；forward
    // pass 無 GBuffer 逐像素 AO）
    vec3 col = amb + Lo
             + albedo * materialEmissive * emissiveScale;
    FragColor = vec4(col, materialAlpha);
}
)";
    return src.c_str();
}

} // namespace Potato
