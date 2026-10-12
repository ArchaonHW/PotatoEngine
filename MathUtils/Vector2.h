#pragma once

#ifndef POTATO_VECTOR2_H
#define POTATO_VECTOR2_H

#include <cmath>

namespace Potato {

class Vector2 {
public:
    float x, y;
    
    Vector2() : x(0.0f), y(0.0f) {}
    Vector2(float x, float y) : x(x), y(y) {}
    
    Vector2 operator+(const Vector2& other) const {
        return Vector2(x + other.x, y + other.y);
    }
    
    Vector2 operator-(const Vector2& other) const {
        return Vector2(x - other.x, y - other.y);
    }
    
    Vector2 operator*(float scalar) const {
        return Vector2(x * scalar, y * scalar);
    }
    
    float Dot(const Vector2& other) const {
        return x * other.x + y * other.y;
    }
    
    float Length() const {
        return std::sqrt(x * x + y * y);
    }
    
    float LengthSquared() const {
        return x * x + y * y;
    }
    
    Vector2 Normalize() const {
        float len = Length();
        if (len > 0.0f) {
            return Vector2(x / len, y / len);
        }
        return Vector2(0.0f, 0.0f);
    }
    
    Vector2 Normalized() const {
        return Normalize();
    }
};

} // namespace Potato

#endif // POTATO_VECTOR2_H