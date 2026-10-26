#pragma once

#include <chrononplugin/effects/presets/light_leak_profiles.hpp>

namespace chrononplugin::effects::presets {
using namespace ::chronon3d;
using namespace ::chronon3d::render_plan;

namespace light_leak {

inline void require_intensity(float intensity) {
    if (!std::isfinite(intensity) || intensity < 0.0f || intensity > kMaxIntensity)
        throw std::invalid_argument("light leak intensity must be in [0, 4]");
}

inline void require_radius(float radius) {
    if (!std::isfinite(radius) || radius < 0.0f || radius > 256.0f)
        throw std::invalid_argument("light leak radius must be in [0, 256]");
}

/// Warm light bleeding in from one frame edge: luminance response (screen)
/// plus a matching warm glow.
inline void edge_warm(LayerPlan& layer, float intensity = 1.0f,
                      Color tint = kWarmTint, float radius = 28.0f) {
    require_intensity(intensity);
    require_radius(radius);
    if (intensity <= 0.0f) return;
    LayerPlan::EffectPlan response;
    response.kind = LayerPlan::EffectKindPlan::LuminanceResponse;
    response.response_threshold = 0.55f;
    response.response_softness = 0.35f;
    response.response_radius = radius;
    response.response_tint = {tint.r, tint.g, tint.b, tint.a};
    response.response_intensity = std::min(intensity, 2.0f);
    response.response_blend = 0; // Screen
    layer.effects.push_back(std::move(response));

    LayerPlan::EffectPlan glow;
    glow.kind = LayerPlan::EffectKindPlan::Glow;
    glow.radius = std::min(radius * 1.5f, 256.0f);
    glow.glow_intensity = std::min(0.30f * intensity, 1.2f);
    glow.effect_color = {tint.r, tint.g, tint.b, tint.a};
    layer.effects.push_back(std::move(glow));
}

/// Bloom growing from one corner: god rays anchored at the corner origin
/// plus a low-threshold bloom.
inline void corner_bloom(LayerPlan& layer, float intensity = 1.0f,
                         int corner = 0, Color tint = kAmberTint) {
    require_intensity(intensity);
    if (corner < 0 || corner > 3)
        throw std::invalid_argument("light leak corner must be 0..3 (TL, TR, BL, BR)");
    if (intensity <= 0.0f) return;
    const std::array<Vec2, 4> origins{Vec2{0.0f, 0.0f}, Vec2{1.0f, 0.0f},
                                      Vec2{0.0f, 1.0f}, Vec2{1.0f, 1.0f}};
    LayerPlan::EffectPlan rays;
    rays.kind = LayerPlan::EffectKindPlan::LightRays;
    rays.light_rays_origin = {origins[corner].x, origins[corner].y};
    rays.light_rays_length = 420.0f;
    rays.light_rays_density = std::min(0.35f + 0.15f * intensity, 0.95f);
    rays.light_rays_color = {tint.r, tint.g, tint.b, tint.a};
    rays.light_rays_decay = 0.90f;
    layer.effects.push_back(std::move(rays));

    LayerPlan::EffectPlan bloom;
    bloom.kind = LayerPlan::EffectKindPlan::Bloom;
    bloom.bloom_threshold = 0.55f;
    bloom.bloom_intensity = std::min(0.45f * intensity, 1.8f);
    bloom.radius = 32.0f;
    layer.effects.push_back(std::move(bloom));
}

/// Luminous band sweeping horizontally: warm response plus a long horizontal
/// streak convolution (the band motion itself is plan-level animation).
inline void horizontal_sweep(LayerPlan& layer, float intensity = 1.0f,
                             Color tint = kWarmTint) {
    require_intensity(intensity);
    if (intensity <= 0.0f) return;
    LayerPlan::EffectPlan response;
    response.kind = LayerPlan::EffectKindPlan::LuminanceResponse;
    response.response_threshold = 0.60f;
    response.response_softness = 0.30f;
    response.response_radius = 24.0f;
    response.response_tint = {tint.r, tint.g, tint.b, tint.a};
    response.response_intensity = std::min(intensity, 2.5f);
    response.response_blend = 0; // Screen
    layer.effects.push_back(std::move(response));

    LayerPlan::EffectPlan streak;
    streak.kind = LayerPlan::EffectKindPlan::DirectionalBlur;
    streak.directional_angle = 0.0f;
    streak.directional_length = 96.0f;
    streak.directional_samples = 32;
    layer.effects.push_back(std::move(streak));
}

/// Diagonal variant of the sweep at 45 degrees.
inline void diagonal_sweep(LayerPlan& layer, float intensity = 1.0f,
                           Color tint = kAmberTint) {
    require_intensity(intensity);
    if (intensity <= 0.0f) return;
    horizontal_sweep(layer, intensity, tint);
    LayerPlan::EffectPlan streak = layer.effects.back();
    layer.effects.pop_back();
    streak.directional_angle = 45.0f;
    layer.effects.push_back(std::move(streak));
}

/// Two opposed leaks: symmetric warm/cool response pair meeting mid-frame.
inline void dual_edge(LayerPlan& layer, float intensity = 1.0f) {
    require_intensity(intensity);
    if (intensity <= 0.0f) return;
    LayerPlan::EffectPlan warm;
    warm.kind = LayerPlan::EffectKindPlan::LuminanceResponse;
    warm.response_threshold = 0.58f;
    warm.response_softness = 0.32f;
    warm.response_radius = 26.0f;
    warm.response_tint = {kWarmTint.r, kWarmTint.g, kWarmTint.b, kWarmTint.a};
    warm.response_intensity = std::min(intensity, 2.0f);
    warm.response_blend = 0;
    layer.effects.push_back(std::move(warm));

    LayerPlan::EffectPlan cool;
    cool.kind = LayerPlan::EffectKindPlan::LuminanceResponse;
    cool.response_threshold = 0.62f;
    cool.response_softness = 0.30f;
    cool.response_radius = 26.0f;
    cool.response_tint = {kCoolTint.r, kCoolTint.g, kCoolTint.b, kCoolTint.a};
    cool.response_intensity = std::min(0.8f * intensity, 2.0f);
    cool.response_blend = 0;
    layer.effects.push_back(std::move(cool));
}

/// Elegant prismatic edge: a small constant RGB dispersion (2 px per channel
/// by default) confined to the luminous zone by the response mask.
inline void prismatic(LayerPlan& layer, float intensity = 1.0f,
                      float dispersion_px = 2.0f) {
    require_intensity(intensity);
    if (!std::isfinite(dispersion_px) || dispersion_px < 0.0f || dispersion_px > 16.0f)
        throw std::invalid_argument("light leak prismatic dispersion must be in [0, 16] px");
    if (intensity <= 0.0f) return;
    LayerPlan::EffectPlan response;
    response.kind = LayerPlan::EffectKindPlan::LuminanceResponse;
    response.response_threshold = 0.65f;
    response.response_softness = 0.25f;
    response.response_radius = 20.0f;
    response.response_tint = {1.0f, 1.0f, 1.0f, 1.0f};
    response.response_intensity = std::min(intensity, 2.0f);
    response.response_blend = 0;
    layer.effects.push_back(std::move(response));

    LayerPlan::EffectPlan split;
    split.kind = LayerPlan::EffectKindPlan::ChromaticAberration;
    split.chromatic_red_offset = dispersion_px;
    split.chromatic_blue_offset = -dispersion_px;
    layer.effects.push_back(std::move(split));
}

/// Pulse look: one warm glow whose breathing is driven plan-level through the
/// light.glow intensity parameter track. The recipe pins the warm tint and a
/// safe radius; the animation owns the on-grow-off curve.
inline void pulse(LayerPlan& layer, float peak_intensity = 1.0f,
                  Color tint = kWarmTint, float radius = 48.0f) {
    require_intensity(peak_intensity);
    require_radius(radius);
    if (peak_intensity <= 0.0f) return;
    LayerPlan::EffectPlan glow;
    glow.kind = LayerPlan::EffectKindPlan::Glow;
    glow.radius = radius;
    glow.glow_intensity = std::min(0.35f * peak_intensity, 1.4f);
    glow.effect_color = {tint.r, tint.g, tint.b, tint.a};
    layer.effects.push_back(std::move(glow));
}

/// Film burn: intense warm response over a low threshold (additive) plus
/// bloom — bright enough to read as a transition matte between scenes.
inline void film_burn(LayerPlan& layer, float intensity = 2.0f,
                      float threshold = 0.35f, Color tint = kWarmTint) {
    require_intensity(intensity);
    if (!std::isfinite(threshold) || threshold < 0.0f || threshold > 1.0f)
        throw std::invalid_argument("light leak film burn threshold must be in [0, 1]");
    if (intensity <= 0.0f) return;
    LayerPlan::EffectPlan burn;
    burn.kind = LayerPlan::EffectKindPlan::LuminanceResponse;
    burn.response_threshold = threshold;
    burn.response_softness = 0.22f;
    burn.response_radius = 30.0f;
    burn.response_tint = {tint.r, tint.g, tint.b, tint.a};
    burn.response_intensity = std::min(intensity, kMaxIntensity);
    burn.response_blend = 1; // Add — the burn must lift to white
    layer.effects.push_back(std::move(burn));

    LayerPlan::EffectPlan bloom;
    bloom.kind = LayerPlan::EffectKindPlan::Bloom;
    bloom.bloom_threshold = threshold + 0.10f;
    bloom.bloom_intensity = std::min(0.5f * intensity, 2.0f);
    bloom.radius = 24.0f;
    layer.effects.push_back(std::move(bloom));
}

/// Soft wide flare: very diffuse warm halation without any streak.
inline void soft_flare(LayerPlan& layer, float intensity = 1.0f,
                       Color tint = kAmberTint, float radius = 96.0f) {
    require_intensity(intensity);
    require_radius(radius);
    if (intensity <= 0.0f) return;
    LayerPlan::EffectPlan response;
    response.kind = LayerPlan::EffectKindPlan::LuminanceResponse;
    response.response_threshold = 0.45f;
    response.response_softness = 0.45f;
    response.response_radius = radius;
    response.response_tint = {tint.r, tint.g, tint.b, tint.a};
    response.response_intensity = std::min(intensity, 2.0f);
    response.response_blend = 0; // Screen
    layer.effects.push_back(std::move(response));

    LayerPlan::EffectPlan glow;
    glow.kind = LayerPlan::EffectKindPlan::Glow;
    glow.radius = std::min(radius, 256.0f);
    glow.glow_intensity = std::min(0.25f * intensity, 1.0f);
    glow.effect_color = {tint.r, tint.g, tint.b, tint.a};
    layer.effects.push_back(std::move(glow));
}

/// Focus glow behind a selected subject: a soft radial emphasis (large warm
/// glow plus a gentle bloom) that plans combine with the subject's own
/// 1.00 -> 1.02 scale emphasis.
inline void focus_glow(LayerPlan& layer, float intensity = 1.0f,
                       Color tint = kAmberTint) {
    require_intensity(intensity);
    if (intensity <= 0.0f) return;
    LayerPlan::EffectPlan glow;
    glow.kind = LayerPlan::EffectKindPlan::Glow;
    glow.radius = 64.0f;
    glow.glow_intensity = std::min(0.30f * intensity, 1.2f);
    glow.effect_color = {tint.r, tint.g, tint.b, tint.a};
    layer.effects.push_back(std::move(glow));

    LayerPlan::EffectPlan bloom;
    bloom.kind = LayerPlan::EffectKindPlan::Bloom;
    bloom.bloom_threshold = 0.70f;
    bloom.bloom_intensity = std::min(0.30f * intensity, 1.2f);
    bloom.radius = 20.0f;
    layer.effects.push_back(std::move(bloom));
}

/// Bokeh pass bonus: radial blur sampling plus a soft glow — background
/// lights melt into round bokeh discs.
inline void bokeh_pass(LayerPlan& layer, float intensity = 1.0f,
                       float amount = 0.35f, Color tint = kAmberTint) {
    require_intensity(intensity);
    if (!std::isfinite(amount) || amount < 0.0f || amount > 1.0f)
        throw std::invalid_argument("light leak bokeh amount must be in [0, 1]");
    if (intensity <= 0.0f) return;
    LayerPlan::EffectPlan blur;
    blur.kind = LayerPlan::EffectKindPlan::RadialBlur;
    blur.radial_center = {0.5f, 0.5f};
    blur.radial_amount = amount * 0.5f;
    blur.radial_render_samples = 16;
    layer.effects.push_back(std::move(blur));

    LayerPlan::EffectPlan glow;
    glow.kind = LayerPlan::EffectKindPlan::Glow;
    glow.radius = 40.0f;
    glow.glow_intensity = std::min(0.30f * intensity, 1.2f);
    glow.effect_color = {tint.r, tint.g, tint.b, tint.a};
    layer.effects.push_back(std::move(glow));
}

} // namespace light_leak

} // namespace chrononplugin::effects::presets
