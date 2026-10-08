#pragma once

#include "Core/CoreTypes.h"
#include "MathUtils/Vector2.h"
#include "MathUtils/Vector3.h"
#include "Rendering/OpenGLRenderer.h" // Shader
#include <vector>

namespace Potato {

/**
 * HDRRenderTarget — RGBA16F 離屏渲染目標 + depth24/stencil8。
 *
 * 與 RenderTarget（RGBA8）的唯一差異是 color attachment 格式：
 * HDR 場景（PBR/SSAO 合成/多光源累加）可輸出 >1 的值，
 * 供 Bloom 提取亮部與 tone mapping 壓縮。
 *
 * Create 需 GL context 已建立；禁拷貝、可 move。
 */
class HDRRenderTarget {
public:
    HDRRenderTarget() = default;
    ~HDRRenderTarget();

    HDRRenderTarget(const HDRRenderTarget&) = delete;
    HDRRenderTarget& operator=(const HDRRenderTarget&) = delete;
    HDRRenderTarget(HDRRenderTarget&& o) noexcept { MoveFrom(o); }
    HDRRenderTarget& operator=(HDRRenderTarget&& o) noexcept {
        if (this != &o) {
            Destroy();
            MoveFrom(o);
        }
        return *this;
    }

    bool Create(int w, int h);
    void Destroy();

    bool IsValid() const { return fbo != 0; }
    int GetWidth() const { return width; }
    int GetHeight() const { return height; }
    uint32 GetColorTexture() const { return colorTex; }

    void Bind() const;   // FBO + viewport 切目標尺寸
    void Unbind() const; // 回預設 framebuffer（viewport 呼叫方回復）

    // 浮點讀回（Bind 狀態下呼叫；輸出 resize 成 W*H*4）
    bool ReadColor(std::vector<float>& outRGBA) const;

private:
    void MoveFrom(HDRRenderTarget& o) noexcept {
        fbo = o.fbo; colorTex = o.colorTex; depthRb = o.depthRb;
        width = o.width; height = o.height;
        o.fbo = o.colorTex = o.depthRb = 0;
        o.width = o.height = 0;
    }

    uint32 fbo = 0;
    uint32 colorTex = 0;
    uint32 depthRb = 0;
    int width = 0;
    int height = 0;
};

/**
 * Bloom 設定（soft-knee 閾值 + 半解析度可分離 Gaussian ping-pong）。
 */
struct BloomSettings {
    float threshold = 1.0f; // 亮部閾值（HDR luminance）
    float knee = 0.5f;      // soft knee 寬度（0=硬閾）
    int blurPasses = 4;     // blur ping-pong 往返次數
    int downsample = 2;     // bloom 工作解析度 = 螢幕 / downsample
    float sigma = 1.6f;     // Gaussian 標準差（半徑固定 4 → 9-tap）
};

/**
 * Bloom — 亮部泛光後處理。
 *
 * 管線：Extract(sceneTex) 全解析度 soft-knee threshold
 *   → 降採樣到工作解析度 → ping-pong 水平/垂直 Gaussian blur ×N
 *   → GetTexture()（RGBA16F，tone map 時相加）。
 *
 * GaussianWeights 為純 CPU（headless 可測）；Extract 需 GL context。
 */
class Bloom {
public:
    Bloom() = default;
    ~Bloom();

    Bloom(const Bloom&) = delete;
    Bloom& operator=(const Bloom&) = delete;
    Bloom(Bloom&& o) noexcept { MoveFrom(o); }
    Bloom& operator=(Bloom&& o) noexcept {
        if (this != &o) {
            Destroy();
            MoveFrom(o);
        }
        return *this;
    }

    // 純 CPU：回傳 radius+1 個權重（center..edge），
    // 正規化使 w[0] + 2*Σw[1..] == 1。radius<=0 回空。
    static std::vector<float> GaussianWeights(int radius, float sigma);

    bool Create(int w, int h, const BloomSettings& settings = BloomSettings());
    void Destroy();

    bool IsValid() const { return valid; }
    int GetWorkWidth() const { return workW; }
    int GetWorkHeight() const { return workH; }

    // 從 HDR scene texture 提取亮部 + blur 累積進 bloom 貼圖
    void Extract(uint32 sceneTex);

    uint32 GetTexture() const { return bloomTex; } // blur 後的亮部貼圖

    // GLSL 330 源碼（消費端自建 pipeline 可重用）
    static const char* FullscreenVertexShader();
    static const char* ThresholdFragmentShader();
    static const char* BlurFragmentShader();

private:
    void MoveFrom(Bloom& o) noexcept;
    static uint32 CreateFloatTarget(int w, int h, uint32& outTex);

    BloomSettings settings;
    std::vector<float> weights;

    uint32 pingFbo[2] = {0, 0};            // blur ping-pong（工作解析度）
    uint32 pingTex[2] = {0, 0};            // [0] 亦為 threshold 首寫目標
    uint32 bloomTex = 0;                   // == pingTex[最後輸出]
    uint32 vao = 0;

    UniquePtr<Shader> thresholdShader;
    UniquePtr<Shader> blurShader;

    int width = 0, height = 0;
    int workW = 0, workH = 0;
    bool valid = false;
};

/**
 * ToneMapper — HDR → LDR 最終合成。
 *
 * Apply(sceneTex, bloomTex) 畫全螢幕三角到目前綁定的 framebuffer：
 *   color = ACES((scene + bloom*strength) * exposure)，再 gamma 1/2.2。
 * ACESFilmic 提供 CPU 同款（headless 可對拍 GLSL 曲線）。
 */
class ToneMapper {
public:
    bool Create();
    void Destroy();
    bool IsValid() const { return shader != nullptr; }

    // bloomTex = 0 時略過 bloom；呼叫方先綁好目標 FBO + viewport
    void Apply(uint32 sceneTex, uint32 bloomTex = 0,
               float exposure = 1.0f, float bloomStrength = 1.0f) const;

    // Naughty Dog 簡化 ACES 曲線，CPU 版（與 GLSL 同公式，測試對拍）
    static Vector3 ACESFilmic(const Vector3& hdr);
    static const char* FragmentShader();

private:
    UniquePtr<Shader> shader;
};

} // namespace Potato
