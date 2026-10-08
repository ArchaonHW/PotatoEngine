#pragma once

#include "Core/CoreTypes.h"
#include "MathUtils/Vector3.h"
#include "MathUtils/Matrix4.h"
#include "Rendering/OpenGLRenderer.h" // Shader
#include <vector>

namespace Potato {

class GBuffer;

/**
 * SSAO 設定（螢幕空間環境光遮蔽，Crytek 式半球取樣 + 旋轉噪聲）。
 */
struct SSAOSettings {
    int kernelSize = 64;          // 半球採樣數（1..64，GLSL 陣列上限 64）
    float radius = 0.5f;          // 採樣半徑（view-space 單位）
    float bias = 0.025f;          // 深度偏差（抗自遮蔽 acne）
    float power = 1.0f;           // 對比曲線（>1 加深遮蔽）
    int noiseSize = 4;            // 旋轉噪聲貼圖邊長（4 為經典值）
    uint32 seed = 0x9E3779B9u;    // kernel/noise 生成種子（可重現）
};

/**
 * AmbientOcclusion — SSAO 後處理 pass。
 *
 * 管線：
 *   1. 場景以 GBuffer 內建 shader 寫出 view-space position/normal
 *   2. Compute() 對 gPosition/gNormal 做半球採樣 AO → 全螢幕三角
 *   3. 內建 4x4 box blur 去噪 → GetAOTexture()
 *   4. 消費端把 AO 乘進光照結果（CompositeFragmentShader 為現成範例）
 *
 * 設計切分（同引擎慣例）：
 *   - GenerateKernel / GenerateNoise 為純 CPU 決定性生成，headless 可測
 *   - Create/Compute 需 GL context；無 context 呼叫會落在 GL no-op 側
 *     （glGen* 函式指標為 nullptr 時 glad 直接崩，呼叫方須先建 context——
 *      與 RenderTarget::Create 相同前提）
 *
 * GL id 唯一所有權：禁拷貝、可 move。
 */
class AmbientOcclusion {
public:
    AmbientOcclusion() = default;
    ~AmbientOcclusion();

    AmbientOcclusion(const AmbientOcclusion&) = delete;
    AmbientOcclusion& operator=(const AmbientOcclusion&) = delete;
    AmbientOcclusion(AmbientOcclusion&& o) noexcept { MoveFrom(o); }
    AmbientOcclusion& operator=(AmbientOcclusion&& o) noexcept {
        if (this != &o) {
            Destroy();
            MoveFrom(o);
        }
        return *this;
    }

    // ---- 純 CPU（headless 可測）----
    // 切線空間 +Z 半球 kernel：方向均勻、長度 <=1，索引越後長度越大
    // （加速分布把樣本集中在片段附近，遮蔽梯度更銳利）。
    // sampleCount<=0 回空；相同 seed 結果可重現。
    static std::vector<Vector3> GenerateKernel(int sampleCount, uint32 seed);
    // tileSize*tileSize 個 xy 平面單位旋轉向量（z=0）。
    static std::vector<Vector3> GenerateNoise(int tileSize, uint32 seed);

    // ---- GL 路徑 ----
    // 建立 AO/blur FBO + 噪聲貼圖 + 編譯 shader。重入安全（先 Destroy）。
    bool Create(int w, int h, const SSAOSettings& settings = SSAOSettings());
    void Destroy();

    bool IsValid() const { return blurFbo != 0; }
    int GetWidth() const { return width; }
    int GetHeight() const { return height; }
    const SSAOSettings& GetSettings() const { return settings; }

    // 從 GBuffer 的 view-space position/normal 算 AO + blur。
    // 呼叫後 AO 結果在 GetAOTexture()。
    void Compute(const GBuffer& gbuffer, const Matrix4& projection);

    uint32 GetAOTexture() const { return blurTex; }   // 模糊後（照明用）
    uint32 GetRawAOTexture() const { return aoTex; }  // 未模糊（除錯）

    // GLSL 330 源碼（供 Shader::LoadFromSource / 消費端自建 pipeline）
    static const char* FullscreenVertexShader(); // 全螢幕三角（gl_VertexID）
    static const char* SSAOFragmentShader();
    static const char* BlurFragmentShader();
    static const char* CompositeFragmentShader(); // sceneTex * aoTex

private:
    void MoveFrom(AmbientOcclusion& o) noexcept;
    static uint32 CreateAOTarget(int w, int h, uint32& outTex);

    uint32 aoFbo = 0;
    uint32 aoTex = 0;
    uint32 blurFbo = 0;
    uint32 blurTex = 0;
    uint32 noiseTex = 0;
    uint32 vao = 0; // 空 VAO——core profile 全螢幕三角仍需綁定

    UniquePtr<Shader> ssaoShader;   // Shader 不可 move，以 UniquePtr 持有
    UniquePtr<Shader> blurShader;

    std::vector<Vector3> kernel;
    SSAOSettings settings;
    int width = 0;
    int height = 0;
};

} // namespace Potato
