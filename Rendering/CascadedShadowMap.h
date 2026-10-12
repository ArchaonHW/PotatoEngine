#pragma once

#include "Core/CoreTypes.h"
#include "MathUtils/Matrix4.h"
#include "MathUtils/Vector3.h"

#include <vector>

namespace Potato {

/**
 * CascadedShadowMap — 級聯陰影貼圖（CSM，大場景方向光用）。
 *
 * 單張 GL_TEXTURE_2D_ARRAY depth24（size² × N 層），逐層各自
 * 綁 FBO 渲染深度（不用 geometry shader 分層——GL3.3 保守路線，
 * 每層一次 draw 呼叫）。
 *
 * 切片法：practical split scheme（對數 × 均勻 λ 混合，
 *   λ=0 全均勻、λ=1 全對數，常用 0.5~0.7）。
 * 各級 light-space：切片視錐 8 角點世界座標 → 光空間
 *   min/max 緊密包圍 + texel grid snap（抗閃爍）+ 深度餘量
 *   （遮擋物可能在切片外）。
 *
 * CPU 段（ComputeSplits/FrustumSliceCorners/ComputeSliceLightSpace）
 * 全部 headless 可測。GL 段 Create/BindCascade/Unbind 需 context。
 *
 * 採樣：SamplingGLSL() 提供 ShadowFactorCSM（sampler2DArray +
 *   layer 的 3×3 PCF）；DeferredLighting 的 useCSM 路徑內嵌之。
 *
 * GL id 唯一所有權：禁拷貝、可 move。
 */
class CascadedShadowMap {
public:
    static constexpr int kMaxCascades = 4;

    CascadedShadowMap() = default;
    ~CascadedShadowMap();

    CascadedShadowMap(const CascadedShadowMap&) = delete;
    CascadedShadowMap& operator=(const CascadedShadowMap&) = delete;
    CascadedShadowMap(CascadedShadowMap&& o) noexcept { MoveFrom(o); }
    CascadedShadowMap& operator=(CascadedShadowMap&& o) noexcept {
        if (this != &o) {
            Destroy();
            MoveFrom(o);
        }
        return *this;
    }

    bool Create(int size, int cascades);
    void Destroy();
    bool IsValid() const { return fbo != 0; }

    // 綁定第 i 層為深度輸出（呼叫前可先呼叫 UpdateMatrices）
    void BindCascade(int i) const;
    void Unbind() const;

    uint32 GetDepthArray() const { return depthTex; }
    int GetSize() const { return size; }
    int GetCascadeCount() const { return cascades; }

    // ---- 純 CPU ----
    // 回傳 N+1 個距離邊界 [near ... far]（view-space 正距離）
    static std::vector<float> ComputeSplits(float nearZ, float farZ,
                                            int cascades,
                                            float lambda = 0.6f);
    // view 距離 → NDC z（Perspective 投影）
    static float ViewDistToNdcZ(const Matrix4& proj, float dist);
    // 切片視錐 8 角點世界座標：view/proj 為相機矩陣，
    // distNear/distFar 為 view-space 正距離
    static std::vector<Vector3> FrustumSliceCorners(
        const Matrix4& view, const Matrix4& proj,
        float distNear, float distFar);
    // 8 角點 → 緊密包圍的正交 light-space 矩陣（含 texel snap）
    static Matrix4 ComputeSliceLightSpace(const Vector3& dir,
                                          const Vector3 corners[8],
                                          int mapSize);

    static const char* SamplingGLSL(); // ShadowFactorCSM（array+layer）

private:
    void MoveFrom(CascadedShadowMap& o) noexcept {
        fbo = o.fbo; depthTex = o.depthTex;
        size = o.size; cascades = o.cascades;
        o.fbo = o.depthTex = 0;
        o.size = o.cascades = 0;
    }

    uint32 fbo = 0;
    uint32 depthTex = 0;
    int size = 0;
    int cascades = 0;
};

} // namespace Potato
