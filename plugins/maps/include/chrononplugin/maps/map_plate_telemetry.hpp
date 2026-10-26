#pragma once

// Chronon3D native dynamic-map telemetry — TICKET-MAPS-NATIVE-PORT fase 5
// («telemetria certificata»).
//
// The overlay cache in refactored refuses every entry without a complete
// chronon.dynamic-map-telemetry.v1 summary (fail-closed). This module builds
// that summary natively and validates it against exactly the invariants the
// Go validator enforces (refactored/internal/capabilities/scripts/
// render_queue_map_once.go), so a native render can never certify a frame it
// did not account for:
//
//  * schema/frames/output_bytes invariants (frames >= 2, output_bytes > 0);
//  * positive dimensions and frame rate;
//  * every tile accounted: disk misses == network fetches + fallbacks +
//    locally rendered, and locally rendered only on the editorial basemap;
//  * prefetch coverage: required tiles == fetched + memory hits, and the
//    plate counters agree with the tile counters;
//  * no unapproved fallback work (engine fallback frames, tile fallbacks and
//    late fetches must all be zero);
//  * non-negative phase timings and positive frame/encode pipelines.
//
// emit() produces the summary JSON only for a telemetry record that passes
// validate(): nothing is fabricated, ever (godlike/07).

#include <cstdint>
#include <string>

namespace chrononplugin::maps {

/// Tile acquisition counters (chronon.dynamic-map-telemetry.v1 `tile` block).
struct TileTelemetry {
    std::int64_t disk_cache_hits = 0;
    std::int64_t disk_cache_misses = 0;
    std::int64_t network_fetches = 0;
    std::int64_t fallbacks = 0;
    std::int64_t bytes_downloaded = 0;
    double fetch_ms = 0.0;
    double prefetch_ms = 0.0;
    std::int64_t prefetch_tiles_requested = 0;
    std::int64_t prefetch_tiles_memory_hits = 0;
    std::int64_t late_tile_fetches = 0;
    std::int64_t rendered_locally = 0;
};

/// Plate composition counters (`plates` block).
struct PlateTelemetry {
    double prepare_ms = 0.0;
    double compose_ms = 0.0;
    std::int64_t plate_count = 0;
    std::int64_t plate_bytes = 0;
    std::int64_t required_tile_count = 0;
    std::int64_t prefetch_tiles_fetched = 0;
    std::int64_t prefetch_tiles_memory_hits = 0;
};

/// One certified dynamic-map telemetry record.
struct DynamicMapTelemetry {
    std::string basemap;      // e.g. "chronon_editorial"; required
    std::int64_t frames = 0;
    int width = 0;
    int height = 0;
    int fps_num = 0;
    int fps_den = 0;
    double renderer_wall_s = 0.0;
    double frame_pipeline_s = 0.0;
    double render_encode_wall_s = 0.0;
    double post_frame_tail_s = 0.0;
    double gate_ms = 0.0;
    std::int64_t engine_fallback_frames = 0;
    std::int64_t output_bytes = 0;
    std::string video_encoder;
    bool gpu_encoder = false;
    std::string gpu_map_sampler;
    std::string encoder_preset;
    int encoder_quality = 0;
    TileTelemetry tile;
    PlateTelemetry plates;

    /// Mirrors the Go validator's invariants. On failure `error` explains
    /// which invariant is violated.
    [[nodiscard]] bool validate(std::string& error) const;

    /// Serializes the chronon.dynamic-map-telemetry.v1 summary. Refuses
    /// (false + error) unless the record passes validate().
    [[nodiscard]] bool emit(std::string& json, std::string& error) const;
};

/// The certified fingerprint of this native maps renderer, baked from the
/// renderer sources at build time. The cache identity must change whenever
/// the renderer code changes (TICKET-MAPS-NATIVE-PORT section 4.2).
[[nodiscard]] const char* native_renderer_fingerprint();

}  // namespace chrononplugin::maps
