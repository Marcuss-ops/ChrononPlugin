// Standalone test for the native map-plate renderer (docs/tickets/
// TICKET-MAPS-NATIVE-PORT.md). Pins the fail-closed contract: malformed
// requests are rejected, compositions outside the native subset are refused
// by name, planning provenance carries no pixel semantics, and a supported
// request renders real outputs together with a complete certified telemetry
// summary.
#include "chrononplugin/maps/map_plate_render.hpp"
#include "chrononplugin/maps/map_plate_telemetry.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

using chrononplugin::maps::MapPlateRequest;
using chrononplugin::maps::MapPlateStatus;
using chrononplugin::maps::render_map_plate;

const std::string kSinglePinPayload =
    "{\"width\":64,\"height\":36,\"fps_num\":30,\"fps_den\":1,"
    "\"duration_us\":66667,"
    "\"pins\":[{\"id\":\"roma\",\"label\":\"Roma\","
    "\"latitude\":41.9028,\"longitude\":12.4964}]}";

MapPlateRequest request_for(const std::string& payload) {
    MapPlateRequest request;
    request.plate_id = "plate-new-orleans";
    request.payload_json = payload;
    request.output_width = 64;
    request.output_height = 36;
    request.frame_rate = 30.0;
    request.frame_count = 2;
    request.output_video = "/tmp/map_plate_render_test_out.mp4";
    request.summary_output = "/tmp/map_plate_render_test_out.telemetry.json";
    return request;
}

// One-pin payload plus the named extra field (JSON fragment).
std::string single_pin_with(const std::string& extra_field) {
    std::string payload =
        "{\"width\":64,\"height\":36,\"fps_num\":30,\"fps_den\":1,"
        "\"duration_us\":66667,"
        "\"pins\":[{\"id\":\"a\",\"label\":\"A\","
        "\"latitude\":41.9,\"longitude\":12.5}]";
    if (!extra_field.empty()) payload += "," + extra_field;
    payload += "}";
    return payload;
}

bool file_nonempty(const std::string& path) {
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0 && st.st_size > 0;
}

}  // namespace

