// Label choreography: five country labels, five motion languages. Every label
// is anchored to a projected pin and reveals with a different Mapcast-style
// treatment: rise, pop, slide, typewriter and letter spread.

#include "chrononplugin/maps/map_story_effects.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace chrononplugin::maps {

namespace {

cv::Scalar accent_for(int index) {
    static const cv::Scalar accents[] = {
        {238, 196, 42},   // amber
        {110, 224, 248},  // ice
        {92, 128, 248},   // coral/red
        {126, 238, 174},  // mint
        {238, 118, 226},  // violet
    };
    return accents[static_cast<std::size_t>(index) % 5];
}

void draw_label_box(cv::Mat& frame, int x, int baseline_y,
                    const std::string& text, double scale, cv::Scalar accent,
                    double font_scale) {
    if (text.empty()) return;
    const int thickness = std::max(1, cvRound(1.2 * scale));
    int baseline = 0;
    const cv::Size extent = cv::getTextSize(
        text, cv::FONT_HERSHEY_SIMPLEX, font_scale, thickness, &baseline);
    x = std::clamp(x, 6, std::max(6, frame.cols - extent.width - 16));
    baseline_y = std::clamp(baseline_y,
                            extent.height + 8,
                            std::max(extent.height + 8, frame.rows - 8));
    const cv::Rect box(x, baseline_y - extent.height - 5,
                       extent.width + 12, extent.height + baseline + 10);
    cv::rectangle(frame, box, cv::Scalar(14, 18, 22), cv::FILLED, cv::LINE_AA);
    cv::line(frame, {x, box.y}, {box.x + box.width, box.y}, accent,
             std::max(1, cvRound(2.0 * scale)), cv::LINE_AA);
    cv::putText(frame, text, {x + 6, baseline_y - 3},
                cv::FONT_HERSHEY_SIMPLEX, font_scale,
                cv::Scalar(246, 248, 246), thickness, cv::LINE_AA);
}

void draw_spread_label(cv::Mat& frame, cv::Point anchor,
                       const std::string& text, double scale, cv::Scalar accent,
                       double local) {
    if (text.empty()) return;
    const double eased = local * local * (3.0 - 2.0 * local);
    const double font = 0.56 * scale;
    const int thickness = std::max(1, cvRound(1.2 * scale));
    const double spacing = 3.0 + 9.0 * (1.0 - eased);
    int baseline = 0;
    const cv::Size one = cv::getTextSize("M", cv::FONT_HERSHEY_SIMPLEX, font,
                                         thickness, &baseline);
    int width = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        int char_baseline = 0;
        width += cv::getTextSize(std::string(1, text[i]),
                                 cv::FONT_HERSHEY_SIMPLEX, font, thickness,
                                 &char_baseline).width;
        if (i + 1 < text.size()) width += cvRound(spacing * scale);
    }
    int x = anchor.x + cvRound(10.0 * scale);
    int baseline_y = anchor.y - cvRound(12.0 * scale);
    x = std::clamp(x, 6, std::max(6, frame.cols - width - 16));
    baseline_y = std::clamp(baseline_y, one.height + 8,
                            std::max(one.height + 8, frame.rows - 8));
    const cv::Rect box(x - 6, baseline_y - one.height - 5,
                       width + 12, one.height + baseline + 10);
    cv::rectangle(frame, box, cv::Scalar(14, 18, 22), cv::FILLED, cv::LINE_AA);
    cv::line(frame, {box.x, box.y}, {box.x + box.width, box.y}, accent,
             std::max(1, cvRound(2.0 * scale)), cv::LINE_AA);
    int cursor = x;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const std::string ch(1, text[i]);
        cv::putText(frame, ch, {cursor, baseline_y - 3},
                    cv::FONT_HERSHEY_SIMPLEX, font,
                    cv::Scalar(246, 248, 246), thickness, cv::LINE_AA);
        int char_baseline = 0;
        cursor += cv::getTextSize(ch, cv::FONT_HERSHEY_SIMPLEX, font,
                                  thickness, &char_baseline).width;
        cursor += cvRound(spacing * scale);
    }
}

}  // namespace

void draw_label_choreography(cv::Mat& frame,
                             const std::vector<cv::Point>& anchors,
                             const std::vector<std::string>& labels,
                             double progress, bool show_text) {
    if (frame.empty() || anchors.empty() || labels.empty()) return;
    if (frame.cols < 160 || frame.rows < 90) return;
    progress = std::clamp(progress, 0.0, 1.0);
    const double scale = std::clamp(frame.rows / 1080.0, 0.45, 1.5);
    const std::size_t count = std::min(anchors.size(), labels.size());

    if (show_text) {
        draw_label_box(frame, cvRound(24.0 * scale), cvRound(56.0 * scale),
                       "LABEL CHOREOGRAPHY / FIVE MOTIONS", scale,
                       accent_for(0), 0.78 * scale);
    }

    for (std::size_t i = 0; i < count; ++i) {
        const double start = static_cast<double>(i) * 0.12;
        const double local = std::clamp((progress - start) / 0.45, 0.0, 1.0);
        if (local <= 0.0) continue;
        const double eased = local * local * (3.0 - 2.0 * local);
        const cv::Point anchor = anchors[i];
        const std::string& text = labels[i];
        const cv::Scalar accent = accent_for(static_cast<int>(i));
        const int style = static_cast<int>(i) % 5;
        const double base_font = 0.58 * scale;

        cv::circle(frame, anchor, std::max(2, cvRound(3.0 * scale)), accent,
                   cv::FILLED, cv::LINE_AA);
        cv::circle(frame, anchor, std::max(5, cvRound(8.0 * scale)), accent,
                   1, cv::LINE_AA);

        if (!show_text) continue;
        if (style == 0) {
            // rise
            const int baseline = anchor.y - cvRound(12.0 * scale) +
                                 cvRound(28.0 * scale * (1.0 - eased));
            draw_label_box(frame, anchor.x + cvRound(12.0 * scale), baseline,
                           text, scale, accent, base_font);
        } else if (style == 1) {
            // pop
            draw_label_box(frame, anchor.x + cvRound(12.0 * scale),
                           anchor.y - cvRound(12.0 * scale), text, scale,
                           accent, base_font * (0.55 + 0.45 * eased));
        } else if (style == 2) {
            // slide
            draw_label_box(frame,
                           anchor.x + cvRound(12.0 * scale) -
                               cvRound(52.0 * scale * (1.0 - eased)),
                           anchor.y - cvRound(12.0 * scale), text, scale,
                           accent, base_font);
        } else if (style == 3) {
            // typewriter
            const std::size_t visible = static_cast<std::size_t>(
                std::ceil(static_cast<double>(text.size()) * eased));
            draw_label_box(frame, anchor.x + cvRound(12.0 * scale),
                           anchor.y - cvRound(12.0 * scale),
                           text.substr(0, visible), scale, accent, base_font);
        } else {
            // spread
            draw_spread_label(frame, anchor, text, scale, accent, local);
        }
    }
}

}  // namespace chrononplugin::maps
