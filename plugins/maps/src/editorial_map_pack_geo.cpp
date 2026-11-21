// Editorial pack geometry: Web Mercator view, catalog loading, projected
// fills/strokes and the alpha drawing primitives. Split out of
// editorial_map_pack.cpp.

#include "editorial_map_pack_internal.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace chrononplugin::maps::editorial_pack_detail {

[[nodiscard]] cv::Scalar rgb(unsigned int hex) {
    return cv::Scalar(static_cast<double>(hex & 0xFF),
                      static_cast<double>((hex >> 8) & 0xFF),
                      static_cast<double>((hex >> 16) & 0xFF));
}

[[nodiscard]] double clamp01(double value) {
    return std::clamp(value, 0.0, 1.0);
}

[[nodiscard]] double smooth01(double start, double end, double t) {
    if (t <= start) return 0.0;
    if (t >= end) return 1.0;
    const double p = (t - start) / (end - start);
    return p * p * (3.0 - 2.0 * p);
}

[[nodiscard]] double ease_out_cubic(double p) {
    const double q = 1.0 - clamp01(p);
    return 1.0 - q * q * q;
}

[[nodiscard]] std::string upper_copy(std::string text) {
    for (char& c : text) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return text;
}

// ── Web Mercator layout ─────────────────────────────────────────────────────

[[nodiscard]] double mercator_x(double lon) {
    return (lon + 180.0) / 360.0;
}

[[nodiscard]] double mercator_y(double lat) {
    const double clamped = std::clamp(lat, -85.05112878, 85.05112878);
    return (1.0 - std::asinh(std::tan(clamped * kPi / 180.0)) / kPi) * 0.5;
}


[[nodiscard]] View fit_box(const LonLatBox& box, const cv::Rect2d& rect) {
    View view;
    const double x0 = mercator_x(box.lon_min);
    const double x1 = mercator_x(box.lon_max);
    const double y0 = mercator_y(box.lat_max);
    const double y1 = mercator_y(box.lat_min);
    const double span_x = std::max(1e-12, x1 - x0);
    const double span_y = std::max(1e-12, y1 - y0);
    view.scale = std::min(rect.width / span_x, rect.height / span_y);
    view.mx0 = (x0 + x1) * 0.5;
    view.my0 = (y0 + y1) * 0.5;
    view.cx = rect.x + rect.width * 0.5;
    view.cy = rect.y + rect.height * 0.5;
    return view;
}

[[nodiscard]] View scaled_view(const View& view, double factor) {
    View out = view;
    out.scale *= factor;
    out.cx *= factor;
    out.cy *= factor;
    return out;
}

[[nodiscard]] std::vector<cv::Point> project_ring(
    const std::vector<GeoPosition>& ring, const View& view) {
    std::vector<cv::Point> out;
    out.reserve(ring.size());
    for (const GeoPosition& position : ring) {
        out.push_back(view.point(position[1], position[0]));
    }
    return out;
}

[[nodiscard]] std::vector<std::vector<cv::Point>> project_rings(
    const GeoFeature& feature, const View& view) {
    std::vector<std::vector<cv::Point>> out;
    for (const GeoPolygon& polygon : feature.polygons) {
        for (const auto& ring : polygon.rings) {
            auto projected = project_ring(ring, view);
            if (projected.size() >= 3) out.push_back(std::move(projected));
        }
    }
    return out;
}

void fill_rings(cv::Mat& frame, const std::vector<std::vector<cv::Point>>& rings,
                const cv::Scalar& color) {
    for (const auto& ring : rings) {
        if (ring.size() < 3) continue;
        std::vector<std::vector<cv::Point>> polygon{ring};
        cv::fillPoly(frame, polygon, color, cv::LINE_AA);
    }
}

void stroke_rings(cv::Mat& frame, const std::vector<std::vector<cv::Point>>& rings,
                  const cv::Scalar& color, int thickness) {
    for (const auto& ring : rings) {
        if (ring.size() < 2) continue;
        cv::polylines(frame, ring, true, color, thickness, cv::LINE_AA);
    }
}

