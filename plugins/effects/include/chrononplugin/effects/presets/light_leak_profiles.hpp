#pragma once

#include <chrononplugin/effects/presets/core.hpp>

namespace chrononplugin::effects::presets {
using namespace ::chronon3d;
using namespace ::chronon3d::render_plan;

// ── Light Leak V1 ──────────────────────────────────────────────────────────
//
// Ten canonical leak recipes composed from the existing luminance, glow,
// bloom, ray and dispersion primitives. Every recipe:
//   * is deterministic — identical inputs push identical EffectPlan fields;
//   * fails closed on out-of-range parameters;
//   * lowers intensity 0 to a fully transparent shape with no optional effect stages;
//   * bounds total energy through the underlying effect validators, so an
//     SDR white plate cannot explode: screen/add blends saturate at the
//     compositor and the response intensity ceiling is enforced here.

namespace light_leak {

inline constexpr float kMaxIntensity = 4.0f;
inline constexpr Color kWarmTint{1.0f, 0.62f, 0.24f, 1.0f};
inline constexpr Color kAmberTint{1.0f, 0.80f, 0.45f, 1.0f};
inline constexpr Color kCoolTint{0.55f, 0.78f, 1.0f, 1.0f};

enum class LightLeakShape : std::uint8_t { Edge, Corner, Sweep, Radial, Band };
enum class LightLeakBlend : std::uint8_t { Screen, Add, SoftLight };
enum class LightLeakEdge : std::uint8_t { Left, Top, Right, Bottom };
enum class LightLeakCorner : std::uint8_t { TopLeft, TopRight, BottomLeft, BottomRight };

/// Declarative controls for one deterministic, screen-space light leak.
/// Geometry is lowered to the existing gradient/shape/effect/compositor path;
/// this profile does not introduce a second pixel renderer.
struct LightLeakProfile {
    LightLeakShape shape{LightLeakShape::Edge};
    LightLeakBlend blend{LightLeakBlend::Screen};
    LightLeakEdge edge{LightLeakEdge::Left};
    LightLeakCorner corner{LightLeakCorner::TopLeft};
    Color color_a{1.0f, 0.62f, 0.24f, 1.0f};
    Color color_b{1.0f, 0.90f, 0.62f, 1.0f};
    float intensity{1.0f};
    float softness{0.35f};       // normalized [0, 1]
    float spread{0.5f};          // normalized radial radius / extent
    float angle{0.0f};           // degrees, clockwise in screen coordinates
    float position{0.5f};        // normalized position; off-canvas allowed [-4, 5]
    float position_y{0.5f};      // normalized radial center Y; off-canvas allowed [-4, 5]
    float width{0.25f};          // normalized band width [0, 1]
    float chroma_split{0.0f};    // symmetric channel offset in pixels
    float grain{0.0f};           // spatially stable seeded grain [0, 1]
    float flicker{0.0f};         // explicit frame-varying noise amount [0, 1]
    std::uint32_t seed{0};

