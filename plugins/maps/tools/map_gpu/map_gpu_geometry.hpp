#pragma once

// Private to the map_gpu tool: the projected map-shape model and the extruded
// 2.5D geometry builder.

#include "map_gpu_math.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace chrononplugin::maps::gpu_detail {

struct Point2 {
    float x = 0.0f;
    float z = 0.0f;
};

struct MapShape {
    std::vector<std::vector<Point2>> rings;
    float min_x = 0.0f;
    float max_x = 0.0f;
    float min_z = 0.0f;
    float max_z = 0.0f;
};

struct Instance {
    float x = 0.0f;       // world x
    float z = 0.0f;       // world z
    float half_x = 0.0f;  // half width
    float half_z = 0.0f;  // half depth
    float height = 0.0f;  // world y extent
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

struct Geometry {
    std::vector<Instance> plates;
    std::vector<Instance> towers;
    std::vector<Vec3> outline;
    float max_height = 0.0f;
};

bool point_in_ring(const std::vector<Point2>& ring, float x, float z);

bool shape_contains(const MapShape& shape, float x, float z);

MapShape load_shape(const std::string& path, const std::string& feature_name,
                    std::string& error);

Geometry build_geometry(const MapShape& shape);

}  // namespace chrononplugin::maps::gpu_detail
