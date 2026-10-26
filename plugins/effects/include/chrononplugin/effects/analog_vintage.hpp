#pragma once

#include <chrononplugin/effects/presets.hpp>
#include <chronon3d/render_graph/effect_gpu_capability.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace chrononplugin::effects {

using namespace ::chronon3d::render_plan;

static_assert(::chronon3d::graph::gpu_effect_capability(
    ::chronon3d::effects::EffectType::Glow)->route ==
    ::chronon3d::graph::GpuEffectRoute::NativeZeroReadback);
static_assert(::chronon3d::graph::gpu_effect_capability(
    ::chronon3d::effects::EffectType::TurbulentDisplace)->route ==
    ::chronon3d::graph::GpuEffectRoute::NativeZeroReadback);
static_assert(::chronon3d::graph::gpu_effect_capability(
    ::chronon3d::effects::EffectType::ChromaticAberration)->route ==
    ::chronon3d::graph::GpuEffectRoute::NativeZeroReadback);
static_assert(::chronon3d::graph::gpu_effect_capability(
    ::chronon3d::effects::EffectType::WaveWarp)->route ==
    ::chronon3d::graph::GpuEffectRoute::NativeZeroReadback);
static_assert(::chronon3d::graph::gpu_effect_capability(
    ::chronon3d::effects::EffectType::Vignette)->route ==
    ::chronon3d::graph::GpuEffectRoute::NativeZeroReadback);
static_assert(::chronon3d::graph::gpu_effect_capability(
    ::chronon3d::effects::EffectType::Noise)->route ==
    ::chronon3d::graph::GpuEffectRoute::NativeZeroReadback);

struct AnalogVintageOptions {
    float canvas_width{1920.0F};
    float canvas_height{1080.0F};
    float boil_pixels{1.5F};
    float boil_scale{0.025F};
    float tight_glow_radius{16.0F};
    float tight_glow_intensity{0.34F};
    float broad_glow_radius{180.0F};
    float broad_glow_intensity{0.12F};
    float scanline_amount{0.035F};
    float grain_amount{0.045F};
    float vignette_amount{0.72F};
    float chromatic_offset{0.8F};
    float posterize_fps{12.0F};
    std::uint32_t seed{1984};
};

