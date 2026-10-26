#pragma once

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/text/animation/text_animator_spec.hpp>

#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace chrononplugin::typography {

using namespace chronon3d;

/// Bounded, reusable text motion studies composed from the canonical
/// TextAnimatorSpec and selector evaluator. These are authoring helpers, not a
/// second runtime or preset registry.
enum class MotionPreset {
    Fade,
    Rise,
    Drop,
    SlideLeft,
    SlideRight,
    Pop,
    Blur,
    Rotate,
    Tracking,
    Typewriter,
    WordReveal
};

struct MotionPresetOptions {
    Frame start{0};
    Frame duration{24};
    bool entering{true};
    TextSelectorOrder order{TextSelectorOrder::Forward};
    f32 distance{48.0f};
    f32 blur_radius{12.0f};
    f32 pop_scale{0.72f};
    f32 rotation_degrees{90.0f};
    f32 tracking_pixels{24.0f};
};

[[nodiscard]] inline TextAnimatorSpec make_motion_preset(
    MotionPreset preset, MotionPresetOptions options = {}) {
    if (options.start.integral() < 0 || options.duration.integral() < 1 ||
        options.duration.integral() > 65535 ||
        !std::isfinite(options.distance) || std::abs(options.distance) > 10000.0f ||
        !std::isfinite(options.blur_radius) || options.blur_radius < 0.0f ||
        options.blur_radius > 256.0f || !std::isfinite(options.pop_scale) ||
        options.pop_scale < 0.0f || options.pop_scale > 4.0f ||
        !std::isfinite(options.rotation_degrees) || std::abs(options.rotation_degrees) > 3600.0f ||
        !std::isfinite(options.tracking_pixels) || std::abs(options.tracking_pixels) > 1000.0f)
        throw std::invalid_argument("text motion preset timing or parameter is outside supported bounds");

    const Frame finish = options.start + options.duration;
    const f32 start_weight = options.entering ? 100.0f : 0.0f;
    const f32 end_weight = options.entering ? 0.0f : 100.0f;

    TextAnimatorSpec result;
    switch (preset) {
        case MotionPreset::Fade: result.id = options.entering ? "text.fade_in" : "text.fade_out"; break;
        case MotionPreset::Rise: result.id = options.entering ? "text.rise_in" : "text.rise_out"; break;
        case MotionPreset::Drop: result.id = options.entering ? "text.drop_in" : "text.drop_out"; break;
        case MotionPreset::SlideLeft: result.id = options.entering ? "text.slide_left_in" : "text.slide_left_out"; break;
        case MotionPreset::SlideRight: result.id = options.entering ? "text.slide_right_in" : "text.slide_right_out"; break;
        case MotionPreset::Pop: result.id = options.entering ? "text.pop_in" : "text.pop_out"; break;
        case MotionPreset::Blur: result.id = options.entering ? "text.blur_in" : "text.blur_out"; break;
        case MotionPreset::Rotate: result.id = options.entering ? "text.rotate_in" : "text.rotate_out"; break;
        case MotionPreset::Tracking: result.id = options.entering ? "text.tracking_in" : "text.tracking_out"; break;
        case MotionPreset::Typewriter: result.id = options.entering ? "text.typewriter_in" : "text.typewriter_out"; break;
        case MotionPreset::WordReveal: result.id = options.entering ? "text.word_reveal_in" : "text.word_reveal_out"; break;
    }

    GlyphSelectorSpec selector;
    selector.id = result.id + ".range";
    selector.unit = preset == MotionPreset::WordReveal ? TextSelectorUnit::Word
        : (preset == MotionPreset::Typewriter ? TextSelectorUnit::Grapheme : TextSelectorUnit::Glyph);
    selector.order = options.order;
    if (preset == MotionPreset::Typewriter || preset == MotionPreset::WordReveal) {
        // An opacity-to-zero animator hides the active range. An entrance
        // contracts [start, end] to reveal the run; an exit expands it from an
        // empty range until the complete run is hidden.
        selector.start = AnimatedValue<f32>{0.0f};
        selector.end = AnimatedValue<f32>{100.0f};
        if (options.entering) {
            selector.start.add_keyframe(options.start, 0.0f);
            selector.start.add_keyframe(finish, 100.0f);
        } else {
            selector.start.add_keyframe(options.start, 100.0f);
            selector.start.add_keyframe(finish, 0.0f);
        }
        result.properties.emplace_back(OpacityProperty{AnimatedValue<f32>{0.0f}});
    } else {
        selector.amount.add_keyframe(options.start, start_weight);
        selector.amount.add_keyframe(finish, end_weight);
        switch (preset) {
            case MotionPreset::Fade:
                result.properties.emplace_back(OpacityProperty{AnimatedValue<f32>{0.0f}});
                break;
            case MotionPreset::Rise:
            case MotionPreset::Drop:
            case MotionPreset::SlideLeft:
            case MotionPreset::SlideRight: {
                f32 x = 0.0f;
                f32 y = 0.0f;
                if (preset == MotionPreset::Rise)
                    y = (options.entering ? -1.0f : 1.0f) * std::abs(options.distance);
                else if (preset == MotionPreset::Drop)
                    y = (options.entering ? 1.0f : -1.0f) * std::abs(options.distance);
                else
                    x = (preset == MotionPreset::SlideLeft ? -1.0f : 1.0f) * std::abs(options.distance);
                result.properties.emplace_back(PositionProperty{
                    AnimatedValue<Vec3>{Vec3{x, y, 0.0f}}});
                break;
            }
            case MotionPreset::Pop: {
                const f32 scale = options.entering ? options.pop_scale
                                                   : 2.0f - options.pop_scale;
                result.properties.emplace_back(ScaleProperty{
                    AnimatedValue<Vec3>{Vec3{scale, scale, 1.0f}}});
                break;
            }
            case MotionPreset::Blur:
                result.properties.emplace_back(BlurProperty{AnimatedValue<f32>{options.blur_radius}});
                break;
            case MotionPreset::Rotate: {
                const f32 sign = options.entering ? -1.0f : 1.0f;
                result.properties.emplace_back(RotationProperty{
                    AnimatedValue<Vec3>{Vec3{0.0f, 0.0f, sign * std::abs(options.rotation_degrees)}}});
                break;
            }
            case MotionPreset::Tracking: {
                result.properties.emplace_back(TrackingProperty{
                    AnimatedValue<f32>{std::abs(options.tracking_pixels)}});
                break;
            }
            case MotionPreset::Typewriter:
            case MotionPreset::WordReveal: break;
        }
    }
    result.selectors.push_back(std::move(selector));
    (void)result.compile();
    if (!result.is_valid())
        throw std::logic_error("text motion preset produced an invalid animator specification");
    return result;
}

} // namespace chrononplugin::typography
