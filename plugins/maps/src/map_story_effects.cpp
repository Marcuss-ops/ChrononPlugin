#include "chrononplugin/maps/map_story_effects.hpp"

#include "chrononplugin/maps/map_camera.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>

namespace chrononplugin::maps {
namespace {
constexpr double kPi = 3.14159265358979323846;
// Consistent warm red territory palette (OpenCV colors are BGR).
const cv::Scalar kTerritoryRed(48, 59, 255);
const cv::Scalar kTerritoryGlow(28, 36, 220);
const cv::Scalar kTerritoryGlowInner(64, 75, 255);
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
                      const std::string& label, double opacity = 1.0) {
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
}  // namespace

void draw_location_callout(cv::Mat& frame, cv::Point anchor,
                           const std::string& label, double opacity) {
    draw_location_tag(frame, anchor, label, opacity);
}

void draw_mountain_ridge_callout(cv::Mat& frame, const cv::Mat& mountain_overlay,
                                 double progress, const std::string& label) {
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
                                     const std::string& date_label) {
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
    cv::putText(frame, date_label, {margin, y - cvRound(22.0 * frame.rows / 1080.0)},
                cv::FONT_HERSHEY_SIMPLEX, 0.8 * frame.rows / 1080.0,
                cv::Scalar(246, 248, 249), std::max(1, cvRound(frame.rows / 540.0)),
                cv::LINE_AA);
}

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

    cv::Mat gray, gray_bgr, desaturated, dimmed;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    cv::cvtColor(gray, gray_bgr, cv::COLOR_GRAY2BGR);
    cv::addWeighted(frame, 0.58, gray_bgr, 0.42, 0.0, desaturated);
    desaturated.convertTo(dimmed, -1, 0.72, 0.0);
    cv::Mat outside;
    cv::bitwise_not(mask, outside);
    dimmed.copyTo(frame, outside);

    cv::Mat tint(frame.size(), CV_8UC3, kTerritoryRed);
    cv::Mat blended;
    const double fill_alpha = 0.17 * progress;
    cv::addWeighted(frame, 1.0 - fill_alpha, tint, fill_alpha, 0.0, blended);
    blended.copyTo(frame, mask);

    cv::Mat broad_glow(frame.size(), CV_8UC3, cv::Scalar(0, 0, 0));
    cv::Mat inner_glow(frame.size(), CV_8UC3, cv::Scalar(0, 0, 0));
    const double scale = static_cast<double>(frame.rows) / 720.0;
    const int broad_width = std::max(8, cvRound(16.0 * scale));
    const int inner_width = std::max(4, cvRound(6.0 * scale));
    const int core_width = std::max(2, cvRound(3.0 * scale));
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
            cv::polylines(broad_glow, partial, false,
                          kTerritoryGlow, broad_width, cv::LINE_AA);
            cv::polylines(inner_glow, partial, false,
                          kTerritoryGlowInner, inner_width, cv::LINE_AA);
            cv::polylines(frame, partial, false,
                          cv::Scalar(250, 249, 244), core_width, cv::LINE_AA);
        }
        consumed += count;
    }
    cv::Mat broad_bloom, inner_bloom;
    cv::GaussianBlur(broad_glow, broad_bloom, {0, 0}, 11.0 * scale);
    cv::addWeighted(frame, 1.0, broad_bloom, 0.34 * progress, 0.0, frame);
    cv::GaussianBlur(inner_glow, inner_bloom, {0, 0}, 4.0 * scale);
    cv::addWeighted(frame, 1.0, inner_bloom, 0.30 * progress, 0.0, frame);
}

