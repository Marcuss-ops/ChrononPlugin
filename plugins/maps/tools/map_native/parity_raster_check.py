#!/usr/bin/env python3
"""Bit-exact parity check for the native vector tile/plate rasterizer.

Compares the native `raster_parity_dump` output with the Python reference
renderer named by the parity contract in
plugins/maps/include/chrononplugin/maps/map_plate_raster.hpp:

  * tiles   -> ChrononTemplate/tools/map/vector_plate_pyramid.py::render_vector_tile
  * plates  -> tools/cartography/fast_plate_sampler.py::FastPlateSampler._compose_px
               (tile slicing, fed with the same render_vector_tile tiles)

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

# Tile corpus: a normal tile, a wrapped x, an out-of-pyramid y (blank sea),
# a dense high-zoom tile, and a whole-world low-zoom tile.
TILES = [
    (5, 17, 11),
    (3, -1, 2),
    (3, 2, -1),
    (8, 137, 90),
    (4, 0, 0),
]

# Plate corpus: fractional pixel offsets exercise the half-even rounding of
# the stitch bounds, negative origins exercise the wrap, and each window is
# split across several tiles.
PLATES = [
    {"left": 17 * 256 + 10.5, "top": 11 * 256 + 3.25, "width": 300, "height": 200, "zoom": 5},
    {"left": -130.5, "top": -64.75, "width": 256, "height": 256, "zoom": 3},
    {"left": 137 * 256 - 7.5, "top": 90 * 256 + 0.5, "width": 128, "height": 64, "zoom": 8},
]


def fail(message):
    print(f"FAIL: {message}")
    return 1


def ensure_reference_interpreter():
    """Re-exec under an interpreter that can import cv2 and numpy.

    The reference renderer (vector_plate_pyramid) needs cv2; the interpreter
    CMake picks for the test may not have it. Probe known interpreters and
    re-exec under the first usable one; skip (77) when none exists.
    """
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
    parser.add_argument("--dump-tool", required=True,
                        help="path to the maps_raster_parity_dump binary")
    parser.add_argument("--python-reference",
                        default=str(workspace / "ChrononTemplate" / "tools" / "map"),
                        help="directory holding vector_plate_pyramid.py")
    parser.add_argument("--cartography",
                        default=str(plugin_maps / "tools" / "cartography"),
                        help="directory holding fast_plate_sampler.py")
    parser.add_argument("--catalog",
                        default=str(workspace / "ChrononTemplate" / "catalog"),
                        help="directory holding the boundary GeoJSON sources")
    args = parser.parse_args()

    dump_tool = Path(args.dump_tool)
    if not dump_tool.is_file():
        print(f"SKIP: native dump tool not built: {dump_tool}")
        return SKIP
    reference_dir = Path(args.python_reference)
    if not (reference_dir / "vector_plate_pyramid.py").is_file():
        print(f"SKIP: Python reference not found under {reference_dir}")
        return SKIP
    countries = Path(args.catalog) / "ne_50m_admin_0_countries.geojson"
    admin2 = Path(args.catalog) / "italy_admin" / "geoBoundaries_ITA_ADM2.simplified.geojson"
    if not countries.is_file() or not admin2.is_file():
        print(f"SKIP: boundary sources not found under {args.catalog}")
        return SKIP

    interpreter = ensure_reference_interpreter()
    if interpreter == "":
        print("SKIP: no Python interpreter with cv2 and numpy available")
        return SKIP
    if interpreter is not None:
        os.execv(interpreter, [interpreter] + sys.argv)

    sys.path.insert(0, str(reference_dir))
    import vector_plate_pyramid as vpp  # noqa: E402
    import numpy as np  # noqa: E402

    spec = {
        "countries_geojson": str(countries),
        "italy_admin2_geojson": str(admin2),
        "tiles": [list(t) for t in TILES],
        "plates": PLATES,
    }

    with tempfile.TemporaryDirectory(prefix="map_parity_") as tmp:
        tmp = Path(tmp)
        spec_path = tmp / "spec.json"
        spec_path.write_text(json.dumps(spec), encoding="utf-8")
        result = subprocess.run([str(dump_tool), str(spec_path), str(tmp)],
                                capture_output=True, text=True)
        if result.returncode != 0:
            return fail(f"native dump tool failed ({result.returncode}): {result.stderr}")

        # Plate reference: FastPlateSampler._compose_px, fed with the very
        # same render_vector_tile tiles via an on-demand tile cache.
        sys.path.insert(0, args.cartography)
        try:
            from fast_plate_sampler import FastPlateSampler
        except Exception as exc:  # pragma: no cover - optional reference
            return fail(f"cannot import fast_plate_sampler: {exc}")

        class OnDemandTiles(dict):
            def get(self, key, default=None):
                value = super().get(key, default)
                if value is default:
                    z, x, y = key
                    value = vpp.render_vector_tile(z, x, y)
                    self[key] = value
                return value

        sampler = types.SimpleNamespace(
            pyr=types.SimpleNamespace(
                tile_cache=OnDemandTiles(),
                record_telemetry=lambda *a, **k: None,
            ))

        compared = 0
        for i, (z, x, y) in enumerate(TILES):
            ref = vpp.render_vector_tile(z, x, y)
            raw = np.fromfile(tmp / f"tile_{i}.bin", dtype=np.uint8)
            if raw.size != 256 * 256 * 3:
                return fail(f"tile {z},{x},{y}: bad blob size {raw.size}")
            native = raw.reshape(256, 256, 3)
            diff = int(np.count_nonzero(ref.astype(np.int16) - native.astype(np.int16)))
            if diff:
                return fail(f"tile {z},{x},{y}: {diff} differing channel values")
            print(f"tile {z},{x},{y}: identical")
            compared += 1

        for i, plate in enumerate(PLATES):
            ref = FastPlateSampler._compose_px(
                sampler, plate["left"], plate["top"],
                plate["width"], plate["height"], plate["zoom"])
            raw = np.fromfile(tmp / f"plate_{i}.bin", dtype=np.uint8)
            if raw.size != plate["height"] * plate["width"] * 3:
                return fail(f"plate {i}: bad blob size {raw.size}")
            native = raw.reshape(plate["height"], plate["width"], 3)
            diff = int(np.count_nonzero(ref.astype(np.int16) - native.astype(np.int16)))
            if diff:
                return fail(f"plate {i} {plate}: {diff} differing channel values")
            print(f"plate {i} {plate['width']}x{plate['height']}@{plate['zoom']}: identical")
            compared += 1

    print(f"PARITY PASS: {compared} raster(s) identical to the Python reference")
    return 0


if __name__ == "__main__":
    sys.exit(main())
