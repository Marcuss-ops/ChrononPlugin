// Orbital descent: an orthographic globe built from the Natural Earth country
// collection turns from Asia toward Europe, grows past the frame edges, and
// hands over to the live editorial map. No texture download is involved: the
// sphere is rasterized from the same polygons the other native compositions
// use.
//
// The sphere is the hot path of this composition. Every frame rotates the same
// immutable country collection, so the unit-sphere coordinates are built once
// and cached; per frame only the view rotation is applied. Rings outside the
// visible hemisphere are rejected by their bounding cap before projection.

#include "chrononplugin/maps/map_story_effects.hpp"

#include "map_story_effects_internal.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace chrononplugin::maps {
using namespace story_detail;

namespace {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct GlobeVertex {
    float sin_lat = 0.0F;
    float cos_lat_cos_lon = 0.0F;
    float cos_lat_sin_lon = 0.0F;
};

struct GlobeRing {
    std::vector<GlobeVertex> points;
    // Bounding spherical cap used to reject rings that are entirely behind
    // the globe. The cap is conservative, so it can never hide a visible ring.
    double cap_x = 0.0;
    double cap_y = 0.0;
    double cap_z = 0.0;
    double cap_angle = 0.0;
    bool hole = false;
};

struct GlobeGeometry {
    std::vector<GlobeRing> rings;
    std::size_t point_count = 0;
    std::uint64_t signature = 0;
};

struct GlobeCache {
    GlobeGeometry geometry;
};

struct Projector {
    double cos_lat = 1.0;
    double sin_lat = 0.0;
    double cos_lon = 1.0;
    double sin_lon = 0.0;

