#pragma once

#include <opencv2/core.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>

namespace chrononplugin::maps {

/// Runtime Esri World Imagery tile pyramid with a persistent on-disk cache.
/// Tiles are decoded and continuously resampled through OpenCV.
class EsriSatellitePyramid {
public:
    explicit EsriSatellitePyramid(std::string cache_directory = {});
    [[nodiscard]] cv::Mat sample(double lat, double lon, double zoom,
                                 int width, int height);
    [[nodiscard]] std::int64_t network_fetches() const noexcept { return network_fetches_; }
    [[nodiscard]] std::int64_t bytes_downloaded() const noexcept { return bytes_downloaded_; }
    [[nodiscard]] std::int64_t disk_hits() const noexcept { return disk_hits_; }
    [[nodiscard]] std::int64_t plate_count() const noexcept { return plate_count_; }
    [[nodiscard]] std::int64_t plate_bytes() const noexcept { return plate_bytes_; }

private:
    [[nodiscard]] cv::Mat tile(int z, std::int64_t x, std::int64_t y);
    [[nodiscard]] cv::Mat plate(double lat, double lon, int z, int width, int height);
    std::string cache_directory_;
    std::unordered_map<std::string, cv::Mat> memory_cache_;
    std::int64_t network_fetches_ = 0;
    std::int64_t bytes_downloaded_ = 0;
    std::int64_t disk_hits_ = 0;
    std::int64_t plate_count_ = 0;
    std::int64_t plate_bytes_ = 0;
};

}  // namespace chrononplugin::maps