void fill_feature(cv::Mat& frame, const GeoFeature& feature, const View& view,
                  const cv::Scalar& fill, const cv::Scalar& hole_color) {
    for (const GeoPolygon& polygon : feature.polygons) {
        if (polygon.rings.empty()) continue;
        auto exterior = project_ring(polygon.rings.front(), view);
        if (exterior.size() >= 3) {
            std::vector<std::vector<cv::Point>> shape{exterior};
            cv::fillPoly(frame, shape, fill, cv::LINE_AA);
        }
        for (std::size_t i = 1; i < polygon.rings.size(); ++i) {
            auto hole = project_ring(polygon.rings[i], view);
            if (hole.size() >= 3) {
                std::vector<std::vector<cv::Point>> shape{hole};
                cv::fillPoly(frame, shape, hole_color, cv::LINE_AA);
            }
        }
    }
}

void stroke_feature(cv::Mat& frame, const GeoFeature& feature, const View& view,
                    const cv::Scalar& color, int thickness) {
    for (const GeoPolygon& polygon : feature.polygons) {
        for (const auto& ring : polygon.rings) {
            auto projected = project_ring(ring, view);
            if (projected.size() >= 2) {
                cv::polylines(frame, projected, true, color, thickness, cv::LINE_AA);
            }
        }
    }
}

[[nodiscard]] bool is_contiguous_state(const GeoFeature& feature) {
    const auto found = feature.properties.find("postal");
    if (found == feature.properties.end()) return true;
    return found->second != "AK" && found->second != "HI";
}

[[nodiscard]] LonLatBox box_of(const GeoFeature& feature) {
    LonLatBox box;
    for (const GeoPolygon& polygon : feature.polygons) {
        for (const auto& ring : polygon.rings) {
            for (const GeoPosition& position : ring) {
                box.add(position[0], position[1]);
            }
        }
    }
    return box;
}

[[nodiscard]] const GeoFeature* find_feature(const GeoFeatureCollection& collection,
                                             const std::string& property,
                                             const std::string& value) {
    for (const GeoFeature& feature : collection.features) {
        const auto found = feature.properties.find(property);
        if (found != feature.properties.end() && found->second == value) {
            return &feature;
        }
    }
    return nullptr;
}

[[nodiscard]] GeoFeatureCollection load_geojson(const fs::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open GeoJSON: " + path.string());
    }
    const std::string text((std::istreambuf_iterator<char>(input)), {});
    return chrononplugin::maps::parse_geojson(text);
}

[[nodiscard]] fs::path resolve_catalog_file(const std::string& requested,
                                            const char* env_name,
                                            const char* filename,
                                            const char* fallback) {
    if (!requested.empty()) return fs::path(requested);
    if (const char* directory = std::getenv(env_name);
        directory != nullptr && *directory != '\0') {
        const fs::path candidate = fs::path(directory) / filename;
        if (fs::exists(candidate)) return candidate;
    }
    return fs::path(fallback);
}

// Same normalisation the plate pipeline uses for `map_place`, so both renderers
// accept "Toscana", "toscana" and "Tuscany" for the same admin area.
[[nodiscard]] std::string normalized_place_key(std::string value) {
    std::string out;
    out.reserve(value.size());
    for (const unsigned char ch : value) {
        if (std::isspace(ch) || ch == '-' || ch == '_' || ch == '\'') continue;
        out.push_back(static_cast<char>(std::tolower(ch)));
    }
    if (out == "italia") return "italy";
    if (out == "tuscany") return "toscana";
    if (out == "lombardy") return "lombardia";
    if (out == "piedmont") return "piemonte";
    if (out == "apulia") return "puglia";
    if (out == "sardinia") return "sardegna";
    if (out == "sicily") return "sicilia";
    return out;
}

// Country-level requests are the common case; a region or a city is resolved
// from the bundled Italian admin catalogs that live next to the country file.
[[nodiscard]] bool load_admin_place(const fs::path& countries_path,
                                    const std::string& requested,
                                    GeoFeatureCollection& places) {
    const std::string key = normalized_place_key(requested);
    const fs::path base = countries_path.parent_path() / "italy_admin";
    for (const char* file : {"geoBoundaries_ITA_ADM3.simplified.geojson",
                             "geoBoundaries_ITA_ADM2.simplified.geojson",
                             "geoBoundaries_ITA_ADM1.simplified.geojson"}) {
        const fs::path path = base / file;
        if (!fs::exists(path)) continue;
        GeoFeatureCollection collection;
        try {
            collection = load_geojson(path);
        } catch (const std::exception&) {
            continue;
        }
        for (const GeoFeature& feature : collection.features) {
            const auto name = feature.properties.find("shapeName");
            if (name == feature.properties.end() ||
                normalized_place_key(name->second) != key) {
                continue;
            }
            places.features.push_back(feature);
        }
        if (!places.features.empty()) return true;
    }
    return false;
}

