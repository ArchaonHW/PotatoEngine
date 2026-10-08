#include "AmbientOcclusion.h"
#include "GBuffer.h"
#include "Logging/Logger.h"
#include <glad/glad.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>

namespace Potato {

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr int kMaxKernel = 64; // 對齊 GLSL samples[64] 上限
}

AmbientOcclusion::~AmbientOcclusion() {
    Destroy();
}

void AmbientOcclusion::MoveFrom(AmbientOcclusion& o) noexcept {
    aoFbo = o.aoFbo; aoTex = o.aoTex;
    blurFbo = o.blurFbo; blurTex = o.blurTex;
    noiseTex = o.noiseTex; vao = o.vao;
    ssaoShader = std::move(o.ssaoShader);
    blurShader = std::move(o.blurShader);
    kernel = std::move(o.kernel);
    settings = o.settings;
    width = o.width; height = o.height;
    o.aoFbo = o.aoTex = o.blurFbo = o.blurTex = o.noiseTex = o.vao = 0;
    o.width = o.height = 0;
}

// ============================================================================
// 純 CPU：kernel / noise 生成（headless 可測）
// ============================================================================

std::vector<Vector3> AmbientOcclusion::GenerateKernel(int sampleCount,
                                                    uint32 seed) {
    if (sampleCount <= 0) {
        return {};
    }
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dir(-1.0f, 1.0f);
    std::uniform_real_distribution<float> uni(0.0f, 1.0f);

    std::vector<Vector3> kernel;
    kernel.reserve(static_cast<size_t>(sampleCount));
    for (int i = 0; i < sampleCount; ++i) {
        // 切線空間 +Z 半球（z>=0），方向均勻、沿徑長度隨機
        Vector3 s(dir(rng), dir(rng), uni(rng));
        s = s.Normalized() * uni(rng);
        // 加速分布：i 越後 scale 越大 → 樣本集中片段附近
        const float t = static_cast<float>(i) / sampleCount;
        kernel.push_back(s * (0.1f + 0.9f * t * t));
    }
    return kernel;
}

std::vector<Vector3> AmbientOcclusion::GenerateNoise(int tileSize, uint32 seed) {
    if (tileSize <= 0) {
        return {};
    }
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> angle(0.0f, 2.0f * kPi);

    std::vector<Vector3> noise;
    noise.reserve(static_cast<size_t>(tileSize) * tileSize);
    for (int i = 0; i < tileSize * tileSize; ++i) {
        const float a = angle(rng);
        noise.emplace_back(std::cos(a), std::sin(a), 0.0f);
    }
    return noise;
}

// ============================================================================
// GL 路徑
// ============================================================================

// 單通道 R8 AO 目標：建 FBO + color texture，回傳 fbo id（0=失敗）
uint32 AmbientOcclusion::CreateAOTarget(int w, int h, uint32& outTex) {
    uint32 fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    glGenTextures(1, &outTex);
    glBindTexture(GL_TEXTURE_2D, outTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, outTex, 0);

    const bool ok =
        glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ok) {
        glDeleteTextures(1, &outTex);
        glDeleteFramebuffers(1, &fbo);
        outTex = 0;
        return 0;
    }
    return fbo;
}

