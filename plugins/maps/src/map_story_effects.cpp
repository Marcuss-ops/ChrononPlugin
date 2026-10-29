#include "chrononplugin/maps/map_story_effects.hpp"

#include "chrononplugin/maps/map_camera.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <map>

namespace chrononplugin::maps {
namespace {
constexpr double kPi = 3.14159265358979323846;
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

void draw_territory_wipe(cv::Mat& frame, const std::vector<GeoPolygon>& polygons,
                         std::pair<double, double> camera_anchor, double zoom,
                         double progress) {
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
    frame.convertTo(dimmed, -1, 0.96, 0.0);
    cv::Mat outside;
    cv::bitwise_not(mask, outside);
    dimmed.copyTo(frame, outside);

    cv::Mat tint(frame.size(), CV_8UC3, cv::Scalar(210, 151, 96));
    cv::Mat blended;
    cv::addWeighted(frame, 0.88, tint, 0.12, 0.0, blended);
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
            cv::polylines(glow, partial, false, cv::Scalar(220, 162, 96), 4, cv::LINE_AA);
            cv::polylines(frame, partial, false, cv::Scalar(250, 218, 170), 1, cv::LINE_AA);
        }
        consumed += count;
    }
    cv::Mat bloom;
    cv::GaussianBlur(glow, bloom, {13, 13}, 0.0);
    cv::addWeighted(frame, 1.0, bloom, 0.16, 0.0, frame);
}

void draw_territory_callout(cv::Mat& frame, cv::Point anchor,
                            const std::string& place_name) {
    if (place_name.empty()) return;
    const cv::Point elbow(anchor.x + 24, anchor.y - 34);
    const cv::Point label_start(anchor.x + 44, anchor.y - 34);
    cv::line(frame, anchor, elbow, cv::Scalar(238, 171, 105), 2, cv::LINE_AA);
    cv::line(frame, elbow, label_start, cv::Scalar(246, 243, 235), 2, cv::LINE_AA);
    cv::circle(frame, anchor, 4, cv::Scalar(246, 249, 252), cv::FILLED, cv::LINE_AA);
    cv::circle(frame, anchor, 5, cv::Scalar(240, 164, 91), 1, cv::LINE_AA);
    cv::putText(frame, place_name, label_start + cv::Point(5, 4),
                cv::FONT_HERSHEY_SIMPLEX, 0.61, cv::Scalar(18, 22, 27),
                3, cv::LINE_AA);
    cv::putText(frame, place_name, label_start + cv::Point(5, 4),
                cv::FONT_HERSHEY_SIMPLEX, 0.61, cv::Scalar(250, 251, 253),
                1, cv::LINE_AA);
}

void draw_area_glow(cv::Mat& frame, cv::Point center, double radius_pixels,
                    double progress, const std::string& label) {
    const double radius = std::clamp(radius_pixels, 8.0,
        static_cast<double>(std::max(frame.cols, frame.rows)));
    const double breath = 0.95 + 0.05 * std::sin(2.0 * kPi * progress);
    const int r = std::max(8, cvRound(radius * breath));

    cv::Mat fill_mask(frame.size(), CV_8UC1, cv::Scalar(0));
    cv::circle(fill_mask, center, r, cv::Scalar(255), cv::FILLED, cv::LINE_AA);
    cv::Mat tint(frame.size(), CV_8UC3, cv::Scalar(210, 151, 96));
    cv::Mat tinted;
    cv::addWeighted(frame, 0.90, tint, 0.10, 0.0, tinted);
    tinted.copyTo(frame, fill_mask);

    cv::Mat glow(frame.size(), CV_8UC3, cv::Scalar(0, 0, 0));
    cv::circle(glow, center, r, cv::Scalar(230, 163, 92), cv::FILLED, cv::LINE_AA);
    cv::GaussianBlur(glow, glow, {0, 0}, std::max(2.0, radius * 0.22));
    cv::addWeighted(frame, 1.0, glow, 0.11, 0.0, frame);
    cv::circle(frame, center, r, cv::Scalar(245, 198, 130), 1, cv::LINE_AA);
    cv::circle(frame, center, 4, cv::Scalar(246, 249, 252), cv::FILLED, cv::LINE_AA);
    cv::circle(frame, center, 2, cv::Scalar(255, 142, 66), cv::FILLED, cv::LINE_AA);
    draw_territory_callout(frame, center, label);
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
