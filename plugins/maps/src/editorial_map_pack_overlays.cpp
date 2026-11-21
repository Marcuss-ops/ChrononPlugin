// Editorial overlay primitives, animated per-style overlays and the finish.
// Split out of editorial_map_pack.cpp.

#include "editorial_map_pack_internal.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace chrononplugin::maps::editorial_pack_detail {

namespace {

void draw_pin(cv::Mat& frame, cv::Point2d point, double alpha,
              const cv::Scalar& fill, const cv::Scalar& ring, double scale,
              double ring_radius) {
    const int radius = std::max(3, static_cast<int>(std::lround(ring_radius * scale)));
    const cv::Point center(static_cast<int>(std::lround(point.x)),
                           static_cast<int>(std::lround(point.y)));
    draw_disc_alpha(frame, {center.x + 2, center.y + 3}, radius + 2,
                    rgb(0x1F2937), alpha * 0.16);
    draw_disc_alpha(frame, center, radius, ring, alpha);
    draw_disc_alpha(frame, center,
                    std::max(2, static_cast<int>(std::lround(radius * 0.44))), fill,
                    alpha);
}

void draw_crosshair(cv::Mat& frame, cv::Point center, const cv::Scalar& color,
                    double alpha, int radius) {
    draw_ring_alpha(frame, center, radius, color, 1, alpha * 0.9);
    draw_line_alpha(frame, {center.x - radius - 8, center.y},
                    {center.x - radius + 2, center.y}, color, 1, alpha);
    draw_line_alpha(frame, {center.x + radius - 2, center.y},
                    {center.x + radius + 8, center.y}, color, 1, alpha);
    draw_line_alpha(frame, {center.x, center.y - radius - 8},
                    {center.x, center.y - radius + 2}, color, 1, alpha);
    draw_line_alpha(frame, {center.x, center.y + radius - 2},
                    {center.x, center.y + radius + 8}, color, 1, alpha);
}

void draw_radar(cv::Mat& frame, cv::Point2d point, double time, double alpha) {
    const cv::Point center(static_cast<int>(std::lround(point.x)),
                           static_cast<int>(std::lround(point.y)));
    for (int radius = 52; radius >= 14; radius -= 8) {
        draw_disc_alpha(frame, center, radius, rgb(0x38BDF8),
                        alpha * (0.030 + (52 - radius) * 0.002));
    }
    draw_ring_alpha(frame, center, 19, rgb(0x8FD8FA), 1, alpha * 0.85);
    draw_disc_alpha(frame, center, 6, rgb(0x38BDF8), alpha);
    draw_disc_alpha(frame, center, 2, rgb(0xFFFFFF), alpha);
    for (int pulse = 0; pulse < 2; ++pulse) {
        const double phase = std::fmod(time * 3.0 + pulse * 0.5, 1.0);
        const int radius = static_cast<int>(std::lround(24.0 + 150.0 * phase));
        draw_ring_alpha(frame, center, radius, rgb(0x38BDF8), 2,
                        alpha * (1.0 - phase) * 0.85);
    }
}

void draw_path_progress(cv::Mat& frame, const std::vector<cv::Point2d>& points,
                        const cv::Scalar& color, int thickness, double progress) {
    if (points.size() < 2 || progress <= 0.0) return;
    std::vector<double> lengths(points.size() - 1, 0.0);
    double total = 0.0;
    for (std::size_t i = 1; i < points.size(); ++i) {
        lengths[i - 1] = cv::norm(points[i] - points[i - 1]);
        total += lengths[i - 1];
    }
    if (total <= 1e-9) return;
    const double target = total * clamp01(progress);
    std::vector<cv::Point> visible;
    visible.push_back({static_cast<int>(std::lround(points[0].x)),
                       static_cast<int>(std::lround(points[0].y))});
    double accumulated = 0.0;
    for (std::size_t i = 1; i < points.size(); ++i) {
        if (accumulated + lengths[i - 1] <= target) {
            visible.push_back({static_cast<int>(std::lround(points[i].x)),
                               static_cast<int>(std::lround(points[i].y))});
            accumulated += lengths[i - 1];
            continue;
        }
        const double fraction = (target - accumulated) / lengths[i - 1];
        const cv::Point2d partial = points[i - 1] + (points[i] - points[i - 1]) * fraction;
        visible.push_back({static_cast<int>(std::lround(partial.x)),
                           static_cast<int>(std::lround(partial.y))});
        break;
    }
    if (visible.size() >= 2) {
        cv::polylines(frame, visible, false, color, thickness, cv::LINE_AA);
    }
}

[[nodiscard]] std::vector<cv::Point2d> bezier_points(cv::Point2d from,
                                                     cv::Point2d to, int steps) {
    const double distance = cv::norm(to - from);
    const double lift = distance * 0.20;
    const cv::Point2d c1(from.x + (to.x - from.x) * 0.33,
                         from.y + (to.y - from.y) * 0.33 - lift);
    const cv::Point2d c2(from.x + (to.x - from.x) * 0.66,
                         from.y + (to.y - from.y) * 0.66 - lift);
    std::vector<cv::Point2d> out;
    out.reserve(steps + 1);
    for (int i = 0; i <= steps; ++i) {
        const double t = static_cast<double>(i) / steps;
        const double u = 1.0 - t;
        out.push_back(from * (u * u * u) + c1 * (3.0 * u * u * t) +
                      c2 * (3.0 * u * t * t) + to * (t * t * t));
    }
    return out;
}

void draw_arc_glow(cv::Mat& frame, const std::vector<cv::Point2d>& points,
                   const cv::Scalar& color, double alpha) {
    if (points.size() < 2) return;
    int min_x = frame.cols, min_y = frame.rows, max_x = 0, max_y = 0;
    std::vector<cv::Point> pixel_points;
    pixel_points.reserve(points.size());
    for (const auto& point : points) {
        const cv::Point pixel(static_cast<int>(std::lround(point.x)),
                              static_cast<int>(std::lround(point.y)));
        pixel_points.push_back(pixel);
        min_x = std::min(min_x, pixel.x);
        min_y = std::min(min_y, pixel.y);
        max_x = std::max(max_x, pixel.x);
        max_y = std::max(max_y, pixel.y);
    }
    constexpr int kPad = 32;
    const cv::Rect roi = clamp_roi(
        frame, {min_x - kPad, min_y - kPad, max_x - min_x + kPad * 2 + 1,
                max_y - min_y + kPad * 2 + 1});
    if (roi.empty()) return;
    for (cv::Point& point : pixel_points) point -= roi.tl();
    cv::Mat glow(roi.size(), CV_8UC3, cv::Scalar(0));
    cv::polylines(glow, pixel_points, false, color, 10, cv::LINE_AA);
    cv::GaussianBlur(glow, glow, cv::Size(0, 0), 7.0);
    cv::Mat target = frame(roi);
    cv::addWeighted(target, 1.0, glow, alpha, 0.0, target);
}

void draw_ground_shadow(cv::Mat& frame, cv::Point2d from, cv::Point2d to,
                        double alpha) {
    const cv::Point a(static_cast<int>(std::lround(from.x)),
                      static_cast<int>(std::lround(from.y + 8)));
    const cv::Point b(static_cast<int>(std::lround(to.x)),
                      static_cast<int>(std::lround(to.y + 8)));
    constexpr int kPad = 40;
    const cv::Rect roi = clamp_roi(
        frame, {std::min(a.x, b.x) - kPad, std::min(a.y, b.y) - kPad,
                std::abs(b.x - a.x) + kPad * 2 + 1,
                std::abs(b.y - a.y) + kPad * 2 + 1});
    if (roi.empty()) return;
    cv::Mat shadow(roi.size(), CV_8UC3, cv::Scalar(0));
    cv::line(shadow, a - roi.tl(), b - roi.tl(), rgb(0x1F2937), 14,
             cv::LINE_AA);
    cv::GaussianBlur(shadow, shadow, cv::Size(0, 0), 9.0);
    cv::Mat target = frame(roi);
    cv::addWeighted(target, 1.0, shadow, alpha, 0.0, target);
}

// ── Finishing: film grain and vignette ──────────────────────────────────────

}  // namespace

