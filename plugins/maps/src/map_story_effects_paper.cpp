// Folded paper atlas: the editorial base plate is printed onto a cream paper
// sheet, folded as a trifold, opened, and then wiped away to reveal the live
// map. The effect is deliberately self-contained: it needs no extra geographic
// source, only the already-composed plate and the map label.

#include "chrononplugin/maps/map_story_effects.hpp"

#include "map_story_effects_internal.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace chrononplugin::maps {
using namespace story_detail;

namespace {

double smooth01(double value) {
    const double t = std::clamp(value, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

std::string upper_text(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return value;
}

void fit_text(cv::Mat& target, const std::string& text, cv::Point origin,
              double max_width, double font, cv::Scalar color, int thickness) {
    if (text.empty()) return;
    int baseline = 0;
    cv::Size extent = cv::getTextSize(text, cv::FONT_HERSHEY_TRIPLEX, font,
                                      thickness, &baseline);
    if (extent.width > max_width && extent.width > 0) {
        font *= max_width / static_cast<double>(extent.width);
        extent = cv::getTextSize(text, cv::FONT_HERSHEY_TRIPLEX, font,
                                 thickness, &baseline);
    }
    cv::putText(target, text, origin, cv::FONT_HERSHEY_TRIPLEX, font, color,
                thickness, cv::LINE_AA);
}

}  // namespace

void draw_paper_map_fold(cv::Mat& frame, double progress,
                         const std::string& label, bool show_text) {
    if (frame.empty() || frame.cols < 320 || frame.rows < 180) return;
    progress = std::clamp(progress, 0.0, 1.0);
    const double scale = std::clamp(frame.rows / 1080.0, 0.45, 1.5);
    const cv::Mat live_map = frame.clone();

    const double unfold = smooth01((progress - 0.02) / 0.30);
    const double wipe = smooth01((progress - 0.46) / 0.22);

    cv::Mat canvas(frame.size(), CV_8UC3, cv::Scalar(12, 14, 18));
    const cv::Point desk_center(frame.cols / 2, frame.rows / 2);
    for (int i = 6; i >= 1; --i) {
        cv::Mat glow = canvas.clone();
        cv::circle(glow, desk_center,
                   cvRound(frame.rows * (0.30 + static_cast<double>(i) * 0.065)),
                   cv::Scalar(30, 27, 21), cv::FILLED, cv::LINE_AA);
        cv::addWeighted(canvas, 0.84, glow, 0.16, 0.0, canvas);
    }

    const int paper_x = cvRound(frame.cols * 0.06);
    const int paper_y = cvRound(frame.rows * 0.07);
    const int paper_w = frame.cols - 2 * paper_x;
    const int paper_h = frame.rows - 2 * paper_y;
    if (paper_w < 180 || paper_h < 120) return;
    const int header_h = std::max(34, cvRound(paper_h * 0.115));
    const int inset = std::max(7, cvRound(10.0 * scale));
    const cv::Rect map_rect(inset, header_h,
                            paper_w - 2 * inset,
                            paper_h - header_h - inset);
    if (map_rect.width < 40 || map_rect.height < 30) return;

    cv::Mat paper(paper_h, paper_w, CV_8UC3, cv::Scalar(211, 226, 237));
    cv::rectangle(paper, {0, 0, paper_w, header_h},
                  cv::Scalar(196, 213, 227), cv::FILLED);
    cv::line(paper, {0, header_h - 1}, {paper_w, header_h - 1},
             cv::Scalar(119, 101, 79), std::max(1, cvRound(1.4 * scale)),
             cv::LINE_AA);

    cv::Rect crop(0, 0, live_map.cols, live_map.rows);
    const double target_aspect = static_cast<double>(map_rect.width) /
                                 std::max(1, map_rect.height);
    const double source_aspect = static_cast<double>(live_map.cols) /
                                 std::max(1, live_map.rows);
    if (source_aspect > target_aspect) {
        crop.width = std::max(1, cvRound(live_map.rows * target_aspect));
        crop.x = (live_map.cols - crop.width) / 2;
    } else {
        crop.height = std::max(1, cvRound(live_map.cols / target_aspect));
        crop.y = (live_map.rows - crop.height) / 2;
    }
    cv::Mat printed;
    cv::resize(live_map(crop), printed, map_rect.size(), 0.0, 0.0,
               cv::INTER_AREA);
    cv::Mat gray;
    cv::cvtColor(printed, gray, cv::COLOR_BGR2GRAY);
    // The editorial plate is intentionally pale. A local histogram stretch
    // gives the paper print enough ink contrast to read as an atlas rather
    // than a blank sheet without changing the underlying geography.
    cv::equalizeHist(gray, gray);
    cv::Mat sepia;
    cv::cvtColor(gray, sepia, cv::COLOR_GRAY2BGR);
    sepia.convertTo(sepia, -1, 0.88, 0.0);
    cv::Mat cream(sepia.size(), CV_8UC3, cv::Scalar(190, 207, 220));
    cv::addWeighted(sepia, 0.68, cream, 0.32, 0.0, sepia);

    cv::Mat grid = sepia.clone();
    const int grid_step = std::max(28, cvRound(96.0 * scale));
    for (int x = grid_step; x < grid.cols; x += grid_step) {
        cv::line(grid, {x, 0}, {x, grid.rows}, cv::Scalar(150, 165, 178),
                 1, cv::LINE_AA);
    }
    for (int y = grid_step; y < grid.rows; y += grid_step) {
        cv::line(grid, {0, y}, {grid.cols, y}, cv::Scalar(150, 165, 178),
                 1, cv::LINE_AA);
    }
    cv::addWeighted(sepia, 0.82, grid, 0.18, 0.0, sepia);
    sepia.copyTo(paper(map_rect));
    cv::rectangle(paper, map_rect, cv::Scalar(103, 85, 65),
                  std::max(1, cvRound(1.2 * scale)), cv::LINE_AA);

    const int pad = std::max(10, cvRound(20.0 * scale));
    const std::string title = upper_text(label.empty() ? "EUROPE" : label);
    const cv::Scalar ink(58, 47, 35);
    fit_text(paper, title, {pad, cvRound(header_h * 0.66)},
             paper_w * 0.56, 1.18 * scale, ink,
             std::max(1, cvRound(2.0 * scale)));
    cv::putText(paper, "PAPER ATLAS / PLATE 01",
                {pad, header_h - cvRound(9.0 * scale)},
                cv::FONT_HERSHEY_SIMPLEX, 0.42 * scale,
                cv::Scalar(102, 86, 67), 1, cv::LINE_AA);
    const std::string edition = "CHRONON CARTOGRAPHY";
    int edition_baseline = 0;
    const cv::Size edition_size = cv::getTextSize(
        edition, cv::FONT_HERSHEY_SIMPLEX, 0.38 * scale, 1, &edition_baseline);
    cv::putText(paper, edition,
                {paper_w - pad - edition_size.width, header_h - cvRound(9.0 * scale)},
                cv::FONT_HERSHEY_SIMPLEX, 0.38 * scale,
                cv::Scalar(102, 86, 67), 1, cv::LINE_AA);

    const int third_a = paper_w / 3;
    const int third_b = (2 * paper_w) / 3;
    const int center_panel_w = third_b - third_a;
    for (const int x : {third_a, third_b}) {
        cv::line(paper, {x, 0}, {x, paper_h}, cv::Scalar(129, 112, 90),
                 std::max(1, cvRound(1.1 * scale)), cv::LINE_AA);
        cv::line(paper, {x + 2, 0}, {x + 2, paper_h},
                 cv::Scalar(239, 243, 240), 1, cv::LINE_AA);
    }

    const int packet_center_x = paper_x + paper_w / 2;
    const int center_x = packet_center_x - center_panel_w / 2;
    const int side_w = cvRound(
        static_cast<double>(third_a) * unfold);
    const int left_x = center_x - side_w;
    const int right_x = center_x + center_panel_w;
    const int packet_right = right_x + side_w;
    const cv::Rect packet(left_x, paper_y,
                          std::max(1, packet_right - left_x), paper_h);

    cv::Mat shadow(frame.size(), CV_8UC1, cv::Scalar(0));
    const int shadow_pad = std::max(8, cvRound(24.0 * scale));
    cv::rectangle(shadow,
                  {packet.x - shadow_pad, packet.y + cvRound(8.0 * scale),
                   packet.width + 2 * shadow_pad, packet.height + shadow_pad},
                  cv::Scalar(255), cv::FILLED);
    cv::GaussianBlur(shadow, shadow, {0, 0}, 18.0 * scale);
    cv::Mat shaded = canvas.clone();
    shaded.setTo(cv::Scalar(0, 0, 0), shadow);
    cv::addWeighted(canvas, 0.58, shaded, 0.42, 0.0, canvas);

    const auto blit_panel = [&](int dest_x, int dest_w,
                                const cv::Rect& source) {
        if (dest_w <= 0) return;
        cv::Mat panel;
        cv::resize(paper(source), panel,
                   {dest_w, paper_h}, 0.0, 0.0,
                   dest_w < source.width ? cv::INTER_AREA : cv::INTER_LINEAR);
        panel.copyTo(canvas(cv::Rect(dest_x, paper_y, dest_w, paper_h)));
        cv::rectangle(canvas, {dest_x, paper_y, dest_w, paper_h},
                      cv::Scalar(99, 83, 64),
                      std::max(1, cvRound(1.2 * scale)), cv::LINE_AA);
    };
    blit_panel(left_x, side_w,
               {0, 0, third_a, paper_h});
    blit_panel(center_x, center_panel_w,
               {third_a, 0, center_panel_w, paper_h});
    blit_panel(right_x, side_w,
               {third_b, 0, paper_w - third_b, paper_h});

    const auto draw_crease = [&](int x) {
        cv::line(canvas, {x, paper_y}, {x, paper_y + paper_h},
                 cv::Scalar(75, 63, 49),
                 std::max(1, cvRound(1.8 * scale)), cv::LINE_AA);
        cv::line(canvas, {x + 2, paper_y}, {x + 2, paper_y + paper_h},
                 cv::Scalar(240, 243, 238), 1, cv::LINE_AA);
    };
    draw_crease(center_x);
    draw_crease(right_x);
    if (side_w > 2) {
        draw_crease(left_x + 1);
        draw_crease(packet_right - 1);
    }

    const double cover_alpha = 1.0 - smooth01((progress - 0.06) / 0.16);
    if (show_text && cover_alpha > 0.01) {
        cv::Mat cover = canvas.clone();
        const int cover_w = std::min(center_panel_w,
                                     cvRound(paper_w * 0.26));
        const int cover_h = std::max(36, cvRound(paper_h * 0.20));
        const cv::Rect cover_rect(packet_center_x - cover_w / 2,
                                  paper_y + paper_h / 2 - cover_h / 2,
                                  cover_w, cover_h);
        cv::rectangle(cover, cover_rect, cv::Scalar(29, 35, 42),
                      cv::FILLED, cv::LINE_AA);
        cv::rectangle(cover, cover_rect, cv::Scalar(150, 123, 83),
                      1, cv::LINE_AA);
        fit_text(cover, title,
                 {cover_rect.x + cvRound(10.0 * scale),
                  cover_rect.y + cvRound(cover_h * 0.55)},
                 cover_rect.width - cvRound(20.0 * scale), 0.58 * scale,
                 cv::Scalar(241, 238, 226), 1);
        cv::putText(cover, "FOLDED ATLAS",
                    {cover_rect.x + cvRound(10.0 * scale),
                     cover_rect.y + cover_rect.height - cvRound(9.0 * scale)},
                    cv::FONT_HERSHEY_SIMPLEX, 0.32 * scale,
                    cv::Scalar(177, 192, 200), 1, cv::LINE_AA);
        cv::addWeighted(canvas, 1.0 - cover_alpha, cover, cover_alpha, 0.0,
                        canvas);
    }

    if (wipe > 0.0) {
        cv::Mat mask(frame.size(), CV_8UC1, cv::Scalar(0));
        const int edge = std::clamp(cvRound(frame.cols * wipe), 0, frame.cols);
        const int feather = std::max(24, cvRound(92.0 * scale));
        const int solid = std::max(0, edge - feather);
        if (solid > 0) {
            mask(cv::Rect(0, 0, solid, frame.rows)).setTo(cv::Scalar(255));
        }
        for (int x = solid; x < edge; ++x) {
            const double t = feather > 0
                ? static_cast<double>(x - solid) / static_cast<double>(feather)
                : 1.0;
            mask.col(x).setTo(cv::Scalar(cvRound(255.0 * smooth01(t))));
        }
        live_map.copyTo(canvas, mask);
        if (wipe < 0.999) {
            cv::Mat seam = canvas.clone();
            cv::line(seam, {edge, 0}, {edge, frame.rows},
                     cv::Scalar(222, 233, 239),
                     std::max(1, cvRound(2.0 * scale)), cv::LINE_AA);
            cv::addWeighted(canvas, 0.76, seam, 0.24, 0.0, canvas);
        }
    }

    frame = canvas;
    const double label_alpha = smooth01((progress - 0.70) / 0.22);
    if (show_text && label_alpha > 0.01 && !label.empty()) {
        draw_location_tag(frame, {frame.cols / 2, frame.rows / 2}, label,
                          label_alpha);
    }
}

}  // namespace chrononplugin::maps
