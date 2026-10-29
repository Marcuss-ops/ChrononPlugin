#include "chrononplugin/maps/map_story_effects.hpp"

#include "chrononplugin/maps/map_camera.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace chrononplugin::maps {
namespace {
constexpr double kPi = 3.14159265358979323846;
const cv::Scalar kAmber(35, 196, 255);
const cv::Scalar kCyan(255, 222, 107);

void line(cv::Mat& frame, cv::Point a, cv::Point b, cv::Scalar color,
          int thickness = 1) {
    cv::line(frame, a, b, color, thickness, cv::LINE_AA);
}

void draw_bracket(cv::Mat& frame, cv::Point center, int radius, int length,
                  cv::Scalar color) {
    for (int sx : {-1, 1}) for (int sy : {-1, 1}) {
        const cv::Point corner(center.x + sx * radius, center.y + sy * radius);
        line(frame, corner, {corner.x - sx * length, corner.y}, color, 2);
        line(frame, corner, {corner.x, corner.y - sy * length}, color, 2);
    }
}

void put_hud(cv::Mat& frame, const std::string& text, cv::Point origin,
             double scale = 0.55) {
    cv::putText(frame, text, origin, cv::FONT_HERSHEY_DUPLEX, scale,
                cv::Scalar(10, 17, 21), 4, cv::LINE_AA);
    cv::putText(frame, text, origin, cv::FONT_HERSHEY_DUPLEX, scale,
                cv::Scalar(230, 242, 237), 1, cv::LINE_AA);
}

std::vector<cv::Point> project_ring(const std::vector<GeoPosition>& ring,
                                    std::pair<double, double> anchor,
                                    double zoom, cv::Size size) {
    std::vector<cv::Point> points;
    points.reserve(ring.size());
    for (const auto& position : ring) {
        const auto [x, y] = geo_to_screen(position[1], position[0], anchor, zoom,
                                          size.width, size.height);
        points.emplace_back(x, y);
    }
    return points;
}
}  // namespace

void draw_satellite_lock(cv::Mat& frame, std::pair<double, double> target,
                         std::pair<double, double> camera_anchor, double zoom,
                         double progress, const std::string& label,
                         bool show_telemetry) {
    progress = std::clamp(progress, 0.0, 1.0);
    const auto [x, y] = geo_to_screen(target.first, target.second, camera_anchor,
                                      zoom, frame.cols, frame.rows);
    const cv::Point center(x, y);
    const int radius = static_cast<int>(std::nearbyint(112.0 - 78.0 * progress));
    const int len = static_cast<int>(std::nearbyint(24.0 - 10.0 * progress));
    draw_bracket(frame, center, radius, len, kAmber);
    const int cross = std::max(8, 34 - static_cast<int>(progress * 23.0));
    line(frame, {x - cross, y}, {x - 5, y}, cv::Scalar(235, 243, 243));
    line(frame, {x + 5, y}, {x + cross, y}, cv::Scalar(235, 243, 243));
    line(frame, {x, y - cross}, {x, y - 5}, cv::Scalar(235, 243, 243));
    line(frame, {x, y + 5}, {x, y + cross}, cv::Scalar(235, 243, 243));
    cv::circle(frame, center, 3, kAmber, cv::FILLED, cv::LINE_AA);

    const double lat_rad = target.first * kPi / 180.0;
    const double mpp = 156543.03392 * std::cos(lat_rad) / std::pow(2.0, zoom);
    const double altitude_km = mpp * frame.rows * 0.95 / 1000.0;
    if (show_telemetry) {
        char ticker[256];
        std::snprintf(ticker, sizeof(ticker), "LAT %+.4f   LON %+.4f   ALT %.2f km   GSD %.1f m/px",
                      target.first, target.second, altitude_km, mpp);
        const int base_y = frame.rows - 28;
        cv::rectangle(frame, {18, base_y - 30}, {frame.cols - 18, frame.rows - 12},
                      cv::Scalar(10, 17, 21), cv::FILLED);
        cv::rectangle(frame, {18, base_y - 30}, {frame.cols - 18, frame.rows - 12},
                      cv::Scalar(79, 99, 91), 1, cv::LINE_AA);
        put_hud(frame, ticker, {30, base_y - 7}, 0.48);
    }
    put_hud(frame, label, {30, 36}, 0.68);
    if (show_telemetry)
        put_hud(frame, progress >= 0.985 ? "TARGET LOCK" : "ACQUIRING TARGET",
                {frame.cols - 260, 36}, 0.48);
}

