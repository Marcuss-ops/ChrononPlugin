#pragma once

// text_focus_animators.hpp — lowering of a static editorial focus profile to
// canonical TextAnimatorSpec primitives. Split out of text_focus_resolver.hpp
// (umbrella header).

#include "text_focus_sampling.hpp"

#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/text/animation/text_animator_spec.hpp>

#include <string>
#include <vector>

namespace chrononplugin::typography {

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

}  // namespace chrononplugin::typography
