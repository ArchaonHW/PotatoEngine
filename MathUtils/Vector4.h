#pragma once

#ifndef POTATO_VECTOR4_H
#define POTATO_VECTOR4_H

#include <cmath>

namespace Potato {

class Vector4 {
public:
    float x, y, z, w;
    
    Vector4() : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}
    Vector4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
    
    Vector4 operator+(const Vector4& other) const {
        return Vector4(x + other.x, y + other.y, z + other.z, w + other.w);
    }
    
    Vector4 operator-(const Vector4& other) const {
        return Vector4(x - other.x, y - other.y, z - other.z, w - other.w);
    }
    
    Vector4 operator*(float scalar) const {
        return Vector4(x * scalar, y * scalar, z * scalar, w * scalar);
    }
    
    float Length() const {
        return std::sqrt(x * x + y * y + z * z + w * w);
    }
    
    Vector4 Normalize() const {
        float len = Length();
        if (len > 0.0f) {
            return Vector4(x / len, y / len, z / len, w / len);
        }
        return Vector4(0.0f, 0.0f, 0.0f, 0.0f);
    }
};

} // namespace Potato

#endif // POTATO_VECTOR4_H