void draw_territory_callout(cv::Mat& frame, cv::Point anchor,
                            const std::string& place_name, double opacity) {
    if (place_name.empty()) return;
    opacity = std::clamp(opacity, 0.0, 1.0);
    if (opacity <= 0.0) return;
    cv::Mat overlay = frame.clone();
    const double scale = static_cast<double>(frame.rows) / 1080.0;
    const int pad_x = std::max(10, cvRound(15.0 * scale));
    const int pad_y = std::max(7, cvRound(10.0 * scale));
    const double font_scale = 1.16 * scale;
    const int font_thickness = std::max(1, cvRound(2.0 * scale));
    int baseline = 0;
    const cv::Size text_size = cv::getTextSize("Provincia di " + place_name,
        cv::FONT_HERSHEY_SIMPLEX, font_scale, font_thickness, &baseline);
    const int card_width = text_size.width + 2 * pad_x;
    const int card_height = text_size.height + 2 * pad_y;
    const cv::Point card_origin(anchor.x + cvRound(60.0 * scale),
                                anchor.y - cvRound(82.0 * scale));
    const cv::Rect card(card_origin.x, card_origin.y, card_width, card_height);
    const cv::Point elbow(anchor.x + cvRound(30.0 * scale),
                          anchor.y - cvRound(42.0 * scale));
    const cv::Point join(card.x, card.y + card.height / 2);
    cv::line(overlay, anchor, elbow, kTerritoryRed,
             std::max(2, cvRound(3.0 * scale)), cv::LINE_AA);
    cv::line(overlay, elbow, join, cv::Scalar(249, 248, 242),
             std::max(2, cvRound(2.5 * scale)), cv::LINE_AA);
    cv::rectangle(overlay, card, cv::Scalar(26, 35, 45), cv::FILLED, cv::LINE_AA);
    cv::rectangle(overlay, card, kTerritoryRed,
                  std::max(1, cvRound(scale)), cv::LINE_AA);
    cv::circle(overlay, anchor, std::max(6, cvRound(8.0 * scale)),
               cv::Scalar(247, 250, 253), cv::FILLED, cv::LINE_AA);
    cv::circle(overlay, anchor, std::max(7, cvRound(10.0 * scale)),
               kTerritoryRed, std::max(2, cvRound(3.0 * scale)),
               cv::LINE_AA);
    const cv::Point text_origin(card.x + pad_x,
        card.y + pad_y + text_size.height);
    cv::putText(overlay, "Provincia di " + place_name, text_origin,
                cv::FONT_HERSHEY_SIMPLEX, font_scale,
                cv::Scalar(251, 252, 253), font_thickness, cv::LINE_AA);
    cv::addWeighted(frame, 1.0 - opacity, overlay, opacity, 0.0, frame);
}

void draw_area_glow(cv::Mat& frame, cv::Point center, double radius_pixels,
                    double progress, const std::string& label) {
    const double radius = std::clamp(radius_pixels, 8.0,
        static_cast<double>(std::max(frame.cols, frame.rows)));
    const double breath = 0.95 + 0.05 * std::sin(2.0 * kPi * progress);
    const int r = std::max(8, cvRound(radius * breath));

    cv::Mat fill_mask(frame.size(), CV_8UC1, cv::Scalar(0));
    cv::circle(fill_mask, center, r, cv::Scalar(255), cv::FILLED, cv::LINE_AA);
    cv::Mat tint(frame.size(), CV_8UC3, kTerritoryRed);
    cv::Mat tinted;
    cv::addWeighted(frame, 0.90, tint, 0.10, 0.0, tinted);
    tinted.copyTo(frame, fill_mask);

    cv::Mat glow(frame.size(), CV_8UC3, cv::Scalar(0, 0, 0));
    cv::circle(glow, center, r, kTerritoryGlow, cv::FILLED, cv::LINE_AA);
    cv::GaussianBlur(glow, glow, {0, 0}, std::max(2.0, radius * 0.22));
    cv::addWeighted(frame, 1.0, glow, 0.11, 0.0, frame);
    cv::circle(frame, center, r, kTerritoryGlowInner, 1, cv::LINE_AA);
    cv::circle(frame, center, 4, cv::Scalar(246, 249, 252), cv::FILLED, cv::LINE_AA);
    cv::circle(frame, center, 2, kTerritoryRed, cv::FILLED, cv::LINE_AA);
    draw_territory_callout(frame, center, label);
}

