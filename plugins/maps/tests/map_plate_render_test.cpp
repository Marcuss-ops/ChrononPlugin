// Standalone test for the native map-plate renderer (docs/tickets/
// TICKET-MAPS-NATIVE-PORT.md). Pins the fail-closed contract: malformed
// requests are rejected, compositions outside the native subset are refused
// by name, planning provenance carries no pixel semantics, and a supported
// request renders real outputs together with a complete certified telemetry
// summary.
#include "chrononplugin/maps/map_plate_render.hpp"
#include "chrononplugin/maps/map_plate_telemetry.hpp"
#include "chrononplugin/maps/map_story_effects.hpp"

#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

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

bool extract_rgb_frame(const std::string& video, int frame, int width, int height,
                       std::vector<unsigned char>& pixels) {
    const std::string path = "/tmp/map_plate_render_test_frame.rgb";
    const std::string command =
        "ffmpeg -v error -y -i " + video + " -vf \"select=eq(n\\," +
        std::to_string(frame) +
        ")\" -vsync 0 -frames:v 1 -f rawvideo -pix_fmt rgb24 " + path;
    if (std::system(command.c_str()) != 0) return false;
    std::ifstream input(path, std::ios::binary);
    pixels.assign(std::istreambuf_iterator<char>(input),
                  std::istreambuf_iterator<char>());
    std::remove(path.c_str());
    return pixels.size() == static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height) * 3U;
}

