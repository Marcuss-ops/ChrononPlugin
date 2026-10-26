#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// text_focus_resolver.hpp — TextFocusResolver authority (text_depth_focus_v1)
// ═══════════════════════════════════════════════════════════════════════════
//
// One authority that turns "which word is in focus" into per-word visual state
// (z / blur / opacity / scale), and lowers the text_depth_focus_v1 preset
// family into the canonical TextAnimatorSpec primitives. It owns the focus
// policy ONLY: Chronon3D keeps applying its existing Gaussian blur (per-glyph
// blur property evaluated post-layout, post-MTSDF composition) and the
// existing CameraPlan DOF. No second blur engine, no new runtime stage.
//
// Two resolution modes:
//   1. Editorial focus — blur grows with |word_index - focus_position| from
//      the focused word. The words stay 2D; the focus plane "travels".
//          blur(i) = clamp(|i - focus_position| * blur_scale, 0, max_blur)
//   2. True depth focus — each word sits at a distinct world Z (word depths),
//      and blur follows the camera depth error (mirrors the legacy
//      DepthOfFieldSettings contract in scene/model/camera/camera_common_types.hpp):
//          depth_error = | |word_z - camera_z| - focus_distance |
//          blur        = clamp(depth_error * aperture_scale, 0, max_blur)
//
// Both modes are continuous in `focus_position` / `focus_distance`, so the
// plane of focus interpolates smoothly between words (no pop at word
// boundaries) and lands exactly on the final word at travel end.
//
// Presets (family text_depth_focus_v1):
//   text_focus_word_static    — focus locked on one word
//   text_focus_word_travel    — focus travels word 0 → N-1
//   text_focus_near_to_far    — depth focus sweep, near plane → far plane
//   text_focus_far_to_near    — depth focus sweep, far plane → near plane
//   text_focus_center_out     — focus from the middle word outward
//   text_focus_edges_in       — focus from the edge words inward
//   text_focus_rack_duo       — two-word rack focus A ⇄ B ⇄ A
//   text_focus_depth_cascade  — words spread in Z (depth_step) + focus travel
//
// Anti-duplication: this header is an authoring helper like text_motion.hpp —
// it emits TextAnimatorSpec data over the canonical selector/evaluator
// pipeline. It is not a registry, not a cache, and not a second runtime.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/text/animation/text_animator_spec.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chrononplugin::typography {

using namespace chronon3d;

enum class TextGroupBasis { Word, Span, Line };
enum class FocusCurve { Linear, SmoothStep, Quadratic };

/// Public editorial-focus authoring contract, independent of camera DOF.
/// `group_ids` is required for Span basis and names the existing semantic text
/// spans selected by the canonical glyph selector.
struct TextFocusProfile {
    TextGroupBasis basis{TextGroupBasis::Word};
    float focus_position{0.0f};
    float blur_near{0.0f};
    float blur_far{14.0f};
    float blur_per_unit{3.0f};
    float opacity_falloff{0.20f};
    float scale_falloff{0.015f};
    FocusCurve curve{FocusCurve::Linear};
    std::vector<std::string> group_ids;

    [[nodiscard]] bool valid(std::size_t group_count) const noexcept {
        const bool valid_basis = basis == TextGroupBasis::Word ||
            basis == TextGroupBasis::Span || basis == TextGroupBasis::Line;
        const bool valid_curve = curve == FocusCurve::Linear ||
            curve == FocusCurve::SmoothStep || curve == FocusCurve::Quadratic;
        if (!valid_basis || !valid_curve ||
            !std::isfinite(focus_position) || !std::isfinite(blur_near) ||
            !std::isfinite(blur_far) || !std::isfinite(blur_per_unit) ||
            !std::isfinite(opacity_falloff) || !std::isfinite(scale_falloff) ||
            blur_near < 0.0f || blur_far < blur_near || blur_far > 256.0f ||
            blur_per_unit < 0.0f || blur_per_unit > 256.0f ||
            opacity_falloff < 0.0f || opacity_falloff > 1.0f ||
            scale_falloff < 0.0f || scale_falloff > 0.5f ||
            (group_count > 0 && (focus_position < 0.0f ||
                focus_position > static_cast<float>(group_count - 1))) ||
            (group_count == 0 && focus_position != 0.0f) ||
            group_count > static_cast<std::size_t>(std::numeric_limits<u32>::max()))
            return false;
        if (basis == TextGroupBasis::Span) {
            if (group_ids.size() != group_count) return false;
            for (std::size_t i = 0; i < group_ids.size(); ++i) {
                if (group_ids[i].empty() || group_ids[i].size() > 256) return false;
                for (std::size_t j = 0; j < i; ++j)
                    if (group_ids[i] == group_ids[j]) return false;
            }
        } else if (!group_ids.empty()) {
            return false;
        }
        return true;
    }
};

// ── TextDepthProfile — bounded depth/blur policy knobs ────────────────────

struct TextDepthProfile {
    float depth_step{80.0f};      // world-Z spacing between adjacent words
    float max_depth{640.0f};      // clamp on |word depth| from the first word
    float blur_per_depth{0.012f}; // blur px per world-unit of depth error
    float max_blur{14.0f};        // clamp on resolved blur radius (px)