[[nodiscard]] Finish make_finish(int width, int height, unsigned int seed,
                                 double grain_strength, double vignette_strength) {
    Finish finish;
    finish.grain_strength = grain_strength;
    if (grain_strength > 0.0) {
        finish.grain.create(height, width, CV_8UC3);
        cv::RNG rng(seed);
        rng.fill(finish.grain, cv::RNG::NORMAL, 128.0, 40.0);
    }
    if (vignette_strength > 0.0) {
        finish.vignette.create(height, width, CV_8UC3);
        const double half_x = std::max(1.0, (width - 1) * 0.5);
        const double half_y = std::max(1.0, (height - 1) * 0.5);
        for (int y = 0; y < height; ++y) {
            cv::Vec3b* row = finish.vignette.ptr<cv::Vec3b>(y);
            const double dy = (y - (height - 1) * 0.5) / half_y;
            for (int x = 0; x < width; ++x) {
                const double dx = (x - (width - 1) * 0.5) / half_x;
                const double radius = clamp01(std::sqrt(dx * dx + dy * dy));
                const double gain =
                    1.0 - vignette_strength * std::pow(radius, 2.2);
                const unsigned char value =
                    cv::saturate_cast<unsigned char>(gain * 255.0);
                row[x] = cv::Vec3b(value, value, value);
            }
        }
    }
    return finish;
}

