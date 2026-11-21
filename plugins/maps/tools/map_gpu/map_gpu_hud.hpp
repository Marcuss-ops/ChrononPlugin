#pragma once

// Private to the map_gpu tool: the bitmap HUD drawn on the readback frame.

#include <cstdint>
#include <string>
#include <vector>

namespace chrononplugin::maps::gpu_detail {

void draw_hud(std::vector<std::uint8_t>& bgr, int width, int height,
              const std::string& scene, int frame, int frames, float t,
              const std::string& renderer);

}  // namespace chrononplugin::maps::gpu_detail
