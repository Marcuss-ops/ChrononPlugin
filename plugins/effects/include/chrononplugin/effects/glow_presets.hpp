#pragma once

#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/math/color.hpp>

namespace chrononplugin::effects::presets {

[[nodiscard]] inline chronon3d::GlowParams canary_black_glow(float radius = 24.0f) {
    return chronon3d::GlowParams{
        .radius = radius, .intensity = 0.25f,
        .color = chronon3d::Color{1.0f, 1.0f, 1.0f, 1.0f}};
}

} // namespace chrononplugin::effects::presets
