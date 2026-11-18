#include "chrononplugin/maps/editorial_map_scene.hpp"

#include "chrononplugin/maps/geojson.hpp"

#include "clipper2/clipper.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chrononplugin::maps {
namespace {

namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;

constexpr double kPi = 3.14159265358979323846;
constexpr double kClipScale = 1000.0;
constexpr double kCanvasPadding = 48.0;

struct Point {
    double x = 0.0;
    double y = 0.0;
};

using Ring = std::vector<Point>;

struct Bounds {
    double minX = std::numeric_limits<double>::max();
    double minY = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double maxY = std::numeric_limits<double>::lowest();

    void add(const Point& point) {
        minX = std::min(minX, point.x);
        minY = std::min(minY, point.y);
        maxX = std::max(maxX, point.x);
        maxY = std::max(maxY, point.y);
    }

    void add(const Bounds& other) {
        if (!other.valid()) return;
        minX = std::min(minX, other.minX);
        minY = std::min(minY, other.minY);
        maxX = std::max(maxX, other.maxX);
        maxY = std::max(maxY, other.maxY);
    }

    [[nodiscard]] bool valid() const { return minX <= maxX && minY <= maxY; }
};

struct LonLatBox {
    double lonMin = 180.0;
    double lonMax = -180.0;
    double latMin = 90.0;
    double latMax = -90.0;

    void add(double lon, double lat) {
        lonMin = std::min(lonMin, lon);
        lonMax = std::max(lonMax, lon);
        latMin = std::min(latMin, lat);
        latMax = std::max(latMax, lat);
    }

    [[nodiscard]] bool valid() const { return lonMin <= lonMax && latMin <= latMax; }
};

struct Rect {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
};

struct View {
    double mx0 = 0.0;
    double my0 = 0.0;
    double scale = 1.0;
    double cx = 0.0;
    double cy = 0.0;

    [[nodiscard]] Point project(double lat, double lon) const {
        return {cx + (mercatorX(lon) - mx0) * scale,
                cy + (mercatorY(lat) - my0) * scale};
    }

    [[nodiscard]] static double mercatorX(double lon) { return (lon + 180.0) / 360.0; }

    [[nodiscard]] static double mercatorY(double lat) {
        const double clamped = std::clamp(lat, -85.05112878, 85.05112878);
        return (1.0 - std::asinh(std::tan(clamped * kPi / 180.0)) / kPi) * 0.5;
    }
};

[[nodiscard]] View fitBox(const LonLatBox& box, const Rect& rect) {
    View view;
    const double x0 = View::mercatorX(box.lonMin);
    const double x1 = View::mercatorX(box.lonMax);
    const double y0 = View::mercatorY(box.latMax);
    const double y1 = View::mercatorY(box.latMin);
    const double spanX = std::max(1e-12, x1 - x0);
    const double spanY = std::max(1e-12, y1 - y0);
    view.scale = std::min(rect.width / spanX, rect.height / spanY);
    view.mx0 = (x0 + x1) * 0.5;
    view.my0 = (y0 + y1) * 0.5;
    view.cx = rect.x + rect.width * 0.5;
    view.cy = rect.y + rect.height * 0.5;
    return view;
}

[[nodiscard]] Rect fractionRect(int width, int height, double x, double y,
                                double w, double h) {
    return {width * x, height * y, width * w, height * h};
}

// Overlay constants are authored on the 1920x1080 reference grid; keep them
// proportional on any other canvas.
struct Design {
    double sx = 1.0;
    double sy = 1.0;
    double s = 1.0;

    [[nodiscard]] double x(double value) const { return value * sx; }
    [[nodiscard]] double y(double value) const { return value * sy; }
    [[nodiscard]] double f(double value) const { return value * s; }
};

[[nodiscard]] double round2(double value) {
    return std::round(value * 100.0) / 100.0;
}

[[nodiscard]] Json pointJson(const Point& point) {
    return Json::array({round2(point.x), round2(point.y)});
}

[[nodiscard]] Json ringJson(const Ring& ring) {
    Json out = Json::array();
    for (const Point& point : ring) out.push_back(pointJson(point));
    return out;
}

[[nodiscard]] Json emptyArray() { return Json::array(); }

[[nodiscard]] double ringArea(const Ring& ring) {
    double twice = 0.0;
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const Point& a = ring[i];
        const Point& b = ring[(i + 1) % ring.size()];
        twice += a.x * b.y - b.x * a.y;
    }
    return twice * 0.5;
}

[[nodiscard]] double pointSegmentDistance(const Point& point, const Point& a,
                                          const Point& b) {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double lengthSquared = dx * dx + dy * dy;
    if (lengthSquared <= 1e-12) {
        const double px = point.x - a.x;
        const double py = point.y - a.y;
        return std::sqrt(px * px + py * py);
    }
    const double t = std::clamp(
        ((point.x - a.x) * dx + (point.y - a.y) * dy) / lengthSquared, 0.0, 1.0);
    const double px = point.x - (a.x + t * dx);
    const double py = point.y - (a.y + t * dy);
    return std::sqrt(px * px + py * py);
}

