#pragma once

// EditorialMapPack — reusable native documentary map treatments.
//
// The pack owns the authoring/raster stage, not Chronon3D: it loads the
// simplified US-states GeoJSON, projects it in Web Mercator, composes the six
// editorial plates and writes a full-frame H.264 clip with a poster still.
//
// The public API is deliberately free of OpenCV and FreeType types, so a
// consumer links the pack without inheriting its raster dependencies. The
// implementation lives in `plugins/maps/src/editorial_map_pack.cpp` and is
// built only when FreeType is available.
//
// Styles:
//   01 editorial_vox     warm paper + terracotta state
//   02 paper_cutout      raised state plate with cast shadow
//   03 dark_radar        night grid + concentric radar pulse
//   04 blueprint         cobalt sheet + 45-degree hatch
//   05 connection_arc    two-point Bezier corridor
//   06 floating_island   isolated silhouette + callout cartouche
//
// Geometry defaults to Indiana / Indianapolis, matching the reference brief.
// `highlight_state` accepts a state name ("Indiana") or postal code ("IN").

#include <cstdint>
#include <string>
#include <vector>

namespace chrononplugin::maps {

enum class EditorialMapStyle : std::uint8_t {
    kEditorialVox = 0,
    kPaperCutout,
    kDarkRadar,
    kBlueprint,
    kConnectionArc,
    kFloatingIsland,
};

struct EditorialMapRequest {
    EditorialMapStyle style = EditorialMapStyle::kEditorialVox;

    std::string output_video;  // required; the pack encodes this file
    std::string poster_png;    // optional; empty disables the poster

    int output_width = 1920;
    int output_height = 1080;
    int fps = 30;
    // Optional exact encoder timebase. When both are positive they win over
    // `fps`, so a 30000/1001 delivery keeps its rational frame rate.
    int fps_num = 0;
    int fps_den = 1;
    int frame_count = 240;
    int crf = 18;
    bool gpu_encoder = false;
    int gpu_index = 0;
    int gpu_quality = 15;
    int threads = 0;  // 0 = OpenCV/default pool, >0 = explicit worker count
    std::string ffmpeg = "ffmpeg";

    // Empty strings use the compile-time workspace defaults.
    std::string states_geojson;
    std::string countries_geojson;
    std::string fonts_dir;

    // Editorial content. Defaults reproduce the Indiana / Washington D.C.
    // reference pair.
    std::string highlight_state = "Indiana";
    // Optional country target. When set, the pack composes the same editorial
    // treatment around this country instead of using the US-states catalog.
    std::string highlight_country;
    std::string title = "INDIANA";
    std::string subtitle = "THE HOOSIER STATE";
    std::string pin_label = "Indianapolis";
    double pin_latitude = 39.7684;
    double pin_longitude = -86.1581;
};

struct EditorialMapResult {
    bool ok = false;
    std::string error;
    int frame_count = 0;
    double render_seconds = 0.0;
};

[[nodiscard]] const char* editorial_map_style_id(EditorialMapStyle style) noexcept;

[[nodiscard]] std::vector<EditorialMapStyle> editorial_map_styles();

/// Renders one style. The pack is fail-closed: a malformed request, a missing
/// geometry/font, a failed encoder or a short write returns ok=false and leaves
/// no partial video at output_video.
[[nodiscard]] EditorialMapResult render_editorial_map(const EditorialMapRequest& request);

}  // namespace chrononplugin::maps
