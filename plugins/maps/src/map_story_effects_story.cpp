// Story callouts and reference treatments (location/mountain/atlas/timeline).
// Split out of map_story_effects.cpp; the raster primitives live in
// map_story_effects_primitives.cpp.

#include "chrononplugin/maps/map_story_effects.hpp"

#include "map_story_effects_internal.hpp"

#include "chrononplugin/maps/map_camera.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

namespace chrononplugin::maps {
using namespace story_detail;

void draw_location_callout(cv::Mat& frame, cv::Point anchor,
                           const std::string& label, double opacity) {
    draw_location_tag(frame, anchor, label, opacity);
}

void draw_mountain_ridge_callout(cv::Mat& frame, const cv::Mat& mountain_overlay,
                                 double progress, const std::string& label,
                                 bool show_text) {
    if (mountain_overlay.empty() || mountain_overlay.channels() != 4) return;
    progress = std::clamp(progress, 0.0, 1.0);
    const double scale = frame.cols / 1920.0;
    const int width = cvRound(1510.0 * scale);
    const int height = cvRound(width * static_cast<double>(mountain_overlay.rows) /
                               mountain_overlay.cols);
    cv::Mat relief;
    cv::resize(mountain_overlay, relief, {width, height}, 0.0, 0.0, cv::INTER_AREA);
    const int x0 = (frame.cols - width) / 2;
    const int y0 = cvRound(370.0 * scale);
    const double opacity = progress * progress * (3.0 - 2.0 * progress);
    for (int y = 0; y < relief.rows; ++y) {
        const int dy = y0 + y;
        if (dy < 0 || dy >= frame.rows) continue;
        const cv::Vec4b* src = relief.ptr<cv::Vec4b>(y);
        cv::Vec3b* dst = frame.ptr<cv::Vec3b>(dy);
        for (int x = 0; x < relief.cols; ++x) {
            const int dx = x0 + x;
            if (dx < 0 || dx >= frame.cols) continue;
            const double alpha = (src[x][3] / 255.0) * opacity;
            if (alpha < 0.005) continue;
            for (int c = 0; c < 3; ++c) {
                dst[dx][c] = cv::saturate_cast<unsigned char>(
                    dst[dx][c] * (1.0 - alpha) + src[x][c] * alpha);
            }
        }
    }
    if (show_text) {
        const std::string title = label.empty() ? "Monti Tauro" : label;
        int baseline = 0;
        const double font = 1.65 * scale;
        const int thickness = std::max(3, cvRound(4.0 * scale));
        const cv::Size size = cv::getTextSize(title, cv::FONT_HERSHEY_SIMPLEX,
                                              font, thickness, &baseline);
        const cv::Point origin((frame.cols - size.width) / 2, cvRound(190.0 * scale));
        cv::putText(frame, title, origin + cv::Point(2, 3), cv::FONT_HERSHEY_SIMPLEX,
                    font, cv::Scalar(30, 30, 30), thickness + 2, cv::LINE_AA);
        cv::putText(frame, title, origin, cv::FONT_HERSHEY_SIMPLEX,
                    font, cv::Scalar(252, 252, 249), thickness, cv::LINE_AA);
    }
    std::vector<cv::Point> arrow{{cvRound(1110.0 * scale), cvRound(265.0 * scale)},
        {cvRound(1165.0 * scale), cvRound(300.0 * scale)},
        {cvRound(1235.0 * scale), cvRound(350.0 * scale)},
        {cvRound(1320.0 * scale), cvRound(430.0 * scale)}};
    cv::polylines(frame, arrow, false, cv::Scalar(28, 29, 30),
                  std::max(5, cvRound(7.0 * scale)), cv::LINE_AA);
    cv::polylines(frame, arrow, false, cv::Scalar(250, 250, 248),
                  std::max(2, cvRound(3.0 * scale)), cv::LINE_AA);
    cv::arrowedLine(frame, arrow[2], arrow.back(), cv::Scalar(250, 250, 248),
                    std::max(2, cvRound(3.0 * scale)), cv::LINE_AA,
                    0, 0.36);
}

void draw_continental_atlas(cv::Mat& frame,
                            const GeoFeatureCollection& countries,
                            std::pair<double, double> camera_anchor,
                            double zoom, double progress,
                            const std::string& focus_country,
                            const std::vector<cv::Point>& label_points,
                            const std::vector<std::string>& labels) {
    cv::Mat gray, gray_bgr, sea_tint(frame.size(), CV_8UC3, cv::Scalar(128, 72, 30));
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    cv::cvtColor(gray, gray_bgr, cv::COLOR_GRAY2BGR);
    cv::addWeighted(gray_bgr, 0.34, sea_tint, 0.66, 0.0, frame);
    const double draw_progress = std::clamp(progress * 1.8, 0.0, 1.0);
    cv::Mat land_mask(frame.size(), CV_8UC1, cv::Scalar(0));
    for (const auto& feature : countries.features) {
        const auto name_it = feature.properties.find("ADMIN");
        const auto continent_it = feature.properties.find("CONTINENT");
        const bool focus = name_it != feature.properties.end() &&
                           name_it->second == focus_country;
        cv::Scalar fill(91, 77, 64);
        if (continent_it != feature.properties.end()) {
            if (continent_it->second == "Europe") fill = cv::Scalar(196, 174, 53);
            else if (continent_it->second == "Asia") fill = cv::Scalar(65, 142, 44);
            else if (continent_it->second == "Africa") fill = cv::Scalar(112, 128, 147);
        }
        if (focus) fill = cv::Scalar(39, 51, 174);
        for (const auto& polygon : feature.polygons) {
            if (polygon.rings.empty()) continue;
            auto exterior = project_ring(polygon.rings.front(), camera_anchor,
                                         zoom, frame.size());
            if (exterior.size() < 3) continue;
            cv::fillPoly(land_mask, std::vector<std::vector<cv::Point>>{exterior},
                         cv::Scalar(255), cv::LINE_AA);
            if (focus) {
                cv::Mat mask(frame.size(), CV_8UC1, cv::Scalar(0));
                cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{exterior},
                             cv::Scalar(255), cv::LINE_AA);
                cv::Mat color(frame.size(), CV_8UC3, fill);
                cv::Mat blended;
                cv::addWeighted(frame, 1.0 - 0.96 * draw_progress,
                                color, 0.96 * draw_progress, 0.0, blended);
                blended.copyTo(frame, mask);
            } else {
                cv::fillPoly(frame, std::vector<std::vector<cv::Point>>{exterior},
                             fill, cv::LINE_AA);
            }
            cv::polylines(frame, exterior, true,
                          cv::Scalar(208, 198, 176),
                          std::max(1, cvRound(frame.rows / 900.0)), cv::LINE_AA);
        }
    }
    cv::Mat hatch(frame.size(), CV_8UC1, cv::Scalar(0));
    const int spacing = std::max(10, cvRound(14.0 * frame.cols / 1920.0));
    for (int offset = -frame.rows; offset < frame.cols; offset += spacing) {
        cv::line(hatch, {offset, 0}, {offset + frame.rows, frame.rows - 1},
                 cv::Scalar(255), std::max(1, cvRound(frame.cols / 1600.0)), cv::LINE_AA);
    }
    cv::bitwise_and(hatch, land_mask, hatch);
    cv::Mat hatch_color;
    cv::addWeighted(frame, 0.84,
                    cv::Mat(frame.size(), CV_8UC3, cv::Scalar(219, 224, 222)),
                    0.16, 0.0, hatch_color);
    hatch_color.copyTo(frame, hatch);
    for (std::size_t i = 0; i < label_points.size() && i < labels.size(); ++i) {
        const std::string& label = labels[i];
        const cv::Point point = label_points[i];
        if (label == "TURCHIA" || label == "TURKEY") {
            const cv::Rect box = draw_reference_label(frame, label,
                {point.x - cvRound(390.0 * frame.cols / 1920.0),
                 point.y - cvRound(250.0 * frame.rows / 1080.0)},
                cv::Scalar(32, 226, 248), cv::Scalar(32, 32, 30));
            const cv::Point start(box.x + box.width - cvRound(12.0 * frame.cols / 1920.0),
                                  box.y + box.height - 2);
            const cv::Point mid(start.x + cvRound(72.0 * frame.cols / 1920.0),
                                start.y + cvRound(78.0 * frame.rows / 1080.0));
            cv::line(frame, start, mid, cv::Scalar(249, 248, 242),
                     std::max(2, cvRound(3.0 * frame.cols / 1920.0)), cv::LINE_AA);
            cv::arrowedLine(frame, mid, point, cv::Scalar(249, 248, 242),
                            std::max(2, cvRound(3.0 * frame.cols / 1920.0)),
                            cv::LINE_AA, 0, 0.35);
        } else if (label == "EUROPA" || label == "EUROPE") {
            draw_reference_label(frame, label,
                {point.x - cvRound(180.0 * frame.cols / 1920.0),
                 point.y - cvRound(245.0 * frame.rows / 1080.0)},
                cv::Scalar(244, 244, 239), cv::Scalar(42, 45, 46));
        } else {
            draw_reference_label(frame, label,
                {point.x + cvRound(24.0 * frame.cols / 1920.0),
                 point.y - cvRound(105.0 * frame.rows / 1080.0)},
                cv::Scalar(42, 64, 194), cv::Scalar(248, 248, 246));
        }
    }
}

