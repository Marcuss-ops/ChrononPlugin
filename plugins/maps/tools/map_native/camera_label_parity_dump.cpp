// Parity dump tool for the native camera/motion and label primitives.
//
// Evaluates the poses, screen projections, easing values and label frames
// named in a JSON spec and writes each result as a raw blob, so
// tools/map_native/parity_camera_label_check.py can compare them with the
// Python reference closures byte-for-byte. The spec is:
//
//   {
//     "poses":    [{"motion": "pure_dive"|"dive_tilt", "anchor": [lat, lon],
//                   "z0": .., "z1": .., "frames": N, "frame": i,
//                   "dive_end": .., "tilt_end": .., "tilt_deg": ..}, ...],
//     "screens":  [{"lat": .., "lon": .., "anchor": [lat, lon], "zoom": ..,
//                   "width": .., "height": ..}, ...],
//     "easings":  [p, ...],            // smootherstep samples
//     "banks":    [p, ...],            // roll_bank samples
//     "labels":   [{"x": .., "y": .., "text": "..", "color": [b, g, r],
//                   "width": .., "height": ..}, ...]
//   }
//
// Outputs: pose_<i>.bin (6 doubles), screen_<i>.bin (2 int32), ease_<i>.bin
// (1 double), bank_<i>.bin (1 double), label_<i>.bin (width*height*3 BGR).

#include "chrononplugin/maps/map_camera.hpp"
#include "chrononplugin/maps/map_label_render.hpp"

#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>

#include <cstdint>
#include <cstdio>
#include <string>

namespace {

int write_blob(const std::string& path, const void* data, std::size_t bytes) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) {
        std::fprintf(stderr, "cannot open output: %s\n", path.c_str());
        return 4;
    }
    const std::size_t written = std::fwrite(data, 1, bytes, f);
    std::fclose(f);
    if (written != bytes) {
        std::fprintf(stderr, "short write: %s\n", path.c_str());
        return 5;
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s <spec.json> <out_dir>\n", argv[0]);
        return 2;
    }
    std::FILE* f = std::fopen(argv[1], "rb");
    if (f == nullptr) {
        std::fprintf(stderr, "cannot open spec: %s\n", argv[1]);
        return 3;
    }
    std::string data;
    char buffer[1 << 16];
    std::size_t got = 0;
    while ((got = std::fread(buffer, 1, sizeof(buffer), f)) > 0) {
        data.append(buffer, got);
    }
    std::fclose(f);

    const nlohmann::json spec = nlohmann::json::parse(data);
    const std::string out_dir = argv[2];

    std::size_t index = 0;
    for (const auto& pose : spec.value("poses", nlohmann::json::array())) {
        const std::string motion = pose.at("motion").get<std::string>();
        const auto anchor_json = pose.at("anchor");
        const std::pair<double, double> anchor{anchor_json.at(0).get<double>(),
                                               anchor_json.at(1).get<double>()};
        const double z0 = pose.at("z0").get<double>();
        const double z1 = pose.at("z1").get<double>();
        const int frames = pose.at("frames").get<int>();
        const int frame = pose.at("frame").get<int>();
        chrononplugin::maps::CameraPose sampled{};
        if (motion == "pure_dive") {
            sampled = chrononplugin::maps::pure_dive_pose(anchor, z0, z1, frames, frame);
        } else if (motion == "dive_tilt") {
            sampled = chrononplugin::maps::dive_tilt_pose(
                anchor, z0, z1, pose.at("dive_end").get<int>(),
                pose.at("tilt_end").get<int>(), pose.at("tilt_deg").get<double>(),
                frames, frame);
        } else {
            std::fprintf(stderr, "unknown motion: %s\n", motion.c_str());
            return 6;
        }
        const std::string path = out_dir + "/pose_" + std::to_string(index) + ".bin";
        if (const int rc = write_blob(path, sampled.data(), sizeof(sampled)); rc != 0) return rc;
        ++index;
    }

    index = 0;
    for (const auto& screen : spec.value("screens", nlohmann::json::array())) {
        const auto anchor_json = screen.at("anchor");
        const std::pair<double, double> anchor{anchor_json.at(0).get<double>(),
                                               anchor_json.at(1).get<double>()};
        const auto [x, y] = chrononplugin::maps::geo_to_screen(
            screen.at("lat").get<double>(), screen.at("lon").get<double>(), anchor,
            screen.at("zoom").get<double>(), screen.at("width").get<int>(),
            screen.at("height").get<int>());
        const std::int32_t out[2] = {x, y};
        const std::string path = out_dir + "/screen_" + std::to_string(index) + ".bin";
        if (const int rc = write_blob(path, out, sizeof(out)); rc != 0) return rc;
        ++index;
    }

    index = 0;
    for (const auto& p : spec.value("easings", nlohmann::json::array())) {
        const double value = chrononplugin::maps::smootherstep(p.get<double>());
        const std::string path = out_dir + "/ease_" + std::to_string(index) + ".bin";
        if (const int rc = write_blob(path, &value, sizeof(value)); rc != 0) return rc;
        ++index;
    }

    index = 0;
    for (const auto& p : spec.value("banks", nlohmann::json::array())) {
        const double value = chrononplugin::maps::roll_bank(p.get<double>());
        const std::string path = out_dir + "/bank_" + std::to_string(index) + ".bin";
        if (const int rc = write_blob(path, &value, sizeof(value)); rc != 0) return rc;
        ++index;
    }

    index = 0;
    for (const auto& label : spec.value("labels", nlohmann::json::array())) {
        const int width = label.at("width").get<int>();
        const int height = label.at("height").get<int>();
        cv::Mat frame(height, width, CV_8UC3, cv::Scalar(0, 0, 0));
        const auto color = label.at("color");
        chrononplugin::maps::draw_camera_aligned_label(
            frame, {label.at("x").get<double>(), label.at("y").get<double>()},
            label.at("text").get<std::string>(),
            cv::Scalar(color.at(0).get<double>(), color.at(1).get<double>(),
                       color.at(2).get<double>()));
        const std::string path = out_dir + "/label_" + std::to_string(index) + ".bin";
        if (const int rc = write_blob(path, frame.data, frame.total() * frame.elemSize());
            rc != 0) {
            return rc;
        }
        ++index;
    }
    return 0;
}
