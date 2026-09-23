#pragma once

/**
 * Tween / Easing - 插值動畫工具（header-only）
 *
 * Easing 函數採 Robert Penner 標準曲線;t 皆為 [0,1] 正規化進度。
 * Tween 為輕量值語義物件：Update(dt) 推進、Value() 取插值結果，
 * 可由 Time/Animation/Gameplay 層持有使用。
 *
 * 決定性：全部 float 純函數——同平台 IEEE754 下逐位可重現;
 * 跨平台 lockstep 用時注意編譯器 FP 設定差異。
 */

#include <cmath>
#include <cstdint>

namespace Potato {
namespace Tween {

enum class Ease : uint8_t {
    Linear,
    QuadIn, QuadOut, QuadInOut,
    CubicIn, CubicOut, CubicInOut,
    SineIn, SineOut, SineInOut,
    ExpoIn, ExpoOut, ExpoInOut,
    BackIn, BackOut, BackInOut,
    BounceOut,
    ElasticOut,
};

constexpr float kPi = 3.14159265358979323846f;

// t ∈ [0,1] → eased 進度(BackIn 會 <0、BackOut/ElasticOut 會 >1,屬預期)
inline float Evaluate(Ease e, float t) {
    switch (e) {
    case Ease::Linear: return t;
    case Ease::QuadIn: return t * t;
    case Ease::QuadOut: return t * (2.0f - t);
    case Ease::QuadInOut:
        return t < 0.5f ? 2.0f * t * t
                        : -1.0f + (4.0f - 2.0f * t) * t;
    case Ease::CubicIn: return t * t * t;
    case Ease::CubicOut: {
        const float u = t - 1.0f;
        return u * u * u + 1.0f;
    }
    case Ease::CubicInOut:
        return t < 0.5f ? 4.0f * t * t * t
                        : (t - 1.0f) * (2.0f * t - 2.0f) *
                              (2.0f * t - 2.0f) + 1.0f;
    case Ease::SineIn: return 1.0f - std::cos(t * kPi * 0.5f);
    case Ease::SineOut: return std::sin(t * kPi * 0.5f);
    case Ease::SineInOut: return -0.5f * (std::cos(kPi * t) - 1.0f);
    case Ease::ExpoIn:
        return t <= 0.0f ? 0.0f : std::pow(2.0f, 10.0f * (t - 1.0f));
    case Ease::ExpoOut:
        return t >= 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
    case Ease::ExpoInOut:
        if (t <= 0.0f) return 0.0f;
        if (t >= 1.0f) return 1.0f;
        return t < 0.5f
                   ? 0.5f * std::pow(2.0f, 20.0f * t - 10.0f)
                   : 1.0f - 0.5f * std::pow(2.0f, -20.0f * t + 10.0f);
    case Ease::BackIn: {
        constexpr float s = 1.70158f;
        return t * t * ((s + 1.0f) * t - s);
    }
    case Ease::BackOut: {
        constexpr float s = 1.70158f;
        const float u = t - 1.0f;
        return u * u * ((s + 1.0f) * u + s) + 1.0f;
    }
    case Ease::BackInOut: {
        constexpr float s = 1.70158f * 1.525f;
        if (t < 0.5f) {
            const float u = 2.0f * t;
            return 0.5f * u * u * ((s + 1.0f) * u - s);
        }
        const float u = 2.0f * t - 2.0f;
        return 0.5f * (u * u * ((s + 1.0f) * u + s) + 2.0f);
    }
    case Ease::BounceOut: {
        if (t < 1.0f / 2.75f) return 7.5625f * t * t;
        if (t < 2.0f / 2.75f) {
            const float u = t - 1.5f / 2.75f;
            return 7.5625f * u * u + 0.75f;
        }
        if (t < 2.5f / 2.75f) {
            const float u = t - 2.25f / 2.75f;
            return 7.5625f * u * u + 0.9375f;
        }
        const float u = t - 2.625f / 2.75f;
        return 7.5625f * u * u + 0.984375f;
    }
    case Ease::ElasticOut: {
        if (t <= 0.0f) return 0.0f;
        if (t >= 1.0f) return 1.0f;
        return std::pow(2.0f, -10.0f * t) *
               std::sin((t * 10.0f - 0.75f) * (2.0f * kPi / 3.0f)) + 1.0f;
    }
    }
    return t;
}

/**
 * 值插補器：from→to 於 duration 秒內依 easing 曲線推進。
 * delay 秒後才開始;Loop() 可循環。
 */
class Tween {
public:
    Tween() = default;
    Tween(float from, float to, float duration, Ease ease = Ease::Linear,
          float delay = 0.0f)
        : from_(from), to_(to), duration_(duration), delay_(delay),
          ease_(ease) {}

    void Update(float dt) { elapsed_ += dt; }

    // 目前插值;delay 期間回 from,結束後夾在 to
    float Value() const {
        const float t = Progress();
        return from_ + (to_ - from_) * Evaluate(ease_, t);
    }

    // 正規化進度 [0,1](已扣 delay)
    float Progress() const {
        if (elapsed_ <= delay_) return 0.0f;
        const float t = (elapsed_ - delay_) / duration_;
        return t >= 1.0f ? 1.0f : t;
    }

    bool Finished() const { return Progress() >= 1.0f; }
    bool Started() const { return elapsed_ > delay_; }

    void Reset() { elapsed_ = 0.0f; }
    void Restart(float from, float to) {
        from_ = from; to_ = to; elapsed_ = 0.0f;
    }
    // 從目前值平滑轉向新目標（速度不連續但位置連續）
    void Retarget(float newTo, float newDuration) {
        from_ = Value(); to_ = newTo;
        duration_ = newDuration; elapsed_ = delay_ = 0.0f;
    }

    float Duration() const { return duration_; }
    float Elapsed() const { return elapsed_; }

private:
    float from_ = 0.0f, to_ = 0.0f;
    float duration_ = 1.0f;
    float delay_ = 0.0f;
    float elapsed_ = 0.0f;
    Ease ease_ = Ease::Linear;
};

// 便捷函數
inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float EaseLerp(float a, float b, float t, Ease e) {
    return Lerp(a, b, Evaluate(e, t));
}

} // namespace Tween
} // namespace Potato