bool AmbientOcclusion::Create(int w, int h, const SSAOSettings& s) {
    if (w <= 0 || h <= 0) {
        char msg[96];
        std::snprintf(msg, sizeof(msg),
                      "AmbientOcclusion::Create 非法尺寸 %dx%d", w, h);
        LOG_ERROR(msg);
        return false;
    }
    Destroy();
    settings = s;
    settings.kernelSize = std::clamp(settings.kernelSize, 1, kMaxKernel);
    settings.noiseSize = std::max(settings.noiseSize, 1);

    kernel = GenerateKernel(settings.kernelSize, settings.seed);

    // 旋轉噪聲貼圖：REPEAT 平鋪全螢幕，每 tile 內 kernel 隨機旋轉
    const auto noise = GenerateNoise(settings.noiseSize, settings.seed);
    glGenTextures(1, &noiseTex);
    glBindTexture(GL_TEXTURE_2D, noiseTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, settings.noiseSize,
                 settings.noiseSize, 0, GL_RGB, GL_FLOAT,
                 noise.empty() ? nullptr : &noise[0].x);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    aoFbo = CreateAOTarget(w, h, aoTex);
    blurFbo = CreateAOTarget(w, h, blurTex);
    if (!aoFbo || !blurFbo) {
        LOG_ERROR("AmbientOcclusion::Create AO FBO 不完整");
        Destroy();
        return false;
    }

    ssaoShader = MakeUnique<Shader>();
    if (!ssaoShader->LoadFromSource(FullscreenVertexShader(),
                                    SSAOFragmentShader())) {
        LOG_ERROR("AmbientOcclusion::Create SSAO shader 編譯失敗");
        Destroy();
        return false;
    }
    blurShader = MakeUnique<Shader>();
    if (!blurShader->LoadFromSource(FullscreenVertexShader(),
                                    BlurFragmentShader())) {
        LOG_ERROR("AmbientOcclusion::Create blur shader 編譯失敗");
        Destroy();
        return false;
    }

    glGenVertexArrays(1, &vao);

    width = w;
    height = h;
    return true;
}

void AmbientOcclusion::Destroy() {
    if (vao) {
        glDeleteVertexArrays(1, &vao);
        vao = 0;
    }
    blurShader.reset();
    ssaoShader.reset();
    if (noiseTex) {
        glDeleteTextures(1, &noiseTex);
        noiseTex = 0;
    }
    const uint32 texes[2] = {aoTex, blurTex};
    for (uint32 t : texes) {
        if (t) glDeleteTextures(1, &t);
    }
    aoTex = blurTex = 0;
    const uint32 fbos[2] = {aoFbo, blurFbo};
    for (uint32 f : fbos) {
        if (f) glDeleteFramebuffers(1, &f);
    }
    aoFbo = blurFbo = 0;
    kernel.clear();
    width = height = 0;
}

void AmbientOcclusion::Compute(const GBuffer& gbuffer,
                               const Matrix4& projection) {
    if (!IsValid() || !gbuffer.IsValid()) {
        return;
    }

    // 全螢幕 pass 不做深度測試；呼叫前後狀態不變
    const GLboolean depthWas = glIsEnabled(GL_DEPTH_TEST);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(vao);

    // Pass 1：半球採樣 AO → aoTex
    glBindFramebuffer(GL_FRAMEBUFFER, aoFbo);
    glViewport(0, 0, width, height);
    ssaoShader->Bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gbuffer.GetPositionTexture());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, gbuffer.GetNormalTexture());
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, noiseTex);
    ssaoShader->SetUniformInt("gPosition", 0);
    ssaoShader->SetUniformInt("gNormal", 1);
    ssaoShader->SetUniformInt("texNoise", 2);
    ssaoShader->SetUniformInt("kernelSize", settings.kernelSize);
    ssaoShader->SetUniformFloat("radius", settings.radius);
    ssaoShader->SetUniformFloat("bias", settings.bias);
    ssaoShader->SetUniformFloat("power", settings.power);
    ssaoShader->SetUniformMat4("projection", projection);
    ssaoShader->SetUniformVec2(
        "noiseScale",
        Vector2(width / static_cast<float>(settings.noiseSize),
                height / static_cast<float>(settings.noiseSize)));
    const int samplesLoc = glGetUniformLocation(ssaoShader->GetProgramID(),
                                                "samples[0]");
    if (samplesLoc >= 0 && !kernel.empty()) {
        glUniform3fv(samplesLoc, static_cast<int>(kernel.size()),
                     &kernel[0].x);
    }
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // Pass 2：4x4 box blur → blurTex
    glBindFramebuffer(GL_FRAMEBUFFER, blurFbo);
    glViewport(0, 0, width, height);
    blurShader->Bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, aoTex);
    blurShader->SetUniformInt("aoInput", 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // 收尾：還原綁定與深度測試狀態
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
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

const char* AmbientOcclusion::FullscreenVertexShader() {
    return R"(
#version 330 core
// 全螢幕三角：三頂點覆蓋整個 clip space，無需 VBO
out vec2 TexCoords;
void main()
{
    vec2 v = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    TexCoords = v;
    gl_Position = vec4(v.x * 2.0 - 1.0, v.y * 2.0 - 1.0, 0.0, 1.0);
}
)";
}