[[nodiscard]] Geometry load_geometry(const fs::path& states_path,
                                     const fs::path& countries_path,
                                     const std::string& highlight_state,
                                     const std::string& highlight_country) {
    Geometry geometry;
    geometry.states = load_geojson(states_path);
    try {
        geometry.countries = load_geojson(countries_path);
    } catch (const std::exception& exc) {
        std::cerr << "[map-style] country context unavailable: " << exc.what() << "\n";
    }
    if (!highlight_country.empty()) {
        geometry.highlighted = find_feature(geometry.countries, "ADMIN",
                                            highlight_country);
        if (geometry.highlighted == nullptr) {
            geometry.highlighted = find_feature(geometry.countries, "NAME",
                                                highlight_country);
        }
        if (geometry.highlighted == nullptr) {
            if (!load_admin_place(countries_path, highlight_country,
                                  geometry.places)) {
                throw std::runtime_error(
                    "highlight country not found in the country catalog and no "
                    "Italian admin area matches: " + highlight_country);
            }
            geometry.region_mode = true;
            geometry.highlighted = &geometry.places.features.front();
            geometry.place_name = highlight_country;
        }
        geometry.country_mode = true;
        geometry.target_box = box_of(*geometry.highlighted);
        // A country is padded in absolute degrees (it spans continents); an
        // admin area is padded relative to its own extent so the framing stays
        // comparable to the plate renderer's `map_place` fit.
        const bool region = geometry.region_mode;
        const double lon_pad = region
            ? std::max(0.25, (geometry.target_box.lon_max -
                              geometry.target_box.lon_min) * 0.30)
            : std::max(4.0, (geometry.target_box.lon_max -
                             geometry.target_box.lon_min) * 0.40);
        const double lat_pad = region
            ? std::max(0.20, (geometry.target_box.lat_max -
                              geometry.target_box.lat_min) * 0.30)
            : std::max(3.0, (geometry.target_box.lat_max -
                             geometry.target_box.lat_min) * 0.30);
        if (region) {
            geometry.place_pin_lat =
                (geometry.target_box.lat_min + geometry.target_box.lat_max) * 0.5;
            geometry.place_pin_lon =
                (geometry.target_box.lon_min + geometry.target_box.lon_max) * 0.5;
        }
        geometry.country_view_box.add(geometry.target_box.lon_min - lon_pad,
                                      geometry.target_box.lat_min - lat_pad);
        geometry.country_view_box.add(geometry.target_box.lon_max + lon_pad,
                                      geometry.target_box.lat_max + lat_pad);
        return geometry;
    }
    const std::string highlight = highlight_state.empty() ? "IN" : highlight_state;
    geometry.highlighted = find_feature(geometry.states, "postal", highlight);
    if (geometry.highlighted == nullptr) {
        geometry.highlighted = find_feature(geometry.states, "name", highlight);
    }
    if (geometry.highlighted == nullptr) {
        throw std::runtime_error("highlight state not found in the state catalog: " +
                                 highlight);
    }
    geometry.target_box = box_of(*geometry.highlighted);
    for (const GeoFeature& feature : geometry.states.features) {
        if (!is_contiguous_state(feature)) continue;
        const LonLatBox box = box_of(feature);
        geometry.lower48.add(box.lon_min, box.lat_min);
        geometry.lower48.add(box.lon_max, box.lat_max);
    }
    if (!geometry.lower48.valid()) {
        throw std::runtime_error("the state catalog has no contiguous US geometry");
    }
    return geometry;
}

void blend_mask(cv::Mat& frame, const cv::Mat& mask, const cv::Scalar& shade,
                double alpha) {
    if (mask.type() != CV_8UC1 || mask.size() != frame.size()) return;
    for (int y = 0; y < frame.rows; ++y) {
        const unsigned char* mask_row = mask.ptr<unsigned char>(y);
        cv::Vec3b* row = frame.ptr<cv::Vec3b>(y);
        for (int x = 0; x < frame.cols; ++x) {
            const double amount = (mask_row[x] / 255.0) * alpha;
            if (amount <= 0.0) continue;
            const double keep = 1.0 - amount;
            row[x][0] = cv::saturate_cast<unsigned char>(row[x][0] * keep + shade[0] * amount);
            row[x][1] = cv::saturate_cast<unsigned char>(row[x][1] * keep + shade[1] * amount);
            row[x][2] = cv::saturate_cast<unsigned char>(row[x][2] * keep + shade[2] * amount);
        }
    }
}

