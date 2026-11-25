// Animated data bars beside the map: a compact infographic built from the same
// validated `data.values` used by the choropleth. The map keeps the terrain,
// while the bars grow in a staggered sequence and the values count in.

#include "chrononplugin/maps/map_story_effects.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace chrononplugin::maps {

namespace {

struct BarRow {
    std::string name;
    double value = 0.0;
};

cv::Scalar mix_color(const cv::Scalar& from, const cv::Scalar& to, double amount) {
    const double t = std::clamp(amount, 0.0, 1.0);
    return cv::Scalar(
        from[0] + (to[0] - from[0]) * t,
        from[1] + (to[1] - from[1]) * t,
        from[2] + (to[2] - from[2]) * t);
}

cv::Scalar bar_color(const std::string& scheme, double amount) {
    const double t = std::clamp(amount, 0.0, 1.0);
    if (scheme == "reds") return mix_color({34, 26, 112}, {66, 73, 245}, t);
    if (scheme == "blues") return mix_color({68, 28, 20}, {255, 196, 72}, t);
    if (scheme == "greens") return mix_color({36, 54, 22}, {178, 246, 88}, t);
    if (scheme == "purples") return mix_color({52, 22, 42}, {226, 84, 244}, t);
    if (scheme == "diverging") {
        const cv::Scalar white(244, 246, 248);
        if (t < 0.5) return mix_color({170, 76, 36}, white, t * 2.0);
        return mix_color(white, {54, 56, 224}, (t - 0.5) * 2.0);
    }
    if (t < 0.34) return mix_color({92, 32, 18}, {230, 196, 54}, t / 0.34);
    if (t < 0.67) {
        return mix_color({230, 196, 54}, {76, 218, 255}, (t - 0.34) / 0.33);
    }
    return mix_color({76, 218, 255}, {36, 48, 240}, (t - 0.67) / 0.33);
}

std::string upper_name(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return value;
}

std::string format_value(double value, const DataMapSpec& spec) {
    std::ostringstream out;
    out << spec.prefix << std::fixed
        << std::setprecision(std::fabs(value) < 10.0 ? 2 : 1)
        << value << spec.unit;
    return out.str();
}

}  // namespace

void draw_data_bars(cv::Mat& frame, const DataMapSpec& spec, double progress,
                    bool show_text) {
    if (frame.empty() || spec.values.empty()) return;
    if (frame.cols < 320 || frame.rows < 180) return;
    progress = std::clamp(progress, 0.0, 1.0);
    const double scale = std::clamp(frame.rows / 1080.0, 0.45, 1.5);

    cv::Mat shade(frame.size(), CV_8UC3, cv::Scalar(12, 16, 22));
    cv::addWeighted(frame, 0.58, shade, 0.42, 0.0, frame);

    const int panel_width = cvRound(frame.cols * 0.40);
    const int panel_x = frame.cols - panel_width;
    const cv::Rect panel_rect(panel_x, 0, panel_width, frame.rows);
    cv::Mat panel = frame(panel_rect).clone();
    panel.setTo(cv::Scalar(12, 16, 20));
    cv::Mat destination = frame(panel_rect);
    cv::addWeighted(destination, 0.16, panel, 0.84, 0.0, destination);
    cv::line(frame, {panel_x, 0}, {panel_x, frame.rows - 1},
             cv::Scalar(72, 168, 224), std::max(1, cvRound(1.5 * scale)),
             cv::LINE_AA);

    if (show_text && !spec.title.empty()) {
        const int x = panel_x + cvRound(30.0 * scale);
        const double font = 0.82 * scale;
        cv::putText(frame, spec.title, {x, cvRound(72.0 * scale)},
                    cv::FONT_HERSHEY_SIMPLEX, font, cv::Scalar(246, 248, 246),
                    2, cv::LINE_AA);
        cv::putText(frame, "VALUES / COMPARISON", {x, cvRound(104.0 * scale)},
                    cv::FONT_HERSHEY_SIMPLEX, 0.46 * scale,
                    cv::Scalar(138, 188, 220), 1, cv::LINE_AA);
    }

    std::vector<BarRow> rows;
    rows.reserve(spec.values.size());
    double minimum = spec.has_min ? spec.min_value : 1e300;
    double maximum = spec.has_max ? spec.max_value : -1e300;
    for (const auto& entry : spec.values) {
        rows.push_back({entry.country, entry.value});
        minimum = std::min(minimum, entry.value);
        maximum = std::max(maximum, entry.value);
    }
    if (rows.empty()) return;
    std::sort(rows.begin(), rows.end(), [](const BarRow& a, const BarRow& b) {
        return a.value > b.value;
    });
    if (maximum <= minimum) maximum = minimum + 1.0;
    const double span = maximum - minimum;
    if (rows.size() > 7) rows.resize(7);

    const int top = cvRound(154.0 * scale);
    const int bottom = frame.rows - cvRound(56.0 * scale);
    const int row_height = std::max(1, (bottom - top) /
        static_cast<int>(rows.size()));
    const int label_width = cvRound(174.0 * scale);
    const int bar_x = panel_x + cvRound(30.0 * scale) + label_width;
    const int max_bar = panel_width - label_width - cvRound(118.0 * scale);
    const int bar_height = std::max(8, cvRound(18.0 * scale));

    for (std::size_t i = 0; i < rows.size(); ++i) {
        const double local = std::clamp(
            (progress - static_cast<double>(i) * 0.045) / 0.38, 0.0, 1.0);
        const double eased = local * local * (3.0 - 2.0 * local);
        const int center_y = top + static_cast<int>(i) * row_height +
                             row_height / 2;
        const int y = center_y - bar_height / 2;
        const double normalized = std::clamp(
            (rows[i].value - minimum) / span, 0.0, 1.0);
        const cv::Scalar color = bar_color(spec.scheme, normalized);

        cv::rectangle(frame,
                      {bar_x, y, max_bar, bar_height},
                      cv::Scalar(34, 42, 48), cv::FILLED, cv::LINE_AA);
        const int width = std::max(1, cvRound(max_bar * normalized * eased));
        cv::rectangle(frame, {bar_x, y, width, bar_height}, color,
                      cv::FILLED, cv::LINE_AA);
        cv::line(frame, {bar_x, y}, {bar_x + width, y},
                 cv::Scalar(255, 255, 255), 1, cv::LINE_AA);

        if (show_text) {
            const std::string name = upper_name(rows[i].name);
            cv::putText(frame, name,
                        {panel_x + cvRound(30.0 * scale),
                         center_y + cvRound(7.0 * scale)},
                        cv::FONT_HERSHEY_SIMPLEX, 0.57 * scale,
                        cv::Scalar(238, 242, 244), 1, cv::LINE_AA);
            const std::string value = format_value(rows[i].value, spec);
            cv::putText(frame, value,
                        {bar_x + max_bar + cvRound(16.0 * scale),
                         center_y + cvRound(7.0 * scale)},
                        cv::FONT_HERSHEY_SIMPLEX, 0.52 * scale,
                        cv::Scalar(226, 232, 236), 1, cv::LINE_AA);
        }
    }
}

}  // namespace chrononplugin::maps