// ── Per-style animated overlays ─────────────────────────────────────────────

void overlay_editorial(cv::Mat& frame, const Geometry& geometry, Fonts& fonts,
                       const View& screen, double t,
                       const chrononplugin::maps::EditorialMapRequest& config) {
    const cv::Point2d indianapolis =
        screen.project(config.pin_latitude, config.pin_longitude);
    const double title_alpha = smooth01(0.02, 0.09, t);
    const double pin_alpha = smooth01(0.10, 0.15, t);
    const double city_alpha = smooth01(0.15, 0.21, t);

    const std::string micro = upper_copy(geometry.country_mode
        ? config.title + " / editorial plate"
        : "United States / editorial plate");
    const double micro_width = fonts.mono.measure(micro, 17, 3);
    draw_text(fonts.mono, frame, micro,
              static_cast<int>(std::lround(1820.0 - micro_width)), 1018, 17,
              rgb(0x33414F), title_alpha * 0.72, 3);
    draw_line_alpha(frame,
                    {static_cast<int>(std::lround(1820.0 - micro_width)), 1002},
                    {1820, 1002}, rgb(0xD94E34), 3, title_alpha * 0.9);
    draw_text(fonts.serif, frame, config.title, 98, 946, 104, rgb(0x1F2937),
              title_alpha);
    draw_line_alpha(frame, {102, 962}, {196, 962}, rgb(0xD94E34), 4,
                    title_alpha);
    draw_text(fonts.sans, frame, upper_copy(config.subtitle), 102, 1002, 21,
              rgb(0x556270), title_alpha * 0.95, 5);

    draw_pin(frame, indianapolis, pin_alpha, rgb(0x111827), rgb(0xF4F1EA), 1.0,
             13.0);
    if (city_alpha > 0.0) {
        draw_text_outlined(fonts.sans_bold, frame, config.pin_label,
                           static_cast<int>(std::lround(indianapolis.x)) + 28,
                           static_cast<int>(std::lround(indianapolis.y)) - 12,
                           32, rgb(0x1F2937), city_alpha, 0, rgb(0xF4F1EA), 2);
    }
    (void)geometry;
}

