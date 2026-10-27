#pragma once

// Chronon3D native map-plate rendering — TICKET-MAPS-NATIVE-PORT.
//
// The dynamic-map overlays of the overlay-throughput golden case were
// rendered by an external Python process launched per item by the Go
// orchestrator. This is the native port of that renderer: tile pyramid →
// plate sampling → flyover camera → labels, plus the certified
// chronon.dynamic-map-telemetry.v1 summary the cache identity requires.
//
// NATIVE SUBSET (anything else is refused, never guessed):
//  * the closed encoder profiles libx264/veryfast/23 and h264_nvenc/p6/15
//    (NVENC still requires the OpenCL sampler, so today: libx264/cpu);
//  * sampler "cpu" (the native vector plates are CPU-sampled);
//  * basemap `chronon_editorial` (offline vector) or `esri_sat` (runtime Esri
//    World Imagery tiles, OpenCV continuous pyramid sampler);
//  * base flyover or the native `route_line` composition;
//  * camera_animation "slow_approach" (the product "dive" shot) over one
//    anchor: an explicit `camera_anchor` or the single unique pin;
//  * label_animation "none"; pin labels are drawn as camera-aligned labels;
//  * no per-pin spoken schedules, route_timing or country_context tours;
//  * at most five unique pins (the product route budget, fail-closed above).
//
// Planning provenance fields (map_group, composition_family, typography, ...)
// are accepted and carry no pixel semantics.
//
// Fail-closed contract:
//  - a malformed request is rejected with kInvalidRequest;
//  - a well-formed request outside the native subset returns kNotImplemented
//    (the consumer keeps its own fallback policy);
//  - the implementation never installs an output artifact unless it can also
//    produce the complete certified telemetry for it (godlike/07): outputs
//    are staged and only renamed into place together with the summary.

#include <cstdint>
#include <string>

namespace chrononplugin::maps {

/// Outcome of one native map-plate render attempt.
enum class MapPlateStatus : std::uint8_t {
    kOk = 0,
    /// The requested composition is outside the native subset — see above.
    kNotImplemented,
    /// The request fails contract validation (non-positive output size,
    /// empty plate id, no pins, inconsistent media contract): rejected,
    /// never guessed.
    kInvalidRequest,
    /// The render or its telemetry failed internally; no artifact installed.
    kRenderFailed,
};

/// One map-plate render request. `payload_json` is the immutable dynamic-map
/// payload (width, height, fps_num, fps_den, duration_us, pins, encoder
/// profile, sampler, camera/label animation), so the native port can be
/// certified against the same inputs the Python renderer consumed.
struct MapPlateRequest {
    std::string plate_id;       // registry plate id (e.g. "plate-new-orleans"), not a URL
    std::string payload_json;   // immutable dynamic-map payload
    int output_width = 0;
    int output_height = 0;
    double frame_rate = 0.0;
    std::int64_t frame_count = 0;
    std::string output_video;    // staged then renamed into place with the summary
    std::string summary_output;  // chronon.dynamic-map-telemetry.v1 JSON
    std::string raw_frames_dir;  // optional: also dump frame_%06d.raw (parity gates)
};

/// Result of one render attempt. `error` is empty if and only if
/// `status == kOk`; every non-OK result explains itself and carries no data.
struct MapPlateRenderResult {
    MapPlateStatus status = MapPlateStatus::kNotImplemented;
    std::string error;
};

/// Renders one map plate natively.
[[nodiscard]] MapPlateRenderResult render_map_plate(const MapPlateRequest& request);

}  // namespace chrononplugin::maps
