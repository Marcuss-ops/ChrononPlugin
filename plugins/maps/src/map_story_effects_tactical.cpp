// Tactical territory treatment with per-phase timing. Split out of
// map_story_effects.cpp.

#include "chrononplugin/maps/map_story_effects.hpp"

#include "map_story_effects_internal.hpp"

#include "chrononplugin/maps/map_camera.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace chrononplugin::maps {
using namespace story_detail;

void draw_tactical_territory(cv::Mat& frame,
                             const std::vector<GeoPolygon>& polygons,
                             std::pair<double, double> camera_anchor,
                             double zoom, double progress,
                             const std::string& title,
                             const std::string& subtitle,
                             TacticalPhaseTimings* timings) {
    if (frame.empty()) return;
    const auto phase_start = [] { return std::chrono::steady_clock::now(); };
    const auto phase_add = [timings](double TacticalPhaseTimings::*field,
                                     std::chrono::steady_clock::time_point start) {
        if (timings == nullptr) return;
        (timings->*field) += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
    };
    progress = std::clamp(progress, 0.0, 1.0);
    const double eased = progress * progress * (3.0 - 2.0 * progress);
    const double scale = std::clamp(frame.rows / 1080.0, 0.45, 1.5);

    // The reference treatment is a screen-space tactical grid, not a raster
    // basemap: the territory is the only geographic layer that moves.
    const auto grid_start = phase_start();
    frame.setTo(cv::Scalar(6, 8, 11));
    const int step = std::max(18, cvRound(54.0 * scale));
    const int major_step = step * 4;
    const cv::Scalar minor_grid(33, 37, 43);
    const cv::Scalar major_grid(50, 55, 64);
    for (int x = 0; x < frame.cols; x += step) {
        const bool major = x % major_step == 0;
        cv::line(frame, {x, 0}, {x, frame.rows - 1},
                 major ? major_grid : minor_grid, 1, cv::LINE_AA);
    }
    for (int y = 0; y < frame.rows; y += step) {
        const bool major = y % major_step == 0;
        cv::line(frame, {0, y}, {frame.cols - 1, y},
                 major ? major_grid : minor_grid, 1, cv::LINE_AA);
    }
    phase_add(&TacticalPhaseTimings::grid_ms, grid_start);

    const auto mask_start = phase_start();
    const cv::Mat mask = territory_mask(frame, polygons, camera_anchor, zoom);
    if (cv::countNonZero(mask) == 0) {
        phase_add(&TacticalPhaseTimings::mask_ms, mask_start);
        return;
    }
    const cv::Moments moments = cv::moments(mask, true);
    const cv::Point center = moments.m00 > 0.0
        ? cv::Point(cvRound(moments.m10 / moments.m00),
                    cvRound(moments.m01 / moments.m00))
        : cv::Point(frame.cols / 2, frame.rows / 2);
    const cv::Rect bounds = cv::boundingRect(mask);
    const double max_radius = std::hypot(
        std::max(center.x - bounds.x, bounds.x + bounds.width - center.x),
        std::max(center.y - bounds.y, bounds.y + bounds.height - center.y));
    cv::Mat reveal(frame.size(), CV_8UC1, cv::Scalar(0));
    cv::circle(reveal, center, cvRound(max_radius * eased * 1.15),
               cv::Scalar(255), cv::FILLED, cv::LINE_AA);
    cv::Mat active;
    cv::bitwise_and(mask, reveal, active);
    if (cv::countNonZero(active) == 0) {
        phase_add(&TacticalPhaseTimings::mask_ms, mask_start);
        return;
    }
    phase_add(&TacticalPhaseTimings::mask_ms, mask_start);

    const auto fill_start = phase_start();
    const cv::Mat fill(frame.size(), CV_8UC3, cv::Scalar(42, 170, 48));
    cv::Mat tinted;
    cv::addWeighted(frame, 0.14, fill, 0.86, 0.0, tinted);
    tinted.copyTo(frame, active);
    phase_add(&TacticalPhaseTimings::fill_ms, fill_start);

    const auto contours_start = phase_start();
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(active, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
    phase_add(&TacticalPhaseTimings::contours_ms, contours_start);
    const cv::Scalar red_glow(28, 36, 230);
    const cv::Scalar red_inner(40, 58, 255);
    const cv::Scalar outline(246, 249, 251);
    const auto scale_points = [](const std::vector<std::vector<cv::Point>>& source,
                                 double factor) {
        std::vector<std::vector<cv::Point>> scaled;
        scaled.reserve(source.size());
        for (const auto& contour : source) {
            std::vector<cv::Point> points;
            points.reserve(contour.size());
            for (const cv::Point& point : contour) {
                points.emplace_back(cvRound(point.x * factor),
                                    cvRound(point.y * factor));
            }
            scaled.push_back(std::move(points));
        }
        return scaled;
    };

    // The halo is a low-frequency layer: rasterize and blur it at 1/4
    // resolution, then upsample. The crisp outline stays at full resolution.
    const double broad_factor = 0.25;
    const cv::Size broad_size(std::max(1, cvRound(frame.cols * broad_factor)),
                              std::max(1, cvRound(frame.rows * broad_factor)));
    cv::Mat broad_small(broad_size, CV_8UC3, cv::Scalar(0, 0, 0));
    auto glow_start = phase_start();
    const auto broad_contours = scale_points(contours, broad_factor);
    cv::drawContours(broad_small, broad_contours, -1, red_glow,
                     std::max(2, cvRound(30.0 * scale * broad_factor)),
                     cv::LINE_AA);
    phase_add(&TacticalPhaseTimings::glow_draw_ms, glow_start);
    auto blur_start = phase_start();
    cv::GaussianBlur(broad_small, broad_small, {0, 0},
                     20.0 * scale * broad_factor);
    cv::Mat broad_glow;
    cv::resize(broad_small, broad_glow, frame.size(), 0.0, 0.0, cv::INTER_LINEAR);
    phase_add(&TacticalPhaseTimings::blur_ms, blur_start);
    cv::addWeighted(frame, 1.0, broad_glow, 0.78 * progress, 0.0, frame);

    const double inner_factor = 0.5;
    const cv::Size inner_size(std::max(1, cvRound(frame.cols * inner_factor)),
                              std::max(1, cvRound(frame.rows * inner_factor)));
    cv::Mat inner_small(inner_size, CV_8UC3, cv::Scalar(0, 0, 0));
    glow_start = phase_start();
    const auto inner_contours = scale_points(contours, inner_factor);
    cv::drawContours(inner_small, inner_contours, -1, red_inner,
                     std::max(2, cvRound(7.0 * scale * inner_factor)),
                     cv::LINE_AA);
    phase_add(&TacticalPhaseTimings::glow_draw_ms, glow_start);
    blur_start = phase_start();
    cv::GaussianBlur(inner_small, inner_small, {0, 0},
                     4.0 * scale * inner_factor);
    cv::Mat inner_glow;
    cv::resize(inner_small, inner_glow, frame.size(), 0.0, 0.0, cv::INTER_LINEAR);
    phase_add(&TacticalPhaseTimings::blur_ms, blur_start);
    cv::addWeighted(frame, 1.0, inner_glow, 0.68 * progress, 0.0, frame);

    const auto outline_start = phase_start();
    cv::drawContours(frame, contours, -1, outline,
                     std::max(1, cvRound(2.4 * scale)), cv::LINE_AA);
    phase_add(&TacticalPhaseTimings::outline_ms, outline_start);

    const auto label_start = phase_start();
    const double label_alpha = std::clamp((progress - 0.42) / 0.20, 0.0, 1.0);
    if (label_alpha > 0.0 && (!title.empty() || !subtitle.empty())) {
        cv::Mat overlay = frame.clone();
        const int x = cvRound(frame.cols * 0.08);
        const int title_y = cvRound(frame.rows * 0.48);
        const int subtitle_y = title_y + cvRound(52.0 * scale);
        const int title_thickness = std::max(2, cvRound(2.5 * scale));
        const int subtitle_thickness = std::max(1, cvRound(1.8 * scale));
        const double title_scale = 1.55 * scale;
        const double subtitle_scale = 0.95 * scale;
        if (!title.empty()) {
            cv::putText(overlay, title, {x + 2, title_y + 2},
                        cv::FONT_HERSHEY_DUPLEX, title_scale,
                        cv::Scalar(0, 0, 0), title_thickness + 1, cv::LINE_AA);
            cv::putText(overlay, title, {x, title_y},
                        cv::FONT_HERSHEY_DUPLEX, title_scale,
                        cv::Scalar(248, 250, 252), title_thickness, cv::LINE_AA);
        }
        if (!subtitle.empty()) {
            cv::putText(overlay, subtitle, {x + 2, subtitle_y + 2},
                        cv::FONT_HERSHEY_SIMPLEX, subtitle_scale,
                        cv::Scalar(0, 0, 0), subtitle_thickness + 1, cv::LINE_AA);
            cv::putText(overlay, subtitle, {x, subtitle_y},
                        cv::FONT_HERSHEY_SIMPLEX, subtitle_scale,
                        cv::Scalar(184, 189, 195), subtitle_thickness, cv::LINE_AA);
        }
        cv::addWeighted(frame, 1.0 - label_alpha, overlay, label_alpha, 0.0, frame);
    }
    phase_add(&TacticalPhaseTimings::label_ms, label_start);
}
}  // namespace chrononplugin::maps