void overlay_paper_cutout(cv::Mat& frame, const Geometry& geometry, Fonts& fonts,
                          const View& screen, double t,
                          const chrononplugin::maps::EditorialMapRequest& config) {
    const double pin_latitude = geometry.country_mode
        ? config.pin_latitude : 39.7684;
    const double pin_longitude = geometry.country_mode
        ? config.pin_longitude : -86.1581;
    const std::string title = geometry.country_mode ? config.title : "INDIANA";
    const std::string pin_label = geometry.country_mode
        ? config.pin_label : "Indianapolis";
    const cv::Point2d indianapolis = screen.project(pin_latitude, pin_longitude);
    const double title_alpha = smooth01(0.02, 0.09, t);
    const double pin_alpha = smooth01(0.12, 0.18, t);
    const double city_alpha = smooth01(0.17, 0.23, t);

    draw_text(fonts.serif, frame, title, 100, 148, 92, rgb(0x2A241C),
              title_alpha);
    draw_line_alpha(frame, {104, 168}, {190, 168}, rgb(0xC05A3E), 3,
                    title_alpha);
    draw_text(fonts.mono, frame, geometry.country_mode ? "ITALY / EUROPE" : "39.77 N / 86.16 W", 104, 204, 18,
              rgb(0x6B6255), title_alpha * 0.9, 2);
    draw_text(fonts.sans, frame, upper_copy(geometry.country_mode
                  ? config.subtitle : "The Hoosier State"), 104, 232, 18,
              rgb(0x7A7164), title_alpha * 0.85, 4);

    draw_pin(frame, indianapolis, pin_alpha, rgb(0x2A241C), rgb(0xF8F3E7), 1.0,
             13.0);
    if (city_alpha > 0.0) {
        draw_text_outlined(fonts.sans_bold, frame, pin_label,
                           static_cast<int>(std::lround(indianapolis.x)) + 26,
                           static_cast<int>(std::lround(indianapolis.y)) - 12,
                           31, rgb(0x2A241C), city_alpha, 0, rgb(0xF8F3E7), 2);
    }
    (void)geometry;
}

void overlay_dark_radar(cv::Mat& frame, const Geometry& geometry, Fonts& fonts,
                        const View& screen, double t,
                        const chrononplugin::maps::EditorialMapRequest& config) {
    const double pin_latitude = geometry.country_mode
        ? config.pin_latitude : 38.9072;
    const double pin_longitude = geometry.country_mode
        ? config.pin_longitude : -77.0369;
    const std::string pin_label = geometry.country_mode
        ? config.pin_label : "WASHINGTON, D.C.";
    const cv::Point2d washington = screen.project(pin_latitude, pin_longitude);
    const double hud_alpha = smooth01(0.02, 0.08, t);
    const double pin_alpha = smooth01(0.05, 0.10, t);
    const double label_alpha = smooth01(0.09, 0.15, t);

    draw_line_alpha(frame, {92, 92}, {172, 92}, rgb(0x38BDF8), 3,
                    hud_alpha * 0.9);
    draw_text(fonts.mono, frame, geometry.country_mode
                  ? "ITALIA // SIGNAL GRID" : "UNITED STATES // SIGNAL GRID", 92, 130, 20,
              rgb(0x8FA3BF), hud_alpha, 3);
    draw_text(fonts.mono, frame, geometry.country_mode
                  ? "42.00 N / 12.50 E" : "38.90 N / 77.03 W", 92, 158, 16,
              rgb(0x5E738D), hud_alpha * 0.9, 2);

    draw_radar(frame, washington, t, pin_alpha);
    draw_text(fonts.mono, frame, pin_label, 92, 976, 24,
              rgb(0xF1F5F9), label_alpha, 4);
    draw_text(fonts.mono, frame, geometry.country_mode
                  ? "COUNTRY OUTLINE / PRIORITY 01" : "FEDERAL DISTRICT / PRIORITY 01", 92, 1006, 16,
              rgb(0x6F849E), label_alpha * 0.9, 2);
    draw_text(fonts.mono, frame, geometry.country_mode
                  ? "GRID REF ITALIA / CENTRALE" : "GRID REF 18S UJ 234 083", 92, 1032, 14,
              rgb(0x475B72), label_alpha * 0.8, 2);
    (void)geometry;
}