void draw_crime_scene(cv::Mat& frame, std::pair<double, double> target,
                      std::pair<double, double> camera_anchor, double zoom,
                      double radius_m, double progress, int frame_index,
                      const std::string& label) {
    cv::Mat gray, blue, green, red, graded;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    gray.convertTo(blue, CV_8U, 1.12);
    gray.convertTo(green, CV_8U, 0.91);
    gray.convertTo(red, CV_8U, 0.72);
    std::vector<cv::Mat> channels{blue, green, red};
    cv::merge(channels, graded);
    cv::addWeighted(graded, 0.92, frame, 0.08, 0.0, frame);
    frame.convertTo(frame, -1, 0.78, 0.0);

    const auto [x, y] = geo_to_screen(target.first, target.second,
        camera_anchor, zoom, frame.cols, frame.rows);
    const double mpp = 156543.03392 * std::cos(target.first * kPi / 180.0) /
                       std::pow(2.0, zoom);
    const int radius = std::clamp(static_cast<int>(std::nearbyint(radius_m / mpp)), 12,
                                  std::max(frame.cols, frame.rows));
    cv::Mat circle_mask(frame.size(), CV_8UC1, cv::Scalar(0));
    cv::circle(circle_mask, {x, y}, radius, cv::Scalar(255), cv::FILLED, cv::LINE_AA);
    cv::Mat hatch(frame.size(), CV_8UC3, cv::Scalar(0, 0, 0));
    for (int d = -frame.rows; d < frame.cols + frame.rows; d += 16)
        line(hatch, {d, frame.rows}, {d + frame.rows, 0}, cv::Scalar(113, 126, 76), 1);
    cv::Mat hatch_masked;
    hatch.copyTo(hatch_masked, circle_mask);
    cv::addWeighted(frame, 1.0, hatch_masked, 0.30, 0.0, frame);

    cv::Mat rings(frame.size(), CV_8UC3, cv::Scalar(0, 0, 0));
    for (int ring = 0; ring < 3; ++ring) {
        const double phase = std::fmod(progress * 2.8 + ring / 3.0, 1.0);
        const int ring_radius = std::max(3, static_cast<int>(phase * radius));
        cv::circle(rings, {x, y}, ring_radius, kCyan, 2, cv::LINE_AA);
    }
    cv::addWeighted(frame, 1.0, rings, 0.58, 0.0, frame);

    for (int degree = 0; degree < 360; degree += 18) {
        const int end = std::min(360, degree + 10);
        cv::ellipse(frame, {x, y}, {radius, radius}, 0.0, degree, end,
                    kAmber, 2, cv::LINE_AA);
    }
    const int pulse = 8 + (frame_index * 3) % std::max(12, radius);
    cv::circle(frame, {x, y}, pulse, kCyan, 2, cv::LINE_AA);
    cv::circle(frame, {x, y}, 6, cv::Scalar(235, 245, 243), cv::FILLED, cv::LINE_AA);
    char caption[160];
    std::snprintf(caption, sizeof(caption), "%s  /  RAGGIO %.1f km  /  %d MIN A PIEDI",
                  label.c_str(), radius_m / 1000.0,
                  static_cast<int>(std::nearbyint(radius_m / 1000.0 * 12.0)));
    put_hud(frame, caption, {28, 42}, 0.62);
}

