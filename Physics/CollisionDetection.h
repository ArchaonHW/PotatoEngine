#pragma once

// ============================================================================
// 碰撞偵測 narrowphase 原語
//
// 技術來源:
//  - OBB-OBB: Separating Axis Theorem (SAT),15 條候選軸
//    (3 面法線 x2 + 9 邊叉積),Ericson "Real-Time Collision Detection" §4.4.1
//  - Sphere-OBB: 球心轉入 box 局部空間後 clamp 求最近點
// ============================================================================

#include "MathUtils/Vector3.h"
#include "MathUtils/Quaternion.h"
#include <cmath>
#include <algorithm>

namespace Potato {

/**
 * 定向包圍盒(OBB):中心 + 半徑向量 + 三個正交軸
 */
struct OBB {
    Vector3 center;
    Vector3 halfExtents;   // 各軸半長
    Vector3 axes[3];       // 正規化世界空間軸

    // 由中心/半徑/旋轉建構(axes 取旋轉後的三個基底向量)
    static OBB FromTransform(const Vector3& center, const Vector3& halfExtents,
                             const Quaternion& rotation) {
        OBB box;
        box.center = center;
        box.halfExtents = halfExtents;
        box.axes[0] = rotation * Vector3(1.0f, 0.0f, 0.0f);
        box.axes[1] = rotation * Vector3(0.0f, 1.0f, 0.0f);
        box.axes[2] = rotation * Vector3(0.0f, 0.0f, 1.0f);
        return box;
    }

    // 世界空間最近點
    Vector3 ClosestPoint(const Vector3& point) const {
        Vector3 d = point - center;
        Vector3 result = center;
        for (int i = 0; i < 3; ++i) {
            float dist = d.Dot(axes[i]);
            float e = (i == 0) ? halfExtents.x : (i == 1) ? halfExtents.y : halfExtents.z;
            dist = std::max(-e, std::min(dist, e));
            result += axes[i] * dist;
        }
        return result;
    }
};

/**
 * SAT 碰撞結果:是否相交 + 最小穿透軸/深度
 */
struct SATResult {
    bool intersects = false;
    Vector3 normal;          // 從 B 指向 A 的最小穿透軸(世界空間)
    float depth = 0.0f;      // 最小穿透深度
};

namespace detail {

// 取得 Vector3 的第 i 個分量
inline float Comp(const Vector3& v, int i) {
    return (i == 0) ? v.x : (i == 1) ? v.y : v.z;
}

} // namespace detail

/**
 * OBB-OBB SAT 測試(Ericson §4.4.1)
 * 15 條軸:A 面法線 x3、B 面法線 x3、A×B 邊叉積 x9
 * 若任一軸上投影分離則不相交;否則回傳最小穿透軸與深度
 */
inline SATResult TestOBBvsOBB(const OBB& a, const OBB& b) {
    SATResult result;
    result.intersects = false;
    result.depth = 1e30f;

    // R[i][j] = A.axes[i] · B.axes[j];AbsR 避免近平行軸的數值不穩
    float R[3][3], AbsR[3][3];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            R[i][j] = a.axes[i].Dot(b.axes[j]);
            AbsR[i][j] = std::fabs(R[i][j]) + 1e-5f; // epsilon 防止邊叉積退化
        }
    }

    // t = B 中心相對 A,表示在 A 的座標系
    Vector3 tVec = b.center - a.center;
    float t[3] = { tVec.Dot(a.axes[0]), tVec.Dot(a.axes[1]), tVec.Dot(a.axes[2]) };

    const float* ea = &a.halfExtents.x;
    const float* eb = &b.halfExtents.x;

    // 記錄最小穿透軸(用於 MTV)
    float bestDepth = 1e30f;
    Vector3 bestAxis(0, 1, 0);

    auto tryAxis = [&](const Vector3& worldAxis, float ra, float rb, float distAlong) -> bool {
        float dist = std::fabs(distAlong);
        float overlap = ra + rb - dist;
        if (overlap < 0.0f) return false;      // 分離軸存在
        if (overlap < bestDepth && worldAxis.LengthSquared() > 1e-8f) {
            bestDepth = overlap;
            bestAxis = worldAxis;
            // 法線統一指向 A 側(distAlong 是 B-A 在該軸的投影)
            if (distAlong > 0.0f) bestAxis = -bestAxis;
        }
        return true;
    };

    // A 的 3 條面法線
    for (int i = 0; i < 3; ++i) {
        float ra = ea[i];
        float rb = eb[0] * AbsR[i][0] + eb[1] * AbsR[i][1] + eb[2] * AbsR[i][2];
        if (!tryAxis(a.axes[i], ra, rb, t[i])) return result;
    }

    // B 的 3 條面法線
    for (int j = 0; j < 3; ++j) {
        float ra = ea[0] * AbsR[0][j] + ea[1] * AbsR[1][j] + ea[2] * AbsR[2][j];
        float rb = eb[j];
        float tj = tVec.Dot(b.axes[j]);
        if (!tryAxis(b.axes[j], ra, rb, tj)) return result;
    }

    // 9 條邊叉積軸 Ai × Bj
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            Vector3 axis = a.axes[i].Cross(b.axes[j]);
            float ra = ea[(i + 1) % 3] * AbsR[(i + 2) % 3][j]
                     + ea[(i + 2) % 3] * AbsR[(i + 1) % 3][j];
            float rb = eb[(j + 1) % 3] * AbsR[i][(j + 2) % 3]
                     + eb[(j + 2) % 3] * AbsR[i][(j + 1) % 3];
            // t·L = t[i+2]*R[i+1][j] - t[i+1]*R[i+2][j]
            float tCross = t[(i + 2) % 3] * R[(i + 1) % 3][j]
                         - t[(i + 1) % 3] * R[(i + 2) % 3][j];
            if (!tryAxis(axis, ra, rb, tCross)) return result;
        }
    }

    result.intersects = true;
    result.normal = bestAxis.Normalized();
    result.depth = bestDepth;
    return result;
}

