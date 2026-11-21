#include "chrononplugin/maps/map_story_effects.hpp"

#include "map_story_effects_internal.hpp"

#include "chrononplugin/maps/map_camera.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

namespace chrononplugin::maps::story_detail {

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

void draw_location_tag(cv::Mat& frame, cv::Point anchor,
                      const std::string& label, double opacity) {
    if (label.empty() || opacity <= 0.0) return;
    const double scale = std::clamp(frame.rows / 1080.0, 0.55, 1.5);
    const int pad = cvRound(12.0 * scale);
    const int thickness = std::max(1, cvRound(2.0 * scale));
    int baseline = 0;
    const auto extent = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX,
                                        0.82 * scale, thickness, &baseline);
    const int width = extent.width + 2 * pad;
    const int height = extent.height + 2 * pad;
    int x = anchor.x + cvRound(34.0 * scale);
    int y = anchor.y - cvRound(68.0 * scale);
    x = std::clamp(x, 8, std::max(8, frame.cols - width - 8));
    y = std::clamp(y, 8, std::max(8, frame.rows - height - 8));
    cv::Mat overlay = frame.clone();
    const cv::Point elbow(anchor.x + cvRound(18.0 * scale),
                          anchor.y - cvRound(22.0 * scale));
    const cv::Point join(x, y + height / 2);
    cv::line(overlay, anchor, elbow, cv::Scalar(226, 238, 247),
             std::max(1, cvRound(1.5 * scale)), cv::LINE_AA);
    cv::line(overlay, elbow, join, cv::Scalar(226, 238, 247),
             std::max(1, cvRound(1.5 * scale)), cv::LINE_AA);
    cv::rectangle(overlay, {x, y, width, height}, cv::Scalar(19, 27, 34),
                  cv::FILLED, cv::LINE_AA);
    cv::rectangle(overlay, {x, y, width, height}, cv::Scalar(88, 123, 145),
                  1, cv::LINE_AA);
    cv::circle(overlay, anchor, std::max(3, cvRound(4.0 * scale)),
               cv::Scalar(245, 249, 251), cv::FILLED, cv::LINE_AA);
    cv::putText(overlay, label, {x + pad, y + pad + extent.height},
                cv::FONT_HERSHEY_SIMPLEX, 0.82 * scale,
                cv::Scalar(247, 249, 250), thickness, cv::LINE_AA);
    cv::addWeighted(frame, 1.0 - opacity, overlay, opacity, 0.0, frame);
}

cv::Rect draw_reference_label(cv::Mat& frame, const std::string& text,
                              cv::Point origin, cv::Scalar fill,
                              cv::Scalar ink) {
    const double scale = frame.cols / 1920.0;
    const int pad_x = std::max(12, cvRound(15.0 * scale));
    const int pad_y = std::max(7, cvRound(9.0 * scale));
    const int thickness = std::max(1, cvRound(2.0 * scale));
    const double font_scale = 1.1 * scale;
    int baseline = 0;
    const cv::Size size = cv::getTextSize(text,
        cv::FONT_HERSHEY_COMPLEX | cv::FONT_ITALIC, font_scale, thickness, &baseline);
    cv::Rect box(origin.x, origin.y, size.width + 2 * pad_x,
                 size.height + 2 * pad_y);
    box.x = std::clamp(box.x, 8, std::max(8, frame.cols - box.width - 8));
    box.y = std::clamp(box.y, 8, std::max(8, frame.rows - box.height - 8));
    cv::rectangle(frame, box, fill, cv::FILLED, cv::LINE_AA);
    cv::putText(frame, text, {box.x + pad_x, box.y + pad_y + size.height},
                cv::FONT_HERSHEY_COMPLEX | cv::FONT_ITALIC, font_scale,
                ink, thickness, cv::LINE_AA);
    return box;
}

cv::Mat territory_mask(const cv::Mat& frame,
                       const std::vector<GeoPolygon>& polygons,
                       std::pair<double, double> anchor, double zoom) {
    cv::Mat mask(frame.size(), CV_8UC1, cv::Scalar(0));
    std::vector<std::vector<cv::Point>> exteriors;
    std::vector<std::vector<cv::Point>> holes;
    for (const auto& polygon : polygons) {
        for (std::size_t i = 0; i < polygon.rings.size(); ++i) {
            auto points = project_ring(polygon.rings[i], anchor, zoom, frame.size());
            if (points.size() >= 3) (i == 0 ? exteriors : holes).push_back(std::move(points));
        }
    }
    if (!exteriors.empty()) cv::fillPoly(mask, exteriors, cv::Scalar(255), cv::LINE_AA);
    if (!holes.empty()) cv::fillPoly(mask, holes, cv::Scalar(0), cv::LINE_AA);
    return mask;
}
}  // namespace chrononplugin::maps::story_detail
