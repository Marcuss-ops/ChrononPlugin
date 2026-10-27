#include "chrononplugin/maps/esri_satellite.hpp"

#include "chrononplugin/maps/map_camera.hpp"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>
#include <unistd.h>

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

std::string tile_path(const std::string& cache, int z, std::int64_t x,
                      std::int64_t y) {
    return (std::filesystem::path(cache) / std::to_string(z) /
            std::to_string(x) / (std::to_string(y) + ".jpg")).string();
}

std::string tile_url(int z, std::int64_t x, std::int64_t y) {
    return "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/" +
           std::to_string(z) + "/" + std::to_string(y) + "/" + std::to_string(x);
}

std::string curl_config_value(const std::string& value) {
    std::string escaped = "\"";
    for (char c : value) {
        if (c == '\\' || c == '"') escaped += '\\';
        if (c == '\n' || c == '\r') throw std::invalid_argument("newline in curl config value");
        escaped += c;
    }
    return escaped + "\"";
}

int rounded(double value) { return static_cast<int>(std::nearbyint(value)); }

std::int64_t wrapped_x(std::int64_t x, int z) {
    const std::int64_t n = std::int64_t{1} << z;
    return (x % n + n) % n;
}

struct TileDownload {
    int z;
    std::int64_t x;
    std::int64_t y;
    std::string final_path;
    std::string temporary_path;
};
}  // namespace

EsriSatellitePyramid::EsriSatellitePyramid(std::string cache_directory) {
    if (cache_directory.empty()) {
        if (const char* override_dir = std::getenv("CHRONON_ESRI_TILE_CACHE_DIR");
            override_dir != nullptr && *override_dir != '\0') {
            cache_directory = override_dir;
        } else {
            const char* home = std::getenv("HOME");
            cache_directory = std::string(home ? home : ".") + "/.cache/chrononplugin/maps/esri_sat";
        }
    }
    cache_directory_ = std::move(cache_directory);
}