void draw_territory_wipe(cv::Mat& frame, const std::vector<GeoPolygon>& polygons,
                         std::pair<double, double> camera_anchor, double zoom,
                         double progress, const std::string& label) {
    if (polygons.empty()) return;
    progress = std::clamp(progress, 0.0, 1.0);
    cv::Mat mask(frame.size(), CV_8UC1, cv::Scalar(0));
    std::vector<std::vector<cv::Point>> exteriors;
    std::vector<std::vector<cv::Point>> holes;
    std::vector<std::vector<cv::Point>> every_ring;
    for (const auto& polygon : polygons) {
        if (polygon.rings.empty()) continue;
        for (std::size_t r = 0; r < polygon.rings.size(); ++r) {
            auto points = project_ring(polygon.rings[r], camera_anchor, zoom, frame.size());
            if (points.size() < 3) continue;
            every_ring.push_back(points);
            if (r == 0) exteriors.push_back(points);
            else holes.push_back(points);
        }
    }
    if (exteriors.empty()) return;
    cv::fillPoly(mask, exteriors, cv::Scalar(255), cv::LINE_AA);
    cv::fillPoly(mask, holes, cv::Scalar(0), cv::LINE_AA);

    cv::Mat dimmed;
    frame.convertTo(dimmed, -1, 0.86, 0.0);
    cv::Mat outside;
    cv::bitwise_not(mask, outside);
    dimmed.copyTo(frame, outside);

    cv::Mat tint(frame.size(), CV_8UC3, cv::Scalar(112, 137, 82));
    cv::Mat blended;
    cv::addWeighted(frame, 0.86, tint, 0.14, 0.0, blended);
    blended.copyTo(frame, mask);

    cv::Mat glow(frame.size(), CV_8UC3, cv::Scalar(0, 0, 0));
    std::size_t total = 0;
    for (const auto& ring : every_ring) total += ring.size();
    const std::size_t revealed = static_cast<std::size_t>(std::ceil(total * progress));
    std::size_t consumed = 0;
    for (const auto& ring : every_ring) {
        const std::size_t count = ring.size() > 1 ? ring.size() - 1 : 0;
        const std::size_t visible = std::min(count, revealed > consumed ? revealed - consumed : 0);
        if (visible > 1) {
            std::vector<cv::Point> partial(ring.begin(), ring.begin() +
                static_cast<std::ptrdiff_t>(visible + 1));
            cv::polylines(glow, partial, false, cv::Scalar(160, 190, 119), 5, cv::LINE_AA);
            cv::polylines(frame, partial, false, cv::Scalar(188, 218, 154), 2, cv::LINE_AA);
        }
        consumed += count;
    }
    cv::Mat bloom;
    cv::GaussianBlur(glow, bloom, {19, 19}, 0.0);
    cv::addWeighted(frame, 1.0, bloom, 0.28, 0.0, frame);
    put_hud(frame, "TERRITORIO // " + label, {28, 38}, 0.66);
}

void draw_area_glow(cv::Mat& frame, cv::Point center, double radius_pixels,
                    double progress) {
    const double radius = std::clamp(radius_pixels, 8.0,
        static_cast<double>(std::max(frame.cols, frame.rows)));
    const double breath = 0.95 + 0.05 * std::sin(2.0 * kPi * progress);
    const int r = std::max(8, cvRound(radius * breath));

    cv::Mat fill_mask(frame.size(), CV_8UC1, cv::Scalar(0));
    cv::circle(fill_mask, center, r, cv::Scalar(255), cv::FILLED, cv::LINE_AA);
    cv::Mat tint(frame.size(), CV_8UC3, cv::Scalar(112, 137, 82));
    cv::Mat tinted;
    cv::addWeighted(frame, 0.92, tint, 0.08, 0.0, tinted);
    tinted.copyTo(frame, fill_mask);

    cv::Mat glow(frame.size(), CV_8UC3, cv::Scalar(0, 0, 0));
    cv::circle(glow, center, r, cv::Scalar(150, 192, 123), cv::FILLED, cv::LINE_AA);
    cv::GaussianBlur(glow, glow, {0, 0}, std::max(2.0, radius * 0.18));
    cv::addWeighted(frame, 1.0, glow, 0.18, 0.0, frame);
    cv::circle(frame, center, r, cv::Scalar(184, 215, 151), 2, cv::LINE_AA);
    cv::circle(frame, center, 4, cv::Scalar(230, 238, 219), cv::FILLED, cv::LINE_AA);
}

void apply_documentary_finish(cv::Mat& frame, int frame_index,
                              double vignette_strength, double grain_strength) {
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
