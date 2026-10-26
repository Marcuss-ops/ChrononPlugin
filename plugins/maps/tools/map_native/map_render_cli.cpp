// Native dynamic-map renderer CLI — the drop-in command-line contract of the
// retired Python renderer (render_dynamic_map_overlay.py):
//
//   maps_dynamic_map_render --input payload.json --output out.mp4
//                           [--summary-output telemetry.json]
//                           [--raw-frames-dir dir]
//
// The payload is the immutable dynamic-map payload; the summary is the
// certified chronon.dynamic-map-telemetry.v1 record. Exit code 0 only when
// both artifacts are installed; every refusal explains itself on stderr.

#include "chrononplugin/maps/map_plate_render.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <cstddef>

namespace {

void usage(const char* argv0) {
    std::fprintf(stderr,
                 "usage: %s --input payload.json --output out.mp4 "
                 "[--summary-output telemetry.json] [--raw-frames-dir dir]\n",
                 argv0);
}

}  // namespace

int main(int argc, char** argv) {
    std::string input, output, summary, raw_dir;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](const char* what) -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", what);
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--input") input = next("--input");
        else if (arg == "--output") output = next("--output");
        else if (arg == "--summary-output") summary = next("--summary-output");
        else if (arg == "--raw-frames-dir") raw_dir = next("--raw-frames-dir");
        else {
            usage(argv[0]);
            return 2;
        }
    }
    if (input.empty() || output.empty()) {
        usage(argv[0]);
        return 2;
    }
    if (summary.empty()) summary = output + ".telemetry.json";

    std::FILE* f = std::fopen(input.c_str(), "rb");
    if (f == nullptr) {
        std::fprintf(stderr, "cannot open payload: %s\n", input.c_str());
        return 2;
    }
    std::string payload;
    char buffer[1 << 16];
    std::size_t got = 0;
    while ((got = std::fread(buffer, 1, sizeof(buffer), f)) > 0) {
        payload.append(buffer, got);
    }
    std::fclose(f);

    // Media contract from the payload (width, height, fps, duration).
    chrononplugin::maps::MapPlateRequest request;
    request.plate_id = "plate-native-map";
    request.payload_json = payload;
    request.output_video = output;
    request.summary_output = summary;
    request.raw_frames_dir = raw_dir;

    // The CLI derives the media contract from the payload fields the product
    // payload always carries.
    nlohmann::json parsed;
    try {
        parsed = nlohmann::json::parse(payload);
    } catch (const std::exception& exc) {
        std::fprintf(stderr, "payload is not valid JSON: %s\n", exc.what());
        return 2;
    }
    const long long fps_num = parsed.value("fps_num", -1);
    const long long fps_den = parsed.value("fps_den", -1);
    const long long duration_us = parsed.value("duration_us", -1);
    request.output_width = parsed.value("width", -1);
    request.output_height = parsed.value("height", -1);
    if (fps_num <= 0 || fps_den <= 0 || duration_us <= 0) {
        std::fprintf(stderr,
                     "payload must carry positive fps_num, fps_den and duration_us\n");
        return 2;
    }
    request.frame_rate = static_cast<double>(fps_num) / static_cast<double>(fps_den);
    request.frame_count = static_cast<long long>(std::nearbyint(
        static_cast<double>(duration_us) * static_cast<double>(fps_num) /
        (1e6 * static_cast<double>(fps_den))));
    if (request.frame_count < 2) {
        std::fprintf(stderr, "payload duration is too short for a map render\n");
        return 2;
    }

    const auto result = chrononplugin::maps::render_map_plate(request);
    if (result.status != chrononplugin::maps::MapPlateStatus::kOk) {
        std::fprintf(stderr, "map render refused: %s\n", result.error.c_str());
        return 1;
    }
    return 0;
}
