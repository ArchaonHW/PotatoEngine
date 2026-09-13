#pragma once

#include "Vector3.h"
#include "Matrix4.h"
#include "MathUtils.h"

namespace Potato {

class Quaternion {
public:
    float x, y, z, w;

    // Constructors
    Quaternion() : x(0.0f), y(0.0f), z(0.0f), w(1.0f) {}
    Quaternion(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
    Quaternion(const Vector3& axis, float angle) {
        float halfAngle = angle * 0.5f;
        float s = Sin(halfAngle);
        x = axis.x * s;
        y = axis.y * s;
        z = axis.z * s;
        w = Cos(halfAngle);
    }

    // Static factory methods
    static Quaternion Identity() {
        return Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
    }

    static Quaternion FromEulerAngles(float pitch, float yaw, float roll) {
        float cy = Cos(yaw * 0.5f);
        float sy = Sin(yaw * 0.5f);
        float cp = Cos(pitch * 0.5f);
        float sp = Sin(pitch * 0.5f);
        float cr = Cos(roll * 0.5f);
        float sr = Sin(roll * 0.5f);

        Quaternion q;
        q.w = cr * cp * cy + sr * sp * sy;
        q.x = sr * cp * cy - cr * sp * sy;
        q.y = cr * sp * cy + sr * cp * sy;
        q.z = cr * cp * sy - sr * sp * cy;
        return q;
    }

    static Quaternion FromAxisAngle(const Vector3& axis, float angle) {
        return Quaternion(axis, angle);
    }

    // Quaternion operations
    Quaternion operator*(const Quaternion& other) const {
        Quaternion result;
        result.w = w * other.w - x * other.x - y * other.y - z * other.z;
        result.x = w * other.x + x * other.w + y * other.z - z * other.y;
        result.y = w * other.y - x * other.z + y * other.w + z * other.x;
        result.z = w * other.z + x * other.y - y * other.x + z * other.w;
        return result;
    }

    Vector3 operator*(const Vector3& vec) const {
        Vector3 qvec(x, y, z);
        Vector3 uv = qvec.Cross(vec);
        Vector3 uuv = qvec.Cross(uv);
        uv = uv * (2.0f * w);
        uuv = uuv * 2.0f;
        return vec + uv + uuv;
    }

    Quaternion operator*(float scalar) const {
        return Quaternion(x * scalar, y * scalar, z * scalar, w * scalar);
    }

    Quaternion operator+(const Quaternion& other) const {
        return Quaternion(x + other.x, y + other.y, z + other.z, w + other.w);
    }

    Quaternion operator-(const Quaternion& other) const {
        return Quaternion(x - other.x, y - other.y, z - other.z, w - other.w);
    }

    Quaternion operator-() const {
        return Quaternion(-x, -y, -z, -w);
    }

    float Dot(const Quaternion& other) const {
        return x * other.x + y * other.y + z * other.z + w * other.w;
    }

    float Length() const {
        return Sqrt(x * x + y * y + z * z + w * w);
    }

    float LengthSquared() const {
        return x * x + y * y + z * z + w * w;
    }

    Quaternion Normalized() const {
        float len = Length();
        if (len < EPSILON) {
            return Identity();
        }
        return Quaternion(x / len, y / len, z / len, w / len);
    }

    Quaternion Conjugate() const {
        return Quaternion(-x, -y, -z, w);
    }

    Quaternion Inverse() const {
        float lenSq = LengthSquared();
        if (lenSq < EPSILON) {
            return Identity();
        }
        return Conjugate() * (1.0f / lenSq);
    }

    // Spherical linear interpolation
    static Quaternion Slerp(const Quaternion& a, const Quaternion& b, float t) {
        float dot = a.Dot(b);
        
        Quaternion tempB = b;
        if (dot < 0.0f) {
            tempB = -b;
            dot = -dot;
        }

        if (dot > 0.9995f) {
            // Linear interpolation for very close quaternions
            return (a * (1.0f - t) + tempB * t).Normalized();
        }

        float theta0 = ACos(Clamp(dot, -1.0f, 1.0f));
        float theta = theta0 * t;
        float sinTheta = Sin(theta);
        float sinTheta0 = Sin(theta0);

        float s0 = Cos(theta) - dot * sinTheta / sinTheta0;
        float s1 = sinTheta / sinTheta0;

        return a * s0 + tempB * s1;
    }

    // Conversion methods
    Vector3 ToEulerAngles() const {
        Vector3 angles;
        
        // Roll (x-axis rotation)
        float sinr_cosp = 2.0f * (w * x + y * z);
        float cosr_cosp = 1.0f - 2.0f * (x * x + y * y);
        angles.x = ATan2(sinr_cosp, cosr_cosp);
        
        // Pitch (y-axis rotation)
        float sinp = 2.0f * (w * y - z * x);
        if (Abs(sinp) >= 1.0f) {
            angles.y = (sinp > 0.0f) ? HALF_PI : -HALF_PI; // Use 90 degrees if out of range
        } else {
            angles.y = ASin(sinp);
        }
        
        // Yaw (z-axis rotation)
        float siny_cosp = 2.0f * (w * z + x * y);
        float cosy_cosp = 1.0f - 2.0f * (y * y + z * z);
        angles.z = ATan2(siny_cosp, cosy_cosp);
        
        return angles;
    }

    Vector3 ToAxisAngle() const {
        Quaternion q = Normalized();
        float angle = 2.0f * ACos(Clamp(q.w, -1.0f, 1.0f));
        float s = Sqrt(1.0f - q.w * q.w);
        
        Vector3 axis;
        if (s < EPSILON) {
            axis = Vector3(1.0f, 0.0f, 0.0f);
        } else {
            axis = Vector3(q.x / s, q.y / s, q.z / s);
        }
        
        return axis * angle;
    }

    Matrix4 ToMatrix4() const {
        Matrix4 result;
        
        float xx = x * x;
        float yy = y * y;
        float zz = z * z;
        float xy = x * y;
        float xz = x * z;
        float yz = y * z;
        float wx = w * x;
        float wy = w * y;
        float wz = w * z;

        result.m[0] = 1.0f - 2.0f * (yy + zz);
        result.m[1] = 2.0f * (xy + wz);
        result.m[2] = 2.0f * (xz - wy);
        result.m[3] = 0.0f;

        result.m[4] = 2.0f * (xy - wz);
        result.m[5] = 1.0f - 2.0f * (xx + zz);
        result.m[6] = 2.0f * (yz + wx);
        result.m[7] = 0.0f;

        result.m[8] = 2.0f * (xz + wy);
        result.m[9] = 2.0f * (yz - wx);
        result.m[10] = 1.0f - 2.0f * (xx + yy);
        result.m[11] = 0.0f;

        result.m[12] = 0.0f;
        result.m[13] = 0.0f;
        result.m[14] = 0.0f;
        result.m[15] = 1.0f;

        return result;
    }
};

} // namespace Potato
