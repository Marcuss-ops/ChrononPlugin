#include "chrononplugin/maps/map_route_render.hpp"

#include <algorithm>
#include <cmath>

#include <opencv2/imgproc.hpp>

#include "chrononplugin/maps/map_camera.hpp"

namespace chrononplugin::maps {

namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;
const cv::Scalar kInk(25, 31, 34);
const cv::Scalar kGold(77, 207, 244);
const cv::Scalar kPaper(247, 249, 247);

[[nodiscard]] double radians(double degrees) { return degrees * kPi / 180.0; }
[[nodiscard]] double degrees(double radians_value) { return radians_value * 180.0 / kPi; }

[[nodiscard]] std::pair<double, double> great_circle_point(
    const MapRouteStop& a, const MapRouteStop& b, double t) {
    const double lat1 = radians(a.latitude), lon1 = radians(a.longitude);
    const double lat2 = radians(b.latitude), lon2 = radians(b.longitude);
    const auto vector = [](double lat, double lon) {
        return cv::Vec3d(std::cos(lat) * std::cos(lon),
                         std::cos(lat) * std::sin(lon), std::sin(lat));
    };
    const cv::Vec3d va = vector(lat1, lon1), vb = vector(lat2, lon2);
    const double dot = std::clamp(va.dot(vb), -1.0, 1.0);
    const double omega = std::acos(dot);
    cv::Vec3d v;
    if (omega < 1e-9) {
        v = va * (1.0 - t) + vb * t;
    } else {
        const double sin_omega = std::sin(omega);
        v = va * (std::sin((1.0 - t) * omega) / sin_omega) +
            vb * (std::sin(t * omega) / sin_omega);
    }
    v /= cv::norm(v);
    return {degrees(std::atan2(v[2], std::hypot(v[0], v[1]))),
            degrees(std::atan2(v[1], v[0]))};
}

[[nodiscard]] std::vector<cv::Point> segment_points(
    const MapRouteStop& a, const MapRouteStop& b,
    std::pair<double, double> anchor, double zoom, cv::Size frame_size,
    int samples = 48) {
    std::vector<cv::Point> points;
    points.reserve(static_cast<std::size_t>(samples) + 1);
    for (int i = 0; i <= samples; ++i) {
        const auto [lat, lon] = great_circle_point(a, b,
            static_cast<double>(i) / static_cast<double>(samples));
        const auto [x, y] = geo_to_screen(lat, lon, anchor, zoom,
                                          frame_size.width, frame_size.height);
        points.emplace_back(x, y);
    }
    return points;
}

void draw_label(cv::Mat& frame, cv::Point p, const std::string& label,
                cv::Point offset = {15, -13}) {
    if (label.empty()) return;
    int baseline = 0;
    const int width = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX,
                                     0.74, 2, &baseline).width;
    if (offset.x < 0) offset.x -= width;
    const cv::Point origin(p.x + offset.x, p.y + offset.y);
    cv::putText(frame, label, origin, cv::FONT_HERSHEY_SIMPLEX, 0.74,
                kInk, 5, cv::LINE_AA);
    cv::putText(frame, label, origin, cv::FONT_HERSHEY_SIMPLEX, 0.74,
                kPaper, 2, cv::LINE_AA);
}

}  // namespace

void draw_animated_pin(cv::Mat& frame, cv::Point position, int frame_index,
                       const std::string& label) {
    const double phase = 2.0 * kPi * static_cast<double>(frame_index % 36) / 36.0;
    const int radius = 12 + static_cast<int>(std::nearbyint(5.0 * (0.5 + 0.5 * std::sin(phase))));
    cv::Mat glow(frame.size(), frame.type(), cv::Scalar(0, 0, 0));
    cv::circle(glow, position, radius + 5, kGold, cv::FILLED, cv::LINE_AA);
    cv::addWeighted(frame, 1.0, glow, 0.19, 0.0, frame);
    cv::circle(frame, position, radius, kGold, 2, cv::LINE_AA);
    cv::circle(frame, position, 8, kInk, cv::FILLED, cv::LINE_AA);
    cv::circle(frame, position, 5, kGold, cv::FILLED, cv::LINE_AA);
    cv::circle(frame, position, 2, kPaper, cv::FILLED, cv::LINE_AA);
    draw_label(frame, position, label);
}

void draw_animated_route(cv::Mat& frame, const std::vector<MapRouteStop>& stops,
                         std::pair<double, double> camera_anchor, double zoom,
                         double progress, int frame_index) {
    if (stops.size() < 2) return;
    progress = std::clamp(progress, 0.0, 1.0);
    std::vector<std::vector<cv::Point>> segments;
    segments.reserve(stops.size() - 1);
    for (std::size_t i = 0; i + 1 < stops.size(); ++i) {
        segments.push_back(segment_points(stops[i], stops[i + 1], camera_anchor,
                                          zoom, frame.size()));
        cv::polylines(frame, segments.back(), false, kInk, 8, cv::LINE_AA);
        cv::polylines(frame, segments.back(), false, cv::Scalar(215, 190, 142),
                      3, cv::LINE_AA);
    }

    const double scaled = progress * static_cast<double>(segments.size());
    const std::size_t active_segment = std::min(
        static_cast<std::size_t>(scaled), segments.size() - 1);
    const double segment_progress = progress >= 1.0 ? 1.0 : scaled - active_segment;
    for (std::size_t i = 0; i < active_segment; ++i) {
        cv::polylines(frame, segments[i], false, kGold, 4, cv::LINE_AA);
    }
    const auto& active = segments[active_segment];
    const std::size_t active_count = std::max<std::size_t>(
        2, static_cast<std::size_t>(std::ceil(segment_progress * (active.size() - 1))) + 1);
    std::vector<cv::Point> partial(active.begin(),
        active.begin() + static_cast<std::ptrdiff_t>(std::min(active_count, active.size())));
    if (partial.size() >= 2) cv::polylines(frame, partial, false, kGold, 4, cv::LINE_AA);

    for (std::size_t i = 0; i < stops.size(); ++i) {
        const auto [x, y] = geo_to_screen(stops[i].latitude, stops[i].longitude,
                                          camera_anchor, zoom,
                                          frame.cols, frame.rows);
        const cv::Point point(x, y);
        cv::circle(frame, point, 10, kPaper, cv::FILLED, cv::LINE_AA);
        cv::circle(frame, point, 7, kInk, cv::FILLED, cv::LINE_AA);
        cv::circle(frame, point, 4, kGold, cv::FILLED, cv::LINE_AA);
        if (i == 0) {
            draw_label(frame, point, stops[i].label, {15, 31});
        } else if (i + 1 == stops.size()) {
            draw_label(frame, point, stops[i].label, {15, -13});
        } else {
            draw_label(frame, point, stops[i].label, {-18, 7});
        }
    }

    const std::size_t head_index = std::min(
        static_cast<std::size_t>(std::nearbyint(segment_progress * (active.size() - 1))),
        active.size() - 1);
    const cv::Point head = active[head_index];
    const int pulse_radius = 13 + static_cast<int>(std::nearbyint(
        7.0 * (0.5 + 0.5 * std::sin(2.0 * kPi * (frame_index % 30) / 30.0))));
    cv::circle(frame, head, pulse_radius, kGold, 2, cv::LINE_AA);
    cv::circle(frame, head, 5, kPaper, cv::FILLED, cv::LINE_AA);
    cv::circle(frame, head, 2, kGold, cv::FILLED, cv::LINE_AA);
}

}  // namespace chrononplugin::maps
