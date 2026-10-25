#include "chrononplugin/maps/geo_boolean.hpp"

#include "clipper2/clipper.h"

#include <cmath>
#include <cstdint>

namespace chrononplugin::maps {
namespace {

// Degrees -> integer grid. 1e6 gives roughly 0.1 m resolution at the equator.
constexpr double kGeoScale = 1e6;

Clipper2Lib::Path64 to_path(const std::vector<GeoPosition>& ring) {
    Clipper2Lib::Path64 path;
    // The GeoJSON ring is closed (first == last); Clipper paths are implicitly closed.
    const std::size_t n = (ring.size() > 1 && ring.front() == ring.back()) ? ring.size() - 1
                                                                           : ring.size();
    path.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        path.emplace_back(static_cast<std::int64_t>(std::llround(ring[i][0] * kGeoScale)),
                          static_cast<std::int64_t>(std::llround(ring[i][1] * kGeoScale)));
    }
    return path;
}

GeoPosition to_position(const Clipper2Lib::Point64& p) {
    return {static_cast<double>(p.x) / kGeoScale, static_cast<double>(p.y) / kGeoScale};
}

std::vector<GeoPosition> to_ring(const Clipper2Lib::Path64& path) {
    std::vector<GeoPosition> ring;
    ring.reserve(path.size() + 1);
    for (const auto& p : path) {
        ring.push_back(to_position(p));
    }
    if (!ring.empty()) {
        ring.push_back(ring.front());  // GeoJSON-style closed ring
    }
    return ring;
}

void add_polygons(Clipper2Lib::Paths64& paths, const std::vector<GeoPolygon>& polygons) {
    for (const auto& polygon : polygons) {
        for (const auto& ring : polygon.rings) {
            if (auto path = to_path(ring); path.size() >= 3) {
                paths.push_back(std::move(path));
            }
        }
    }
}

// Walks the PolyTree: each non-hole node is an outer polygon whose hole
// children become rings; holes' children are outers of a new level.
void collect(const Clipper2Lib::PolyPath64& node, std::vector<GeoPolygon>& out) {
    for (std::size_t i = 0; i < node.Count(); ++i) {
        const auto* child = node.Child(i);
        if (child->IsHole()) {
            continue;  // handled when visiting its parent outer
        }
        GeoPolygon polygon;
        polygon.rings.push_back(to_ring(child->Polygon()));
        for (std::size_t j = 0; j < child->Count(); ++j) {
            const auto* hole = child->Child(j);
            if (!hole->IsHole()) {
                continue;
            }
            polygon.rings.push_back(to_ring(hole->Polygon()));
            collect(*hole, out);  // islands inside the hole
        }
        out.push_back(std::move(polygon));
    }
}

}  // namespace

std::vector<GeoPolygon> geo_boolean(GeoBooleanOp op,
                                    const std::vector<GeoPolygon>& subject,
                                    const std::vector<GeoPolygon>& clip) {
    Clipper2Lib::ClipType type = Clipper2Lib::ClipType::Union;
    switch (op) {
        case GeoBooleanOp::Union: type = Clipper2Lib::ClipType::Union; break;
        case GeoBooleanOp::Difference: type = Clipper2Lib::ClipType::Difference; break;
        case GeoBooleanOp::Intersection: type = Clipper2Lib::ClipType::Intersection; break;
        case GeoBooleanOp::Xor: type = Clipper2Lib::ClipType::Xor; break;
    }
    Clipper2Lib::Clipper64 clipper;
    Clipper2Lib::Paths64 subject_paths;
    Clipper2Lib::Paths64 clip_paths;
    add_polygons(subject_paths, subject);
    add_polygons(clip_paths, clip);
    clipper.AddSubject(subject_paths);
    clipper.AddClip(clip_paths);

    Clipper2Lib::PolyTree64 tree;
    clipper.Execute(type, Clipper2Lib::FillRule::EvenOdd, tree);

    std::vector<GeoPolygon> out;
    collect(tree, out);
    return out;
}

std::vector<GeoPolygon> geo_dissolve(const std::vector<GeoPolygon>& polygons) {
    std::vector<GeoPolygon> merged;
    for (const auto& polygon : polygons) {
        merged = geo_boolean(GeoBooleanOp::Union, merged, {polygon});
    }
    return merged;
}

}  // namespace chrononplugin::maps
