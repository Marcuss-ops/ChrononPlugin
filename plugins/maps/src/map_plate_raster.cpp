// Chronon3D native vector map-plate rasterization — TICKET-MAPS-NATIVE-PORT
// fase 2. Pixel-parity with the Python renderer (see the header contract):
// every formula below mirrors ChrononTemplate/tools/map/vector_plate_pyramid.py
// (render_vector_tile / _global_px / _tile_latlon / _graticule_step_deg) and
// Chronon3d/tools/cartography/fast_plate_sampler.py (_compose_px) in the same
// order of operations. "round" is Python's round(): half-to-even.

#include "chrononplugin/maps/map_plate_raster.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

#include <nlohmann/json.hpp>
#include <opencv2/imgproc.hpp>
#include <cstdint>
#include <cstddef>

namespace chrononplugin::maps {

namespace {

constexpr int kTile = 256;
constexpr double kWebMercatorLatLimit = 85.05112878;

// BGR paper palette (values identical to vector_plate_pyramid.py).
const cv::Scalar kSea(245, 247, 247);
const cv::Scalar kLand(224, 226, 226);
const cv::Scalar kBorder(188, 190, 190);
const cv::Scalar kGrid(234, 236, 236);
const cv::Scalar kItalyAdminLine(170, 172, 172);

// (lon_min, lon_max, lat_min, lat_max) — ITALY_BBOX.
constexpr double kItalyLonMin = 6.5;
constexpr double kItalyLonMax = 18.5;
constexpr double kItalyLatMin = 35.0;
constexpr double kItalyLatMax = 47.5;

/// Python's modulo for tile x wrapping (result in [0, m)).
[[nodiscard]] std::int64_t py_mod(std::int64_t x, std::int64_t m) {
    const std::int64_t r = x % m;
    return r < 0 ? r + m : r;
}

[[nodiscard]] double clamp_lat(double lat) {
    return std::min(std::max(lat, -kWebMercatorLatLimit), kWebMercatorLatLimit);
}

/// math.radians(x) == x * (pi / 180.0) in CPython.
[[nodiscard]] double radians(double deg) { return deg * (M_PI / 180.0); }
/// math.degrees(x) == x * (180.0 / pi) in CPython.
[[nodiscard]] double degrees(double rad) { return rad * (180.0 / M_PI); }

/// Python round(v) — half-to-even — then int().
[[nodiscard]] int round_half_even(double v) {
    return static_cast<int>(std::nearbyint(v));
}

/// _global_px(lat, lon, zoom): global pixel coords at 256-px tiles.
[[nodiscard]] std::pair<double, double> global_px(double lat, double lon, int zoom) {
    const double scale = 256.0 * static_cast<double>(std::int64_t{1} << zoom);
    const double x = (lon + 180.0) / 360.0 * scale;
    const double lat_rad = radians(clamp_lat(lat));
    const double y =
        (1.0 - std::log(std::tan(lat_rad) + 1.0 / std::cos(lat_rad)) / M_PI) * 0.5 * scale;
    return {x, y};
}

/// _tile_latlon(z, x, y): (lon_min, lon_max, lat_min, lat_max).
struct TileBBox {
    double lon_min, lon_max, lat_min, lat_max;
};
[[nodiscard]] TileBBox tile_latlon(int z, std::int64_t x, std::int64_t y) {
    const double scale = 256.0 * static_cast<double>(std::int64_t{1} << z);
    TileBBox b{};
    b.lon_min = (static_cast<double>(x) * 256.0 / scale) * 360.0 - 180.0;
    b.lon_max = (static_cast<double>(x + 1) * 256.0 / scale) * 360.0 - 180.0;
    const double n_top = M_PI - 2.0 * M_PI * (static_cast<double>(y) * 256.0 / scale);
    const double n_bot = M_PI - 2.0 * M_PI * (static_cast<double>(y + 1) * 256.0 / scale);
    b.lat_max = degrees(std::atan(std::sinh(n_top)));
    b.lat_min = degrees(std::atan(std::sinh(n_bot)));
    return b;
}

/// _graticule_step_deg(zoom).
[[nodiscard]] double graticule_step_deg(int zoom) {
    if (zoom <= 6) return 10.0;
    if (zoom <= 8) return 5.0;
    if (zoom <= 10) return 2.0;
    return 1.0;
}

/// _polygons(): Polygon -> one polygon, MultiPolygon -> its polygons, else none.
/// Only the outer ring (polygon[0]) is drawn, and only with >= 3 positions.
void append_geometry_rings(const nlohmann::json& geometry,
                           std::vector<std::vector<std::pair<double, double>>>& out) {
    const std::string type = geometry.value("type", "");
    std::vector<const nlohmann::json*> polygons;
    if (type == "Polygon" || type == "MultiPolygon") {
        if (!geometry.contains("coordinates")) {
            throw std::runtime_error(type + " geometry without coordinates");
        }
        const auto& coords = geometry.at("coordinates");
        if (type == "Polygon") {
            polygons.push_back(&coords);
        } else {
            for (const auto& polygon : coords) {
                polygons.push_back(&polygon);
            }
        }
    }
    for (const nlohmann::json* polygon : polygons) {
        if (polygon->empty()) continue;
        const auto& ring = polygon->at(0);
        if (ring.size() < 3) continue;
        std::vector<std::pair<double, double>> raw;
        raw.reserve(ring.size());
        for (const auto& position : ring) {
            raw.emplace_back(position.at(0).get<double>(), position.at(1).get<double>());
        }
        out.push_back(std::move(raw));
    }
}

}  // namespace

std::vector<VectorPlateRaster::RawRing> VectorPlateRaster::parse_geojson(
    const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        throw std::runtime_error("cannot open GeoJSON source: " + path);
    }
    std::string data;
    char buffer[1 << 16];
    std::size_t got = 0;
    while ((got = std::fread(buffer, 1, sizeof(buffer), f)) > 0) {
        data.append(buffer, got);
    }
    std::fclose(f);

