// Projected geometry, clipping and canvas segments of the editorial scene
// bridge. Split out of editorial_map_scene.cpp.

#include "editorial_map_scene_internal.hpp"

#include "clipper2/clipper.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace chrononplugin::maps::editorial_map_scene_detail {

namespace {

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

}  // namespace

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
}  // namespace chrononplugin::maps::editorial_map_scene_detail