    [[nodiscard]] bool valid() const noexcept {
        const auto valid_color = [](const Color& color) {
            const auto unit = [](float value) {
                return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
            };
            return unit(color.r) && unit(color.g) && unit(color.b) && unit(color.a);
        };
        const bool valid_shape = shape == LightLeakShape::Edge ||
            shape == LightLeakShape::Corner || shape == LightLeakShape::Sweep ||
            shape == LightLeakShape::Radial || shape == LightLeakShape::Band;
        const bool valid_blend = blend == LightLeakBlend::Screen ||
            blend == LightLeakBlend::Add || blend == LightLeakBlend::SoftLight;
        const bool valid_edge = edge == LightLeakEdge::Left || edge == LightLeakEdge::Top ||
            edge == LightLeakEdge::Right || edge == LightLeakEdge::Bottom;
        const bool valid_corner = corner == LightLeakCorner::TopLeft ||
            corner == LightLeakCorner::TopRight || corner == LightLeakCorner::BottomLeft ||
            corner == LightLeakCorner::BottomRight;
        return valid_shape && valid_blend && valid_edge && valid_corner &&
            valid_color(color_a) && valid_color(color_b) &&
            std::isfinite(intensity) && intensity >= 0.0f && intensity <= kMaxIntensity &&
            std::isfinite(softness) && softness >= 0.0f && softness <= 1.0f &&
            std::isfinite(spread) && spread >= 0.0f && spread <= 1.0f &&
            std::isfinite(angle) && std::abs(angle) <= 3600.0f &&
            std::isfinite(position) && position >= -4.0f && position <= 5.0f &&
            std::isfinite(position_y) && position_y >= -4.0f && position_y <= 5.0f &&
            std::isfinite(width) && width >= 0.0f && width <= 1.0f &&
            std::isfinite(chroma_split) && chroma_split >= 0.0f && chroma_split <= 16.0f &&
            std::isfinite(grain) && grain >= 0.0f && grain <= 1.0f &&
            std::isfinite(flicker) && flicker >= 0.0f && flicker <= 1.0f;
    }
};

/// Deterministic time driver for one leak emitter. Time is supplied by the
/// caller (normally frame / fps); no frame-local random state is used.
struct AnimatedLightLeakEmitter {
    LightLeakProfile profile{};
    Vec2 velocity{0.0f, 0.0f}; // normalized canvas units per second
    float pulse_amount{0.0f};
    float pulse_speed{0.0f};  // radians per second
    float phase{0.0f};
    std::string id{"light_leak"};
};

/// Canonical lowering for profile-authored leaks. The generated full-canvas
/// shape uses a gradient mask, then the ordinary ordered effect stack and
/// layer blend mode; equal profile/canvas/id values produce equal plans.
class LightLeakResolver {
public:
    [[nodiscard]] static LayerPlan resolve(const LightLeakProfile& profile,
                                           Vec2 canvas_size,
                                           std::string id = "light_leak") {
        if (!profile.valid() || !std::isfinite(canvas_size.x) ||
            !std::isfinite(canvas_size.y) || canvas_size.x <= 0.0f ||
            canvas_size.y <= 0.0f || canvas_size.x > 1000000.0f ||
            canvas_size.y > 1000000.0f)
            throw std::invalid_argument("light leak profile or canvas is outside supported bounds");

        LayerPlan layer;
        layer.id = std::move(id);
        layer.type = LayerType::Shape;
        layer.size = {canvas_size.x, canvas_size.y};
        layer.size_dimensions = 2;
        layer.position = {canvas_size.x * 0.5f, canvas_size.y * 0.5f, 0.0f};
        layer.position_dimensions = 2;
        layer.screen_space = true;
        layer.blend_mode = profile.blend == LightLeakBlend::Add
            ? chronon3d::BlendMode::Add
            : profile.blend == LightLeakBlend::SoftLight
                ? chronon3d::BlendMode::SoftLight : chronon3d::BlendMode::Screen;

        ShapeContentPlan shape;
        shape.type = ShapeContentTypePlan::Rect;
        shape.fill_color = std::array<float, 4>{
            profile.color_a.r, profile.color_a.g, profile.color_a.b, 0.0f};
        const Vec2 center{profile.position, profile.position_y};
        const float strength = profile.intensity;
        const Color clear = Color::transparent();
        const Color a{profile.color_a.r, profile.color_a.g,
                      profile.color_a.b, profile.color_a.a};
        const Color b{profile.color_b.r, profile.color_b.g,
                      profile.color_b.b, profile.color_b.a};
        const auto with_intensity = [strength](Color color) {
            color.r = std::min(color.r * strength, kMaxIntensity);
            color.g = std::min(color.g * strength, kMaxIntensity);
            color.b = std::min(color.b * strength, kMaxIntensity);
            color.a = std::clamp(color.a * strength, 0.0f, 1.0f);
            return color;
        };
        const auto with_opacity = [](Color color, float opacity) {
            color.a *= opacity;
            return color;
        };
        const float diagonal_extent = std::sqrt(1.0f +
            std::pow(std::min(canvas_size.x, canvas_size.y) /
                     std::max(canvas_size.x, canvas_size.y), 2.0f));
        const float radial_radius = std::max(
            profile.spread * diagonal_extent, 0.001f);
        const float sweep_length = std::max(1.0f,
            std::abs(std::cos(profile.angle * 0.01745329251994329577f)) +
            std::abs(std::sin(profile.angle * 0.01745329251994329577f)));

        switch (profile.shape) {
            case LightLeakShape::Edge: {
                Vec2 edge_center{};
                switch (profile.edge) {
                    case LightLeakEdge::Left: edge_center = {0.0f, profile.position}; break;
                    case LightLeakEdge::Top: edge_center = {profile.position, 0.0f}; break;
                    case LightLeakEdge::Right: edge_center = {1.0f, profile.position}; break;
                    case LightLeakEdge::Bottom: edge_center = {profile.position, 1.0f}; break;
                }
                const Color peak = with_intensity(a);
                shape.gradient = graphics::GradientDefinition::radial(
                    edge_center, radial_radius,
                    {{0.0f, peak}, {1.0f, clear}});
                break;
            }
            case LightLeakShape::Corner: {
                constexpr std::array<Vec2, 4> corners{
                    Vec2{0.0f, 0.0f}, Vec2{1.0f, 0.0f},
                    Vec2{0.0f, 1.0f}, Vec2{1.0f, 1.0f}};
                const auto corner = static_cast<std::size_t>(profile.corner);
                const Color peak = with_intensity(a);
                shape.gradient = graphics::GradientDefinition::radial(
                    corners[corner], radial_radius,
                    {{0.0f, peak}, {1.0f, clear}});
                break;
            }
            case LightLeakShape::Radial: {
                const Color color_a = with_intensity(a);
                const Color color_b = with_intensity(b);
                shape.gradient = graphics::GradientDefinition::radial(
                    center, radial_radius,
                    {{0.0f, color_a}, {0.55f, color_b}, {1.0f, clear}});
                break;
            }
            case LightLeakShape::Sweep:
            case LightLeakShape::Band: {
                const float radians = profile.angle *
                    (3.14159265358979323846f / 180.0f);
                const Vec2 tangent{std::cos(radians), std::sin(radians)};
                const Vec2 normal{-tangent.y, tangent.x};
                const Vec2 band_center = Vec2{0.5f, 0.5f} + normal *
                    ((profile.position - 0.5f) * sweep_length);
                const float half_width = std::max(profile.width, 0.001f) * 0.5f;
                const float half_width_t = std::clamp(
                    half_width / (2.0f * sweep_length), 0.001f, 0.499f);
                const float extension = 1.5f * sweep_length;
                const Vec2 start = band_center - normal * extension;
                const Vec2 end = band_center + normal * extension;
                const float band_opacity = profile.shape == LightLeakShape::Band
                    ? 1.0f : 0.5f + 0.5f * std::clamp(profile.spread, 0.0f, 1.0f);
                Color band_color = with_intensity(with_opacity(a, band_opacity));
                shape.gradient = graphics::GradientDefinition::linear(
                    start, end,
                    {{0.0f, clear}, {0.5f - half_width_t, clear},
                     {0.5f, band_color}, {0.5f + half_width_t, clear}, {1.0f, clear}});
                break;
            }
        }
        layer.shape = std::move(shape);
        if (profile.intensity == 0.0f) return layer;

        if (profile.softness > 0.0f) {
            LayerPlan::EffectPlan blur;
            blur.kind = LayerPlan::EffectKindPlan::GaussianBlur;
            blur.radius = std::min(profile.softness *
                std::min(canvas_size.x, canvas_size.y) * 0.10f, 256.0f);
            layer.effects.push_back(std::move(blur));
        }
        if (profile.chroma_split > 0.0f) {
            LayerPlan::EffectPlan split;
            split.kind = LayerPlan::EffectKindPlan::ChromaticAberration;
            split.chromatic_red_offset = profile.chroma_split;
            split.chromatic_blue_offset = -profile.chroma_split;
            layer.effects.push_back(std::move(split));
        }
        const auto append_noise = [&](float amount, std::uint32_t seed, bool animated) {
            if (amount <= 0.0f) return;
            LayerPlan::EffectPlan noise;
            noise.kind = LayerPlan::EffectKindPlan::Noise;
            noise.amount = amount;
            noise.seed = seed;
            noise.animated = animated;
            noise.rgb_noise = true;
            noise.noise_size = 1.0f;
            layer.effects.push_back(std::move(noise));
        };
        // Keep the seeded film grain spatially stable even when the caller
        // separately requests frame-varying flicker.
        append_noise(profile.grain, profile.seed, false);
        append_noise(profile.flicker, profile.seed ^ 0x9e3779b9U, true);
        // Chroma separation precedes grain so seeded noise does not decorrelate
        // channel displacement or add an unrequested fringe of its own.
        return layer;
    }

