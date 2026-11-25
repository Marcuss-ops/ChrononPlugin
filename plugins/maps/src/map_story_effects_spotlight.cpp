// Country spotlight: the map is desaturated, the selected country keeps a
// warm fill and a traced glowing border, and the camera does the rest. This is
// the native counterpart of Mapcast's spotlight style, using the same named
// place polygons and camera pose as the other map compositions.

#include "chrononplugin/maps/map_story_effects.hpp"

#include "map_story_effects_internal.hpp"

#include "chrononplugin/maps/map_camera.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

namespace chrononplugin::maps {
using namespace story_detail;

void draw_country_spotlight(cv::Mat& frame,
                            const std::vector<GeoPolygon>& polygons,
                            std::pair<double, double> camera_anchor,
                            double zoom, double progress,
                            const std::string& label, bool show_text) {
    if (frame.empty() || polygons.empty()) return;
    progress = std::clamp(progress, 0.0, 1.0);
    const double eased = progress * progress * (3.0 - 2.0 * progress);
    const double scale = std::clamp(frame.rows / 1080.0, 0.45, 1.5);

    cv::Mat gray;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    cv::cvtColor(gray, frame, cv::COLOR_GRAY2BGR);
    cv::Mat shade(frame.size(), CV_8UC3, cv::Scalar(20, 26, 34));
    cv::addWeighted(frame, 0.72, shade, 0.28, 0.0, frame);

    const cv::Mat mask = territory_mask(frame, polygons, camera_anchor, zoom);
    if (cv::countNonZero(mask) == 0) return;
    const cv::Rect bounds = cv::boundingRect(mask);

    // Soft warm halo around the selected country.
    cv::Mat halo;
    const int kernel = std::max(3, cvRound(18.0 * scale) | 1);
    cv::dilate(mask, halo,
               cv::getStructuringElement(cv::MORPH_ELLIPSE, {kernel, kernel}));
    cv::GaussianBlur(halo, halo, {0, 0}, 18.0 * scale);
    cv::Mat halo_color(frame.size(), CV_8UC3, cv::Scalar(22, 78, 216));
    cv::Mat halo_blend;
    cv::addWeighted(frame, 1.0 - 0.44 * eased, halo_color, 0.44 * eased, 0.0,
                    halo_blend);
    halo_blend.copyTo(frame, halo);

    // Warm country fill on top of the desaturated plate.
    cv::Mat fill_color(frame.size(), CV_8UC3, cv::Scalar(34, 98, 232));
    cv::Mat fill_blend;
    cv::addWeighted(frame, 1.0 - 0.70 * eased, fill_color, 0.70 * eased, 0.0,
                    fill_blend);
    fill_blend.copyTo(frame, mask);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask.clone(), contours, cv::RETR_EXTERNAL,
                     cv::CHAIN_APPROX_SIMPLE);
    for (const auto& contour : contours) {
        cv::polylines(frame, contour, true, cv::Scalar(20, 30, 36),
                      std::max(3, cvRound(5.0 * scale)), cv::LINE_AA);
        cv::polylines(frame, contour, true, cv::Scalar(244, 248, 250),
                      std::max(1, cvRound(2.0 * scale)), cv::LINE_AA);
    }

    // A restrained tactical reticle keeps the spotlight readable without
    // turning this into a HUD composition.
    const int arm = cvRound(22.0 * scale);
    const int margin = cvRound(10.0 * scale);
    const cv::Scalar reticle(235, 242, 245);
    const int thickness = std::max(1, cvRound(1.5 * scale));
    const int left = bounds.x - margin;
    const int right = bounds.x + bounds.width + margin;
    const int top = bounds.y - margin;
    const int bottom = bounds.y + bounds.height + margin;
    cv::line(frame, {left, top}, {left + arm, top}, reticle, thickness, cv::LINE_AA);
    cv::line(frame, {left, top}, {left, top + arm}, reticle, thickness, cv::LINE_AA);
    cv::line(frame, {right, top}, {right - arm, top}, reticle, thickness, cv::LINE_AA);
    cv::line(frame, {right, top}, {right, top + arm}, reticle, thickness, cv::LINE_AA);
    cv::line(frame, {left, bottom}, {left + arm, bottom}, reticle, thickness, cv::LINE_AA);
    cv::line(frame, {left, bottom}, {left, bottom - arm}, reticle, thickness, cv::LINE_AA);
    cv::line(frame, {right, bottom}, {right - arm, bottom}, reticle, thickness, cv::LINE_AA);
    cv::line(frame, {right, bottom}, {right, bottom - arm}, reticle, thickness, cv::LINE_AA);

    if (show_text && !label.empty()) {
        const cv::Point anchor(bounds.x + bounds.width / 2,
                               bounds.y + bounds.height / 2);
        draw_location_tag(frame, anchor, label, eased);
    }
}

}  // namespace chrononplugin::maps