[[nodiscard]] Ring simplify(const Ring& input, double epsilon) {
    if (input.size() < 3 || epsilon <= 0.0) return input;
    // A GeoJSON ring repeats its first vertex. RDP works on the open sequence
    // and the consumer closes the path itself.
    Ring points = input;
    if (points.size() > 1) {
        const Point& first = points.front();
        const Point& last = points.back();
        if (std::abs(first.x - last.x) < 1e-9 && std::abs(first.y - last.y) < 1e-9)
            points.pop_back();
    }
    if (points.size() < 3) return {};
    std::vector<char> keep(points.size(), 0);
    keep.front() = 1;
    keep.back() = 1;
    struct Range {
        std::size_t first;
        std::size_t last;
    };
    std::vector<Range> stack{{0, points.size() - 1}};
    while (!stack.empty()) {
        const Range range = stack.back();
        stack.pop_back();
        if (range.last <= range.first + 1) continue;
        double farthest = -1.0;
        std::size_t index = range.first;
        for (std::size_t i = range.first + 1; i < range.last; ++i) {
            const double distance =
                pointSegmentDistance(points[i], points[range.first], points[range.last]);
            if (distance > farthest) {
                farthest = distance;
                index = i;
            }
        }
        if (farthest > epsilon) {
            keep[index] = 1;
            stack.push_back({range.first, index});
            stack.push_back({index, range.last});
        }
    }
    Ring out;
    out.reserve(points.size());
    for (std::size_t i = 0; i < points.size(); ++i) {
        if (!keep[i]) continue;
        if (!out.empty()) {
            const Point& last = out.back();
            if (std::abs(last.x - points[i].x) < 1e-6 &&
                std::abs(last.y - points[i].y) < 1e-6)
                continue;
        }
        out.push_back(points[i]);
    }
    return out.size() >= 3 ? out : Ring{};
}

[[nodiscard]] Clipper2Lib::Path64 toPath(const Ring& ring) {
    Clipper2Lib::Path64 path;
    path.reserve(ring.size());
    for (const Point& point : ring) {
        path.emplace_back(static_cast<std::int64_t>(std::llround(point.x * kClipScale)),
                          static_cast<std::int64_t>(std::llround(point.y * kClipScale)));
    }
    return path;
}

[[nodiscard]] Ring fromPath(const Clipper2Lib::Path64& path) {
    Ring ring;
    ring.reserve(path.size());
    for (const Clipper2Lib::Point64& point : path) {
        ring.push_back({static_cast<double>(point.x) / kClipScale,
                        static_cast<double>(point.y) / kClipScale});
    }
    return ring;
}

[[nodiscard]] Clipper2Lib::Path64 clipRectangle(const Bounds& bounds) {
    const auto x0 = static_cast<std::int64_t>(std::llround(bounds.minX * kClipScale));
    const auto y0 = static_cast<std::int64_t>(std::llround(bounds.minY * kClipScale));
    const auto x1 = static_cast<std::int64_t>(std::llround(bounds.maxX * kClipScale));
    const auto y1 = static_cast<std::int64_t>(std::llround(bounds.maxY * kClipScale));
    return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
}

[[nodiscard]] std::vector<Ring> clipRing(const Ring& ring, const Bounds& clipBounds) {
    if (ring.size() < 3) return {};
    Clipper2Lib::Clipper64 clipper;
    clipper.AddSubject({toPath(ring)});
    clipper.AddClip({clipRectangle(clipBounds)});
    Clipper2Lib::Paths64 solution;
    clipper.Execute(Clipper2Lib::ClipType::Intersection, Clipper2Lib::FillRule::EvenOdd,
                    solution);
    std::vector<Ring> out;
    out.reserve(solution.size());
    for (const auto& path : solution) {
        Ring clipped = fromPath(path);
        if (clipped.size() >= 3 && std::abs(ringArea(clipped)) >= 0.75)
            out.push_back(std::move(clipped));
    }
    return out;
}

// Liang-Barsky segment clipping against the canvas.
[[nodiscard]] bool clipSegment(Point& a, Point& b, double width, double height) {
    double t0 = 0.0;
    double t1 = 1.0;
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const std::array<double, 4> p{-dx, dx, -dy, dy};
    const std::array<double, 4> q{a.x, width - a.x, a.y, height - a.y};
    for (std::size_t i = 0; i < 4; ++i) {
        if (std::abs(p[i]) < 1e-12) {
            if (q[i] < 0.0) return false;
            continue;
        }
        const double t = q[i] / p[i];
        if (p[i] < 0.0) {
            if (t > t1) return false;
            if (t > t0) t0 = t;
        } else {
            if (t < t0) return false;
            if (t < t1) t1 = t;
        }
    }
    const Point original = a;
    a = {original.x + dx * t0, original.y + dy * t0};
    b = {original.x + dx * t1, original.y + dy * t1};
    return true;
}

[[nodiscard]] std::vector<Ring> projectRing(const std::vector<GeoPosition>& ring,
                                            const View& view, double tolerance,
                                            const Bounds& clipBounds) {
    if (ring.empty()) return {};
    double lonMin = 180.0;
    double lonMax = -180.0;
    for (const GeoPosition& position : ring) {
        lonMin = std::min(lonMin, position[0]);
        lonMax = std::max(lonMax, position[0]);
    }
    // Natural Earth splits most antimeridian polygons; this guard keeps a
    // wrapping ring from becoming a full-width band in a continental view.
    if (lonMax - lonMin > 180.0) return {};

    Ring projected;
    projected.reserve(ring.size());
    for (const GeoPosition& position : ring)
        projected.push_back(view.project(position[1], position[0]));
    Ring simplified = simplify(projected, tolerance);
    if (simplified.size() < 3) return {};
    return clipRing(simplified, clipBounds);
}

[[nodiscard]] Json featureJson(const GeoFeature& feature, const View& view,
                               double tolerance, const Bounds& clipBounds) {
    Json polygons = Json::array();
    for (const GeoPolygon& polygon : feature.polygons) {
        Json rings = Json::array();
        for (const auto& sourceRing : polygon.rings) {
            for (const Ring& ring : projectRing(sourceRing, view, tolerance, clipBounds))
                rings.push_back(ringJson(ring));
        }
        if (!rings.empty()) polygons.push_back(std::move(rings));
    }
    return Json{{"polygons", std::move(polygons)}};
}

