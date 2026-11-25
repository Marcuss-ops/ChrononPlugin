// Data-driven country choropleth: tint each country by a numeric value, keep
// the terrain visible, reveal the fills with a short stagger, then add a
// legend and name/value labels. Inspired by the data-map scenes of Mapcast,
// but implemented on the native Natural Earth polygons and OpenCV compositor.

#include "chrononplugin/maps/map_story_effects.hpp"

#include "map_story_effects_internal.hpp"

#include "chrononplugin/maps/map_camera.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace chrononplugin::maps {
using namespace story_detail;

namespace {

std::string normalized_key(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (const unsigned char ch : value) {
        if (std::isalnum(ch)) {
            out.push_back(static_cast<char>(std::tolower(ch)));
        }
    }
    return out;
}

cv::Scalar mix_color(const cv::Scalar& from, const cv::Scalar& to, double amount) {
    const double t = std::clamp(amount, 0.0, 1.0);
    return cv::Scalar(
        from[0] + (to[0] - from[0]) * t,
        from[1] + (to[1] - from[1]) * t,
        from[2] + (to[2] - from[2]) * t);
}

cv::Scalar scheme_color(const std::string& scheme, double amount) {
    const double t = std::clamp(amount, 0.0, 1.0);
    if (scheme == "reds") {
        return mix_color({34, 26, 112}, {66, 73, 245}, t);
    }
    if (scheme == "blues") {
        return mix_color({68, 28, 20}, {255, 196, 72}, t);
    }
    if (scheme == "greens") {
        return mix_color({36, 54, 22}, {178, 246, 88}, t);
    }
    if (scheme == "purples") {
        return mix_color({52, 22, 42}, {226, 84, 244}, t);
    }
    if (scheme == "diverging") {
        const cv::Scalar white(244, 246, 248);
        if (t < 0.5) return mix_color({170, 76, 36}, white, t * 2.0);
        return mix_color(white, {54, 56, 224}, (t - 0.5) * 2.0);
    }
    // heat: deep blue -> cyan -> yellow -> red
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

bool feature_value(const GeoFeature& feature,
                   const std::map<std::string, double>& values,
                   double& value) {
    static constexpr std::array<const char*, 4> keys{
        "ADMIN", "NAME", "ISO_A3", "ISO_A2"};
    for (const char* key : keys) {
        const auto property = feature.properties.find(key);
        if (property == feature.properties.end()) continue;
        const auto found = values.find(normalized_key(property->second));
        if (found != values.end()) {
            value = found->second;
            return true;
        }
    }
    return false;
}

std::string feature_name(const GeoFeature& feature) {
    for (const char* key : {"ADMIN", "NAME"}) {
        const auto property = feature.properties.find(key);
        if (property != feature.properties.end()) return property->second;
    }
    return feature.id;
}

bool feature_label_point(const GeoFeature& feature,
                         std::pair<double, double> anchor, double zoom,
                         cv::Size frame_size, cv::Point& point) {
    double best_area = 0.0;
    std::vector<cv::Point> best_ring;
    for (const auto& polygon : feature.polygons) {
        if (polygon.rings.empty()) continue;
        auto ring = project_ring(polygon.rings.front(), anchor, zoom, frame_size);
        if (ring.size() < 3) continue;
        const double area = std::fabs(cv::contourArea(ring));
        if (area > best_area) {
            best_area = area;
            best_ring = std::move(ring);
        }
    }
    if (best_ring.empty()) return false;
    const cv::Moments moments = cv::moments(best_ring);
    if (moments.m00 > 1e-6) {
        point = {cvRound(moments.m10 / moments.m00),
                 cvRound(moments.m01 / moments.m00)};
    } else {
        const cv::Rect bounds = cv::boundingRect(best_ring);
        point = {bounds.x + bounds.width / 2, bounds.y + bounds.height / 2};
    }
    return point.x >= 0 && point.x < frame_size.width &&
           point.y >= 0 && point.y < frame_size.height;
}

cv::Rect draw_country_label(cv::Mat& frame, cv::Point anchor_point,
                            const std::string& text, double scale,
                            cv::Scalar accent,
                            const std::vector<cv::Rect>& placed) {
    if (text.empty()) return {};
    const double font = 0.44 * scale;
    const int thickness = std::max(1, cvRound(1.2 * scale));
    int baseline = 0;
    const cv::Size extent = cv::getTextSize(
        text, cv::FONT_HERSHEY_SIMPLEX, font, thickness, &baseline);
    int x = anchor_point.x + cvRound(8.0 * scale);
    int baseline_y = anchor_point.y - cvRound(8.0 * scale);
    x = std::clamp(x, 6, std::max(6, frame.cols - extent.width - 14));
    baseline_y = std::clamp(baseline_y,
                             extent.height + 8,
                             std::max(extent.height + 8, frame.rows - 8));
    const cv::Rect box(x, baseline_y - extent.height - 5,
                       extent.width + 10, extent.height + baseline + 9);
    for (const cv::Rect& other : placed) {
        if ((other & box).area() > 0) return {};
    }
    cv::rectangle(frame, box, cv::Scalar(14, 18, 22), cv::FILLED, cv::LINE_AA);
    cv::line(frame, {x, baseline_y - extent.height - 5},
             {x + extent.width + 10, baseline_y - extent.height - 5},
             accent, std::max(1, cvRound(2.0 * scale)), cv::LINE_AA);
    cv::putText(frame, text, {x + 5, baseline_y - 3},
                cv::FONT_HERSHEY_SIMPLEX, font, cv::Scalar(246, 248, 246),
                thickness, cv::LINE_AA);
    cv::circle(frame, anchor_point, std::max(2, cvRound(2.5 * scale)),
               accent, cv::FILLED, cv::LINE_AA);
    return box;
}

void draw_legend(cv::Mat& frame, const DataMapSpec& spec,
                 double minimum, double maximum, double scale) {
    const int x = cvRound(38.0 * scale);
    const int width = cvRound(410.0 * scale);
    const int height = cvRound(134.0 * scale);
    const int y = frame.rows - height - cvRound(36.0 * scale);
    if (x + width >= frame.cols || y <= 0) return;

    const cv::Rect panel_rect(x, y, width, height);
    cv::Mat panel = frame(panel_rect).clone();
    panel.setTo(cv::Scalar(14, 18, 22));
    cv::Mat destination = frame(panel_rect);
    cv::addWeighted(destination, 0.30, panel, 0.70, 0.0, destination);

    std::string title = spec.title.empty() ? "DATA MAP" : spec.title;
    double font = 0.46 * scale;
    int baseline = 0;
    cv::Size title_size = cv::getTextSize(
        title, cv::FONT_HERSHEY_SIMPLEX, font, 1, &baseline);
    if (title_size.width > width - 28) {
        font *= static_cast<double>(width - 28) / title_size.width;
        title_size = cv::getTextSize(title, cv::FONT_HERSHEY_SIMPLEX, font, 1,
                                    &baseline);
    }
    cv::putText(frame, title, {x + 14, y + 26},
                cv::FONT_HERSHEY_SIMPLEX, font, cv::Scalar(246, 248, 246),
                1, cv::LINE_AA);

    const int bar_x = x + 14;
    const int bar_y = y + 44;
    const int bar_width = width - 28;
    const int bar_height = cvRound(13.0 * scale);
    constexpr int kSteps = 64;
    for (int i = 0; i < kSteps; ++i) {
        const int left = bar_x + i * bar_width / kSteps;
        const int right = bar_x + (i + 1) * bar_width / kSteps;
        cv::rectangle(frame,
                      cv::Rect(left, bar_y, std::max(1, right - left), bar_height),
                      scheme_color(spec.scheme,
                                   static_cast<double>(i) / (kSteps - 1)),
                      cv::FILLED);
    }
    cv::rectangle(frame, {bar_x, bar_y, bar_width, bar_height},
                  cv::Scalar(222, 226, 228), 1, cv::LINE_AA);

    const std::string low = format_value(minimum, spec);
    const std::string high = format_value(maximum, spec);
    cv::putText(frame, low, {bar_x, y + 86}, cv::FONT_HERSHEY_SIMPLEX,
                0.40 * scale, cv::Scalar(220, 224, 226), 1, cv::LINE_AA);
    int high_baseline = 0;
    const cv::Size high_size = cv::getTextSize(
        high, cv::FONT_HERSHEY_SIMPLEX, 0.40 * scale, 1, &high_baseline);
    cv::putText(frame, high, {bar_x + bar_width - high_size.width, y + 86},
                cv::FONT_HERSHEY_SIMPLEX, 0.40 * scale,
                cv::Scalar(220, 224, 226), 1, cv::LINE_AA);
    cv::putText(frame, "values", {bar_x, y + 112}, cv::FONT_HERSHEY_SIMPLEX,
                0.34 * scale, cv::Scalar(150, 158, 164), 1, cv::LINE_AA);
}

}  // namespace

void draw_data_choropleth(cv::Mat& frame,
                          const GeoFeatureCollection& countries,
                          std::pair<double, double> camera_anchor,
                          double zoom, double progress,
                          const DataMapSpec& spec) {
    if (frame.empty() || countries.features.empty() || spec.values.empty()) return;
    progress = std::clamp(progress, 0.0, 1.0);
    const double scale = std::clamp(frame.rows / 1080.0, 0.45, 1.5);

    // Keep the terrain readable under the data tint.
    cv::Mat shade(frame.size(), CV_8UC3, cv::Scalar(18, 22, 26));
    cv::addWeighted(frame, 0.70, shade, 0.30, 0.0, frame);

    std::map<std::string, double> values;
    for (const auto& entry : spec.values) {
        values[normalized_key(entry.country)] = entry.value;
    }
    if (values.empty()) return;

    double minimum = spec.has_min ? spec.min_value : 1e300;
    double maximum = spec.has_max ? spec.max_value : -1e300;
    for (const auto& entry : spec.values) {
        minimum = std::min(minimum, entry.value);
        maximum = std::max(maximum, entry.value);
    }
    if (!std::isfinite(minimum) || !std::isfinite(maximum)) return;
    if (maximum <= minimum) maximum = minimum + 1.0;
    const double span = maximum - minimum;

    struct LabelCandidate {
        cv::Point point;
        std::string text;
        cv::Scalar accent;
        double value = 0.0;
    };
    std::vector<LabelCandidate> label_candidates;

    std::size_t index = 0;
    for (const auto& feature : countries.features) {
        double value = 0.0;
        const bool has_value = feature_value(feature, values, value);
        if (!has_value) {
            ++index;
            continue;
        }
        cv::Mat mask(frame.size(), CV_8UC1, cv::Scalar(0));
        std::vector<std::vector<cv::Point>> outlines;
        for (const auto& polygon : feature.polygons) {
            if (polygon.rings.empty()) continue;
            auto exterior = project_ring(polygon.rings.front(), camera_anchor,
                                         zoom, frame.size());
            if (exterior.size() < 3) continue;
            cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{exterior},
                         cv::Scalar(255), cv::LINE_AA);
            outlines.push_back(std::move(exterior));
            for (std::size_t ring = 1; ring < polygon.rings.size(); ++ring) {
                auto hole = project_ring(polygon.rings[ring], camera_anchor,
                                         zoom, frame.size());
                if (hole.size() >= 3) {
                    cv::fillPoly(mask,
                                 std::vector<std::vector<cv::Point>>{hole},
                                 cv::Scalar(0), cv::LINE_AA);
                }
            }
        }
        if (cv::countNonZero(mask) == 0) {
            ++index;
            continue;
        }

        const double normalized = std::clamp((value - minimum) / span, 0.0, 1.0);
        const cv::Scalar color = scheme_color(spec.scheme, normalized);
        const double local = std::clamp(
            progress * 1.45 - static_cast<double>(index % 10) * 0.014,
            0.0, 1.0);
        const double alpha = 0.64 * local;
        if (alpha > 0.001) {
            cv::Mat tint(frame.size(), CV_8UC3, color);
            cv::Mat blended;
            cv::addWeighted(frame, 1.0 - alpha, tint, alpha, 0.0, blended);
            blended.copyTo(frame, mask);
        }
        for (const auto& outline : outlines) {
            cv::polylines(frame, outline, true, cv::Scalar(238, 242, 240),
                          std::max(1, cvRound(1.2 * scale)), cv::LINE_AA);
        }

        if (spec.show_text && spec.labels != "none" && local > 0.55) {
            cv::Point label_point;
            if (feature_label_point(feature, camera_anchor, zoom, frame.size(),
                                    label_point)) {
                const std::string name = upper_name(feature_name(feature));
                const std::string value_text = format_value(value, spec);
                std::string text = name;
                if (spec.labels == "value") {
                    text = value_text;
                } else if (spec.labels == "both") {
                    text += "  " + value_text;
                }
                if (!text.empty()) {
                    label_candidates.push_back(
                        {label_point, text, color, value});
                }
            }
        }
        ++index;
    }

