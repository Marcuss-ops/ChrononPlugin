#pragma once

#include <opencv2/core.hpp>

#include <string>
#include <utility>
#include <vector>

namespace chrononplugin::maps {

struct MapRouteStop {
    std::string label;
    double latitude = 0.0;
    double longitude = 0.0;
};

/// Draws a growing great-circle route, labeled stops and a moving pulse head.
/// progress is clamped to [0, 1].
void draw_animated_route(cv::Mat& frame, const std::vector<MapRouteStop>& stops,
                         std::pair<double, double> camera_anchor, double zoom,
                         double progress, int frame_index,
                         const std::vector<double>& altitude_m = {});

/// Draws one editorial location pin with a live OpenCV pulse.
void draw_animated_pin(cv::Mat& frame, cv::Point position, int frame_index,
                       const std::string& label);

}  // namespace chrononplugin::maps