int main() {
    // Malformed requests are rejected outright.
    CHECK(render_map_plate(MapPlateRequest{}).status == MapPlateStatus::kInvalidRequest);
    auto no_output = request_for(kSinglePinPayload);
    no_output.output_video.clear();
    CHECK(render_map_plate(no_output).status == MapPlateStatus::kInvalidRequest);
    auto bad_payload = request_for("{not json");
    CHECK(render_map_plate(bad_payload).status == MapPlateStatus::kInvalidRequest);
    auto no_pins = request_for("{\"width\":64,\"height\":36,\"pins\":[]}");
    CHECK(render_map_plate(no_pins).status == MapPlateStatus::kInvalidRequest);
    auto size_mismatch = request_for(kSinglePinPayload);
    size_mismatch.output_width = 32;
    CHECK(render_map_plate(size_mismatch).status == MapPlateStatus::kInvalidRequest);

    // Fields the renderer does not understand are refused, never guessed.
    auto unknown_field = request_for(single_pin_with("\"unmapped_field\":1"));
    const auto refused = render_map_plate(unknown_field);
    CHECK(refused.status == MapPlateStatus::kNotImplemented);
    CHECK(!refused.error.empty());

    // Pin-level fields follow the same rule: per-pin presentation belongs to
    // composed presentations that are not ported, so it is refused by name —
    // never silently dropped from the wire.
    const std::string extras_pin_payload =
        "{\"width\":64,\"height\":36,\"pins\":["
        "{\"id\":\"a\",\"label\":\"A\",\"latitude\":41.9,\"longitude\":12.5,"
        "\"color\":\"#FF3B30\"}]}";
    const auto refused_pin = render_map_plate(request_for(extras_pin_payload));
    CHECK(refused_pin.status == MapPlateStatus::kNotImplemented);
    CHECK(refused_pin.error.find("color") != std::string::npos);

    // Compositions outside the base flyover are refused BY NAME.
    CHECK(render_map_plate(request_for(single_pin_with("\"map_animation\":\"route_line\"")))
              .status == MapPlateStatus::kNotImplemented);
    CHECK(render_map_plate(request_for(
                              single_pin_with("\"pin_timing\":[{\"start_us\":0,\"end_us\":1}]")))
              .status == MapPlateStatus::kNotImplemented);
    CHECK(render_map_plate(request_for(single_pin_with("\"basemap\":\"esri_light\"")))
              .status == MapPlateStatus::kNotImplemented);
    CHECK(render_map_plate(request_for(single_pin_with("\"area_glow_radius_km\":12.0")))
              .status == MapPlateStatus::kNotImplemented);
    CHECK(render_map_plate(request_for(single_pin_with("\"route_timing\":{\"seconds_per_city\":1.0,\"travel_seconds\":1.0}")))
              .status == MapPlateStatus::kNotImplemented);
    CHECK(render_map_plate(request_for(single_pin_with("\"country_context\":{\"iso\":\"IT\"}")))
              .status == MapPlateStatus::kNotImplemented);

    // Unsupported animations stay refused, never guessed.
    CHECK(render_map_plate(request_for(single_pin_with("\"label_animation\":\"pop\"")))
              .status == MapPlateStatus::kNotImplemented);
    CHECK(render_map_plate(request_for(single_pin_with("\"sampler\":\"opencv_opencl\"")))
              .status == MapPlateStatus::kNotImplemented);
    auto nvenc = request_for(single_pin_with(
        "\"encoder_profile\":{\"codec\":\"h264_nvenc\",\"gpu\":true,"
        "\"preset\":\"p6\",\"quality\":15},\"sampler\":\"cpu\""));
    CHECK(render_map_plate(nvenc).status == MapPlateStatus::kNotImplemented);

    // The five-stop route budget is fail-closed.
    nlohmann::json six_pins = {
        {"width", 64}, {"height", 36}, {"pins", nlohmann::json::array()}};
    for (int i = 0; i < 6; ++i) {
        six_pins["pins"].push_back({{"id", "p" + std::to_string(i)},
                                    {"latitude", 10.0 + i},
                                    {"longitude", 20.0 + i}});
    }
    CHECK(render_map_plate(request_for(six_pins.dump())).status ==
          MapPlateStatus::kInvalidRequest);

    // Multi-pin tours need an explicit anchor until the tour is ported.
    const std::string tour_payload =
        "{\"width\":64,\"height\":36,\"pins\":["
        "{\"latitude\":41.9,\"longitude\":12.5,\"id\":\"a\"},"
        "{\"latitude\":40.8,\"longitude\":14.3,\"id\":\"b\"}]}";
    CHECK(render_map_plate(request_for(tour_payload)).status ==
          MapPlateStatus::kNotImplemented);

    if (::access("/usr/bin/ffmpeg", X_OK) != 0) {
        std::printf("note: /usr/bin/ffmpeg absent; render cases skipped\n");
    } else {
        // Planning provenance carries no pixel semantics: the Go lane may
        // send it and the render still certifies the base flyover.
        const std::string meta_payload = single_pin_with(
            "\"map_group\":\"mg\",\"map_group_strategy\":\"split\","
            "\"composition_family\":\"location_focus\","
            "\"composition_fallback\":\"\",\"typography\":\"default\","
            "\"map_animation\":\"\"");
        const auto meta_result = render_map_plate(request_for(meta_payload));
        CHECK(meta_result.status == MapPlateStatus::kOk);

        // A supported request renders real outputs and certified telemetry.
        const auto result = render_map_plate(request_for(kSinglePinPayload));
        CHECK(result.status == MapPlateStatus::kOk);
        if (result.status == MapPlateStatus::kOk) {
            CHECK(file_nonempty("/tmp/map_plate_render_test_out.mp4"));
            CHECK(file_nonempty("/tmp/map_plate_render_test_out.telemetry.json"));
            std::FILE* f =
                std::fopen("/tmp/map_plate_render_test_out.telemetry.json", "rb");
            CHECK(f != nullptr);
            if (f != nullptr) {
                std::string data;
                char buffer[4096];
                std::size_t got = 0;
                while ((got = std::fread(buffer, 1, sizeof(buffer), f)) > 0) {
                    data.append(buffer, got);
                }
                std::fclose(f);
                const nlohmann::json summary = nlohmann::json::parse(data);
                CHECK(summary.at("schema") == "chronon.dynamic-map-telemetry.v1");
                CHECK(summary.at("frames") == 2);
                CHECK(summary.at("output_bytes").get<std::int64_t>() > 0);
                CHECK(summary.at("renderer_fingerprint") ==
                      chrononplugin::maps::native_renderer_fingerprint());
            }
        }
        std::remove("/tmp/map_plate_render_test_out.mp4");
        std::remove("/tmp/map_plate_render_test_out.telemetry.json");
    }

    if (g_failures != 0) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("map_plate_render: all checks passed\n");
    return 0;
}
