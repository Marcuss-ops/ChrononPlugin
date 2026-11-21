// Territory treatments: wipe, flood fill, extrusion, satellite scan, callouts.
// Split out of map_story_effects.cpp.

#include "chrononplugin/maps/map_story_effects.hpp"

#include "map_story_effects_internal.hpp"

#include "chrononplugin/maps/map_camera.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

namespace chrononplugin::maps {
using namespace story_detail;

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
    // `place_name` is already the exact name resolved from the geographic
    // catalog. Do not attach an administrative qualifier here: the same
    // renderer handles countries, regions, and local territories, and a
    // hard-coded "Provincia di" mislabels countries such as Italia/Grecia.
    const std::string title = place_name;
    opacity = std::clamp(opacity, 0.0, 1.0);
    if (opacity <= 0.0) return;
    cv::Mat overlay = frame.clone();
    const double scale = static_cast<double>(frame.rows) / 1080.0;
    const int pad_x = std::max(10, cvRound(15.0 * scale));
    const int pad_y = std::max(7, cvRound(10.0 * scale));
    const double font_scale = 1.16 * scale;
    const int font_thickness = std::max(1, cvRound(2.0 * scale));
    int baseline = 0;
    const cv::Size text_size = cv::getTextSize(title,
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
    cv::putText(overlay, title, text_origin,
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
}  // namespace chrononplugin::maps
