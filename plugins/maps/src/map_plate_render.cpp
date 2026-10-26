// Chronon3D native map-plate rendering — TICKET-MAPS-NATIVE-PORT.
//
// Native flyover renderer: vector plate sampling (fase 2), camera-aligned
// labels (fase 3), the product "dive" motion (fase 4) and the certified
// chronon.dynamic-map-telemetry.v1 summary (fase 5). Outputs are staged and
// installed only together with a complete telemetry summary; every payload
// outside the native subset is refused, never guessed.

#include "chrononplugin/maps/map_plate_render.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "chrononplugin/maps/map_camera.hpp"
#include "chrononplugin/maps/map_label_render.hpp"
#include "chrononplugin/maps/map_plate_compose.hpp"
#include "chrononplugin/maps/map_plate_raster.hpp"
#include "chrononplugin/maps/map_plate_telemetry.hpp"

namespace chrononplugin::maps {

namespace {

/// The closed encoder profiles the product accepts (mirrors
/// render_dynamic_map_overlay.resolve_encoder_profile, exact tuples only).
struct EncoderProfile {
    std::string codec;
    bool gpu = false;
    std::string preset;
    int quality = 0;
};

bool resolve_encoder_profile(const nlohmann::json& profile, EncoderProfile& out,
                             std::string& error) {
    if (!profile.is_object()) {
        error = "encoder_profile must be an object";
        return false;
    }
    const std::string codec = profile.value("codec", "");
    EncoderProfile certified;
    if (codec == "h264_nvenc") {
        certified = {"h264_nvenc", true, "p6", 15};
    } else if (codec == "libx264") {
        certified = {"libx264", false, "veryfast", 23};
    } else {
        error = "unsupported dynamic-map encoder profile codec: " + codec;
        return false;
    }
    const EncoderProfile given{codec, profile.value("gpu", false),
                               profile.value("preset", ""), profile.value("quality", 0)};
    if (given.codec != certified.codec || given.gpu != certified.gpu ||
        given.preset != certified.preset || given.quality != certified.quality) {
        error = "unsupported or incomplete dynamic-map encoder profile";
        return false;
    }
    out = certified;
    return true;
}

/// render_dynamic_map_overlay.encoder_arguments (exact argument order).
std::vector<std::string> encoder_arguments(const EncoderProfile& profile) {
    if (profile.gpu) {
        return {"-c:v", profile.codec, "-gpu", "0", "-preset", profile.preset,
                "-tune", "hq", "-rc", "vbr", "-cq", std::to_string(profile.quality),
                "-b:v", "0"};
    }
    return {"-c:v", profile.codec, "-preset", profile.preset,
            "-crf", std::to_string(profile.quality)};
}

/// Shell single-quote escaping for the ffmpeg command line.
[[nodiscard]] std::string shell_quote(const std::string& value) {
    std::string out = "'";
    for (const char c : value) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

[[nodiscard]] double seconds_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

struct Pin {
    std::string text;
    double lat = 0.0;
    double lon = 0.0;
};

/// normalize_map_pins: one stop per coordinate (round-4 key), fail-closed
/// above the five-stop route budget.
bool normalize_pins(const nlohmann::json& raw_pins, std::vector<Pin>& pins,
                    std::string& error) {
    if (!raw_pins.is_array()) {
        error = "pins must be an array";
        return false;
    }
    std::set<std::pair<long, long>> seen;
    for (const auto& raw : raw_pins) {
        Pin pin;
        if (!raw.contains("latitude") || !raw.contains("longitude")) {
            error = "every pin requires latitude and longitude";
            return false;
        }
        pin.lat = raw.at("latitude").get<double>();
        pin.lon = raw.at("longitude").get<double>();
        const std::string label = raw.value("label", "");
        pin.text = label.empty() ? raw.value("id", "") : label;
        if (pin.text.empty()) {
            error = "every pin requires a label or id";
            return false;
        }
        const auto key = std::make_pair(
            std::lround(pin.lat * 1e4), std::lround(pin.lon * 1e4));
        if (seen.insert(key).second) {
            pins.push_back(std::move(pin));
        }
    }
    if (pins.empty()) {
        error = "the map plate requires at least one pin";
        return false;
    }
    if (pins.size() > 5) {
        error = "map group exceeds the five-stop route budget; the planner must "
                "split it (the renderer never silently drops a grounded place)";
        return false;
    }
    return true;
}

[[nodiscard]] const char* catalog_dir() {
    if (const char* env = std::getenv("CHRONON_MAPS_CATALOG_DIR")) {
        return env;
    }
#ifdef CHRONON_MAPS_CATALOG_DIR_DEFAULT
    return CHRONON_MAPS_CATALOG_DIR_DEFAULT;
#else
    return "";
#endif
}

/// The payload keys the renderer knows. Keys outside this set are refused:
/// the renderer never ignores a field it does not understand.
///
///  * metadata (map_group, composition_family, ...) are planning provenance
///    and carry no pixel semantics: accepted and recorded by the consumer;
///  * semantic keys must state what the native subset certifies, otherwise
///    the request is refused with kNotImplemented — never approximated.
[[nodiscard]] bool known_payload_keys(const nlohmann::json& payload,
                                      std::string& error) {
    static const std::set<std::string> kKnown = {
        // media contract
        "width", "height", "fps_num", "fps_den", "duration_us",
        // planning provenance (no pixel semantics)
        "plate_id", "map_group", "map_group_strategy", "composition_family",
        "composition_fallback", "typography",
        // semantics, verified below
        "pins", "pin_timing", "encoder_profile", "sampler",
        "camera_animation", "label_animation", "map_animation", "basemap",
        "route_timing", "country_context", "area_glow_radius_km",
        "camera_anchor", "camera_start_zoom", "camera_end_zoom",
    };
    for (auto it = payload.begin(); it != payload.end(); ++it) {
        if (kKnown.find(it.key()) == kKnown.end()) {
            error = "payload field outside the native map subset: " + it.key();
            return false;
        }
    }
    return true;
}

/// Semantic fields the native subset does not certify yet: any non-default
/// value is refused by name (the port adds them one parity gate at a time).
[[nodiscard]] bool supported_semantics(const nlohmann::json& payload,
                                       std::string& error) {
    const std::string map_animation = payload.value("map_animation", "");
    if (!map_animation.empty()) {
        error = "map composition '" + map_animation +
                "' is not ported natively yet (only the base flyover is)";
        return false;
    }
    const auto& pin_timing = payload.find("pin_timing");
    if (pin_timing != payload.end() && !pin_timing->is_null() &&
        !pin_timing->empty()) {
        error = "per-pin spoken timing schedules are not ported natively yet";
        return false;
    }
    if (payload.contains("route_timing")) {
        error = "route_timing map tours are not ported natively yet";
        return false;
    }
    if (payload.contains("country_context")) {
        error = "country_context map tours are not ported natively yet";
        return false;
    }
    if (payload.value("area_glow_radius_km", 0.0) != 0.0) {
        error = "area glow overlays are not ported natively yet";
        return false;
    }
    const std::string basemap = payload.value("basemap", "");
    if (!basemap.empty() && basemap != "chronon_editorial") {
        error = "basemap '" + basemap + "' is not available natively: the "
                "native renderer draws the editorial vector basemap only";
        return false;
    }
    return true;
}

}  // namespace

MapPlateRenderResult render_map_plate(const MapPlateRequest& request) {
    if (request.plate_id.empty() || request.payload_json.empty() ||
        request.output_width <= 0 || request.output_height <= 0 ||
        request.frame_rate <= 0.0 || request.frame_count <= 0 ||
        request.output_video.empty() || request.summary_output.empty()) {
        return {MapPlateStatus::kInvalidRequest,
                "map-plate request fails contract validation (plate id, payload, "
                "output size, frame rate, frame count, output video and summary "
                "output are all required)"};
    }

    nlohmann::json payload;
    try {
        payload = nlohmann::json::parse(request.payload_json);
    } catch (const std::exception& exc) {
        return {MapPlateStatus::kInvalidRequest,
                std::string("map-plate payload is not valid JSON: ") + exc.what()};
    }
    if (!payload.is_object()) {
        return {MapPlateStatus::kInvalidRequest, "map-plate payload must be an object"};
    }

    std::string error;
    if (!known_payload_keys(payload, error)) {
        return {MapPlateStatus::kNotImplemented, error};
    }
    if (!supported_semantics(payload, error)) {
        return {MapPlateStatus::kNotImplemented, error};
    }

    // Media contract: the payload and the request must agree.
    if (payload.contains("width") && payload.at("width").get<int>() != request.output_width) {
        return {MapPlateStatus::kInvalidRequest,
                "payload width does not match the request output width"};
    }
    if (payload.contains("height") &&
        payload.at("height").get<int>() != request.output_height) {
        return {MapPlateStatus::kInvalidRequest,
                "payload height does not match the request output height"};
    }
    // The media contract's exact rational frame rate (the cache identity
    // compares num/den, so it is preserved verbatim).
    int fps_num = static_cast<int>(std::lround(request.frame_rate * 1000.0));
    int fps_den = 1000;
    if (payload.contains("fps_num") || payload.contains("fps_den")) {
        const int num = payload.value("fps_num", 0);
        const int den = payload.value("fps_den", 0);
        if (num <= 0 || den <= 0 ||
            std::abs(request.frame_rate -
                     static_cast<double>(num) / static_cast<double>(den)) > 1e-9) {
            return {MapPlateStatus::kInvalidRequest,
                    "payload frame rate does not match the request frame rate"};
        }
        fps_num = num;
        fps_den = den;
    }

    // Camera/label animations: only the product "dive" and neutral labels.
    const std::string camera_animation = payload.value("camera_animation", "slow_approach");
    if (camera_animation != "slow_approach") {
        return {MapPlateStatus::kNotImplemented,
                "native map subset supports camera_animation 'slow_approach' only, got '" +
                    camera_animation + "'"};
    }
    const std::string label_animation = payload.value("label_animation", "none");
    if (label_animation != "none") {
        return {MapPlateStatus::kNotImplemented,
                "native map subset supports label_animation 'none' only, got '" +
                    label_animation + "'"};
    }

    // Encoder profile and sampler (closed contract).
    EncoderProfile profile{"libx264", false, "veryfast", 23};
    if (payload.contains("encoder_profile")) {
        if (!resolve_encoder_profile(payload.at("encoder_profile"), profile, error)) {
            return {MapPlateStatus::kInvalidRequest, error};
        }
    }
    std::string sampler = payload.value("sampler", "");
    if (sampler.empty()) {
        // render_dynamic_map_overlay.default_map_sampler
        sampler = (profile.codec == "h264_nvenc" && profile.gpu) ? "opencv_opencl" : "cpu";
    }
    if (sampler != "opencv_opencl" && sampler != "cpu") {
        return {MapPlateStatus::kInvalidRequest,
                "unsupported declared map sampler: " + sampler};
    }
    if (sampler != "cpu") {
        return {MapPlateStatus::kNotImplemented,
                "the native vector plates are CPU-sampled; sampler '" + sampler +
                    "' is not available natively yet"};
    }
    if (profile.codec == "h264_nvenc") {
        return {MapPlateStatus::kNotImplemented,
                "the NVENC map profile requires the OpenCL sampler, which the "
                "native renderer does not provide yet"};
    }

    // Pins and camera anchor.
    std::vector<Pin> pins;
    if (!normalize_pins(payload.value("pins", nlohmann::json::array()), pins, error)) {
        return {MapPlateStatus::kInvalidRequest, error};
    }
    std::pair<double, double> anchor{pins.front().lat, pins.front().lon};
    if (payload.contains("camera_anchor")) {
        const auto& a = payload.at("camera_anchor");
        if (!a.is_array() || a.size() != 2) {
            return {MapPlateStatus::kInvalidRequest,
                    "camera_anchor must be a [lat, lon] pair"};
        }
        anchor = {a.at(0).get<double>(), a.at(1).get<double>()};
    } else if (pins.size() > 1) {
        return {MapPlateStatus::kNotImplemented,
                "multi-pin map tours are not ported natively yet; provide an "
                "explicit camera_anchor or a single pin"};
    }
    const double start_zoom = payload.value("camera_start_zoom", 5.2);
    const double end_zoom = payload.value("camera_end_zoom", 17.8);

    // Boundary sources for the vector plates.
    const std::string base = catalog_dir();
    if (base[0] == '\0') {
        return {MapPlateStatus::kRenderFailed,
                "no map boundary catalog configured (CHRONON_MAPS_CATALOG_DIR)"};
    }
    const std::string countries = base + "/ne_50m_admin_0_countries.geojson";
    const std::string admin2 =
        base + "/italy_admin/geoBoundaries_ITA_ADM2.simplified.geojson";

    const auto total_start = std::chrono::steady_clock::now();
    const auto prepare_start = total_start;
    std::unique_ptr<VectorPlateRaster> raster;
    try {
        raster = std::make_unique<VectorPlateRaster>(VectorMapSources{countries, admin2});
    } catch (const std::exception& exc) {
        return {MapPlateStatus::kRenderFailed,
                std::string("cannot prepare the vector plates: ") + exc.what()};
    }
    const double prepare_ms = seconds_since(prepare_start) * 1e3;

    // Stage the video and the summary; install both only when certified.
    // Staged names keep the artifact suffixes so ffmpeg infers the container.
    const std::string staged_video = request.output_video + ".staged.mp4";
    const std::string staged_summary = request.summary_output + ".staged.json";

    std::string ffmpeg_cmd = "/usr/bin/ffmpeg -hide_banner -loglevel error -y";
    ffmpeg_cmd += " -f rawvideo -pix_fmt bgr24 -video_size " +
                  std::to_string(request.output_width) + "x" +
                  std::to_string(request.output_height);
    ffmpeg_cmd += " -framerate " + std::to_string(fps_num) + "/" +
                  std::to_string(fps_den);
    ffmpeg_cmd += " -i -";
    for (const std::string& arg : encoder_arguments(profile)) {
        ffmpeg_cmd += " " + arg;
    }
    ffmpeg_cmd += " -pix_fmt yuv420p -movflags +faststart " + shell_quote(staged_video);

    std::FILE* encoder = ::popen(ffmpeg_cmd.c_str(), "w");
    if (encoder == nullptr) {
        return {MapPlateStatus::kRenderFailed,
                "cannot launch the ffmpeg encoder for the map plate"};
    }

    PlateComposeStats stats;
    double compose_wall = 0.0;
    double frame_wall = 0.0;
    bool io_error = false;
    const std::size_t frame_bytes = static_cast<std::size_t>(request.output_width) *
                                    request.output_height * 3;
    std::vector<unsigned char> buffer(frame_bytes);

    const auto pipeline_start = std::chrono::steady_clock::now();
    for (std::int64_t f = 0; f < request.frame_count && !io_error; ++f) {
        const auto frame_start = std::chrono::steady_clock::now();
        const double zoom =
            builder_dive_zoom(start_zoom, end_zoom,
                              static_cast<int>(request.frame_count), static_cast<int>(f));
        const auto compose_start = std::chrono::steady_clock::now();
        cv::Mat frame = compose_sample_continuous(*raster, anchor.first, anchor.second,
                                                  zoom, request.output_width,
                                                  request.output_height, &stats);
        compose_wall += seconds_since(compose_start);
        for (const Pin& pin : pins) {
            const auto [x, y] = geo_to_screen(pin.lat, pin.lon, anchor, zoom,
                                              request.output_width,
                                              request.output_height);
            draw_camera_aligned_label(frame, {static_cast<double>(x),
                                              static_cast<double>(y)},
                                      pin.text, {255, 255, 255});
        }
        if (!request.raw_frames_dir.empty()) {
            const std::string path = request.raw_frames_dir + "/frame_" +
                                     std::to_string(1000000 + f).substr(1) + ".raw";
            std::FILE* raw = std::fopen(path.c_str(), "wb");
            if (raw == nullptr ||
                std::fwrite(frame.data, 1, frame_bytes, raw) != frame_bytes) {
                io_error = true;
            }
            if (raw != nullptr) std::fclose(raw);
        }
        if (std::fwrite(frame.data, 1, frame_bytes, encoder) != frame_bytes) {
            io_error = true;
        }
        frame_wall += seconds_since(frame_start);
    }
    const double pipeline_wall = seconds_since(pipeline_start);

    const int encoder_status = ::pclose(encoder);
    if (io_error || encoder_status != 0) {
        std::remove(staged_video.c_str());
        return {MapPlateStatus::kRenderFailed,
                "the native map render pipeline failed while writing frames"};
    }
    std::FILE* staged = std::fopen(staged_video.c_str(), "rb");
    std::int64_t output_bytes = 0;
    if (staged != nullptr) {
        std::fseek(staged, 0, SEEK_END);
        output_bytes = static_cast<std::int64_t>(std::ftell(staged));
        std::fclose(staged);
    }
    // Certified telemetry — the counters account for every tile the native
    // renderer produced (all rendered locally on the editorial basemap).
    DynamicMapTelemetry telemetry;
    telemetry.basemap = "chronon_editorial";
    telemetry.frames = request.frame_count;
    telemetry.width = request.output_width;
    telemetry.height = request.output_height;
    telemetry.fps_num = fps_num;
    telemetry.fps_den = fps_den;
    telemetry.renderer_wall_s = seconds_since(total_start);
    telemetry.frame_pipeline_s = frame_wall > 0.0 ? frame_wall : pipeline_wall;
    telemetry.render_encode_wall_s = pipeline_wall;
    telemetry.post_frame_tail_s = 0.0;
    telemetry.gate_ms = 0.0;
    telemetry.output_bytes = output_bytes;
    telemetry.video_encoder = profile.codec;
    telemetry.gpu_encoder = profile.gpu;
    telemetry.gpu_map_sampler = sampler;
    telemetry.encoder_preset = profile.preset;
    telemetry.encoder_quality = profile.quality;
    telemetry.tile.disk_cache_hits = 0;
    telemetry.tile.disk_cache_misses = stats.tile_renders;
    telemetry.tile.network_fetches = 0;
    telemetry.tile.fallbacks = 0;
    telemetry.tile.rendered_locally = stats.tile_renders;
    telemetry.tile.fetch_ms = compose_wall * 1e3;
    telemetry.tile.prefetch_ms = 0.0;
    telemetry.tile.prefetch_tiles_requested = stats.tile_renders;
    telemetry.tile.prefetch_tiles_memory_hits = 0;
    telemetry.plates.prepare_ms = prepare_ms;
    telemetry.plates.compose_ms = compose_wall * 1e3;
    telemetry.plates.plate_count = stats.plate_composes;
    telemetry.plates.plate_bytes = stats.plate_bytes;
    telemetry.plates.required_tile_count = stats.tile_renders;
    telemetry.plates.prefetch_tiles_fetched = stats.tile_renders;
    telemetry.plates.prefetch_tiles_memory_hits = 0;

    std::string summary_json;
    if (!telemetry.emit(summary_json, error)) {
        std::remove(staged_video.c_str());
        return {MapPlateStatus::kRenderFailed,
                "the native map render cannot certify its telemetry: " + error};
    }

    // Install the outputs together, only now that both exist.
    std::FILE* summary_file = std::fopen(staged_summary.c_str(), "wb");
    if (summary_file == nullptr ||
        std::fwrite(summary_json.data(), 1, summary_json.size(), summary_file) !=
            summary_json.size()) {
        if (summary_file != nullptr) std::fclose(summary_file);
        std::remove(staged_video.c_str());
        std::remove(staged_summary.c_str());
        return {MapPlateStatus::kRenderFailed,
                "cannot write the map plate telemetry summary"};
    }
    std::fclose(summary_file);
    if (std::rename(staged_video.c_str(), request.output_video.c_str()) != 0 ||
        std::rename(staged_summary.c_str(), request.summary_output.c_str()) != 0) {
        std::remove(staged_video.c_str());
        std::remove(staged_summary.c_str());
        std::remove(request.output_video.c_str());
        return {MapPlateStatus::kRenderFailed,
                "cannot install the map plate outputs"};
    }
    return {MapPlateStatus::kOk, ""};
}

}  // namespace chrononplugin::maps