/// Add a deterministic vintage-paper treatment using Chronon3D's native
/// effect plan. Every selected operation has a strict Vulkan route; there is
/// no software readback or CPU pixel pass in this recipe.
inline void apply_analog_vintage(LayerPlan& layer,
                                 const AnalogVintageOptions& options = {}) {
    const float values[]{options.canvas_width, options.canvas_height,
        options.boil_pixels, options.boil_scale,
        options.tight_glow_radius, options.tight_glow_intensity,
        options.broad_glow_radius, options.broad_glow_intensity,
        options.scanline_amount, options.grain_amount,
        options.vignette_amount, options.chromatic_offset,
        options.posterize_fps};
    for (const float value : values)
        if (!std::isfinite(value))
            throw std::invalid_argument("analog vintage settings must be finite");
    if (options.canvas_width <= 0.0F || options.canvas_width > 16384.0F ||
        options.canvas_height <= 0.0F || options.canvas_height > 16384.0F ||
        options.boil_pixels < 0.0F || options.boil_pixels > 8.0F ||
        options.boil_scale <= 0.0F || options.boil_scale > 1.0F ||
        options.tight_glow_radius < 0.0F || options.tight_glow_radius > 256.0F ||
        options.broad_glow_radius < 0.0F || options.broad_glow_radius > 256.0F ||
        options.tight_glow_intensity < 0.0F || options.tight_glow_intensity > 4.0F ||
        options.broad_glow_intensity < 0.0F || options.broad_glow_intensity > 4.0F ||
        options.scanline_amount < 0.0F || options.scanline_amount > 0.25F ||
        options.grain_amount < 0.0F || options.grain_amount > 0.25F ||
        options.vignette_amount < 0.0F || options.vignette_amount > 1.0F ||
        options.chromatic_offset < 0.0F || options.chromatic_offset > 16.0F ||
        options.posterize_fps <= 0.0F || options.posterize_fps > 60.0F)
        throw std::invalid_argument("analog vintage settings exceed supported ranges");

    // Line boil is quantized so frame-to-frame edge changes have an analog
    // cadence instead of smooth digital wobble.
    if (options.boil_pixels > 0.0F) {
        LayerPlan::EffectPlan boil;
        boil.kind = LayerPlan::EffectKindPlan::TurbulentDisplace;
        boil.amount = options.boil_pixels / std::min(options.canvas_width,
                                                      options.canvas_height);
        boil.warp_size = options.boil_scale;
        boil.warp_complexity = 1;
        boil.warp_speed = 1.0F;
        boil.posterize_fps = options.posterize_fps;
        layer.effects.push_back(std::move(boil));
    }

    if (options.chromatic_offset > 0.0F)
        presets::rgb_split(layer, options.chromatic_offset);

    // Square-wave displacement gives the CRT/VHS line rhythm while remaining
    // on the core's native Vulkan warp-bulk route.
    if (options.scanline_amount > 0.0F)
        presets::scanlines(layer, options.scanline_amount, 0.025F);

    if (options.tight_glow_intensity > 0.0F) {
        LayerPlan::EffectPlan glow;
        glow.kind = LayerPlan::EffectKindPlan::Glow;
        glow.radius = options.tight_glow_radius;
        glow.glow_intensity = options.tight_glow_intensity;
        glow.effect_color = {1.0F, 0.78F, 0.54F, 1.0F};
        layer.effects.push_back(std::move(glow));
    }
    if (options.broad_glow_intensity > 0.0F) {
        LayerPlan::EffectPlan glow;
        glow.kind = LayerPlan::EffectKindPlan::Glow;
        glow.radius = options.broad_glow_radius;
        glow.glow_intensity = options.broad_glow_intensity;
        glow.effect_color = {1.0F, 0.78F, 0.54F, 1.0F};
        layer.effects.push_back(std::move(glow));
    }

    if (options.vignette_amount > 0.0F) {
        LayerPlan::EffectPlan vignette;
        vignette.kind = LayerPlan::EffectKindPlan::Vignette;
        vignette.radial_center = {0.5F, 0.46F};
        vignette.radius = 0.68F;
        vignette.amount = options.vignette_amount;
        vignette.softness = 0.62F;
        vignette.effect_color = {0.025F, 0.020F, 0.018F, 1.0F};
        layer.effects.push_back(std::move(vignette));
    }

    if (options.grain_amount > 0.0F) {
        LayerPlan::EffectPlan grain;
        grain.kind = LayerPlan::EffectKindPlan::Noise;
        grain.amount = options.grain_amount;
        grain.seed = options.seed;
        grain.animated = true;
        grain.noise_size = 1.0F;
        grain.posterize_fps = options.posterize_fps;
        layer.effects.push_back(std::move(grain));
    }
}

/// Warm charcoal plate with a centered radial falloff, intended as the bottom
/// layer behind the subject and film-dirt overlay.
[[nodiscard]] inline LayerPlan make_analog_vintage_backdrop(
    float width = 1920.0F, float height = 1080.0F) {
    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.0F ||
        height <= 0.0F || width > 16384.0F || height > 16384.0F)
        throw std::invalid_argument("analog vintage backdrop dimensions are invalid");
    LayerPlan background;
    background.id = "analog_vintage_backdrop";
    background.type = LayerType::Color;
    background.screen_space = true;
    background.position = {width * 0.5F, height * 0.5F, 0.0F};
    background.position_dimensions = 2;
    background.size = {width, height};
    background.size_dimensions = 2;
    background.color = {0.10F, 0.075F, 0.058F, 1.0F};
    LayerPlan::EffectPlan falloff;
    falloff.kind = LayerPlan::EffectKindPlan::Vignette;
    falloff.radial_center = {0.5F, 0.46F};
    falloff.radius = 0.72F;
    falloff.softness = 0.82F;
    falloff.amount = 0.88F;
    falloff.effect_color = {0.0F, 0.0F, 0.0F, 1.0F};
    background.effects.push_back(std::move(falloff));
    return background;
}

