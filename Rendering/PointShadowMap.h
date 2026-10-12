#pragma once

#include "Core/CoreTypes.h"
#include "MathUtils/Vector3.h"
#include "MathUtils/Matrix4.h"
#include <vector>

namespace Potato {

/**
 * PointShadowMap — 點光陰影貼圖（GL_TEXTURE_CUBE_MAP 深度方塊）。
 *
 * 與 ShadowMap（方向光正交）/ 聚光透視不同，點光需全向覆蓋：
 * 六個 90° 透視 face 各畫一次場景，深度存「距離/far」線性值
 * （深度 fragment shader 寫 gl_FragDepth），光照端以
 *   fragWorldPos - lightPos 的世界向量直接採樣 cube 比對。
 *
 * 管線：
 *   1. ComputeFaceMatrices() 產生六面 light-space 矩陣（純 CPU）
 *   2. 對 i=0..5：BindFace(i) + Depth*Shader（uniform
 *      lightSpaceMatrix/model/lightPos/farPlane）畫場景
 *   3. 光照端 PointShadowFactor()（SamplingGLSL 供字串拼接）：
 *      世界方向採樣 + 3x3 切平面 PCF
 *
 * Create 需 GL context 已建立；禁拷貝、可 move。
 */
class PointShadowMap {
public:
    PointShadowMap() = default;
    ~PointShadowMap();

    PointShadowMap(const PointShadowMap&) = delete;
    PointShadowMap& operator=(const PointShadowMap&) = delete;
    PointShadowMap(PointShadowMap&& o) noexcept { MoveFrom(o); }
    PointShadowMap& operator=(PointShadowMap&& o) noexcept {
        if (this != &o) {
            Destroy();
            MoveFrom(o);
        }
        return *this;
    }

    // 建立/重建（size = 每面深度貼圖邊長，512/1024 常用）。重入安全。
    bool Create(int size);
    void Destroy();

    bool IsValid() const { return fbo != 0; }
    int GetSize() const { return size; }
    uint32 GetDepthCube() const { return depthCube; }
    uint32 GetFBO() const { return fbo; }

    // 把第 i 面（0..5 → GL_TEXTURE_CUBE_MAP_POSITIVE_X+i）接到 FBO
    // 並切 viewport；呼叫方清深度、畫場景，六面畫完 Unbind。
    void BindFace(int i) const;
    void Unbind() const;

    // ---- 純 CPU（headless 可測）----
    // 六面 light-space 矩陣：LookAt(pos, pos+axis) × Perspective(90°)
    // near=range/100、far=range；pos 為世界空間光源位置。
    static std::vector<Matrix4> ComputeFaceMatrices(const Vector3& pos,
                                                    float range);

    // ---- 內建 GLSL 330 ----
    // 深度 pass：uniform lightSpaceMatrix/model（頂點）+
    //   lightPos/farPlane（片段）；gl_FragDepth = length(world-pos)/far
    static const char* DepthVertexShader();
    static const char* DepthFragmentShader();
    // 光照端片段：PointShadowFactor(fragWorldPos, worldNormal,
    //   lightWorldPos, pointShadowCube, farPlane, bias, texelSize)
    //   → [0,1]（1=全亮）；bias 為 [0,1] 深度比例，內部乘 far 折
    //   世界單位並做法線坡度縮放（同 ShadowFactor 語義）
    static const char* SamplingGLSL();

private:
    void MoveFrom(PointShadowMap& o) noexcept {
        fbo = o.fbo; depthCube = o.depthCube; size = o.size;
        o.fbo = o.depthCube = 0;
        o.size = 0;
    }

    uint32 fbo = 0;
    uint32 depthCube = 0;
    int size = 0;
};

} // namespace Potato
