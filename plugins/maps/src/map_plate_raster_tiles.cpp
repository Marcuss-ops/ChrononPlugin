// Chronon3D native vector map-plate rasterization — TICKET-MAPS-NATIVE-PORT
// fase 2. Per-tile rasterization and plate stitching (render_vector_tile and
// FastPlateSampler._compose_px); the raster lifecycle and the persistent tile
// cache live in map_plate_raster.cpp.

#include "chrononplugin/maps/map_plate_raster.hpp"

#include "map_plate_raster_internal.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>
#include <vector>

#include <opencv2/imgproc.hpp>
#include <cstdint>
#include <cstddef>

namespace chrononplugin::maps {

using namespace raster_detail;
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
            const auto key = std::make_tuple(zoom, py_mod(tx, max_idx), static_cast<std::int64_t>(ty));
            auto found = tile_cache_.find(key);
            cv::Mat tile;
            if (found != tile_cache_.end()) {
                ++tile_cache_hits_;
                tile = found->second;
            } else {
                // ELIM-05: a tile already rasterized by an earlier render of the
                // same area is reused from the persistent cache instead of paying
                // the CPU rasterization again. The key is content-addressed, so a
                // changed source can never answer with another map's tile.
                tile = load_tile_from_disk(zoom, py_mod(tx, max_idx), ty);
                if (!tile.empty()) {
                    ++tile_cache_hits_;
                    ++tile_disk_hits_;
                } else {
                    ++tile_cache_misses_;
                    tile = render_tile(zoom, tx, ty);
                    store_tile_to_disk(zoom, py_mod(tx, max_idx), ty, tile);
                }
                // Bound memory to 1024 BGR tiles (~192 MiB), independent of video length.
                if (tile_cache_.size() >= 1024) tile_cache_.erase(tile_cache_.begin());
                tile_cache_.emplace(key, tile);
            }
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
