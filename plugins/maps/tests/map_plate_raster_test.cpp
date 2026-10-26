// Standalone test for the native vector tile/plate rasterizer (phase 2 of
// docs/tickets/TICKET-MAPS-NATIVE-PORT.md). It pins the properties that do not
// need the Python reference: tile geometry, sea outside the pyramid, x wrap,
// determinism, and plate stitching being identical to the tile it covers.
// Pixel parity against the Python renderer is a separate, still-open gate.
#include "chrononplugin/maps/map_plate_raster.hpp"

#include <opencv2/core.hpp>

#include <cstdio>
#include <string>

namespace {

int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

const std::string kCountries = CHRONON_MAPS_COUNTRIES_GEOJSON;
const std::string kItalyAdmin2 = CHRONON_MAPS_ITALY_ADMIN2_GEOJSON;

bool same_pixels(const cv::Mat& a, const cv::Mat& b) {
    if (a.size() != b.size() || a.type() != b.type()) return false;
    cv::Mat diff;
    cv::absdiff(a, b, diff);
    return cv::countNonZero(diff.reshape(1)) == 0;
}

}  // namespace

int main() {
    using chrononplugin::maps::VectorMapSources;
    using chrononplugin::maps::VectorPlateRaster;

    VectorPlateRaster raster(VectorMapSources{kCountries, kItalyAdmin2});

    // A tile is always 256x256 BGR, 8-bit.
    const cv::Mat tile = raster.render_tile(5, 17, 11);
    CHECK(tile.rows == 256 && tile.cols == 256);
    CHECK(tile.type() == CV_8UC3);

    // Rendering is a pure function of (z, x, y): two calls are identical.
    CHECK(same_pixels(tile, raster.render_tile(5, 17, 11)));

    // Outside the pyramid vertically the tile is the blank sea colour.
    const cv::Mat above = raster.render_tile(3, 2, -1);
    const cv::Mat below = raster.render_tile(3, 2, 8);
    const cv::Scalar sea(245, 247, 247);
    CHECK(cv::norm(above, cv::Mat(above.size(), above.type(), sea)) == 0.0);
    CHECK(cv::norm(below, cv::Mat(below.size(), below.type(), sea)) == 0.0);

    // x wraps like Python's modulo: x = -1 at z=3 equals x = 7.
    CHECK(same_pixels(raster.render_tile(3, -1, 2), raster.render_tile(3, 7, 2)));

    // A plate whose window is exactly one tile equals that tile.
    const double left = 17.0 * 256.0;
    const double top = 11.0 * 256.0;
    const cv::Mat plate = raster.compose_plate(left, top, 256, 256, 5);
    CHECK(plate.rows == 256 && plate.cols == 256);
    CHECK(same_pixels(plate, tile));

    // The raster carries rings for the zoom it was asked for.
    CHECK(raster.ring_count(5) > 0);

    if (g_failures != 0) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("map_plate_raster: all checks passed\n");
    return 0;
}
