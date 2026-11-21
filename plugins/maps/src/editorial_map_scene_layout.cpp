// Layout/label JSON builders and catalog loading for the editorial scene.
// Split out of editorial_map_scene.cpp.

#include "editorial_map_scene_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace chrononplugin::maps::editorial_map_scene_detail {

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
                             const std::string& color, int tracking,
                             const std::string& align, double opacity,
                             const std::string& stroke,
                             double strokeWidth) {
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
}  // namespace chrononplugin::maps::editorial_map_scene_detail
