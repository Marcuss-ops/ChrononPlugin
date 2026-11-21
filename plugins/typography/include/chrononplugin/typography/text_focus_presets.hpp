#pragma once

// text_focus_presets.hpp — the text_depth_focus_v1 preset family, its travel
// schedule and the per-word band animators. Split out of
// text_focus_resolver.hpp (umbrella header).

#include "text_focus_animators.hpp"
#include "text_focus_sampling.hpp"

#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/text/animation/text_animator_spec.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chrononplugin::typography {

// ── text_depth_focus_v1 preset family ─────────────────────────────────────

enum class FocusTravelKind {
    WordStatic,    // focus locked on one word
    WordTravel,    // focus travels word 0 → N-1
    NearToFar,     // depth focus sweep, near plane → far plane
    FarToNear,     // depth focus sweep, far plane → near plane
    CenterOut,     // focus from the middle word outward
    EdgesIn,       // focus from the edge words inward
    RackDuo,       // two-word rack focus A ⇄ B ⇄ A
    DepthCascade   // words spread in Z + focus travel
};

/// Stable serialized ids for the text_depth_focus_v1 family.
[[nodiscard]] inline std::string_view focus_travel_id(FocusTravelKind kind) noexcept {
    switch (kind) {
        case FocusTravelKind::WordStatic:   return "text_focus_word_static";
        case FocusTravelKind::WordTravel:   return "text_focus_word_travel";
        case FocusTravelKind::NearToFar:    return "text_focus_near_to_far";
        case FocusTravelKind::FarToNear:    return "text_focus_far_to_near";
        case FocusTravelKind::CenterOut:    return "text_focus_center_out";
        case FocusTravelKind::EdgesIn:      return "text_focus_edges_in";
        case FocusTravelKind::RackDuo:      return "text_focus_rack_duo";
        case FocusTravelKind::DepthCascade: return "text_focus_depth_cascade";
    }
    return "text_focus_word_static";
}

struct FocusPresetOptions {
    Frame start{0};
    Frame duration{48};
    float camera_world_z{0.0f};    // true-depth focus reference plane
    std::size_t focused_word{0};   // WordStatic anchor
    std::size_t rack_a{0};         // RackDuo endpoints
    std::size_t rack_b{1};
    std::size_t stops{8};          // baked focus keyframes per travel
    TextFocusOptions focus{};
    bool depth_cascade{false};     // attach per-word Z depth offsets
};

namespace detail {

/// Resolve a continuous focus position in word-index space for one preset at
/// normalized progress t ∈ [0, 1].
[[nodiscard]] inline float travel_position(
    FocusTravelKind kind, float t, std::size_t word_count,
    const FocusPresetOptions& options) {
    if (word_count == 0) return 0.0f;
    const float last = static_cast<float>(word_count - 1);
    const float mid = static_cast<float>(word_count / 2);
    const std::size_t anchor = std::min(options.focused_word, word_count - 1);
    const std::size_t a = std::min(options.rack_a, word_count - 1);
    const std::size_t b = std::min(options.rack_b, word_count - 1);
    const float edges_in_a = last * t * 0.5f;
    switch (kind) {
        case FocusTravelKind::WordStatic:   return static_cast<float>(anchor);
        case FocusTravelKind::WordTravel:   return last * t;
        case FocusTravelKind::NearToFar:    return last * t;
        case FocusTravelKind::FarToNear:    return last * (1.0f - t);
        case FocusTravelKind::CenterOut:
            // The focus plane starts at the middle word and "travels" outward:
            // the blur basin widens symmetrically around the mid word.
            return mid;
        case FocusTravelKind::EdgesIn:
            // Two focus planes start at the outer words and converge on center.
            return edges_in_a;
        case FocusTravelKind::RackDuo: {
            const float fa = static_cast<float>(a);
            const float fb = static_cast<float>(b);
            return t < 0.5f ? fa + (fb - fa) * (t * 2.0f)
                            : fb + (fa - fb) * ((t - 0.5f) * 2.0f);
        }
        case FocusTravelKind::DepthCascade: return last * t;
    }
    return 0.0f;
}

/// Basin half-width (in words) for the symmetric presets at progress t.
/// CenterOut widens from 0 to `last`, EdgesIn narrows from `last` to 0.
[[nodiscard]] inline float basin_half_width(
    FocusTravelKind kind, float t, std::size_t word_count) {
    if (word_count == 0) return 0.0f;
    const float last = static_cast<float>(word_count - 1);
    switch (kind) {
        case FocusTravelKind::CenterOut: return last * t;
        default:                         return 0.0f;
    }
}

} // namespace detail