    Vec3 project(const GlobeVertex& vertex) const {
        const double cos_delta =
            static_cast<double>(vertex.cos_lat_cos_lon) * cos_lon +
            static_cast<double>(vertex.cos_lat_sin_lon) * sin_lon;
        return {
            static_cast<double>(vertex.cos_lat_sin_lon) * cos_lon -
                static_cast<double>(vertex.cos_lat_cos_lon) * sin_lon,
            cos_lat * static_cast<double>(vertex.sin_lat) -
                sin_lat * cos_delta,
            sin_lat * static_cast<double>(vertex.sin_lat) +
                cos_lat * cos_delta,
        };
    }
};

struct Star {
    int x = 0;
    int y = 0;
    int base = 0;
    int radius = 1;
    double phase = 0.0;
};

struct StarField {
    int cols = -1;
    int rows = -1;
    std::vector<Star> stars;
};

double smooth01(double value) {
    const double t = std::clamp(value, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

std::uint64_t mix64(std::uint64_t hash, std::uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
    return hash;
}

std::uint64_t double_bits(double value) {
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "double must be 64 bit");
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

std::uint64_t geometry_signature(const GeoFeatureCollection& countries,
                                 std::size_t* point_count) {
    std::uint64_t hash = mix64(
        0x474c4f4245ULL,
        reinterpret_cast<std::uintptr_t>(countries.features.data()));
    std::size_t points = 0;
    for (const GeoFeature& feature : countries.features) {
        hash = mix64(hash, feature.polygons.size());
        for (const GeoPolygon& polygon : feature.polygons) {
            hash = mix64(hash, polygon.rings.size());
            for (const auto& ring : polygon.rings) {
                hash = mix64(hash, ring.size());
                points += ring.size();
                if (!ring.empty()) {
                    hash = mix64(hash, double_bits(ring.front()[0]));
                    hash = mix64(hash, double_bits(ring.front()[1]));
                    hash = mix64(hash, double_bits(ring.back()[0]));
                    hash = mix64(hash, double_bits(ring.back()[1]));
                }
            }
        }
    }
    *point_count = points;
    return hash;
}

GlobeGeometry build_globe_geometry(const GeoFeatureCollection& countries) {
    GlobeGeometry geometry;
    geometry.point_count = 0;
    for (const GeoFeature& feature : countries.features) {
        for (const GeoPolygon& polygon : feature.polygons) {
            for (std::size_t ring_index = 0; ring_index < polygon.rings.size();
                 ++ring_index) {
                const auto& source = polygon.rings[ring_index];
                if (source.size() < 3) continue;
                GlobeRing ring;
                ring.hole = ring_index > 0;
                ring.points.reserve(source.size());
                double sum_x = 0.0;
                double sum_y = 0.0;
                double sum_z = 0.0;
                for (const GeoPosition& position : source) {
                    const double lon = position[0] * kPi / 180.0;
                    const double lat = position[1] * kPi / 180.0;
                    const double cos_lat = std::cos(lat);
                    const double sin_lat = std::sin(lat);
                    const double cos_lon = std::cos(lon);
                    const double sin_lon = std::sin(lon);
                    const double x = cos_lat * cos_lon;
                    const double y = sin_lat;
                    const double z = cos_lat * sin_lon;
                    ring.points.push_back({
                        static_cast<float>(sin_lat),
                        static_cast<float>(x),
                        static_cast<float>(z),
                    });
                    sum_x += x;
                    sum_y += y;
                    sum_z += z;
                }
                geometry.point_count += ring.points.size();
                const double sum_length =
                    std::sqrt(sum_x * sum_x + sum_y * sum_y + sum_z * sum_z);
                if (sum_length > 1e-12) {
                    const double cx = sum_x / sum_length;
                    const double cy = sum_y / sum_length;
                    const double cz = sum_z / sum_length;
                    double max_dot = -1.0;
                    for (const GlobeVertex& vertex : ring.points) {
                        const double dot =
                            cx * static_cast<double>(vertex.cos_lat_cos_lon) +
                            cy * static_cast<double>(vertex.sin_lat) +
                            cz * static_cast<double>(vertex.cos_lat_sin_lon);
                        max_dot = std::max(max_dot, dot);
                    }
                    ring.cap_x = cx;
                    ring.cap_y = cy;
                    ring.cap_z = cz;
                    ring.cap_angle =
                        std::acos(std::clamp(max_dot, -1.0, 1.0));
                } else {
                    // A ring spanning more than a hemisphere (or a degenerate
                    // centroid) must never be rejected by the cap test.
                    ring.cap_angle = kPi;
                }
                geometry.rings.push_back(std::move(ring));
            }
        }
    }
    return geometry;
}

const GlobeGeometry& globe_geometry(const GeoFeatureCollection& countries) {
    static thread_local GlobeCache cache;
    std::size_t point_count = 0;
    const std::uint64_t signature = geometry_signature(countries, &point_count);
    if (cache.geometry.signature != signature ||
        cache.geometry.point_count != point_count) {
        cache.geometry = build_globe_geometry(countries);
        cache.geometry.signature = signature;
        cache.geometry.point_count = point_count;
    }
    return cache.geometry;
}

bool ring_may_be_visible(const GlobeRing& ring, const Projector& projector) {
    if (ring.cap_angle >= kPi * 0.5) return true;
    const double view_x = projector.cos_lat * projector.cos_lon;
    const double view_y = projector.sin_lat;
    const double view_z = projector.cos_lat * projector.sin_lon;
    const double dot =
        ring.cap_x * view_x + ring.cap_y * view_y + ring.cap_z * view_z;
    // cos(90deg + cap) = -sin(cap): a ring whose whole cap lies past the
    // horizon has no visible point.
    return dot >= -std::sin(ring.cap_angle) - 1e-12;
}

inline cv::Point to_screen(const Vec3& point, cv::Point center, double radius) {
    return {
        cvRound(center.x + point.x * radius),
        cvRound(center.y - point.y * radius),
    };
}

void project_clipped(const GlobeRing& ring, const Projector& projector,
                     cv::Point center, double radius,
                     std::vector<cv::Point>& out) {
    out.clear();
    if (ring.points.empty()) return;
    out.reserve(ring.points.size() + 4);
    Vec3 previous = projector.project(ring.points.back());
    bool previous_visible = previous.z >= 0.0;
    for (const GlobeVertex& vertex : ring.points) {
        const Vec3 current = projector.project(vertex);
        const bool current_visible = current.z >= 0.0;
        if (current_visible != previous_visible) {
            const double denominator = previous.z - current.z;
            const double t = std::fabs(denominator) < 1e-12
                ? 0.0 : previous.z / denominator;
            out.push_back(to_screen({
                previous.x + (current.x - previous.x) * t,
                previous.y + (current.y - previous.y) * t,
                0.0,
            }, center, radius));
        }
        if (current_visible) out.push_back(to_screen(current, center, radius));
        previous = current;
        previous_visible = current_visible;
    }
}

cv::Rect circle_bounds(cv::Point center, int radius, cv::Size size) {
    const int x0 = std::max(0, center.x - radius);
    const int y0 = std::max(0, center.y - radius);
    const int x1 = std::min(size.width, center.x + radius + 1);
    const int y1 = std::min(size.height, center.y + radius + 1);
    return {x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

Vec3 project_to_view(double lon_deg, double lat_deg, double center_lon_deg,
                     double center_lat_deg) {
    const double lon = lon_deg * kPi / 180.0;
    const double lat = lat_deg * kPi / 180.0;
    const double center_lon = center_lon_deg * kPi / 180.0;
    const double center_lat = center_lat_deg * kPi / 180.0;
    const double delta_lon = lon - center_lon;
    const double cos_lat = std::cos(lat);
    const double sin_lat = std::sin(lat);
    const double sin_center = std::sin(center_lat);
    const double cos_center = std::cos(center_lat);
    const double cos_delta = std::cos(delta_lon);
    return {
        cos_lat * std::sin(delta_lon),
        cos_center * sin_lat - sin_center * cos_lat * cos_delta,
        sin_center * sin_lat + cos_center * cos_lat * cos_delta,
    };
}

void draw_graticule(cv::Mat& canvas, cv::Point center, double radius,
                    double center_lon, double center_lat, double scale) {
    const cv::Scalar ink(73, 88, 102);
    const int thickness = std::max(1, cvRound(0.9 * scale));
    for (int lat = -60; lat <= 60; lat += 30) {
        std::vector<cv::Point> segment;
        for (int lon = -180; lon <= 180; lon += 4) {
            const Vec3 view = project_to_view(lon, lat, center_lon, center_lat);
            if (view.z < 0.0) {
                if (segment.size() >= 2) {
                    cv::polylines(canvas, segment, false, ink, thickness,
                                  cv::LINE_AA);
                }
                segment.clear();
            } else {
                segment.push_back(to_screen(view, center, radius));
            }
        }
        if (segment.size() >= 2) {
            cv::polylines(canvas, segment, false, ink, thickness, cv::LINE_AA);
        }
    }
    for (int lon = -180; lon < 180; lon += 30) {
        std::vector<cv::Point> segment;
        for (int lat = -84; lat <= 84; lat += 4) {
            const Vec3 view = project_to_view(lon, lat, center_lon, center_lat);
            if (view.z < 0.0) {
                if (segment.size() >= 2) {
                    cv::polylines(canvas, segment, false, ink, thickness,
                                  cv::LINE_AA);
                }
                segment.clear();
            } else {
                segment.push_back(to_screen(view, center, radius));
            }
        }
        if (segment.size() >= 2) {
            cv::polylines(canvas, segment, false, ink, thickness, cv::LINE_AA);
        }
    }
}

const StarField& star_field(int cols, int rows) {
    static thread_local StarField field;
    if (field.cols == cols && field.rows == rows) return field;
    field.cols = cols;
    field.rows = rows;
    field.stars.clear();
    cv::RNG rng(0x51a7e5u);
    const int count = std::max(80, rows / 5);
    field.stars.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        Star star;
        star.x = rng.uniform(0, cols);
        star.y = rng.uniform(0, rows);
        star.base = rng.uniform(45, 185);
        star.radius = (i % 19 == 0) ? 2 : 1;
        star.phase = static_cast<double>(i) * 1.7;
        field.stars.push_back(star);
    }
    return field;
}

void draw_stars(cv::Mat& canvas, double progress) {
    const StarField& field = star_field(canvas.cols, canvas.rows);
    for (const Star& star : field.stars) {
        const double twinkle =
            0.72 + 0.28 * std::sin(progress * 31.0 + star.phase);
        const int value = std::clamp(cvRound(star.base * twinkle), 0, 255);
        cv::circle(canvas, {star.x, star.y}, star.radius,
                   cv::Scalar(value, value, value), cv::FILLED, cv::LINE_AA);
    }
}

}  // namespace

void draw_globe_dive(cv::Mat& frame,
                     const GeoFeatureCollection& countries,
                     double progress, const std::string& label,
                     bool show_text) {
    if (frame.empty() || frame.cols < 320 || frame.rows < 180) return;
    progress = std::clamp(progress, 0.0, 1.0);
    const double scale = std::clamp(frame.rows / 1080.0, 0.45, 1.5);
    const double reveal = smooth01((progress - 0.67) / 0.16);
    if (reveal >= 0.999) {
        if (show_text && !label.empty()) {
            draw_location_tag(frame, {frame.cols / 2, frame.rows / 2}, label,
                              1.0);
        }
        return;
    }
    if (countries.features.empty()) return;

    const double flight = smooth01(progress / 0.74);
    const double radius = frame.rows * (0.36 + 1.30 * std::pow(flight, 1.6));
    const cv::Point center(
        frame.cols / 2,
        frame.rows / 2 + cvRound(frame.rows * 0.035 * (1.0 - flight)));
    const double center_lon = 104.0 - 96.0 * flight;
    const double center_lat = 20.0 + 31.0 * flight;

    cv::Mat canvas(frame.size(), CV_8UC3, cv::Scalar(7, 6, 5));
    const double backdrop_radius = radius * 1.55;
    const double far_x = std::max(center.x, canvas.cols - 1 - center.x);
    const double far_y = std::max(center.y, canvas.rows - 1 - center.y);
    if (backdrop_radius * backdrop_radius >= far_x * far_x + far_y * far_y) {
        canvas.setTo(cv::Scalar(13, 18, 24));
    } else {
        cv::circle(canvas, center, cvRound(backdrop_radius),
                   cv::Scalar(13, 18, 24), cv::FILLED, cv::LINE_AA);
    }
    draw_stars(canvas, progress);

    const int atmosphere_radius = cvRound(radius + 20.0 * scale);
    const int blur_sigma = std::max(2, cvRound(12.0 * scale));
    const cv::Rect atmosphere_roi = circle_bounds(
        center, atmosphere_radius + 3 * blur_sigma + 2, canvas.size());
    if (atmosphere_roi.width > 0 && atmosphere_roi.height > 0) {
        // The atmosphere is a soft radial glow: rendering it at half
        // resolution costs a quarter of the blur and is visually identical
        // after the bilinear expansion.
        const double atmosphere_scale = frame.rows >= 720 ? 0.5 : 1.0;
        const int small_width = std::max(
            1, cvRound(atmosphere_roi.width * atmosphere_scale));
        const int small_height = std::max(
            1, cvRound(atmosphere_roi.height * atmosphere_scale));
        cv::Mat atmosphere(small_height, small_width, CV_8UC3,
                           cv::Scalar(0, 0, 0));
        const cv::Point local_center(
            cvRound((center.x - atmosphere_roi.x) * atmosphere_scale),
            cvRound((center.y - atmosphere_roi.y) * atmosphere_scale));
        cv::circle(atmosphere, local_center,
                   cvRound(atmosphere_radius * atmosphere_scale),
                   cv::Scalar(44, 28, 14), cv::FILLED, cv::LINE_AA);
        cv::circle(atmosphere, local_center,
                   cvRound((radius + 10.0 * scale) * atmosphere_scale),
                   cv::Scalar(86, 57, 30), cv::FILLED, cv::LINE_AA);
        cv::GaussianBlur(atmosphere, atmosphere, {0, 0},
                         std::max(1.0, 12.0 * scale * atmosphere_scale));
        if (atmosphere_scale < 1.0) {
            cv::Mat expanded;
            cv::resize(atmosphere, expanded, atmosphere_roi.size(), 0.0, 0.0,
                       cv::INTER_LINEAR);
            atmosphere = std::move(expanded);
        }
        cv::addWeighted(canvas(atmosphere_roi), 1.0, atmosphere, 0.56, 0.0,
                        canvas(atmosphere_roi));
    }

    cv::circle(canvas, center, cvRound(radius), cv::Scalar(63, 43, 24),
               cv::FILLED, cv::LINE_AA);
    const cv::Point shadow_center =
        center + cv::Point(cvRound(radius * 0.12), cvRound(radius * 0.16));
    const int shadow_radius = cvRound(radius * 0.98);
    const cv::Rect shadow_roi =
        circle_bounds(shadow_center, shadow_radius, canvas.size());
    if (shadow_roi.width > 0 && shadow_roi.height > 0) {
        cv::Mat shadow = canvas(shadow_roi).clone();
        cv::circle(shadow,
                   shadow_center - cv::Point(shadow_roi.x, shadow_roi.y),
                   shadow_radius, cv::Scalar(4, 5, 8), cv::FILLED,
                   cv::LINE_AA);
        cv::addWeighted(canvas(shadow_roi), 0.82, shadow, 0.18, 0.0,
                        canvas(shadow_roi));
    }

    draw_graticule(canvas, center, radius, center_lon, center_lat, scale);

    const Projector projector{
        std::cos(center_lat * kPi / 180.0),
        std::sin(center_lat * kPi / 180.0),
        std::cos(center_lon * kPi / 180.0),
        std::sin(center_lon * kPi / 180.0),
    };
    const GlobeGeometry& geometry = globe_geometry(countries);
    std::vector<std::vector<cv::Point>> land_rings;
    std::vector<std::vector<cv::Point>> hole_rings;
    land_rings.reserve(geometry.rings.size());
    hole_rings.reserve(geometry.rings.size() / 4);
    std::vector<cv::Point> projected;
    for (const GlobeRing& ring : geometry.rings) {
        if (!ring_may_be_visible(ring, projector)) continue;
        project_clipped(ring, projector, center, radius, projected);
        if (projected.size() < 3) continue;
        if (ring.hole) {
            hole_rings.push_back(projected);
        } else {
            land_rings.push_back(projected);
        }
    }

    // The fill does not need anti-aliasing: the AA border below owns the
    // silhouette, while LINE_8 is markedly cheaper over thousands of rings.
    if (!land_rings.empty()) {
        cv::fillPoly(canvas, land_rings, cv::Scalar(176, 199, 213),
                     cv::LINE_8);
    }
    if (!hole_rings.empty()) {
        cv::fillPoly(canvas, hole_rings, cv::Scalar(63, 43, 24),
                     cv::LINE_8);
    }
    const int border_thickness = std::max(1, cvRound(1.1 * scale));
    if (!land_rings.empty()) {
        cv::polylines(canvas, land_rings, true, cv::Scalar(83, 75, 55),
                      border_thickness, cv::LINE_AA);
    }
    if (!hole_rings.empty()) {
        cv::polylines(canvas, hole_rings, true, cv::Scalar(83, 75, 55),
                      border_thickness, cv::LINE_AA);
    }

    cv::circle(canvas, center, cvRound(radius), cv::Scalar(142, 112, 70),
               std::max(1, cvRound(1.4 * scale)), cv::LINE_AA);

    if (reveal <= 0.0) {
        frame = std::move(canvas);
    } else {
        cv::Mat result;
        cv::addWeighted(frame, reveal, canvas, 1.0 - reveal, 0.0, result);
        frame = std::move(result);
    }

    const double label_alpha = smooth01((progress - 0.78) / 0.18);
    if (show_text && label_alpha > 0.01 && !label.empty()) {
        draw_location_tag(frame, {frame.cols / 2, frame.rows / 2}, label,
                          label_alpha);
    }
}

}  // namespace chrononplugin::maps
