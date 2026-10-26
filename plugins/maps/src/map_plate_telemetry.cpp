// Chronon3D native dynamic-map telemetry — TICKET-MAPS-NATIVE-PORT fase 5.
// validate() mirrors the Go validator invariants 1:1 (render_queue_map_once.go);
// emit() serializes the summary only for a validated record.

#include "chrononplugin/maps/map_plate_telemetry.hpp"

#include <nlohmann/json.hpp>

namespace chrononplugin::maps {

namespace {

bool positive_seconds(double value) { return value > 0.0; }
bool non_negative(double value) { return value >= 0.0; }

}  // namespace

bool DynamicMapTelemetry::validate(std::string& error) const {
    auto reject = [&error](const char* message) {
        error = message;
        return false;
    };

    if (frames < 2) return reject("telemetry frames must be >= 2");
    if (output_bytes <= 0) return reject("telemetry output_bytes must be > 0");
    if (basemap.empty()) return reject("telemetry basemap is required");
    if (width <= 0 || height <= 0) return reject("telemetry dimensions must be positive");
    if (fps_num <= 0 || fps_den <= 0) return reject("telemetry fps must be positive");
    if (tile.disk_cache_hits + tile.disk_cache_misses == 0) {
        return reject("telemetry must account for at least one tile");
    }
    if (plates.required_tile_count == 0) return reject("telemetry plates.required_tile_count is required");
    if (plates.plate_count == 0) return reject("telemetry plates.plate_count is required");
    if (!non_negative(plates.prepare_ms) || !non_negative(plates.compose_ms) ||
        !non_negative(gate_ms) || !non_negative(tile.fetch_ms) ||
        !non_negative(tile.prefetch_ms) || !non_negative(post_frame_tail_s) ||
        !non_negative(renderer_wall_s)) {
        return reject("telemetry phase timings must be non-negative");
    }
    if (!positive_seconds(frame_pipeline_s)) {
        return reject("telemetry frame_pipeline_s must be > 0");
    }
    if (!positive_seconds(render_encode_wall_s)) {
        return reject("telemetry render_encode_wall_s must be > 0");
    }
    if (video_encoder.empty() || gpu_map_sampler.empty() || encoder_preset.empty()) {
        return reject("telemetry encoder and sampler fields are required");
    }
    if (tile.rendered_locally < 0 || tile.network_fetches < 0 || tile.fallbacks < 0) {
        return reject("telemetry tile counters must be non-negative");
    }
    if (tile.rendered_locally > 0 && basemap != "chronon_editorial") {
        return reject("locally rendered tiles are only admitted on the editorial basemap");
    }
    if (tile.disk_cache_misses !=
        tile.network_fetches + tile.fallbacks + tile.rendered_locally) {
        return reject("telemetry tile cache/fetch counters do not balance");
    }
    if (plates.required_tile_count !=
            plates.prefetch_tiles_fetched + plates.prefetch_tiles_memory_hits ||
        tile.prefetch_tiles_requested != plates.required_tile_count ||
        tile.prefetch_tiles_memory_hits != plates.prefetch_tiles_memory_hits) {
        return reject("telemetry prefetch coverage mismatch");
    }
    if (engine_fallback_frames != 0 || tile.fallbacks != 0 || tile.late_tile_fetches != 0) {
        return reject("telemetry reports unapproved fallback work");
    }
    error.clear();
    return true;
}

bool DynamicMapTelemetry::emit(std::string& json, std::string& error) const {
    if (!validate(error)) {
        json.clear();
        return false;
    }
    nlohmann::json summary = {
        {"schema", "chronon.dynamic-map-telemetry.v1"},
        {"basemap", basemap},
        {"renderer_fingerprint", native_renderer_fingerprint()},
        {"frames", frames},
        {"dimensions", {{"width", width}, {"height", height}}},
        {"fps", {{"num", fps_num}, {"den", fps_den}}},
        {"renderer_wall_s", renderer_wall_s},
        {"frame_pipeline_s", frame_pipeline_s},
        {"render_encode_wall_s", render_encode_wall_s},
        {"post_frame_tail_s", post_frame_tail_s},
        {"engine_fallback_frames", engine_fallback_frames},
        {"output_bytes", output_bytes},
        {"video_encoder", video_encoder},
        {"gpu_encoder", gpu_encoder},
        {"gpu_map_sampler", gpu_map_sampler},
        {"encoder_preset", encoder_preset},
        {"encoder_quality", encoder_quality},
        {"gate_ms", gate_ms},
        {"tile",
         {{"tile_disk_cache_hits", tile.disk_cache_hits},
          {"tile_disk_cache_misses", tile.disk_cache_misses},
          {"tile_network_fetches", tile.network_fetches},
          {"tile_fallbacks", tile.fallbacks},
          {"tile_bytes_downloaded", tile.bytes_downloaded},
          {"tile_fetch_ms", tile.fetch_ms},
          {"prefetch_ms", tile.prefetch_ms},
          {"prefetch_tiles_requested", tile.prefetch_tiles_requested},
          {"prefetch_tiles_memory_hits", tile.prefetch_tiles_memory_hits},
          {"late_tile_fetches", tile.late_tile_fetches},
          {"tile_rendered_locally", tile.rendered_locally}}},
        {"plates",
         {{"prepare_ms", plates.prepare_ms},
          {"plate_compose_ms", plates.compose_ms},
          {"plate_count", plates.plate_count},
          {"plate_bytes", plates.plate_bytes},
          {"required_tile_count", plates.required_tile_count},
          {"prefetch_tiles_fetched", plates.prefetch_tiles_fetched},
          {"prefetch_tiles_memory_hits", plates.prefetch_tiles_memory_hits}}},
    };
    json = summary.dump();
    error.clear();
    return true;
}

#ifndef CHRONONPLUGIN_MAPS_RENDERER_FINGERPRINT
#define CHRONONPLUGIN_MAPS_RENDERER_FINGERPRINT "maps-native-unfingerprinted"
#endif

const char* native_renderer_fingerprint() {
    return CHRONONPLUGIN_MAPS_RENDERER_FINGERPRINT;
}

}  // namespace chrononplugin::maps