void draw_historic_location_timeline(cv::Mat& frame,
                                     const std::vector<GeoPolygon>& country,
                                     std::pair<double, double> camera_anchor,
                                     double zoom, double progress,
                                     cv::Point location,
                                     const std::string& city,
                                     const std::string& date_label,
                                     bool show_text) {
    progress = std::clamp(progress, 0.0, 1.0);
    cv::Mat gray, gray_bgr, tint(frame.size(), CV_8UC3, cv::Scalar(55, 44, 31));
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    cv::cvtColor(gray, gray_bgr, cv::COLOR_GRAY2BGR);
    cv::addWeighted(gray_bgr, 0.84, tint, 0.16, 0.0, gray_bgr);
    gray_bgr.convertTo(frame, -1, 0.62, 0.0);
    for (const auto& polygon : country) {
        for (std::size_t r = 0; r < polygon.rings.size(); ++r) {
            auto points = project_ring(polygon.rings[r], camera_anchor, zoom, frame.size());
            if (points.size() < 3) continue;
            cv::polylines(frame, points, true, cv::Scalar(192, 211, 221),
                          std::max(1, cvRound(frame.rows / 540.0)), cv::LINE_AA);
        }
    }
    cv::Mat edge(frame.size(), CV_8UC3, cv::Scalar(0, 0, 0));
    cv::circle(edge, location, cvRound(28.0 * frame.rows / 1080.0),
               cv::Scalar(29, 185, 246), cv::FILLED, cv::LINE_AA);
    cv::GaussianBlur(edge, edge, {0, 0}, 12.0 * frame.rows / 1080.0);
    cv::addWeighted(frame, 1.0, edge, 0.48 * progress, 0.0, frame);
    draw_location_tag(frame, location, city, std::clamp(progress * 3.0, 0.0, 1.0));

    const int margin = cvRound(120.0 * frame.cols / 1920.0);
    const int y = frame.rows - cvRound(108.0 * frame.rows / 1080.0);
    const int right = frame.cols - margin;
    cv::line(frame, {margin, y}, {right, y}, cv::Scalar(102, 111, 118),
             std::max(1, cvRound(frame.rows / 540.0)), cv::LINE_AA);
    const int marker_x = margin + cvRound((right - margin) * 0.61);
    cv::line(frame, {margin, y}, {marker_x, y}, cv::Scalar(226, 232, 236),
             std::max(2, cvRound(frame.rows / 360.0)), cv::LINE_AA);
    cv::circle(frame, {marker_x, y}, cvRound(7.0 * frame.rows / 1080.0),
               cv::Scalar(247, 249, 250), cv::FILLED, cv::LINE_AA);
    if (show_text && !date_label.empty()) {
        cv::putText(frame, date_label, {margin, y - cvRound(22.0 * frame.rows / 1080.0)},
                    cv::FONT_HERSHEY_SIMPLEX, 0.8 * frame.rows / 1080.0,
                    cv::Scalar(246, 248, 249), std::max(1, cvRound(frame.rows / 540.0)),
                    cv::LINE_AA);
    }
}
}  // namespace chrononplugin::maps