    [[nodiscard]] bool valid() const noexcept {
        return std::isfinite(depth_step) && depth_step >= 0.0f && depth_step <= 1000000.0f &&
               std::isfinite(max_depth) && max_depth >= 0.0f && max_depth <= 1000000.0f &&
               std::isfinite(blur_per_depth) && blur_per_depth >= 0.0f && blur_per_depth <= 256.0f &&
               std::isfinite(max_blur) && max_blur >= 0.0f && max_blur <= 256.0f;
    }
};

// ── TextFocusState — continuous focus position (word index space) ─────────

struct TextFocusState {
    float focus_position{0.0f};  // continuous word index; 1.5 = between words
    float focus_progress{1.0f};  // 0..1 normalized travel progress
};

// ── TextWordFocusSample — resolved per-word visual state ──────────────────

struct TextWordFocusSample {
    float z{0.0f};            // world depth offset from the first word
    float blur_radius{0.0f};  // gaussian blur radius in pixels
    float opacity{1.0f};
    float scale{1.0f};
};

// ── TextFocusOptions — bounded knobs shared by both modes ─────────────────

struct TextFocusOptions {
    TextDepthProfile profile{};
    float blur_scale{3.0f};        // editorial: blur px per word of distance
    float unfocused_opacity{0.8f}; // floor opacity for far-from-focus words
    float unfocused_scale{0.985f}; // floor scale for far-from-focus words
    float focused_scale{1.0f};     // scale at the focus plane

    [[nodiscard]] bool valid() const noexcept {
        return profile.valid() &&
               std::isfinite(blur_scale) && blur_scale >= 0.0f && blur_scale <= 256.0f &&
               std::isfinite(unfocused_opacity) &&
               unfocused_opacity >= 0.0f && unfocused_opacity <= 1.0f &&
               std::isfinite(unfocused_scale) && unfocused_scale > 0.0f &&
               unfocused_scale <= 1.0f &&
               std::isfinite(focused_scale) && focused_scale > 0.0f && focused_scale <= 4.0f;
    }
};

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

/// Lower a static editorial focus profile to the canonical per-group text
/// animator stack. Layout is not rebuilt or modified: only blur/opacity/scale
/// post-layout properties are emitted. Span selection uses semantic ids.
[[nodiscard]] inline std::vector<TextAnimatorSpec> make_text_focus_animators(
    const TextFocusProfile& profile, std::size_t group_count) {
    const auto samples = resolve_text_focus(profile, group_count);
    std::vector<TextAnimatorSpec> result;
    result.reserve(group_count);

    // Selector positions are unit centers: (index + 0.5) / total_units.
    // The matching [i/N, (i+1)/N] window therefore selects exactly group i
    // when the caller's group_count matches the compiled Word/Line map.
    const f32 group_width = group_count > 0
        ? 100.0f / static_cast<f32>(group_count) : 100.0f;
    for (std::size_t i = 0; i < group_count; ++i) {
        TextAnimatorSpec animator;

        animator.id = "text_focus_" + std::to_string(i);
        GlyphSelectorSpec selector;
        selector.id = animator.id + "_selector";
        selector.shape = TextSelectorShape::Square;
        selector.order = TextSelectorOrder::Forward;
        // Word grouping has no units for separators. A visual line can start
        // with whitespace, so first-glyph whitespace exclusion would wrongly
        // suppress its whole unit; spaces themselves carry no visible glyph
        // effect and should remain part of the line selection.
        selector.exclude_spaces = profile.basis != TextGroupBasis::Line;
        if (profile.basis == TextGroupBasis::Word) {
            selector.unit = TextSelectorUnit::Word;
            selector.start = AnimatedValue<f32>{static_cast<f32>(i) * group_width};
            selector.end = AnimatedValue<f32>{static_cast<f32>(i + 1) * group_width};
        } else if (profile.basis == TextGroupBasis::Line) {
            selector.unit = TextSelectorUnit::Line;
            selector.start = AnimatedValue<f32>{static_cast<f32>(i) * group_width};
            selector.end = AnimatedValue<f32>{static_cast<f32>(i + 1) * group_width};
        } else {
            selector.unit = TextSelectorUnit::Glyph;
            selector.start = AnimatedValue<f32>{0.0f};
            selector.end = AnimatedValue<f32>{100.0f};
            selector.semantic_id = profile.group_ids[i];
        }
        selector.amount = AnimatedValue<f32>{100.0f};
        animator.selectors.push_back(std::move(selector));

        BlurProperty blur;
        blur.radius.set(samples[i].blur_radius);
        OpacityProperty opacity;
        opacity.value.set(samples[i].opacity);
        ScaleProperty scale;
        scale.value.set(Vec3{samples[i].scale, samples[i].scale, 1.0f});
        animator.properties.push_back(std::move(blur));
        animator.properties.push_back(std::move(opacity));
        animator.properties.push_back(std::move(scale));
        (void)animator.compile();
        result.push_back(std::move(animator));
    }
    return result;
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

} // namespace chrononplugin::typography
