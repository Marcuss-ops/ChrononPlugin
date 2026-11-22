// Standalone test for the native vector tile/plate rasterizer (phase 2 of
// docs/tickets/TICKET-MAPS-NATIVE-PORT.md). It pins the properties that do not
// need the Python reference: tile geometry, sea outside the pyramid, x wrap,
// determinism, and plate stitching being identical to the tile it covers.
// Pixel parity against the Python renderer is a separate, still-open gate.
#include "chrononplugin/maps/map_plate_raster.hpp"

#include <opencv2/core.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <unistd.h>

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

    const auto parses_before = VectorPlateRaster::geojson_parse_count();
    VectorPlateRaster raster(VectorMapSources{kCountries, kItalyAdmin2});
    // The two boundary files are parsed once for the process, not once per
    // raster/job. Every later instance must be served by the file cache.
    CHECK(VectorPlateRaster::geojson_parse_count() == parses_before + 2);

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
    CHECK(raster.tile_cache_misses() == 1);
    CHECK(raster.tile_cache_hits() == 0);
    const cv::Mat repeated = raster.compose_plate(left, top, 256, 256, 5);
    CHECK(same_pixels(repeated, tile));
    CHECK(raster.tile_cache_misses() == 1);
    CHECK(raster.tile_cache_hits() == 1);
    const cv::Mat wrapped = raster.compose_plate(left + 32.0 * 256.0, top, 256, 256, 5);
    CHECK(same_pixels(wrapped, tile));
    CHECK(raster.tile_cache_misses() == 1);
    CHECK(raster.tile_cache_hits() == 2);

    // A cold raster (no prior cache entry) composing the same window must be
    // pixel-identical: tile reuse may never change what the plate shows.
    {
        VectorPlateRaster cold(VectorMapSources{kCountries, kItalyAdmin2});
        const cv::Mat cold_plate = cold.compose_plate(left, top, 256, 256, 5);
        CHECK(same_pixels(cold_plate, plate));
        CHECK(cold.tile_cache_hits() == 0);
        CHECK(cold.tile_cache_misses() == 1);
    }

    // A multi-tile window at a fractional offset: the second composition is
    // served from the tile cache, adds no miss, and stays pixel-identical to a
    // cold raster's composition of the same window.
    {
        const double multi_left = left + 100.0;
        const double multi_top = top + 60.0;
        const cv::Mat multi = raster.compose_plate(multi_left, multi_top, 512, 512, 5);
        const std::int64_t misses_before = raster.tile_cache_misses();
        const std::int64_t hits_before = raster.tile_cache_hits();
        const cv::Mat multi_again = raster.compose_plate(multi_left, multi_top, 512, 512, 5);
        CHECK(same_pixels(multi, multi_again));
        CHECK(raster.tile_cache_misses() == misses_before);
        CHECK(raster.tile_cache_hits() > hits_before);
        VectorPlateRaster cold_multi_raster(VectorMapSources{kCountries, kItalyAdmin2});
        const cv::Mat cold_multi = cold_multi_raster.compose_plate(multi_left, multi_top, 512, 512, 5);
        CHECK(same_pixels(multi, cold_multi));
    }

    // The raster carries rings for the zoom it was asked for.
    CHECK(raster.ring_count(5) > 0);

    // ELIM-05 — persistent tile cache. A second render of the same area in a
    // SEPARATE raster instance (cold memory cache) must be served from disk,
    // pixel-identical, and a corrupt entry must fall back to rasterization.
    {
        namespace fs = std::filesystem;
        const fs::path dir =
            fs::temp_directory_path() /
            ("chronon-map-tile-cache-" + std::to_string(static_cast<long long>(getpid())));
        std::error_code ec;
        fs::remove_all(dir, ec);

        chrononplugin::maps::DiskCacheOptions disk;
        disk.dir = dir.string();
        disk.max_bytes = 8u * 1024u * 1024u;
        disk.evict_every = 1;

        const double cache_left = left + 7.0;
        const double cache_top = top + 9.0;
        cv::Mat written;
        std::int64_t writes = 0;
        double cold_ms = 0.0;
        double warm_ms = 0.0;
        {
            VectorPlateRaster producer(VectorMapSources{kCountries, kItalyAdmin2}, disk);
            const auto start = std::chrono::steady_clock::now();
            written = producer.compose_plate(cache_left, cache_top, 512, 512, 5);
            cold_ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - start)
                          .count();
            writes = producer.tile_disk_writes();
            CHECK(writes > 0);
            CHECK(producer.tile_disk_hits() == 0);
        }

        {
            VectorPlateRaster consumer(VectorMapSources{kCountries, kItalyAdmin2}, disk);
            const auto start = std::chrono::steady_clock::now();
            const cv::Mat reused = consumer.compose_plate(cache_left, cache_top, 512, 512, 5);
            warm_ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - start)
                          .count();
            CHECK(consumer.tile_disk_hits() == writes);
            CHECK(same_pixels(reused, written));
        }
        std::printf("map_plate_raster: persistent cache cold=%.1f ms warm=%.1f ms "
                    "(%lld tiles from disk)\n",
                    cold_ms, warm_ms, static_cast<long long>(writes));

        // Corrupt the first cached tile: it must be treated as a miss and the
        // recomputed plate must still be pixel-identical.
        std::string victim;
        for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".tile") {
                victim = entry.path().string();
                break;
            }
        }
        CHECK(!victim.empty());
        if (!victim.empty()) {
            std::FILE* f = std::fopen(victim.c_str(), "wb");
            CHECK(f != nullptr);
            if (f != nullptr) {
                const char garbage[64] = {1, 2, 3};
                std::fwrite(garbage, 1, sizeof(garbage), f);
                std::fclose(f);
            }
            VectorPlateRaster repaired(VectorMapSources{kCountries, kItalyAdmin2}, disk);
            const cv::Mat after_repair =
                repaired.compose_plate(cache_left, cache_top, 512, 512, 5);
            CHECK(same_pixels(after_repair, written));
            CHECK(repaired.tile_disk_writes() > 0);
        }

        // Byte bound: with a 400 KiB budget a 512x512 plate (4+ tiles) must
        // leave the directory at or under the budget after the sweep. A fresh
        // directory is used on purpose: the sweep is driven by writes, so a
        // cache that is only read cannot be re-bounded (documented behaviour).
        const fs::path bounded_dir = dir.string() + "-bounded";
        fs::remove_all(bounded_dir, ec);
        chrononplugin::maps::DiskCacheOptions bounded = disk;
        bounded.dir = bounded_dir.string();
        bounded.max_bytes = 400u * 1024u;
        {
            VectorPlateRaster bounded_raster(VectorMapSources{kCountries, kItalyAdmin2}, bounded);
            (void)bounded_raster.compose_plate(cache_left + 3.0, cache_top + 5.0, 512, 512, 5);
        }
        std::uint64_t total = 0;
        for (const auto& entry : fs::recursive_directory_iterator(bounded_dir, ec)) {
            if (entry.is_regular_file()) total += entry.file_size(ec);
        }
        CHECK(total <= bounded.max_bytes);

        fs::remove_all(dir, ec);
        fs::remove_all(bounded_dir, ec);
    }

    if (g_failures != 0) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("map_plate_raster: all checks passed\n");
    return 0;
}