const char* AmbientOcclusion::SSAOFragmentShader() {
    return R"(
#version 330 core
out float FragColor;
in vec2 TexCoords;

uniform sampler2D gPosition; // view-space 位置（RGB16F）
uniform sampler2D gNormal;   // view-space 法線（RGB16F）
uniform sampler2D texNoise;  // 旋轉噪聲（REPEAT 平鋪）

uniform vec3 samples[64];
uniform int kernelSize;
uniform float radius;
uniform float bias;
uniform float power;
uniform mat4 projection;
uniform vec2 noiseScale;

void main()
{
    vec3 fragPos = texture(gPosition, TexCoords).xyz;
    vec3 normal = texture(gNormal, TexCoords).xyz;

    // 背景像素（幾何 pass clear 後 normal=0）不吃遮蔽
    if (dot(normal, normal) < 1e-6)
    {
        FragColor = 1.0;
        return;
    }
    normal = normalize(normal);

    // Gram-Schmidt 以噪聲向量旋轉 TBN → 每像素隨機 kernel 朝向
    vec3 randomVec = texture(texNoise, TexCoords * noiseScale).xyz;
    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);

    float occlusion = 0.0;
    for (int i = 0; i < kernelSize; ++i)
    {
        // 切線空間 kernel → view-space 半球樣本
        vec3 samplePos = fragPos + (TBN * samples[i]) * radius;

        // 投影回螢幕空間取該像素實際深度
        vec4 offset = projection * vec4(samplePos, 1.0);
        offset.xyz /= offset.w;
        offset.xyz = offset.xyz * 0.5 + 0.5;
        float sampleDepth = texture(gPosition, offset.xy).z;

        // view space 朝 -Z 看：z 越不負越靠近相機。
        // 實際深度更近 → 樣本被遮蔽；rangeCheck 緩解遠處浮剪影響
        float rangeCheck =
            smoothstep(0.0, 1.0, radius / abs(fragPos.z - sampleDepth));
        occlusion += (sampleDepth >= samplePos.z + bias ? 1.0 : 0.0)
                     * rangeCheck;
    }
    float ao = 1.0 - occlusion / float(kernelSize);
    FragColor = pow(clamp(ao, 0.0, 1.0), power);
}
)";
}

const char* AmbientOcclusion::BlurFragmentShader() {
    return R"(
#version 330 core
out float FragColor;
in vec2 TexCoords;

uniform sampler2D aoInput;

void main()
{
    vec2 texel = 1.0 / vec2(textureSize(aoInput, 0));
    float result = 0.0;
    for (int x = -2; x < 2; ++x)
    {
        for (int y = -2; y < 2; ++y)
        {
            result += texture(aoInput,
                              TexCoords + vec2(float(x), float(y)) * texel).r;
        }
    }
    FragColor = result * (1.0 / 16.0);
}
)";
}

const char* AmbientOcclusion::CompositeFragmentShader() {
    return R"(
#version 330 core
out vec4 FragColor;
in vec2 TexCoords;

uniform sampler2D sceneTex; // 已光照場景色
uniform sampler2D aoTex;    // GetAOTexture()

void main()
{
    vec4 scene = texture(sceneTex, TexCoords);
    float ao = texture(aoTex, TexCoords).r;
    FragColor = vec4(scene.rgb * ao, scene.a);
}
)";
}

} // namespace Potato