    std::sort(label_candidates.begin(), label_candidates.end(),
              [](const LabelCandidate& a, const LabelCandidate& b) {
                  return a.value > b.value;
              });
    std::vector<cv::Rect> placed_labels;
    for (const auto& candidate : label_candidates) {
        if (placed_labels.size() >= 8) break;
        const cv::Rect box = draw_country_label(
            frame, candidate.point, candidate.text, scale, candidate.accent,
            placed_labels);
        if (box.width > 0) placed_labels.push_back(box);
    }

    if (spec.show_text && !spec.title.empty()) {
        const int x = cvRound(38.0 * scale);
        const int baseline_y = cvRound(62.0 * scale);
        double font = 0.82 * scale;
        int baseline = 0;
        cv::Size extent = cv::getTextSize(
            spec.title, cv::FONT_HERSHEY_SIMPLEX, font, 2, &baseline);
        if (extent.width > frame.cols - x * 2) {
            font *= static_cast<double>(frame.cols - x * 2) / extent.width;
            extent = cv::getTextSize(spec.title, cv::FONT_HERSHEY_SIMPLEX, font,
                                     2, &baseline);
        }
        const cv::Rect title_background(
            x - cvRound(12.0 * scale), baseline_y - extent.height - cvRound(10.0 * scale),
            extent.width + cvRound(24.0 * scale),
            extent.height + baseline + cvRound(20.0 * scale));
        const cv::Rect title_roi = title_background &
            cv::Rect(0, 0, frame.cols, frame.rows);
        if (title_roi.area() > 0) {
            cv::Mat destination = frame(title_roi);
            cv::Mat panel = destination.clone();
            panel.setTo(cv::Scalar(14, 18, 22));
            cv::addWeighted(destination, 0.30, panel, 0.70, 0.0, destination);
        }
        cv::putText(frame, spec.title, {x, baseline_y},
                    cv::FONT_HERSHEY_SIMPLEX, font, cv::Scalar(246, 248, 246),
                    std::max(1, cvRound(2.0 * scale)), cv::LINE_AA);
    }
    if (spec.show_text && spec.show_legend) {
        draw_legend(frame, spec, minimum, maximum, scale);
    }
}

}  // namespace chrononplugin::maps
