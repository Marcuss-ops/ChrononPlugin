// Payload normalisation and fail-closed validation of the native map renderer.
// Split out of map_plate_render.cpp.

#include "chrononplugin/maps/map_plate_render.hpp"

#include "map_plate_render_internal.hpp"

#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <opencv2/imgproc.hpp>

#include "chrononplugin/maps/geojson.hpp"
#include "chrononplugin/maps/map_camera.hpp"
#include "chrononplugin/maps/map_label_render.hpp"
#include "chrononplugin/maps/map_plate_raster.hpp"
#include "chrononplugin/maps/map_route_render.hpp"

namespace chrononplugin::maps::map_plate_detail {

std::string normalized_place(std::string value) {
    std::string out;
    out.reserve(value.size());
    for (const unsigned char ch : value) {
        if (std::isspace(ch) || ch == '-' || ch == '_') continue;
        out.push_back(static_cast<char>(std::tolower(ch)));
    }
    if (out == "italia") return "italy";
    if (out == "milan") return "milano";
    if (out == "lombardy") return "lombardia";
    if (out == "tuscany") return "toscana";
    return out;
}

bool feature_name_matches(const GeoFeature& feature, const std::string& place,
                          const std::vector<std::string>& keys) {
    for (const auto& key : keys) {
        const auto it = feature.properties.find(key);
        if (it != feature.properties.end() &&
            normalized_place(it->second) == place) return true;
    }
    return false;
}

bool load_named_place(const std::string& catalog, const std::string& requested,
                      std::vector<GeoPolygon>& polygons, PlaceViewport& viewport,
                      int width, int height, std::string& error) {
    std::string place = normalized_place(requested);
    const std::size_t comma = place.find(',');
    if (comma != std::string::npos) place.resize(comma);
    const std::vector<std::pair<std::string, std::vector<std::string>>> sources{
        {catalog + "/ne_50m_admin_0_countries.geojson", {"ADMIN", "NAME"}},
        {catalog + "/italy_admin/geoBoundaries_ITA_ADM3.simplified.geojson", {"shapeName"}},
        {catalog + "/italy_admin/geoBoundaries_ITA_ADM2.simplified.geojson", {"shapeName"}},
        {catalog + "/italy_admin/geoBoundaries_ITA_ADM1.simplified.geojson", {"shapeName"}},
    };
    try {
        for (const auto& [path, keys] : sources) {
            std::shared_ptr<const GeoFeatureCollection> collection;
            try {
                collection = load_geojson_file_cached(path);
            } catch (const std::exception&) {
                continue;
            }
            for (const auto& feature : collection->features) {
                if (!feature_name_matches(feature, place, keys)) continue;
                polygons = feature.polygons;
            double min_lon = 180.0, max_lon = -180.0;
            double min_lat = 90.0, max_lat = -90.0;
            for (const auto& polygon : polygons) {
                if (polygon.rings.empty()) continue;
                for (const auto& point : polygon.rings.front()) {
                    min_lon = std::min(min_lon, point[0]);
                    max_lon = std::max(max_lon, point[0]);
                    min_lat = std::min(min_lat, point[1]);
                    max_lat = std::max(max_lat, point[1]);
                }
            }
                if (polygons.empty() || min_lon >= max_lon || min_lat >= max_lat) {
                    error = "place '" + requested + "' has no drawable boundary";
                    return false;
                }
                const auto mercator_y = [](double lat) {
                const double clamped = std::clamp(lat, -85.05112878, 85.05112878);
                const double radians = clamped * 3.14159265358979323846 / 180.0;
                return (1.0 - std::asinh(std::tan(radians)) / 3.14159265358979323846) * 0.5;
            };
                const double span_x = (max_lon - min_lon) / 360.0;
                const double span_y = std::abs(mercator_y(max_lat) - mercator_y(min_lat));
                const double fit_x = (width * 0.78) / (256.0 * span_x);
                const double fit_y = (height * 0.82) / (256.0 * span_y);
                viewport.fit_zoom = std::clamp(std::log2(std::min(fit_x, fit_y)), 0.0, 18.0);
                const double north_y = mercator_y(max_lat);
                const double south_y = mercator_y(min_lat);
                const double center_y = (north_y + south_y) * 0.5;
                const double center_lat = std::atan(std::sinh(
                3.14159265358979323846 * (1.0 - 2.0 * center_y))) *
                180.0 / 3.14159265358979323846;
                viewport.anchor = {center_lat, (min_lon + max_lon) * 0.5};
                return true;
            }
        }
    } catch (const std::exception& exc) {
        error = "cannot resolve map_place '" + requested + "': " + exc.what();
        return false;
    }
    error = "map_place '" + requested + "' was not found in the local country or Italy administrative boundary catalogs";
    return false;
}

void draw_named_place_fill(cv::Mat& frame, const std::vector<GeoPolygon>& polygons,
                           std::pair<double, double> anchor, double zoom,
                           double progress) {
    if (frame.empty() || polygons.empty()) return;
    cv::Mat mask(frame.size(), CV_8UC1, cv::Scalar(0));
    for (const auto& polygon : polygons) {
        for (std::size_t ring = 0; ring < polygon.rings.size(); ++ring) {
            std::vector<cv::Point> points;
            for (const auto& coordinate : polygon.rings[ring]) {
                const auto [x, y] = geo_to_screen(coordinate[1], coordinate[0], anchor,
                    zoom, frame.cols, frame.rows);
                points.emplace_back(x, y);
            }
            if (points.size() < 3) continue;
            cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{points},
                         cv::Scalar(ring == 0 ? 255 : 0), cv::LINE_AA);
        }
    }
    const double alpha = 0.48 * std::clamp(progress, 0.0, 1.0);
    cv::Mat color(frame.size(), CV_8UC3, cv::Scalar(54, 92, 226));
    cv::Mat blended;
    cv::addWeighted(frame, 1.0 - alpha, color, alpha, 0.0, blended);
    blended.copyTo(frame, mask);
    cv::Mat edge(frame.size(), CV_8UC1, cv::Scalar(0));
    cv::Canny(mask, edge, 60, 140);
    cv::Mat outline(frame.size(), CV_8UC3, cv::Scalar(255, 180, 84));
    outline.copyTo(frame, edge);
}

