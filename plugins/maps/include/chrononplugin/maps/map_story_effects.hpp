#pragma once

#include "chrononplugin/maps/geo_feature.hpp"

#include <opencv2/core.hpp>

#include <string>
#include <utility>
#include <vector>

namespace chrononplugin::maps {

void draw_satellite_lock(cv::Mat& frame, std::pair<double, double> target,
                         std::pair<double, double> camera_anchor, double zoom,
                         double progress, const std::string& label,
                         bool show_telemetry = true);

void draw_crime_scene(cv::Mat& frame, std::pair<double, double> target,
                      std::pair<double, double> camera_anchor, double zoom,
                      double radius_m, double progress, int frame_index,
                      const std::string& label);

void draw_territory_wipe(cv::Mat& frame, const std::vector<GeoPolygon>& polygons,
                         std::pair<double, double> camera_anchor, double zoom,
                         double progress, const std::string& label);

void apply_documentary_finish(cv::Mat& frame, int frame_index,
                              double vignette_strength, double grain_strength);

void apply_radial_motion_blur(cv::Mat& frame, double zoom_delta);

void draw_area_glow(cv::Mat& frame, cv::Point center, double radius_pixels,
                    double progress);

}  // namespace chrononplugin::maps
