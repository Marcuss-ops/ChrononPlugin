#pragma once

#include "chrononplugin/maps/geo_feature.hpp"

#include <opencv2/core.hpp>

#include <string>
#include <utility>
#include <vector>

namespace chrononplugin::maps {

void draw_territory_wipe(cv::Mat& frame, const std::vector<GeoPolygon>& polygons,
                         std::pair<double, double> camera_anchor, double zoom,
                         double progress);

void draw_territory_callout(cv::Mat& frame, cv::Point geographic_anchor,
                            const std::string& place_name);

void apply_documentary_finish(cv::Mat& frame, int frame_index,
                              double vignette_strength, double grain_strength);

void apply_radial_motion_blur(cv::Mat& frame, double zoom_delta);

void draw_area_glow(cv::Mat& frame, cv::Point center, double radius_pixels,
                    double progress, const std::string& label);

}  // namespace chrononplugin::maps
