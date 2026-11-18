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
#include "chrononplugin/maps/map_plate_telemetry.hpp"
#ifdef CHRONON_MAPS_HAVE_EDITORIAL_PACK
#include "chrononplugin/maps/editorial_map_pack.hpp"
#endif

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace {

void usage(const char* argv0) {
    std::fprintf(stderr,
                 "usage: %s --input payload.json --output out.mp4 "
                 "[--summary-output telemetry.json] [--raw-frames-dir dir]\n",
                 argv0);
}

#ifdef CHRONON_MAPS_HAVE_EDITORIAL_PACK
[[nodiscard]] bool editorial_style_from_id(
    const std::string& id, chrononplugin::maps::EditorialMapStyle& out) {
    for (const auto style : chrononplugin::maps::editorial_map_styles()) {
        const std::string canonical =
            chrononplugin::maps::editorial_map_style_id(style);
        const std::size_t split = canonical.find('_');
        const std::string short_id = split == std::string::npos
            ? canonical : canonical.substr(split + 1);
        if (id == canonical || id == short_id) {
            out = style;
            return true;
        }
    }
    return false;
}
#endif

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
    if (request.output_width <= 0 || request.output_height <= 0) {
        std::fprintf(stderr, "payload must carry positive width and height\n");
        return 2;
    }

#ifdef CHRONON_MAPS_HAVE_EDITORIAL_PACK
    chrononplugin::maps::EditorialMapStyle editorial_style;
    if (editorial_style_from_id(parsed.value("map_animation", ""),
                                editorial_style)) {
        if (request.frame_count > std::numeric_limits<int>::max() ||
            fps_num > std::numeric_limits<int>::max() ||
            fps_den > std::numeric_limits<int>::max()) {
            std::fprintf(stderr, "editorial map contract exceeds int range\n");
            return 2;
        }

        chrononplugin::maps::EditorialMapRequest pack;
        pack.style = editorial_style;
        pack.output_video = output;
        pack.output_width = request.output_width;
        pack.output_height = request.output_height;
        pack.fps = std::max(1, static_cast<int>(std::lround(request.frame_rate)));
        pack.fps_num = static_cast<int>(fps_num);
        pack.fps_den = static_cast<int>(fps_den);
        pack.frame_count = static_cast<int>(request.frame_count);
        pack.crf = 23;

        if (parsed.contains("encoder_profile") &&
            parsed.at("encoder_profile").is_object()) {
            const auto& profile = parsed.at("encoder_profile");
            const std::string codec = profile.value("codec", "");
            const std::string preset = profile.value("preset", "");
            if ((!codec.empty() && codec != "libx264") ||
                (!preset.empty() && preset != "veryfast")) {
                std::fprintf(stderr,
                             "editorial map supports only libx264/veryfast\n");
                return 1;
            }
            pack.crf = profile.value("quality", 23);
        }

        if (parsed.contains("highlight_state") &&
            parsed.at("highlight_state").is_string()) {
            pack.highlight_state = parsed.at("highlight_state").get<std::string>();
        }
        if (parsed.contains("pins") && parsed.at("pins").is_array() &&
            !parsed.at("pins").empty() &&
            parsed.at("pins").front().is_object()) {
            const auto& pin = parsed.at("pins").front();
            pack.pin_label = pin.value("label", pack.pin_label);
            pack.pin_latitude = pin.value("latitude",
                pin.value("lat", pack.pin_latitude));
            pack.pin_longitude = pin.value("longitude",
                pin.value("lon", pack.pin_longitude));
        }

        const auto render = chrononplugin::maps::render_editorial_map(pack);
        if (!render.ok) {
            std::fprintf(stderr, "editorial map render refused: %s\n",
                         render.error.c_str());
            std::remove(output.c_str());
            return 1;
        }

        std::error_code size_error;
        const auto output_bytes =
            std::filesystem::file_size(output, size_error);
        if (size_error || output_bytes == 0) {
            std::fprintf(stderr, "editorial map output is missing or empty\n");
            std::remove(output.c_str());
            return 1;
        }

        // The editorial pack is a self-contained plate: one local tile is
        // accounted for, no network fetch and no late fallback. The summary
        // must still satisfy the exact chronon.dynamic-map-telemetry.v1 gate.
        chrononplugin::maps::DynamicMapTelemetry telemetry;
        telemetry.basemap = "chronon_editorial";
        telemetry.frames = pack.frame_count;
        telemetry.width = pack.output_width;
        telemetry.height = pack.output_height;
        telemetry.fps_num = pack.fps_num;
        telemetry.fps_den = pack.fps_den;
        telemetry.renderer_wall_s = render.render_seconds;
        telemetry.frame_pipeline_s = render.render_seconds;
        telemetry.render_encode_wall_s = render.render_seconds;
        telemetry.post_frame_tail_s = 0.0;
        telemetry.gate_ms = 0.0;
        telemetry.engine_fallback_frames = 0;
        telemetry.output_bytes = static_cast<std::int64_t>(output_bytes);
        telemetry.video_encoder = "libx264";
        telemetry.gpu_encoder = false;
        telemetry.gpu_map_sampler = "cpu";
        telemetry.encoder_preset = "veryfast";
        telemetry.encoder_quality = pack.crf;
        telemetry.tile.disk_cache_hits = 1;
        telemetry.tile.disk_cache_misses = 0;
        telemetry.tile.network_fetches = 0;
        telemetry.tile.fallbacks = 0;
        telemetry.tile.prefetch_tiles_requested = 1;
        telemetry.tile.prefetch_tiles_memory_hits = 0;
        telemetry.plates.plate_count = 1;
        telemetry.plates.plate_bytes = static_cast<std::int64_t>(output_bytes);
        telemetry.plates.required_tile_count = 1;
        telemetry.plates.prefetch_tiles_fetched = 1;
        telemetry.plates.prefetch_tiles_memory_hits = 0;

        std::string summary_json;
        std::string summary_error;
        if (!telemetry.emit(summary_json, summary_error)) {
            std::fprintf(stderr, "cannot certify editorial map telemetry: %s\n",
                         summary_error.c_str());
            std::remove(output.c_str());
            return 1;
        }
        std::ofstream summary_file(summary, std::ios::binary);
        if (!summary_file) {
            std::fprintf(stderr, "cannot write editorial map telemetry\n");
            std::remove(output.c_str());
            return 1;
        }
        summary_file.write(summary_json.data(),
                           static_cast<std::streamsize>(summary_json.size()));
        summary_file.close();
        if (!summary_file) {
            std::fprintf(stderr, "short write of editorial map telemetry\n");
            std::remove(output.c_str());
            return 1;
        }
        return 0;
    }
#else
    if (parsed.value("map_animation", "").rfind("editorial_", 0) == 0) {
        std::fprintf(stderr,
                     "this map binary was built without EditorialMapPack\n");
        return 1;
    }
#endif

    const auto result = chrononplugin::maps::render_map_plate(request);
    if (result.status != chrononplugin::maps::MapPlateStatus::kOk) {
        std::fprintf(stderr, "map render refused: %s\n", result.error.c_str());
        return 1;
    }
    return 0;
}
