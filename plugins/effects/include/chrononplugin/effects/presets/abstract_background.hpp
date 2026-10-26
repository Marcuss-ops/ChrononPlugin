#pragma once

#include <chrononplugin/effects/presets/light_leak.hpp>

namespace chrononplugin::effects::presets {
using namespace ::chronon3d;
using namespace ::chronon3d::render_plan;

/// Procedural abstract backgrounds composed from canonical Field2D, gradient,
/// shape and effect contracts. These recipes author layers only; rendering,
/// blending and animation continue through the normal RenderPlan pipeline.
namespace abstract_background {

using namespace light_leak;

inline LayerPlan field_layer(Vec2 canvas_size, std::string id, Color tint,
                             graphics::Field2D field,
                             BlendMode blend = BlendMode::Screen,
                             std::uint32_t render_scale = 4,
                             std::optional<graphics::GradientDefinition> field_ramp = std::nullopt) {
    const auto channel_valid = [](float channel) {
        return std::isfinite(channel) && channel >= 0.0f && channel <= 1.0f;
    };
    if (!std::isfinite(canvas_size.x) || !std::isfinite(canvas_size.y) ||
        canvas_size.x <= 0.0f || canvas_size.y <= 0.0f ||
        canvas_size.x > 1000000.0f || canvas_size.y > 1000000.0f ||
        (render_scale != 1 && render_scale != 2 && render_scale != 4) ||
        !channel_valid(tint.r) || !channel_valid(tint.g) ||
        !channel_valid(tint.b) || !channel_valid(tint.a) ||
        !std::isfinite(field.frequency) || field.frequency <= 0.0f ||
        field.frequency > 4096.0f ||
        (field.generator == graphics::Field2DGenerator::Fractal &&
         (field.octaves == 0 || field.octaves > 16 ||
          !std::isfinite(field.persistence) || field.persistence < 0.0f ||
          field.persistence > 1.0f || !std::isfinite(field.lacunarity) ||
          field.lacunarity <= 0.0f || field.lacunarity > 8.0f)))
        throw std::invalid_argument("abstract background inputs are outside supported bounds");
    LayerPlan layer;
    layer.id = std::move(id);
    layer.type = LayerType::Shape;
    layer.size = {canvas_size.x, canvas_size.y};
    layer.size_dimensions = 2;
    layer.position = {canvas_size.x * 0.5f, canvas_size.y * 0.5f, 0.0f};
    layer.position_dimensions = 2;
    layer.screen_space = true;
    layer.blend_mode = blend;
    layer.color = {tint.r, tint.g, tint.b, tint.a};
    ShapeContentPlan shape;
    shape.type = ShapeContentTypePlan::Rect;
    shape.fill_color = std::array<float, 4>{tint.r, tint.g, tint.b, tint.a};
    shape.field = std::move(field);
    shape.field_render_scale = render_scale;
    shape.field_ramp = std::move(field_ramp);
    layer.shape = std::move(shape);
    return layer;
}

inline graphics::Field2D warped_field(std::uint32_t seed, float frequency,
                                      float warp, std::uint32_t octaves = 4) {
    graphics::Field2D field;
    field.generator = graphics::Field2DGenerator::Fractal;
    field.seed = seed;
    field.frequency = frequency;
    field.octaves = octaves;
    if (warp > 0.0f) {
        graphics::Field2DOperator op;
        op.kind = graphics::Field2DOperatorKind::DomainWarp;
        op.amount = warp;
        field.operators.push_back(op);
    }
    return field;
}

inline graphics::GradientDefinition color_ramp(std::vector<graphics::GradientStop> stops) {
    return graphics::GradientDefinition::linear({0.0f, 0.5f}, {1.0f, 0.5f},
                                                  std::move(stops));
}

inline graphics::GradientDefinition neon_ramp() {
    return color_ramp({{0.0f, Color{0.015f, 0.025f, 0.16f, 1.0f}},
                       {0.32f, Color{0.24f, 0.035f, 0.58f, 1.0f}},
                       {0.62f, Color{0.88f, 0.055f, 0.68f, 1.0f}},
                       {0.82f, Color{0.12f, 0.7f, 0.94f, 1.0f}},
                       {1.0f, Color{1.0f, 0.68f, 0.92f, 1.0f}}});
}

inline graphics::GradientDefinition magenta_ramp() {
    return color_ramp({{0.0f, Color{0.08f, 0.008f, 0.22f, 1.0f}},
                       {0.42f, Color{0.56f, 0.025f, 0.48f, 1.0f}},
                       {0.78f, Color{0.98f, 0.12f, 0.62f, 1.0f}},
                       {1.0f, Color{1.0f, 0.72f, 0.9f, 1.0f}}});
}

inline graphics::GradientDefinition cyan_ramp() {
    return color_ramp({{0.0f, Color{0.008f, 0.035f, 0.16f, 1.0f}},
                       {0.38f, Color{0.025f, 0.2f, 0.62f, 1.0f}},
                       {0.74f, Color{0.04f, 0.72f, 0.86f, 1.0f}},
                       {1.0f, Color{0.68f, 0.98f, 1.0f, 1.0f}}});
}

inline std::vector<LayerPlan> aurora_horizon(Vec2 canvas_size,
                                              std::uint32_t seed = 1) {
    std::vector<LayerPlan> layers;
    LightLeakProfile base;
    base.shape = LightLeakShape::Band;
    base.angle = 90.0f;
    base.position = 0.72f;
    base.width = 0.42f;
    base.spread = 0.75f;
    base.softness = 0.5f;
    base.color_a = Color{0.34f, 0.08f, 0.72f, 0.72f};
    base.color_b = Color{0.95f, 0.32f, 0.74f, 1.0f};
    base.seed = seed;
    layers.push_back(LightLeakResolver::resolve(base, canvas_size, "aurora_horizon"));
    auto field = warped_field(seed, 2.2f, 0.16f, 5);
    layers.push_back(field_layer(canvas_size, "aurora_horizon_ridge",
        Color{0.64f, 0.22f, 0.9f, 0.38f}, std::move(field)));
    return layers;
}

inline std::vector<LayerPlan> liquid_neon(Vec2 canvas_size,
                                          std::uint32_t seed = 7,
                                          Color tint = Color{1.0f, 1.0f, 1.0f, 1.0f},
                                          std::optional<graphics::GradientDefinition> ramp = neon_ramp()) {
    std::vector<LayerPlan> layers;
    layers.push_back(field_layer(canvas_size, "liquid_neon_field_a",
        tint, warped_field(seed, 2.0f, 0.22f, 5), BlendMode::Screen, 4, ramp));
    layers.push_back(field_layer(canvas_size, "liquid_neon_field_b",
        Color{0.12f, 0.55f, 0.95f, 0.44f},
        warped_field(seed ^ 0x9e3779b9U, 3.7f, 0.12f, 4), BlendMode::Screen, 4, ramp));
    return layers;
}

inline std::vector<LayerPlan> liquid_magenta(Vec2 canvas_size,
                                             std::uint32_t seed = 43) {
    return liquid_neon(canvas_size, seed, Color{0.92f, 0.08f, 0.62f, 0.86f}, magenta_ramp());
}

inline std::vector<LayerPlan> liquid_cyan(Vec2 canvas_size,
                                           std::uint32_t seed = 47) {
    return liquid_neon(canvas_size, seed, Color{0.04f, 0.62f, 0.9f, 0.82f}, cyan_ramp());
}

inline std::vector<LayerPlan> metaball_soft(Vec2 canvas_size,
                                             std::uint32_t seed = 11) {
    auto field = warped_field(seed, 3.0f, 0.05f, 2);
    field.generator = graphics::Field2DGenerator::Cellular;
    graphics::Field2DOperator soften;
    soften.kind = graphics::Field2DOperatorKind::Smoothstep;
    soften.edge = 0.42f;
    soften.softness = 0.28f;
    field.operators.push_back(soften);
    return {field_layer(canvas_size, "metaball_soft", Color{0.48f, 0.12f, 0.82f, 0.72f},
                        std::move(field))};
}

inline std::vector<LayerPlan> metaball_neon(Vec2 canvas_size,
                                             std::uint32_t seed = 53) {
    auto layers = metaball_soft(canvas_size, seed);
    layers.front().shape->fill_color = std::array<float, 4>{0.92f, 0.08f, 0.68f, 0.9f};
    layers.front().color = {0.92f, 0.08f, 0.68f, 0.9f};
    LayerPlan::EffectPlan glow;
    glow.kind = LayerPlan::EffectKindPlan::Glow;
    glow.radius = 42.0f;
    glow.glow_intensity = 0.8f;
    glow.effect_color = {0.7f, 0.16f, 1.0f, 1.0f};
    layers.front().effects.push_back(std::move(glow));
    return layers;
}

inline std::vector<LayerPlan> glass_orb(Vec2 canvas_size,
                                         std::uint32_t seed = 17) {
    const float diameter = std::min(canvas_size.x, canvas_size.y) * 0.56f;
    LightLeakProfile profile;
    profile.shape = LightLeakShape::Radial;
    profile.position = 0.5f;
    profile.position_y = 0.5f;
    profile.spread = 0.62f;
    profile.softness = 0.18f;
    profile.color_a = Color{0.72f, 0.24f, 1.0f, 0.72f};
    profile.color_b = Color{0.96f, 0.4f, 0.82f, 0.62f};
    profile.seed = seed;
    auto orb = LightLeakResolver::resolve(profile, canvas_size, "glass_orb");
    orb.size = {diameter, diameter};
    orb.position = {canvas_size.x * 0.5f, canvas_size.y * 0.48f, 0.0f};
    orb.shape->type = ShapeContentTypePlan::Ellipse;
    auto optical = BackdropFilterPlan::glass_refractive();
    optical.refraction_field = warped_field(seed ^ 0xa511e9b3U, 3.2f, 0.035f, 3);
    optical.rim_strength = 1.1f;
    optical.rim_width = std::min(diameter * 0.04f, 12.0f);
    optical.validate();
    orb.backdrop_filter = std::move(optical);
    LayerPlan::EffectPlan bloom;
    bloom.kind = LayerPlan::EffectKindPlan::Bloom;
    bloom.bloom_threshold = 0.56f;
    bloom.bloom_intensity = 0.45f;
    bloom.radius = 28.0f;
    orb.effects.push_back(std::move(bloom));
    return {std::move(orb)};
}

inline std::vector<LayerPlan> dual_orbs(Vec2 canvas_size,
                                         std::uint32_t seed = 19) {
    auto layers = glass_orb(canvas_size, seed);
    LightLeakProfile second;
    second.shape = LightLeakShape::Radial;
    second.position = 0.72f;
    second.position_y = 0.38f;
    second.spread = 0.28f;
    second.softness = 0.2f;
    second.color_a = Color{0.12f, 0.7f, 1.0f, 0.72f};
    second.color_b = Color{0.45f, 0.84f, 1.0f, 0.58f};
    second.seed = seed ^ 0x85ebca6bU;
    layers.push_back(LightLeakResolver::resolve(second, canvas_size, "dual_orbs_cool"));
    return layers;
}

inline std::vector<LayerPlan> topographic_contours(Vec2 canvas_size,
                                                    std::uint32_t seed = 23,
                                                    float frequency = 8.0f,
                                                    float line_width = 0.08f) {
    if (!std::isfinite(frequency) || frequency < 1.0f || frequency > 128.0f ||
        !std::isfinite(line_width) || line_width < 0.01f || line_width > 0.4f)
        throw std::invalid_argument("contour frequency/line width is outside supported bounds");
    graphics::Field2D field;
    field.generator = graphics::Field2DGenerator::Rings;
    field.seed = seed;
    field.frequency = frequency;
    graphics::Field2DOperator warp;
    warp.kind = graphics::Field2DOperatorKind::DomainWarp;
    warp.amount = 0.12f;
    field.operators.push_back(warp);
    graphics::Field2DOperator isolate_lines;
    isolate_lines.kind = graphics::Field2DOperatorKind::Smoothstep;
    isolate_lines.edge = 1.0f - line_width * 0.5f;
    isolate_lines.softness = line_width;
    field.operators.push_back(isolate_lines);
    return {field_layer(canvas_size, "topographic_contours",
                        Color{0.86f, 0.91f, 1.0f, 0.68f}, std::move(field), BlendMode::Screen)};
}

inline std::vector<LayerPlan> cinematic_warped_ring(Vec2 canvas_size,
                                                     std::uint32_t seed = 59,
                                                     Color tint = Color{1.0f, 0.38f, 0.12f, 0.9f}) {
    graphics::Field2D field;
    field.generator = graphics::Field2DGenerator::Rings;
    field.frequency = 1.6f;
    field.seed = seed;
    graphics::Field2DOperator warp;
    warp.kind = graphics::Field2DOperatorKind::DomainWarp;
    warp.amount = 0.11f;
    field.operators.push_back(warp);
    graphics::Field2DOperator isolate;
    isolate.kind = graphics::Field2DOperatorKind::Threshold;
    isolate.edge = 0.92f;
    field.operators.push_back(isolate);
    auto ring = field_layer(canvas_size, "cinematic_warped_ring", tint,
                            std::move(field), BlendMode::Add);
    LayerPlan::EffectPlan blur;
    blur.kind = LayerPlan::EffectKindPlan::GaussianBlur;
    blur.radius = 14.0f;
    ring.effects.push_back(std::move(blur));
    return {std::move(ring)};
}

inline std::vector<LayerPlan> cinematic_radial_burst(
    Vec2 canvas_size, std::uint32_t lobes = 8,
    Color tint = Color{1.0f, 0.58f, 0.12f, 0.9f}) {
    const auto channel_valid = [](float channel) {
        return std::isfinite(channel) && channel >= 0.0f && channel <= 1.0f;
    };
    if (lobes < 5 || lobes > 12 || !std::isfinite(canvas_size.x) ||
        !std::isfinite(canvas_size.y) || canvas_size.x <= 0.0f || canvas_size.y <= 0.0f ||
        canvas_size.x > 1000000.0f || canvas_size.y > 1000000.0f ||
        !channel_valid(tint.r) || !channel_valid(tint.g) ||
        !channel_valid(tint.b) || !channel_valid(tint.a))
        throw std::invalid_argument("cinematic burst inputs are outside supported bounds");
    graphics::Field2D field;
    field.generator = graphics::Field2DGenerator::Burst;
    field.seed = 0;
    field.frequency = 1.0f;
    field.octaves = lobes;
    auto layer = field_layer(canvas_size, "cinematic_radial_burst", tint,
                             std::move(field), BlendMode::Add);
    LayerPlan::EffectPlan blur;
    blur.kind = LayerPlan::EffectKindPlan::GaussianBlur;
    blur.radius = std::min(std::min(canvas_size.x, canvas_size.y) * 0.025f, 64.0f);
    layer.effects.push_back(std::move(blur));
    LayerPlan::EffectPlan bloom;
    bloom.kind = LayerPlan::EffectKindPlan::Bloom;
    bloom.bloom_threshold = 0.42f;
    bloom.bloom_intensity = 0.72f;
    bloom.radius = 36.0f;
    layer.effects.push_back(std::move(bloom));
    return {std::move(layer)};
}


inline std::vector<LayerPlan> cinematic_film_burn(Vec2 canvas_size,
                                                   float intensity = 2.0f,
                                                   std::uint32_t seed = 79) {
    light_leak::require_intensity(intensity);
    LightLeakProfile source;
    source.shape = LightLeakShape::Radial;
    source.position = 0.5f;
    source.position_y = 0.5f;
    source.spread = 0.88f;
    source.softness = 0.42f;
    source.intensity = intensity;
    source.color_a = Color{0.64f, 0.04f, 0.01f, 1.0f};
    source.color_b = Color{1.0f, 0.34f, 0.04f, 0.92f};
    source.blend = LightLeakBlend::Add;
    source.grain = 0.06f;
    source.seed = seed;
    auto layer = LightLeakResolver::resolve(source, canvas_size, "cinematic_film_burn");
    light_leak::film_burn(layer, source.intensity, 0.35f, light_leak::kWarmTint);
    return {std::move(layer)};
}

inline std::vector<LayerPlan> plasma_orb(Vec2 canvas_size,
                                         std::uint32_t seed = 61) {
    auto layers = glass_orb(canvas_size, seed);
    auto surface = warped_field(seed ^ 0x9e3779b9U, 6.0f, 0.025f, 3);
    graphics::Field2DOperator edge;
    edge.kind = graphics::Field2DOperatorKind::Threshold;
    edge.edge = 0.72f;
    surface.operators.push_back(edge);
    layers.push_back(field_layer(canvas_size, "plasma_orb_surface",
        Color{0.95f, 0.56f, 1.0f, 0.3f}, std::move(surface), BlendMode::Add));
    return layers;
}

inline std::vector<LayerPlan> flowlines_vertical(Vec2 canvas_size,
                                                  std::uint32_t seed = 29,
                                                  std::uint32_t line_count = 80,
                                                  float displacement = 0.004f,
                                                  bool allow_crossing = false) {
    if (line_count < 4 || line_count > 512 || !std::isfinite(displacement) ||
        displacement < 0.0f || displacement > 0.25f ||
        (!allow_crossing && displacement > 0.5f / static_cast<float>(line_count)))
        throw std::invalid_argument("flowline spacing/displacement is outside supported bounds");
    graphics::Field2D field;
    field.generator = graphics::Field2DGenerator::Stripes;
    field.seed = seed;
    field.frequency = static_cast<float>(line_count);
    graphics::Field2DOperator warp;
    warp.kind = graphics::Field2DOperatorKind::DomainWarp;
    warp.amount = displacement;
    field.operators.push_back(warp);
    return {field_layer(canvas_size, "flowlines_vertical",
                        Color{0.82f, 0.9f, 1.0f, 0.6f}, std::move(field), BlendMode::Screen)};
}

inline std::vector<LayerPlan> flowlines_mesh(Vec2 canvas_size,
                                              std::uint32_t seed = 67,
                                              std::uint32_t line_count = 48,
                                              float displacement = 0.003f,
                                              bool allow_crossing = false) {
    auto vertical = flowlines_vertical(canvas_size, seed, line_count,
                                       displacement, allow_crossing);
    graphics::Field2D horizontal;
    horizontal.generator = graphics::Field2DGenerator::Stripes;
    horizontal.seed = seed ^ 0x85ebca6bU;
    horizontal.frequency = static_cast<float>(line_count);
    horizontal.transform.rotation_radians = 1.57079632679f;
    graphics::Field2DOperator warp;
    warp.kind = graphics::Field2DOperatorKind::DomainWarp;
    warp.amount = displacement;
    horizontal.operators.push_back(warp);
    vertical.push_back(field_layer(canvas_size, "flowlines_mesh_horizontal",
        Color{0.62f, 0.76f, 1.0f, 0.42f}, std::move(horizontal), BlendMode::Screen));
    return vertical;
}

inline std::vector<LayerPlan> folded_ribbon(Vec2 canvas_size,
                                            std::uint32_t seed = 31,
                                            float repeat = 5.0f,
                                            float warp_amount = 0.08f) {
    if (!std::isfinite(repeat) || repeat < 1.0f || repeat > 64.0f ||
        !std::isfinite(warp_amount) || warp_amount < 0.0f || warp_amount > 0.25f)
        throw std::invalid_argument("fold repeat/warp is outside supported bounds");
    graphics::Field2D field;
    field.generator = graphics::Field2DGenerator::Stripes;
    field.seed = seed;
    field.frequency = repeat;
    graphics::Field2DOperator warp;
    warp.kind = graphics::Field2DOperatorKind::DomainWarp;
    warp.amount = warp_amount;
    if (warp_amount > 0.0f) field.operators.push_back(warp);
    return {field_layer(canvas_size, "folded_ribbon",
                        Color{0.54f, 0.26f, 0.9f, 0.8f}, std::move(field), BlendMode::Screen)};
}

inline std::vector<LayerPlan> gradient_fan(Vec2 canvas_size,
                                           std::uint32_t seed = 71) {
    graphics::Field2D field;
    field.generator = graphics::Field2DGenerator::Conic;
    field.frequency = 1.0f;
    field.seed = seed;
    graphics::Field2DOperator warp;
    warp.kind = graphics::Field2DOperatorKind::DomainWarp;
    warp.amount = 0.045f;
    field.operators.push_back(warp);
    return {field_layer(canvas_size, "gradient_fan",
                        Color{0.58f, 0.32f, 0.92f, 0.58f}, std::move(field), BlendMode::Screen)};
}

inline std::vector<LayerPlan> blue_atmosphere(Vec2 canvas_size,
                                               std::uint32_t seed = 37) {
    LightLeakProfile profile;
    profile.shape = LightLeakShape::Radial;
    profile.position = 0.38f;
    profile.position_y = 0.48f;
    profile.spread = 0.9f;
    profile.softness = 0.62f;
    profile.color_a = Color{0.05f, 0.22f, 0.58f, 0.64f};
    profile.color_b = Color{0.08f, 0.62f, 0.92f, 0.38f};
    profile.seed = seed;
    return {LightLeakResolver::resolve(profile, canvas_size, "blue_atmosphere")};
}

inline std::vector<LayerPlan> mega_mix(Vec2 canvas_size,
                                       std::uint32_t seed = 41) {
    auto layers = aurora_horizon(canvas_size, seed);
    auto liquid = liquid_neon(canvas_size, seed ^ 0x9e3779b9U);
    auto orbs = dual_orbs(canvas_size, seed ^ 0x85ebca6bU);
    auto contours = topographic_contours(canvas_size, seed ^ 0xc2b2ae35U);
    layers.insert(layers.end(), std::make_move_iterator(liquid.begin()),
                  std::make_move_iterator(liquid.end()));
    layers.insert(layers.end(), std::make_move_iterator(orbs.begin()),
                  std::make_move_iterator(orbs.end()));
    layers.insert(layers.end(), std::make_move_iterator(contours.begin()),
                  std::make_move_iterator(contours.end()));
    return layers;
}

} // namespace abstract_background

} // namespace chrononplugin::effects::presets