void overlay_blueprint(cv::Mat& frame, const Geometry& geometry, Fonts& fonts,
                       const View& screen, double t, int width, int height,
                       const chrononplugin::maps::EditorialMapRequest& config) {
    const cv::Point2d indianapolis = screen.project(
        geometry.country_mode ? config.pin_latitude : 39.7684,
        geometry.country_mode ? config.pin_longitude : -86.1581);
    const double panel_alpha = smooth01(0.03, 0.09, t);
    const double label_alpha = smooth01(0.12, 0.18, t);
    const cv::Rect panel(static_cast<int>(std::lround(width * 0.04)),
                         static_cast<int>(std::lround(height * 0.745)),
                         static_cast<int>(std::lround(width * 0.33)),
                         static_cast<int>(std::lround(height * 0.205)));
    draw_rect_alpha(frame, panel, rgb(0x0A2136), rgb(0x3D6C93), 1,
                    panel_alpha * 0.94);

    const int text_x = panel.x + static_cast<int>(std::lround(width * 0.016));
    draw_text(fonts.sans_bold, frame,
              geometry.country_mode ? config.title : "INDIANA",
              text_x, panel.y + 62, 56,
              rgb(0xE8F3FC), panel_alpha, 9);
    draw_line_alpha(frame, {text_x, panel.y + 80},
                    {panel.x + panel.width - 26, panel.y + 80},
                    rgb(0x3D6C93), 1, panel_alpha);
    draw_text(fonts.mono, frame, geometry.country_mode
                  ? "ITALY / EUROPE" : "39.7684 N / 86.1581 W", text_x,
              panel.y + 112, 18, rgb(0xA8C8E8), panel_alpha * 0.95, 1);
    draw_text(fonts.mono, frame, "ADMINISTRATIVE PLATE / 1:2,000,000", text_x,
              panel.y + 140, 15, rgb(0x7FA5C6), panel_alpha * 0.9, 1);
    draw_line_alpha(frame, {text_x, panel.y + 176},
                    {text_x + 190, panel.y + 176}, rgb(0xA8C8E8), 2,
                    panel_alpha);
    for (int tick = 0; tick <= 4; ++tick) {
        const int x = text_x + tick * 47;
        draw_line_alpha(frame, {x, panel.y + 170}, {x, panel.y + 184},
                        rgb(0xA8C8E8), 1, panel_alpha);
    }
    draw_text(fonts.mono, frame, "0", text_x, panel.y + 200, 13,
              rgb(0x7FA5C6), panel_alpha * 0.9);
    draw_text(fonts.mono, frame, "100 KM", text_x + 146, panel.y + 200, 13,
              rgb(0x7FA5C6), panel_alpha * 0.9);

    draw_crosshair(frame,
                   {static_cast<int>(std::lround(indianapolis.x)),
                    static_cast<int>(std::lround(indianapolis.y))},
                   rgb(0xE3C68E), label_alpha, 12);
    draw_text(fonts.mono, frame, geometry.country_mode
                  ? config.pin_label : "INDIANAPOLIS",
              static_cast<int>(std::lround(indianapolis.x)) + 30,
              static_cast<int>(std::lround(indianapolis.y)) - 14, 17,
              rgb(0xE3C68E), label_alpha, 2);
    (void)geometry;
}