void draw_radar_reveal(cv::Mat& frame, cv::Point center, double progress,
                       const std::string& label) {
    progress = std::clamp(progress, 0.0, 1.0);
    const double scale = frame.rows / 1080.0;
    const double eased = progress * progress * (3.0 - 2.0 * progress);
    const int radius = std::max(2, cvRound(58.0 * scale * eased));
    cv::Mat ring(frame.size(), CV_8UC3, cv::Scalar(0, 0, 0));
    cv::circle(ring, center, radius, cv::Scalar(207, 225, 237),
               std::max(1, cvRound(1.5 * scale)), cv::LINE_AA);
    cv::GaussianBlur(ring, ring, {0, 0}, 4.0 * scale);
    cv::addWeighted(frame, 1.0, ring, 0.16, 0.0, frame);
    cv::circle(frame, center, radius, cv::Scalar(220, 235, 244),
               std::max(1, cvRound(1.2 * scale)), cv::LINE_AA);
    cv::circle(frame, center, cvRound(7.0 * scale), cv::Scalar(248, 251, 252),
               cv::FILLED, cv::LINE_AA);
    draw_location_tag(frame, center, label, std::clamp(progress * 4.0, 0.0, 1.0));
}

void draw_satellite_scan(cv::Mat& frame, const std::vector<GeoPolygon>& polygons,
                         std::pair<double, double> camera_anchor, double zoom,
                         double progress, const std::string& label) {
    progress = std::clamp(progress, 0.0, 1.0);
    const cv::Mat mask = territory_mask(frame, polygons, camera_anchor, zoom);
    cv::Mat lit = frame.clone();
    cv::Mat tint(frame.size(), CV_8UC3, kTerritoryRed);
    cv::addWeighted(lit, 0.86, tint, 0.14, 0.0, lit);
    cv::Mat reveal(frame.size(), CV_8UC1, cv::Scalar(0));
    const int scan_y = cvRound(progress * frame.rows);
    cv::rectangle(reveal, {0, 0, frame.cols, std::clamp(scan_y, 0, frame.rows)},
                  cv::Scalar(255), cv::FILLED);
    cv::Mat active;
    cv::bitwise_and(mask, reveal, active);
    lit.copyTo(frame, active);
    const int beam_y = std::clamp(scan_y, 0, frame.rows - 1);
    if (progress < 0.98) {
        cv::line(frame, {0, beam_y}, {frame.cols - 1, beam_y},
                 cv::Scalar(224, 241, 249), std::max(1, cvRound(frame.rows / 900.0)), cv::LINE_AA);
    }
    cv::Mat contours;
    cv::Canny(mask, contours, 80, 160);
    cv::Mat edge(frame.size(), CV_8UC3, cv::Scalar(0, 0, 0));
    edge.setTo(kTerritoryGlowInner, contours);
    cv::addWeighted(frame, 1.0, edge, 0.62, 0.0, frame);
    const auto [x, y] = geo_to_screen(camera_anchor.first, camera_anchor.second,
        camera_anchor, zoom, frame.cols, frame.rows);
    draw_location_tag(frame, {x, y}, label, std::clamp(progress * 3.0, 0.0, 1.0));
}