/// normalize_map_pins: one stop per coordinate (round-4 key), fail-closed
/// above the five-stop route budget.
bool normalize_pins(const nlohmann::json& raw_pins, std::vector<Pin>& pins,
                    std::string& error, bool allow_unlabelled) {
    if (!raw_pins.is_array()) {
        error = "pins must be an array";
        return false;
    }
    std::set<std::pair<long, long>> seen;
    for (const auto& raw : raw_pins) {
        Pin pin;
        if (!raw.contains("latitude") || !raw.contains("longitude")) {
            error = "every pin requires latitude and longitude";
            return false;
        }
        pin.lat = raw.at("latitude").get<double>();
        pin.lon = raw.at("longitude").get<double>();
        const std::string label = raw.value("label", "");
        pin.text = label.empty() ? raw.value("id", "") : label;
        if (pin.text.empty() && !allow_unlabelled) {
            error = "every pin requires a label or id";
            return false;
        }
        const auto key = std::make_pair(
            std::lround(pin.lat * 1e4), std::lround(pin.lon * 1e4));
        if (seen.insert(key).second) {
            pins.push_back(std::move(pin));
        }
    }
    if (pins.empty() && !allow_unlabelled) {
        error = "the map plate requires at least one pin";
        return false;
    }
    if (pins.size() > 5) {
        error = "map group exceeds the five-stop route budget; the planner must "
                "split it (the renderer never silently drops a grounded place)";
        return false;
    }
    return true;
}

[[nodiscard]] const char* catalog_dir() {
    if (const char* env = std::getenv("CHRONON_MAPS_CATALOG_DIR")) {
        return env;
    }
#ifdef CHRONON_MAPS_CATALOG_DIR_DEFAULT
    return CHRONON_MAPS_CATALOG_DIR_DEFAULT;
#else
    return "";
#endif
}

