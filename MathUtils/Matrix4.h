#pragma once

#include "Vector3.h"
#include "Vector4.h"
#include "MathUtils.h"
#include <cstring>

namespace Potato {

class Matrix4 {
public:
    // Matrix elements (column-major for OpenGL compatibility)
    float m[16];

    // Constructors
    Matrix4() {
        m[0] = 1.0f; m[4] = 0.0f; m[8] = 0.0f; m[12] = 0.0f;
        m[1] = 0.0f; m[5] = 1.0f; m[9] = 0.0f; m[13] = 0.0f;
        m[2] = 0.0f; m[6] = 0.0f; m[10] = 1.0f; m[14] = 0.0f;
        m[3] = 0.0f; m[7] = 0.0f; m[11] = 0.0f; m[15] = 1.0f;
    }

    Matrix4(float diagonal) {
        Identity();
        m[0] = m[5] = m[10] = m[15] = diagonal;
    }

    Matrix4(const float* values) {
        std::memcpy(m, values, 16 * sizeof(float));
    }

    // Static factory methods
    static Matrix4 Identity() {
        Matrix4 result;
        result.m[0] = 1.0f; result.m[4] = 0.0f; result.m[8] = 0.0f; result.m[12] = 0.0f;
        result.m[1] = 0.0f; result.m[5] = 1.0f; result.m[9] = 0.0f; result.m[13] = 0.0f;
        result.m[2] = 0.0f; result.m[6] = 0.0f; result.m[10] = 1.0f; result.m[14] = 0.0f;
        result.m[3] = 0.0f; result.m[7] = 0.0f; result.m[11] = 0.0f; result.m[15] = 1.0f;
        return result;
    }

    static Matrix4 Translation(float x, float y, float z) {
        Matrix4 result = Identity();
        result.m[12] = x;
        result.m[13] = y;
        result.m[14] = z;
        return result;
    }

    static Matrix4 RotationX(float angle) {
        Matrix4 result = Identity();
        float c = Cos(angle);
        float s = Sin(angle);
        result.m[5] = c;
        result.m[6] = s;
        result.m[9] = -s;
        result.m[10] = c;
        return result;
    }

    static Matrix4 RotationY(float angle) {
        Matrix4 result = Identity();
        float c = Cos(angle);
        float s = Sin(angle);
        result.m[0] = c;
        result.m[2] = -s;
        result.m[8] = s;
        result.m[10] = c;
        return result;
    }

    static Matrix4 RotationZ(float angle) {
        Matrix4 result = Identity();
        float c = Cos(angle);
        float s = Sin(angle);
        result.m[0] = c;
        result.m[1] = s;
        result.m[4] = -s;
        result.m[5] = c;
        return result;
    }

    static Matrix4 Scale(float x, float y, float z) {
        Matrix4 result = Identity();
        result.m[0] = x;
        result.m[5] = y;
        result.m[10] = z;
        return result;
    }

    static Matrix4 Perspective(float fov, float aspect, float near, float far) {
        Matrix4 result;
        float tanHalfFov = Tan(fov * 0.5f);
        
        result.m[0] = 1.0f / (aspect * tanHalfFov);
        result.m[5] = 1.0f / tanHalfFov;
        result.m[10] = -(far + near) / (far - near);
        result.m[11] = -1.0f;
        result.m[14] = -(2.0f * far * near) / (far - near);
        result.m[15] = 0.0f;
        
        return result;
    }

    static Matrix4 LookAt(const Vector3& eye, const Vector3& center, const Vector3& up) {
        Vector3 f = (center - eye).Normalized();
        Vector3 s = f.Cross(up).Normalized();
        Vector3 u = s.Cross(f);

        Matrix4 result;
        result.m[0] = s.x;
        result.m[4] = s.y;
        result.m[8] = s.z;
        result.m[1] = u.x;
        result.m[5] = u.y;
        result.m[9] = u.z;
        result.m[2] = -f.x;
        result.m[6] = -f.y;
        result.m[10] = -f.z;
        result.m[12] = -s.Dot(eye);
        result.m[13] = -u.Dot(eye);
        result.m[14] = f.Dot(eye);
        result.m[15] = 1.0f;

        return result;
    }