    nlohmann::json doc = nlohmann::json::parse(data);
    if (!doc.contains("features")) {
        throw std::runtime_error("GeoJSON has no features: " + path);
    }
    std::vector<RawRing> rings;
    for (const auto& feature : doc.at("features")) {
        // feature.get("geometry") or {}
        const auto it = feature.find("geometry");
        if (it == feature.end() || it->is_null()) continue;
        append_geometry_rings(*it, rings);
    }
    return rings;
}

VectorPlateRaster::VectorPlateRaster(VectorMapSources sources) {
    if (sources.countries_geojson.empty() || sources.italy_admin2_geojson.empty()) {
        throw std::runtime_error("both boundary sources are required");
    }
    countries_ = parse_geojson(sources.countries_geojson);
    admin2_ = parse_geojson(sources.italy_admin2_geojson);
    if (countries_.empty()) {
        throw std::runtime_error("countries source yielded no rings: " +
                                 sources.countries_geojson);
    }
}

const VectorPlateRaster::ZoomData& VectorPlateRaster::zoom_data(int z) const {
    auto it = zoom_cache_.find(z);
    if (it != zoom_cache_.end()) return it->second;

    ZoomData data;
    auto project = [z](const std::vector<RawRing>& raw, std::vector<Ring>& out) {
        out.reserve(raw.size());
        for (const RawRing& raw_ring : raw) {
            std::vector<cv::Point> pts;
            pts.reserve(raw_ring.size());
            double minx = 0, maxx = 0, miny = 0, maxy = 0;
            bool first = true;
            for (const auto& [lon, lat] : raw_ring) {
                const auto [gx, gy] = global_px(lat, lon, z);
                if (first) {
                    minx = maxx = gx;
                    miny = maxy = gy;
                    first = false;
                } else {
                    minx = std::min(minx, gx);
                    maxx = std::max(maxx, gx);
                    miny = std::min(miny, gy);
                    maxy = std::max(maxy, gy);
                }
                // np.round(pts).astype(np.int32)
                pts.emplace_back(round_half_even(gx), round_half_even(gy));
            }
            out.push_back(Ring{minx, maxx, miny, maxy, std::move(pts)});
        }
    };
    project(countries_, data.countries);
    project(admin2_, data.admin2);
    return zoom_cache_.emplace(z, std::move(data)).first->second;
}

std::size_t VectorPlateRaster::ring_count(int z) const {
    const ZoomData& data = zoom_data(z);
    return data.countries.size() + data.admin2.size();
}

