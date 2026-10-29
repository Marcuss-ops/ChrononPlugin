#include "chrononplugin/maps/map_route_render.hpp"

#include <algorithm>
#include <cmath>

#include <opencv2/imgproc.hpp>

#include "chrononplugin/maps/map_camera.hpp"

namespace chrononplugin::maps {

namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;
const cv::Scalar kMapBlue(248, 139, 60);
const cv::Scalar kPaper(244, 247, 250);

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
                                     0.61, 1, &baseline).width;
    if (offset.x < 0) offset.x -= width;
    const cv::Point origin(p.x + offset.x, p.y + offset.y);
    cv::putText(frame, label, origin + cv::Point(1, 1), cv::FONT_HERSHEY_SIMPLEX,
                0.61, cv::Scalar(15, 25, 35), 2, cv::LINE_AA);
    cv::putText(frame, label, origin, cv::FONT_HERSHEY_SIMPLEX, 0.61,
                kPaper, 1, cv::LINE_AA);
}

}  // namespace

void draw_animated_route(cv::Mat& frame, const std::vector<MapRouteStop>& stops,
                         std::pair<double, double> camera_anchor, double zoom,
                         double progress, int frame_index,
                         const std::vector<double>& altitude_m) {
    (void)frame_index;
    if (stops.size() < 2) return;
    progress = std::clamp(progress, 0.0, 1.0);
    std::vector<std::vector<cv::Point>> segments;
    segments.reserve(stops.size() - 1);
    for (std::size_t i = 0; i + 1 < stops.size(); ++i) {
        segments.push_back(segment_points(stops[i], stops[i + 1], camera_anchor,
                                          zoom, frame.size()));
        cv::polylines(frame, segments.back(), false, cv::Scalar(209, 196, 174),
                      1, cv::LINE_AA);
    }

    const double scaled = progress * static_cast<double>(segments.size());
    const std::size_t active_segment = std::min(
        static_cast<std::size_t>(scaled), segments.size() - 1);
    const double segment_progress = progress >= 1.0 ? 1.0 : scaled - active_segment;
    cv::Mat route_glow(frame.size(), frame.type(), cv::Scalar(0, 0, 0));
    for (std::size_t i = 0; i < active_segment; ++i) {
        cv::polylines(frame, segments[i], false, kMapBlue, 2, cv::LINE_AA);
        cv::polylines(route_glow, segments[i], false, kMapBlue, 8, cv::LINE_AA);
    }
    const auto& active = segments[active_segment];
    const std::size_t active_count = std::max<std::size_t>(
        2, static_cast<std::size_t>(std::ceil(segment_progress * (active.size() - 1))) + 1);
    std::vector<cv::Point> partial(active.begin(),
        active.begin() + static_cast<std::ptrdiff_t>(std::min(active_count, active.size())));
    if (partial.size() >= 2) {
        cv::polylines(frame, partial, false, kMapBlue, 2, cv::LINE_AA);
        cv::polylines(route_glow, partial, false, kMapBlue, 9, cv::LINE_AA);
    }
    cv::Mat bloom;
    cv::GaussianBlur(route_glow, bloom, {13, 13}, 0.0);
    cv::addWeighted(frame, 1.0, bloom, 0.11, 0.0, frame);

    for (std::size_t i = 0; i < stops.size(); ++i) {
        const auto [x, y] = geo_to_screen(stops[i].latitude, stops[i].longitude,
                                          camera_anchor, zoom,
                                          frame.cols, frame.rows);
        const cv::Point point(x, y);
        if (!altitude_m.empty()) {
            const double altitude = altitude_m[std::min(i, altitude_m.size() - 1)];
            const int stem = std::clamp(static_cast<int>(std::nearbyint(14.0 + altitude / 1000.0 * 5.0)),
                                        14, 78);
            const cv::Point top(point.x, point.y - stem);
            cv::line(frame, point, top, cv::Scalar(186, 224, 238), 2, cv::LINE_AA);
            cv::line(frame, top, {top.x + 15, top.y}, cv::Scalar(186, 224, 238), 2, cv::LINE_AA);
        }
        cv::circle(frame, point, 5, kPaper, cv::FILLED, cv::LINE_AA);
        cv::circle(frame, point, 3, kMapBlue, cv::FILLED, cv::LINE_AA);
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
    cv::circle(frame, head, 4, kPaper, cv::FILLED, cv::LINE_AA);
    cv::circle(frame, head, 2, kMapBlue, cv::FILLED, cv::LINE_AA);
}

}  // namespace chrononplugin::maps