void draw_territory_flood_fill(cv::Mat& frame,
                               const std::vector<GeoPolygon>& polygons,
                               std::pair<double, double> camera_anchor,
                               double zoom, double progress,
                               const std::string& label) {
    progress = std::clamp(progress, 0.0, 1.0);
    const cv::Mat mask = territory_mask(frame, polygons, camera_anchor, zoom);
    cv::Mat tint(frame.size(), CV_8UC3, kTerritoryRed);
    cv::Mat blended;
    cv::addWeighted(frame, 0.86, tint, 0.14, 0.0, blended);
    blended.copyTo(frame, mask);
    cv::Mat edge(frame.size(), CV_8UC3, cv::Scalar(0, 0, 0));
    cv::Mat contours;
    cv::Canny(mask, contours, 80, 160);
    edge.setTo(kTerritoryGlow, contours);
    cv::GaussianBlur(edge, edge, {0, 0}, 6.0 * frame.rows / 1080.0);
    cv::addWeighted(frame, 1.0, edge, 0.28 * progress, 0.0, frame);
    cv::Mat crisp_edge(frame.size(), CV_8UC3, cv::Scalar(0, 0, 0));
    crisp_edge.setTo(kTerritoryGlowInner, contours);
    cv::addWeighted(frame, 1.0, crisp_edge, 0.78 * progress, 0.0, frame);
    const auto [x, y] = geo_to_screen(camera_anchor.first, camera_anchor.second,
        camera_anchor, zoom, frame.cols, frame.rows);
    draw_location_tag(frame, {x, y}, label, std::clamp(progress * 3.0, 0.0, 1.0));
}

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

void draw_light_beam_drop(cv::Mat& frame, cv::Point center, double progress,
                          const std::string& label) {
    progress = std::clamp(progress, 0.0, 1.0);
    const double scale = frame.rows / 1080.0;
    const int radius = cvRound((4.0 + 44.0 * progress) * scale);
    cv::Mat glow(frame.size(), CV_8UC3, cv::Scalar(0, 0, 0));
    cv::circle(glow, center, std::max(2, radius), cv::Scalar(210, 227, 239),
               std::max(1, cvRound(1.5 * scale)), cv::LINE_AA);
    cv::GaussianBlur(glow, glow, {0, 0}, 5.0 * scale);
    cv::addWeighted(frame, 1.0, glow, 0.22, 0.0, frame);
    cv::circle(frame, center, std::max(2, radius), cv::Scalar(225, 238, 246),
               std::max(1, cvRound(1.0 * scale)), cv::LINE_AA);
    cv::circle(frame, center, std::max(3, cvRound(5.0 * scale)),
               cv::Scalar(246, 249, 251), cv::FILLED, cv::LINE_AA);
    draw_location_tag(frame, center, label, std::clamp(progress * 3.0, 0.0, 1.0));
}

void draw_city_network(cv::Mat& frame, const std::vector<cv::Point>& nodes,
                       const std::vector<std::string>& labels, double progress) {
    if (nodes.empty()) return;
    progress = std::clamp(progress, 0.0, 1.0);
    const double scale = frame.rows / 1080.0;
    for (std::size_t i = 1; i < nodes.size(); ++i) {
        const double local = std::clamp(progress * (nodes.size() - 1) - (i - 1), 0.0, 1.0);
        if (local <= 0.0) continue;
        const cv::Point end(cvRound(nodes[0].x + (nodes[i].x - nodes[0].x) * local),
                            cvRound(nodes[0].y + (nodes[i].y - nodes[0].y) * local));
        cv::line(frame, nodes[0], end, cv::Scalar(211, 225, 235),
                 std::max(1, cvRound(2.0 * scale)), cv::LINE_AA);
        cv::circle(frame, end, std::max(3, cvRound(5.0 * scale)),
                   cv::Scalar(240, 246, 250), cv::FILLED, cv::LINE_AA);
        if (local > 0.82 && i < labels.size())
            draw_location_tag(frame, end, labels[i], (local - 0.82) / 0.18);
    }
    cv::circle(frame, nodes[0], std::max(5, cvRound(8.0 * scale)),
               cv::Scalar(250, 252, 253), cv::FILLED, cv::LINE_AA);
    if (!labels.empty()) draw_location_tag(frame, nodes[0], labels[0], progress);
}

