#pragma once

// Private to plugins/maps: the shared raster primitives of the story-effect
// treatments. map_story_effects.cpp is split into one translation unit per
// treatment family; only these helpers cross the split. This header is not
// part of the public plugin contract.

#include "chrononplugin/maps/geo_feature.hpp"

#include <opencv2/core.hpp>

#include <string>
#include <utility>
#include <vector>

namespace chrononplugin::maps::story_detail {

inline constexpr double kPi = 3.14159265358979323846;
// Consistent warm red territory palette (OpenCV colors are BGR).
inline const cv::Scalar kTerritoryRed(48, 59, 255);
inline const cv::Scalar kTerritoryGlow(28, 36, 220);
inline const cv::Scalar kTerritoryGlowInner(64, 75, 255);

std::vector<cv::Point> project_ring(const std::vector<GeoPosition>& ring,
                                    std::pair<double, double> anchor,
                                    double zoom, cv::Size size);

void draw_location_tag(cv::Mat& frame, cv::Point anchor, const std::string& label,
                       double opacity = 1.0);

cv::Rect draw_reference_label(cv::Mat& frame, const std::string& text,
                              cv::Point origin, cv::Scalar fill, cv::Scalar ink);

cv::Mat territory_mask(const cv::Mat& frame,
                       const std::vector<GeoPolygon>& polygons,
                       std::pair<double, double> anchor, double zoom);

}  // namespace chrononplugin::maps::story_detail
