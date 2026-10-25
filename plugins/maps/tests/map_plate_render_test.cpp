// Standalone test for the native map-plate scaffolding (no test framework
// dependency, same shape as geo_core_test.cpp). Pins the fail-closed
// contract of docs/tickets/TICKET-MAPS-NATIVE-PORT.md: a malformed request is
// rejected, an accepted request reports kNotImplemented, and no result ever
// fabricates success.
#include "chrononplugin/maps/map_plate_render.hpp"

#include <cstdio>
#include <string>

namespace {

int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

chrononplugin::maps::MapPlateRequest valid_request() {
    chrononplugin::maps::MapPlateRequest request;
    request.plate_id = "plate-new-orleans";
    request.payload_json = R"({"pins":["a"],"camera":{"kind":"flyover"}})";
    request.output_width = 1280;
    request.output_height = 720;
    request.frame_rate = 30.0;
    request.frame_count = 42;
    return request;
}

}  // namespace

int main() {
    using chrononplugin::maps::MapPlateStatus;
    using chrononplugin::maps::render_map_plate;

    // An accepted request is never fabricated into success: the port has not
    // landed, so the stub must say so and explain itself.
    const auto pending = render_map_plate(valid_request());
    CHECK(pending.status == MapPlateStatus::kNotImplemented);
    CHECK(!pending.error.empty());

    // Contract validation is live from day one: every malformed shape is
    // rejected with kInvalidRequest, never guessed at.
    auto empty_plate = valid_request();
    empty_plate.plate_id.clear();
    CHECK(render_map_plate(empty_plate).status == MapPlateStatus::kInvalidRequest);

    auto empty_payload = valid_request();
    empty_payload.payload_json.clear();
    CHECK(render_map_plate(empty_payload).status == MapPlateStatus::kInvalidRequest);

    auto zero_size = valid_request();
    zero_size.output_height = 0;
    CHECK(render_map_plate(zero_size).status == MapPlateStatus::kInvalidRequest);

    auto zero_frames = valid_request();
    zero_frames.frame_count = 0;
    CHECK(render_map_plate(zero_frames).status == MapPlateStatus::kInvalidRequest);

    // error is empty if and only if the status is kOk — no silent failures.
    for (const auto& result : {pending, render_map_plate(empty_plate)}) {
        CHECK(result.status == MapPlateStatus::kOk ? result.error.empty() : !result.error.empty());
    }

    if (g_failures != 0) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("map_plate_render scaffolding: all checks passed\n");
    return 0;
}
