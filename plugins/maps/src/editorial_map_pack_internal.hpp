#pragma once

// Private to plugins/maps: the shared model and raster primitives of the
// editorial map pack. editorial_map_pack.cpp is split into geometry/plates/
// text/overlay/entry translation units; only these entities cross the split.
// This header is not part of the public plugin contract.

#include "chrononplugin/maps/editorial_map_pack.hpp"
#include "chrononplugin/maps/geojson.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <opencv2/core.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace chrononplugin::maps::editorial_pack_detail {

namespace fs = std::filesystem;

inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kPlateScale = 2.12;  // 2x supersampling with 6% overscan.

[[nodiscard]] cv::Scalar rgb(unsigned int hex);
[[nodiscard]] double clamp01(double value);
[[nodiscard]] double smooth01(double start, double end, double t);
[[nodiscard]] double ease_out_cubic(double p);
[[nodiscard]] std::string upper_copy(std::string text);
[[nodiscard]] double mercator_x(double lon);
[[nodiscard]] double mercator_y(double lat);

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

[[nodiscard]] View fit_box(const LonLatBox& box, const cv::Rect2d& rect);
[[nodiscard]] View scaled_view(const View& view, double factor);

struct Geometry {
    GeoFeatureCollection states;
    GeoFeatureCollection countries;
    // Italian admin areas, filled only when the requested target is a region or
    // a city instead of a country.
    GeoFeatureCollection places;
    const GeoFeature* highlighted = nullptr;
    bool country_mode = false;
    bool region_mode = false;
    std::string place_name;
    double place_pin_lat = 0.0;
    double place_pin_lon = 0.0;
    LonLatBox lower48;
    LonLatBox target_box;
    LonLatBox country_view_box;
};

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
               int tracking = 0);
void draw_text_centered(Font& font, cv::Mat& frame, const std::string& text,
                        int center_x, int baseline, int px,
                        const cv::Scalar& color, double alpha,
                        int tracking = 0);
void draw_text_outlined(Font& font, cv::Mat& frame, const std::string& text,
                        int x, int baseline, int px, const cv::Scalar& color,
                        double alpha, int tracking, const cv::Scalar& outline,
                        int radius);

[[nodiscard]] std::vector<cv::Point> project_ring(
    const std::vector<GeoPosition>& ring, const View& view);

[[nodiscard]] std::vector<std::vector<cv::Point>> project_rings(
    const GeoFeature& feature, const View& view);

void fill_rings(cv::Mat& frame, const std::vector<std::vector<cv::Point>>& rings,
                const cv::Scalar& color);
void stroke_rings(cv::Mat& frame, const std::vector<std::vector<cv::Point>>& rings,
                  const cv::Scalar& color, int thickness);
void fill_feature(cv::Mat& frame, const GeoFeature& feature, const View& view,
                  const cv::Scalar& fill, const cv::Scalar& hole_color);
void stroke_feature(cv::Mat& frame, const GeoFeature& feature, const View& view,
                    const cv::Scalar& color, int thickness);

// Blend a filled/blurred mask with a shade color at a per-pixel alpha.
void blend_mask(cv::Mat& frame, const cv::Mat& mask, const cv::Scalar& shade,
                double alpha);

[[nodiscard]] cv::Rect clamp_roi(const cv::Mat& frame, const cv::Rect& rect);
void draw_line_alpha(cv::Mat& frame, cv::Point from, cv::Point to,
                     const cv::Scalar& color, int thickness, double alpha);
void draw_rect_alpha(cv::Mat& frame, const cv::Rect& rect, const cv::Scalar& fill,
                     const cv::Scalar& border, int border_thickness, double alpha);
void draw_disc_alpha(cv::Mat& frame, cv::Point center, int radius,
                     const cv::Scalar& color, double alpha);
void draw_ring_alpha(cv::Mat& frame, cv::Point center, int radius,
                     const cv::Scalar& color, int thickness, double alpha);

void draw_country_context(cv::Mat& frame, const GeoFeatureCollection& countries,
                          const View& view, const cv::Scalar& fill,
                          const cv::Scalar& border, int thickness,
                          const cv::Scalar& water);
void draw_states(cv::Mat& frame, const GeoFeatureCollection& states, const View& view,
                 const cv::Scalar& fill, const cv::Scalar& border, int thickness,
                 const cv::Scalar& water, bool outline_only = false);
void draw_region_context(cv::Mat& frame, const Geometry& geometry,
                         const View& view, const cv::Scalar& fill,
                         const cv::Scalar& border, int thickness,
                         const cv::Scalar& water, bool outline_only = false);
void draw_graticule(cv::Mat& frame, const View& view, const cv::Scalar& color,
                    int thickness, double step, bool crosses);
void draw_hatch(cv::Mat& frame, const GeoFeature& feature, const View& view,
                const cv::Scalar& color, int spacing, int thickness);

[[nodiscard]] GeoFeatureCollection load_geojson(const fs::path& path);

[[nodiscard]] fs::path resolve_catalog_file(const std::string& requested,
                                            const char* env_name,
                                            const char* filename,
                                            const char* fallback);

[[nodiscard]] Geometry load_geometry(const fs::path& states_path,
                                     const fs::path& countries_path,
                                     const std::string& highlight_state,
                                     const std::string& highlight_country);

struct Plates {
    cv::Mat base;
    cv::Mat hi;
};

// The plates are composed at 2.12x (2x supersampling + 6% overscan) and then
// reduced once to 1.06x, which is exactly the largest frame the push-in can
// show. Per-frame sampling therefore only crops and performs a small resize

[[nodiscard]] View style_view(const std::string& slug, const Geometry& geometry,
                              int width, int height);

[[nodiscard]] Plates build_plates(const std::string& slug, const Geometry& geometry,
                                  int width, int height);

[[nodiscard]] cv::Mat sample_plates(const Plates& plates, double zoom,
                                    double highlight_alpha, int width, int height);

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
                                 double grain_strength, double vignette_strength);

void draw_overlay(const std::string& slug, cv::Mat& frame,
                  const Geometry& geometry, Fonts& fonts, const View& screen,
                  double t, int width, int height,
                  const chrononplugin::maps::EditorialMapRequest& config);

}  // namespace chrononplugin::maps::editorial_pack_detail