    /// Resolve a bounded emitter set at an explicit timestamp. Position and
    /// intensity are pure functions of time, so random-access frames match a
    /// sequential render.
    [[nodiscard]] static std::vector<LayerPlan> resolve_many(
        const std::vector<AnimatedLightLeakEmitter>& emitters,
        Vec2 canvas_size, double time_seconds) {
        if (emitters.empty() || emitters.size() > 8 ||
            !std::isfinite(time_seconds) || std::abs(time_seconds) > 1.0e9)
            throw std::invalid_argument("light leak emitter set or time is outside supported bounds");
        std::vector<LayerPlan> layers;
        layers.reserve(emitters.size());
        std::vector<std::string> ids;
        ids.reserve(emitters.size());
        for (const auto& emitter : emitters) {
            if (emitter.id.empty() || !std::isfinite(emitter.velocity.x) ||
                !std::isfinite(emitter.velocity.y) || std::abs(emitter.velocity.x) > 100.0f ||
                std::abs(emitter.velocity.y) > 100.0f ||
                !std::isfinite(emitter.pulse_amount) || emitter.pulse_amount < 0.0f ||
                emitter.pulse_amount > 1.0f || !std::isfinite(emitter.pulse_speed) ||
                std::abs(emitter.pulse_speed) > 1000.0f || !std::isfinite(emitter.phase))
                throw std::invalid_argument("light leak emitter animation is outside supported bounds");
            if (std::find(ids.begin(), ids.end(), emitter.id) != ids.end())
                throw std::invalid_argument("light leak emitter ids must be unique");
            ids.push_back(emitter.id);
            auto profile = emitter.profile;
            profile.position += emitter.velocity.x * static_cast<float>(time_seconds);
            profile.position_y += emitter.velocity.y * static_cast<float>(time_seconds);
            const float pulse = 1.0f + emitter.pulse_amount * std::sin(
                emitter.phase + emitter.pulse_speed * static_cast<float>(time_seconds));
            profile.intensity = std::clamp(profile.intensity * pulse, 0.0f, kMaxIntensity);
            layers.push_back(resolve(profile, canvas_size, emitter.id));
        }
        return layers;
    }
};

/// A soft, warm emitter centered on the frame. The radial fill is generated
/// by LightLeakResolver and remains editable through the ordinary RenderPlan
/// shape and effect contracts.
[[nodiscard]] inline LayerPlan cinematic_soft_blob(
    Vec2 canvas_size, float intensity = 1.0f,
    Color inner = kWarmTint, Color outer = kAmberTint,
    std::uint32_t seed = 0, std::string id = "cinematic_soft_blob") {
    LightLeakProfile profile;
    profile.shape = LightLeakShape::Radial;
    profile.blend = LightLeakBlend::Screen;
    profile.color_a = inner;
    profile.color_b = outer;
    profile.position = 0.5f;
    profile.position_y = 0.5f;
    profile.spread = 0.72f;
    profile.softness = 0.22f;
    profile.intensity = intensity;
    profile.grain = 0.035f;
    profile.seed = seed;
    auto layer = LightLeakResolver::resolve(profile, canvas_size, std::move(id));
    if (intensity > 0.0f) {
        LayerPlan::EffectPlan bloom;
        bloom.kind = LayerPlan::EffectKindPlan::Bloom;
        bloom.bloom_threshold = 0.58f;
        bloom.bloom_intensity = std::min(0.18f * intensity, 0.72f);
        bloom.radius = 32.0f;
        layer.effects.push_back(std::move(bloom));
    }
    return layer;
}

/// Large emitter anchored to a selected frame edge. Position is normalized
/// along that edge, so callers can animate it with ordinary layer tracks.
[[nodiscard]] inline LayerPlan cinematic_edge_burn(
    Vec2 canvas_size, LightLeakEdge edge = LightLeakEdge::Right,
    float position = 0.5f, float intensity = 1.0f,
    Color tint = kWarmTint, std::uint32_t seed = 0,
    std::string id = "cinematic_edge_burn") {
    LightLeakProfile profile;
    profile.shape = LightLeakShape::Edge;
    profile.edge = edge;
    profile.position = position;
    profile.position_y = position;
    profile.spread = 0.82f;
    profile.softness = 0.3f;
    profile.intensity = intensity;
    profile.color_a = tint;
    profile.blend = LightLeakBlend::Screen;
    profile.grain = 0.04f;
    profile.seed = seed;
    return LightLeakResolver::resolve(profile, canvas_size, std::move(id));
}

/// Diagonal broad streak suitable for the moving ribbon/sweep portion of a
/// cinematic background. Motion is authored by normal layer transforms.
[[nodiscard]] inline LayerPlan cinematic_diagonal_streak(
    Vec2 canvas_size, float angle = 35.0f, float position = 0.5f,
    float width = 0.2f, float intensity = 0.8f,
    Color tint = kAmberTint, std::string id = "cinematic_diagonal_streak") {
    LightLeakProfile profile;
    profile.shape = LightLeakShape::Band;
    profile.angle = angle;
    profile.position = position;
    profile.width = width;
    profile.spread = 0.8f;
    profile.softness = 0.18f;
    profile.intensity = intensity;
    profile.color_a = tint;
    profile.blend = LightLeakBlend::Add;
    auto layer = LightLeakResolver::resolve(profile, canvas_size, std::move(id));
    if (intensity > 0.0f) {
        LayerPlan::EffectPlan blur;
        blur.kind = LayerPlan::EffectKindPlan::GaussianBlur;
        blur.radius = std::min(std::min(canvas_size.x, canvas_size.y) * 0.035f, 96.0f);
        layer.effects.push_back(std::move(blur));
    }
    return layer;
}

[[nodiscard]] inline LayerPlan cinematic_bezier_ribbon(
    Vec2 canvas_size, Color tint = kAmberTint, float width_fraction = 0.12f,
    float softness = 0.8f, std::string id = "cinematic_bezier_ribbon") {
    if (!std::isfinite(canvas_size.x) || !std::isfinite(canvas_size.y) ||
        canvas_size.x <= 0.0f || canvas_size.y <= 0.0f ||
        canvas_size.x > 1000000.0f || canvas_size.y > 1000000.0f ||
        !std::isfinite(width_fraction) || width_fraction <= 0.0f || width_fraction > 0.5f ||
        !std::isfinite(softness) || softness < 0.0f || softness > 1.0f)
        throw std::invalid_argument("cinematic Bezier ribbon inputs are outside supported bounds");
    LightLeakProfile color_check;
    color_check.color_a = tint;
    if (!color_check.valid())
        throw std::invalid_argument("cinematic Bezier ribbon tint must be an SDR color");

    LayerPlan layer;
    layer.id = std::move(id);
    layer.type = LayerType::Shape;
    layer.size = {canvas_size.x, canvas_size.y};
    layer.size_dimensions = 2;
    layer.position = {canvas_size.x * 0.5f, canvas_size.y * 0.5f, 0.0f};
    layer.position_dimensions = 2;
    layer.screen_space = true;
    layer.blend_mode = BlendMode::Add;
    layer.color = {tint.r, tint.g, tint.b, tint.a};

    ShapeContentPlan shape;
    shape.type = ShapeContentTypePlan::Path;
    ShapePathCommandPlan move;
    move.type = ShapePathCommandTypePlan::MoveTo;
    move.point = {0.0f, canvas_size.y * 0.62f};
    ShapePathCommandPlan curve;
    curve.type = ShapePathCommandTypePlan::CubicTo;
    curve.point = {canvas_size.x, canvas_size.y * 0.38f};
    curve.control1 = {canvas_size.x * 0.28f, canvas_size.y * 0.06f};
    curve.control2 = {canvas_size.x * 0.72f, canvas_size.y * 0.94f};
    shape.path = {move, curve};
    chronon3d::render_plan::StrokeStyle stroke;
    stroke.width = std::min(canvas_size.x, canvas_size.y) * width_fraction;
    stroke.solid_color = std::array<float, 4>{tint.r, tint.g, tint.b, tint.a};
    stroke.cap = "round";
    stroke.join = "round";
    graphics::BrushProfile brush;
    brush.size = *stroke.width;
    brush.hardness = 1.0f - softness;
    brush.edge_softness = softness;
    stroke.brush = std::move(brush);
    shape.stroke = std::move(stroke);
    layer.shape = std::move(shape);

    LayerPlan::EffectPlan bloom;
    bloom.kind = LayerPlan::EffectKindPlan::Bloom;
    bloom.bloom_threshold = 0.5f;
    bloom.bloom_intensity = 0.55f;
    bloom.radius = std::min(std::min(canvas_size.x, canvas_size.y) * 0.06f, 96.0f);
    layer.effects.push_back(std::move(bloom));
    return layer;
}

} // namespace light_leak

} // namespace chrononplugin::effects::presets
