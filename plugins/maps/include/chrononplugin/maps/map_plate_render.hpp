#pragma once

// Chronon3D native map-plate rendering — SCAFFOLDING (TICKET-MAPS-NATIVE-PORT).
//
// The dynamic-map overlays of the overlay-throughput golden case are rendered
// today by an external Python process (ChrononTemplate/tools/map/
// render_dynamic_map_overlay.py, 797 lines) launched per item by the Go
// orchestrator. That process is the single most expensive item class in the
// render window (20,66 s and 37,37 s wall per map in the v3 golden run,
// TICKET-OVERLAY-THROUGHPUT-100-MIN) while phrase items cost 2–5 s. The port
// brings map rendering natively into the render pipeline: tile pyramid →
// textured planes → flyover camera → labels, on the same GPU lanes as every
// other item class.
//
// This header is the CONTRACT of that port, landed early so the call sites,
// the cache identity and the certified-telemetry rules have a stable shape to
// compile against. The implementation is a fail-closed stub: no frame and no
// telemetry is ever fabricated (godlike/07), every attempt reports
// kNotImplemented until the port phases in.

#include <cstdint>
#include <string>

namespace chrononplugin::maps {

/// Outcome of one native map-plate render attempt.
enum class MapPlateStatus : std::uint8_t {
    kOk = 0,
    /// The port has not landed yet — see docs/tickets/TICKET-MAPS-NATIVE-PORT.md.
    kNotImplemented,
    /// The request fails contract validation (non-positive output size,
    /// empty plate id, no pins): rejected, never guessed.
    kInvalidRequest,
};

/// One map-plate render request. `payload_json` is the same immutable
/// dynamic-map payload the Python renderer consumes today (pins, camera,
/// composition), so the native port can be certified against it frame by
/// frame before the orchestrator cuts over.
struct MapPlateRequest {
    std::string plate_id;      // registry plate id (e.g. "plate-new-orleans"), not a URL
    std::string payload_json;  // immutable dynamic-map payload (pins/camera/composition)
    int output_width = 0;
    int output_height = 0;
    double frame_rate = 0.0;
    std::int64_t frame_count = 0;
};

/// Result of one render attempt. `error` is empty if and only if
/// `status == kOk`; every non-OK result explains itself and carries no data.
struct MapPlateRenderResult {
    MapPlateStatus status = MapPlateStatus::kNotImplemented;
    std::string error;
};

/// Renders one map plate natively.
///
/// Fail-closed contract:
///  - a malformed request is rejected with kInvalidRequest;
///  - until the port lands, every accepted request returns kNotImplemented;
///  - the implementation never writes an output artifact unless it can also
///    produce the certified telemetry the cache identity requires
///    (chronon.dynamic-map-telemetry.v1).
[[nodiscard]] MapPlateRenderResult render_map_plate(const MapPlateRequest& request);

}  // namespace chrononplugin::maps