/**
 * Sphere-OBB 測試
 */
inline SATResult TestSphereVsOBB(const Vector3& sphereCenter, float sphereRadius,
                                 const OBB& box) {
    SATResult result;

    Vector3 closest = box.ClosestPoint(sphereCenter);
    Vector3 diff = sphereCenter - closest;
    float dist = diff.Length();

    if (dist > sphereRadius) {
        result.intersects = false;
        return result;
    }

    result.intersects = true;
    if (dist > 1e-6f) {
        result.normal = diff / dist;           // box → sphere
        result.depth = sphereRadius - dist;
    } else {
        // 球心在盒內:沿最小穿透面推出
        float bestDepth = 1e30f;
        Vector3 bestAxis(0, 1, 0);
        Vector3 d = sphereCenter - box.center;
        for (int i = 0; i < 3; ++i) {
            float along = d.Dot(box.axes[i]);
            float pen = detail::Comp(box.halfExtents, i) - std::fabs(along);
            if (pen < bestDepth) {
                bestDepth = pen;
                bestAxis = (along >= 0.0f) ? box.axes[i] : -box.axes[i];
            }
        }
        result.normal = bestAxis;
        result.depth = sphereRadius + bestDepth;
    }
    return result;
}

/**
 * Sphere-Sphere 測試
 */
inline SATResult TestSphereVsSphere(const Vector3& centerA, float radiusA,
                                    const Vector3& centerB, float radiusB) {
    SATResult result;
    Vector3 diff = centerA - centerB;
    float dist = diff.Length();
    float sum = radiusA + radiusB;

    if (dist >= sum) {
        result.intersects = false;
        return result;
    }
    result.intersects = true;
    result.normal = (dist > 1e-6f) ? diff / dist : Vector3(0, 1, 0);
    result.depth = sum - dist;
    return result;
}

/**
 * 線段-球體測試
 * ray: p(t) = origin + dir*t,dir 需正規化,t ∈ [0, maxT]
 * 命中回傳 true,t = 命中距離,normal = 命中點表面法線
 */
inline bool RayVsSphere(const Vector3& origin, const Vector3& dir,
                        const Vector3& center, float radius,
                        float maxT, float& t, Vector3& normal) {
    Vector3 oc = origin - center;
    float b = oc.Dot(dir);
    float c = oc.Dot(oc) - radius * radius;
    float disc = b * b - c;
    if (disc < 0.0f) return false;

    float sq = std::sqrt(disc);
    t = -b - sq;                    // 近端命中點
    if (t < 0.0f) t = -b + sq;      // 原點在球內 → 取遠端
    if (t < 0.0f || t > maxT) return false;

    Vector3 hit = origin + dir * t;
    Vector3 n = hit - center;
    normal = (n.LengthSquared() > 1e-8f) ? n.Normalized() : -dir;
    return true;
}

/**
 * 線段-OBB 測試(slab 法)
 * 把射線投影到 OBB 三軸,求進入/離開區間;tNear 所在軸即命中面法線
 */
inline bool RayVsOBB(const Vector3& origin, const Vector3& dir,
                     const OBB& box, float maxT, float& t, Vector3& normal) {
    float tNear = 0.0f;
    float tFar = maxT;
    int hitAxis = -1;
    float hitSign = 1.0f;

    for (int i = 0; i < 3; ++i) {
        float e = (i == 0) ? box.halfExtents.x
                : (i == 1) ? box.halfExtents.y : box.halfExtents.z;
        float o = (origin - box.center).Dot(box.axes[i]);
        float d = dir.Dot(box.axes[i]);

        if (std::fabs(d) < 1e-8f) {
            // 射線平行此 slab:原點必須在 slab 內
            if (o < -e || o > e) return false;
            continue;
        }
        float t1 = (-e - o) / d;
        float t2 = ( e - o) / d;
        float sign = -1.0f;
        if (t1 > t2) { std::swap(t1, t2); sign = 1.0f; }

        if (t1 > tNear) { tNear = t1; hitAxis = i; hitSign = sign; }
        if (t2 < tFar)  { tFar = t2; }
        if (tNear > tFar) return false;
    }

    if (hitAxis < 0) {
        // 原點在盒內:視為 t=0 命中,法線頂著射線方向
        t = 0.0f;
        normal = -dir;
        return true;
    }
    t = tNear;
    normal = box.axes[hitAxis] * hitSign;
    return true;
}

} // namespace Potato
