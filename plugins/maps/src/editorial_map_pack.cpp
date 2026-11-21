// Native editorial map-style gallery — six documentary map treatments in C++.
//
// This tool renders the styles as full-frame 1920x1080 clips through the same
// C++/OpenCV map stack used by the native map renderer:
//
//   01 editorial_vox     warm paper, terracotta state, serif/sans editorial type
//   02 paper_cutout      raised state plate with a soft cast shadow
//   03 dark_radar        night-blue grid, mono type, concentric radar pulse
//   04 blueprint         cobalt technical sheet, 45-degree hatch on the state
//   05 connection_arc    tight corridor crop, two-point Bezier arc + ground shadow
//   06 floating_island   isolated state silhouette, 45-degree callout cartouche
//
// All geometry comes from Natural Earth: the simplified US-states asset built
// by build_editorial_map_assets.py plus the 50m countries catalog. Every clip
// gets the same cinematic grammar: an imperceptible 100% -> 106% push-in, a
// 0.35 s state ignition, a pin/label second beat, and typography that rises
// into place. Plates are composed at 2.12x (2x supersampling + 6% overscan) and
// resampled per frame with INTER_AREA, so state borders stay crisp under the
// push-in.
//
// Usage:
//   maps_editorial_style_gallery --out-dir out/editorial-map-styles
//     [--states asset.geojson] [--countries ne_50m...geojson]
//     [--fonts-dir Chronon3d/assets/fonts] [--only 01,03]
//     [--width 1920] [--height 1080] [--fps 30] [--seconds 8]

#include "chrononplugin/maps/editorial_map_pack.hpp"
#include "chrononplugin/maps/geojson.hpp"

#include <opencv2/core/utility.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifndef CHRONON_MAPS_EDITORIAL_STATES_DEFAULT
#define CHRONON_MAPS_EDITORIAL_STATES_DEFAULT "plugins/maps/assets/us_states_editorial.geojson"
#endif
#ifndef CHRONON_MAPS_COUNTRIES_DEFAULT
#define CHRONON_MAPS_COUNTRIES_DEFAULT "../ChrononTemplate/catalog/ne_50m_admin_0_countries.geojson"
#endif
#ifndef CHRONON_MAPS_FONTS_DIR_DEFAULT
#define CHRONON_MAPS_FONTS_DIR_DEFAULT "../Chronon3d/assets/fonts"
#endif
#ifndef CHRONON_MAPS_FONTS_DIR_SOURCE
#define CHRONON_MAPS_FONTS_DIR_SOURCE "../Chronon3d/assets/fonts"
#endif

namespace {

namespace fs = std::filesystem;
using chrononplugin::maps::GeoFeature;
using chrononplugin::maps::GeoFeatureCollection;
using chrononplugin::maps::GeoPolygon;
using chrononplugin::maps::GeoPosition;

constexpr double kPi = 3.14159265358979323846;
constexpr double kPlateScale = 2.12;  // 2x supersampling with 6% overscan.

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

struct LonLatBox {
    double lon_min = 180.0;
    double lon_max = -180.0;
    double lat_min = 90.0;
    double lat_max = -90.0;

    void add(double lon, double lat) {
        lon_min = std::min(lon_min, lon);
        lon_max = std::max(lon_max, lon);
        lat_min = std::min(lat_min, lat);
        lat_max = std::max(lat_max, lat);
    }

    [[nodiscard]] bool valid() const {
        return lon_min <= lon_max && lat_min <= lat_max;
    }
};

struct View {
    double mx0 = 0.0;
    double my0 = 0.0;
    double scale = 1.0;
    double cx = 0.0;
    double cy = 0.0;

    [[nodiscard]] cv::Point2d project(double lat, double lon) const {
        return {cx + (mercator_x(lon) - mx0) * scale,
                cy + (mercator_y(lat) - my0) * scale};
    }

    [[nodiscard]] cv::Point point(double lat, double lon) const {
        const cv::Point2d p = project(lat, lon);
        return {static_cast<int>(std::lround(p.x)),
                static_cast<int>(std::lround(p.y))};
    }
};

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

// ── FreeType text ───────────────────────────────────────────────────────────

class Font {
public:
    explicit Font(const fs::path& path) {
        if (FT_Init_FreeType(&library_) != 0) {
            throw std::runtime_error("FreeType: cannot initialise library");
        }
        if (FT_New_Face(library_, path.string().c_str(), 0, &face_) != 0) {
            FT_Done_FreeType(library_);
            library_ = nullptr;
            throw std::runtime_error("FreeType: cannot open font " + path.string());
        }
    }

    ~Font() {
        if (face_ != nullptr) FT_Done_Face(face_);
        if (library_ != nullptr) FT_Done_FreeType(library_);
    }

    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;
    Font(Font&&) = delete;
    Font& operator=(Font&&) = delete;

    [[nodiscard]] double measure(const std::string& text, int px, int tracking = 0) {
        ensure_size(px);
        double width = 0.0;
        for (const unsigned char c : text) {
            if (FT_Load_Char(face_, c, FT_LOAD_DEFAULT) != 0) continue;
            width += static_cast<double>(face_->glyph->advance.x >> 6);
            width += tracking;
        }
        if (!text.empty() && tracking != 0) width -= tracking;
        return width;
    }

    void draw(cv::Mat& frame, const std::string& text, int x, int baseline, int px,
              const cv::Scalar& color, double opacity = 1.0, int tracking = 0) {
        if (text.empty() || opacity <= 0.001) return;
        if (frame.type() != CV_8UC3) throw std::runtime_error("font target is not BGR8");
        ensure_size(px);
        int pen_x = x;
        for (const unsigned char c : text) {
            if (FT_Load_Char(face_, c, FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT) != 0) {
                continue;
            }
            const FT_GlyphSlot glyph = face_->glyph;
            const FT_Bitmap& bitmap = glyph->bitmap;
            if (bitmap.pixel_mode == FT_PIXEL_MODE_GRAY && bitmap.width > 0 &&
                bitmap.rows > 0 && bitmap.buffer != nullptr) {
                const int left = pen_x + glyph->bitmap_left;
                const int top = baseline - glyph->bitmap_top;
                for (unsigned int row = 0; row < bitmap.rows; ++row) {
                    const int y = top + static_cast<int>(row);
                    if (y < 0 || y >= frame.rows) continue;
                    const unsigned char* source =
                        bitmap.buffer + static_cast<long>(row) * bitmap.pitch;
                    cv::Vec3b* target = frame.ptr<cv::Vec3b>(y);
                    for (unsigned int col = 0; col < bitmap.width; ++col) {
                        const int xx = left + static_cast<int>(col);
                        if (xx < 0 || xx >= frame.cols) continue;
                        const double alpha =
                            (static_cast<double>(source[col]) / 255.0) * opacity;
                        if (alpha <= 0.0) continue;
                        const double keep = 1.0 - alpha;
                        target[xx][0] = cv::saturate_cast<unsigned char>(
                            target[xx][0] * keep + color[0] * alpha);
                        target[xx][1] = cv::saturate_cast<unsigned char>(
                            target[xx][1] * keep + color[1] * alpha);
                        target[xx][2] = cv::saturate_cast<unsigned char>(
                            target[xx][2] * keep + color[2] * alpha);
                    }
                }
            }
            pen_x += static_cast<int>(glyph->advance.x >> 6) + tracking;
        }
    }

private:
    void ensure_size(int px) {
        const int clamped = std::max(4, px);
        if (clamped == size_px_) return;
        if (FT_Set_Pixel_Sizes(face_, 0, static_cast<FT_UInt>(clamped)) != 0) {
            throw std::runtime_error("FreeType: cannot set pixel size");
        }
        size_px_ = clamped;
    }