    // Matrix operations
    Matrix4 operator*(const Matrix4& other) const {
        Matrix4 result;
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                result.m[col * 4 + row] = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    result.m[col * 4 + row] += m[k * 4 + row] * other.m[col * 4 + k];
                }
            }
        }
        return result;
    }

    Vector4 operator*(const Vector4& vec) const {
        Vector4 result;
        result.x = m[0] * vec.x + m[4] * vec.y + m[8] * vec.z + m[12] * vec.w;
        result.y = m[1] * vec.x + m[5] * vec.y + m[9] * vec.z + m[13] * vec.w;
        result.z = m[2] * vec.x + m[6] * vec.y + m[10] * vec.z + m[14] * vec.w;
        result.w = m[3] * vec.x + m[7] * vec.y + m[11] * vec.z + m[15] * vec.w;
        return result;
    }

    Vector3 TransformPoint(const Vector3& point) const {
        Vector4 p(point.x, point.y, point.z, 1.0f);
        Vector4 result = (*this) * p;
        return Vector3(result.x, result.y, result.z);
    }

    Vector3 TransformVector(const Vector3& vec) const {
        Vector4 v(vec.x, vec.y, vec.z, 0.0f);
        Vector4 result = (*this) * v;
        return Vector3(result.x, result.y, result.z);
    }

    Matrix4 Transposed() const {
        Matrix4 result;
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                result.m[j * 4 + i] = m[i * 4 + j];
            }
        }
        return result;
    }

    Matrix4 Inverse() const {
        // Simplified inverse for general matrices
        Matrix4 result;
        float inv[16];
        
        inv[0] = m[5]  * m[10] * m[15] - m[5]  * m[11] * m[14] - m[9]  * m[6]  * m[15] + m[9]  * m[7]  * m[14] + m[13] * m[6]  * m[11] - m[13] * m[7]  * m[10];
        inv[4] = -m[4]  * m[10] * m[15] + m[4]  * m[11] * m[14] + m[8]  * m[6]  * m[15] - m[8]  * m[7]  * m[14] - m[12] * m[6]  * m[11] + m[12] * m[7]  * m[10];
        inv[8] = m[4]  * m[9]  * m[15] - m[4]  * m[11] * m[13] - m[8]  * m[5]  * m[15] + m[8]  * m[7]  * m[13] + m[12] * m[5]  * m[11] - m[12] * m[7]  * m[9];
        inv[12] = -m[4]  * m[9]  * m[14] + m[4]  * m[10] * m[13] + m[8]  * m[5]  * m[14] - m[8]  * m[6]  * m[13] - m[12] * m[5]  * m[10] + m[12] * m[6]  * m[9];
        inv[1] = -m[1]  * m[10] * m[15] + m[1]  * m[11] * m[14] + m[9]  * m[2]  * m[15] - m[9]  * m[3]  * m[14] - m[13] * m[2]  * m[11] + m[13] * m[3]  * m[10];
        inv[5] = m[0]  * m[10] * m[15] - m[0]  * m[11] * m[14] - m[8]  * m[2]  * m[15] + m[8]  * m[3]  * m[14] + m[12] * m[2]  * m[11] - m[12] * m[3]  * m[10];
        inv[9] = -m[0]  * m[9]  * m[15] + m[0]  * m[11] * m[13] + m[8]  * m[1]  * m[15] - m[8]  * m[3]  * m[13] - m[12] * m[1]  * m[11] + m[12] * m[3]  * m[9];
        inv[13] = m[0]  * m[9]  * m[14] - m[0]  * m[10] * m[13] - m[8]  * m[1]  * m[14] + m[8]  * m[2]  * m[13] + m[12] * m[1]  * m[10] - m[12] * m[2]  * m[9];
        inv[2] = m[1]  * m[6]  * m[15] - m[1]  * m[7]  * m[14] - m[5]  * m[2]  * m[15] + m[5]  * m[3]  * m[14] + m[13] * m[2]  * m[7]  - m[13] * m[3]  * m[6];
        inv[6] = -m[0]  * m[6]  * m[15] + m[0]  * m[7]  * m[14] + m[4]  * m[2]  * m[15] - m[4]  * m[3]  * m[14] - m[12] * m[2]  * m[7]  + m[12] * m[3]  * m[6];
        inv[10] = m[0]  * m[5]  * m[15] - m[0]  * m[7]  * m[13] - m[4]  * m[1]  * m[15] + m[4]  * m[3]  * m[13] + m[12] * m[1]  * m[7]  - m[12] * m[3]  * m[5];
        inv[14] = -m[0]  * m[5]  * m[14] + m[0]  * m[6]  * m[13] + m[4]  * m[1]  * m[14] - m[4]  * m[2]  * m[13] - m[12] * m[1]  * m[6]  + m[12] * m[2]  * m[5];
        inv[3] = -m[1]  * m[6]  * m[11] + m[1]  * m[7]  * m[10] + m[5]  * m[2]  * m[11] - m[5]  * m[3]  * m[10] - m[9]  * m[2]  * m[7]  + m[9]  * m[3]  * m[6];
        inv[7] = m[0]  * m[6]  * m[11] - m[0]  * m[7]  * m[10] - m[4]  * m[2]  * m[11] + m[4]  * m[3]  * m[10] + m[8]  * m[2]  * m[7]  - m[8]  * m[3]  * m[6];
        inv[11] = -m[0]  * m[5]  * m[11] + m[0]  * m[7]  * m[9]  + m[4]  * m[1]  * m[11] - m[4]  * m[3]  * m[9]  - m[8]  * m[1]  * m[7]  + m[8]  * m[3]  * m[5];
        inv[15] = m[0]  * m[5]  * m[10] - m[0]  * m[6]  * m[9]  - m[4]  * m[1]  * m[10] + m[4]  * m[2]  * m[9]  + m[8]  * m[1]  * m[6]  - m[8]  * m[2]  * m[5];

        float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
        
        if (Abs(det) < EPSILON) {
            return Identity(); // Non-invertible matrix
        }

        det = 1.0f / det;
        for (int i = 0; i < 16; ++i) {
            result.m[i] = inv[i] * det;
        }

        return result;
    }

    float* Data() {
        return m;
    }

    const float* Data() const {
        return m;
    }
};

} // namespace Potato
