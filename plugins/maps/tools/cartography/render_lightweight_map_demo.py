#!/usr/bin/env python3
"""Create a small three-treatment Pavia map demo from one cached tile plate."""

import argparse
import gzip
import json
import math
import subprocess
import tempfile
import time
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont


def mercator_px(lat: float, lon: float, zoom: int) -> tuple[float, float]:
    lat = max(-85.05112878, min(85.05112878, lat))
    scale = 256 * (2 ** zoom)
    x = (lon + 180.0) / 360.0 * scale
    y = (1.0 - math.asinh(math.tan(math.radians(lat))) / math.pi) * 0.5 * scale
    return x, y


def projected_paths(feature: dict, zoom: int, center: tuple[float, float],
                    size: tuple[int, int]) -> list[list[tuple[int, int]]]:
    cx, cy = mercator_px(center[0], center[1], zoom)
    x0, y0 = cx - size[0] / 2, cy - size[1] / 2
    geometry = feature["geometry"]
    polygons = geometry["coordinates"] if geometry["type"] == "MultiPolygon" else [geometry["coordinates"]]
    paths = []
    for polygon in polygons:
        for ring in polygon:
            path = []
            for lon, lat, *_ in ring:
                x, y = mercator_px(lat, lon, zoom)
                path.append((round(x - x0), round(y - y0)))
            if len(path) >= 3:
                paths.append(path)
    return paths


def font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont:
    name = "DejaVuSans-Bold.ttf" if bold else "DejaVuSans.ttf"
    return ImageFont.truetype(f"/usr/share/fonts/truetype/dejavu/{name}", size)


def glow_layer(size, paths, color, radius, intensity):
    mask = Image.new("L", size, 0)
    draw = ImageDraw.Draw(mask)
    width = 3
    for path in paths:
        draw.line(path, fill=round(255 * intensity), width=width, joint="curve")
    mask = mask.filter(ImageFilter.GaussianBlur(radius))
    layer = Image.new("RGBA", size, (*color, 0))
    layer.putalpha(mask)
    return layer


def draw_callout(image: Image.Image, anchor, style_index: int):
    draw = ImageDraw.Draw(image)
    x, y = anchor
    scale = image.width / 1280.0
    color = [(123, 235, 224), (106, 186, 255), (250, 198, 115)][style_index]
    # Slim leader, small anchor and an unobtrusive editorial label.
    label_x = min(image.width - round(330 * scale), max(round(28 * scale), x + round(105 * scale)))
    label_y = max(round(26 * scale), y - round(108 * scale))
    elbow = (x + round(24 * scale), y - round(36 * scale))
    end = (label_x, label_y + round(20 * scale))
    draw.line([anchor, elbow, (end[0] - round(16 * scale), elbow[1]), end],
              fill=(*color, 220), width=max(2, round(2 * scale)))
    r = round(6 * scale)
    draw.ellipse((x - r, y - r, x + r, y + r), fill=(246, 250, 251, 255))
    r = round(12 * scale)
    draw.ellipse((x - r, y - r, x + r, y + r), outline=(*color, 210),
                 width=max(2, round(2 * scale)))
    draw.text((label_x, label_y - round(3 * scale)), "PAVIA",
              font=font(round(28 * scale), True), fill=(248, 250, 250))
    draw.text((label_x, label_y + round(32 * scale)), "LOMBARDIA · ITALIA",
              font=font(round(15 * scale)), fill=(177, 191, 203))