/// The payload keys the renderer knows. Keys outside this set are refused:
/// the renderer never ignores a field it does not understand.
///
///  * metadata (map_group, composition_family, ...) are planning provenance
///    and carry no pixel semantics: accepted and recorded by the consumer;
///  * semantic keys must state what the native subset certifies, otherwise
///    the request is refused with kNotImplemented — never approximated.
[[nodiscard]] bool known_payload_keys(const nlohmann::json& payload,
                                      std::string& error) {
    static const std::set<std::string> kKnown = {
        // media contract
        "width", "height", "fps_num", "fps_den", "duration_us",
        // planning provenance (no pixel semantics)
        "plate_id", "map_group", "map_group_strategy", "composition_family",
        "composition_fallback", "typography",
        // semantics, verified below
        "pins", "pin_timing", "encoder_profile", "sampler",
        "camera_animation", "label_animation", "map_animation", "basemap",
        "route_timing", "country_context", "area_glow_radius_km",
        "camera_anchor", "camera_start_zoom", "camera_end_zoom",
        "camera_motion_duration_us", "map_level",
        "map_place", "show_location_tag", "show_text",
        "map_preset",
        "territory_feature", "territory_geojson", "territory_subtitle",
        "country_feature", "timeline_label", "mountain_overlay_asset",
        "documentary_finish", "route_altitudes_m",
    };
    for (auto it = payload.begin(); it != payload.end(); ++it) {
        if (kKnown.find(it.key()) == kKnown.end()) {
            error = "payload field outside the native map subset: " + it.key();
            return false;
        }
    }
    return true;
}

/// Pin-level fields follow the same rule as the payload: only the grounded
/// place identity is known. Per-pin presentation (accent color, cards,
/// subtitles) belongs to composed presentations that are not ported, so it is
/// refused by name — the renderer never silently ignores a field it does not
/// draw.
[[nodiscard]] bool known_pin_keys(const nlohmann::json& payload,
                                  std::string& error) {
    static const std::set<std::string> kKnownPinKeys = {
        "id", "label", "latitude", "longitude",
    };
    const auto& pins = payload.find("pins");
    if (pins == payload.end()) {
        return true;
    }
    for (const auto& raw : *pins) {
        if (!raw.is_object()) {
            continue;  // shape errors belong to normalize_pins
        }
        for (auto it = raw.begin(); it != raw.end(); ++it) {
            if (kKnownPinKeys.find(it.key()) == kKnownPinKeys.end()) {
                error = "pin field outside the native map subset: " + it.key();
                return false;
            }
        }
    }
    return true;
}

