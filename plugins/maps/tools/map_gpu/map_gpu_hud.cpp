// Bitmap HUD (5x7, BGR output; no OpenCV) for the map_gpu tool. Split out of
// map_gpu_render.cpp.

#include "map_gpu_hud.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace chrononplugin::maps::gpu_detail {

struct Glyph {
    char character;
    std::array<std::uint8_t, 7> rows;
};

const std::array<Glyph, 40>& glyphs() {
    static const std::array<Glyph, 40> kGlyphs{{
        {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
        {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
        {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
        {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
        {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
        {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
        {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}},
        {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
        {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
        {'J', {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}},
        {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
        {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
        {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
        {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},
        {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
        {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
        {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
        {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
        {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
        {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
        {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
        {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
        {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}},
        {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
        {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
        {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
        {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
        {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
        {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
        {'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},
        {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
        {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
        {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
        {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
        {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
        {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
        {'/', {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10}},
        {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},
        {':', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}},
        {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    }};
    return kGlyphs;
}

const Glyph* find_glyph(char character) {
    if (character == ' ') return nullptr;
    const char upper = static_cast<char>(
        std::toupper(static_cast<unsigned char>(character)));
    for (const Glyph& glyph : glyphs()) {
        if (glyph.character == upper) return &glyph;
    }
    return nullptr;
}

void put_pixel(std::vector<std::uint8_t>& bgr, int width, int height,
               int x, int y, std::uint8_t b, std::uint8_t g, std::uint8_t r) {
    if (x < 0 || y < 0 || x >= width || y >= height) return;
    const std::size_t offset = (static_cast<std::size_t>(y) * width + x) * 3;
    bgr[offset] = b;
    bgr[offset + 1] = g;
    bgr[offset + 2] = r;
}

void fill_rect(std::vector<std::uint8_t>& bgr, int width, int height,
               int x, int y, int w, int h,
               std::uint8_t b, std::uint8_t g, std::uint8_t r) {
    for (int yy = 0; yy < h; ++yy) {
        for (int xx = 0; xx < w; ++xx) {
            put_pixel(bgr, width, height, x + xx, y + yy, b, g, r);
        }
    }
}

int text_width(const std::string& text, int scale) {
    return static_cast<int>(text.size()) * 6 * scale;
}

void draw_text(std::vector<std::uint8_t>& bgr, int width, int height,
               int x, int y, const std::string& text, int scale,
               std::uint8_t b, std::uint8_t g, std::uint8_t r,
               bool shadow = true) {
    int cursor = x;
    for (const char character : text) {
        const Glyph* glyph = find_glyph(character);
        if (character == ' ') {
            cursor += 4 * scale;
            continue;
        }
        if (glyph == nullptr) {
            cursor += 6 * scale;
            continue;
        }
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if ((glyph->rows[row] & (1u << (4 - col))) == 0) continue;
                if (shadow) {
                    fill_rect(bgr, width, height,
                              cursor + col * scale + 2, y + row * scale + 2,
                              scale, scale, 0, 0, 0);
                }
                fill_rect(bgr, width, height,
                          cursor + col * scale, y + row * scale,
                          scale, scale, b, g, r);
            }
        }
        cursor += 6 * scale;
    }
}

void draw_corner(std::vector<std::uint8_t>& bgr, int width, int height,
                 int x, int y, int sx, int sy, int length,
                 std::uint8_t b, std::uint8_t g, std::uint8_t r) {
    fill_rect(bgr, width, height, sx > 0 ? x : x - length, y,
              length, 2, b, g, r);
    fill_rect(bgr, width, height, x, sy > 0 ? y : y - length,
              2, length, b, g, r);
}

void draw_hud(std::vector<std::uint8_t>& bgr, int width, int height,
              const std::string& scene, int frame, int frames, float t,
              const std::string& renderer) {
    const int margin = 42;
    const std::uint8_t cyan_b = 255, cyan_g = 214, cyan_r = 62;
    const std::uint8_t white_b = 248, white_g = 250, white_r = 252;
    draw_corner(bgr, width, height, margin, margin, 1, 1, 58,
                cyan_b, cyan_g, cyan_r);
    draw_corner(bgr, width, height, width - margin, margin, -1, 1, 58,
                cyan_b, cyan_g, cyan_r);

    const std::string title = scene == "particles"
        ? "GPU PARTICLE FLOW / COMPUTE SHADER"
        : "GPU VOLUMETRIC / DEPTH BUFFER";
    draw_text(bgr, width, height, margin, margin + 24, title, 3,
              cyan_b, cyan_g, cyan_r);
    const std::string subtitle = scene == "particles"
        ? "32768 AGENTS / SSBO TRAILS"
        : "24-BIT DEPTH / INSTANCED 2.5D";
    draw_text(bgr, width, height, margin, margin + 58, subtitle, 2,
              210, 214, 222);

    char progress[64];
    std::snprintf(progress, sizeof(progress), "%s %03d / %03d",
                  scene == "particles" ? "FLOW" : "SCAN",
                  frame + 1, frames);
    draw_text(bgr, width, height,
              width - margin - text_width(progress, 2), margin + 24,
              progress, 2, 235, 235, 240);

    draw_text(bgr, width, height, margin, height - 108, "JERUSALEM", 5,
              white_b, white_g, white_r);
    const std::string footer = renderer;
    draw_text(bgr, width, height,
              margin, height - 46, footer, 2, 160, 174, 188);

    const int line_y = margin + 92;
    fill_rect(bgr, width, height, margin, line_y, 122, 2,
              cyan_b, cyan_g, cyan_r);
    fill_rect(bgr, width, height, width - margin - 122, line_y, 122, 2,
              cyan_b, cyan_g, cyan_r);

    // A small GPU/scan telemetry strip; it is deliberately outside the 3D
    // render so the HUD cannot hide whether the depth pass is working.
    const int bar_x = width - margin - 260;
    const int bar_y = height - 58;
    fill_rect(bgr, width, height, bar_x, bar_y, 260, 2, 50, 70, 84);
    fill_rect(bgr, width, height, bar_x, bar_y,
              static_cast<int>(260.0f * std::clamp(t, 0.0f, 1.0f)), 2,
              cyan_b, cyan_g, cyan_r);
}

}  // namespace chrononplugin::maps::gpu_detail
