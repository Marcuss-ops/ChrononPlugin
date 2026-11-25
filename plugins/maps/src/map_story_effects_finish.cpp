// Documentary finish: vignette, grain and radial motion blur. Split out of
// map_story_effects.cpp.
//
// Vignette + grain used to round-trip the whole frame through CV_32FC3 and
// CV_16SC3 arrays. The fast path below keeps that look but applies the same
// gain and grain in one 8-bit pass: the vignette mask is cached as a 8.8
// fixed-point gain and grain comes from a cached signed-16 texture, translated
// per frame. Frames too large for the bounded texture cache use the reference
// path unchanged.

#include "chrononplugin/maps/map_story_effects.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <utility>

namespace chrononplugin::maps {

namespace {

constexpr std::size_t kMaxFastFinishPixels =
    static_cast<std::size_t>(1920) * 1080 * 2;

using FinishKey = std::array<int, 3>;

FinishKey finish_key(const cv::Mat& frame, double strength) {
    return {frame.cols, frame.rows,
            static_cast<int>(std::lround(strength * 1'000'000.0))};
}

cv::Mat& vignette_gain(const cv::Mat& frame, double strength) {
    static std::map<FinishKey, cv::Mat> cache;
    const FinishKey key = finish_key(frame, strength);
    auto found = cache.find(key);
    if (found != cache.end()) return found->second;
    if (cache.size() >= 8) cache.clear();

    cv::Mat gain(frame.rows, frame.cols, CV_16UC1);
    const double radius_x = std::max(1.0, frame.cols * 0.72);
    const double radius_y = std::max(1.0, frame.rows * 0.72);
    for (int y = 0; y < frame.rows; ++y) {
        std::uint16_t* row = gain.ptr<std::uint16_t>(y);
        const double dy = (y - (frame.rows - 1) * 0.5) / radius_y;
        for (int x = 0; x < frame.cols; ++x) {
            const double dx = (x - (frame.cols - 1) * 0.5) / radius_x;
            const double radial = std::clamp(dx * dx + dy * dy, 0.0, 1.0);
            // The reference conversion is algebraically 1 - strength * r.
            // Store it in 8.8 fixed point so the 8-bit pass rounds exactly
            // like the float path did before adding grain.
            const int value = static_cast<int>(
                std::lround((1.0 - strength * radial) * 256.0));
            row[x] = static_cast<std::uint16_t>(std::clamp(value, 0, 256));
        }
    }
    return cache.emplace(key, std::move(gain)).first->second;
}

cv::Mat& grain_texture(const cv::Mat& frame, double strength) {
    static std::map<FinishKey, cv::Mat> cache;
    const FinishKey key = finish_key(frame, strength);
    auto found = cache.find(key);
    if (found != cache.end()) return found->second;
    if (cache.size() >= 4) cache.clear();

    // A translated texture gives independent-looking film grain without
    // generating millions of normal samples on every frame. The padding is
    // capped so a 4K fallback never allocates an unexpectedly large bank.
    const int pad = std::clamp(std::min(frame.cols, frame.rows) / 4, 16, 256);
    cv::Mat texture(frame.rows + pad, frame.cols + pad, CV_16SC3);
    const std::uint64_t seed =
        0x5EED1234ULL ^ (static_cast<std::uint64_t>(key[2]) * 0x9E3779B9ULL);
    cv::RNG rng(seed);
    rng.fill(texture, cv::RNG::NORMAL, 0.0, std::max(1e-9, strength * 255.0));
    return cache.emplace(key, std::move(texture)).first->second;
}

cv::Mat grain_view(const cv::Mat& frame, const cv::Mat& texture,
                   int frame_index) {
    const int max_x = texture.cols - frame.cols;
    const int max_y = texture.rows - frame.rows;
    const std::uint32_t hash =
        0x9E3779B9U * static_cast<std::uint32_t>(frame_index + 1) ^
        0x85EBCA6BU;
    const int x = max_x > 0 ? static_cast<int>(hash % (max_x + 1)) : 0;
    const int y = max_y > 0
        ? static_cast<int>((hash >> 16) % static_cast<std::uint32_t>(max_y + 1))
        : 0;
    return texture(cv::Rect(x, y, frame.cols, frame.rows));
}

void apply_documentary_finish_reference(cv::Mat& frame, int frame_index,
                                        double vignette_strength,
                                        double grain_strength) {
    vignette_strength = std::clamp(vignette_strength, 0.0, 0.8);
    grain_strength = std::clamp(grain_strength, 0.0, 0.12);
    static std::map<std::pair<int, int>, cv::Mat> masks;
    const auto key = std::pair{frame.cols, frame.rows};
    auto found = masks.find(key);
    if (found == masks.end()) {
        cv::Mat mask(frame.size(), CV_32FC3);
        const double rx = std::max(1.0, frame.cols * 0.72);
        const double ry = std::max(1.0, frame.rows * 0.72);
        for (int y = 0; y < frame.rows; ++y) {
            cv::Vec3f* row = mask.ptr<cv::Vec3f>(y);
            for (int x = 0; x < frame.cols; ++x) {
                const double dx = (x - (frame.cols - 1) * 0.5) / rx;
                const double dy = (y - (frame.rows - 1) * 0.5) / ry;
                const double radial = std::clamp(dx * dx + dy * dy, 0.0, 1.0);
                const float value = static_cast<float>(1.0 - 0.42 * radial);
                row[x] = cv::Vec3f(value, value, value);
            }
        }
        found = masks.emplace(key, std::move(mask)).first;
    }
    if (vignette_strength > 0.0) {
        cv::Mat mask;
        found->second.convertTo(mask, CV_32FC3, vignette_strength / 0.42,
                                1.0 - vignette_strength / 0.42);
        cv::Mat float_frame;
        frame.convertTo(float_frame, CV_32FC3);
        cv::multiply(float_frame, mask, float_frame);
        float_frame.convertTo(frame, CV_8UC3);
    }
    if (grain_strength > 0.0) {
        cv::RNG rng(0x5EED1234U + static_cast<std::uint64_t>(frame_index));
        cv::Mat noise(frame.size(), CV_16SC3);
        rng.fill(noise, cv::RNG::NORMAL, 0.0, grain_strength * 255.0);
        cv::Mat wide, result;
        frame.convertTo(wide, CV_16SC3);
        cv::add(wide, noise, wide);
        wide.convertTo(result, CV_8UC3);
        result.copyTo(frame);
    }
}

}  // namespace

void apply_documentary_finish(cv::Mat& frame, int frame_index,
                              double vignette_strength, double grain_strength) {
    vignette_strength = std::clamp(vignette_strength, 0.0, 0.8);
    grain_strength = std::clamp(grain_strength, 0.0, 0.12);
    if (vignette_strength <= 0.0 && grain_strength <= 0.0) return;
    if (frame.empty() || frame.type() != CV_8UC3 ||
        static_cast<std::size_t>(frame.total()) > kMaxFastFinishPixels) {
        apply_documentary_finish_reference(frame, frame_index,
                                           vignette_strength, grain_strength);
        return;
    }

    const bool has_vignette = vignette_strength > 0.0;
    const bool has_grain = grain_strength > 0.0;
    const cv::Mat* gain =
        has_vignette ? &vignette_gain(frame, vignette_strength) : nullptr;
    cv::Mat noise;
    if (has_grain) {
        const cv::Mat& texture = grain_texture(frame, grain_strength);
        noise = grain_view(frame, texture, frame_index);
    }

    if (has_vignette && has_grain) {
        for (int y = 0; y < frame.rows; ++y) {
            uchar* pixel = frame.ptr<uchar>(y);
            const std::uint16_t* gain_row =
                gain->ptr<std::uint16_t>(y);
            const std::int16_t* noise_row =
                noise.ptr<std::int16_t>(y);
            for (int x = 0; x < frame.cols; ++x) {
                const int base = x * 3;
                const int g = gain_row[x];
                const int b = (static_cast<int>(pixel[base]) * g + 128) >> 8;
                const int green =
                    (static_cast<int>(pixel[base + 1]) * g + 128) >> 8;
                const int r =
                    (static_cast<int>(pixel[base + 2]) * g + 128) >> 8;
                const int n = noise_row[base];
                pixel[base] = cv::saturate_cast<uchar>(b + n);
                pixel[base + 1] = cv::saturate_cast<uchar>(
                    green + noise_row[base + 1]);
                pixel[base + 2] = cv::saturate_cast<uchar>(
                    r + noise_row[base + 2]);
            }
        }
    } else if (has_vignette) {
        for (int y = 0; y < frame.rows; ++y) {
            uchar* pixel = frame.ptr<uchar>(y);
            const std::uint16_t* gain_row =
                gain->ptr<std::uint16_t>(y);
            for (int x = 0; x < frame.cols; ++x) {
                const int base = x * 3;
                const int g = gain_row[x];
                const int b = (static_cast<int>(pixel[base]) * g + 128) >> 8;
                const int green =
                    (static_cast<int>(pixel[base + 1]) * g + 128) >> 8;
                const int r =
                    (static_cast<int>(pixel[base + 2]) * g + 128) >> 8;
                pixel[base] = cv::saturate_cast<uchar>(b);
                pixel[base + 1] = cv::saturate_cast<uchar>(green);
                pixel[base + 2] = cv::saturate_cast<uchar>(r);
            }
        }
    } else {
        for (int y = 0; y < frame.rows; ++y) {
            uchar* pixel = frame.ptr<uchar>(y);
            const std::int16_t* noise_row =
                noise.ptr<std::int16_t>(y);
            for (int x = 0; x < frame.cols; ++x) {
                const int base = x * 3;
                pixel[base] = cv::saturate_cast<uchar>(
                    static_cast<int>(pixel[base]) + noise_row[base]);
                pixel[base + 1] = cv::saturate_cast<uchar>(
                    static_cast<int>(pixel[base + 1]) + noise_row[base + 1]);
                pixel[base + 2] = cv::saturate_cast<uchar>(
                    static_cast<int>(pixel[base + 2]) + noise_row[base + 2]);
            }
        }
    }
}

void apply_radial_motion_blur(cv::Mat& frame, double zoom_delta) {
    if (zoom_delta <= 0.015) return;
    const double spread = std::clamp(zoom_delta * 0.035, 0.0, 0.022);
    const cv::Point2f center((frame.cols - 1) * 0.5F, (frame.rows - 1) * 0.5F);
    cv::Mat inner, outer, mixed;
    cv::warpAffine(frame, inner,
        cv::getRotationMatrix2D(center, 0.0, 1.0 - spread), frame.size(),
        cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    cv::warpAffine(frame, outer,
        cv::getRotationMatrix2D(center, 0.0, 1.0 + spread), frame.size(),
        cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    cv::addWeighted(frame, 0.54, inner, 0.23, 0.0, mixed);
    cv::addWeighted(mixed, 1.0, outer, 0.23, 0.0, frame);
}
}  // namespace chrononplugin::maps
