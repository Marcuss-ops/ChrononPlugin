#pragma once

// text_focus_sampling.hpp — pure focus resolvers of the text_depth_focus_v1
// authority (editorial distance focus and true-depth focus). Split out of
// text_focus_resolver.hpp (umbrella header).

#include "text_focus_profile.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace chrononplugin::typography {

namespace detail {

[[nodiscard]] inline float clamp_blur(float blur, const TextDepthProfile& profile) noexcept {
    return std::clamp(blur, 0.0f, profile.max_blur);
}

} // namespace detail

// Resolve one word from its non-negative distance to the focus plane; shared
// by single-focus travel and the dual-plane edges-in preset.
[[nodiscard]] inline TextWordFocusSample resolve_word_focus_distance(
    float distance, const TextFocusOptions& options = {}) {
    if (!options.valid() || !std::isfinite(distance) || distance < 0.0f)
        throw std::invalid_argument("text focus distance or options are outside supported bounds");
    TextWordFocusSample sample;
    sample.blur_radius = detail::clamp_blur(distance * options.blur_scale, options.profile);
    const float blur_share = options.profile.max_blur > 0.0f
        ? std::clamp(sample.blur_radius / options.profile.max_blur, 0.0f, 1.0f)
        : 0.0f;
    sample.opacity = 1.0f + (options.unfocused_opacity - 1.0f) * blur_share;
    const float t = options.blur_scale > 0.0f ? blur_share : 0.0f;
    sample.scale = options.focused_scale +
                   (options.unfocused_scale - options.focused_scale) * t;
    return sample;
}

// ── Editorial focus resolver ──────────────────────────────────────────────
//
// blur(i) = clamp(|i - focus_position| * blur_scale, 0, max_blur)
//
// Continuous in focus_position: while the focus plane travels from word 1 to
// word 2, both words become partially sharp (no pop at the boundary) and the
// final word is exactly sharp when focus_position reaches its index.

[[nodiscard]] inline std::vector<TextWordFocusSample> resolve_word_focus(
    std::size_t word_count,
    const TextFocusState& state,
    const TextFocusOptions& options = {}) {
    if (!options.valid())
        throw std::invalid_argument("text focus options are outside supported bounds");
    if (!std::isfinite(state.focus_position) || !std::isfinite(state.focus_progress) ||
        state.focus_progress < 0.0f || state.focus_progress > 1.0f)
        throw std::invalid_argument("text focus state must be finite with progress in [0, 1]");
    if (word_count > 0 &&
        (state.focus_position < 0.0f ||
         state.focus_position > static_cast<float>(word_count - 1)))
        throw std::invalid_argument("text focus position must fall within the word-index range");
    std::vector<TextWordFocusSample> samples(word_count);
    for (std::size_t i = 0; i < word_count; ++i) {
        const float distance = std::fabs(static_cast<float>(i) - state.focus_position);
        samples[i] = resolve_word_focus_distance(distance, options);
    }
    return samples;
}

namespace detail {

[[nodiscard]] inline float focus_curve_distance(
    float distance, float maximum_distance, FocusCurve curve) noexcept {
    if (curve == FocusCurve::Linear || maximum_distance <= 0.0f)
        return distance;
    const float t = std::clamp(distance / maximum_distance, 0.0f, 1.0f);
    switch (curve) {
        case FocusCurve::Linear:
            return distance;
        case FocusCurve::SmoothStep:
            return maximum_distance * t * t * (3.0f - 2.0f * t);
        case FocusCurve::Quadratic:
            return maximum_distance * t * t;
    }
    return distance;
}

} // namespace detail

