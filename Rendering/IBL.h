#pragma once

#include "Core/CoreTypes.h"
#include "MathUtils/Vector2.h"
#include "MathUtils/Vector3.h"
#include "Rendering/OpenGLRenderer.h" // Shader

#include <vector>

namespace Potato {

/**
 * IBL（Image-Based Lighting）— 環境光基建。
 *
 * 兩層切分：
 *  - 純 CPU（headless 可測，全部決定性）：
 *    equirect→cubemap 轉換、irradiance 半球卷積、GGX prefilter
 *    （Hammersley 重要性採樣）、Epic split-sum BRDF LUT、程序化天空。
 *    全走 CPU 而非 cubemap-render shader——結果逐像素可驗證，
 *    無 GL 也能跑完整生成鏈。
 *  - GL 端只負責把 CPU 產出的 face/mip/LUT 上傳成
 *    samplerCube / sampler2D，供 DeferredLighting 的 useIBL 路徑使用。
 *
 * 座標慣例：equirect 輸入 RGBA32F，row 0 = 北極頂端
 *   （u=atan2(z,x)/2π+0.5, v=asin(y)/π+0.5，v=0 頂端）。
 *   cubemap face 順序 GL_TEXTURE_CUBE_MAP_POSITIVE_X..NEGATIVE_Z。
 */
namespace IBL {

// ---- 低差異序列與 GGX 採樣（純 CPU）----
float RadicalInverse(uint32 bits);                 // Van der Corput
Vector2 Hammersley(uint32 i, uint32 n);            // (i/n, radicalInverse)
// 對 GGX 半向量做重要性採樣：回傳 world-space H
Vector3 ImportanceSampleGGX(const Vector2& xi, const Vector3& n,
                            float roughness);

// ---- BRDF LUT（Epic split-sum 第二項；純 CPU）----
float GeometrySchlickGGX_IBL(float ndotx, float roughness); // k=(r+1)²/8
float GeometrySmithIBL(float ndotv, float ndotl, float roughness);
// (scale, bias)；samples 越大越準（64² LUT 配 256+ 可接受）
Vector2 IntegrateBRDF(float ndotv, float roughness, int samples = 256);
// size×size RG32F 像素資料（row 0 = roughness 0）
std::vector<float> BuildBRDFLUT(int size, int samples = 256);

// ---- Equirect / cubemap CPU 端（純 CPU）----
// 雙線性採樣 equirect RGBA32F；dir 不需正規化（內部 normalize）
Vector3 SampleEquirect(const float* rgba, int w, int h, const Vector3& dir);
// equirect → 單一 cubemap face（size×size RGBA32F）；face 0..5
std::vector<float> EquirectToCubeFace(const float* rgba, int w, int h,
                                      int face, int size);
// irradiance 半球卷積 → 單一 face（輸出 size×size RGBA32F）
std::vector<float> ConvolveIrradianceFace(const float* rgba, int w, int h,
                                          int face, int size,
                                          int thetaSteps = 24,
                                          int phiSteps = 48);
// GGX prefilter（split-sum N=V=R 近似）→ 單一 face 單一 mip
std::vector<float> PrefilterFace(const float* rgba, int w, int h,
                                 int face, int size, float roughness,
                                 int samples = 64);

// face 序 +X,-X,+Y,-Y,+Z,-Z 的 texel→world dir 映射（GL 慣例）
Vector3 CubeFaceDir(int face, float u, float v); // u,v ∈ [-1,1]

// ---- 程序化 HDR 天空（免 .hdr 載入器的測試/原型用環境）----
// 天頂藍→地平線暖灰漸層 + sunDir 方向的高 HDR 太陽盤。
// 輸出 w×h RGBA32F。
std::vector<float> ProceduralSkyEquirect(int w, int h,
                                         const Vector3& sunDir,
                                         float sunIntensity = 15.f);

} // namespace IBL

/**
 * EnvironmentMaps — IBL 紋理組的 GL 持有端。
 *
 *   EnvironmentMaps env;
 *   env.Create();                       // 需 GL context
 *   auto sky = IBL::ProceduralSkyEquirect(256, 128, {0.3,0.7,-0.5});
 *   env.Generate(sky.data(), 256, 128); // CPU 生成 + 上傳
 *
 * 產出四張圖：env cubemap（skybox 可直接用）、irradiance cubemap、
 * prefiltered cubemap（roughness 對應 mip）、BRDF LUT（RG32F）。
 * Create 需 GL context；Generate 前段為 CPU（無 context 可先
 * 用 IBL:: 命名空間函式自行產生資料）。禁拷貝、可 move。
 */
class EnvironmentMaps {
public:
    static constexpr int kMaxPrefilterMips = 5; // 64/32/16/8/4

    EnvironmentMaps() = default;
    ~EnvironmentMaps();

    EnvironmentMaps(const EnvironmentMaps&) = delete;
    EnvironmentMaps& operator=(const EnvironmentMaps&) = delete;
    EnvironmentMaps(EnvironmentMaps&& o) noexcept { MoveFrom(o); }
    EnvironmentMaps& operator=(EnvironmentMaps&& o) noexcept {
        if (this != &o) {
            Destroy();
            MoveFrom(o);
        }
        return *this;
    }

    // cubeSize：env cubemap 解析度；irradiance 固定 32²、
    // prefilter 基底 64²×5 mips
    bool Create(int cubeSize = 256);
    void Destroy();
    bool IsValid() const { return envCube != 0; }

    // 從 equirect RGBA32F 生成全套（CPU 計算 + GL 上傳）。
    // lutSize/lutSamples 控制 BRDF LUT 精度。需已 Create。
    bool Generate(const float* equirectRGBA, int w, int h,
                  int lutSize = 64, int lutSamples = 256);

    uint32 GetEnvCubemap() const { return envCube; }
    uint32 GetIrradianceMap() const { return irrCube; }
    uint32 GetPrefilterMap() const { return preCube; }
    uint32 GetBRDFLUT() const { return brdfLUT; }
    int GetCubeSize() const { return cubeSize; }

private:
    void MoveFrom(EnvironmentMaps& o) noexcept;
    void UploadCubeFace(uint32 tex, int face, int size,
                        const float* rgba);
    void UploadCubeFaceMip(uint32 tex, int face, int size, int level,
                           const float* rgba);

    uint32 envCube = 0;
    uint32 irrCube = 0;
    uint32 preCube = 0;
    uint32 brdfLUT = 0;
    int cubeSize = 0;
};

} // namespace Potato