[[nodiscard]] Json featuresJson(const GeoFeatureCollection& collection, const View& view,
                                double tolerance, const Bounds& clipBounds) {
    Json out = Json::array();
    for (const GeoFeature& feature : collection.features)
        out.push_back(featureJson(feature, view, tolerance, clipBounds));
    return out;
}

[[nodiscard]] Bounds boundsOf(const Json& polygons) {
    Bounds bounds;
    for (const auto& polygon : polygons) {
        for (const auto& ring : polygon) {
            for (const auto& point : ring) {
                bounds.add(Point{point[0].get<double>(), point[1].get<double>()});
            }
        }
    }
    return bounds;
}

[[nodiscard]] Json hatchSegments(const Json& highlightPolygons, double spacing,
                                 const Rect& clipRect) {
    Json out = Json::array();
    const Bounds highlight = boundsOf(highlightPolygons);
    if (!highlight.valid()) return out;

    Clipper2Lib::Paths64 clipPaths;
    for (const auto& polygon : highlightPolygons) {
        for (const auto& ring : polygon) {
            Ring points;
            for (const auto& point : ring)
                points.push_back({point[0].get<double>(), point[1].get<double>()});
            if (points.size() >= 3) clipPaths.push_back(toPath(points));
        }
    }
    if (clipPaths.empty()) return out;

    Clipper2Lib::Paths64 openPaths;
    const double step = std::max(3.0, spacing);
    const double height = highlight.maxY - highlight.minY;
    for (double offset = -height; offset < (highlight.maxX - highlight.minX) + height;
         offset += step) {
        const Point a{highlight.minX + offset, highlight.minY};
        const Point b{highlight.minX + offset + height, highlight.maxY};
        openPaths.push_back({Clipper2Lib::Point64{
                                 static_cast<std::int64_t>(std::llround(a.x * kClipScale)),
                                 static_cast<std::int64_t>(std::llround(a.y * kClipScale))},
                             Clipper2Lib::Point64{
                                 static_cast<std::int64_t>(std::llround(b.x * kClipScale)),
                                 static_cast<std::int64_t>(std::llround(b.y * kClipScale))}});
    }

    Clipper2Lib::Clipper64 clipper;
    clipper.AddOpenSubject(openPaths);
    clipper.AddClip(clipPaths);
    Clipper2Lib::Paths64 closed;
    Clipper2Lib::Paths64 open;
    clipper.Execute(Clipper2Lib::ClipType::Intersection, Clipper2Lib::FillRule::EvenOdd,
                    closed, open);
    for (const auto& path : open) {
        if (path.size() < 2) continue;
        Point a = {static_cast<double>(path.front().x) / kClipScale,
                   static_cast<double>(path.front().y) / kClipScale};
        Point b = {static_cast<double>(path.back().x) / kClipScale,
                   static_cast<double>(path.back().y) / kClipScale};
        if (!clipSegment(a, b, clipRect.width, clipRect.height)) continue;
        if (std::hypot(b.x - a.x, b.y - a.y) < 0.5) continue;
        out.push_back(Json::array({pointJson(a), pointJson(b)}));
    }
    return out;
}

[[nodiscard]] Json graticuleSegments(const View& view, double step, bool crosses,
                                     const Rect& clipRect) {
    Json out = Json::array();
    auto addSegment = [&](Point a, Point b) {
        if (!clipSegment(a, b, clipRect.width, clipRect.height)) return;
        out.push_back(Json::array({pointJson(a), pointJson(b)}));
    };
    for (double lon = -180.0; lon <= 180.0 + 1e-9; lon += step) {
        addSegment(view.project(85.0, lon), view.project(-85.0, lon));
    }
    for (double lat = -80.0; lat <= 80.0 + 1e-9; lat += step) {
        addSegment(view.project(lat, -180.0), view.project(lat, 180.0));
    }
    if (crosses) {
        for (double lon = -180.0; lon <= 180.0 + 1e-9; lon += step) {
            for (double lat = -80.0; lat <= 80.0 + 1e-9; lat += step) {
                const Point center = view.project(lat, lon);
                if (center.x < 0.0 || center.y < 0.0 || center.x >= clipRect.width ||
                    center.y >= clipRect.height)
                    continue;
                addSegment({center.x - 4.0, center.y}, {center.x + 4.0, center.y});
                addSegment({center.x, center.y - 4.0}, {center.x, center.y + 4.0});
            }
        }
    }
    return out;
}

[[nodiscard]] double estimateTextWidth(const std::string& text, double size,
                                       int tracking, const std::string& font) {
    const double factor = font == "mono" ? 0.60
                        : font == "serif_bold" ? 0.54
                                               : 0.56;
    double width = static_cast<double>(text.size()) * size * factor;
    if (text.size() > 1) width += static_cast<double>(text.size() - 1) * tracking;
    return width;
}

[[nodiscard]] Json labelJson(const std::string& text, double x, double y,
                             const std::string& font, double size,
                             const std::string& color, int tracking = 0,
                             const std::string& align = "left", double opacity = 1.0,
                             const std::string& stroke = "",
                             double strokeWidth = 0.0) {
    return Json{{"text", text},
                {"x", round2(x)},
                {"y", round2(y)},
                {"width", round2(estimateTextWidth(text, size, tracking, font))},
                {"font", font},
                {"size", round2(size)},
                {"color", color},
                {"tracking", tracking},
                {"align", align},
                {"opacity", round2(opacity)},
                {"stroke", stroke},
                {"stroke_width", round2(strokeWidth)}};
}