    FT_Library library_ = nullptr;
    FT_Face face_ = nullptr;
    int size_px_ = 0;
};

struct Fonts {
    explicit Fonts(const fs::path& directory)
        : serif(directory / "PlayfairDisplay-Bold.ttf"),
          sans(directory / "Inter-Regular.ttf"),
          sans_bold(directory / "Inter-SemiBold.ttf"),
          mono(directory / "UbuntuMono-R.ttf") {}

    Font serif;
    Font sans;
    Font sans_bold;
    Font mono;
};

void draw_text(Font& font, cv::Mat& frame, const std::string& text, int x,
               int baseline, int px, const cv::Scalar& color, double alpha,
               int tracking = 0) {
    font.draw(frame, text, x, baseline, px, color, alpha, tracking);
}

void draw_text_centered(Font& font, cv::Mat& frame, const std::string& text,
                        int center_x, int baseline, int px,
                        const cv::Scalar& color, double alpha,
                        int tracking = 0) {
    const double width = font.measure(text, px, tracking);
    font.draw(frame, text, static_cast<int>(std::lround(center_x - width * 0.5)),
              baseline, px, color, alpha, tracking);
}

void draw_text_outlined(Font& font, cv::Mat& frame, const std::string& text,
                        int x, int baseline, int px, const cv::Scalar& color,
                        double alpha, int tracking, const cv::Scalar& outline,
                        int radius) {
    static constexpr int kOffsets[8][2] = {
        {-1, 0}, {1, 0}, {0, -1}, {0, 1},
        {-1, -1}, {1, -1}, {-1, 1}, {1, 1},
    };
    for (const auto& offset : kOffsets) {
        font.draw(frame, text, x + offset[0] * radius, baseline + offset[1] * radius,
                  px, outline, alpha * 0.55, tracking);
    }
    font.draw(frame, text, x, baseline, px, color, alpha, tracking);
}

// ── Geometry and CSS-like layers ────────────────────────────────────────────

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

struct Geometry {
    GeoFeatureCollection states;
    GeoFeatureCollection countries;
    const GeoFeature* highlighted = nullptr;
    bool country_mode = false;
    LonLatBox lower48;
    LonLatBox target_box;
    LonLatBox country_view_box;
};

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
            throw std::runtime_error("highlight country not found in the country catalog: " +
                                     highlight_country);
        }
        geometry.country_mode = true;
        geometry.target_box = box_of(*geometry.highlighted);
        const double lon_pad = std::max(4.0,
            (geometry.target_box.lon_max - geometry.target_box.lon_min) * 0.40);
        const double lat_pad = std::max(3.0,
            (geometry.target_box.lat_max - geometry.target_box.lat_min) * 0.30);
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

// Blend a filled/blurred mask with a shade color at a per-pixel alpha.
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

// ── Plates ──────────────────────────────────────────────────────────────────

[[nodiscard]] cv::Mat textured_canvas(int width, int height, unsigned int hex,
                                      unsigned int seed, double mottle,
                                      double fine) {
    cv::Mat canvas(height, width, CV_8UC3, rgb(hex));
    cv::RNG rng(seed);
    cv::Mat coarse(height / 6 + 2, width / 6 + 2, CV_8UC3);
    rng.fill(coarse, cv::RNG::NORMAL, 128.0, 26.0);
    cv::Mat coarse_full;
    cv::resize(coarse, coarse_full, cv::Size(width, height), 0.0, 0.0,
               cv::INTER_CUBIC);
    cv::addWeighted(canvas, 1.0, coarse_full, mottle, -128.0 * mottle, canvas);

    cv::Mat fine_noise(height / 3 + 2, width / 3 + 2, CV_8UC3);
    rng.fill(fine_noise, cv::RNG::NORMAL, 128.0, 18.0);
    cv::Mat fine_full;
    cv::resize(fine_noise, fine_full, cv::Size(width, height), 0.0, 0.0,
               cv::INTER_CUBIC);
    cv::addWeighted(canvas, 1.0, fine_full, fine, -128.0 * fine, canvas);
    return canvas;
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
                 const cv::Scalar& water, bool outline_only = false) {
    for (const GeoFeature& state : states.features) {
        if (!is_contiguous_state(state)) continue;
        if (!outline_only) fill_feature(frame, state, view, fill, water);
        stroke_feature(frame, state, view, border, thickness);
    }
}

