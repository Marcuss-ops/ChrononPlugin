#pragma once

#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/render_plan/easing_contract.hpp>
#include <chronon3d/animation/core/animated_value.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <stdexcept>
#include <utility>
#include <vector>

namespace chrononplugin::effects::presets {
using namespace ::chronon3d;
using namespace ::chronon3d::render_plan;



inline void rgb_split(LayerPlan& layer, float separation = 4.0f) {
    if (!std::isfinite(separation) || separation < 0.0f || separation > 128.0f)
        throw std::invalid_argument("RGB split separation must be in [0, 128]");
    LayerPlan::EffectPlan effect;
    effect.kind = LayerPlan::EffectKindPlan::ChromaticAberration;
    effect.chromatic_red_offset = separation;
    effect.chromatic_blue_offset = -separation;
    layer.effects.push_back(std::move(effect));
}

/// Add the engine's isotropic Gaussian blur as a lightweight lens-softening preset.
inline void lens_blur(LayerPlan& layer, float radius = 8.0f) {
    if (!std::isfinite(radius) || radius < 0.0f || radius > 256.0f)
        throw std::invalid_argument("lens blur radius must be in [0, 256]");
    LayerPlan::EffectPlan effect;
    effect.kind = LayerPlan::EffectKindPlan::GaussianBlur;
    effect.radius = radius;
    layer.effects.push_back(std::move(effect));
}

/// Add canonical organic alpha-boundary perturbation; unlike turbulent
/// displacement this leaves non-boundary RGB/alpha pixels untouched.
inline void roughen_edges(LayerPlan& layer, float amount = 2.0f,
                          float scale = 8.0f, std::uint32_t complexity = 2,
                          float evolution = 0.0f, std::uint32_t seed = 0) {
    if (!std::isfinite(amount) || amount < 0.0f || amount > 64.0f ||
        !std::isfinite(scale) || scale <= 0.0f || scale > 1024.0f ||
        complexity < 1 || complexity > 4 || !std::isfinite(evolution) ||
        std::abs(evolution) > 100000.0f)
        throw std::invalid_argument("roughen edges parameters are outside supported bounds");
    LayerPlan::EffectPlan effect;
    effect.kind = LayerPlan::EffectKindPlan::RoughenEdges;
    effect.amount = amount;
    effect.roughen_scale = scale;
    effect.roughen_complexity = complexity;
    effect.warp_evolution = evolution;
    effect.roughen_seed = seed;
    layer.effects.push_back(std::move(effect));
}

/// Duplicate a source layer and add an ellipse mask plus Gaussian blur. The
/// returned plan is an overlay: render it above the untouched source so the
/// feathered focus region is blurred while the unmasked source remains visible.
[[nodiscard]] inline LayerPlan masked_lens_blur(
    const LayerPlan& source, Vec2 canvas_size, float radius = 12.0f,
    Vec2 center = Vec2{0.5f, 0.5f}, Vec2 size = Vec2{0.55f, 0.70f},
    float feather = 48.0f, std::string suffix = "_lens_blur") {
    if (source.id.empty() || suffix.empty() || suffix.size() > 64 ||
        !std::isfinite(canvas_size.x) || !std::isfinite(canvas_size.y) ||
        canvas_size.x <= 0.0f || canvas_size.y <= 0.0f ||
        !std::isfinite(radius) || radius < 0.0f || radius > 256.0f ||
        !std::isfinite(center.x) || !std::isfinite(center.y) ||
        center.x < 0.0f || center.x > 1.0f || center.y < 0.0f || center.y > 1.0f ||
        !std::isfinite(size.x) || !std::isfinite(size.y) ||
        size.x <= 0.0f || size.x > 1.0f || size.y <= 0.0f || size.y > 1.0f ||
        !std::isfinite(feather) || feather < 0.0f || feather > 1000000.0f)
        throw std::invalid_argument("masked lens blur parameters are outside supported bounds");
    LayerPlan overlay = source;
    overlay.id += suffix;
    overlay.masks.clear();
    overlay.path_mask.reset();
    LayerPlan::PathMaskPlan focus;
    focus.type = "ellipse";
    focus.mode = "add";
    focus.position = {(center.x - 0.5f) * canvas_size.x,
                      (center.y - 0.5f) * canvas_size.y};
    focus.size = {size.x * canvas_size.x, size.y * canvas_size.y};
    focus.feather = feather;
    overlay.masks.push_back(std::move(focus));
    LayerPlan::EffectPlan blur;
    blur.kind = LayerPlan::EffectKindPlan::GaussianBlur;
    blur.radius = radius;
    overlay.effects.push_back(std::move(blur));
    return overlay;
}

struct TextureLayerSpec {
    std::string id;
    std::string asset;
    float opacity{1.0f};
    BlendMode blend{BlendMode::SoftLight};
    float grain{0.0f};
    float contrast{1.0f};
    float saturation{1.0f};
    std::uint32_t seed{0};
};

/// Lower a texture asset to a regular image layer with canonical blend and
/// grading effects. The layer remains editable by all normal RenderPlan tools.
class TextureLayerComposer {
public:
    [[nodiscard]] static LayerPlan compose(const TextureLayerSpec& spec,
                                           Vec2 canvas_size) {
        if (spec.id.empty() || spec.id.size() > 256 || spec.asset.empty() ||
            spec.asset.size() > 4096 || !std::isfinite(canvas_size.x) ||
            !std::isfinite(canvas_size.y) || canvas_size.x <= 0.0f ||
            canvas_size.y <= 0.0f || canvas_size.x > 1000000.0f ||
            canvas_size.y > 1000000.0f || !std::isfinite(spec.opacity) ||
            spec.opacity < 0.0f || spec.opacity > 1.0f ||
            !std::isfinite(spec.grain) || spec.grain < 0.0f || spec.grain > 1.0f ||
            !std::isfinite(spec.contrast) || spec.contrast < 0.0f || spec.contrast > 4.0f ||
            !std::isfinite(spec.saturation) || spec.saturation < 0.0f || spec.saturation > 4.0f)
            throw std::invalid_argument("texture layer composer inputs are outside supported bounds");
        LayerPlan layer;
        layer.id = spec.id;
        layer.type = LayerType::Image;
        layer.asset = spec.asset;
        layer.size = {canvas_size.x, canvas_size.y};
        layer.size_dimensions = 2;
        layer.position = {canvas_size.x * 0.5f, canvas_size.y * 0.5f, 0.0f};
        layer.position_dimensions = 2;
        layer.blend_mode = spec.blend;
        layer.opacity = spec.opacity;
        if (spec.contrast != 1.0f) {
            LayerPlan::EffectPlan effect;
            effect.kind = LayerPlan::EffectKindPlan::Catalog;
            effect.canonical_effect_id = "color.contrast";
            effect.canonical_params.push_back({"value", spec.contrast});
            layer.effects.push_back(std::move(effect));
        }
        if (spec.saturation != 1.0f) {
            LayerPlan::EffectPlan effect;
            effect.kind = LayerPlan::EffectKindPlan::Catalog;
            effect.canonical_effect_id = "color.saturation";
            effect.canonical_params.push_back({"value", spec.saturation});
            layer.effects.push_back(std::move(effect));
        }
        if (spec.grain > 0.0f) {
            LayerPlan::EffectPlan effect;
            effect.kind = LayerPlan::EffectKindPlan::Noise;
            effect.amount = spec.grain;
            effect.seed = spec.seed;
            effect.rgb_noise = true;
            layer.effects.push_back(std::move(effect));
        }
        return layer;
    }
};

struct DocumentaryDepthLookProfile {
    float texture_opacity{0.32f};
    float grain{0.08f};
    float edge_roughness{1.5f};
    float chromatic_aberration{0.75f};
    float vignette{0.28f};
    float focus_blur{0.0f};
    float focus_feather{120.0f};
    std::uint32_t seed{23};
};

/// Compose a deterministic depth treatment from texture plates, foreground
/// grading, an optional feathered blur overlay and the foreground itself. The
/// returned order is painter order; move the optional blur overlay above the
/// clean source when extracting or inserting layers into an existing plan.
class DocumentaryDepthLook {
public:
    [[nodiscard]] static std::vector<LayerPlan> compose(
        LayerPlan foreground, std::vector<TextureLayerSpec> textures,
        Vec2 canvas_size, const DocumentaryDepthLookProfile& profile = {}) {
        if (foreground.id.empty() || textures.size() > 12 ||
            !std::isfinite(profile.texture_opacity) || profile.texture_opacity < 0.0f ||
            profile.texture_opacity > 1.0f || !std::isfinite(profile.grain) ||
            profile.grain < 0.0f || profile.grain > 1.0f ||
            !std::isfinite(profile.edge_roughness) || profile.edge_roughness < 0.0f ||
            profile.edge_roughness > 64.0f ||
            !std::isfinite(profile.chromatic_aberration) ||
            profile.chromatic_aberration < 0.0f || profile.chromatic_aberration > 16.0f ||
            !std::isfinite(profile.vignette) || profile.vignette < 0.0f || profile.vignette > 1.0f ||
            !std::isfinite(profile.focus_blur) || profile.focus_blur < 0.0f || profile.focus_blur > 256.0f ||
            !std::isfinite(profile.focus_feather) || profile.focus_feather < 0.0f ||
            profile.focus_feather > 1000000.0f)
            throw std::invalid_argument("documentary depth look settings are outside supported bounds");
        std::vector<LayerPlan> layers;
        layers.reserve(textures.size() + 2);
        for (auto& texture : textures) {
            texture.opacity *= profile.texture_opacity;
            texture.seed ^= profile.seed;
            layers.push_back(TextureLayerComposer::compose(texture, canvas_size));
        }
        if (profile.vignette > 0.0f) {
            LayerPlan::EffectPlan vignette;
            vignette.kind = LayerPlan::EffectKindPlan::Vignette;
            vignette.amount = profile.vignette;
            vignette.radius = 0.72f;
            vignette.softness = 0.55f;
            foreground.effects.push_back(std::move(vignette));
        }
        roughen_edges(foreground, profile.edge_roughness, 8.0f, 2, 0.0f, profile.seed);
        if (profile.grain > 0.0f) {
            LayerPlan::EffectPlan grain;
            grain.kind = LayerPlan::EffectKindPlan::Noise;
            grain.amount = profile.grain;
            grain.seed = profile.seed;
            grain.rgb_noise = true;
            foreground.effects.push_back(std::move(grain));
        }
        if (profile.chromatic_aberration > 0.0f)
            rgb_split(foreground, profile.chromatic_aberration);
        layers.push_back(foreground);
        if (profile.focus_blur > 0.0f)
            layers.push_back(masked_lens_blur(foreground, canvas_size, profile.focus_blur,
                Vec2{0.5f, 0.48f}, Vec2{0.55f, 0.70f}, profile.focus_feather,
                "_documentary_focus_blur"));
        return layers;
    }
};

inline void scanlines(LayerPlan& layer, float intensity = 0.02f,
                      float spacing = 0.035f) {
    if (!std::isfinite(intensity) || intensity < 0.0f || intensity > 0.25f ||
        !std::isfinite(spacing) || spacing < 0.002f || spacing > 1.0f)
        throw std::invalid_argument("scanline intensity/spacing is outside supported bounds");
    LayerPlan::EffectPlan effect;
    effect.kind = LayerPlan::EffectKindPlan::WaveWarp;
    effect.amount = intensity;
    effect.warp_wavelength = spacing;
    effect.warp_direction = {0.0f, 1.0f};
    effect.wave_type = 2;
    layer.effects.push_back(std::move(effect));
}

/// Compose horizontal square-wave distortion, RGB separation, and seeded RGB
/// grain. All three stages lower through the core's generic effect stack.
inline void glitch(LayerPlan& layer, float intensity = 0.5f,
                   std::uint32_t seed = 0) {
    if (!std::isfinite(intensity) || intensity < 0.0f || intensity > 1.0f)
        throw std::invalid_argument("glitch intensity must be in [0, 1]");
    LayerPlan::EffectPlan warp;
    warp.kind = LayerPlan::EffectKindPlan::WaveWarp;
    warp.amount = 0.025f * intensity;
    warp.warp_wavelength = 0.035f;
    warp.warp_speed = 0.0f;
    warp.warp_direction = {0.0f, 1.0f};
    warp.wave_type = 2; // WaveWarpParams::WaveType::Square
    layer.effects.push_back(std::move(warp));

    LayerPlan::EffectPlan split;
    split.kind = LayerPlan::EffectKindPlan::ChromaticAberration;
    split.chromatic_red_offset = 8.0f * intensity;
    split.chromatic_blue_offset = -8.0f * intensity;
    layer.effects.push_back(std::move(split));

    LayerPlan::EffectPlan grain;
    grain.kind = LayerPlan::EffectKindPlan::Noise;
    grain.amount = 0.12f * intensity;
    grain.seed = seed;
    grain.animated = true;
    grain.rgb_noise = true;
    grain.noise_size = 1.0f;
    layer.effects.push_back(std::move(grain));
}

/// Build a bounded moving aurora palette for a filled rect/rounded-rect/ellipse
/// layer. Its period is baked at quarter-period keyframes over the layer span.
inline GradientAnimationPlan aurora_gradient(
    Frame start, Frame duration, FrameRate fps, float period_seconds = 6.0f) {
    if (start.integral() < 0 || duration.integral() < 1 || duration.integral() > 65535 ||
        !std::isfinite(period_seconds) || period_seconds <= 0.0f ||
        fps.num() <= 0 || fps.den() <= 0)
        throw std::invalid_argument("aurora gradient requires bounded positive timing and frame rate");
    const double raw_period = static_cast<double>(period_seconds) * fps.num() / fps.den();
    const auto period = static_cast<std::int64_t>(std::llround(raw_period));
    if (period < 4 || period > 65535)
        throw std::invalid_argument("aurora gradient period must fit in 4..65535 frames");

    const std::array<Color, 4> palette{
        Color{0.05f, 0.78f, 0.66f, 1.0f},
        Color{0.12f, 0.34f, 0.92f, 1.0f},
        Color{0.58f, 0.16f, 0.82f, 1.0f},
        Color{0.92f, 0.25f, 0.53f, 1.0f}};
    const auto color_at = [&](float position, float phase) {
        const float coordinate = std::fmod(position * 4.0f + phase * 4.0f, 4.0f);
        const auto left = static_cast<std::size_t>(std::floor(coordinate)) % palette.size();
        const auto right = (left + 1) % palette.size();
        return chronon3d::lerp(palette[left], palette[right], coordinate - std::floor(coordinate));
    };
    const auto gradient_at = [&](float phase) {
        return graphics::GradientDefinition::linear(
            Vec2{0.0f, 0.0f}, Vec2{1.0f, 0.0f},
            {{0.0f, color_at(0.0f, phase)},
             {0.5f, color_at(0.5f, phase)},
             {1.0f, color_at(1.0f, phase)}});
    };

    GradientAnimationPlan result;
    result.easing = ::chronon3d::render_plan::detail::easing_name::kLinear;
    for (std::int64_t cycle = 0; cycle < duration.integral(); cycle += period) {
        for (int quarter = 0; quarter < 4; ++quarter) {
            const auto relative = cycle + static_cast<std::int64_t>(std::llround(
                static_cast<double>(period) * static_cast<double>(quarter) / 4.0));
            if (relative >= duration.integral()) continue;
            result.keyframes.push_back({start + Frame{relative}, gradient_at(
                static_cast<float>(quarter) * 0.25f)});
        }
    }
    const float final_phase = static_cast<float>(duration.integral() % period) /
                              static_cast<float>(period);
    result.keyframes.push_back({start + duration, gradient_at(final_phase)});
    return result;
}

/// Velocity-driven chromatic RGB separation. Offset direction and magnitude
/// scale strictly with layer velocity vector.
inline void velocity_rgb_split(LayerPlan& layer, Vec2 velocity, float separation = 4.0f) {
    if (!std::isfinite(separation) || separation < 0.0f || separation > 128.0f ||
        !std::isfinite(velocity.x) || !std::isfinite(velocity.y))
        throw std::invalid_argument("velocity RGB split parameters are invalid");
    LayerPlan::EffectPlan effect;
    effect.kind = LayerPlan::EffectKindPlan::ChromaticAberration;
    effect.chromatic_red_offset = separation;
    effect.chromatic_blue_offset = -separation;
    effect.chromatic_offset_mode = 2; // Velocity mode
    effect.chromatic_velocity = {velocity.x, velocity.y};
    layer.effects.push_back(std::move(effect));
}

/// CRT display recipe: barrel lens curvature + scanline lattice + phosphor RGB separation + bloom glow + RF noise.
inline void crt(LayerPlan& layer, float curvature = 0.08f, float scanline_intensity = 0.035f,
                float rgb_separation = 2.5f, float bloom_amount = 0.35f,
                float rf_noise = 0.05f, std::uint32_t seed = 1984) {
    if (!std::isfinite(curvature) || curvature < -1.0f || curvature > 1.0f ||
        !std::isfinite(scanline_intensity) || scanline_intensity < 0.0f || scanline_intensity > 0.25f)
        throw std::invalid_argument("CRT recipe parameters are outside supported bounds");

    // 1. Lens Distortion (barrel curvature)
    LayerPlan::EffectPlan lens;
    lens.kind = LayerPlan::EffectKindPlan::LensDistortion;
    lens.lens_curvature = curvature;
    lens.lens_scale = 1.0f + std::abs(curvature) * 0.25f; // compensate zoom
    layer.effects.push_back(std::move(lens));

    // 2. Scanline bands
    scanlines(layer, scanline_intensity, 0.025f);

    // 3. Phosphor RGB split
    if (rgb_separation > 0.001f) {
        rgb_split(layer, rgb_separation);
    }

    // 4. Phosphor Bloom
    if (bloom_amount > 0.001f) {
        LayerPlan::EffectPlan bloom;
        bloom.kind = LayerPlan::EffectKindPlan::Bloom;
        bloom.bloom_threshold = 0.65f;
        bloom.bloom_intensity = bloom_amount;
        bloom.radius = 16.0f;
        layer.effects.push_back(std::move(bloom));
    }

    // 5. Analog RF noise
    if (rf_noise > 0.001f) {
        LayerPlan::EffectPlan grain;
        grain.kind = LayerPlan::EffectKindPlan::Noise;
        grain.amount = rf_noise;
        grain.seed = seed;
        grain.animated = true;
        layer.effects.push_back(std::move(grain));
    }
}

/// Paper/document fold: geometric crease deformation and lighting shading.
inline void paper_fold(LayerPlan& layer, float amount = 0.30f, float position = 0.5f,
                       bool horizontal = false, float radius = 0.20f) {
    if (!std::isfinite(amount) || amount < -1.0f || amount > 1.0f ||
        !std::isfinite(position) || position < 0.0f || position > 1.0f ||
        !std::isfinite(radius) || radius < 0.01f || radius > 1.0f)
        throw std::invalid_argument("paper fold parameters are outside supported bounds");

    // 1. Fold warp
    LayerPlan::EffectPlan fold;
    fold.kind = LayerPlan::EffectKindPlan::FoldWarp;
    fold.fold_axis = horizontal ? 1 : 0;
    fold.fold_position = position;
    fold.fold_amount = amount;
    fold.fold_radius = radius;
    fold.fold_falloff = 1.2f;
    fold.fold_perspective = 0.06f;
    layer.effects.push_back(std::move(fold));

    // 2. Crease shading (shadow and specular highlight)
    LayerPlan::EffectPlan crease;
    crease.kind = LayerPlan::EffectKindPlan::CreaseShading;
    crease.crease_axis = horizontal ? 1 : 0;
    crease.crease_position = position;
    crease.crease_width = 28.0f;
    crease.crease_dark_side = 0.35f * std::abs(amount);
    crease.crease_light_side = 0.20f * std::abs(amount);
    crease.crease_softness = 0.50f;
    layer.effects.push_back(std::move(crease));
}

/// Film halation: warm photochemical bleed in high-luminance areas.
inline void halation(LayerPlan& layer, float intensity = 1.0f, float threshold = 0.75f,
                     float radius = 22.0f,
                     Color tint = Color{1.0f, 0.32f, 0.12f, 1.0f}) {
    if (!std::isfinite(intensity) || intensity < 0.0f || intensity > 8.0f ||
        !std::isfinite(threshold) || threshold < 0.0f || threshold > 1.0f ||
        !std::isfinite(radius) || radius < 0.0f || radius > 256.0f)
        throw std::invalid_argument("halation parameters are outside supported bounds");

    LayerPlan::EffectPlan hal;
    hal.kind = LayerPlan::EffectKindPlan::LuminanceResponse;
    hal.response_threshold = threshold;
    hal.response_softness = 0.20f;
    hal.response_radius = radius;
    hal.response_tint = {tint.r, tint.g, tint.b, tint.a};
    hal.response_intensity = intensity;
    hal.response_blend = 0; // Screen
    layer.effects.push_back(std::move(hal));
}

/// Anamorphic lens flare streak: luminance trigger + horizontal streak convolution + additive tint.
inline void anamorphic_flare(LayerPlan& layer, float length = 56.0f, float intensity = 1.2f,
                             Color tint = Color{0.18f, 0.65f, 1.0f, 1.0f}) {
    if (!std::isfinite(length) || length < 0.0f || length > 512.0f ||
        !std::isfinite(intensity) || intensity < 0.0f || intensity > 8.0f)
        throw std::invalid_argument("anamorphic flare parameters are outside supported bounds");

    LayerPlan::EffectPlan flare;
    flare.kind = LayerPlan::EffectKindPlan::LuminanceResponse;
    flare.response_threshold = 0.80f;
    flare.response_softness = 0.15f;
    flare.response_radius = 8.0f;
    flare.response_tint = {tint.r, tint.g, tint.b, tint.a};
    flare.response_intensity = intensity;
    flare.response_blend = 1; // Add

    LayerPlan::EffectPlan streak;
    streak.kind = LayerPlan::EffectKindPlan::DirectionalBlur;
    streak.directional_angle = 0.0f; // Pure horizontal anamorphic streak
    streak.directional_length = length;
    streak.directional_samples = static_cast<int>(std::clamp(length * 0.5f, 8.0f, 64.0f));

    layer.effects.push_back(std::move(flare));
    layer.effects.push_back(std::move(streak));
}

/// Atmospheric convective heatwave: organic fractal turbulence with slow vertical drift.
inline void heatwave(LayerPlan& layer, float amount = 0.035f, float speed = 1.2f,
                     float size = 0.12f, float evolution = 0.0f) {
    if (!std::isfinite(amount) || amount < 0.0f || amount > 1.0f ||
        !std::isfinite(speed) || !std::isfinite(size) || size <= 0.0f)
        throw std::invalid_argument("heatwave parameters are outside supported bounds");

    LayerPlan::EffectPlan displace;
    displace.kind = LayerPlan::EffectKindPlan::TurbulentDisplace;
    displace.amount = amount;
    displace.warp_size = size;
    displace.warp_speed = speed;
    displace.warp_evolution = evolution;
    displace.warp_complexity = 3;
    layer.effects.push_back(std::move(displace));
}

/// Frosted glass backdrop composition.
inline void frosted_glass(LayerPlan& layer, const BackdropFilterPlan& filter = BackdropFilterPlan::glass_frosted()) {
    filter.validate();
    layer.backdrop_filter = filter;
}

} // namespace chrononplugin::effects::presets