[[nodiscard]] Json pinJson(double x, double y, const std::string& label,
                           double labelX, double labelY, double radius,
                           const std::string& fill, const std::string& ring,
                           const std::string& style) {
    return Json{{"x", round2(x)},
                {"y", round2(y)},
                {"label", label},
                {"label_x", round2(labelX)},
                {"label_y", round2(labelY)},
                {"radius", round2(radius)},
                {"fill", fill},
                {"ring", ring},
                {"style", style}};
}

[[nodiscard]] Json segmentJson(const Point& a, const Point& b, const std::string& color,
                               double width) {
    return Json{{"from", pointJson(a)},
                {"to", pointJson(b)},
                {"color", color},
                {"width", round2(width)}};
}

[[nodiscard]] Json frameJson(double x, double y, double width, double height,
                             const std::string& fill, const std::string& stroke,
                             double strokeWidth) {
    return Json{{"x", round2(x)},
                {"y", round2(y)},
                {"width", round2(width)},
                {"height", round2(height)},
                {"fill", fill},
                {"stroke", stroke},
                {"stroke_width", round2(strokeWidth)}};
}

[[nodiscard]] fs::path resolveCatalogFile(const std::string& requested,
                                          const char* envName, const char* filename,
                                          const char* fallback) {
    if (!requested.empty()) return fs::path(requested);
    if (const char* directory = std::getenv(envName);
        directory != nullptr && *directory != '\0') {
        const fs::path candidate = fs::path(directory) / filename;
        if (fs::exists(candidate)) return candidate;
    }
    return fs::path(fallback);
}

[[nodiscard]] GeoFeatureCollection loadGeoJson(const fs::path& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open GeoJSON: " + path.string());
    const std::string text((std::istreambuf_iterator<char>(input)), {});
    return parse_geojson(text);
}

[[nodiscard]] const GeoFeature* findFeature(const GeoFeatureCollection& collection,
                                            const std::string& property,
                                            const std::string& value) {
    for (const GeoFeature& feature : collection.features) {
        const auto found = feature.properties.find(property);
        if (found != feature.properties.end() && found->second == value) return &feature;
    }
    return nullptr;
}

[[nodiscard]] bool isContiguousState(const GeoFeature& feature) {
    const auto found = feature.properties.find("postal");
    if (found == feature.properties.end()) return true;
    return found->second != "AK" && found->second != "HI";
}

[[nodiscard]] LonLatBox boxOf(const GeoFeature& feature) {
    LonLatBox box;
    for (const GeoPolygon& polygon : feature.polygons) {
        for (const auto& ring : polygon.rings) {
            for (const GeoPosition& position : ring) box.add(position[0], position[1]);
        }
    }
    return box;
}

[[nodiscard]] std::string shortStyleId(EditorialMapStyle style) {
    const std::string id = editorial_map_style_id(style);
    return id.size() > 3 ? id.substr(3) : id;
}

[[nodiscard]] Json paletteFor(EditorialMapStyle style) {
    switch (style) {
        case EditorialMapStyle::kEditorialVox:
            return Json{{"background", "#CBD5E1"}, {"accent", "#D94E34"},
                        {"ink", "#1F2937"}, {"muted", "#556270"},
                        {"paper", "#F4F1EA"},
                        {"countries_fill", "#E9E4D8"},
                        {"countries_stroke", "#C9C0B2"},
                        {"states_fill", "#F4F1EA"}, {"states_stroke", "#B8B2A6"},
                        {"highlight_fill", "#D94E34"},
                        {"highlight_stroke", "#B33A25"}, {"grid", ""}, {"hatch", ""},
                        {"panel_fill", ""}, {"panel_stroke", ""}, {"shadow", ""}};
        case EditorialMapStyle::kPaperCutout:
            return Json{{"background", "#E3DED3"}, {"accent", "#C05A3E"},
                        {"ink", "#2A241C"}, {"muted", "#6B6255"},
                        {"paper", "#F8F3E7"},
                        {"countries_fill", ""}, {"countries_stroke", ""},
                        {"states_fill", "#D8D0C1"}, {"states_stroke", "#B8AD9B"},
                        {"highlight_fill", "#F8F3E7"},
                        {"highlight_stroke", "#B9AE9B"}, {"grid", ""}, {"hatch", ""},
                        {"panel_fill", ""}, {"panel_stroke", ""}, {"shadow", "#2B241B"}};
        case EditorialMapStyle::kDarkRadar:
            return Json{{"background", "#0B0F17"}, {"accent", "#38BDF8"},
                        {"ink", "#F1F5F9"}, {"muted", "#6F849E"},
                        {"paper", "#0B0F17"},
                        {"countries_fill", ""}, {"countries_stroke", ""},
                        {"states_fill", "#131A26"}, {"states_stroke", "#2D3B55"},
                        {"highlight_fill", ""}, {"highlight_stroke", ""},
                        {"grid", "#1B2636"}, {"hatch", ""},
                        {"panel_fill", ""}, {"panel_stroke", ""}, {"shadow", ""}};
        case EditorialMapStyle::kBlueprint:
            return Json{{"background", "#0E2A47"}, {"accent", "#E3C68E"},
                        {"ink", "#E8F3FC"}, {"muted", "#7FA5C6"},
                        {"paper", "#0E2A47"},
                        {"countries_fill", ""}, {"countries_stroke", ""},
                        {"states_fill", ""}, {"states_stroke", "#8FB8DA"},
                        {"highlight_fill", "#0E2A47"},
                        {"highlight_stroke", "#DCEBF8"}, {"grid", "#17395C"},
                        {"hatch", "#E3C68E"},
                        {"panel_fill", "#0A2136"}, {"panel_stroke", "#3D6C93"},
                        {"shadow", ""}};
        case EditorialMapStyle::kConnectionArc:
            return Json{{"background", "#C7D5DE"}, {"accent", "#D94E34"},
                        {"ink", "#1F2937"}, {"muted", "#5B6470"},
                        {"paper", "#F8F3E7"},
                        {"countries_fill", "#DED7C9"},
                        {"countries_stroke", "#C3B9A8"},
                        {"states_fill", "#E7E1D4"}, {"states_stroke", "#B7AE9E"},
                        {"highlight_fill", "#D94E34"},
                        {"highlight_stroke", "#7E2E1F"}, {"grid", ""}, {"hatch", ""},
                        {"panel_fill", ""}, {"panel_stroke", ""}, {"shadow", ""}};
        case EditorialMapStyle::kFloatingIsland:
            return Json{{"background", "#EDE8DE"}, {"accent", "#D94E34"},
                        {"ink", "#1F2937"}, {"muted", "#5B6470"},
                        {"paper", "#EDE8DE"},
                        {"countries_fill", ""}, {"countries_stroke", ""},
                        {"states_fill", ""}, {"states_stroke", ""},
                        {"highlight_fill", "#D94E34"},
                        {"highlight_stroke", "#7E2E1F"}, {"grid", ""}, {"hatch", ""},
                        {"panel_fill", ""}, {"panel_stroke", ""},
                        {"shadow", "#3B2A20"}};
    }
    return Json::object();
}

