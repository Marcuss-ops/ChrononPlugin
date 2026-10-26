// Chronon3D native map label rendering — TICKET-MAPS-NATIVE-PORT fase 3.
// Mirrors geo_documentary.draw_camera_aligned_label exactly: one cv::putText
// call, half-even rounded anchor, same font/scale/thickness/line type.

#include "chrononplugin/maps/map_label_render.hpp"

#include <cmath>

#include <opencv2/imgproc.hpp>

namespace chrononplugin::maps {

void draw_camera_aligned_label(cv::Mat& frame, std::pair<double, double> projected,
                               const std::string& text, cv::Scalar color_bgr) {
    const int x = static_cast<int>(std::nearbyint(projected.first));
    const int y = static_cast<int>(std::nearbyint(projected.second));
    cv::putText(frame, text, {x, y}, cv::FONT_HERSHEY_SIMPLEX, 0.6, color_bgr, 2,
                cv::LINE_AA);
}

}  // namespace chrononplugin::maps
