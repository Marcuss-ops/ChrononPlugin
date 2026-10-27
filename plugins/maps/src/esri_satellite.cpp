#include "chrononplugin/maps/esri_satellite.hpp"

#include "chrononplugin/maps/map_camera.hpp"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace chrononplugin::maps {
namespace {
std::string quote(const std::string& value) {
    std::string out = "'";
    for (char c : value) out += c == '\'' ? "'\\''" : std::string(1, c);
    return out + "'";
}

std::string key_for(int z, std::int64_t x, std::int64_t y) {
    return std::to_string(z) + "/" + std::to_string(x) + "/" + std::to_string(y);
}
}  // namespace

EsriSatellitePyramid::EsriSatellitePyramid(std::string cache_directory) {
    if (cache_directory.empty()) {
        const char* home = std::getenv("HOME");
        cache_directory = std::string(home ? home : ".") + "/.cache/chrononplugin/maps/esri_sat";
    }
    cache_directory_ = std::move(cache_directory);
}

cv::Mat EsriSatellitePyramid::tile(int z, std::int64_t x, std::int64_t y) {
    const std::int64_t n = std::int64_t{1} << z;
    x = (x % n + n) % n;
    if (y < 0 || y >= n) return cv::Mat(256, 256, CV_8UC3, cv::Scalar(0, 0, 0));
    const std::string key = key_for(z, x, y);
    if (const auto found = memory_cache_.find(key); found != memory_cache_.end()) return found->second;
    const std::filesystem::path path = std::filesystem::path(cache_directory_) /
        std::to_string(z) / std::to_string(x) / (std::to_string(y) + ".jpg");
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    cv::Mat image = cv::imread(path.string(), cv::IMREAD_COLOR);
    if (!image.empty() && image.cols == 256 && image.rows == 256) {
        ++disk_hits_;
        memory_cache_.emplace(key, image);
        return image;
    }
    const std::string url = "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/" +
                            std::to_string(z) + "/" + std::to_string(y) + "/" + std::to_string(x);
    const std::string temp = path.string() + ".part";
    const std::string cmd = "/usr/bin/curl -fsSL --max-time 12 --retry 1 -A 'ChrononPlugin/1.0' -o " +
                            quote(temp) + " " + quote(url) + " >/dev/null 2>&1";
    const int status = std::system(cmd.c_str());
    if (status == 0) {
        image = cv::imread(temp, cv::IMREAD_COLOR);
        if (!image.empty() && image.cols == 256 && image.rows == 256) {
            std::filesystem::rename(temp, path, ec);
            ++network_fetches_;
            std::error_code size_error;
            const auto bytes = std::filesystem::file_size(path, size_error);
            if (!size_error) bytes_downloaded_ += static_cast<std::int64_t>(bytes);
            memory_cache_.emplace(key, image);
            return image;
        }
    }
    std::filesystem::remove(temp, ec);
    throw std::runtime_error("Esri World Imagery tile fetch/decode failed: " + key);
}

cv::Mat EsriSatellitePyramid::plate(double lat, double lon, int z, int width, int height) {
    ++plate_count_;
    plate_bytes_ += static_cast<std::int64_t>(width) * height * 3;
    const auto [gx, gy] = latlon_to_global_px(lat, lon, z);
    const double left = gx - width * 0.5, top = gy - height * 0.5;
    cv::Mat out(height, width, CV_8UC3, cv::Scalar(0, 0, 0));
    const auto tx0 = static_cast<std::int64_t>(std::floor(left / 256.0));
    const auto tx1 = static_cast<std::int64_t>(std::ceil((left + width) / 256.0));
    const auto ty0 = static_cast<std::int64_t>(std::floor(top / 256.0));
    const auto ty1 = static_cast<std::int64_t>(std::ceil((top + height) / 256.0));
    for (auto ty = ty0; ty < ty1; ++ty) for (auto tx = tx0; tx < tx1; ++tx) {
        const double x0 = std::max(left, static_cast<double>(tx * 256));
        const double y0 = std::max(top, static_cast<double>(ty * 256));
        const double x1 = std::min(left + width, static_cast<double>((tx + 1) * 256));
        const double y1 = std::min(top + height, static_cast<double>((ty + 1) * 256));
        const int dx = static_cast<int>(std::nearbyint(x0 - left));
        const int dy = static_cast<int>(std::nearbyint(y0 - top));
        const int sx = static_cast<int>(std::nearbyint(x0 - tx * 256));
        const int sy = static_cast<int>(std::nearbyint(y0 - ty * 256));
        const int w = std::min(static_cast<int>(std::nearbyint(x1 - x0)), 256 - sx);
        const int h = std::min(static_cast<int>(std::nearbyint(y1 - y0)), 256 - sy);
        if (w > 0 && h > 0) tile(z, tx, ty)(cv::Rect(sx, sy, w, h)).copyTo(out(cv::Rect(dx, dy, w, h)));
    }
    return out;
}

cv::Mat EsriSatellitePyramid::sample(double lat, double lon, double zoom, int width, int height) {
    const int lo = static_cast<int>(std::floor(zoom));
    const double t = zoom - lo;
    const double alpha = t * t * (3.0 - 2.0 * t);
    const double scale_lo = std::pow(2.0, t);
    cv::Mat low_crop = plate(lat, lon, lo,
        static_cast<int>(std::nearbyint(width / scale_lo)) + 4,
        static_cast<int>(std::nearbyint(height / scale_lo)) + 4);
    cv::Mat low;
    cv::resize(low_crop, low, {width, height}, 0, 0, cv::INTER_LINEAR);
    if (alpha <= 0.001) return low;
    const double scale_hi = std::pow(2.0, t - 1.0);
    cv::Mat high_crop = plate(lat, lon, lo + 1,
        static_cast<int>(std::nearbyint(width / scale_hi)) + 4,
        static_cast<int>(std::nearbyint(height / scale_hi)) + 4);
    cv::Mat high, blended;
    cv::resize(high_crop, high, {width, height}, 0, 0, cv::INTER_LINEAR);
    cv::addWeighted(low, 1.0 - alpha, high, alpha, 0.0, blended);
    return blended;
}
}  // namespace chrononplugin::maps
