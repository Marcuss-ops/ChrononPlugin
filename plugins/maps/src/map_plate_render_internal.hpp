#pragma once

// Private to plugins/maps: the closed encoder contract and the payload
// normalisation shared by the map-plate render pipeline. map_plate_render.cpp
// is split into encoder/payload/pipeline translation units; only these entities
// cross the split. This header is not part of the public plugin contract.

#include "chrononplugin/maps/geo_feature.hpp"
#include "chrononplugin/maps/map_plate_render.hpp"

#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>

#include <chrono>
#include <string>
#include <utility>
#include <vector>

namespace chrononplugin::maps::map_plate_detail {

/// The closed encoder profiles the product accepts (mirrors
/// render_dynamic_map_overlay.resolve_encoder_profile, exact tuples only).
struct EncoderProfile {
    std::string codec;
    bool gpu = false;
    std::string preset;
    int quality = 0;
};

bool resolve_encoder_profile(const nlohmann::json& profile, EncoderProfile& out,
                             std::string& error);

/// render_dynamic_map_overlay.encoder_arguments (exact argument order).
std::vector<std::string> encoder_arguments(const EncoderProfile& profile);

/// Shell single-quote escaping for the ffmpeg command line.
[[nodiscard]] std::string shell_quote(const std::string& value);

/// The rawvideo-in / H.264-out ffmpeg invocation writing `staged_video`.
[[nodiscard]] std::string map_plate_ffmpeg_command(
    const MapPlateRequest& request, int fps_num, int fps_den,
    const EncoderProfile& profile, const std::string& staged_video);

[[nodiscard]] double seconds_since(std::chrono::steady_clock::time_point start);

struct Pin {
    std::string text;
    double lat = 0.0;
    double lon = 0.0;
};

struct PlaceViewport {
    std::pair<double, double> anchor{0.0, 0.0};
    double fit_zoom = 5.2;
};

std::string normalized_place(std::string value);

bool feature_name_matches(const GeoFeature& feature, const std::string& place,
                          const std::vector<std::string>& keys);

bool load_named_place(const std::string& catalog, const std::string& requested,
                      std::vector<GeoPolygon>& polygons, PlaceViewport& viewport,
                      int width, int height, std::string& error);

void draw_named_place_fill(cv::Mat& frame, const std::vector<GeoPolygon>& polygons,
                           std::pair<double, double> anchor, double zoom,
                           double progress);

/// normalize_map_pins: one stop per coordinate (round-4 key), fail-closed
/// above the five-stop route budget.
bool normalize_pins(const nlohmann::json& raw_pins, std::vector<Pin>& pins,
                    std::string& error, bool allow_unlabelled = false);

[[nodiscard]] const char* catalog_dir();

/// The payload keys the renderer knows. Keys outside this set are refused: the
/// renderer never ignores a field it does not understand.
[[nodiscard]] bool known_payload_keys(const nlohmann::json& payload,
                                      std::string& error);

/// Pin-level fields follow the same rule as the payload: only the grounded
/// place identity is known.
[[nodiscard]] bool known_pin_keys(const nlohmann::json& payload,
                                  std::string& error);

/// Semantic fields the native subset does not certify yet: any non-default
/// value is refused by name (the port adds them one parity gate at a time).
[[nodiscard]] bool supported_semantics(const nlohmann::json& payload,
                                       std::string& error);

}  // namespace chrononplugin::maps::map_plate_detail
