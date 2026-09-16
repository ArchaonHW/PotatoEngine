#pragma once

#include "Vector3.h"
#include "Matrix4.h"

namespace Potato {

/**
 * 平面 (ax + by + cz + d = 0),normal 朝內側為正
 */
struct Plane {
    Vector3 normal;
    float d;

    Plane() : normal(Vector3::Zero()), d(0.0f) {}
    Plane(const Vector3& n, float dist) : normal(n), d(dist) {}

    // 點到平面的帶符號距離（正值 = 在平面的 normal 側）
    float Distance(const Vector3& point) const {
        return normal.Dot(point) + d;
    }

    void Normalize() {
        float len = normal.Length();
        if (len > 1e-6f) {
            normal = normal / len;
            d /= len;
        }
    }
};

/**
 * 軸對齊包圍盒
 */
struct AABB {
    Vector3 min;
    Vector3 max;

    AABB() : min(Vector3::Zero()), max(Vector3::Zero()) {}
    AABB(const Vector3& min_, const Vector3& max_) : min(min_), max(max_) {}

    Vector3 GetCenter() const { return (min + max) * 0.5f; }
    Vector3 GetExtents() const { return (max - min) * 0.5f; }

    bool Contains(const Vector3& point) const {
        return point.x >= min.x && point.x <= max.x &&
               point.y >= min.y && point.y <= max.y &&
               point.z >= min.z && point.z <= max.z;
    }
};

/**
 * 視錐體 - 由 view-projection 矩陣提取六個裁剪平面
 * 用於 Frustum Culling(視錐剔除)
 */
class Frustum {
public:
    enum PlaneIndex {
        Left = 0,
        Right,
        Bottom,
        Top,
        Near,
        Far,
        Count
    };

    Plane planes[Count];

    // Gribb-Hartmann 提取法;viewProj 為 column-major 的 projection * view
    static Frustum FromMatrix(const Matrix4& viewProj) {
        Frustum frustum;
        const float* m = viewProj.m;

        // rowN = (m[N], m[N+4], m[N+8], m[N+12])
        frustum.planes[Left]   = Plane(Vector3(m[3] + m[0],  m[7] + m[4],  m[11] + m[8]),  m[15] + m[12]);
        frustum.planes[Right]  = Plane(Vector3(m[3] - m[0],  m[7] - m[4],  m[11] - m[8]),  m[15] - m[12]);
        frustum.planes[Bottom] = Plane(Vector3(m[3] + m[1],  m[7] + m[5],  m[11] + m[9]),  m[15] + m[13]);
        frustum.planes[Top]    = Plane(Vector3(m[3] - m[1],  m[7] - m[5],  m[11] - m[9]),  m[15] - m[13]);
        frustum.planes[Near]   = Plane(Vector3(m[3] + m[2],  m[7] + m[6],  m[11] + m[10]), m[15] + m[14]);
        frustum.planes[Far]    = Plane(Vector3(m[3] - m[2],  m[7] - m[6],  m[11] - m[10]), m[15] - m[14]);

        for (int i = 0; i < Count; ++i) {
            frustum.planes[i].Normalize();
        }
        return frustum;
    }

    bool ContainsPoint(const Vector3& point) const {
        for (int i = 0; i < Count; ++i) {
            if (planes[i].Distance(point) < 0.0f) return false;
        }
        return true;
    }

    // 球體完全在外側才剔除;相交或內含都回傳 true
    bool ContainsSphere(const Vector3& center, float radius) const {
        for (int i = 0; i < Count; ++i) {
            if (planes[i].Distance(center) < -radius) return false;
        }
        return true;
    }

    // AABB 與視錐:對每個平面取「最正向頂點」,若都在平面外側則剔除
    bool ContainsAABB(const AABB& box) const {
        for (int i = 0; i < Count; ++i) {
            const Vector3& n = planes[i].normal;
            Vector3 positive(
                n.x >= 0.0f ? box.max.x : box.min.x,
                n.y >= 0.0f ? box.max.y : box.min.y,
                n.z >= 0.0f ? box.max.z : box.min.z);
            if (planes[i].Distance(positive) < 0.0f) return false;
        }
        return true;
    }
};

} // namespace Potato
