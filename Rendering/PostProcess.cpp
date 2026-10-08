#include "PostProcess.h"
#include "Logging/Logger.h"
#include <glad/glad.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Potato {

namespace {
constexpr int kBlurRadius = 4; // 9-tap 可分離 Gaussian

// 全螢幕三角：綁空 VAO 畫 3 頂點（core profile 需 VAO）
void DrawFullscreenTriangle(uint32 vao) {
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}
} // namespace

// ============================================================================
// HDRRenderTarget
// ============================================================================

HDRRenderTarget::~HDRRenderTarget() {
    Destroy();
}

bool HDRRenderTarget::Create(int w, int h) {
    if (w <= 0 || h <= 0) {
        char msg[96];
        std::snprintf(msg, sizeof(msg),
                      "HDRRenderTarget::Create 非法尺寸 %dx%d", w, h);
        LOG_ERROR(msg);
        return false;
    }
    Destroy(); // 重入安全：resize 直接 Create 新尺寸

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    glGenTextures(1, &colorTex);
    glBindTexture(GL_TEXTURE_2D, colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA,
                 GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, colorTex, 0);

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
        std::snprintf(msg, sizeof(msg),
                      "HDRRenderTarget::Create FBO 不完整 %dx%d", w, h);
        LOG_ERROR(msg);
        Destroy();
        return false;
    }
    width = w;
    height = h;
    return true;
}

void HDRRenderTarget::Destroy() {
    if (depthRb) {
        glDeleteRenderbuffers(1, &depthRb);
        depthRb = 0;
    }
    if (colorTex) {
        glDeleteTextures(1, &colorTex);
        colorTex = 0;
    }
    if (fbo) {
        glDeleteFramebuffers(1, &fbo);
        fbo = 0;
    }
    width = height = 0;
}

void HDRRenderTarget::Bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width, height);
}

void HDRRenderTarget::Unbind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool HDRRenderTarget::ReadColor(std::vector<float>& outRGBA) const {
    if (!IsValid()) return false;
    outRGBA.resize(static_cast<size_t>(width) * height * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_FLOAT, outRGBA.data());
    return true;
}

// ============================================================================
// Bloom
// ============================================================================

Bloom::~Bloom() {
    Destroy();
}

void Bloom::MoveFrom(Bloom& o) noexcept {
    settings = o.settings;
    weights = std::move(o.weights);
    pingFbo[0] = o.pingFbo[0]; pingFbo[1] = o.pingFbo[1];
    pingTex[0] = o.pingTex[0]; pingTex[1] = o.pingTex[1];
    bloomTex = o.bloomTex; vao = o.vao;
    thresholdShader = std::move(o.thresholdShader);
    blurShader = std::move(o.blurShader);
    width = o.width; height = o.height;
    workW = o.workW; workH = o.workH;
    valid = o.valid;
    o.bloomTex = o.vao = 0;
    o.pingFbo[0] = o.pingFbo[1] = o.pingTex[0] = o.pingTex[1] = 0;
    o.width = o.height = o.workW = o.workH = 0;
    o.valid = false;
}

std::vector<float> Bloom::GaussianWeights(int radius, float sigma) {
    if (radius <= 0 || sigma <= 0.0f) {
        return {};
    }
    std::vector<float> w(static_cast<size_t>(radius) + 1);
    const float s2 = 2.0f * sigma * sigma;
    float sum = 0.0f;
    for (int i = 0; i <= radius; ++i) {
        w[i] = std::exp(-static_cast<float>(i * i) / s2);
        sum += (i == 0) ? w[i] : 2.0f * w[i]; // 左右對稱
    }
    for (float& v : w) v /= sum;
    return w;
}