/// Semantic fields the native subset does not certify yet: any non-default
/// value is refused by name (the port adds them one parity gate at a time).
[[nodiscard]] bool supported_semantics(const nlohmann::json& payload,
                                       std::string& error) {
    const std::string map_animation = payload.value("map_animation", "");
    const bool has_map_place = payload.contains("map_place") &&
        payload.at("map_place").is_string() &&
        !payload.at("map_place").get<std::string>().empty();
    if (supported_map_animations().find(map_animation) ==
        supported_map_animations().end()) {
        error = "map composition '" + map_animation +
                "' is not ported natively yet";
        return false;
    }
    const auto& pin_timing = payload.find("pin_timing");
    if (pin_timing != payload.end() && !pin_timing->is_null() &&
        !pin_timing->empty()) {
        error = "per-pin spoken timing schedules are not ported natively yet";
        return false;
    }
    if (payload.contains("route_timing")) {
        const auto& timing = payload.at("route_timing");
        if (!timing.is_object() || !timing.contains("start_us") ||
            !timing.contains("travel_us") ||
            timing.value("start_us", std::int64_t{-1}) < 0 ||
            timing.value("travel_us", std::int64_t{0}) <= 0) {
            error = "route_timing requires non-negative start_us and positive travel_us";
            return false;
        }
        for (auto it = timing.begin(); it != timing.end(); ++it) {
            if (it.key() != "start_us" && it.key() != "travel_us") {
                error = "unsupported route_timing field: " + it.key();
                return false;
            }
        }
    }
    if (payload.contains("country_context")) {
        error = "country_context map tours are not ported natively yet";
        return false;
    }
    const std::string camera_animation = payload.value("camera_animation", "slow_approach");
    if (supported_map_camera_animations().count(camera_animation) == 0) {
        error = "camera_animation must be slow_approach or exponential_dive";
        return false;
    }
    if (payload.contains("map_place") && !has_map_place) {
        error = "map_place must be a non-empty place name";
        return false;
    }
    if (map_animation == "territory_glow_reveal" && !has_map_place &&
        (!payload.contains("territory_feature") ||
         !payload.at("territory_feature").is_string())) {
        error = "territory_glow_reveal requires territory_feature";
        return false;
    }
    if (map_animation == "area_glow_pulse" && !has_map_place &&
        (!std::isfinite(payload.value("area_glow_radius_km", 0.0)) ||
         payload.value("area_glow_radius_km", 0.0) <= 0.0)) {
        error = "area_glow_pulse requires a positive area_glow_radius_km";
        return false;
    }
    if ((map_animation == "continental_atlas" ||
         map_animation == "historic_location_timeline") &&
        !has_map_place &&
        (!payload.contains("country_feature") ||
         !payload.at("country_feature").is_string())) {
        error = map_animation + " requires country_feature";
        return false;
    }
    if (map_animation == "mountain_ridge_callout" && !has_map_place &&
        (!payload.contains("mountain_overlay_asset") ||
         !payload.at("mountain_overlay_asset").is_string())) {
        error = "mountain_ridge_callout requires mountain_overlay_asset";
        return false;
    }
    if (map_animation == "historic_location_timeline" && !has_map_place &&
        (!payload.contains("timeline_label") ||
         !payload.at("timeline_label").is_string())) {
        error = "historic_location_timeline requires timeline_label";
        return false;
    }
    if ((map_animation == "satellite_scan_reveal" ||
         map_animation == "territory_flood_fill" ||
         map_animation == "territory_tactical_fill" ||
         map_animation == "territory_extrusion" ||
         map_animation == "volumetric_laser_scan" ||
         map_animation == "vector_particle_flow" ||
         map_animation == "dark_mirror_diorama" ||
         map_animation == "voronoi_heat_contours" ||
         map_animation == "holographic_postfx") && !has_map_place &&
        (!payload.contains("territory_feature") ||
         !payload.at("territory_feature").is_string())) {
        error = map_animation + " requires territory_feature";
        return false;
    }
    if (payload.contains("territory_geojson") &&
        (!payload.at("territory_geojson").is_string() ||
         payload.at("territory_geojson").get<std::string>().empty())) {
        error = "territory_geojson must be a non-empty path";
        return false;
    }
    if (payload.contains("territory_subtitle") &&
        !payload.at("territory_subtitle").is_string()) {
        error = "territory_subtitle must be a string";
        return false;
    }
    const bool territory_composition =
        map_animation == "territory_glow_reveal" ||
        map_animation == "satellite_scan_reveal" ||
        map_animation == "territory_flood_fill" ||
        map_animation == "territory_tactical_fill" ||
        map_animation == "territory_extrusion" ||
        map_animation == "volumetric_laser_scan" ||
        map_animation == "vector_particle_flow" ||
        map_animation == "dark_mirror_diorama" ||
        map_animation == "voronoi_heat_contours" ||
        map_animation == "holographic_postfx";
    if (payload.contains("territory_geojson") && !territory_composition && !has_map_place) {
        error = "territory_geojson is only supported by territory compositions";
        return false;
    }
    if (payload.contains("territory_subtitle") &&
        map_animation != "territory_tactical_fill") {
        error = "territory_subtitle is only supported by territory_tactical_fill";
        return false;
    }
    if (map_animation == "city_network_expansion" && !has_map_place && payload.contains("pins") &&
        payload.at("pins").size() < 2) {
        error = "city_network_expansion requires at least two pins";
        return false;
    }
    if (payload.contains("documentary_finish") &&
        !payload.at("documentary_finish").is_object()) {
        error = "documentary_finish must be an object";
        return false;
    }
    const std::string basemap = payload.value("basemap", "");
    if (!basemap.empty() && supported_map_basemaps().count(basemap) == 0) {
        error = "basemap '" + basemap + "' is not available natively: the "
                "native renderer supports Esri World Topo Map, World Imagery "
                "and chronon_editorial";
        return false;
    }
    return true;
}
}  // namespace chrononplugin::maps::map_plate_detail