/// Resolve group-scoped editorial focus for Word, semantic Span or Line groups.
/// The resolver is pure: the caller supplies the evaluated continuous focus
/// position, so direct frame-N sampling is identical to sequential sampling.
[[nodiscard]] inline std::vector<TextWordFocusSample> resolve_text_focus(
    const TextFocusProfile& profile, std::size_t group_count) {
    if (!profile.valid(group_count))
        throw std::invalid_argument("text focus profile or group basis is outside supported bounds");
    std::vector<TextWordFocusSample> samples(group_count);
    for (std::size_t i = 0; i < group_count; ++i) {
        const float raw_distance = std::fabs(static_cast<float>(i) - profile.focus_position);
        const float raw_max_distance = std::max(
            profile.focus_position,
            static_cast<float>(group_count - 1) - profile.focus_position);
        const float distance = detail::focus_curve_distance(
            raw_distance, raw_max_distance, profile.curve);
        auto& sample = samples[i];
        const float max_distance = raw_max_distance;
        const float normalized_distance = max_distance > 0.0f
            ? std::clamp(distance / max_distance, 0.0f, 1.0f) : 0.0f;
        sample.blur_radius = std::clamp(profile.blur_near +
            distance * profile.blur_per_unit, profile.blur_near, profile.blur_far);
        sample.opacity = 1.0f - normalized_distance * profile.opacity_falloff;
        sample.scale = 1.0f - normalized_distance * profile.scale_falloff;
    }
    return samples;
}

// ── Word depth assignment (depth cascade) ─────────────────────────────────
//
// Word i sits at z = -i * depth_step, clamped to |z| <= max_depth, so the
// camera focus_distance addressing stays inside a bounded corridor.

[[nodiscard]] inline std::vector<float> resolve_word_depths(
    std::size_t word_count, const TextDepthProfile& profile = {}) {
    if (!profile.valid())
        throw std::invalid_argument("text depth profile is outside supported bounds");
    std::vector<float> depths(word_count);
    for (std::size_t i = 0; i < word_count; ++i) {
        const float raw = static_cast<float>(i) * profile.depth_step;
        depths[i] = -std::min(raw, profile.max_depth);
    }
    return depths;
}

// ── True depth focus resolver ─────────────────────────────────────────────
//
// The camera sits at `camera_world_z`; word depths are world Z. Distance from
// the camera along the optical axis is |word_z - camera_world_z|, and blur
// follows the legacy DOF contract of DepthOfFieldSettings:
//     depth_error = | |word_z - camera_z| - focus_distance |
//     blur        = clamp(depth_error * aperture_scale, 0, max_blur)
//
// A word exactly at the focus distance is exactly sharp; a farther word is
// always blurrier than a nearer one; pushing the camera forward re-resolves
// focus exactly like a physical lens pull.

[[nodiscard]] inline std::vector<TextWordFocusSample> resolve_word_depth_focus(
    const std::vector<float>& word_depths,
    float camera_world_z,
    float focus_distance,
    float aperture_scale,
    const TextDepthProfile& profile = {}) {
    if (!profile.valid())
        throw std::invalid_argument("text depth profile is outside supported bounds");
    if (!std::isfinite(camera_world_z) || std::abs(camera_world_z) > 1000000.0f ||
        !std::isfinite(focus_distance) || focus_distance < 0.0f || focus_distance > 1000000.0f ||
        !std::isfinite(aperture_scale) || aperture_scale < 0.0f || aperture_scale > 256.0f)
        throw std::invalid_argument("depth focus camera parameters are outside supported bounds");
    std::vector<TextWordFocusSample> samples(word_depths.size());
    for (std::size_t i = 0; i < word_depths.size(); ++i) {
        const float depth = word_depths[i];
        if (!std::isfinite(depth) || std::abs(depth) > 1000000.0f)
            throw std::invalid_argument("word depth must be finite and within supported bounds");
        const float camera_depth = std::fabs(depth - camera_world_z);
        const float depth_error = std::fabs(camera_depth - focus_distance);
        const float raw_blur = depth_error * aperture_scale;
        auto& sample = samples[i];
        sample.z = depth;
        sample.blur_radius = detail::clamp_blur(raw_blur, profile);
        // Subtle companion easing on the same normalized error (0.985 floor
        // mirrors the editorial unfocused_scale default). At zero aperture the
        // whole plane is sharp and scale stays 1.
        const float normalized = profile.max_blur > 0.0f && aperture_scale > 0.0f
            ? std::clamp(sample.blur_radius / profile.max_blur, 0.0f, 1.0f)
            : 0.0f;
        sample.opacity = 1.0f;
        sample.scale = 1.0f + (0.985f - 1.0f) * normalized;
    }
    return samples;
}

}  // namespace chrononplugin::typography
