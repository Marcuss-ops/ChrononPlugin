#!/usr/bin/env python3
"""Build the small editorial US-states GeoJSON used by the native map gallery.

The gallery renders at 1920x1080, so the Natural Earth 50m admin-1 source is
simplified to a sub-pixel tolerance and stripped to the two properties the
renderer reads (`name`, `postal`). Alaska and Hawaii are excluded: every
editorial style frames the contiguous United States. Raw source data is never
committed; the generated catalog is.

Usage:
  python3 build_editorial_map_assets.py \
      --source .tmp/mapassets/ne_50m_admin_1_states_provinces.geojson \
      --output plugins/maps/assets/us_states_editorial.geojson
"""
from __future__ import annotations

import argparse
import json
import math
import urllib.request
from pathlib import Path

SOURCE_URL = (
    "https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/"
    "geojson/ne_50m_admin_1_states_provinces.geojson"
)
EXCLUDED_POSTAL = {"AK", "HI"}
TOLERANCE_DEGREES = 0.004


def perpendicular_distance(point, start, end):
    if start == end:
        return math.hypot(point[0] - start[0], point[1] - start[1])
    dx = end[0] - start[0]
    dy = end[1] - start[1]
    denominator = math.hypot(dx, dy)
    return abs(dy * point[0] - dx * point[1] + end[0] * start[1] - end[1] * start[0]) / denominator


def simplify(points, tolerance):
    """Douglas-Peucker on a closed ring; the caller re-closes the result."""
    if len(points) <= 4:
        return points
    first, last = points[0], points[-1]
    index, distance = 0, 0.0
    for i in range(1, len(points) - 1):
        current = perpendicular_distance(points[i], first, last)
        if current > distance:
            index, distance = i, current
    if distance <= tolerance:
        return [first, last]
    left = simplify(points[: index + 1], tolerance)
    right = simplify(points[index:], tolerance)
    return left[:-1] + right


def simplify_ring(ring, tolerance):
    points = [(round(float(lon), 4), round(float(lat), 4)) for lon, lat, *_ in ring]
    if points and points[0] != points[-1]:
        points.append(points[0])
    simplified = simplify(points, tolerance)
    if len(simplified) < 4:
        return None
    if simplified[0] != simplified[-1]:
        simplified.append(simplified[0])
    return [[lon, lat] for lon, lat in simplified]


def simplify_geometry(geometry, tolerance):
    if geometry["type"] == "Polygon":
        polygons = [geometry["coordinates"]]
        multi = False
    elif geometry["type"] == "MultiPolygon":
        polygons = geometry["coordinates"]
        multi = True
    else:
        return None
    cleaned = []
    for polygon in polygons:
        rings = []
        for ring in polygon:
            simplified = simplify_ring(ring, tolerance)
            if simplified:
                rings.append(simplified)
        if rings:
            cleaned.append(rings)
    if not cleaned:
        return None
    return {
        "type": "MultiPolygon" if multi or len(cleaned) > 1 else "Polygon",
        "coordinates": cleaned if (multi or len(cleaned) > 1) else cleaned[0],
    }


def load_source(source: Path) -> dict:
    if source.is_file():
        return json.loads(source.read_text(encoding="utf-8"))
    if str(source).startswith(("http://", "https://")):
        with urllib.request.urlopen(str(source), timeout=90) as response:
            return json.loads(response.read().decode("utf-8"))
    raise SystemExit(f"source GeoJSON not found: {source}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path(SOURCE_URL))
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(__file__).resolve().parents[2] / "assets" / "us_states_editorial.geojson",
    )
    parser.add_argument("--tolerance", type=float, default=TOLERANCE_DEGREES)
    args = parser.parse_args()

    document = load_source(args.source)
    features = []
    input_points = 0
    output_points = 0
    for feature in document.get("features", []):
        properties = feature.get("properties", {})
        if properties.get("adm0_a3") != "USA":
            continue
        postal = properties.get("postal") or ""
        if postal in EXCLUDED_POSTAL:
            continue
        geometry = simplify_geometry(feature.get("geometry", {}), args.tolerance)
        if geometry is None:
            continue
        for polygon in geometry["coordinates"] if geometry["type"] == "MultiPolygon" else [geometry["coordinates"]]:
            for ring in polygon:
                output_points += len(ring)
        input_rings = feature["geometry"]["coordinates"]
        if feature["geometry"]["type"] == "Polygon":
            input_rings = [input_rings]
        for polygon in input_rings:
            for ring in polygon:
                input_points += len(ring)
        features.append(
            {
                "type": "Feature",
                "properties": {"name": properties.get("name"), "postal": postal},
                "geometry": geometry,
            }
        )

    features.sort(key=lambda item: (item["properties"]["postal"] != "IN", item["properties"]["postal"]))
    output = {"type": "FeatureCollection", "features": features}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, separators=(",", ":")), encoding="utf-8")
    print(
        f"wrote {args.output} features={len(features)} "
        f"points={input_points}->{output_points} bytes={args.output.stat().st_size}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
