// Certified telemetry and output installation of the native map renderer.
// Split out of map_plate_render_pipeline.cpp: the driver measures, this unit
// turns the measurements into chronon.dynamic-map-telemetry.v1 and installs
// video + summary together.

#include "map_plate_render_pipeline_internal.hpp"

#include <cstdio>
#include <string>

namespace chrononplugin::maps::map_plate_detail {

DynamicMapTelemetry build_map_plate_telemetry(const TelemetryInputs& inputs) {
    // Distinguish downloaded/cached imagery tiles from locally rasterized
    // editorial tiles.
    DynamicMapTelemetry telemetry;
    telemetry.basemap = inputs.satellite
        ? (inputs.rendered_basemap == "esri_topo" ? "esri_topo" : "esri_sat")
        : "chronon_editorial";
    telemetry.frames = inputs.frames;
    telemetry.width = inputs.width;
    telemetry.height = inputs.height;
    telemetry.fps_num = inputs.fps_num;
    telemetry.fps_den = inputs.fps_den;
    telemetry.renderer_wall_s = inputs.total_wall_s;
    telemetry.frame_pipeline_s =
        inputs.frame_wall_s > 0.0 ? inputs.frame_wall_s : inputs.pipeline_wall_s;
    telemetry.render_encode_wall_s = inputs.pipeline_wall_s;
    telemetry.post_frame_tail_s = 0.0;
    telemetry.gate_ms = 0.0;
    telemetry.output_bytes = inputs.output_bytes;
    telemetry.video_encoder = inputs.profile.codec;
    telemetry.gpu_encoder = inputs.profile.gpu;
    telemetry.gpu_map_sampler = inputs.sampler;
    telemetry.encoder_preset = inputs.profile.preset;
    telemetry.encoder_quality = inputs.profile.quality;
    // Editorial plates reuse tiles in memory across frames; the wire field is
    // named "disk_cache_hits" for the satellite pyramid, so the local raster
    // cache reports its hits here as well. Without this the certified log says
    // "cache_hit=0" while tens of thousands of tile placements were served
    // from the cache and every one of them was cached.
    const EsriSatellitePyramid* satellite = inputs.satellite;
    const VectorPlateRaster* raster = inputs.raster;
    if (inputs.self_contained_map) {
        // The treatment renders exactly one screen-space plate locally; no map
        // tile is fetched or sampled. Account for that plate instead of
        // inventing tile work: the certified counters stay balanced and the
        // editorial basemap is the one that admits locally rendered work.
        telemetry.tile.disk_cache_hits = 0;
        telemetry.tile.disk_cache_misses = 1;
        telemetry.tile.network_fetches = 0;
        telemetry.tile.bytes_downloaded = 0;
        telemetry.tile.fallbacks = 0;
        telemetry.tile.rendered_locally = 1;
        telemetry.tile.fetch_ms = 0.0;
        telemetry.tile.prefetch_ms = 0.0;
        telemetry.tile.prefetch_tiles_requested = 1;
        telemetry.tile.prefetch_tiles_memory_hits = 0;
        telemetry.plates.prepare_ms = inputs.prepare_ms;
        telemetry.plates.compose_ms = inputs.effect_wall_s * 1e3;
        telemetry.plates.plate_count = 1;
        telemetry.plates.plate_bytes =
            static_cast<std::int64_t>(inputs.frame_bytes);
        telemetry.plates.required_tile_count = 1;
        telemetry.plates.prefetch_tiles_fetched = 1;
        telemetry.plates.prefetch_tiles_memory_hits = 0;
    } else {
        telemetry.tile.disk_cache_hits = satellite ? satellite->disk_hits() : raster->tile_cache_hits();
        telemetry.tile.persistent_cache_hits = satellite ? 0 : raster->tile_disk_hits();
        telemetry.tile.disk_cache_misses = satellite ? satellite->network_fetches() : raster->tile_cache_misses();
        telemetry.tile.network_fetches = satellite ? satellite->network_fetches() : 0;
        telemetry.tile.bytes_downloaded = satellite ? satellite->bytes_downloaded() : 0;
        telemetry.tile.fallbacks = 0;
        telemetry.tile.rendered_locally = satellite ? 0 : raster->tile_cache_misses();
        telemetry.tile.fetch_ms = satellite ? satellite->prefetch_ms() : inputs.compose_wall_s * 1e3;
        telemetry.tile.prefetch_ms = satellite ? satellite->prefetch_ms() : 0.0;
        const std::int64_t tile_count = satellite
            ? satellite->disk_hits() + satellite->network_fetches()
            : raster->tile_cache_hits() + raster->tile_cache_misses();
        const std::int64_t tile_cache_hits = satellite ? satellite->disk_hits() : raster->tile_cache_hits();
        const std::int64_t tile_fetched = satellite ? satellite->network_fetches() : raster->tile_cache_misses();
        telemetry.tile.prefetch_tiles_requested = tile_count;
        telemetry.tile.prefetch_tiles_memory_hits = tile_cache_hits;
        telemetry.plates.prepare_ms = inputs.prepare_ms;
        telemetry.plates.compose_ms = inputs.compose_wall_s * 1e3;
        telemetry.plates.plate_count = satellite ? satellite->plate_count() : inputs.stats.plate_composes;
        telemetry.plates.plate_bytes = satellite ? satellite->plate_bytes() : inputs.stats.plate_bytes;
        telemetry.plates.required_tile_count = tile_count;
        telemetry.plates.prefetch_tiles_fetched = tile_fetched;
        telemetry.plates.prefetch_tiles_memory_hits = tile_cache_hits;
    }
    return telemetry;
}

const char* install_plate_outputs(const MapPlateRequest& request,
                                  const std::string& summary_json,
                                  const std::string& staged_video,
                                  const std::string& staged_summary) {
    // Install the outputs together, only now that both exist.
    std::FILE* summary_file = std::fopen(staged_summary.c_str(), "wb");
    if (summary_file == nullptr ||
        std::fwrite(summary_json.data(), 1, summary_json.size(), summary_file) !=
            summary_json.size()) {
        if (summary_file != nullptr) std::fclose(summary_file);
        std::remove(staged_video.c_str());
        std::remove(staged_summary.c_str());
        return "cannot write the map plate telemetry summary";
    }
    std::fclose(summary_file);
    if (std::rename(staged_video.c_str(), request.output_video.c_str()) != 0 ||
        std::rename(staged_summary.c_str(), request.summary_output.c_str()) != 0) {
        std::remove(staged_video.c_str());
        std::remove(staged_summary.c_str());
        std::remove(request.output_video.c_str());
        return "cannot install the map plate outputs";
    }
    return nullptr;
}

}  // namespace chrononplugin::maps::map_plate_detail