void overlay_connection_arc(cv::Mat& frame, const Geometry& geometry,
                            Fonts& fonts, const View& screen, double t,
                            const chrononplugin::maps::EditorialMapRequest& config) {
    // A region render keeps the same arc idea, but the far end is the country
    // capital instead of Milano, and the distance follows the real coordinates.
    const bool region = geometry.region_mode;
    const double source_lat = region ? 41.9028
        : (geometry.country_mode ? 45.4642 : 38.9072);
    const double source_lon = region ? 12.4964
        : (geometry.country_mode ? 9.19 : -77.0369);
    const cv::Point2d washington = screen.project(source_lat, source_lon);
    const cv::Point2d indianapolis = screen.project(
        geometry.country_mode ? config.pin_latitude : 39.7684,
        geometry.country_mode ? config.pin_longitude : -86.1581);
    const double title_alpha = smooth01(0.02, 0.08, t);
    const double source_alpha = smooth01(0.06, 0.11, t);
    const double arc_progress = smooth01(0.10, 0.42, t);
    const double destination_alpha = smooth01(0.36, 0.43, t);
    const double distance_alpha = smooth01(0.34, 0.42, t);

    double distance_km = 480.0;
    if (region) {
        const double to_rad = 3.14159265358979323846 / 180.0;
        const double dlat = (config.pin_latitude - source_lat) * to_rad;
        const double dlon = (config.pin_longitude - source_lon) * to_rad;
        const double a = std::sin(dlat * 0.5) * std::sin(dlat * 0.5) +
            std::cos(source_lat * to_rad) *
            std::cos(config.pin_latitude * to_rad) *
            std::sin(dlon * 0.5) * std::sin(dlon * 0.5);
        distance_km = 6371.0 * 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
        distance_km = std::round(distance_km / 10.0) * 10.0;
    }

    draw_text(fonts.serif, frame, region
                  ? upper_copy(config.title)
                  : (geometry.country_mode ? "MILANO - ROMA"
                                           : "WASHINGTON - INDIANAPOLIS"),
              98, 112, 46, rgb(0x1F2937), title_alpha);
    draw_text(fonts.mono, frame, region
                  ? "CONNECTION ARC / APPROX. " +
                        std::to_string(static_cast<int>(distance_km)) + " KM"
                  : (geometry.country_mode
                         ? "CONNECTION ARC / APPROX. 480 KM"
                         : "CONNECTION ARC / APPROX. 800 KM"),
              100, 146, 17, rgb(0x5B6470), title_alpha * 0.9, 2);

    draw_ground_shadow(frame, washington, indianapolis,
                       arc_progress * 0.30);
    std::vector<cv::Point2d> curve = bezier_points(washington, indianapolis, 96);
    std::vector<cv::Point2d> visible;
    const std::size_t count = static_cast<std::size_t>(
        std::lround(static_cast<double>(curve.size() - 1) * arc_progress));
    visible.assign(curve.begin(), curve.begin() + static_cast<long>(count) + 1);
    if (visible.size() >= 2) {
        draw_arc_glow(frame, visible, rgb(0xD94E34), 0.34);
        draw_path_progress(frame, curve, rgb(0xF8F3E7), 7, arc_progress);
        draw_path_progress(frame, curve, rgb(0xD94E34), 4, arc_progress);
    }

    draw_pin(frame, washington, source_alpha, rgb(0x1F2937), rgb(0xF8F3E7), 1.0,
             12.0);
    draw_pin(frame, indianapolis, destination_alpha, rgb(0xD94E34),
             rgb(0xF8F3E7), 1.05, 14.0);

    const std::string dc_label = region
        ? "ROMA" : (geometry.country_mode ? "MILANO" : "WASHINGTON, D.C.");
    const double dc_width = fonts.sans_bold.measure(dc_label, 27);
    draw_text_outlined(
        fonts.sans_bold, frame, dc_label,
        static_cast<int>(std::lround(washington.x - dc_width - 24)),
        static_cast<int>(std::lround(washington.y)) + 42, 27, rgb(0x1F2937),
        destination_alpha, 0, rgb(0xF8F3E7), 2);
    const std::string indy_label = geometry.country_mode
        ? config.pin_label : "INDIANAPOLIS";
    const double indy_width = fonts.sans_bold.measure(indy_label, 27);
    draw_text_outlined(
        fonts.sans_bold, frame, indy_label,
        static_cast<int>(std::lround(indianapolis.x - indy_width - 24)),
        static_cast<int>(std::lround(indianapolis.y)) - 22, 27,
        rgb(0x1F2937), destination_alpha, 0, rgb(0xF8F3E7), 2);

    if (distance_alpha > 0.0) {
        const cv::Point2d midpoint = curve[curve.size() / 2];
        draw_text_centered(fonts.mono, frame,
                           region
                               ? std::to_string(static_cast<int>(distance_km)) + " KM"
                               : (geometry.country_mode ? "480 KM" : "800 KM"),
                           static_cast<int>(std::lround(midpoint.x)),
                           static_cast<int>(std::lround(midpoint.y)) - 20, 19,
                           rgb(0x5B6470), distance_alpha, 2);
    }
    (void)geometry;
}