// RGBA16F 目標（bloom 工作貼圖），回傳 fbo id（0=失敗）
uint32 Bloom::CreateFloatTarget(int w, int h, uint32& outTex) {
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

bool Bloom::Create(int w, int h, const BloomSettings& s) {
    if (w <= 0 || h <= 0 || s.downsample <= 0) {
        LOG_ERROR("Bloom::Create 非法參數");
        return false;
    }
    Destroy();
    settings = s;
    weights = GaussianWeights(kBlurRadius, settings.sigma);
    if (weights.empty()) {
        LOG_ERROR("Bloom::Create GaussianWeights 失敗");
        return false;
    }

    workW = std::max(1, w / settings.downsample);
    workH = std::max(1, h / settings.downsample);

    pingFbo[0] = CreateFloatTarget(workW, workH, pingTex[0]);
    pingFbo[1] = CreateFloatTarget(workW, workH, pingTex[1]);
    if (!pingFbo[0] || !pingFbo[1]) {
        LOG_ERROR("Bloom::Create FBO 不完整");
        Destroy();
        return false;
    }

    thresholdShader = MakeUnique<Shader>();
    if (!thresholdShader->LoadFromSource(FullscreenVertexShader(),
                                         ThresholdFragmentShader())) {
        LOG_ERROR("Bloom::Create threshold shader 編譯失敗");
        Destroy();
        return false;
    }
    blurShader = MakeUnique<Shader>();
    if (!blurShader->LoadFromSource(FullscreenVertexShader(),
                                    BlurFragmentShader())) {
        LOG_ERROR("Bloom::Create blur shader 編譯失敗");
        Destroy();
        return false;
    }

    glGenVertexArrays(1, &vao);

    width = w;
    height = h;
    bloomTex = pingTex[0];
    valid = true;
    return true;
}

void Bloom::Destroy() {
    if (vao) {
        glDeleteVertexArrays(1, &vao);
        vao = 0;
    }
    blurShader.reset();
    thresholdShader.reset();
    const uint32 texes[2] = {pingTex[0], pingTex[1]};
    for (uint32 t : texes) {
        if (t) glDeleteTextures(1, &t);
    }
    pingTex[0] = pingTex[1] = bloomTex = 0;
    const uint32 fbos[2] = {pingFbo[0], pingFbo[1]};
    for (uint32 f : fbos) {
        if (f) glDeleteFramebuffers(1, &f);
    }
    pingFbo[0] = pingFbo[1] = 0;
    weights.clear();
    width = height = workW = workH = 0;
    valid = false;
}

void Bloom::Extract(uint32 sceneTex) {
    if (!valid || sceneTex == 0) {
        return;
    }
    const GLboolean depthWas = glIsEnabled(GL_DEPTH_TEST);
    glDisable(GL_DEPTH_TEST);

    // Pass 1：soft-knee threshold 直接降採樣到工作解析度（linear filter
    // 採樣 sceneTex → 免費 box-ish 降採樣），寫入 pingTex[0]
    glBindFramebuffer(GL_FRAMEBUFFER, pingFbo[0]);
    glViewport(0, 0, workW, workH);
    thresholdShader->Bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneTex);
    thresholdShader->SetUniformInt("sceneTex", 0);
    thresholdShader->SetUniformFloat("threshold", settings.threshold);
    thresholdShader->SetUniformFloat("knee", settings.knee);
    DrawFullscreenTriangle(vao);

    // Pass 2：ping-pong 可分離 Gaussian blur（水平↔垂直往返）
    blurShader->Bind();
    blurShader->SetUniformInt("image", 0);
    const int wLoc = glGetUniformLocation(blurShader->GetProgramID(),
                                          "weights[0]");
    if (wLoc >= 0) {
        glUniform1fv(wLoc, static_cast<int>(weights.size()), weights.data());
    }
    bool horizontal = true;
    int dst = 1; // pingTex[0] 已有 threshold 結果，先寫到 pingFbo[1]
    for (int i = 0; i < settings.blurPasses * 2; ++i) {
        glBindFramebuffer(GL_FRAMEBUFFER, pingFbo[dst]);
        glViewport(0, 0, workW, workH);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, pingTex[1 - dst]);
        blurShader->SetUniformInt("horizontal", horizontal ? 1 : 0);
        blurShader->SetUniformVec2(
            "texelSize", Vector2(1.0f / workW, 1.0f / workH));
        DrawFullscreenTriangle(vao);
        horizontal = !horizontal;
        dst = 1 - dst;
    }
    bloomTex = pingTex[1 - dst]; // 最後一次寫入的對面 = 最終結果

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (depthWas) {
        glEnable(GL_DEPTH_TEST);
    }
}