void draw_territory_extrusion(cv::Mat& frame,
                              const std::vector<GeoPolygon>& polygons,
                              std::pair<double, double> camera_anchor,
                              double zoom, double progress,
                              const std::string& label) {
    progress = std::clamp(progress, 0.0, 1.0);
    const cv::Mat mask = territory_mask(frame, polygons, camera_anchor, zoom);
    const double scale = frame.rows / 1080.0;
    const int depth = cvRound(8.0 * scale * progress);
    // A small offset shadow separates the filled territory from the satellite.
    cv::Mat shifted;
    const cv::Mat transform = (cv::Mat_<double>(2, 3) << 1, 0, 0,
        0, 1, depth);
    cv::warpAffine(mask, shifted, transform, frame.size(), cv::INTER_NEAREST,
                   cv::BORDER_CONSTANT, cv::Scalar(0));
    const cv::Mat base = frame.clone();
    cv::Mat extrusion = base.clone();
    cv::Mat shade(frame.size(), CV_8UC3, cv::Scalar(83, 94, 103));
    shade.copyTo(extrusion, shifted);
    frame = std::move(extrusion);
    cv::Mat tint(frame.size(), CV_8UC3, kTerritoryRed);
    cv::Mat top;
    cv::addWeighted(base, 0.94, tint, 0.06, 0.0, top);
    top.copyTo(frame, mask);
    cv::Mat contours;
    cv::Canny(mask, contours, 80, 160);
    cv::Mat glow(frame.size(), CV_8UC3, cv::Scalar(0, 0, 0));
    glow.setTo(kTerritoryGlow, contours);
    cv::GaussianBlur(glow, glow, {0, 0}, 3.0 * scale);
    cv::addWeighted(frame, 1.0, glow, 0.2 * progress, 0.0, frame);
    cv::Mat bright(frame.size(), CV_8UC3, cv::Scalar(0, 0, 0));
    bright.setTo(cv::Scalar(246, 248, 249), contours);
    cv::addWeighted(frame, 1.0, bright, 0.85, 0.0, frame);
    const auto [x, y] = geo_to_screen(camera_anchor.first, camera_anchor.second,
        camera_anchor, zoom, frame.cols, frame.rows);
    draw_location_tag(frame, {x, y}, label,
                      std::clamp((progress - 0.55) * 2.2, 0.0, 1.0));
}

void draw_volumetric_laser_scan(cv::Mat& frame,
                                const std::vector<GeoPolygon>& polygons,
                                std::pair<double, double> camera_anchor,
                                double zoom, double progress,
                                const std::string& label) {
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

void draw_vector_particle_flow(cv::Mat& frame,
                               const std::vector<GeoPolygon>& polygons,
                               std::pair<double, double> camera_anchor,
                               double zoom, double progress,
                               const std::string& label) {
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

void draw_dark_mirror_diorama(cv::Mat& frame,
                              const std::vector<GeoPolygon>& polygons,
                              std::pair<double, double> camera_anchor,
                              double zoom, double progress,
                              const std::string& label) {
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

void draw_voronoi_heat_contours(cv::Mat& frame,
                                const std::vector<GeoPolygon>& polygons,
                                std::pair<double, double> camera_anchor,
                                double zoom, double progress,
                                const std::string& label) {
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

void draw_holographic_postfx(cv::Mat& frame,
                             const std::vector<GeoPolygon>& polygons,
                             std::pair<double, double> camera_anchor,
                             double zoom, double progress,
                             const std::string& label) {
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
    cv::putText(distorted, "HOLOGRAPHIC OPTIC // LIVE",
                {cvRound(width * 0.07), cvRound(height * 0.10)},
                cv::FONT_HERSHEY_SIMPLEX, 0.72 * scale, hud,
                std::max(1, cvRound(1.5 * scale)), cv::LINE_AA);
    const int sweep = cvRound(std::fmod(progress * 1.6, 1.0) * height);
    cv::line(distorted, {0, sweep}, {width - 1, sweep}, cv::Scalar(180, 120, 40),
             1, cv::LINE_AA);
    cv::putText(distorted, label.empty() ? "JERUSALEM" : label,
                {cvRound(width * 0.07), cvRound(height * 0.88)},
                cv::FONT_HERSHEY_DUPLEX, 1.35 * scale,
                cv::Scalar(248, 250, 252),
                std::max(2, cvRound(2.0 * scale)), cv::LINE_AA);
    frame = distorted;
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