void overlay_floating_island(cv::Mat& frame, const Geometry& geometry,
                             Fonts& fonts, const View& screen, double t,
                             const chrononplugin::maps::EditorialMapRequest& config) {
    const cv::Point2d anchor = geometry.country_mode
        ? screen.project(config.pin_latitude, config.pin_longitude)
        : screen.project(40.20, -84.82);
    const double line_progress = smooth01(0.10, 0.22, t);
    const double text_alpha = smooth01(0.20, 0.27, t);
    const int rise = static_cast<int>(std::lround((1.0 - text_alpha) * 10.0));

    // A true 45-degree leader climbs from the state's eastern border to the
    // cartouche level, then runs horizontally into it. The previous elbow was
    // above the frame whenever the silhouette was framed tall.
    const double target_y = 150.0;
    const cv::Point2d elbow(anchor.x + std::max(40.0, anchor.y - target_y),
                            target_y);
    const cv::Point2d end(1285.0, target_y);
    const std::vector<cv::Point2d> path{anchor, elbow, end};
    draw_path_progress(frame, path, rgb(0x1F2937), 2, line_progress);
    draw_disc_alpha(frame,
                    {static_cast<int>(std::lround(anchor.x)),
                     static_cast<int>(std::lround(anchor.y))},
                    4, rgb(0xD94E34), line_progress);

    const int cartouche_x = 1315;
    const int cartouche_top = 160;
    draw_line_alpha(frame, {cartouche_x - 22, cartouche_top + 54},
                    {cartouche_x - 22, cartouche_top + 10},
                    rgb(0xD94E34), 4, text_alpha);
    draw_text(fonts.serif, frame, geometry.country_mode
                  ? config.title : "INDIANA", cartouche_x,
              cartouche_top + 70 + rise, 74, rgb(0x1F2937), text_alpha);
    draw_line_alpha(frame, {cartouche_x, cartouche_top + 88 + rise},
                    {cartouche_x + 340, cartouche_top + 88 + rise},
                    rgb(0xB8B2A6), 1, text_alpha);
    draw_text(fonts.mono, frame, geometry.country_mode
                  ? "ITALIA / EUROPA" : "39.77 N / 86.15 W", cartouche_x,
              cartouche_top + 120 + rise, 19, rgb(0x5B6470), text_alpha, 2);
    draw_text(fonts.sans, frame, upper_copy(geometry.country_mode
                  ? config.subtitle : "The Hoosier State"), cartouche_x,
              cartouche_top + 148 + rise, 17, rgb(0x6B7280),
              text_alpha * 0.9, 4);
    (void)geometry;
}

void draw_overlay(const std::string& slug, cv::Mat& frame,
                  const Geometry& geometry, Fonts& fonts, const View& screen,
                  double t, int width, int height,
                  const chrononplugin::maps::EditorialMapRequest& config) {
    if (slug == "01_editorial_vox") {
        overlay_editorial(frame, geometry, fonts, screen, t, config);
    } else if (slug == "02_paper_cutout") {
        overlay_paper_cutout(frame, geometry, fonts, screen, t, config);
    } else if (slug == "03_dark_radar") {
        overlay_dark_radar(frame, geometry, fonts, screen, t, config);
    } else if (slug == "04_blueprint") {
        overlay_blueprint(frame, geometry, fonts, screen, t, width, height,
                          config);
    } else if (slug == "05_connection_arc") {
        overlay_connection_arc(frame, geometry, fonts, screen, t, config);
    } else if (slug == "06_floating_island") {
        overlay_floating_island(frame, geometry, fonts, screen, t, config);
    }
}
}  // namespace chrononplugin::maps::editorial_pack_detail