/// Lower one text_depth_focus_v1 preset into canonical per-word band
/// animators (one TextAnimatorSpec per word band: blur + opacity + scale
/// keyframed over `stops` focus positions). Each band selects exactly one
/// word via the canonical Word selector; the evaluator's Add blend makes
/// bands independent. O(word_count * stops) spec cost — never O(characters).
[[nodiscard]] inline std::vector<TextAnimatorSpec> make_focus_animators(
    FocusTravelKind kind,
    std::size_t word_count,
    const FocusPresetOptions& options = {}) {
    if (!options.focus.valid())
        throw std::invalid_argument("text focus preset options are outside supported bounds");
    if (options.duration.integral() < 1 || options.duration.integral() > 65535 ||
        options.start.integral() < 0 || options.start.integral() > 1000000 ||
        options.start.integral() + options.duration.integral() > 1000000 ||
        !std::isfinite(options.camera_world_z) || std::abs(options.camera_world_z) > 1000000.0f)
        throw std::invalid_argument("text focus preset timing or camera position is outside supported bounds");

    std::vector<TextAnimatorSpec> result;
    if (word_count == 0) return result;

    // Canonical V1 budget: at most five keyframes per property. The engine
    // interpolates between stops, so five focus positions already produce a
    // continuous plane-of-focus travel.
    const std::size_t available_frames = static_cast<std::size_t>(options.duration.integral()) + 1;
    const std::size_t stop_count = std::min({std::clamp<std::size_t>(options.stops, 2, 5),
                                             available_frames});
    std::vector<std::pair<Frame, TextFocusState>> schedule;
    schedule.reserve(stop_count);
    for (std::size_t s = 0; s < stop_count; ++s) {
        const float t = static_cast<float>(s) /
                        static_cast<float>(stop_count - 1);
        const Frame frame = options.start +
            Frame{static_cast<i64>(std::llround(
                static_cast<double>(options.duration.integral()) * t))};
        TextFocusState state;
        state.focus_progress = t;
        state.focus_position = detail::travel_position(kind, t, word_count, options);
        schedule.push_back({frame, state});
    }
    // The final stop always lands exactly on the terminal focus position so
    // TestFinalFocusExact holds for every travel preset.
    schedule.back().second.focus_position =
        detail::travel_position(kind, 1.0f, word_count, options);

    // Depth is emitted as one selected 2.5D position per word, not a single
    // whole-line transform that silently leaves every word at z=0. A real
    // camera DOF buffer remains layer/pixel scoped in Chronon; these offsets
    // are the glyph-state inputs used by depth-aware downstream consumers.
    const bool cascade = options.depth_cascade ||
                         kind == FocusTravelKind::DepthCascade ||
                         kind == FocusTravelKind::NearToFar ||
                         kind == FocusTravelKind::FarToNear;
    const auto depths = cascade
        ? resolve_word_depths(word_count, options.focus.profile)
        : std::vector<float>{};
    const f32 width = 100.0f / static_cast<f32>(word_count);
    if (cascade) {
        for (std::size_t i = 0; i < word_count; ++i) {
            TextAnimatorSpec depth_spec;
            depth_spec.id = std::string{focus_travel_id(kind)} + "_depth_word_" + std::to_string(i);
            GlyphSelectorSpec selector;
            selector.id = depth_spec.id + "_selector";
            selector.unit = TextSelectorUnit::Word;
            selector.shape = TextSelectorShape::Square;
            selector.order = TextSelectorOrder::Index;
            selector.exclude_spaces = true;
            selector.start = AnimatedValue<f32>{static_cast<f32>(i) * width};
            selector.end = AnimatedValue<f32>{static_cast<f32>(i + 1) * width};
            selector.amount = AnimatedValue<f32>{100.0f};
            depth_spec.selectors.push_back(std::move(selector));
            PositionProperty z_offset;
            z_offset.value = AnimatedValue<Vec3>{Vec3{0.0f, 0.0f, depths[i]}};
            depth_spec.properties.push_back(std::move(z_offset));
            (void)depth_spec.compile();
            result.push_back(std::move(depth_spec));
        }
    }
    for (std::size_t i = 0; i < word_count; ++i) {
        TextAnimatorSpec band;
        band.id = std::string{focus_travel_id(kind)} + "_band_" + std::to_string(i);
        band.enabled = true;

        GlyphSelectorSpec selector;
        selector.id = band.id + "_selector";
        selector.unit = TextSelectorUnit::Word;
        selector.shape = TextSelectorShape::Square;
        selector.order = TextSelectorOrder::Index;
        selector.exclude_spaces = true;
        selector.start = AnimatedValue<f32>{static_cast<f32>(i) * width};
        selector.end = AnimatedValue<f32>{static_cast<f32>(i + 1) * width};
        selector.amount = AnimatedValue<f32>{100.0f};
        band.selectors.push_back(std::move(selector));

        struct StopValues { float blur; float opacity; float scale; };
        std::vector<StopValues> values;
        values.reserve(schedule.size());
        for (const auto& [frame, state] : schedule) {
            TextWordFocusSample sample;
            if (kind == FocusTravelKind::EdgesIn) {
                const float last = static_cast<float>(word_count - 1);
                const float position_a = last * state.focus_progress * 0.5f;
                const float position_b = last - position_a;
                const float distance = std::min(
                    std::fabs(static_cast<float>(i) - position_a),
                    std::fabs(static_cast<float>(i) - position_b));
                sample = resolve_word_focus_distance(distance, options.focus);
            } else if (kind == FocusTravelKind::CenterOut) {
                const float middle = static_cast<float>(word_count / 2);
                const float half_width = detail::basin_half_width(
                    kind, state.focus_progress, word_count);
                const float distance = std::max(
                    std::fabs(static_cast<float>(i) - middle) - half_width, 0.0f);
                sample = resolve_word_focus_distance(distance, options.focus);
            } else if (cascade) {
                const auto& focus_depths = depths;
                const float focus_index = std::clamp(
                    state.focus_position, 0.0f, static_cast<float>(word_count - 1));
                const auto lower = static_cast<std::size_t>(std::floor(focus_index));
                const auto upper = std::min(lower + 1, word_count - 1);
                const float fraction = focus_index - static_cast<float>(lower);
                const float focus_z = focus_depths[lower] +
                    (focus_depths[upper] - focus_depths[lower]) * fraction;
                const float focus_distance = std::fabs(focus_z - options.camera_world_z);
                const auto depth_samples = resolve_word_depth_focus(
                    focus_depths, options.camera_world_z, focus_distance,
                    options.focus.profile.blur_per_depth, options.focus.profile);
                sample = depth_samples[i];
            } else {
                const auto samples = resolve_word_focus(
                    word_count, TextFocusState{state.focus_position, state.focus_progress},
                    options.focus);
                sample = samples[i];
            }
            values.push_back({sample.blur_radius, sample.opacity, sample.scale});
        }
        // set() pins the pre-start hold to the first stop AND suppresses the
        // phantom frame-0 baseline that aggregate-init would otherwise emit
        // on the first add_keyframe, so the keyframe count stays exact.
        BlurProperty blur;
        blur.radius.set(values.front().blur);
        OpacityProperty opacity;
        opacity.value.set(values.front().opacity);
        ScaleProperty scale;
        scale.value.set(Vec3{values.front().scale, values.front().scale, 1.0f});
        for (std::size_t s = 0; s < schedule.size(); ++s) {
            const auto& [frame, state] = schedule[s];
            blur.radius.add_keyframe(frame, values[s].blur);
            opacity.value.add_keyframe(frame, values[s].opacity);
            scale.value.add_keyframe(
                frame, Vec3{values[s].scale, values[s].scale, 1.0f});
        }
        band.properties.push_back(std::move(blur));
        band.properties.push_back(std::move(opacity));
        band.properties.push_back(std::move(scale));
        (void)band.compile();
        result.push_back(std::move(band));
    }
    return result;
}

/// Convenience overload: static focus on one word with zero travel.
[[nodiscard]] inline std::vector<TextAnimatorSpec> make_static_focus_animators(
    std::size_t word_count,
    std::size_t focused_word,
    const TextFocusOptions& options = {}) {
    FocusPresetOptions preset;
    preset.focused_word = focused_word;
    preset.focus = options;
    preset.stops = 2; // start + end, same position (static hold)
    return make_focus_animators(FocusTravelKind::WordStatic, word_count, preset);
}

}  // namespace chrononplugin::typography