void draw_region_context(cv::Mat& frame, const Geometry& geometry,
                         const View& view, const cv::Scalar& fill,
                         const cv::Scalar& border, int thickness,
                         const cv::Scalar& water, bool outline_only = false) {
    if (geometry.country_mode) {
        draw_country_context(frame, geometry.countries, view, fill, border,
                             thickness, water);
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

[[nodiscard]] std::vector<std::vector<cv::Point>> scaled_rings(
    std::vector<std::vector<cv::Point>> rings, double scale, cv::Point2d offset) {
    double min_x = 1e18, min_y = 1e18, max_x = -1e18, max_y = -1e18;
    for (const auto& ring : rings) {
        for (const cv::Point& point : ring) {
            min_x = std::min(min_x, static_cast<double>(point.x));
            min_y = std::min(min_y, static_cast<double>(point.y));
            max_x = std::max(max_x, static_cast<double>(point.x));
            max_y = std::max(max_y, static_cast<double>(point.y));
        }
    }
    const cv::Point2d center((min_x + max_x) * 0.5, (min_y + max_y) * 0.5);
    for (auto& ring : rings) {
        for (cv::Point& point : ring) {
            const cv::Point2d p = center + (cv::Point2d(point.x, point.y) - center) * scale + offset;
            point = {static_cast<int>(std::lround(p.x)),
                     static_cast<int>(std::lround(p.y))};
        }
    }
    return rings;
}

[[nodiscard]] std::vector<std::vector<cv::Point>> offset_rings(
    std::vector<std::vector<cv::Point>> rings, cv::Point2d offset) {
    for (auto& ring : rings) {
        for (cv::Point& point : ring) {
            point.x += static_cast<int>(std::lround(offset.x));
            point.y += static_cast<int>(std::lround(offset.y));
        }
    }
    return rings;
}

struct Plates {
    cv::Mat base;
    cv::Mat hi;
};

// The plates are composed at 2.12x (2x supersampling + 6% overscan) and then
// reduced once to 1.06x, which is exactly the largest frame the push-in can
// show. Per-frame sampling therefore only crops and performs a small resize
// instead of downscaling a 4K plate twice for every frame.
[[nodiscard]] Plates finalize_plates(const cv::Mat& base, const cv::Mat& hi,
                                     int width, int height) {
    const cv::Size preview_size(
        static_cast<int>(std::lround(width * 1.06)),
        static_cast<int>(std::lround(height * 1.06)));
    Plates plates;
    cv::resize(base, plates.base, preview_size, 0.0, 0.0, cv::INTER_AREA);
    if (!hi.empty()) {
        cv::resize(hi, plates.hi, preview_size, 0.0, 0.0, cv::INTER_AREA);
    }
    return plates;
}

[[nodiscard]] cv::Rect2d fraction_rect(int width, int height, double x, double y,
                                       double w, double h) {
    return {width * x, height * y, width * w, height * h};
}

[[nodiscard]] View style_view(const std::string& slug, const Geometry& geometry,
                              int width, int height) {
    const LonLatBox& context_box = geometry.country_mode
        ? geometry.country_view_box : geometry.lower48;
    if (slug == "01_editorial_vox") {
        return fit_box(context_box,
                       fraction_rect(width, height, 0.035, 0.055, 0.93, 0.815));
    }
    if (slug == "02_paper_cutout") {
        return fit_box(context_box,
                       fraction_rect(width, height, 0.05, 0.15, 0.90, 0.78));
    }
    if (slug == "03_dark_radar") {
        return fit_box(context_box,
                       fraction_rect(width, height, 0.03, 0.07, 0.94, 0.84));
    }
    if (slug == "04_blueprint") {
        return fit_box(context_box,
                       fraction_rect(width, height, 0.05, 0.08, 0.90, 0.70));
    }
    if (slug == "05_connection_arc") {
        if (geometry.country_mode) {
            return fit_box(geometry.country_view_box,
                           fraction_rect(width, height, 0.03, 0.02, 0.94, 0.96));
        }
        LonLatBox box;
        box.add(-90.4, 36.6);
        box.add(-73.8, 43.0);
        return fit_box(box, fraction_rect(width, height, 0.03, 0.02, 0.94, 0.96));
    }
    if (slug == "06_floating_island") {
        return fit_box(geometry.target_box,
                       fraction_rect(width, height, 0.22, 0.15, 0.38, 0.68));
    }
    throw std::runtime_error("unknown map style: " + slug);
}

[[nodiscard]] Plates build_plates(const std::string& slug, const Geometry& geometry,
                                  int width, int height) {
    const int plate_width = static_cast<int>(std::lround(width * kPlateScale));
    const double pfx = static_cast<double>(plate_width) / width;
    const int plate_height = static_cast<int>(std::lround(height * pfx));
    const View screen = style_view(slug, geometry, width, height);
    const View plate = scaled_view(screen, pfx);
    const int border = std::max(1, static_cast<int>(std::lround(pfx)));
    Plates plates;

    if (slug == "01_editorial_vox") {
        const cv::Scalar sea = rgb(0xCBD5E1);
        plates.base = textured_canvas(plate_width, plate_height, 0xCBD5E1, 0xE011,
                                      0.055, 0.022);
        if (!geometry.country_mode) {
            draw_country_context(plates.base, geometry.countries, plate,
                                 rgb(0xE9E4D8), rgb(0xC9C0B2), border, sea);
        }
        draw_region_context(plates.base, geometry, plate, rgb(0xF4F1EA),
                            rgb(0xB8B2A6), border, sea);
        plates.hi = plates.base.clone();
        fill_feature(plates.hi, *geometry.highlighted, plate, rgb(0xD94E34), sea);
        stroke_feature(plates.hi, *geometry.highlighted, plate, rgb(0xB33A25),
                       border + 1);
        return finalize_plates(plates.base, plates.hi, width, height);
    }

    if (slug == "02_paper_cutout") {
        plates.base = textured_canvas(plate_width, plate_height, 0xE3DED3, 0xE022,
                                      0.065, 0.028);
        draw_region_context(plates.base, geometry, plate, rgb(0xD8D0C1),
                            rgb(0xB8AD9B), border, rgb(0xE3DED3));
        plates.hi = plates.base.clone();
        const auto rings = scaled_rings(project_rings(*geometry.highlighted, plate),
                                        1.035, {-8.0 * pfx, -8.0 * pfx});
        cv::Mat shadow_mask(plates.hi.size(), CV_8UC1, cv::Scalar(0));
        fill_rings(shadow_mask, offset_rings(rings, {18.0 * pfx, 26.0 * pfx}),
                   cv::Scalar(255));
        cv::GaussianBlur(shadow_mask, shadow_mask, cv::Size(0, 0), 26.0 * pfx);
        blend_mask(plates.hi, shadow_mask, rgb(0x2B241B), 0.32);
        fill_rings(plates.hi, offset_rings(rings, {2.0 * pfx, 3.0 * pfx}),
                   rgb(0xCFC4B1));
        fill_rings(plates.hi, rings, rgb(0xF8F3E7));
        stroke_rings(plates.hi, rings, rgb(0xB9AE9B), border + 1);
        return finalize_plates(plates.base, plates.hi, width, height);
    }

    if (slug == "03_dark_radar") {
        plates.base = textured_canvas(plate_width, plate_height, 0x0B0F17, 0xE033,
                                      0.045, 0.012);
        draw_graticule(plates.base, plate, rgb(0x1B2636), std::max(1, border / 2),
                       5.0, true);
        draw_region_context(plates.base, geometry, plate, rgb(0x131A26),
                            rgb(0x2D3B55), border, rgb(0x0B0F17));
        plates.hi = plates.base.clone();
        fill_feature(plates.hi, *geometry.highlighted, plate, rgb(0x1E3A5F),
                     rgb(0x0B0F17));
        stroke_feature(plates.hi, *geometry.highlighted, plate, rgb(0x38BDF8),
                       border + 1);
        return finalize_plates(plates.base, plates.hi, width, height);
    }

    if (slug == "04_blueprint") {
        plates.base = textured_canvas(plate_width, plate_height, 0x0E2A47, 0xE044,
                                      0.035, 0.018);
        draw_graticule(plates.base, plate, rgb(0x17395C), std::max(1, border / 2),
                       3.0, false);
        const int inset = static_cast<int>(std::lround(46.0 * pfx));
        cv::rectangle(plates.base, {inset, inset, plate_width - inset * 2,
                                    plate_height - inset * 2},
                      rgb(0x3D6C93), border, cv::LINE_AA);
        draw_region_context(plates.base, geometry, plate, rgb(0x0E2A47),
                            rgb(0x8FB8DA), border, rgb(0x0E2A47), true);
        plates.hi = plates.base.clone();
        draw_hatch(plates.hi, *geometry.highlighted, plate, rgb(0xE3C68E),
                   std::max(6, static_cast<int>(std::lround(7.0 * pfx))), border);
        stroke_feature(plates.hi, *geometry.highlighted, plate, rgb(0x061726),
                       border * 4);
        stroke_feature(plates.hi, *geometry.highlighted, plate, rgb(0xDCEBF8),
                       std::max(2, border * 2));
        return finalize_plates(plates.base, plates.hi, width, height);
    }

    if (slug == "05_connection_arc") {
        const cv::Scalar water = rgb(0xC7D5DE);
        plates.base = textured_canvas(plate_width, plate_height, 0xC7D5DE, 0xE055,
                                      0.055, 0.022);
        if (!geometry.country_mode) {
            draw_country_context(plates.base, geometry.countries, plate,
                                 rgb(0xDED7C9), rgb(0xC3B9A8), border, water);
        }
        draw_region_context(plates.base, geometry, plate, rgb(0xE7E1D4),
                            rgb(0xB7AE9E), border, water);
        plates.hi = plates.base.clone();
        fill_feature(plates.hi, *geometry.highlighted, plate, rgb(0xD94E34),
                     water);
        stroke_feature(plates.hi, *geometry.highlighted, plate, rgb(0xA43A28),
                       border + 1);
        return finalize_plates(plates.base, plates.hi, width, height);
    }

    if (slug == "06_floating_island") {
        plates.base = textured_canvas(plate_width, plate_height, 0xEDE8DE, 0xE066,
                                      0.055, 0.026);
        plates.hi = plates.base.clone();
        const auto rings = project_rings(*geometry.highlighted, plate);
        cv::Mat shadow_mask(plates.hi.size(), CV_8UC1, cv::Scalar(0));
        fill_rings(shadow_mask, offset_rings(rings, {18.0 * pfx, 28.0 * pfx}),
                   cv::Scalar(255));
        cv::GaussianBlur(shadow_mask, shadow_mask, cv::Size(0, 0), 30.0 * pfx);
        blend_mask(plates.hi, shadow_mask, rgb(0x3B2A20), 0.28);
        fill_rings(plates.hi, rings, rgb(0xD94E34));
        stroke_rings(plates.hi, rings, rgb(0x7E2E1F), border + 1);
        return finalize_plates(plates.base, plates.hi, width, height);
    }

    throw std::runtime_error("unknown map style: " + slug);
}

[[nodiscard]] cv::Mat sample_plates(const Plates& plates, double zoom,
                                    double highlight_alpha, int width, int height) {
    const bool full_plate = zoom <= 1.0000001;
    const int crop_width = full_plate
        ? plates.base.cols
        : std::max(2, static_cast<int>(std::lround(plates.base.cols / zoom)));
    const int crop_height = full_plate
        ? plates.base.rows
        : std::max(2, static_cast<int>(std::lround(plates.base.rows / zoom)));
    const cv::Point2d center(plates.base.cols * 0.5, plates.base.rows * 0.5);
    const cv::Size crop_size(crop_width, crop_height);

    auto extract = [&](const cv::Mat& plate) {
        cv::Mat out;
        if (full_plate) {
            out = plate.clone();
        } else {
            cv::getRectSubPix(plate, crop_size, center, out);
        }
        cv::resize(out, out, cv::Size(width, height), 0.0, 0.0, cv::INTER_AREA);
        return out;
    };

    if (!plates.hi.empty() && highlight_alpha >= 0.999) {
        return extract(plates.hi);
    }
    cv::Mat frame = extract(plates.base);
    if (!plates.hi.empty() && highlight_alpha > 0.001) {
        cv::Mat hi = extract(plates.hi);
        cv::addWeighted(frame, 1.0 - highlight_alpha, hi, highlight_alpha, 0.0,
                        frame);
    }
    return frame;
}

// ── Overlay primitives ──────────────────────────────────────────────────────

void draw_pin(cv::Mat& frame, cv::Point2d point, double alpha,
              const cv::Scalar& fill, const cv::Scalar& ring, double scale,
              double ring_radius) {
    const int radius = std::max(3, static_cast<int>(std::lround(ring_radius * scale)));
    const cv::Point center(static_cast<int>(std::lround(point.x)),
                           static_cast<int>(std::lround(point.y)));
    draw_disc_alpha(frame, {center.x + 2, center.y + 3}, radius + 2,
                    rgb(0x1F2937), alpha * 0.16);
    draw_disc_alpha(frame, center, radius, ring, alpha);
    draw_disc_alpha(frame, center,
                    std::max(2, static_cast<int>(std::lround(radius * 0.44))), fill,
                    alpha);
}

void draw_crosshair(cv::Mat& frame, cv::Point center, const cv::Scalar& color,
                    double alpha, int radius) {
    draw_ring_alpha(frame, center, radius, color, 1, alpha * 0.9);
    draw_line_alpha(frame, {center.x - radius - 8, center.y},
                    {center.x - radius + 2, center.y}, color, 1, alpha);
    draw_line_alpha(frame, {center.x + radius - 2, center.y},
                    {center.x + radius + 8, center.y}, color, 1, alpha);
    draw_line_alpha(frame, {center.x, center.y - radius - 8},
                    {center.x, center.y - radius + 2}, color, 1, alpha);
    draw_line_alpha(frame, {center.x, center.y + radius - 2},
                    {center.x, center.y + radius + 8}, color, 1, alpha);
}

void draw_radar(cv::Mat& frame, cv::Point2d point, double time, double alpha) {
    const cv::Point center(static_cast<int>(std::lround(point.x)),
                           static_cast<int>(std::lround(point.y)));
    for (int radius = 52; radius >= 14; radius -= 8) {
        draw_disc_alpha(frame, center, radius, rgb(0x38BDF8),
                        alpha * (0.030 + (52 - radius) * 0.002));
    }
    draw_ring_alpha(frame, center, 19, rgb(0x8FD8FA), 1, alpha * 0.85);
    draw_disc_alpha(frame, center, 6, rgb(0x38BDF8), alpha);
    draw_disc_alpha(frame, center, 2, rgb(0xFFFFFF), alpha);
    for (int pulse = 0; pulse < 2; ++pulse) {
        const double phase = std::fmod(time * 3.0 + pulse * 0.5, 1.0);
        const int radius = static_cast<int>(std::lround(24.0 + 150.0 * phase));
        draw_ring_alpha(frame, center, radius, rgb(0x38BDF8), 2,
                        alpha * (1.0 - phase) * 0.85);
    }
}

void draw_path_progress(cv::Mat& frame, const std::vector<cv::Point2d>& points,
                        const cv::Scalar& color, int thickness, double progress) {
    if (points.size() < 2 || progress <= 0.0) return;
    std::vector<double> lengths(points.size() - 1, 0.0);
    double total = 0.0;
    for (std::size_t i = 1; i < points.size(); ++i) {
        lengths[i - 1] = cv::norm(points[i] - points[i - 1]);
        total += lengths[i - 1];
    }
    if (total <= 1e-9) return;
    const double target = total * clamp01(progress);
    std::vector<cv::Point> visible;
    visible.push_back({static_cast<int>(std::lround(points[0].x)),
                       static_cast<int>(std::lround(points[0].y))});
    double accumulated = 0.0;
    for (std::size_t i = 1; i < points.size(); ++i) {
        if (accumulated + lengths[i - 1] <= target) {
            visible.push_back({static_cast<int>(std::lround(points[i].x)),
                               static_cast<int>(std::lround(points[i].y))});
            accumulated += lengths[i - 1];
            continue;
        }
        const double fraction = (target - accumulated) / lengths[i - 1];
        const cv::Point2d partial = points[i - 1] + (points[i] - points[i - 1]) * fraction;
        visible.push_back({static_cast<int>(std::lround(partial.x)),
                           static_cast<int>(std::lround(partial.y))});
        break;
    }
    if (visible.size() >= 2) {
        cv::polylines(frame, visible, false, color, thickness, cv::LINE_AA);
    }
}

[[nodiscard]] std::vector<cv::Point2d> bezier_points(cv::Point2d from,
                                                     cv::Point2d to, int steps) {
    const double distance = cv::norm(to - from);
    const double lift = distance * 0.20;
    const cv::Point2d c1(from.x + (to.x - from.x) * 0.33,
                         from.y + (to.y - from.y) * 0.33 - lift);
    const cv::Point2d c2(from.x + (to.x - from.x) * 0.66,
                         from.y + (to.y - from.y) * 0.66 - lift);
    std::vector<cv::Point2d> out;
    out.reserve(steps + 1);
    for (int i = 0; i <= steps; ++i) {
        const double t = static_cast<double>(i) / steps;
        const double u = 1.0 - t;
        out.push_back(from * (u * u * u) + c1 * (3.0 * u * u * t) +
                      c2 * (3.0 * u * t * t) + to * (t * t * t));
    }
    return out;
}

void draw_arc_glow(cv::Mat& frame, const std::vector<cv::Point2d>& points,
                   const cv::Scalar& color, double alpha) {
    if (points.size() < 2) return;
    int min_x = frame.cols, min_y = frame.rows, max_x = 0, max_y = 0;
    std::vector<cv::Point> pixel_points;
    pixel_points.reserve(points.size());
    for (const auto& point : points) {
        const cv::Point pixel(static_cast<int>(std::lround(point.x)),
                              static_cast<int>(std::lround(point.y)));
        pixel_points.push_back(pixel);
        min_x = std::min(min_x, pixel.x);
        min_y = std::min(min_y, pixel.y);
        max_x = std::max(max_x, pixel.x);
        max_y = std::max(max_y, pixel.y);
    }
    constexpr int kPad = 32;
    const cv::Rect roi = clamp_roi(
        frame, {min_x - kPad, min_y - kPad, max_x - min_x + kPad * 2 + 1,
                max_y - min_y + kPad * 2 + 1});
    if (roi.empty()) return;
    for (cv::Point& point : pixel_points) point -= roi.tl();
    cv::Mat glow(roi.size(), CV_8UC3, cv::Scalar(0));
    cv::polylines(glow, pixel_points, false, color, 10, cv::LINE_AA);
    cv::GaussianBlur(glow, glow, cv::Size(0, 0), 7.0);
    cv::Mat target = frame(roi);
    cv::addWeighted(target, 1.0, glow, alpha, 0.0, target);
}

void draw_ground_shadow(cv::Mat& frame, cv::Point2d from, cv::Point2d to,
                        double alpha) {
    const cv::Point a(static_cast<int>(std::lround(from.x)),
                      static_cast<int>(std::lround(from.y + 8)));
    const cv::Point b(static_cast<int>(std::lround(to.x)),
                      static_cast<int>(std::lround(to.y + 8)));
    constexpr int kPad = 40;
    const cv::Rect roi = clamp_roi(
        frame, {std::min(a.x, b.x) - kPad, std::min(a.y, b.y) - kPad,
                std::abs(b.x - a.x) + kPad * 2 + 1,
                std::abs(b.y - a.y) + kPad * 2 + 1});
    if (roi.empty()) return;
    cv::Mat shadow(roi.size(), CV_8UC3, cv::Scalar(0));
    cv::line(shadow, a - roi.tl(), b - roi.tl(), rgb(0x1F2937), 14,
             cv::LINE_AA);
    cv::GaussianBlur(shadow, shadow, cv::Size(0, 0), 9.0);
    cv::Mat target = frame(roi);
    cv::addWeighted(target, 1.0, shadow, alpha, 0.0, target);
}

// ── Finishing: film grain and vignette ──────────────────────────────────────

struct Finish {
    cv::Mat grain;
    cv::Mat vignette;
    double grain_strength = 0.0;

    void apply(cv::Mat& frame) const {
        if (grain_strength > 0.0 && !grain.empty()) {
            cv::addWeighted(frame, 1.0, grain, grain_strength,
                            -128.0 * grain_strength, frame);
        }
        if (!vignette.empty()) {
            cv::multiply(frame, vignette, frame, 1.0 / 255.0);
        }
    }
};

[[nodiscard]] Finish make_finish(int width, int height, unsigned int seed,
                                 double grain_strength, double vignette_strength) {
    Finish finish;
    finish.grain_strength = grain_strength;
    if (grain_strength > 0.0) {
        finish.grain.create(height, width, CV_8UC3);
        cv::RNG rng(seed);
        rng.fill(finish.grain, cv::RNG::NORMAL, 128.0, 40.0);
    }
    if (vignette_strength > 0.0) {
        finish.vignette.create(height, width, CV_8UC3);
        const double half_x = std::max(1.0, (width - 1) * 0.5);
        const double half_y = std::max(1.0, (height - 1) * 0.5);
        for (int y = 0; y < height; ++y) {
            cv::Vec3b* row = finish.vignette.ptr<cv::Vec3b>(y);
            const double dy = (y - (height - 1) * 0.5) / half_y;
            for (int x = 0; x < width; ++x) {
                const double dx = (x - (width - 1) * 0.5) / half_x;
                const double radius = clamp01(std::sqrt(dx * dx + dy * dy));
                const double gain =
                    1.0 - vignette_strength * std::pow(radius, 2.2);
                const unsigned char value =
                    cv::saturate_cast<unsigned char>(gain * 255.0);
                row[x] = cv::Vec3b(value, value, value);
            }
        }
    }
    return finish;
}

// ── Per-style animated overlays ─────────────────────────────────────────────

void overlay_editorial(cv::Mat& frame, const Geometry& geometry, Fonts& fonts,
                       const View& screen, double t,
                       const chrononplugin::maps::EditorialMapRequest& config) {
    const cv::Point2d indianapolis =
        screen.project(config.pin_latitude, config.pin_longitude);
    const double title_alpha = smooth01(0.02, 0.09, t);
    const double pin_alpha = smooth01(0.10, 0.15, t);
    const double city_alpha = smooth01(0.15, 0.21, t);

    const std::string micro = upper_copy(geometry.country_mode
        ? config.title + " / editorial plate"
        : "United States / editorial plate");
    const double micro_width = fonts.mono.measure(micro, 17, 3);
    draw_text(fonts.mono, frame, micro,
              static_cast<int>(std::lround(1820.0 - micro_width)), 1018, 17,
              rgb(0x33414F), title_alpha * 0.72, 3);
    draw_line_alpha(frame,
                    {static_cast<int>(std::lround(1820.0 - micro_width)), 1002},
                    {1820, 1002}, rgb(0xD94E34), 3, title_alpha * 0.9);
    draw_text(fonts.serif, frame, config.title, 98, 946, 104, rgb(0x1F2937),
              title_alpha);
    draw_line_alpha(frame, {102, 962}, {196, 962}, rgb(0xD94E34), 4,
                    title_alpha);
    draw_text(fonts.sans, frame, upper_copy(config.subtitle), 102, 1002, 21,
              rgb(0x556270), title_alpha * 0.95, 5);

    draw_pin(frame, indianapolis, pin_alpha, rgb(0x111827), rgb(0xF4F1EA), 1.0,
             13.0);
    if (city_alpha > 0.0) {
        draw_text_outlined(fonts.sans_bold, frame, config.pin_label,
                           static_cast<int>(std::lround(indianapolis.x)) + 28,
                           static_cast<int>(std::lround(indianapolis.y)) - 12,
                           32, rgb(0x1F2937), city_alpha, 0, rgb(0xF4F1EA), 2);
    }
    (void)geometry;
}

void overlay_paper_cutout(cv::Mat& frame, const Geometry& geometry, Fonts& fonts,
                          const View& screen, double t,
                          const chrononplugin::maps::EditorialMapRequest& config) {
    const double pin_latitude = geometry.country_mode
        ? config.pin_latitude : 39.7684;
    const double pin_longitude = geometry.country_mode
        ? config.pin_longitude : -86.1581;
    const std::string title = geometry.country_mode ? config.title : "INDIANA";
    const std::string pin_label = geometry.country_mode
        ? config.pin_label : "Indianapolis";
    const cv::Point2d indianapolis = screen.project(pin_latitude, pin_longitude);
    const double title_alpha = smooth01(0.02, 0.09, t);
    const double pin_alpha = smooth01(0.12, 0.18, t);
    const double city_alpha = smooth01(0.17, 0.23, t);

    draw_text(fonts.serif, frame, title, 100, 148, 92, rgb(0x2A241C),
              title_alpha);
    draw_line_alpha(frame, {104, 168}, {190, 168}, rgb(0xC05A3E), 3,
                    title_alpha);
    draw_text(fonts.mono, frame, geometry.country_mode ? "ITALY / EUROPE" : "39.77 N / 86.16 W", 104, 204, 18,
              rgb(0x6B6255), title_alpha * 0.9, 2);
    draw_text(fonts.sans, frame, upper_copy(geometry.country_mode
                  ? config.subtitle : "The Hoosier State"), 104, 232, 18,
              rgb(0x7A7164), title_alpha * 0.85, 4);

    draw_pin(frame, indianapolis, pin_alpha, rgb(0x2A241C), rgb(0xF8F3E7), 1.0,
             13.0);
    if (city_alpha > 0.0) {
        draw_text_outlined(fonts.sans_bold, frame, pin_label,
                           static_cast<int>(std::lround(indianapolis.x)) + 26,
                           static_cast<int>(std::lround(indianapolis.y)) - 12,
                           31, rgb(0x2A241C), city_alpha, 0, rgb(0xF8F3E7), 2);
    }
    (void)geometry;
}

void overlay_dark_radar(cv::Mat& frame, const Geometry& geometry, Fonts& fonts,
                        const View& screen, double t,
                        const chrononplugin::maps::EditorialMapRequest& config) {
    const double pin_latitude = geometry.country_mode
        ? config.pin_latitude : 38.9072;
    const double pin_longitude = geometry.country_mode
        ? config.pin_longitude : -77.0369;
    const std::string pin_label = geometry.country_mode
        ? config.pin_label : "WASHINGTON, D.C.";
    const cv::Point2d washington = screen.project(pin_latitude, pin_longitude);
    const double hud_alpha = smooth01(0.02, 0.08, t);
    const double pin_alpha = smooth01(0.05, 0.10, t);
    const double label_alpha = smooth01(0.09, 0.15, t);

    draw_line_alpha(frame, {92, 92}, {172, 92}, rgb(0x38BDF8), 3,
                    hud_alpha * 0.9);
    draw_text(fonts.mono, frame, geometry.country_mode
                  ? "ITALIA // SIGNAL GRID" : "UNITED STATES // SIGNAL GRID", 92, 130, 20,
              rgb(0x8FA3BF), hud_alpha, 3);
    draw_text(fonts.mono, frame, geometry.country_mode
                  ? "42.00 N / 12.50 E" : "38.90 N / 77.03 W", 92, 158, 16,
              rgb(0x5E738D), hud_alpha * 0.9, 2);

    draw_radar(frame, washington, t, pin_alpha);
    draw_text(fonts.mono, frame, pin_label, 92, 976, 24,
              rgb(0xF1F5F9), label_alpha, 4);
    draw_text(fonts.mono, frame, geometry.country_mode
                  ? "COUNTRY OUTLINE / PRIORITY 01" : "FEDERAL DISTRICT / PRIORITY 01", 92, 1006, 16,
              rgb(0x6F849E), label_alpha * 0.9, 2);
    draw_text(fonts.mono, frame, geometry.country_mode
                  ? "GRID REF ITALIA / CENTRALE" : "GRID REF 18S UJ 234 083", 92, 1032, 14,
              rgb(0x475B72), label_alpha * 0.8, 2);
    (void)geometry;
}

void overlay_blueprint(cv::Mat& frame, const Geometry& geometry, Fonts& fonts,
                       const View& screen, double t, int width, int height,
                       const chrononplugin::maps::EditorialMapRequest& config) {
    const cv::Point2d indianapolis = screen.project(
        geometry.country_mode ? config.pin_latitude : 39.7684,
        geometry.country_mode ? config.pin_longitude : -86.1581);
    const double panel_alpha = smooth01(0.03, 0.09, t);
    const double label_alpha = smooth01(0.12, 0.18, t);
    const cv::Rect panel(static_cast<int>(std::lround(width * 0.04)),
                         static_cast<int>(std::lround(height * 0.745)),
                         static_cast<int>(std::lround(width * 0.33)),
                         static_cast<int>(std::lround(height * 0.205)));
    draw_rect_alpha(frame, panel, rgb(0x0A2136), rgb(0x3D6C93), 1,
                    panel_alpha * 0.94);

    const int text_x = panel.x + static_cast<int>(std::lround(width * 0.016));
    draw_text(fonts.sans_bold, frame,
              geometry.country_mode ? config.title : "INDIANA",
              text_x, panel.y + 62, 56,
              rgb(0xE8F3FC), panel_alpha, 9);
    draw_line_alpha(frame, {text_x, panel.y + 80},
                    {panel.x + panel.width - 26, panel.y + 80},
                    rgb(0x3D6C93), 1, panel_alpha);
    draw_text(fonts.mono, frame, geometry.country_mode
                  ? "ITALY / EUROPE" : "39.7684 N / 86.1581 W", text_x,
              panel.y + 112, 18, rgb(0xA8C8E8), panel_alpha * 0.95, 1);
    draw_text(fonts.mono, frame, "ADMINISTRATIVE PLATE / 1:2,000,000", text_x,
              panel.y + 140, 15, rgb(0x7FA5C6), panel_alpha * 0.9, 1);
    draw_line_alpha(frame, {text_x, panel.y + 176},
                    {text_x + 190, panel.y + 176}, rgb(0xA8C8E8), 2,
                    panel_alpha);
    for (int tick = 0; tick <= 4; ++tick) {
        const int x = text_x + tick * 47;
        draw_line_alpha(frame, {x, panel.y + 170}, {x, panel.y + 184},
                        rgb(0xA8C8E8), 1, panel_alpha);
    }
    draw_text(fonts.mono, frame, "0", text_x, panel.y + 200, 13,
              rgb(0x7FA5C6), panel_alpha * 0.9);
    draw_text(fonts.mono, frame, "100 KM", text_x + 146, panel.y + 200, 13,
              rgb(0x7FA5C6), panel_alpha * 0.9);

    draw_crosshair(frame,
                   {static_cast<int>(std::lround(indianapolis.x)),
                    static_cast<int>(std::lround(indianapolis.y))},
                   rgb(0xE3C68E), label_alpha, 12);
    draw_text(fonts.mono, frame, geometry.country_mode
                  ? config.pin_label : "INDIANAPOLIS",
              static_cast<int>(std::lround(indianapolis.x)) + 30,
              static_cast<int>(std::lround(indianapolis.y)) - 14, 17,
              rgb(0xE3C68E), label_alpha, 2);
    (void)geometry;
}

void overlay_connection_arc(cv::Mat& frame, const Geometry& geometry,
                            Fonts& fonts, const View& screen, double t,
                            const chrononplugin::maps::EditorialMapRequest& config) {
    const cv::Point2d washington = geometry.country_mode
        ? screen.project(45.4642, 9.19) : screen.project(38.9072, -77.0369);
    const cv::Point2d indianapolis = screen.project(
        geometry.country_mode ? config.pin_latitude : 39.7684,
        geometry.country_mode ? config.pin_longitude : -86.1581);
    const double title_alpha = smooth01(0.02, 0.08, t);
    const double source_alpha = smooth01(0.06, 0.11, t);
    const double arc_progress = smooth01(0.10, 0.42, t);
    const double destination_alpha = smooth01(0.36, 0.43, t);
    const double distance_alpha = smooth01(0.34, 0.42, t);

    draw_text(fonts.serif, frame, geometry.country_mode
                  ? "MILANO - ROMA" : "WASHINGTON - INDIANAPOLIS", 98, 112, 46,
              rgb(0x1F2937), title_alpha);
    draw_text(fonts.mono, frame, geometry.country_mode
                  ? "CONNECTION ARC / APPROX. 480 KM"
                  : "CONNECTION ARC / APPROX. 800 KM", 100, 146,
              17, rgb(0x5B6470), title_alpha * 0.9, 2);

    draw_ground_shadow(frame, washington, indianapolis,
                       arc_progress * 0.30);
    std::vector<cv::Point2d> curve = bezier_points(washington, indianapolis, 96);
    std::vector<cv::Point2d> visible;
    const std::size_t count = static_cast<std::size_t>(
        std::lround(static_cast<double>(curve.size() - 1) * arc_progress));
    visible.assign(curve.begin(), curve.begin() + static_cast<long>(count) + 1);
    if (visible.size() >= 2) {
        draw_arc_glow(frame, visible, rgb(0xD94E34), 0.34);
        draw_path_progress(frame, curve, rgb(0xF8F3E7), 7, arc_progress);
        draw_path_progress(frame, curve, rgb(0xD94E34), 4, arc_progress);
    }

    draw_pin(frame, washington, source_alpha, rgb(0x1F2937), rgb(0xF8F3E7), 1.0,
             12.0);
    draw_pin(frame, indianapolis, destination_alpha, rgb(0xD94E34),
             rgb(0xF8F3E7), 1.05, 14.0);

    const std::string dc_label = geometry.country_mode ? "MILANO" : "WASHINGTON, D.C.";
    const double dc_width = fonts.sans_bold.measure(dc_label, 27);
    draw_text_outlined(
        fonts.sans_bold, frame, dc_label,
        static_cast<int>(std::lround(washington.x - dc_width - 24)),
        static_cast<int>(std::lround(washington.y)) + 42, 27, rgb(0x1F2937),
        destination_alpha, 0, rgb(0xF8F3E7), 2);
    const std::string indy_label = geometry.country_mode
        ? config.pin_label : "INDIANAPOLIS";
    const double indy_width = fonts.sans_bold.measure(indy_label, 27);
    draw_text_outlined(
        fonts.sans_bold, frame, indy_label,
        static_cast<int>(std::lround(indianapolis.x - indy_width - 24)),
        static_cast<int>(std::lround(indianapolis.y)) - 22, 27,
        rgb(0x1F2937), destination_alpha, 0, rgb(0xF8F3E7), 2);

    if (distance_alpha > 0.0) {
        const cv::Point2d midpoint = curve[curve.size() / 2];
        draw_text_centered(fonts.mono, frame,
                           geometry.country_mode ? "480 KM" : "800 KM",
                           static_cast<int>(std::lround(midpoint.x)),
                           static_cast<int>(std::lround(midpoint.y)) - 20, 19,
                           rgb(0x5B6470), distance_alpha, 2);
    }
    (void)geometry;
}

void overlay_floating_island(cv::Mat& frame, const Geometry& geometry,
                             Fonts& fonts, const View& screen, double t,
                             const chrononplugin::maps::EditorialMapRequest& config) {
    const cv::Point2d anchor = geometry.country_mode
        ? screen.project(config.pin_latitude, config.pin_longitude)
        : screen.project(40.20, -84.82);
    const double line_progress = smooth01(0.10, 0.22, t);
    const double text_alpha = smooth01(0.20, 0.27, t);
    const int rise = static_cast<int>(std::lround((1.0 - text_alpha) * 10.0));

    // A true 45-degree leader climbs from the state's eastern border to the
    // cartouche level, then runs horizontally into it. The previous elbow was
    // above the frame whenever the silhouette was framed tall.
    const double target_y = 150.0;
    const cv::Point2d elbow(anchor.x + std::max(40.0, anchor.y - target_y),
                            target_y);
    const cv::Point2d end(1285.0, target_y);
    const std::vector<cv::Point2d> path{anchor, elbow, end};
    draw_path_progress(frame, path, rgb(0x1F2937), 2, line_progress);
    draw_disc_alpha(frame,
                    {static_cast<int>(std::lround(anchor.x)),
                     static_cast<int>(std::lround(anchor.y))},
                    4, rgb(0xD94E34), line_progress);

    const int cartouche_x = 1315;
    const int cartouche_top = 160;
    draw_line_alpha(frame, {cartouche_x - 22, cartouche_top + 54},
                    {cartouche_x - 22, cartouche_top + 10},
                    rgb(0xD94E34), 4, text_alpha);
    draw_text(fonts.serif, frame, geometry.country_mode
                  ? config.title : "INDIANA", cartouche_x,
              cartouche_top + 70 + rise, 74, rgb(0x1F2937), text_alpha);
    draw_line_alpha(frame, {cartouche_x, cartouche_top + 88 + rise},
                    {cartouche_x + 340, cartouche_top + 88 + rise},
                    rgb(0xB8B2A6), 1, text_alpha);
    draw_text(fonts.mono, frame, geometry.country_mode
                  ? "ITALIA / EUROPA" : "39.77 N / 86.15 W", cartouche_x,
              cartouche_top + 120 + rise, 19, rgb(0x5B6470), text_alpha, 2);
    draw_text(fonts.sans, frame, upper_copy(geometry.country_mode
                  ? config.subtitle : "The Hoosier State"), cartouche_x,
              cartouche_top + 148 + rise, 17, rgb(0x6B7280),
              text_alpha * 0.9, 4);
    (void)geometry;
}

void draw_overlay(const std::string& slug, cv::Mat& frame,
                  const Geometry& geometry, Fonts& fonts, const View& screen,
                  double t, int width, int height,
                  const chrononplugin::maps::EditorialMapRequest& config) {
    if (slug == "01_editorial_vox") {
        overlay_editorial(frame, geometry, fonts, screen, t, config);
    } else if (slug == "02_paper_cutout") {
        overlay_paper_cutout(frame, geometry, fonts, screen, t, config);
    } else if (slug == "03_dark_radar") {
        overlay_dark_radar(frame, geometry, fonts, screen, t, config);
    } else if (slug == "04_blueprint") {
        overlay_blueprint(frame, geometry, fonts, screen, t, width, height,
                          config);
    } else if (slug == "05_connection_arc") {
        overlay_connection_arc(frame, geometry, fonts, screen, t, config);
    } else if (slug == "06_floating_island") {
        overlay_floating_island(frame, geometry, fonts, screen, t, config);
    }
}

// ── CLI and video encoding ──────────────────────────────────────────────────

struct Style {
    std::string slug;
    double zoom = 0.05;
    double grain = 0.03;
    double vignette = 0.18;
};

[[nodiscard]] std::vector<Style> styles() {
    return {
        {"01_editorial_vox", 0.060, 0.045, 0.20},
        {"02_paper_cutout", 0.050, 0.035, 0.16},
        {"03_dark_radar", 0.055, 0.022, 0.34},
        {"04_blueprint", 0.050, 0.022, 0.18},
        {"05_connection_arc", 0.070, 0.035, 0.20},
        {"06_floating_island", 0.040, 0.040, 0.14},
    };
}

[[nodiscard]] std::string shell_quote(const std::string& value) {
    std::string out = "'";
    for (const char c : value) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

}  // namespace

namespace chrononplugin::maps {

namespace {

[[nodiscard]] bool style_for_enum(EditorialMapStyle enum_style, Style& out) {
    const std::string id = editorial_map_style_id(enum_style);
    for (const Style& candidate : styles()) {
        if (candidate.slug == id) {
            out = candidate;
            return true;
        }
    }
    return false;
}

}  // namespace

EditorialMapResult render_editorial_map(const EditorialMapRequest& request) {
    EditorialMapResult result;
    auto fail = [&result](std::string message) {
        result.ok = false;
        result.error = std::move(message);
        return result;
    };

    if (request.output_video.empty()) return fail("output_video is required");
    if (request.output_width <= 0 || request.output_height <= 0) {
        return fail("output dimensions must be positive");
    }
    if (request.fps <= 0) return fail("fps must be positive");
    if (request.frame_count < 2) return fail("frame_count must be at least 2");

    Style style;
    if (!style_for_enum(request.style, style)) {
        return fail("unknown editorial map style");
    }

    const fs::path states_path = resolve_catalog_file(
        request.states_geojson, "CHRONON_MAPS_CATALOG_DIR",
        "us_states_editorial.geojson", CHRONON_MAPS_EDITORIAL_STATES_DEFAULT);
    const fs::path countries_path = resolve_catalog_file(
        request.countries_geojson, "CHRONON_MAPS_CATALOG_DIR",
        "ne_50m_admin_0_countries.geojson", CHRONON_MAPS_COUNTRIES_DEFAULT);
    std::string fonts_dir = request.fonts_dir;
    if (fonts_dir.empty()) {
        const char* env_fonts = std::getenv("CHRONON_MAPS_FONTS_DIR");
        fonts_dir = env_fonts != nullptr && *env_fonts != '\0'
            ? env_fonts : CHRONON_MAPS_FONTS_DIR_DEFAULT;
        if (!fs::is_directory(fonts_dir) &&
            (env_fonts == nullptr || *env_fonts == '\0')) {
            fonts_dir = CHRONON_MAPS_FONTS_DIR_SOURCE;
        }
    }

    try {
        const auto started = std::chrono::steady_clock::now();
        const Geometry geometry =
            load_geometry(states_path, countries_path, request.highlight_state,
                          request.highlight_country);
        const fs::path fonts_path(fonts_dir);
        {
            // Fail before opening the encoder if the font set is incomplete.
            Fonts probe(fonts_path);
            (void)probe;
        }

        unsigned int hardware = std::thread::hardware_concurrency();
        if (hardware == 0) hardware = 4;
        // Past twelve workers the per-frame ROI work stops scaling: the
        // threaded resize/blur path saturates memory bandwidth and x264 also
        // wants cores. An explicit request can still override this.
        const int workers = request.threads > 0
            ? request.threads
            : static_cast<int>(std::min(hardware, 12U));
        cv::setNumThreads(workers);

        const Plates plates =
            build_plates(style.slug, geometry, request.output_width,
                         request.output_height);
        const View screen = style_view(style.slug, geometry,
                                       request.output_width,
                                       request.output_height);
        const Finish finish = make_finish(
            request.output_width, request.output_height,
            0x5EED0000U + static_cast<unsigned int>(
                std::hash<std::string>{}(style.slug) & 0xFFFFU),
            style.grain, style.vignette);

        const fs::path video_path(request.output_video);
        if (!video_path.parent_path().empty()) {
            fs::create_directories(video_path.parent_path());
        }
        const fs::path poster_path(request.poster_png);
        if (!poster_path.empty() && !poster_path.parent_path().empty()) {
            fs::create_directories(poster_path.parent_path());
        }

        const std::string ffmpeg = request.ffmpeg.empty() ? "ffmpeg" : request.ffmpeg;
        const std::string rate =
            request.fps_num > 0 && request.fps_den > 0
                ? std::to_string(request.fps_num) + "/" +
                      std::to_string(request.fps_den)
                : std::to_string(request.fps);
        const std::string encoder_args = request.gpu_encoder
            ? " -c:v h264_nvenc -gpu " + std::to_string(request.gpu_index) +
                  " -preset p6 -tune hq -rc vbr -cq " +
                  std::to_string(request.gpu_quality) + " -b:v 0"
            : " -c:v libx264 -preset veryfast -crf " +
                  std::to_string(request.crf);
        const std::string command =
            ffmpeg + " -hide_banner -loglevel error -y -f rawvideo -pix_fmt bgr24 -s " +
            std::to_string(request.output_width) + "x" +
            std::to_string(request.output_height) + " -r " + rate +
            " -i -" + encoder_args + " -pix_fmt yuv420p -movflags +faststart " +
            shell_quote(video_path.string());

        std::FILE* encoder = ::popen(command.c_str(), "w");
        if (encoder == nullptr) {
            return fail("cannot launch ffmpeg");
        }

        const int poster_frame = std::min(
            request.frame_count - 1,
            static_cast<int>(std::lround(request.frame_count * 0.80)));
        std::atomic<bool> failed{false};
        std::mutex error_mutex;
        std::string error_text;

        const auto render_one = [&](int frame_index, Fonts& fonts) {
            const double t = static_cast<double>(frame_index) /
                             static_cast<double>(request.frame_count - 1);
            const double zoom = 1.0 + style.zoom * t;
            const double highlight_alpha = plates.hi.empty()
                ? 0.0 : smooth01(0.015, 0.090, t);
            cv::Mat frame = sample_plates(plates, zoom, highlight_alpha,
                                          request.output_width,
                                          request.output_height);
            View zoomed = screen;
            zoomed.scale *= zoom;
            draw_overlay(style.slug, frame, geometry, fonts, zoomed, t,
                         request.output_width, request.output_height, request);
            finish.apply(frame);
            return frame;
        };

        const int batch_size = std::clamp(workers * 2, 4, 16);

        bool write_failed = false;
        for (int start = 0; start < request.frame_count && !write_failed;
             start += batch_size) {
            const int count = std::min(batch_size, request.frame_count - start);
            std::vector<cv::Mat> frames(static_cast<std::size_t>(count));

            cv::parallel_for_(cv::Range(0, count), [&](const cv::Range& range) {
                thread_local std::string tl_fonts_dir;
                thread_local std::unique_ptr<Fonts> tl_fonts;
                try {
                    if (!tl_fonts || tl_fonts_dir != fonts_dir) {
                        tl_fonts = std::make_unique<Fonts>(fonts_path);
                        tl_fonts_dir = fonts_dir;
                    }
                    for (int i = range.start; i < range.end; ++i) {
                        if (failed.load(std::memory_order_relaxed)) return;
                        frames[static_cast<std::size_t>(i)] =
                            render_one(start + i, *tl_fonts);
                    }
                } catch (const std::exception& exc) {
                    if (!failed.exchange(true)) {
                        std::lock_guard<std::mutex> lock(error_mutex);
                        error_text = exc.what();
                    }
                } catch (...) {
                    if (!failed.exchange(true)) {
                        std::lock_guard<std::mutex> lock(error_mutex);
                        error_text = "unknown frame rendering failure";
                    }
                }
            });

            if (failed.load()) break;
            for (int i = 0; i < count; ++i) {
                cv::Mat& frame = frames[static_cast<std::size_t>(i)];
                if (frame.empty() || frame.cols != request.output_width ||
                    frame.rows != request.output_height) {
                    write_failed = true;
                    error_text = "frame renderer returned an invalid frame";
                    break;
                }
                if (!poster_path.empty() && start + i == poster_frame &&
                    !cv::imwrite(poster_path.string(), frame)) {
                    write_failed = true;
                    error_text = "cannot write the poster frame";
                    break;
                }
                const std::size_t bytes = static_cast<std::size_t>(
                    request.output_width) * request.output_height * 3U;
                if (std::fwrite(frame.data, 1, bytes, encoder) != bytes) {
                    write_failed = true;
                    error_text = "short write to the video encoder";
                    break;
                }
            }
        }

        const int encoder_status = ::pclose(encoder);
        const double elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started).count();
        if (write_failed || failed.load() || encoder_status != 0) {
            std::remove(video_path.string().c_str());
            if (error_text.empty()) {
                error_text = "ffmpeg failed with status " +
                             std::to_string(encoder_status);
            }
            return fail(error_text);
        }

        result.ok = true;
        result.frame_count = request.frame_count;
        result.render_seconds = elapsed;
        return result;
    } catch (const std::exception& exc) {
        return fail(exc.what());
    } catch (...) {
        return fail("unknown editorial map rendering failure");
    }
}

}  // namespace chrononplugin::maps
