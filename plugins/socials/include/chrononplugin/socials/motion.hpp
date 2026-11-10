#pragma once

#include <algorithm>
#include <cmath>
#include <string_view>

namespace chrononplugin::socials {

// Small, dependency-free motion vocabulary shared by the social layouts.
// RenderPlan keeps the actual interpolation on the native side: these helpers
// exist to author, preview and test the same curves that the JSON names
// (`out_expo`, `out_back`, `spring`) resolve to inside Chronon3D.
enum class Easing {
    Linear,
    EaseOutExpo,
    EaseOutBack,
    Spring,
};

[[nodiscard]] inline float EaseOutExpo(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return (t >= 1.0f) ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
}

[[nodiscard]] inline float EaseOutBack(float t, float overshoot = 1.70158f) {
    t = std::clamp(t, 0.0f, 1.0f);
    const float f = t - 1.0f;
    return f * f * ((overshoot + 1.0f) * f + overshoot) + 1.0f;
}

[[nodiscard]] inline float DampedSpring(float t, float frequency = 12.0f,
                                        float damping = 0.7f) {
    t = std::clamp(t, 0.0f, 1.0f);
    const float omega = frequency * std::sqrt(std::max(0.0f, 1.0f - damping * damping));
    return 1.0f - std::exp(-damping * frequency * t) * std::cos(omega * t);
}

[[nodiscard]] inline float EvaluateEasing(Easing easing, float t) {
    switch (easing) {
        case Easing::EaseOutExpo: return EaseOutExpo(t);
        case Easing::EaseOutBack: return EaseOutBack(t);
        case Easing::Spring: return DampedSpring(t);
        case Easing::Linear: return std::clamp(t, 0.0f, 1.0f);
    }
    return std::clamp(t, 0.0f, 1.0f);
}

// Wire name consumed by chronon.render-plan.v3 animation tracks.
[[nodiscard]] inline std::string_view RenderPlanEasingName(Easing easing) {
    switch (easing) {
        case Easing::EaseOutExpo: return "out_expo";
        case Easing::EaseOutBack: return "out_back";
        case Easing::Spring: return "spring";
        case Easing::Linear: return "linear";
    }
    return "linear";
}

// A single two-key tween, matching the authored property granularity used by
// the social cards (opacity, translation, scale, progress).
struct AnimProperty {
    float start_value = 0.0f;
    float target_value = 1.0f;
    float start_time = 0.0f;   // seconds
    float duration = 1.0f;     // seconds
    Easing easing = Easing::Linear;

    [[nodiscard]] float Evaluate(float current_time) const {
        if (current_time <= start_time) return start_value;
        if (duration <= 0.0f || current_time >= start_time + duration) return target_value;
        const float t = (current_time - start_time) / duration;
        const float factor = EvaluateEasing(easing, t);
        return start_value + (target_value - start_value) * factor;
    }
};

}  // namespace chrononplugin::socials
