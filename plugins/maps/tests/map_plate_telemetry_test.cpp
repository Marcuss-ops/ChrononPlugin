// Standalone test for the certified dynamic-map telemetry (fase 5 of
// docs/tickets/TICKET-MAPS-NATIVE-PORT.md). Pins the fail-closed contract: a
// complete record emits a valid chronon.dynamic-map-telemetry.v1 summary,
// every incomplete or unbalanced record is refused with an explanation, and
// the renderer fingerprint is baked and non-empty.
#include "chrononplugin/maps/map_plate_telemetry.hpp"

#include <nlohmann/json.hpp>

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

using chrononplugin::maps::DynamicMapTelemetry;

DynamicMapTelemetry complete_record() {
    DynamicMapTelemetry t;
    t.basemap = "chronon_editorial";
    t.frames = 120;
    t.width = 1920;
    t.height = 1080;
    t.fps_num = 24;
    t.fps_den = 1;
    t.renderer_wall_s = 9.0;
    t.frame_pipeline_s = 3.0;
    t.render_encode_wall_s = 3.5;
    t.post_frame_tail_s = 0.5;
    t.gate_ms = 2.0;
    t.output_bytes = 1024;
    t.video_encoder = "libx264";
    t.gpu_encoder = false;
    t.gpu_map_sampler = "cpu";
    t.encoder_preset = "veryfast";
    t.encoder_quality = 23;
    t.tile.disk_cache_hits = 7;
    t.tile.disk_cache_misses = 7;
    t.tile.network_fetches = 0;
    t.tile.fallbacks = 0;
    t.tile.rendered_locally = 7;
    t.tile.prefetch_tiles_requested = 7;
    t.tile.prefetch_tiles_memory_hits = 0;
    t.plates.prepare_ms = 4.0;
    t.plates.compose_ms = 1.0;
    t.plates.plate_count = 11;
    t.plates.plate_bytes = 42;
    t.plates.required_tile_count = 7;
    t.plates.prefetch_tiles_fetched = 7;
    t.plates.prefetch_tiles_memory_hits = 0;
    return t;
}

}  // namespace

int main() {
    std::string error;
    std::string json;

    // A complete record validates and emits a parsable summary.
    const DynamicMapTelemetry ok = complete_record();
    CHECK(ok.validate(error));
    CHECK(ok.emit(json, error));
    CHECK(!json.empty());
    const nlohmann::json summary = nlohmann::json::parse(json);
    CHECK(summary.at("schema") == "chronon.dynamic-map-telemetry.v1");
    CHECK(summary.at("frames") == 120);
    CHECK(summary.at("dimensions").at("width") == 1920);
    CHECK(summary.at("tile").at("tile_rendered_locally") == 7);
    CHECK(summary.at("plates").at("required_tile_count") == 7);
    CHECK(summary.at("renderer_fingerprint") ==
          chrononplugin::maps::native_renderer_fingerprint());

    // The fingerprint is baked: non-empty and not the fallback marker.
    const std::string fingerprint = chrononplugin::maps::native_renderer_fingerprint();
    CHECK(!fingerprint.empty());
    CHECK(fingerprint != "maps-native-unfingerprinted");

    // Every incomplete record is refused, with an explanation, and emit()
    // never fabricates a summary for it.
    auto refused = [](DynamicMapTelemetry t) {
        std::string err, out;
        const bool valid = t.validate(err);
        const bool emitted = t.emit(out, err);
        CHECK(!valid);
        CHECK(!emitted);
        CHECK(out.empty());
    };

    auto few_frames = complete_record();
    few_frames.frames = 1;
    refused(few_frames);

    auto no_bytes = complete_record();
    no_bytes.output_bytes = 0;
    refused(no_bytes);

    auto no_basemap = complete_record();
    no_basemap.basemap.clear();
    refused(no_basemap);

    auto bad_dims = complete_record();
    bad_dims.height = -1;
    refused(bad_dims);

    auto bad_fps = complete_record();
    bad_fps.fps_den = 0;
    refused(bad_fps);

    auto unaccounted = complete_record();
    unaccounted.tile.disk_cache_hits = 0;
    unaccounted.tile.disk_cache_misses = 0;
    refused(unaccounted);

    auto no_tiles = complete_record();
    no_tiles.plates.required_tile_count = 0;
    no_tiles.tile.prefetch_tiles_requested = 0;
    refused(no_tiles);

    auto no_plates = complete_record();
    no_plates.plates.plate_count = 0;
    refused(no_plates);

    auto negative_ms = complete_record();
    negative_ms.plates.compose_ms = -0.5;
    refused(negative_ms);

    auto zero_pipeline = complete_record();
    zero_pipeline.frame_pipeline_s = 0.0;
    refused(zero_pipeline);

    auto zero_encode = complete_record();
    zero_encode.render_encode_wall_s = 0.0;
    refused(zero_encode);

    auto no_encoder = complete_record();
    no_encoder.video_encoder.clear();
    refused(no_encoder);

    // Tile accounting must balance.
    auto unbalanced = complete_record();
    unbalanced.tile.disk_cache_misses = 3;
    refused(unbalanced);

    // Locally rendered tiles are editorial-only.
    auto raster_local = complete_record();
    raster_local.basemap = "esri_light";
    refused(raster_local);

    // Prefetch coverage must close.
    auto prefetch_gap = complete_record();
    prefetch_gap.plates.prefetch_tiles_fetched = 2;
    refused(prefetch_gap);

    // Unapproved fallback work is never certified.
    auto fallback = complete_record();
    fallback.tile.late_tile_fetches = 1;
    refused(fallback);

    auto engine_fallback = complete_record();
    engine_fallback.engine_fallback_frames = 3;
    refused(engine_fallback);

    if (g_failures != 0) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("map_plate_telemetry: all checks passed\n");
    return 0;
}
