#!/usr/bin/env python3
"""Bit-exact parity check for the native camera/motion and label primitives.

Compares the native `camera_label_parity_dump` output with the Python
reference closures named by the parity contracts in
plugins/maps/include/chrononplugin/maps/{map_camera,map_label_render}.hpp:

  * smootherstep / latlon_to_global_px -> geo_runtime / dynamic_tile_pyramid
  * roll_bank / geo_to_screen / shot motions -> render_geo_camera_small_places
  * labels -> geo_documentary.draw_camera_aligned_label (cv2.putText)

Exit codes: 0 parity PASS, 1 parity FAIL, 77 prerequisites missing (skip).
"""

import argparse
import json
import os
import struct
import subprocess
import sys
import tempfile
import types
from pathlib import Path

SKIP = 77

EASINGS = [0.0, 0.25, 0.5, 0.75, 1.0, -0.3, 1.7]
BANKS = [0.0, 0.25, 0.5, 0.75, 1.0, -0.2, 1.4]

POSES = [
    {"motion": "pure_dive", "anchor": [41.9028, 12.4964], "z0": 5.0, "z1": 9.5,
     "frames": 60, "frame": f}
    for f in (0, 1, 17, 42, 58, 59)
] + [
    {"motion": "dive_tilt", "anchor": [40.8518, 14.2681], "z0": 6.0, "z1": 11.0,
     "dive_end": 20, "tilt_end": 50, "tilt_deg": 12.5, "frames": 60, "frame": f}
    for f in (0, 20, 21, 35, 49, 50, 59)
]

SCREENS = [
    {"lat": 41.9028, "lon": 12.4964, "anchor": [41.9028, 12.4964],
     "zoom": 7.5, "width": 1280, "height": 720},
    {"lat": 45.4642, "lon": 9.1900, "anchor": [41.9028, 12.4964],
     "zoom": 6.25, "width": 1920, "height": 1080},
    {"lat": -33.8688, "lon": 151.2093, "anchor": [41.9028, 12.4964],
     "zoom": 4.0, "width": 854, "height": 480},
    {"lat": 89.0, "lon": 400.0, "anchor": [-45.0, -200.0],
     "zoom": 3.75, "width": 640, "height": 360},
]

LABELS = [
    {"x": 640.5, "y": 360.25, "text": "Roma", "color": [255, 255, 255],
     "width": 1280, "height": 720},
    {"x": -12.5, "y": 40.75, "text": "Napoli 1860", "color": [40, 80, 255],
     "width": 640, "height": 480},
    {"x": 100.0, "y": 200.0, "text": "", "color": [0, 0, 0],
     "width": 320, "height": 240},
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
    parser.add_argument("--dump-tool", required=True,
                        help="path to the camera_label_parity_dump binary")
    parser.add_argument("--python-reference",
                        default=str(workspace / "ChrononTemplate" / "tools" / "map"),
                        help="directory holding render_geo_camera_small_places.py")
    parser.add_argument("--cartography",
                        default=str(plugin_maps / "tools" / "cartography"),
                        help="directory holding dynamic_tile_pyramid.py")
    args = parser.parse_args()

    dump_tool = Path(args.dump_tool)
    if not dump_tool.is_file():
        print(f"SKIP: native dump tool not built: {dump_tool}")
        return SKIP
    reference_dir = Path(args.python_reference)
    if not (reference_dir / "render_geo_camera_small_places.py").is_file():
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
        import cv2
        import numpy as np
        import render_geo_camera_small_places as cam
        from dynamic_tile_pyramid import latlon_to_global_px
        import geo_runtime
    except Exception as exc:
        print(f"SKIP: cannot import the Python reference modules: {exc}")
        return SKIP

    spec = {
        "poses": POSES,
        "screens": SCREENS,
        "easings": EASINGS,
        "banks": BANKS,
        "labels": LABELS,
    }

    with tempfile.TemporaryDirectory(prefix="map_cam_parity_") as tmp:
        tmp = Path(tmp)
        spec_path = tmp / "spec.json"
        spec_path.write_text(json.dumps(spec), encoding="utf-8")
        result = subprocess.run([str(dump_tool), str(spec_path), str(tmp)],
                                capture_output=True, text=True)
        if result.returncode != 0:
            return fail(f"native dump tool failed ({result.returncode}): {result.stderr}")

        compared = 0

        for i, p in enumerate(EASINGS):
            raw = (tmp / f"ease_{i}.bin").read_bytes()
            (native,) = struct.unpack("<d", raw)
            ref = geo_runtime._smootherstep(p)
            if native != ref:
                return fail(f"smootherstep({p}): native {native!r} != ref {ref!r}")
            compared += 1
        print(f"smootherstep: {len(EASINGS)} samples identical")

        for i, p in enumerate(BANKS):
            raw = (tmp / f"bank_{i}.bin").read_bytes()
            (native,) = struct.unpack("<d", raw)
            ref = cam.roll_bank(p)
            if native != ref:
                return fail(f"roll_bank({p}): native {native!r} != ref {ref!r}")
            compared += 1
        print(f"roll_bank: {len(BANKS)} samples identical")

        for i, pose in enumerate(POSES):
            raw = (tmp / f"pose_{i}.bin").read_bytes()
            native = struct.unpack("<6d", raw)
            anchor = tuple(pose["anchor"])
            if pose["motion"] == "pure_dive":
                ref = cam.pure_dive_motion(anchor, pose["z0"], pose["z1"],
                                           pose["frames"])(pose["frame"])
            else:
                ref = cam.dive_tilt_motion(anchor, pose["z0"], pose["z1"],
                                           pose["dive_end"], pose["tilt_end"],
                                           pose["tilt_deg"],
                                           pose["frames"])(pose["frame"])
            if native != tuple(ref):
                return fail(f"pose {i} {pose['motion']} f={pose['frame']}: "
                            f"native {native} != ref {tuple(ref)}")
            compared += 1
        print(f"camera poses: {len(POSES)} identical")

        for i, screen in enumerate(SCREENS):
            raw = (tmp / f"screen_{i}.bin").read_bytes()
            native = struct.unpack("<2i", raw)
            ref = cam.geo_to_screen(screen["lat"], screen["lon"],
                                    tuple(screen["anchor"]), screen["zoom"],
                                    screen["width"], screen["height"])
            if native != tuple(ref):
                return fail(f"geo_to_screen {i}: native {native} != ref {tuple(ref)}")
            compared += 1
        print(f"geo_to_screen: {len(SCREENS)} identical")

        for i, label in enumerate(LABELS):
            raw = np.fromfile(tmp / f"label_{i}.bin", dtype=np.uint8)
            if raw.size != label["height"] * label["width"] * 3:
                return fail(f"label {i}: bad blob size {raw.size}")
            native = raw.reshape(label["height"], label["width"], 3)
            ref = np.zeros((label["height"], label["width"], 3), dtype=np.uint8)
            cv2.putText(ref, label["text"],
                        (round(label["x"]), round(label["y"])),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.6,
                        tuple(label["color"]), 2, cv2.LINE_AA)
            diff = int(np.count_nonzero(ref.astype(np.int16) - native.astype(np.int16)))
            if diff:
                return fail(f"label {i} {label['text']!r}: {diff} differing channel values")
            compared += 1
        print(f"labels: {len(LABELS)} identical")

    print(f"PARITY PASS: {compared} sample(s) identical to the Python reference")
    return 0


if __name__ == "__main__":
    sys.exit(main())