const char* Bloom::FullscreenVertexShader() {
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

const char* Bloom::ThresholdFragmentShader() {
    return R"(
#version 330 core
out vec4 FragColor;
in vec2 TexCoords;

uniform sampler2D sceneTex;
uniform float threshold;
uniform float knee;

// soft-knee（Call of Duty 式）：knee 寬度內二次曲線平滑進入閾值
void main()
{
    vec3 c = texture(sceneTex, TexCoords).rgb;
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));

    float soft = luma - threshold + knee;
    soft = clamp(soft, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 1e-4);
    float contribution = max(soft, luma - threshold)
                       / max(luma, 1e-4);
    FragColor = vec4(c * clamp(contribution, 0.0, 1.0), 1.0);
}
)";
}

const char* Bloom::BlurFragmentShader() {
    return R"(
#version 330 core
out vec4 FragColor;
in vec2 TexCoords;

uniform sampler2D image;
uniform bool horizontal;
uniform vec2 texelSize;
uniform float weights[5];

void main()
{
    vec3 result = texture(image, TexCoords).rgb * weights[0];
    for (int i = 1; i < 5; ++i)
    {
        vec2 off = horizontal ? vec2(texelSize.x * float(i), 0.0)
                              : vec2(0.0, texelSize.y * float(i));
        result += texture(image, TexCoords + off).rgb * weights[i];
        result += texture(image, TexCoords - off).rgb * weights[i];
    }
    FragColor = vec4(result, 1.0);
}
)";
}

// ============================================================================
// ToneMapper
// ============================================================================

Vector3 ToneMapper::ACESFilmic(const Vector3& hdr) {
    // Naughty Dog 近似曲線，與 FragmentShader 同公式
    const float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
    auto f = [&](float x) {
        return std::clamp((x * (a * x + b)) / (x * (c * x + d) + e),
                          0.0f, 1.0f);
    };
    return Vector3(f(hdr.x), f(hdr.y), f(hdr.z));
}

bool ToneMapper::Create() {
    shader = MakeUnique<Shader>();
    return shader->LoadFromSource(Bloom::FullscreenVertexShader(),
                                  FragmentShader());
}

void ToneMapper::Destroy() {
    shader.reset();
}

void ToneMapper::Apply(uint32 sceneTex, uint32 bloomTex,
                       float exposure, float bloomStrength) const {
    if (!shader) {
        return;
    }
    const GLboolean depthWas = glIsEnabled(GL_DEPTH_TEST);
    glDisable(GL_DEPTH_TEST);

    shader->Bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneTex);
    shader->SetUniformInt("sceneTex", 0);
    if (bloomTex) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, bloomTex);
        shader->SetUniformInt("bloomTex", 1);
    }
    shader->SetUniformInt("useBloom", bloomTex ? 1 : 0);
    shader->SetUniformFloat("exposure", exposure);
    shader->SetUniformFloat("bloomStrength", bloomStrength);

    uint32 vao = 0;
    glGenVertexArrays(1, &vao); // 用完即刪：Apply 無狀態物件
    DrawFullscreenTriangle(vao);
    glDeleteVertexArrays(1, &vao);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (depthWas) {
        glEnable(GL_DEPTH_TEST);
    }
}

const char* ToneMapper::FragmentShader() {
    return R"(
#version 330 core
out vec4 FragColor;
in vec2 TexCoords;

uniform sampler2D sceneTex;
uniform sampler2D bloomTex;
uniform bool useBloom;
uniform float exposure;
uniform float bloomStrength;

// Naughty Dog 近似 ACES（與 ToneMapper::ACESFilmic 同公式）
vec3 aces(vec3 x)
{
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    vec3 hdr = texture(sceneTex, TexCoords).rgb;
    if (useBloom)
        hdr += texture(bloomTex, TexCoords).rgb * bloomStrength;
    vec3 c = aces(hdr * exposure);
    c = pow(c, vec3(1.0 / 2.2)); // gamma 校正
    FragColor = vec4(c, 1.0);
}
)";
}

} // namespace Potato