// Render one composition at a fixed camera and return the mean absolute RGB
// delta between the midpoint and the final frame. A static tail returns ~0.
bool render_motion_delta(const std::string& payload, int width, int height,
                         int frames, int mid_frame, int end_frame, double& delta) {
    auto request = request_for(payload);
    request.output_width = width;
    request.output_height = height;
    request.frame_count = frames;
    const bool rendered = render_map_plate(request).status == MapPlateStatus::kOk;
    std::vector<unsigned char> mid;
    std::vector<unsigned char> end;
    const bool decoded = rendered &&
        extract_rgb_frame(request.output_video, mid_frame, width, height, mid) &&
        extract_rgb_frame(request.output_video, end_frame, width, height, end);
    std::remove(request.output_video.c_str());
    std::remove(request.summary_output.c_str());
    if (!decoded || mid.size() != end.size() || mid.empty()) return false;
    double total = 0.0;
    for (std::size_t i = 0; i < mid.size(); ++i) {
        total += std::abs(static_cast<int>(mid[i]) - static_cast<int>(end[i]));
    }
    delta = total / static_cast<double>(mid.size());
    return true;
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

    // Unknown map compositions are refused; route_line is now natively ported.
    CHECK(render_map_plate(request_for(single_pin_with("\"map_animation\":\"3d_orbit\"")))
              .status == MapPlateStatus::kNotImplemented);
    CHECK(render_map_plate(request_for(single_pin_with(
              "\"map_animation\":\"territory_tactical_fill\"")))
              .status == MapPlateStatus::kNotImplemented);
    CHECK(render_map_plate(request_for(single_pin_with(
              "\"territory_subtitle\":\"not a tactical map\"")))
              .status == MapPlateStatus::kNotImplemented);
    CHECK(render_map_plate(request_for(single_pin_with(
              "\"relief_overlay\":\"made_up_style\"")))
              .status == MapPlateStatus::kNotImplemented);
    CHECK(render_map_plate(request_for(
                              single_pin_with("\"pin_timing\":[{\"start_us\":0,\"end_us\":1}]")))
              .status == MapPlateStatus::kNotImplemented);
    CHECK(render_map_plate(request_for(single_pin_with("\"basemap\":\"esri_light\"")))
              .status == MapPlateStatus::kNotImplemented);
    CHECK(render_map_plate(request_for(single_pin_with("\"route_timing\":{\"seconds_per_city\":1.0,\"travel_seconds\":1.0}")))
              .status == MapPlateStatus::kNotImplemented);
    CHECK(render_map_plate(request_for(single_pin_with("\"country_context\":{\"iso\":\"IT\"}")))
              .status == MapPlateStatus::kNotImplemented);

    // Unsupported animations stay refused, never guessed.
    CHECK(render_map_plate(request_for(single_pin_with("\"label_animation\":\"pop\"")))
              .status == MapPlateStatus::kNotImplemented);
    CHECK(render_map_plate(request_for(single_pin_with(
              "\"map_animation\":\"data_choropleth_reveal\"")))
              .status == MapPlateStatus::kNotImplemented);
    CHECK(render_map_plate(request_for(single_pin_with(
              "\"map_animation\":\"country_spotlight_reveal\"")))
              .status == MapPlateStatus::kNotImplemented);
    CHECK(render_map_plate(request_for(single_pin_with("\"sampler\":\"opencv_opencl\"")))
              .status == MapPlateStatus::kNotImplemented);

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

    // Multi-pin routes need an explicit anchor.
    const std::string tour_payload =
        "{\"width\":64,\"height\":36,\"pins\":["
        "{\"latitude\":41.9,\"longitude\":12.5,\"id\":\"a\"},"
        "{\"latitude\":40.8,\"longitude\":14.3,\"id\":\"b\"}]}";
    CHECK(render_map_plate(request_for(tour_payload)).status ==
          MapPlateStatus::kNotImplemented);

    // area_glow_pulse must keep moving for the whole window. A half-cycle
    // offset keeps the midpoint and the end on different breath samples.
    {
        const std::int64_t duration_us = 5'600'000;
        const double mid_phase =
            chrononplugin::maps::area_glow_pulse_phase(0.5, duration_us);
        const double end_phase =
            chrononplugin::maps::area_glow_pulse_phase(1.0, duration_us);
        CHECK(std::fabs(mid_phase - end_phase) > 0.1);
        cv::Mat mid(180, 320, CV_8UC3, cv::Scalar(24, 26, 28));
        cv::Mat end = mid.clone();
        chrononplugin::maps::draw_area_glow(mid, {160, 90}, 80.0, mid_phase,
                                            "Firenze");
        chrononplugin::maps::draw_area_glow(end, {160, 90}, 80.0, end_phase,
                                            "Firenze");
        CHECK(cv::norm(mid, end, cv::NORM_L1) > 0.0);
    }

    // The paper-atlas treatment is a full-window composition: its folded
    // opening and its scan wipe must both be non-static even without a camera.
    {
        cv::Mat paper_folded(360, 640, CV_8UC3, cv::Scalar(28, 36, 44));
        cv::Mat paper_open = paper_folded.clone();
        cv::Mat paper_map = paper_folded.clone();
        chrononplugin::maps::draw_paper_map_fold(paper_folded, 0.16,
                                                 "Europa", true);
        chrononplugin::maps::draw_paper_map_fold(paper_open, 0.38,
                                                 "Europa", true);
        chrononplugin::maps::draw_paper_map_fold(paper_map, 0.92,
                                                 "Europa", true);
        CHECK(cv::norm(paper_folded, paper_open, cv::NORM_L1) > 0.0);
        CHECK(cv::norm(paper_open, paper_map, cv::NORM_L1) > 0.0);
    }

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

        // A live three-stop route is rendered entirely in native C++/OpenCV.
        const std::string route_payload =
            "{\"width\":64,\"height\":36,\"fps_num\":30,\"fps_den\":1,"
            "\"duration_us\":66667,\"map_animation\":\"route_line\","
            "\"camera_anchor\":[41.9028,12.4964],\"camera_start_zoom\":5.8,"
            "\"camera_end_zoom\":6.1,\"pins\":["
            "{\"id\":\"roma\",\"label\":\"Roma\",\"latitude\":41.9028,\"longitude\":12.4964},"
            "{\"id\":\"firenze\",\"label\":\"Firenze\",\"latitude\":43.7696,\"longitude\":11.2558},"
            "{\"id\":\"venezia\",\"label\":\"Venezia\",\"latitude\":45.4408,\"longitude\":12.3155}]}";
        const auto route = render_map_plate(request_for(route_payload));
        CHECK(route.status == MapPlateStatus::kOk);
        if (route.status == MapPlateStatus::kOk) {
            CHECK(file_nonempty("/tmp/map_plate_render_test_out.mp4"));
            CHECK(file_nonempty("/tmp/map_plate_render_test_out.telemetry.json"));
        }
        std::remove("/tmp/map_plate_render_test_out.mp4");
        std::remove("/tmp/map_plate_render_test_out.telemetry.json");

        // Area glow is a supported composition, not a refused field.
        const std::string area_glow_payload =
            "{\"width\":64,\"height\":36,\"fps_num\":30,\"fps_den\":1,"
            "\"duration_us\":66667,\"map_animation\":\"area_glow_pulse\","
            "\"area_glow_radius_km\":12.0,\"pins\":[{\"id\":\"a\","
            "\"label\":\"A\",\"latitude\":41.9,\"longitude\":12.5}]}";
        const auto area_glow = render_map_plate(request_for(area_glow_payload));
        CHECK(area_glow.status == MapPlateStatus::kOk);
        if (area_glow.status == MapPlateStatus::kOk) {
            CHECK(file_nonempty("/tmp/map_plate_render_test_out.mp4"));
            CHECK(file_nonempty("/tmp/map_plate_render_test_out.telemetry.json"));
        }
        std::remove("/tmp/map_plate_render_test_out.mp4");
        std::remove("/tmp/map_plate_render_test_out.telemetry.json");

        // Data choropleth: country fills, a legend and name/value labels are
        // rendered from the payload's data object on the Natural Earth set.
        const std::string data_map_payload =
            "{\"width\":64,\"height\":36,\"fps_num\":30,\"fps_den\":1,"
            "\"duration_us\":66667,\"map_animation\":\"data_choropleth_reveal\","
            "\"camera_anchor\":[48.0,10.0],\"camera_start_zoom\":3.6,"
            "\"camera_end_zoom\":3.8,\"pins\":[{\"id\":\"europe\","
            "\"label\":\"Europa\",\"latitude\":48.0,\"longitude\":10.0}],"
            "\"data\":{\"title\":\"Test data\",\"scheme\":\"heat\","
            "\"labels\":\"both\",\"values\":{\"Italy\":1.9,\"France\":2.8}}}";
        const auto data_map = render_map_plate(request_for(data_map_payload));
        CHECK(data_map.status == MapPlateStatus::kOk);
        if (data_map.status == MapPlateStatus::kOk) {
            CHECK(file_nonempty("/tmp/map_plate_render_test_out.mp4"));
            CHECK(file_nonempty("/tmp/map_plate_render_test_out.telemetry.json"));
        }
        std::remove("/tmp/map_plate_render_test_out.mp4");
        std::remove("/tmp/map_plate_render_test_out.telemetry.json");

        // Data bars: the same validated data values as the choropleth, laid
        // out as a staggered bar chart beside the map.
        const std::string data_bars_payload =
            "{\"width\":64,\"height\":36,\"fps_num\":30,\"fps_den\":1,"
            "\"duration_us\":66667,\"map_animation\":\"data_bars_reveal\","
            "\"camera_anchor\":[48.0,10.0],\"camera_start_zoom\":3.6,"
            "\"camera_end_zoom\":3.8,\"pins\":[{\"id\":\"europe\","
            "\"label\":\"Europa\",\"latitude\":48.0,\"longitude\":10.0}],"
            "\"data\":{\"title\":\"Test bars\",\"scheme\":\"heat\","
            "\"values\":{\"Italy\":1.9,\"France\":2.8}}}";
        const auto data_bars = render_map_plate(request_for(data_bars_payload));
        CHECK(data_bars.status == MapPlateStatus::kOk);
        if (data_bars.status == MapPlateStatus::kOk) {
            CHECK(file_nonempty("/tmp/map_plate_render_test_out.mp4"));
            CHECK(file_nonempty("/tmp/map_plate_render_test_out.telemetry.json"));
        }
        std::remove("/tmp/map_plate_render_test_out.mp4");
        std::remove("/tmp/map_plate_render_test_out.telemetry.json");

        // Label choreography: five projected pins, five label treatments.
        const std::string label_payload =
            "{\"width\":64,\"height\":36,\"fps_num\":30,\"fps_den\":1,"
            "\"duration_us\":66667,\"map_animation\":\"label_choreography\","
            "\"camera_anchor\":[49.0,8.0],\"camera_start_zoom\":3.6,"
            "\"camera_end_zoom\":3.8,\"pins\":["
            "{\"id\":\"it\",\"label\":\"ITALIA\",\"latitude\":41.9,\"longitude\":12.5},"
            "{\"id\":\"fr\",\"label\":\"FRANCIA\",\"latitude\":48.85,\"longitude\":2.35},"
            "{\"id\":\"de\",\"label\":\"GERMANIA\",\"latitude\":52.52,\"longitude\":13.4}]}";
        const auto label_map = render_map_plate(request_for(label_payload));
        CHECK(label_map.status == MapPlateStatus::kOk);
        if (label_map.status == MapPlateStatus::kOk) {
            CHECK(file_nonempty("/tmp/map_plate_render_test_out.mp4"));
            CHECK(file_nonempty("/tmp/map_plate_render_test_out.telemetry.json"));
        }
        std::remove("/tmp/map_plate_render_test_out.mp4");
        std::remove("/tmp/map_plate_render_test_out.telemetry.json");

        // Country spotlight: named-place polygons, desaturated surroundings
        // and a warm focus treatment. map_place is the fail-closed identity.
        const std::string spotlight_payload =
            "{\"width\":64,\"height\":36,\"fps_num\":30,\"fps_den\":1,"
            "\"duration_us\":66667,\"map_animation\":\"country_spotlight_reveal\","
            "\"map_place\":\"Italy\",\"pins\":[{\"id\":\"roma\","
            "\"label\":\"Italia\",\"latitude\":41.9028,\"longitude\":12.4964}]}";
        const auto spotlight = render_map_plate(request_for(spotlight_payload));
        CHECK(spotlight.status == MapPlateStatus::kOk);
        if (spotlight.status == MapPlateStatus::kOk) {
            CHECK(file_nonempty("/tmp/map_plate_render_test_out.mp4"));
            CHECK(file_nonempty("/tmp/map_plate_render_test_out.telemetry.json"));
        }
        std::remove("/tmp/map_plate_render_test_out.mp4");
        std::remove("/tmp/map_plate_render_test_out.telemetry.json");

        // Paper atlas: the sheet opens over the plate and is then wiped into
        // the live map; it must render natively from the base plate alone.
        const std::string paper_payload =
            "{\"width\":320,\"height\":180,\"fps_num\":30,\"fps_den\":1,"
            "\"duration_us\":266667,\"map_animation\":\"paper_map_fold\","
            "\"camera_anchor\":[50.0,10.0],\"camera_start_zoom\":3.8,"
            "\"camera_end_zoom\":4.2,\"pins\":[{\"id\":\"europe\","
            "\"label\":\"Europa\",\"latitude\":50.0,\"longitude\":10.0}]}";
        auto paper_request = request_for(paper_payload);
        paper_request.output_width = 320;
        paper_request.output_height = 180;
        paper_request.frame_count = 8;
        const auto paper_result = render_map_plate(paper_request);
        CHECK(paper_result.status == MapPlateStatus::kOk);
        if (paper_result.status == MapPlateStatus::kOk) {
            CHECK(file_nonempty(paper_request.output_video));
            CHECK(file_nonempty(paper_request.summary_output));
        }
        std::remove(paper_request.output_video.c_str());
        std::remove(paper_request.summary_output.c_str());

        // Globe dive: the Natural Earth world collection is projected
        // orthographically and then handed off to the live map.
        const std::string globe_payload =
            "{\"width\":320,\"height\":180,\"fps_num\":30,\"fps_den\":1,"
            "\"duration_us\":266667,\"map_animation\":\"globe_dive\","
            "\"camera_anchor\":[50.0,10.0],\"camera_start_zoom\":3.8,"
            "\"camera_end_zoom\":4.2,\"pins\":[{\"id\":\"europe\","
            "\"label\":\"Europa\",\"latitude\":50.0,\"longitude\":10.0}]}";
        auto globe_request = request_for(globe_payload);
        globe_request.output_width = 320;
        globe_request.output_height = 180;
        globe_request.frame_count = 8;
        const auto globe_result = render_map_plate(globe_request);
        CHECK(globe_result.status == MapPlateStatus::kOk);
        if (globe_result.status == MapPlateStatus::kOk) {
            CHECK(file_nonempty(globe_request.output_video));
            CHECK(file_nonempty(globe_request.summary_output));
        }
        std::remove(globe_request.output_video.c_str());
        std::remove(globe_request.summary_output.c_str());

        // Full-window motion: a fixed camera leaves the pulse as the only
        // moving element, so the midpoint and the last frame must differ.
        const std::string pulse_payload =
            "{\"width\":320,\"height\":180,\"fps_num\":30,\"fps_den\":1,"
            "\"duration_us\":2000000,\"map_animation\":\"area_glow_pulse\","
            "\"area_glow_radius_km\":12.0,\"camera_start_zoom\":8.0,"
            "\"camera_end_zoom\":8.0,\"pins\":[{\"id\":\"firenze\","
            "\"label\":\"Firenze\",\"latitude\":43.7696,\"longitude\":11.2558}]}";
        auto pulse_request = request_for(pulse_payload);
        pulse_request.output_width = 320;
        pulse_request.output_height = 180;
        pulse_request.frame_count = 60;
        const auto pulse_result = render_map_plate(pulse_request);
        CHECK(pulse_result.status == MapPlateStatus::kOk);
        if (pulse_result.status == MapPlateStatus::kOk) {
            std::vector<unsigned char> mid_pixels;
            std::vector<unsigned char> end_pixels;
            CHECK(extract_rgb_frame(pulse_request.output_video, 30, 320, 180,
                                    mid_pixels));
            CHECK(extract_rgb_frame(pulse_request.output_video, 59, 320, 180,
                                    end_pixels));
            if (mid_pixels.size() == end_pixels.size() && !mid_pixels.empty()) {
                double delta = 0.0;
                for (std::size_t i = 0; i < mid_pixels.size(); ++i) {
                    delta += std::abs(static_cast<int>(mid_pixels[i]) -
                                      static_cast<int>(end_pixels[i]));
                }
                delta /= static_cast<double>(mid_pixels.size());
                CHECK(delta > 0.1);
            }
        }
        std::remove(pulse_request.output_video.c_str());
        std::remove(pulse_request.summary_output.c_str());

        // The already-certified route families must also keep moving to the
        // end. A fixed camera isolates the route progress from the camera.
        const std::string route_motion_payload =
            "{\"width\":320,\"height\":180,\"fps_num\":30,\"fps_den\":1,"
            "\"duration_us\":2000000,\"map_animation\":\"route_line\","
            "\"camera_anchor\":[42.0,12.0],\"camera_start_zoom\":5.5,"
            "\"camera_end_zoom\":5.5,\"pins\":["
            "{\"id\":\"roma\",\"label\":\"Roma\",\"latitude\":41.9028,\"longitude\":12.4964},"
            "{\"id\":\"firenze\",\"label\":\"Firenze\",\"latitude\":43.7696,\"longitude\":11.2558}]}";
        double route_delta = 0.0;
        CHECK(render_motion_delta(route_motion_payload, 320, 180, 60, 30, 59,
                                  route_delta));
        CHECK(route_delta > 0.1);

        const std::string multi_leg_motion_payload =
            "{\"width\":320,\"height\":180,\"fps_num\":30,\"fps_den\":1,"
            "\"duration_us\":2000000,\"map_animation\":\"multi_leg_flight\","
            "\"camera_anchor\":[43.0,12.0],\"camera_start_zoom\":5.5,"
            "\"camera_end_zoom\":5.5,\"pins\":["
            "{\"id\":\"roma\",\"label\":\"Roma\",\"latitude\":41.9028,\"longitude\":12.4964},"
            "{\"id\":\"firenze\",\"label\":\"Firenze\",\"latitude\":43.7696,\"longitude\":11.2558},"
            "{\"id\":\"venezia\",\"label\":\"Venezia\",\"latitude\":45.4408,\"longitude\":12.3155}]}";
        double multi_leg_delta = 0.0;
        CHECK(render_motion_delta(multi_leg_motion_payload, 320, 180, 60, 30,
                                  59, multi_leg_delta));
        CHECK(multi_leg_delta > 0.1);

        // h264_nvenc rejects 64x36 (below its minimum frame size), so the
        // NVENC contract is exercised at a valid 320x180 resolution.
        const std::string nvenc_payload =
            "{\"width\":320,\"height\":180,\"fps_num\":30,\"fps_den\":1,"
            "\"duration_us\":66667,\"encoder_profile\":{\"codec\":\"h264_nvenc\","
            "\"gpu\":true,\"preset\":\"p6\",\"quality\":15},\"sampler\":\"cpu\","
            "\"pins\":[{\"id\":\"a\",\"label\":\"A\",\"latitude\":41.9,"
            "\"longitude\":12.5}]}";
        auto nvenc = request_for(nvenc_payload);
        nvenc.output_width = 320;
        nvenc.output_height = 180;
        const auto nvenc_result = render_map_plate(nvenc);
        CHECK(nvenc_result.status == MapPlateStatus::kOk);
        if (nvenc_result.status == MapPlateStatus::kOk) {
            std::ifstream summary_file("/tmp/map_plate_render_test_out.telemetry.json");
            nlohmann::json summary;
            summary_file >> summary;
            CHECK(summary.at("video_encoder") == "h264_nvenc");
            CHECK(summary.at("gpu_encoder").get<bool>());
        }
        std::remove("/tmp/map_plate_render_test_out.mp4");
        std::remove("/tmp/map_plate_render_test_out.telemetry.json");

        // The tactical treatment accepts a payload-supplied GeoJSON catalog:
        // this is the arbitrary-territory path used by the Jerusalem showcase.
        const std::string tactical_geojson = "/tmp/map_plate_tactical_test.geojson";
        {
            std::ofstream geojson(tactical_geojson);
            geojson << R"({"type":"FeatureCollection","features":[{"type":"Feature","properties":{"shapeName":"Testland"},"geometry":{"type":"Polygon","coordinates":[[[12.4,41.8],[12.6,41.8],[12.6,42.0],[12.4,42.0],[12.4,41.8]]]}}]})";
        }
        const std::string tactical_payload =
            "{\"width\":64,\"height\":36,\"fps_num\":30,\"fps_den\":1,"
            "\"duration_us\":66667,\"map_animation\":\"territory_tactical_fill\","
            "\"territory_geojson\":\"" + tactical_geojson + "\","
            "\"territory_feature\":\"Testland\",\"territory_subtitle\":\"Test subtitle\","
            "\"camera_anchor\":[41.9,12.5],\"camera_start_zoom\":8.0,"
            "\"camera_end_zoom\":8.5,\"pins\":[{\"id\":\"testland\",\"label\":\"Testland\","
            "\"latitude\":41.9,\"longitude\":12.5}]}";
        const auto tactical = render_map_plate(request_for(tactical_payload));
        CHECK(tactical.status == MapPlateStatus::kOk);
        if (tactical.status == MapPlateStatus::kOk) {
            CHECK(file_nonempty("/tmp/map_plate_render_test_out.mp4"));
            CHECK(file_nonempty("/tmp/map_plate_render_test_out.telemetry.json"));
        }
        std::remove("/tmp/map_plate_render_test_out.mp4");
        std::remove("/tmp/map_plate_render_test_out.telemetry.json");
        std::remove(tactical_geojson.c_str());
    }

    if (g_failures != 0) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("map_plate_render: all checks passed\n");
    return 0;
}