[[nodiscard]] cv::Rect clamp_roi(const cv::Mat& frame, const cv::Rect& rect) {
    const int x0 = std::max(0, rect.x);
    const int y0 = std::max(0, rect.y);
    const int x1 = std::min(frame.cols, rect.x + rect.width);
    const int y1 = std::min(frame.rows, rect.y + rect.height);
    return {x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

void draw_line_alpha(cv::Mat& frame, cv::Point from, cv::Point to,
                     const cv::Scalar& color, int thickness, double alpha) {
    if (alpha <= 0.001) return;
    const int pad = std::max(2, thickness + 2);
    const cv::Rect roi = clamp_roi(
        frame, {std::min(from.x, to.x) - pad, std::min(from.y, to.y) - pad,
                std::abs(to.x - from.x) + pad * 2 + 1,
                std::abs(to.y - from.y) + pad * 2 + 1});
    if (roi.empty()) return;
    cv::Mat target = frame(roi);
    cv::Mat layer = target.clone();
    cv::line(layer, from - roi.tl(), to - roi.tl(), color, thickness,
             cv::LINE_AA);
    cv::addWeighted(target, 1.0 - alpha, layer, alpha, 0.0, target);
}

void draw_rect_alpha(cv::Mat& frame, const cv::Rect& rect, const cv::Scalar& fill,
                     const cv::Scalar& border, int border_thickness, double alpha) {
    if (alpha <= 0.001) return;
    const int pad = std::max(2, border_thickness + 2);
    const cv::Rect roi = clamp_roi(
        frame, {rect.x - pad, rect.y - pad, rect.width + pad * 2,
                rect.height + pad * 2});
    if (roi.empty()) return;
    cv::Mat target = frame(roi);
    cv::Mat layer = target.clone();
    const cv::Rect local(rect.x - roi.x, rect.y - roi.y, rect.width, rect.height);
    cv::rectangle(layer, local, fill, cv::FILLED, cv::LINE_AA);
    if (border_thickness > 0) {
        cv::rectangle(layer, local, border, border_thickness, cv::LINE_AA);
    }
    cv::addWeighted(target, 1.0 - alpha, layer, alpha, 0.0, target);
}

void draw_disc_alpha(cv::Mat& frame, cv::Point center, int radius,
                     const cv::Scalar& color, double alpha) {
    if (alpha <= 0.001 || radius <= 0) return;
    const cv::Rect roi = clamp_roi(
        frame, {center.x - radius - 2, center.y - radius - 2,
                radius * 2 + 5, radius * 2 + 5});
    if (roi.empty()) return;
    cv::Mat target = frame(roi);
    cv::Mat layer = target.clone();
    cv::Mat mask(roi.size(), CV_8UC1, cv::Scalar(0));
    const cv::Point local = center - roi.tl();
    cv::circle(layer, local, radius, color, cv::FILLED, cv::LINE_AA);
    cv::circle(mask, local, radius, cv::Scalar(255), cv::FILLED, cv::LINE_AA);
    cv::Mat blended;
    cv::addWeighted(target, 1.0 - alpha, layer, alpha, 0.0, blended);
    blended.copyTo(target, mask);
}

void draw_ring_alpha(cv::Mat& frame, cv::Point center, int radius,
                     const cv::Scalar& color, int thickness, double alpha) {
    if (alpha <= 0.001 || radius <= 0) return;
    const int pad = thickness + 3;
    const cv::Rect roi = clamp_roi(
        frame, {center.x - radius - pad, center.y - radius - pad,
                radius * 2 + pad * 2 + 1, radius * 2 + pad * 2 + 1});
    if (roi.empty()) return;
    cv::Mat target = frame(roi);
    cv::Mat layer = target.clone();
    cv::Mat mask(roi.size(), CV_8UC1, cv::Scalar(0));
    const cv::Point local = center - roi.tl();
    cv::circle(layer, local, radius, color, thickness, cv::LINE_AA);
    cv::circle(mask, local, radius, cv::Scalar(255), thickness + 1, cv::LINE_AA);
    cv::Mat blended;
    cv::addWeighted(target, 1.0 - alpha, layer, alpha, 0.0, blended);
    blended.copyTo(target, mask);
}

void draw_country_context(cv::Mat& frame, const GeoFeatureCollection& countries,
                          const View& view, const cv::Scalar& fill,
                          const cv::Scalar& border, int thickness,
                          const cv::Scalar& water) {
    for (const GeoFeature& country : countries.features) {
        const auto admin = country.properties.find("ADMIN");
        if (admin != country.properties.end() &&
            admin->second == "United States of America") {
            continue;
        }
        fill_feature(frame, country, view, fill, water);
        stroke_feature(frame, country, view, border, thickness);
    }
}

void draw_states(cv::Mat& frame, const GeoFeatureCollection& states, const View& view,
                 const cv::Scalar& fill, const cv::Scalar& border, int thickness,
                 const cv::Scalar& water, bool outline_only) {
    for (const GeoFeature& state : states.features) {
        if (!is_contiguous_state(state)) continue;
        if (!outline_only) fill_feature(frame, state, view, fill, water);
        stroke_feature(frame, state, view, border, thickness);
    }
}

void draw_region_context(cv::Mat& frame, const Geometry& geometry,
                         const View& view, const cv::Scalar& fill,
                         const cv::Scalar& border, int thickness,
                         const cv::Scalar& water, bool outline_only) {
    if (geometry.country_mode) {
        draw_country_context(frame, geometry.countries, view, fill, border,
                             thickness, water);
        if (geometry.region_mode) {
            // Italian admin borders give the zoomed country its internal
            // structure; the highlighted area is filled separately.
            for (const GeoFeature& place : geometry.places.features) {
                if (&place == geometry.highlighted) continue;
                stroke_feature(frame, place, view, border, thickness);
            }
        }
    } else {
        draw_states(frame, geometry.states, view, fill, border, thickness,
                    water, outline_only);
    }
}

void draw_graticule(cv::Mat& frame, const View& view, const cv::Scalar& color,
                    int thickness, double step, bool crosses) {
    for (double lon = -180.0; lon <= 180.0; lon += step) {
        const cv::Point a = view.point(-10.0, lon);
        const cv::Point b = view.point(70.0, lon);
        cv::line(frame, a, b, color, thickness, cv::LINE_AA);
    }
    for (double lat = -80.0; lat <= 80.0; lat += step) {
        const cv::Point a = view.point(lat, -180.0);
        const cv::Point b = view.point(lat, 180.0);
        cv::line(frame, a, b, color, thickness, cv::LINE_AA);
    }
    if (!crosses) return;
    for (double lon = -180.0; lon <= 180.0; lon += step) {
        for (double lat = -80.0; lat <= 80.0; lat += step) {
            const cv::Point center = view.point(lat, lon);
            if (center.x < 0 || center.y < 0 || center.x >= frame.cols ||
                center.y >= frame.rows) {
                continue;
            }
            const int arm = 4;
            cv::line(frame, {center.x - arm, center.y}, {center.x + arm, center.y},
                     color, thickness, cv::LINE_AA);
            cv::line(frame, {center.x, center.y - arm}, {center.x, center.y + arm},
                     color, thickness, cv::LINE_AA);
        }
    }
}

void draw_hatch(cv::Mat& frame, const GeoFeature& feature, const View& view,
                const cv::Scalar& color, int spacing, int thickness) {
    cv::Mat mask(frame.size(), CV_8UC1, cv::Scalar(0));
    for (const GeoPolygon& polygon : feature.polygons) {
        if (polygon.rings.empty()) continue;
        auto exterior = project_ring(polygon.rings.front(), view);
        if (exterior.size() >= 3) {
            std::vector<std::vector<cv::Point>> shape{exterior};
            cv::fillPoly(mask, shape, cv::Scalar(255), cv::LINE_AA);
        }
    }
    cv::Mat hatch(frame.size(), CV_8UC1, cv::Scalar(0));
    for (int offset = -frame.rows; offset < frame.cols + frame.rows;
         offset += std::max(3, spacing)) {
        cv::line(hatch, {offset, 0}, {offset + frame.rows, frame.rows - 1},
                 cv::Scalar(255), thickness, cv::LINE_AA);
    }
    cv::bitwise_and(hatch, mask, hatch);
    cv::Mat colored(frame.size(), CV_8UC3, color);
    cv::Mat blended;
    cv::addWeighted(frame, 0.08, colored, 0.92, 0.0, blended);
    blended.copyTo(frame, hatch);
}
}  // namespace chrononplugin::maps::editorial_pack_detail
