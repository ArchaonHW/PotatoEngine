#pragma once

#include "Core/CoreTypes.h"
#include "MathUtils/Vector3.h"
#include "MathUtils/Matrix4.h"
#include "Rendering/OpenGLRenderer.h" // Shader
#include <vector>

namespace Potato {

class GBuffer;

/**
 * SSR 設定（螢幕空間反射——view-space raymarch）。
 */
struct SSRSettings {
    int maxSteps = 64;       // 線性掃描段數
    float maxDistance = 30.0f; // 行進最遠距離（view-space 單位）
    int binarySteps = 6;     // 穿越後二分細化次數
    float thickness = 0.5f;  // 命中深度容差（防漏背面/薄片穿透）
    float intensity = 1.0f;  // 合成時反射乘率
};

/**
 * ScreenSpaceReflection — SSR 後處理 pass。
 *
 * 管線：
 *   1. 場景 GBuffer 寫出 view-space position/normal/albedo/material
 *   2. deferred 光照合成出 HDR sceneTex
 *   3. Compute() 以 reflect(-V,N) 在 view space raymarch：
 *      每步投影回螢幕、與 gPosition 比深度，穿越後二分細化
 *      → 命中處採 sceneTex 當反射色，乘 Schlick F × (1-roughness)
 *        × 朝相機/邊緣淡出 → ssrTex（已加權反射量）
 *   4. Apply() 把 sceneTex + ssrTex*intensity 合成到 resolveTex
 *      ——deferred 管線在 bloom/tonemap 前呼叫
 *
 * 設計切分（同引擎慣例）：
 *   - MarchStepTs / ViewToUV 為純 CPU，headless 可測
 *   - Create/Compute/Apply 需 GL context
 *
 * GL id 唯一所有權：禁拷貝、可 move。
 */
class ScreenSpaceReflection {
public:
    ScreenSpaceReflection() = default;
    ~ScreenSpaceReflection();

    ScreenSpaceReflection(const ScreenSpaceReflection&) = delete;
    ScreenSpaceReflection& operator=(const ScreenSpaceReflection&) = delete;
    ScreenSpaceReflection(ScreenSpaceReflection&& o) noexcept {
        MoveFrom(o);
    }
    ScreenSpaceReflection& operator=(ScreenSpaceReflection&& o) noexcept {
        if (this != &o) {
            Destroy();
            MoveFrom(o);
        }
        return *this;
    }

    // ---- 純 CPU（headless 可測）----
    // 行進參數 t_i = maxDist*(i/steps)^2——二次分布把樣本集中在
    // 片段附近（近處反射解析度高、遠處只需粗探）。
    // steps<=0 或 maxDist<=0 回空；嚴格遞增、首項>0、末項=maxDist。
    static std::vector<float> MarchStepTs(int steps, float maxDist);
    // view-space 點經 proj 投影→螢幕 uv [0,1]；回傳 false=在相機後方
    static bool ViewToUV(const Vector3& viewPos, const Matrix4& proj,
                         float& outU, float& outV);

    // ---- GL 路徑 ----
    bool Create(int w, int h, const SSRSettings& settings = SSRSettings());
    void Destroy();

    bool IsValid() const { return resolveFbo != 0; }
    const SSRSettings& GetSettings() const { return settings; }

    // 從 GBuffer + HDR sceneTex raymarch → GetTexture()
    void Compute(const GBuffer& gbuffer, uint32 sceneTex,
                 const Matrix4& projection);
    // sceneTex + ssrTex*intensity → GetResolvedTexture()
    void Apply(uint32 sceneTex);

    uint32 GetTexture() const { return ssrTex; }         // 加權反射量
    uint32 GetResolvedTexture() const { return resolveTex; } // 合成後

    static const char* FullscreenVertexShader();
    static const char* SSRFragmentShader();
    static const char* CompositeFragmentShader();

private:
    void MoveFrom(ScreenSpaceReflection& o) noexcept;
    static uint32 CreateFloatTarget(int w, int h, uint32& outTex);

    uint32 ssrFbo = 0;
    uint32 ssrTex = 0;
    uint32 resolveFbo = 0;
    uint32 resolveTex = 0;
    uint32 vao = 0;

    UniquePtr<Shader> ssrShader;
    UniquePtr<Shader> compShader;

    SSRSettings settings;
    int width = 0;
    int height = 0;
};

} // namespace Potato