cv::Mat EsriSatellitePyramid::tile(int z, std::int64_t x, std::int64_t y) {
    const std::int64_t n = std::int64_t{1} << z;
    x = (x % n + n) % n;
    if (y < 0 || y >= n) return cv::Mat(256, 256, CV_8UC3, cv::Scalar(0, 0, 0));
    const std::string key = key_for(z, x, y);
    if (const auto found = memory_cache_.find(key); found != memory_cache_.end()) return found->second;
    const std::filesystem::path path(tile_path(cache_directory_, z, x, y));
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    cv::Mat image = cv::imread(path.string(), cv::IMREAD_COLOR);
    if (!image.empty() && image.cols == 256 && image.rows == 256) {
        ++disk_hits_;
        memory_cache_.emplace(key, image);
        return image;
    }
    const std::string url = tile_url(z, x, y);
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

void EsriSatellitePyramid::collect_plate_keys(
    std::set<std::tuple<int, std::int64_t, std::int64_t>>& keys,
    double lat, double lon, int z, int width, int height) const {
    if (z < 0 || z > 30 || width <= 0 || height <= 0)
        throw std::invalid_argument("invalid Esri tile viewport or zoom");
    const auto [gx, gy] = latlon_to_global_px(lat, lon, z);
    const double left = gx - width * 0.5, top = gy - height * 0.5;
    const auto tx0 = static_cast<std::int64_t>(std::floor(left / 256.0));
    const auto tx1 = static_cast<std::int64_t>(std::ceil((left + width) / 256.0));
    const auto ty0 = static_cast<std::int64_t>(std::floor(top / 256.0));
    const auto ty1 = static_cast<std::int64_t>(std::ceil((top + height) / 256.0));
    const std::int64_t n = std::int64_t{1} << z;
    for (auto ty = ty0; ty < ty1; ++ty) {
        if (ty < 0 || ty >= n) continue;
        for (auto tx = tx0; tx < tx1; ++tx)
            keys.emplace(z, wrapped_x(tx, z), ty);
    }
}

void EsriSatellitePyramid::prefetch_trajectory(
    double lat, double lon, const std::vector<double>& frame_zooms,
    int width, int height) {
    const auto started = std::chrono::steady_clock::now();
    std::set<std::tuple<int, std::int64_t, std::int64_t>> keys;
    for (const double zoom : frame_zooms) {
        const int lo = static_cast<int>(std::floor(zoom));
        const double t = zoom - lo;
        const double alpha = t * t * (3.0 - 2.0 * t);
        const double scale_lo = std::pow(2.0, t);
        collect_plate_keys(keys, lat, lon, lo,
            rounded(width / scale_lo) + 4, rounded(height / scale_lo) + 4);
        if (alpha > 0.001) {
            const double scale_hi = std::pow(2.0, t - 1.0);
            collect_plate_keys(keys, lat, lon, lo + 1,
                rounded(width / scale_hi) + 4, rounded(height / scale_hi) + 4);
        }
    }

    std::vector<TileDownload> missing;
    missing.reserve(keys.size());
    const std::string process_id = std::to_string(static_cast<long long>(::getpid()));
    for (const auto& [z, x, y] : keys) {
        const std::string key = key_for(z, x, y);
        if (memory_cache_.find(key) != memory_cache_.end()) continue;
        const std::string final_path = tile_path(cache_directory_, z, x, y);
        const bool exists = std::filesystem::exists(final_path);
        cv::Mat cached = exists ? cv::imread(final_path, cv::IMREAD_COLOR) : cv::Mat();
        if (!cached.empty() && cached.cols == 256 && cached.rows == 256) {
            memory_cache_.emplace(key, std::move(cached));
            ++disk_hits_;
            continue;
        }
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(final_path).parent_path(), ec);
        const std::string temporary_path = final_path + ".prefetch." + process_id + ".part";
        std::filesystem::remove(temporary_path, ec);
        missing.push_back({z, x, y, final_path, temporary_path});
    }

    int curl_status = 0;
    if (!missing.empty()) {
        const std::string config_path = (std::filesystem::temp_directory_path() /
            ("chronon-esri-prefetch." + process_id + ".curlrc")).string();
        {
            std::ofstream config(config_path, std::ios::binary | std::ios::trunc);
            if (!config) throw std::runtime_error("cannot create Esri curl batch config");
            for (const auto& item : missing) {
                config << "output = " << curl_config_value(item.temporary_path) << '\n'
                       << "url = " << curl_config_value(tile_url(item.z, item.x, item.y)) << '\n';
            }
            if (!config) throw std::runtime_error("cannot write Esri curl batch config");
        }
        const std::string command = "/usr/bin/curl --parallel --parallel-max 32 --http2 --fail --silent --show-error --max-time 15 --retry 1 -A 'ChrononPlugin/1.0' --config " +
                                    quote(config_path) + " >/dev/null";
        curl_status = std::system(command.c_str());
        std::error_code ec;
        std::filesystem::remove(config_path, ec);
    }

    std::vector<std::string> failed;
    for (const auto& item : missing) {
        cv::Mat image = std::filesystem::exists(item.temporary_path)
            ? cv::imread(item.temporary_path, cv::IMREAD_COLOR) : cv::Mat();
        if (!image.empty() && image.cols == 256 && image.rows == 256) {
            std::error_code ec;
            std::filesystem::rename(item.temporary_path, item.final_path, ec);
            if (ec) {
                std::filesystem::remove(item.temporary_path, ec);
                failed.push_back(key_for(item.z, item.x, item.y));
                continue;
            }
            std::error_code size_error;
            const auto bytes = std::filesystem::file_size(item.final_path, size_error);
            if (!size_error) bytes_downloaded_ += static_cast<std::int64_t>(bytes);
            ++network_fetches_;
            memory_cache_.emplace(key_for(item.z, item.x, item.y), std::move(image));
        } else {
            std::error_code ec;
            std::filesystem::remove(item.temporary_path, ec);
            failed.push_back(key_for(item.z, item.x, item.y));
        }
    }
    prefetch_ms_ = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    if (curl_status != 0 || !failed.empty()) {
        throw std::runtime_error("Esri HTTP/2 tile prefetch failed for " +
            std::to_string(failed.size()) + " tile(s); curl status=" +
            std::to_string(curl_status));
    }
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
