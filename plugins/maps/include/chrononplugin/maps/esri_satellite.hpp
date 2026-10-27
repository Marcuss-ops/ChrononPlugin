#pragma once

#include <opencv2/core.hpp>

#include <cstdint>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>

namespace chrononplugin::maps {

/// Runtime Esri World Imagery tile pyramid with a persistent on-disk cache.
/// Tiles are decoded and continuously resampled through OpenCV.
class EsriSatellitePyramid {
public:
    explicit EsriSatellitePyramid(std::string cache_directory = {});
    [[nodiscard]] cv::Mat sample(double lat, double lon, double zoom,
                                 int width, int height);
    /// Collect every tile sampled by the deterministic camera path and fetch
    /// cache misses concurrently before video encoding starts (HTTP/2 when
    /// the Esri edge negotiates it, otherwise curl's HTTP/1.1 parallel mode).
    void prefetch_trajectory(double lat, double lon, double start_zoom,
                             double end_zoom, int frame_count,
                             int width, int height);
    [[nodiscard]] std::int64_t network_fetches() const noexcept { return network_fetches_; }
    [[nodiscard]] std::int64_t bytes_downloaded() const noexcept { return bytes_downloaded_; }
    [[nodiscard]] std::int64_t disk_hits() const noexcept { return disk_hits_; }
    [[nodiscard]] std::int64_t plate_count() const noexcept { return plate_count_; }
    [[nodiscard]] std::int64_t plate_bytes() const noexcept { return plate_bytes_; }
    [[nodiscard]] double prefetch_ms() const noexcept { return prefetch_ms_; }

private:
    [[nodiscard]] cv::Mat tile(int z, std::int64_t x, std::int64_t y);
    [[nodiscard]] cv::Mat plate(double lat, double lon, int z, int width, int height);
    void collect_plate_keys(std::set<std::tuple<int, std::int64_t, std::int64_t>>& keys,
                            double lat, double lon, int z, int width, int height) const;
    std::string cache_directory_;
    std::unordered_map<std::string, cv::Mat> memory_cache_;
    std::int64_t network_fetches_ = 0;
    std::int64_t bytes_downloaded_ = 0;
    std::int64_t disk_hits_ = 0;
    std::int64_t plate_count_ = 0;
    std::int64_t plate_bytes_ = 0;
    double prefetch_ms_ = 0.0;
};

}  // namespace chrononplugin::maps
