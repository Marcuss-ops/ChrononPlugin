#include "chrononplugin/maps/map_story_effects.hpp"

#include "chrononplugin/maps/map_camera.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
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