cv::Mat VectorPlateRaster::render_tile(int z, std::int64_t x, std::int64_t y) const {
    cv::Mat img(kTile, kTile, CV_8UC3, kSea);
    const std::int64_t max_idx = std::int64_t{1} << z;
    if (y < 0 || y >= max_idx) {
        return img;
    }
    x = py_mod(x, max_idx);
    const TileBBox bbox = tile_latlon(z, x, y);
    const int ox = static_cast<int>(x * 256);
    const int oy = static_cast<int>(y * 256);

    // Graticule under the geography (same float walk as the Python renderer).
    const double step = graticule_step_deg(z);
    double lon = std::ceil(bbox.lon_min / step) * step;
    while (lon <= bbox.lon_max) {
        const double gx = global_px(0.0, lon, z).first;
        const int px = round_half_even(gx - static_cast<double>(x) * 256.0);
        if (px >= 0 && px < 256) {
            cv::line(img, {px, 0}, {px, 255}, kGrid, 1, cv::LINE_AA);
        }
        lon += step;
    }
    double lat = std::ceil(bbox.lat_min / step) * step;
    while (lat <= bbox.lat_max) {
        const double gy = global_px(lat, 0.0, z).second;
        const int py = round_half_even(gy - static_cast<double>(y) * 256.0);
        if (py >= 0 && py < 256) {
            cv::line(img, {0, py}, {255, py}, kGrid, 1, cv::LINE_AA);
        }
        lat += step;
    }

    const ZoomData& data = zoom_data(z);
    const double x0 = static_cast<double>(ox), x1 = static_cast<double>(ox + 256);
    const double y0 = static_cast<double>(oy), y1 = static_cast<double>(oy + 256);
    auto visible = [&](const Ring& ring) {
        return !(ring.maxx < x0 || ring.minx > x1 || ring.maxy < y0 || ring.miny > y1);
    };
    auto shift = [&](const Ring& ring) {
        std::vector<cv::Point> pts;
        pts.reserve(ring.pts.size());
        for (const cv::Point& p : ring.pts) {
            pts.emplace_back(p.x - ox, p.y - oy);
        }
        return pts;
    };

    // Country land fill, then borders — one ring per call with LINE_AA exactly
    // like cv2.fillPoly/cv2.polylines, seam anti-aliasing included.
    for (const Ring& ring : data.countries) {
        if (!visible(ring)) continue;
        const std::vector<std::vector<cv::Point>> poly{shift(ring)};
        cv::fillPoly(img, poly, kLand, cv::LINE_AA);
    }
    for (const Ring& ring : data.countries) {
        if (!visible(ring)) continue;
        const std::vector<std::vector<cv::Point>> poly{shift(ring)};
        cv::polylines(img, poly, true, kBorder, 1, cv::LINE_AA);
    }
    // Italian provinces where the tile meets Italy.
    if (!(kItalyLonMax < bbox.lon_min || kItalyLonMin > bbox.lon_max ||
          kItalyLatMax < bbox.lat_min || kItalyLatMin > bbox.lat_max)) {
        for (const Ring& ring : data.admin2) {
            if (!visible(ring)) continue;
            const std::vector<std::vector<cv::Point>> poly{shift(ring)};
            cv::polylines(img, poly, true, kItalyAdminLine, 1, cv::LINE_AA);
        }
    }
    return img;
}

cv::Mat VectorPlateRaster::compose_plate(double left, double top, int width, int height,
                                         int zoom) const {
    // FastPlateSampler._compose_px: stitch 256x256 tiles, never re-rasterize.
    cv::Mat plate(height, width, CV_8UC3, cv::Scalar(0, 0, 0));
    const std::int64_t max_idx = std::int64_t{1} << zoom;
    const int ty_begin = static_cast<int>(std::floor(top / 256.0));
    const int ty_end = static_cast<int>(std::ceil((top + height) / 256.0));
    for (int ty = ty_begin; ty < ty_end; ++ty) {
        if (ty < 0 || static_cast<std::int64_t>(ty) >= max_idx) continue;
        const double y0 = std::max(top, static_cast<double>(ty) * 256.0);
        const double y1 = std::min(top + height, static_cast<double>(ty + 1) * 256.0);
        if (y1 <= y0) continue;
        const int dy0 = round_half_even(y0 - top);
        const int dy1 = round_half_even(y1 - top);
        const int sy0 = round_half_even(y0 - static_cast<double>(ty) * 256.0);
        const int sy1 = round_half_even(y1 - static_cast<double>(ty) * 256.0);
        const int tx_begin = static_cast<int>(std::floor(left / 256.0));
        const int tx_end = static_cast<int>(std::ceil((left + width) / 256.0));
        for (int tx = tx_begin; tx < tx_end; ++tx) {
            const double x0 = std::max(left, static_cast<double>(tx) * 256.0);
            const double x1 = std::min(left + width, static_cast<double>(tx + 1) * 256.0);
            if (x1 <= x0) continue;
            const int dx0 = round_half_even(x0 - left);
            const int dx1 = round_half_even(x1 - left);
            const int sx0 = round_half_even(x0 - static_cast<double>(tx) * 256.0);
            const int sx1 = round_half_even(x1 - static_cast<double>(tx) * 256.0);
            const cv::Mat tile = render_tile(zoom, tx, ty);
            const int hs = std::min(dy1 - dy0, sy1 - sy0);
            const int ws = std::min(dx1 - dx0, sx1 - sx0);
            if (hs > 0 && ws > 0) {
                tile(cv::Rect(sx0, sy0, ws, hs)).copyTo(plate(cv::Rect(dx0, dy0, ws, hs)));
            }
        }
    }
    return plate;
}

}  // namespace chrononplugin::maps
