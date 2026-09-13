#pragma once

#include <cmath>
#include <cstdlib>

namespace Potato {

// Mathematical constants
constexpr float PI = 3.14159265358979323846f;
constexpr float TWO_PI = 2.0f * PI;
constexpr float HALF_PI = PI * 0.5f;
constexpr float DEG_TO_RAD = PI / 180.0f;
constexpr float RAD_TO_DEG = 180.0f / PI;
constexpr float EPSILON = 1e-6f;

// Utility functions
inline float DegToRad(float degrees) {
    return degrees * DEG_TO_RAD;
}

inline float RadToDeg(float radians) {
    return radians * RAD_TO_DEG;
}

inline float Clamp(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

inline float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

inline float Abs(float value) {
    return std::abs(value);
}

inline float Sqrt(float value) {
    return std::sqrt(value);
}

inline float Sin(float radians) {
    return std::sin(radians);
}

inline float Cos(float radians) {
    return std::cos(radians);
}

inline float Tan(float radians) {
    return std::tan(radians);
}

inline float ASin(float value) {
    return std::asin(value);
}

inline float ACos(float value) {
    return std::acos(value);
}

inline float ATan(float value) {
    return std::atan(value);
}

inline float ATan2(float y, float x) {
    return std::atan2(y, x);
}

inline float Min(float a, float b) {
    return (a < b) ? a : b;
}

inline float Max(float a, float b) {
    return (a > b) ? a : b;
}

inline int RandomInt(int min, int max) {
    return min + (rand() % (max - min + 1));
}

inline float RandomFloat(float min, float max) {
    return min + (static_cast<float>(rand()) / RAND_MAX) * (max - min);
}

} // namespace Potato