[[nodiscard]] Json buildScene(EditorialMapStyle style,
                              const EditorialMapSceneOptions& options,
                              const GeoFeatureCollection& states,
                              const GeoFeatureCollection& countries,
                              const GeoFeature* highlight) {
    const double width = options.width;
    const double height = options.height;
    const Design design{width / 1920.0, height / 1080.0,
                        std::min(width / 1920.0, height / 1080.0)};

    LonLatBox lower48;
    for (const GeoFeature& feature : states.features) {
        if (!isContiguousState(feature)) continue;
        const LonLatBox box = boxOf(feature);
        lower48.add(box.lonMin, box.latMin);
        lower48.add(box.lonMax, box.latMax);
    }
    if (!lower48.valid())
        throw std::runtime_error("the state catalog has no contiguous US geometry");

    const std::string id = editorial_map_style_id(style);
    View view;
    if (id == "01_editorial_vox") {
        view = fitBox(lower48, fractionRect(options.width, options.height,
                                            0.035, 0.055, 0.93, 0.815));
    } else if (id == "02_paper_cutout") {
        view = fitBox(lower48, fractionRect(options.width, options.height,
                                            0.05, 0.15, 0.90, 0.78));
    } else if (id == "03_dark_radar") {
        view = fitBox(lower48, fractionRect(options.width, options.height,
                                            0.03, 0.07, 0.94, 0.84));
    } else if (id == "04_blueprint") {
        view = fitBox(lower48, fractionRect(options.width, options.height,
                                            0.05, 0.08, 0.90, 0.70));
    } else if (id == "05_connection_arc") {
        LonLatBox east;
        east.add(-90.4, 36.6);
        east.add(-73.8, 43.0);
        view = fitBox(east, fractionRect(options.width, options.height,
                                         0.03, 0.02, 0.94, 0.96));
    } else if (id == "06_floating_island") {
        view = fitBox(boxOf(*highlight), fractionRect(options.width, options.height,
                                                      0.22, 0.15, 0.38, 0.68));
    } else {
        throw std::runtime_error("unknown map style: " + id);
    }

    const Bounds clipBounds{-kCanvasPadding, -kCanvasPadding,
                            width + kCanvasPadding, height + kCanvasPadding};
    const Bounds canvasBounds{0.0, 0.0, width, height};
    const Rect canvasRect{0.0, 0.0, width, height};
    const double tolerance = std::max(0.1, options.simplify_tolerance);
    const bool needsCountries = id == "01_editorial_vox" || id == "05_connection_arc";
    const bool needsStates = id != "06_floating_island";
    const bool needsHighlight = id == "01_editorial_vox" || id == "02_paper_cutout" ||
                                id == "04_blueprint" || id == "06_floating_island";

    Json highlightPolygons = Json::array();
    if (needsHighlight) {
        highlightPolygons = featureJson(*highlight, view, tolerance, clipBounds)["polygons"];
    }

    const Json palette = paletteFor(style);
    const Point indianapolis = view.project(options.pin_latitude, options.pin_longitude);
    const Point washington = view.project(38.9072, -77.0369);

    Json labels = Json::array();
    Json accents = Json::array();
    Json frames = Json::array();
    Json pins = Json::array();
    Json hatch = Json::array();
    Json graticule = Json::array();
    Json arc = nullptr;
    Json leader = nullptr;
    Json radar = nullptr;
    Json shadow = nullptr;

    if (id == "01_editorial_vox") {
        const std::string micro = "UNITED STATES / EDITORIAL PLATE";
        const double microWidth = estimateTextWidth(micro, design.f(17), 3, "mono");
        labels.push_back(labelJson(micro, design.x(1820) - microWidth,
                                   design.y(1018), "mono", design.f(17), "#33414F",
                                   3, "left", 0.72));
        accents.push_back(segmentJson({design.x(1820) - microWidth, design.y(1002)},
                                      {design.x(1820), design.y(1002)},
                                      "#D94E34", design.f(3)));
        labels.push_back(labelJson(options.title, design.x(98), design.y(946),
                                   "serif_bold", design.f(104), "#1F2937"));
        accents.push_back(segmentJson({design.x(102), design.y(962)},
                                      {design.x(196), design.y(962)},
                                      "#D94E34", design.f(4)));
        labels.push_back(labelJson(options.subtitle, design.x(102), design.y(1002),
                                   "sans_regular", design.f(21), "#556270", 5,
                                   "left", 0.95));
        labels.push_back(labelJson(options.pin_label,
                                   indianapolis.x + design.x(28),
                                   indianapolis.y - design.y(12), "sans_semibold",
                                   design.f(32), "#1F2937", 0, "left", 1.0,
                                   "#F4F1EA", design.f(2)));
        pins.push_back(pinJson(indianapolis.x, indianapolis.y, options.pin_label,
                               indianapolis.x + design.x(28),
                               indianapolis.y - design.y(12), design.f(13),
                               "#111827", "#F4F1EA", "dot"));
    } else if (id == "02_paper_cutout") {
        labels.push_back(labelJson("INDIANA", design.x(100), design.y(148),
                                   "serif_bold", design.f(92), "#2A241C"));
        accents.push_back(segmentJson({design.x(104), design.y(168)},
                                      {design.x(190), design.y(168)},
                                      "#C05A3E", design.f(3)));
        labels.push_back(labelJson("39.77 N / 86.16 W", design.x(104), design.y(204),
                                   "mono", design.f(18), "#6B6255", 2, "left", 0.9));
        labels.push_back(labelJson(options.subtitle, design.x(104), design.y(232),
                                   "sans_regular", design.f(18), "#7A7164", 4,
                                   "left", 0.85));
        labels.push_back(labelJson(options.pin_label,
                                   indianapolis.x + design.x(26),
                                   indianapolis.y - design.y(12), "sans_semibold",
                                   design.f(31), "#2A241C", 0, "left", 1.0,
                                   "#F8F3E7", design.f(2)));
        pins.push_back(pinJson(indianapolis.x, indianapolis.y, options.pin_label,
                               indianapolis.x + design.x(26),
                               indianapolis.y - design.y(12), design.f(13),
                               "#2A241C", "#F8F3E7", "dot"));
        shadow = Json{{"offset", Json::array({design.x(18), design.y(26)})},
                      {"blur", design.f(26)},
                      {"color", "#2B241B"},
                      {"opacity", 0.32},
                      {"scale", 1.035}};
    } else if (id == "03_dark_radar") {
        labels.push_back(labelJson("UNITED STATES // SIGNAL GRID", design.x(92),
                                   design.y(130), "mono", design.f(20), "#8FA3BF",
                                   3));
        labels.push_back(labelJson("38.90 N / 77.03 W", design.x(92), design.y(158),
                                   "mono", design.f(16), "#5E738D", 2, "left", 0.9));
        labels.push_back(labelJson("WASHINGTON, D.C.", design.x(92), design.y(976),
                                   "mono", design.f(24), "#F1F5F9", 4));
        labels.push_back(labelJson("FEDERAL DISTRICT / PRIORITY 01", design.x(92),
                                   design.y(1006), "mono", design.f(16), "#6F849E",
                                   2, "left", 0.9));
        labels.push_back(labelJson("GRID REF 18S UJ 234 083", design.x(92),
                                   design.y(1032), "mono", design.f(14), "#475B72",
                                   2, "left", 0.8));
        accents.push_back(segmentJson({design.x(92), design.y(92)},
                                      {design.x(172), design.y(92)},
                                      "#38BDF8", design.f(3)));
        radar = Json{{"x", round2(washington.x)}, {"y", round2(washington.y)},
                     {"color", "#38BDF8"}, {"max_radius", design.f(174)}};
        graticule = graticuleSegments(view, 5.0, true, canvasRect);
    } else if (id == "04_blueprint") {
        const int inset = static_cast<int>(std::lround(design.f(46)));
        frames.push_back(frameJson(inset, inset, width - inset * 2.0,
                                   height - inset * 2.0, "", "#3D6C93",
                                   std::max(1.0, design.f(1))));
        const Rect panel{width * 0.04, height * 0.745, width * 0.33, height * 0.205};
        frames.push_back(frameJson(panel.x, panel.y, panel.width, panel.height,
                                   "#0A2136", "#3D6C93", 1.0));
        const double textX = panel.x + width * 0.016;
        labels.push_back(labelJson("INDIANA", textX, panel.y + design.y(62),
                                   "sans_semibold", design.f(56), "#E8F3FC", 9));
        accents.push_back(segmentJson({textX, panel.y + design.y(80)},
                                      {panel.x + panel.width - design.x(26),
                                       panel.y + design.y(80)},
                                      "#3D6C93", 1.0));
        labels.push_back(labelJson("39.7684 N / 86.1581 W", textX,
                                   panel.y + design.y(112), "mono", design.f(18),
                                   "#A8C8E8", 1, "left", 0.95));
        labels.push_back(labelJson("ADMINISTRATIVE PLATE / 1:2,000,000", textX,
                                   panel.y + design.y(140), "mono", design.f(15),
                                   "#7FA5C6", 1, "left", 0.9));
        accents.push_back(segmentJson({textX, panel.y + design.y(176)},
                                      {textX + design.x(190), panel.y + design.y(176)},
                                      "#A8C8E8", design.f(2)));
        for (int tick = 0; tick <= 4; ++tick) {
            const double x = textX + tick * design.x(47);
            accents.push_back(segmentJson({x, panel.y + design.y(170)},
                                          {x, panel.y + design.y(184)},
                                          "#A8C8E8", 1.0));
        }
        labels.push_back(labelJson("0", textX, panel.y + design.y(200), "mono",
                                   design.f(13), "#7FA5C6", 0, "left", 0.9));
        labels.push_back(labelJson("100 KM", textX + design.x(146),
                                   panel.y + design.y(200), "mono", design.f(13),
                                   "#7FA5C6", 0, "left", 0.9));
        labels.push_back(labelJson("INDIANAPOLIS",
                                   indianapolis.x + design.x(30),
                                   indianapolis.y - design.y(14), "mono",
                                   design.f(17), "#E3C68E", 2));
        pins.push_back(pinJson(indianapolis.x, indianapolis.y, "INDIANAPOLIS",
                               indianapolis.x + design.x(30),
                               indianapolis.y - design.y(14), design.f(12),
                               "#E3C68E", "#E3C68E", "crosshair"));
        hatch = hatchSegments(highlightPolygons, std::max(6.0, design.f(7.0)),
                              canvasRect);
        graticule = graticuleSegments(view, 3.0, false, canvasRect);
    } else if (id == "05_connection_arc") {
        labels.push_back(labelJson("WASHINGTON - INDIANAPOLIS", design.x(98),
                                   design.y(112), "serif_bold", design.f(46),
                                   "#1F2937"));
        labels.push_back(labelJson("CONNECTION ARC / APPROX. 800 KM", design.x(100),
                                   design.y(146), "mono", design.f(17), "#5B6470",
                                   2, "left", 0.9));
        const double dcWidth = estimateTextWidth("WASHINGTON, D.C.", design.f(27), 0,
                                                 "sans_semibold");
        labels.push_back(labelJson("WASHINGTON, D.C.",
                                   washington.x - design.x(24) - dcWidth,
                                   washington.y + design.y(42), "sans_semibold",
                                   design.f(27), "#1F2937", 0, "left", 1.0,
                                   "#F8F3E7", design.f(2)));
        const std::string indyLabel = "INDIANAPOLIS";
        const double indyWidth = estimateTextWidth(indyLabel, design.f(27), 0,
                                                   "sans_semibold");
        labels.push_back(labelJson(indyLabel,
                                   indianapolis.x - design.x(24) - indyWidth,
                                   indianapolis.y - design.y(22), "sans_semibold",
                                   design.f(27), "#1F2937", 0, "left", 1.0,
                                   "#F8F3E7", design.f(2)));

        const double dx = indianapolis.x - washington.x;
        const double dy = indianapolis.y - washington.y;
        const double distance = std::hypot(dx, dy);
        const double lift = distance * 0.20;
        const Point c1{washington.x + dx * 0.33, washington.y + dy * 0.33 - lift};
        const Point c2{washington.x + dx * 0.66, washington.y + dy * 0.66 - lift};
        const Point midpoint{(washington.x + 3.0 * c1.x + 3.0 * c2.x + indianapolis.x) / 8.0,
                             (washington.y + 3.0 * c1.y + 3.0 * c2.y + indianapolis.y) / 8.0};
        labels.push_back(labelJson("800 KM", midpoint.x,
                                   midpoint.y - design.y(20), "mono", design.f(19),
                                   "#5B6470", 2, "center"));
        arc = Json{{"from", pointJson(washington)}, {"to", pointJson(indianapolis)},
                   {"control1", pointJson(c1)}, {"control2", pointJson(c2)},
                   {"distance_text", "800 KM"},
                   {"distance_x", round2(midpoint.x)},
                   {"distance_y", round2(midpoint.y - design.y(20))}};
        pins.push_back(pinJson(washington.x, washington.y, "WASHINGTON, D.C.",
                               washington.x - design.x(24) - dcWidth,
                               washington.y + design.y(42), design.f(12),
                               "#1F2937", "#F8F3E7", "dot"));
        pins.push_back(pinJson(indianapolis.x, indianapolis.y, "INDIANAPOLIS",
                               indianapolis.x - design.x(24) - indyWidth,
                               indianapolis.y - design.y(22), design.f(14),
                               "#D94E34", "#F8F3E7", "dot"));
    } else if (id == "06_floating_island") {
        const Point anchor = view.project(40.20, -84.82);
        const double targetY = design.y(150);
        const Point elbow{anchor.x + std::max(design.x(40), anchor.y - targetY),
                          targetY};
        const Point end{design.x(1285), targetY};
        leader = Json{{"points", Json::array({pointJson(anchor), pointJson(elbow),
                                              pointJson(end)})},
                      {"anchor_radius", design.f(4)}};
        const double cartoucheX = design.x(1315);
        const double cartoucheTop = design.y(160);
        accents.push_back(segmentJson({cartoucheX - design.x(22),
                                       cartoucheTop + design.y(54)},
                                      {cartoucheX - design.x(22),
                                       cartoucheTop + design.y(10)},
                                      "#D94E34", design.f(4)));
        labels.push_back(labelJson("INDIANA", cartoucheX,
                                   cartoucheTop + design.y(70), "serif_bold",
                                   design.f(74), "#1F2937"));
        accents.push_back(segmentJson({cartoucheX, cartoucheTop + design.y(88)},
                                      {cartoucheX + design.x(340),
                                       cartoucheTop + design.y(88)},
                                      "#B8B2A6", 1.0));
        labels.push_back(labelJson("39.77 N / 86.15 W", cartoucheX,
                                   cartoucheTop + design.y(120), "mono",
                                   design.f(19), "#5B6470", 2));
        labels.push_back(labelJson("THE HOOSIER STATE", cartoucheX,
                                   cartoucheTop + design.y(148), "sans_regular",
                                   design.f(17), "#6B7280", 4, "left", 0.9));
        shadow = Json{{"offset", Json::array({design.x(18), design.y(28)})},
                      {"blur", design.f(30)},
                      {"color", "#3B2A20"},
                      {"opacity", 0.28},
                      {"scale", 1.0}};
    }

    // Roles let the template pack own choreography without guessing from
    // label text or list order.
    auto assignRole = [&labels](std::size_t index, const char* role) {
        if (index < labels.size()) labels[index]["role"] = role;
    };
    if (id == "01_editorial_vox") {
        assignRole(0, "micro");
        assignRole(1, "title");
        assignRole(2, "subtitle");
        assignRole(3, "pin");
    } else if (id == "02_paper_cutout") {
        assignRole(0, "title");
        assignRole(1, "data");
        assignRole(2, "subtitle");
        assignRole(3, "pin");
    } else if (id == "03_dark_radar") {
        assignRole(0, "hud");
        assignRole(1, "data");
        assignRole(2, "hud");
        assignRole(3, "data");
        assignRole(4, "data");
    } else if (id == "04_blueprint") {
        assignRole(0, "title");
        assignRole(1, "data");
        assignRole(2, "data");
        assignRole(3, "data");
        assignRole(4, "data");
        assignRole(5, "pin");
    } else if (id == "05_connection_arc") {
        assignRole(0, "title");
        assignRole(1, "subtitle");
        assignRole(2, "pin");
        assignRole(3, "pin");
        assignRole(4, "data");
    } else if (id == "06_floating_island") {
        assignRole(0, "title");
        assignRole(1, "data");
        assignRole(2, "subtitle");
    }

    Json scene;
    scene["schema"] = editorial_map_scene_schema_id();
    scene["style"] = shortStyleId(style);
    scene["style_id"] = editorial_map_style_id(style);
    scene["canvas"] = Json::array({options.width, options.height});
    scene["fps"] = options.fps;
    scene["frame_count"] = options.frame_count;
    scene["title"] = options.title;
    scene["subtitle"] = options.subtitle;
    scene["palette"] = palette;
    // Keep only contiguous states in every continental composition.
    Json stateFeatures = Json::array();
    if (needsStates) {
        for (const GeoFeature& state : states.features) {
            if (!isContiguousState(state)) continue;
            stateFeatures.push_back(featureJson(state, view, tolerance * 1.2,
                                                clipBounds));
        }
    }
    scene["states"] = std::move(stateFeatures);
    Json countryFeatures = Json::array();
    if (needsCountries) {
        for (const GeoFeature& country : countries.features) {
            const auto admin = country.properties.find("ADMIN");
            if (admin != country.properties.end() &&
                admin->second == "United States of America")
                continue;
            countryFeatures.push_back(featureJson(country, view, tolerance * 1.2,
                                                  clipBounds));
        }
    }
    scene["countries"] = std::move(countryFeatures);
    scene["highlight"] = Json{{"polygons", std::move(highlightPolygons)}};
    scene["hatch_segments"] = std::move(hatch);
    scene["graticule_segments"] = std::move(graticule);
    scene["accent_segments"] = std::move(accents);
    scene["frames"] = std::move(frames);
    scene["pins"] = std::move(pins);
    scene["labels"] = std::move(labels);
    scene["arc"] = std::move(arc);
    scene["leader"] = std::move(leader);
    scene["radar"] = std::move(radar);
    scene["shadow"] = std::move(shadow);
    return scene;
}

}  // namespace