struct FilmDirtOptions {
    float canvas_width{1920.0F};
    float canvas_height{1080.0F};
    std::uint32_t dust_particles{36};
    std::uint32_t scratches{2};
    float maximum_opacity{0.42F};
    std::uint32_t flicker_interval_frames{2};
    std::uint32_t seed{1984};
};

/// Create sparse, flickering white dust and vertical film scratches as
/// screen-space native shape layers. Append the returned layers above footage.
[[nodiscard]] inline std::vector<LayerPlan> make_film_dirt_overlay(
    const FilmDirtOptions& options = {}) {
    if (!std::isfinite(options.canvas_width) || !std::isfinite(options.canvas_height) ||
        !std::isfinite(options.maximum_opacity) || options.canvas_width <= 0.0F ||
        options.canvas_height <= 0.0F || options.canvas_width > 16384.0F ||
        options.canvas_height > 16384.0F || options.dust_particles > 256U ||
        options.scratches > 16U || options.maximum_opacity < 0.0F ||
        options.maximum_opacity > 0.8F || options.flicker_interval_frames == 0U ||
        options.flicker_interval_frames > 600U)
        throw std::invalid_argument("film dirt settings exceed supported ranges");

    auto next_random = [](std::uint64_t& state) {
        state += 0x9e3779b97f4a7c15ULL;
        std::uint64_t value = state;
        value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
        return value ^ (value >> 31U);
    };
    auto random_unit = [&](std::uint64_t& state) {
        return static_cast<float>(next_random(state) >> 40U) / 16777215.0F;
    };

    std::vector<LayerPlan> layers;
    layers.reserve(options.dust_particles + options.scratches);
    std::uint64_t state = options.seed;
    const auto add_dirt_shape = [&](std::string id, ShapeContentTypePlan shape_type,
                                    float x, float y, float width, float height,
                                    std::uint32_t index, std::vector<LayerPlan>& output) {
        LayerPlan dirt;
        dirt.id = std::move(id);
        dirt.type = LayerType::Shape;
        dirt.screen_space = true;
        dirt.position = {x, y, 0.0F};
        dirt.position_dimensions = 2;
        dirt.size = {width, height};
        dirt.size_dimensions = 2;
        dirt.blend_mode = chronon3d::BlendMode::Screen;
        dirt.opacity = options.maximum_opacity;
        dirt.shape.emplace();
        dirt.shape->type = shape_type;
        dirt.shape->fill_color = std::array<float, 4>{1.0F, 1.0F, 1.0F, 1.0F};
        AnimationTrackPlan flicker;
        flicker.property = "opacity";
        flicker.random_hold = AnimationTrackPlan::RandomHoldDriverPlan{
            .interval_frames = options.flicker_interval_frames,
            .min = 0.0F,
            .max = options.maximum_opacity,
            .seed = options.seed ^ (index * 0x9e3779b9U)};
        dirt.animation = AnimationPlan{.tracks = {std::move(flicker)}};
        output.push_back(std::move(dirt));
    };

    for (std::uint32_t i = 0; i < options.dust_particles; ++i) {
        const float x = random_unit(state) * options.canvas_width;
        const float y = random_unit(state) * options.canvas_height;
        const float diameter = 0.7F + random_unit(state) * 1.8F;
        add_dirt_shape("film_dust_" + std::to_string(i), ShapeContentTypePlan::Ellipse,
                       x, y, diameter, diameter, i, layers);
    }
    for (std::uint32_t i = 0; i < options.scratches; ++i) {
        const float x = random_unit(state) * options.canvas_width;
        const float y = random_unit(state) * options.canvas_height;
        const float height = options.canvas_height * (0.12F + random_unit(state) * 0.55F);
        add_dirt_shape("film_scratch_" + std::to_string(i), ShapeContentTypePlan::Rect,
                       x, y, 0.7F + random_unit(state) * 0.8F, height,
                       options.dust_particles + i, layers);
    }
    return layers;
}

} // namespace chrononplugin::effects
