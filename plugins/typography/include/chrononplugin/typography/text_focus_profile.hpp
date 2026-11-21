#pragma once

// text_focus_profile.hpp — data contract of the text_depth_focus_v1 authority.
// Split out of text_focus_resolver.hpp (which remains the umbrella header):
// enums, bounded profiles, options and the resolved per-word sample.

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/math/glm_types.hpp>

#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
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

}  // namespace chrononplugin::typography