const char* editorial_map_scene_schema_id() noexcept {
    return "chrononplugin.editorial-map-scene.v1";
}

EditorialMapSceneResult build_editorial_map_scene(EditorialMapStyle style,
                                                  const EditorialMapSceneOptions& options) {
    EditorialMapSceneResult result;
    auto fail = [&result](std::string message) {
        result.ok = false;
        result.error = std::move(message);
        return result;
    };

    if (options.width < 64 || options.width > 8192 || options.height < 64 ||
        options.height > 8192)
        return fail("canvas dimensions must be 64..8192 pixels");
    if (options.fps < 1 || options.fps > 240) return fail("fps must be in 1..240");
    if (options.frame_count < 2) return fail("frame_count must be at least 2");
    if (!std::isfinite(options.simplify_tolerance) ||
        options.simplify_tolerance < 0.1 || options.simplify_tolerance > 8.0)
        return fail("simplify_tolerance must be in [0.1,8]");

    try {
        const fs::path statesPath = resolveCatalogFile(
            options.states_geojson, "CHRONON_MAPS_CATALOG_DIR",
            "us_states_editorial.geojson", CHRONON_MAPS_EDITORIAL_STATES_DEFAULT);
        const fs::path countriesPath = resolveCatalogFile(
            options.countries_geojson, "CHRONON_MAPS_CATALOG_DIR",
            "ne_50m_admin_0_countries.geojson", CHRONON_MAPS_COUNTRIES_DEFAULT);

        const GeoFeatureCollection states = loadGeoJson(statesPath);
        GeoFeatureCollection countries;
        try {
            countries = loadGeoJson(countriesPath);
        } catch (const std::exception&) {
            // A continental treatment can run without country context, exactly
            // like the raster pack.
        }

        const GeoFeature* highlight = findFeature(states, "postal", options.highlight_state);
        if (highlight == nullptr) highlight = findFeature(states, "name", options.highlight_state);
        if (highlight == nullptr)
            return fail("highlight state not found in the state catalog: " +
                        options.highlight_state);

        Json scene = buildScene(style, options, states, countries, highlight);
        result.json = scene.dump(2);
        result.ok = true;
        return result;
    } catch (const std::exception& error) {
        return fail(error.what());
    }
}

}  // namespace chrononplugin::maps
