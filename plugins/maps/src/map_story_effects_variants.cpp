// Tactical-derived treatments: laser scan, particle flow, diorama, voronoi and
// holographic post effects. Split out of map_story_effects.cpp.

#include "chrononplugin/maps/map_story_effects.hpp"

#include "map_story_effects_internal.hpp"

#include "chrononplugin/maps/map_camera.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

namespace chrononplugin::maps {
using namespace story_detail;

void draw_volumetric_laser_scan(cv::Mat& frame,
                                const std::vector<GeoPolygon>& polygons,
                                std::pair<double, double> camera_anchor,
                                double zoom, double progress,
                                const std::string& label, bool show_text) {
    if (frame.empty()) return;
    progress = std::clamp(progress, 0.0, 1.0);
    const double eased = progress * progress * (3.0 - 2.0 * progress);
    const double scale = std::clamp(frame.rows / 1080.0, 0.45, 1.5);
    draw_tactical_territory(frame, polygons, camera_anchor, zoom, 0.90, "", "");
    cv::Mat shade(frame.size(), CV_8UC3, cv::Scalar(3, 5, 8));
    cv::addWeighted(frame, 0.42, shade, 0.58, 0.0, frame);
    const cv::Mat mask = territory_mask(frame, polygons, camera_anchor, zoom);
    if (cv::countNonZero(mask) == 0) return;
    const cv::Rect bounds = cv::boundingRect(mask);
    const int step_x = std::max(12, cvRound(28.0 * scale));
    const int step_y = std::max(10, cvRound(22.0 * scale));
    const int scan_y = std::clamp(cvRound(bounds.y + bounds.height -
        bounds.height * eased * 1.08), 0, frame.rows - 1);

    for (int gy = bounds.y; gy < bounds.y + bounds.height; gy += step_y) {
        for (int gx = bounds.x; gx < bounds.x + bounds.width; gx += step_x) {
            if (gx < 0 || gx >= frame.cols || gy < 0 || gy >= frame.rows) continue;
            if (mask.at<unsigned char>(gy, gx) == 0 || gy < scan_y - 1) continue;
            const double noise = std::fabs(std::sin(
                static_cast<double>(gx) * 0.071 +
                static_cast<double>(gy) * 0.113));
            const int height = cvRound((12.0 + 62.0 * noise) * scale);
            const int half = std::max(2, step_x / 3);
            const int top_y = std::max(0, gy - height);
            const int side_h = gy - top_y;
            if (side_h <= 0) continue;
            const cv::Scalar edge(255, 205, 60);
            const cv::Scalar side(58, 34, 18);
            cv::rectangle(frame, cv::Rect(gx - half, top_y, half * 2, side_h),
                          side, cv::FILLED);
            cv::line(frame, {gx - half, top_y}, {gx - half, gy}, edge,
                     1, cv::LINE_AA);
            cv::line(frame, {gx + half, top_y}, {gx + half, gy}, edge,
                     1, cv::LINE_AA);
            cv::line(frame, {gx - half, top_y}, {gx + half, top_y},
                     cv::Scalar(255, 240, 180), 1, cv::LINE_AA);
        }
    }

    cv::Mat beam(frame.size(), CV_8UC3, cv::Scalar(0, 0, 0));
    cv::line(beam, {bounds.x - 30, scan_y}, {bounds.x + bounds.width + 30, scan_y},
             cv::Scalar(180, 130, 40), std::max(3, cvRound(6.0 * scale)),
             cv::LINE_AA);
    cv::GaussianBlur(beam, beam, {0, 0}, 10.0 * scale);
    cv::addWeighted(frame, 1.0, beam, 0.65, 0.0, frame);
    cv::line(frame, {bounds.x - 30, scan_y}, {bounds.x + bounds.width + 30, scan_y},
             cv::Scalar(255, 240, 170), std::max(1, cvRound(2.0 * scale)),
             cv::LINE_AA);

    if (show_text) {
        const int x = cvRound(frame.cols * 0.07);
        cv::putText(frame, "VOLUMETRIC 2.5D / LASER SCAN", {x, cvRound(frame.rows * 0.10)},
                    cv::FONT_HERSHEY_SIMPLEX, 0.72 * scale,
                    cv::Scalar(255, 220, 90), std::max(1, cvRound(1.5 * scale)),
                    cv::LINE_AA);
        cv::putText(frame, label.empty() ? "JERUSALEM" : label,
                    {x, cvRound(frame.rows * 0.90)},
                    cv::FONT_HERSHEY_DUPLEX, 1.3 * scale,
                    cv::Scalar(248, 250, 252), std::max(2, cvRound(2.0 * scale)),
                    cv::LINE_AA);
    }
}

void draw_vector_particle_flow(cv::Mat& frame,
                               const std::vector<GeoPolygon>& polygons,
                               std::pair<double, double> camera_anchor,
                               double zoom, double progress,
                               const std::string& label, bool show_text) {
    if (frame.empty()) return;
    progress = std::clamp(progress, 0.0, 1.0);
    const double scale = std::clamp(frame.rows / 1080.0, 0.45, 1.5);
    draw_tactical_territory(frame, polygons, camera_anchor, zoom, 1.0, "", "");
    cv::Mat shade(frame.size(), CV_8UC3, cv::Scalar(2, 4, 7));
    cv::addWeighted(frame, 0.30, shade, 0.70, 0.0, frame);
    const cv::Mat mask = territory_mask(frame, polygons, camera_anchor, zoom);
    if (cv::countNonZero(mask) == 0) return;
    const cv::Rect bounds = cv::boundingRect(mask);

    cv::Mat roads(frame.size(), CV_8UC3, cv::Scalar(0, 0, 0));
    for (int i = 0; i < 12; ++i) {
        const int y = bounds.y + cvRound((i + 0.5) * bounds.height / 12.0);
        cv::line(roads, {bounds.x, y}, {bounds.x + bounds.width, y},
                 cv::Scalar(16, 48, 62), 1, cv::LINE_AA);
    }
    for (int i = 0; i < 9; ++i) {
        const int x = bounds.x + cvRound((i + 0.5) * bounds.width / 9.0);
        cv::line(roads, {x, bounds.y}, {x, bounds.y + bounds.height},
                 cv::Scalar(12, 35, 48), 1, cv::LINE_AA);
    }
    roads.copyTo(frame, mask);

    const int lanes = 12;
    const int particle_count = 560;
    for (int p = 0; p < particle_count; ++p) {
        const int lane = p % lanes;
        const double direction = (p / lanes) % 2 == 0 ? 1.0 : -1.0;
        const double phase = std::fmod(
            static_cast<double>(p) * 0.61803398875 + progress * 1.75, 1.0);
        const double t = direction > 0.0 ? phase : 1.0 - phase;
        const int cy = bounds.y + cvRound((lane + 0.5) * bounds.height / lanes +
            std::sin(t * 6.283185307 + p) * 5.0 * scale);
        const int cx = bounds.x + cvRound(t * bounds.width);
        if (cx < 0 || cx >= frame.cols || cy < 0 || cy >= frame.rows ||
            mask.at<unsigned char>(cy, cx) == 0) continue;
        cv::Point last(cx, cy);
        const cv::Scalar trail = (p % 3 == 0)
            ? cv::Scalar(255, 180, 40) : cv::Scalar(20, 160, 255);
        for (int k = 1; k <= 6; ++k) {
            const double tk = t - direction * 0.012 * k;
            if (tk < 0.0 || tk > 1.0) break;
            const int tx = bounds.x + cvRound(tk * bounds.width);
            const int ty = bounds.y + cvRound((lane + 0.5) * bounds.height / lanes +
                std::sin(tk * 6.283185307 + p) * 5.0 * scale);
            if (tx < 0 || tx >= frame.cols || ty < 0 || ty >= frame.rows ||
                mask.at<unsigned char>(ty, tx) == 0) break;
            const double fade = 1.0 - static_cast<double>(k) / 7.0;
            cv::line(frame, last, {tx, ty},
                     cv::Scalar(trail[0] * fade, trail[1] * fade, trail[2] * fade),
                     1, cv::LINE_AA);
            last = {tx, ty};
        }
        cv::circle(frame, {cx, cy}, std::max(1, cvRound(2.0 * scale)),
                   cv::Scalar(255, 245, 180), cv::FILLED, cv::LINE_AA);
    }

    if (show_text) {
        const int x = cvRound(frame.cols * 0.07);
        cv::putText(frame, "VECTOR FLOW / 560 CPU AGENTS",
                    {x, cvRound(frame.rows * 0.10)}, cv::FONT_HERSHEY_SIMPLEX,
                    0.72 * scale, cv::Scalar(80, 220, 255),
                    std::max(1, cvRound(1.5 * scale)), cv::LINE_AA);
        cv::putText(frame, label.empty() ? "JERUSALEM" : label,
                    {x, cvRound(frame.rows * 0.90)}, cv::FONT_HERSHEY_DUPLEX,
                    1.3 * scale, cv::Scalar(248, 250, 252),
                    std::max(2, cvRound(2.0 * scale)), cv::LINE_AA);
    }
}

void draw_dark_mirror_diorama(cv::Mat& frame,
                              const std::vector<GeoPolygon>& polygons,
                              std::pair<double, double> camera_anchor,
                              double zoom, double progress,
                              const std::string& label, bool show_text) {
    if (frame.empty()) return;
    progress = std::clamp(progress, 0.0, 1.0);
    const double scale = std::clamp(frame.rows / 1080.0, 0.45, 1.5);
    draw_tactical_territory(frame, polygons, camera_anchor, zoom, 1.0, "", "");
    const cv::Mat map_layer = frame.clone();
    const int width = frame.cols;
    const int height = frame.rows;
    const int horizon = cvRound(height * 0.58);
    frame.setTo(cv::Scalar(3, 5, 9));

    cv::Point2f vanishing(width * 0.5F, static_cast<float>(horizon) - 12.0F);
    for (int i = -10; i <= 10; ++i) {
        const int x = width / 2 + i * width / 14;
        cv::line(frame, vanishing, {x, height - 1}, cv::Scalar(18, 42, 58),
                 1, cv::LINE_AA);
    }
    for (int i = 1; i <= 7; ++i) {
        const int y = horizon + cvRound((height - horizon) * i * i / 49.0);
        cv::line(frame, {0, y}, {width - 1, y}, cv::Scalar(14, 34, 48),
                 1, cv::LINE_AA);
    }

    const std::vector<cv::Point2f> source{{0, 0}, {width - 1.0F, 0},
        {width - 1.0F, height - 1.0F}, {0, height - 1.0F}};
    const std::vector<cv::Point2f> target{{width * 0.16F, height * 0.04F},
        {width * 0.84F, height * 0.04F},
        {width * 0.98F, static_cast<float>(horizon)},
        {width * 0.02F, static_cast<float>(horizon)}};
    const cv::Mat transform = cv::getPerspectiveTransform(source, target);
    cv::Mat tilted;
    cv::warpPerspective(map_layer, tilted, transform, frame.size(),
                        cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
    cv::Mat white(map_layer.size(), CV_8UC3, cv::Scalar(255, 255, 255));
    cv::Mat white_warped;
    cv::warpPerspective(white, white_warped, transform, frame.size(),
                        cv::INTER_NEAREST, cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
    cv::Mat map_mask;
    cv::cvtColor(white_warped, map_mask, cv::COLOR_BGR2GRAY);
    cv::threshold(map_mask, map_mask, 8, 255, cv::THRESH_BINARY);
    tilted.copyTo(frame, map_mask);

    const int reflection_height = std::min(horizon, height - horizon);
    cv::Mat reflection_source = tilted(cv::Rect(0, 0, width, horizon));
    cv::Mat reflection;
    cv::flip(reflection_source, reflection, 0);
    cv::resize(reflection, reflection, cv::Size(width, reflection_height),
               0.0, 0.0, cv::INTER_LINEAR);
    cv::GaussianBlur(reflection, reflection, {0, 0}, 6.0 * scale);
    reflection.convertTo(reflection, -1, 0.50);
    cv::Mat tint(reflection.size(), CV_8UC3, cv::Scalar(70, 20, 8));
    cv::addWeighted(reflection, 0.82, tint, 0.18, 0.0, reflection);
    const cv::Rect lower(0, horizon, width, reflection_height);
    cv::Mat destination = frame(lower);
    for (int y = 0; y < reflection_height; ++y) {
        const double alpha = 0.62 * progress *
            (1.0 - static_cast<double>(y) / std::max(1, reflection_height));
        cv::addWeighted(destination.row(y), 1.0 - alpha,
                        reflection.row(y), alpha, 0.0, destination.row(y));
    }

    cv::Mat glow(frame.size(), CV_8UC3, cv::Scalar(0, 0, 0));
    cv::line(glow, {0, horizon}, {width - 1, horizon},
             cv::Scalar(120, 45, 12), std::max(2, cvRound(3.0 * scale)),
             cv::LINE_AA);
    cv::GaussianBlur(glow, glow, {0, 0}, 12.0 * scale);
    cv::addWeighted(frame, 1.0, glow, 0.75, 0.0, frame);

    if (show_text) {
        const int x = cvRound(width * 0.07);
        cv::putText(frame, "DARK MIRROR / DIORAMA", {x, cvRound(height * 0.10)},
                    cv::FONT_HERSHEY_SIMPLEX, 0.72 * scale,
                    cv::Scalar(255, 180, 70), std::max(1, cvRound(1.5 * scale)),
                    cv::LINE_AA);
        cv::putText(frame, label.empty() ? "JERUSALEM" : label,
                    {x, cvRound(height * 0.49)}, cv::FONT_HERSHEY_DUPLEX,
                    1.3 * scale, cv::Scalar(248, 250, 252),
                    std::max(2, cvRound(2.0 * scale)), cv::LINE_AA);
    }
}

void draw_voronoi_heat_contours(cv::Mat& frame,
                                const std::vector<GeoPolygon>& polygons,
                                std::pair<double, double> camera_anchor,
                                double zoom, double progress,
                                const std::string& label, bool show_text) {
    if (frame.empty()) return;
    progress = std::clamp(progress, 0.0, 1.0);
    const double scale = std::clamp(frame.rows / 1080.0, 0.45, 1.5);
    draw_tactical_territory(frame, polygons, camera_anchor, zoom, 1.0, "", "");
    cv::Mat shade(frame.size(), CV_8UC3, cv::Scalar(3, 5, 8));
    cv::addWeighted(frame, 0.38, shade, 0.62, 0.0, frame);
    const cv::Mat mask = territory_mask(frame, polygons, camera_anchor, zoom);
    if (cv::countNonZero(mask) == 0) return;
    const cv::Rect bounds = cv::boundingRect(mask);

    const int sw = 480;
    const int sh = std::max(1, sw * frame.rows / frame.cols);
    cv::Mat small_mask;
    cv::resize(mask, small_mask, {sw, sh}, 0.0, 0.0, cv::INTER_NEAREST);
    const cv::Rect sb = cv::boundingRect(small_mask);
    if (sb.width < 4 || sb.height < 4) return;
    const cv::Point2f center(sb.x + sb.width * 0.5F, sb.y + sb.height * 0.5F);
    const float radius = 0.34F * std::min(sb.width, sb.height);
    std::vector<cv::Point2f> seeds;
    seeds.emplace_back(center);
    for (int i = 0; i < 7; ++i) {
        const double angle = 2.0 * kPi * i / 7.0;
        const cv::Point2f seed(center.x + std::cos(angle) * radius,
                               center.y + std::sin(angle) * radius);
        const int sx = std::clamp(cvRound(seed.x), 0, sw - 1);
        const int sy = std::clamp(cvRound(seed.y), 0, sh - 1);
        if (small_mask.at<unsigned char>(sy, sx) != 0) seeds.push_back(seed);
    }
    if (seeds.size() < 2) return;

    cv::Mat heat(sh, sw, CV_32F, cv::Scalar(0));
    cv::Mat labels(sh, sw, CV_16S, cv::Scalar(-1));
    for (int y = 0; y < sh; ++y) {
        for (int x = 0; x < sw; ++x) {
            if (small_mask.at<unsigned char>(y, x) == 0) continue;
            float best = 1e30F;
            float second = 1e30F;
            short best_index = 0;
            for (std::size_t s = 0; s < seeds.size(); ++s) {
                const float dx = static_cast<float>(x) - seeds[s].x;
                const float dy = static_cast<float>(y) - seeds[s].y;
                const float distance = dx * dx + dy * dy;
                if (distance < best) {
                    second = best;
                    best = distance;
                    best_index = static_cast<short>(s);
                } else if (distance < second) {
                    second = distance;
                }
            }
            heat.at<float>(y, x) = best;
            labels.at<short>(y, x) = best_index;
            (void)second;
        }
    }
    double minimum = 0.0;
    double maximum = 1.0;
    cv::minMaxLoc(heat, &minimum, &maximum);
    const double span = std::max(1e-6, maximum - minimum);
    cv::Mat heat_bgr(sh, sw, CV_8UC3, cv::Scalar(0, 0, 0));
    cv::Mat lines(sh, sw, CV_8UC3, cv::Scalar(0, 0, 0));
    for (int y = 0; y < sh; ++y) {
        for (int x = 0; x < sw; ++x) {
            if (small_mask.at<unsigned char>(y, x) == 0) continue;
            const float t = static_cast<float>((heat.at<float>(y, x) - minimum) / span);
            int blue = 0;
            int green = 0;
            int red = 0;
            if (t < 0.5F) {
                const float u = t * 2.0F;
                blue = 255;
                green = cvRound(255.0F * (1.0F - u));
                red = cvRound(255.0F * u);
            } else {
                const float u = (t - 0.5F) * 2.0F;
                blue = cvRound(255.0F * (1.0F - u));
                green = cvRound(180.0F * u);
                red = 255;
            }
            heat_bgr.at<cv::Vec3b>(y, x) = cv::Vec3b(
                static_cast<unsigned char>(blue), static_cast<unsigned char>(green),
                static_cast<unsigned char>(red));
            const float phase = t * 10.0F - static_cast<float>(progress) * 3.0F;
            const float fraction = phase - std::floor(phase);
            if (fraction < 0.08F) {
                lines.at<cv::Vec3b>(y, x) = cv::Vec3b(40, 220, 255);
            }
            const short here = labels.at<short>(y, x);
            if ((x > 0 && labels.at<short>(y, x - 1) != here) ||
                (y > 0 && labels.at<short>(y - 1, x) != here)) {
                lines.at<cv::Vec3b>(y, x) = cv::Vec3b(245, 250, 255);
            }
        }
    }

    cv::Mat heat_full, lines_full, mask_full;
    cv::resize(heat_bgr, heat_full, frame.size(), 0.0, 0.0, cv::INTER_LINEAR);
    cv::resize(lines, lines_full, frame.size(), 0.0, 0.0, cv::INTER_NEAREST);
    cv::resize(small_mask, mask_full, frame.size(), 0.0, 0.0, cv::INTER_NEAREST);
    cv::Mat blended;
    cv::addWeighted(frame, 0.22, heat_full, 0.78, 0.0, blended);
    blended.copyTo(frame, mask_full);
    cv::addWeighted(frame, 1.0, lines_full, 0.65, 0.0, frame);
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    cv::drawContours(frame, contours, -1, cv::Scalar(245, 250, 255),
                     std::max(1, cvRound(2.0 * scale)), cv::LINE_AA);
    for (const auto& seed : seeds) {
        const int sx = cvRound(seed.x * frame.cols / static_cast<double>(sw));
        const int sy = cvRound(seed.y * frame.rows / static_cast<double>(sh));
        cv::circle(frame, {sx, sy}, std::max(2, cvRound(4.0 * scale)),
                   cv::Scalar(255, 245, 180), cv::FILLED, cv::LINE_AA);
    }

    if (show_text) {
        const int x = cvRound(frame.cols * 0.07);
        cv::putText(frame, "VORONOI HEAT / LIVE CONTOURS",
                    {x, cvRound(frame.rows * 0.10)}, cv::FONT_HERSHEY_SIMPLEX,
                    0.72 * scale, cv::Scalar(120, 220, 255),
                    std::max(1, cvRound(1.5 * scale)), cv::LINE_AA);
        cv::putText(frame, label.empty() ? "JERUSALEM" : label,
                    {x, cvRound(frame.rows * 0.90)}, cv::FONT_HERSHEY_DUPLEX,
                    1.3 * scale, cv::Scalar(248, 250, 252),
                    std::max(2, cvRound(2.0 * scale)), cv::LINE_AA);
    }
}

void draw_holographic_postfx(cv::Mat& frame,
                             const std::vector<GeoPolygon>& polygons,
                             std::pair<double, double> camera_anchor,
                             double zoom, double progress,
                             const std::string& label, bool show_text) {
    if (frame.empty()) return;
    progress = std::clamp(progress, 0.0, 1.0);
    const double scale = std::clamp(frame.rows / 1080.0, 0.45, 1.5);
    draw_tactical_territory(frame, polygons, camera_anchor, zoom, 1.0, "", "");
    const cv::Mat source = frame.clone();
    const int width = frame.cols;
    const int height = frame.rows;
    const cv::Point2f center((width - 1) * 0.5F, (height - 1) * 0.5F);
    cv::Mat map_x(height, width, CV_32F);
    cv::Mat map_y(height, width, CV_32F);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float nx = (x - center.x) / center.x;
            const float ny = (y - center.y) / center.y;
            const float radius = nx * nx + ny * ny;
            const float factor = 1.0F - 0.10F * radius;
            map_x.at<float>(y, x) = center.x + nx * factor * center.x;
            map_y.at<float>(y, x) = center.y + ny * factor * center.y;
        }
    }
    cv::Mat distorted;
    cv::remap(source, distorted, map_x, map_y, cv::INTER_LINEAR,
              cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
    cv::Mat channels[3];
    cv::split(distorted, channels);
    cv::Mat red;
    cv::Mat blue;
    cv::warpAffine(channels[2], red,
                   cv::getRotationMatrix2D(center, 0.0, 1.004), frame.size(),
                   cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    cv::warpAffine(channels[0], blue,
                   cv::getRotationMatrix2D(center, 0.0, 0.996), frame.size(),
                   cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    channels[2] = red;
    channels[0] = blue;
    cv::merge(channels, 3, distorted);

    cv::Mat scanlines(frame.size(), CV_8UC3, cv::Scalar(255, 255, 255));
    for (int y = 0; y < height; y += 3) {
        cv::line(scanlines, {0, y}, {width - 1, y}, cv::Scalar(0, 0, 0),
                 1, cv::LINE_AA);
    }
    cv::multiply(distorted, scanlines, distorted, 1.0 / 255.0);

    cv::Mat gray;
    cv::Mat bright_mask;
    cv::cvtColor(distorted, gray, cv::COLOR_BGR2GRAY);
    cv::threshold(gray, bright_mask, 175, 255, cv::THRESH_BINARY);
    cv::Mat bloom = cv::Mat::zeros(frame.size(), CV_8UC3);
    distorted.copyTo(bloom, bright_mask);
    cv::GaussianBlur(bloom, bloom, {0, 0}, 6.0 * scale);
    cv::addWeighted(distorted, 1.0, bloom, 0.45, 0.0, distorted);

    cv::Mat soft;
    cv::GaussianBlur(distorted, soft, {0, 0}, 3.5 * scale);
    const int band = std::max(1, height / 6);
    for (int y = 0; y < band; ++y) {
        const double alpha = 0.70 * (1.0 - static_cast<double>(y) / band);
        cv::addWeighted(distorted.row(y), 1.0 - alpha, soft.row(y), alpha,
                        0.0, distorted.row(y));
    }
    for (int y = height - band; y < height; ++y) {
        const double alpha = 0.70 *
            (static_cast<double>(y - (height - band)) / band);
        cv::addWeighted(distorted.row(y), 1.0 - alpha, soft.row(y), alpha,
                        0.0, distorted.row(y));
    }

    const cv::Scalar hud(255, 220, 80);
    cv::line(distorted, {24, 24}, {120, 24}, hud,
             std::max(1, cvRound(2.0 * scale)), cv::LINE_AA);
    cv::line(distorted, {24, 24}, {24, 80}, hud,
             std::max(1, cvRound(2.0 * scale)), cv::LINE_AA);
    cv::line(distorted, {width - 24, 24}, {width - 120, 24}, hud,
             std::max(1, cvRound(2.0 * scale)), cv::LINE_AA);
    cv::line(distorted, {width - 24, 24}, {width - 24, 80}, hud,
             std::max(1, cvRound(2.0 * scale)), cv::LINE_AA);
    if (show_text) {
        cv::putText(distorted, "HOLOGRAPHIC OPTIC // LIVE",
                    {cvRound(width * 0.07), cvRound(height * 0.10)},
                    cv::FONT_HERSHEY_SIMPLEX, 0.72 * scale, hud,
                    std::max(1, cvRound(1.5 * scale)), cv::LINE_AA);
    }
    const int sweep = cvRound(std::fmod(progress * 1.6, 1.0) * height);
    cv::line(distorted, {0, sweep}, {width - 1, sweep}, cv::Scalar(180, 120, 40),
             1, cv::LINE_AA);
    if (show_text) {
        cv::putText(distorted, label.empty() ? "JERUSALEM" : label,
                    {cvRound(width * 0.07), cvRound(height * 0.88)},
                    cv::FONT_HERSHEY_DUPLEX, 1.35 * scale,
                    cv::Scalar(248, 250, 252),
                    std::max(2, cvRound(2.0 * scale)), cv::LINE_AA);
    }
    frame = distorted;
}
}  // namespace chrononplugin::maps