def render_demo(base_path: Path, geojson_path: Path, output_path: Path,
                zoom: int, center: tuple[float, float], fps: int = 30):
    base = Image.open(base_path).convert("RGB")
    size = base.size
    collection = json.loads(geojson_path.read_text(encoding="utf-8"))
    feature = next(f for f in collection["features"]
                   if f["properties"].get("shapeName") == "Pavia")
    paths = projected_paths(feature, zoom, center, size)
    anchor = tuple(round(v) for v in (
        mercator_px(center[0], center[1], zoom)[0] -
        (mercator_px(center[0], center[1], zoom)[0] - size[0] / 2),
        mercator_px(center[0], center[1], zoom)[1] -
        (mercator_px(center[0], center[1], zoom)[1] - size[1] / 2),
    ))
    # The highlighted administrative feature itself is the reusable vector payload.
    feature_bytes = json.dumps({"type": "FeatureCollection", "features": [feature]},
                               separators=(",", ":")).encode()
    feature_gz = gzip.compress(feature_bytes, compresslevel=9)

    # Three quiet treatments of the same tile plate and the same vector path.
    styles = [
        {"name": "01 · THIN VECTOR", "fill": (89, 217, 207, 25),
         "line": (141, 244, 231, 240), "glow": None},
        {"name": "02 · SOFT GLOW", "fill": (75, 161, 255, 28),
         "line": (203, 234, 255, 250), "glow": ((48, 147, 255), 12, 0.72)},
        {"name": "03 · WARM EDITORIAL", "fill": (236, 171, 80, 28),
         "line": (255, 210, 144, 245), "glow": ((238, 155, 65), 7, 0.42)},
    ]
    output_path.parent.mkdir(parents=True, exist_ok=True)
    started = time.perf_counter()
    with tempfile.TemporaryDirectory(prefix="pavia-light-map-") as temp:
        temp_path = Path(temp)
        for index, style in enumerate(styles):
            frames = []
            for frame_number in range(fps * 2):
                progress = min(1.0, frame_number / max(1, round(fps * 0.55)))
                visible_points = []
                for path in paths:
                    count = max(2, round((len(path) - 1) * progress) + 1)
                    visible_points.append(path[:count])
                image = base.convert("RGBA")
                if style["glow"]:
                    image.alpha_composite(glow_layer(size, visible_points, *style["glow"]))
                overlay = Image.new("RGBA", size, (0, 0, 0, 0))
                overlay_draw = ImageDraw.Draw(overlay)
                for path in visible_points:
                    if len(path) >= 3:
                        overlay_draw.polygon(path, fill=style["fill"])
                    if len(path) >= 2:
                        overlay_draw.line(path, fill=style["line"], width=2, joint="curve")
                image.alpha_composite(overlay)
                draw = ImageDraw.Draw(image)
                draw_callout(image, anchor, index)
                scale = image.width / 1280.0
                draw.text((round(40 * scale), image.height - round(47 * scale)), style["name"],
                          font=font(round(17 * scale), True), fill=(235, 240, 242, 220))
                frames.append(image.convert("RGB"))
            # Store a still for quick inspection and build video from generated frames.
            frames[30].save(output_path.with_name(f"{output_path.stem}_{index + 1}.webp"),
                            "WEBP", quality=89, method=6)
            for frame_number, frame in enumerate(frames):
                frame.save(temp_path / f"{index * len(frames) + frame_number:04d}.jpg",
                           "JPEG", quality=94, subsampling=0)
        render_seconds = time.perf_counter() - started
        command = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
                   "-framerate", str(fps), "-i", str(temp_path / "%04d.jpg"),
                   "-c:v", "h264_nvenc", "-preset", "p6", "-cq", "18",
                   "-pix_fmt", "yuv420p", "-movflags", "+faststart", str(output_path)]
        subprocess.run(command, check=True)

    return {
        "styles": len(styles), "frames": len(styles) * fps * 2,
        "render_seconds": render_seconds,
        "vector_geojson_bytes": len(feature_bytes),
        "vector_geojson_gzip_bytes": len(feature_gz),
        "base_plate_bytes": base_path.stat().st_size,
        "video_bytes": output_path.stat().st_size,
        "feature_points": sum(len(path) for path in paths),
        "source_feature": feature["properties"]["shapeName"],
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", type=Path, required=True)
    parser.add_argument("--geojson", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--zoom", type=int, default=12)
    parser.add_argument("--center", type=float, nargs=2, default=(45.185, 9.158),
                        metavar=("LAT", "LON"))
    args = parser.parse_args()
    import pprint
    pprint.pp(render_demo(args.base, args.geojson, args.output,
                          args.zoom, tuple(args.center)))
