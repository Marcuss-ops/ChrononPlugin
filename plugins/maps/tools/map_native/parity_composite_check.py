#!/usr/bin/env python3
"""Bit-exact composite parity check for the native map-plate renderer.

Runs the native `maps_dynamic_map_render` on a corpus of flyover programs and
compares every raw frame byte-for-byte with a Python reference composed from
the product's own functions:

  * camera zoom   -> geo_runtime._ShotBuilder.pose ("dive" preset)
  * plate sampling -> dynamic_tile_pyramid.DynamicTilePyramid.render_plate_at_zoom
                      + .sample_continuous (fed with offline vector tiles)
  * labels        -> render_geo_camera_small_places.geo_to_screen +
                      geo_documentary.draw_camera_aligned_label

The vector tiles are always rendered fresh: the Python pyramid's disk cache
is a JPEG-95 round trip (vector_plate_pyramid.TILE_SUFFIX = ".jpg"), so
cache-hit tiles are lossy and cache-state-dependent. The deterministic
reference for the native port is the freshly rendered vector tile; the cache
is bypassed here on purpose.

Exit codes: 0 parity PASS, 1 parity FAIL, 77 prerequisites missing (skip).
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile
import types
from pathlib import Path

SKIP = 77

# Each program renders a flyover whose frames must match the reference
# exactly. Fractional zooms exercise the z_lo/z_hi cross-fade; 7.0 exercises
# the alpha=0 single-plate path.
PROGRAMS = [
    {
        "name": "rome-dive",
        "anchor": [41.9028, 12.4964],
        "pins": [{"id": "roma", "label": "Roma", "latitude": 41.9028, "longitude": 12.4964}],
        "width": 160, "height": 90, "fps_num": 30, "fps_den": 1, "duration_us": 133333,
        "camera_start_zoom": 5.2, "camera_end_zoom": 17.8,
    },
    {
        "name": "naples-hold",
        "anchor": [40.8518, 14.2681],
        "pins": [{"id": "napoli", "label": "Napoli", "latitude": 40.8518, "longitude": 14.2681}],
        "width": 96, "height": 96, "fps_num": 30, "fps_den": 1, "duration_us": 66667,
        "camera_start_zoom": 7.0, "camera_end_zoom": 7.0,
    },
    {
        "name": "two-pin-fade",
        "anchor": [43.0, 12.0],
        "pins": [
            {"id": "firenze", "label": "Firenze", "latitude": 43.7696, "longitude": 11.2558},
            {"id": "siena", "label": "Siena", "latitude": 43.3188, "longitude": 11.3308},
        ],
        "width": 120, "height": 68, "fps_num": 25, "fps_den": 1, "duration_us": 120000,
        "camera_start_zoom": 6.35, "camera_end_zoom": 8.75,
    },
]


def fail(message):
    print(f"FAIL: {message}")
    return 1


def ensure_reference_interpreter():
    """Re-exec under an interpreter that can import cv2 and numpy."""
    def usable(exe):
        try:
            probe = subprocess.run([exe, "-c", "import cv2, numpy"],
                                   capture_output=True, timeout=120)
            return probe.returncode == 0
        except (OSError, subprocess.SubprocessError):
            return False

    if usable(sys.executable):
        return None
    for candidate in ("/usr/bin/python3", "python3"):
        if candidate != sys.executable and usable(candidate):
            return candidate
    return ""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    script_dir = Path(__file__).resolve().parent
    plugin_maps = script_dir.parent.parent  # plugins/maps
    workspace = plugin_maps.parent.parent.parent  # ChrononPlugin/..
    parser.add_argument("--render-tool", required=True,
                        help="path to the maps_dynamic_map_render binary")
    parser.add_argument("--python-reference",
                        default=str(workspace / "ChrononTemplate" / "tools" / "map"),
                        help="directory holding the product map modules")
    parser.add_argument("--cartography",
                        default=str(plugin_maps / "tools" / "cartography"),
                        help="directory holding dynamic_tile_pyramid.py")
    parser.add_argument("--catalog",
                        default=str(workspace / "ChrononTemplate" / "catalog"),
                        help="directory holding the boundary GeoJSON sources")
    args = parser.parse_args()

    render_tool = Path(args.render_tool)
    if not render_tool.is_file():
        print(f"SKIP: native render tool not built: {render_tool}")
        return SKIP
    reference_dir = Path(args.python_reference)
    if not (reference_dir / "geo_runtime.py").is_file():
        print(f"SKIP: Python reference not found under {reference_dir}")
        return SKIP

    interpreter = ensure_reference_interpreter()
    if interpreter == "":
        print("SKIP: no Python interpreter with cv2 and numpy available")
        return SKIP
    if interpreter is not None:
        os.execv(interpreter, [interpreter] + sys.argv)

    sys.path.insert(0, str(reference_dir))
    sys.path.insert(0, args.cartography)
    try:
        import numpy as np
        import dynamic_tile_pyramid as dtp
        import vector_plate_pyramid as vpp
        import geo_runtime
        import render_geo_camera_small_places as cam
        from geo_documentary import draw_camera_aligned_label
        from geo_places import Place
    except Exception as exc:
        print(f"SKIP: cannot import the Python reference modules: {exc}")
        return SKIP

    compared = 0
    with tempfile.TemporaryDirectory(prefix="map_composite_parity_") as tmp:
        tmp = Path(tmp)
        for program in PROGRAMS:
            name = program["name"]
            payload = {
                "width": program["width"], "height": program["height"],
                "fps_num": program["fps_num"], "fps_den": program["fps_den"],
                "duration_us": program["duration_us"],
                "pins": program["pins"],
                "camera_anchor": program["anchor"],
                "camera_start_zoom": program["camera_start_zoom"],
                "camera_end_zoom": program["camera_end_zoom"],
                "encoder_profile": {"codec": "libx264", "gpu": False,
                                    "preset": "veryfast", "quality": 23},
                "sampler": "cpu",
            }
            out_dir = tmp / name
            out_dir.mkdir()
            payload_path = out_dir / "payload.json"
            payload_path.write_text(json.dumps(payload), encoding="utf-8")
            result = subprocess.run(
                [str(render_tool), "--input", str(payload_path),
                 "--output", str(out_dir / "out.mp4"),
                 "--summary-output", str(out_dir / "telemetry.json"),
                 "--raw-frames-dir", str(out_dir)],
                capture_output=True, text=True,
                env={**os.environ, "CHRONON_MAPS_CATALOG_DIR": args.catalog})
            if result.returncode != 0:
                return fail(f"{name}: native render failed: {result.stderr}")

            # Python reference frame loop, product functions only.
            pyr = vpp.VectorPlatePyramid()
            pyr.render_plate_at_zoom = dtp.DynamicTilePyramid.render_plate_at_zoom.__get__(pyr)
            pyr.sample_continuous = dtp.DynamicTilePyramid.sample_continuous.__get__(pyr)

            def fresh_tile(self, z, x, y):
                # Deterministic reference: never read the lossy disk cache.
                return (z, x, y), vpp.render_vector_tile(z, x, y)

            pyr._fetch_single_tile = fresh_tile.__get__(pyr)

            frames = round(program["duration_us"] * program["fps_num"] /
                           (1e6 * program["fps_den"]))
            place = Place(query=name, display_name=name,
                          lat=program["anchor"][0], lon=program["anchor"][1])
            builder = geo_runtime._ShotBuilder(place, "dive", frames)
            builder.start_zoom = program["camera_start_zoom"]
            builder.end_zoom = program["camera_end_zoom"]

            def project(lat, lon, pose):
                return cam.geo_to_screen(lat, lon, tuple(program["anchor"]),
                                         pose[0], program["width"], program["height"])

            for f in range(frames):
                zoom = builder.pose(f)[0]
                frame = pyr.sample_continuous(program["anchor"][0], program["anchor"][1],
                                              zoom, program["width"], program["height"])
                pose = (zoom, 0.0, 0.0, 0.0)
                for pin in program["pins"]:
                    draw_camera_aligned_label(frame, (pin["latitude"], pin["longitude"]),
                                              pin["label"], pose, project, 0.0)
                raw_path = out_dir / f"frame_{f:06d}.raw"
                if not raw_path.is_file():
                    return fail(f"{name}: native frame {f} missing")
                native = np.fromfile(raw_path, dtype=np.uint8).reshape(
                    program["height"], program["width"], 3)
                if not np.array_equal(frame, native):
                    diff = int(np.count_nonzero(
                        frame.astype(np.int16) - native.astype(np.int16)))
                    return fail(f"{name}: frame {f} differs ({diff} channels)")
                compared += 1
            print(f"{name}: {frames} frame(s) identical")

    print(f"PARITY PASS: {compared} composite frame(s) identical to the Python reference")
    return 0


if __name__ == "__main__":
    sys.exit(main())
