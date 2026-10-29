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
                            const std::string& place_name,
                            double opacity = 1.0);

void apply_documentary_finish(cv::Mat& frame, int frame_index,
                              double vignette_strength, double grain_strength);

void apply_radial_motion_blur(cv::Mat& frame, double zoom_delta);

void draw_area_glow(cv::Mat& frame, cv::Point center, double radius_pixels,
                    double progress, const std::string& label);

void draw_radar_reveal(cv::Mat& frame, cv::Point center, double progress,
                       const std::string& label);
void draw_satellite_scan(cv::Mat& frame, const std::vector<GeoPolygon>& polygons,
                         std::pair<double, double> camera_anchor, double zoom,
                         double progress, const std::string& label);
void draw_territory_flood_fill(cv::Mat& frame,
                               const std::vector<GeoPolygon>& polygons,
                               std::pair<double, double> camera_anchor,
                               double zoom, double progress,
                               const std::string& label);
void draw_light_beam_drop(cv::Mat& frame, cv::Point center, double progress,
                          const std::string& label);
void draw_city_network(cv::Mat& frame, const std::vector<cv::Point>& nodes,
                       const std::vector<std::string>& labels, double progress);
void draw_territory_extrusion(cv::Mat& frame,
                              const std::vector<GeoPolygon>& polygons,
                              std::pair<double, double> camera_anchor,
                              double zoom, double progress,
                              const std::string& label);
void draw_location_callout(cv::Mat& frame, cv::Point anchor,
                           const std::string& label, double opacity = 1.0);
void draw_mountain_ridge_callout(cv::Mat& frame, const cv::Mat& mountain_overlay,
                                 double progress, const std::string& label);
void draw_continental_atlas(cv::Mat& frame,
                            const GeoFeatureCollection& countries,
                            std::pair<double, double> camera_anchor,
                            double zoom, double progress,
                            const std::string& focus_country,
                            const std::vector<cv::Point>& label_points,
                            const std::vector<std::string>& labels);
void draw_historic_location_timeline(cv::Mat& frame,
                                     const std::vector<GeoPolygon>& country,
                                     std::pair<double, double> camera_anchor,
                                     double zoom, double progress,
                                     cv::Point location,
                                     const std::string& city,
                                     const std::string& date_label);

}  // namespace chrononplugin::maps
