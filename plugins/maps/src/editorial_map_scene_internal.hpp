#pragma once

// Private to plugins/maps: the projected-geometry model and the JSON builders
// shared by the editorial scene split. editorial_map_scene.cpp is divided into
// geometry/filter, layout, styles and entry-point translation units; only these
// entities cross the split. This header is not part of the public contract.

#include "chrononplugin/maps/editorial_map_scene.hpp"

#include "chrononplugin/maps/geojson.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

namespace chrononplugin::maps::editorial_map_scene_detail {

namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;

inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kClipScale = 1000.0;
inline constexpr double kCanvasPadding = 48.0;

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

// ── Projected geometry and canvas clipping ─────────────────────────────────

[[nodiscard]] View fitBox(const LonLatBox& box, const Rect& rect);

[[nodiscard]] Rect fractionRect(int width, int height, double x, double y,
                                double w, double h);

[[nodiscard]] double round2(double value);
[[nodiscard]] Json pointJson(const Point& point);
[[nodiscard]] Json ringJson(const Ring& ring);
[[nodiscard]] Json emptyArray();
[[nodiscard]] double ringArea(const Ring& ring);

[[nodiscard]] std::vector<Ring> projectRing(const std::vector<GeoPosition>& ring,
                                            const View& view, double tolerance,
                                            const Bounds& clipBounds);

[[nodiscard]] Json featureJson(const GeoFeature& feature, const View& view,
                               double tolerance, const Bounds& clipBounds);

[[nodiscard]] Json featuresJson(const GeoFeatureCollection& collection,
                                const View& view, double tolerance,
                                const Bounds& clipBounds);

[[nodiscard]] Bounds boundsOf(const Json& polygons);

[[nodiscard]] Json hatchSegments(const Json& highlightPolygons, double spacing,
                                 const Rect& clipRect);

[[nodiscard]] Json graticuleSegments(const View& view, double step, bool crosses,
                                     const Rect& clipRect);

// ── Layout, labels and catalog loading ─────────────────────────────────────

[[nodiscard]] double estimateTextWidth(const std::string& text, double size,
                                       int tracking, const std::string& font);

[[nodiscard]] Json labelJson(const std::string& text, double x, double y,
                             const std::string& font, double size,
                             const std::string& color, int tracking = 0,
                             const std::string& align = "left",
                             double opacity = 1.0, const std::string& stroke = "",
                             double strokeWidth = 0.0);

[[nodiscard]] Json pinJson(double x, double y, const std::string& label,
                           double labelX, double labelY, double radius,
                           const std::string& fill, const std::string& ring,
                           const std::string& style);

[[nodiscard]] Json segmentJson(const Point& a, const Point& b,
                               const std::string& color, double width);

[[nodiscard]] Json frameJson(double x, double y, double width, double height,
                             const std::string& fill, const std::string& stroke,
                             double strokeWidth);

[[nodiscard]] fs::path resolveCatalogFile(const std::string& requested,
                                          const char* envName,
                                          const char* filename,
                                          const char* fallback);

[[nodiscard]] GeoFeatureCollection loadGeoJson(const fs::path& path);

[[nodiscard]] const GeoFeature* findFeature(const GeoFeatureCollection& collection,
                                            const std::string& property,
                                            const std::string& value);

[[nodiscard]] bool isContiguousState(const GeoFeature& feature);

[[nodiscard]] LonLatBox boxOf(const GeoFeature& feature);

[[nodiscard]] std::string shortStyleId(EditorialMapStyle style);

// ── Styles ─────────────────────────────────────────────────────────────────

[[nodiscard]] Json paletteFor(EditorialMapStyle style);

[[nodiscard]] Json buildScene(EditorialMapStyle style,
                              const EditorialMapSceneOptions& options,
                              const GeoFeatureCollection& states,
                              const GeoFeatureCollection& countries,
                              const GeoFeature* highlight);

}  // namespace chrononplugin::maps::editorial_map_scene_detail
