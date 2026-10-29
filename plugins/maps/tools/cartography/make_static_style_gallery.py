#!/usr/bin/env python3
"""Create static map-style samples with different palettes and line density."""

import argparse
import json
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


WIDTH, HEIGHT = 1920, 1080
CENTER = (45.185, 9.158)
ZOOM = 9
FONT_REG = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
FONT_BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"


def project(lat, lon):
    lat = max(-85.05112878, min(85.05112878, lat))
    scale = 256 * (2 ** ZOOM)
    x = (lon + 180.0) / 360.0 * scale
    y = (1.0 - math.asinh(math.tan(math.radians(lat))) / math.pi) * 0.5 * scale
    cx = (CENTER[1] + 180.0) / 360.0 * scale
    cy = (1.0 - math.asinh(math.tan(math.radians(CENTER[0]))) / math.pi) * 0.5 * scale
    return round(x - (cx - WIDTH / 2)), round(y - (cy - HEIGHT / 2))


def paths_for(feature):
    geom = feature.get("geometry") or {}
    kind, coords = geom.get("type"), geom.get("coordinates", [])
    polygons = coords if kind == "MultiPolygon" else [coords] if kind == "Polygon" else []
    paths = []
    for polygon in polygons:
        if not polygon:
            continue
        for ring in polygon:
            path = [project(lat, lon) for lon, lat, *_ in ring]
            if len(path) >= 3:
                min_x = min(x for x, _ in path)
                max_x = max(x for x, _ in path)
                min_y = min(y for _, y in path)
                max_y = max(y for _, y in path)
                if max_x < -10 or min_x > WIDTH + 10 or max_y < -10 or min_y > HEIGHT + 10:
                    continue
                paths.append(path)
    return paths


def font(size, bold=False):
    return ImageFont.truetype(FONT_BOLD if bold else FONT_REG, size)


def label_panel(image, title, dark):
    draw = ImageDraw.Draw(image, "RGBA")
    fill = (7, 15, 24, 184) if dark else (248, 247, 243, 218)
    ink = (245, 248, 250, 255) if dark else (35, 43, 50, 255)
    draw.rounded_rectangle((38, 34, 610, 98), radius=14, fill=fill)
    draw.text((62, 48), title, font=font(23, True), fill=ink)


def draw_feature(image, paths, fill=None, line=(255, 255, 255), width=2):
    if fill is not None:
        layer = Image.new("RGBA", image.size, (0, 0, 0, 0))
        draw = ImageDraw.Draw(layer)
        for path in paths:
            draw.polygon(path, fill=fill)
        image.alpha_composite(layer)
    draw = ImageDraw.Draw(image, "RGBA")
    for path in paths:
        draw.line(path + [path[0]], fill=line, width=width, joint="curve")


def style_map(base, pavia_paths, title, colors, credit, dark=True):
    image = base.convert("RGBA")
    draw_feature(image, pavia_paths, colors[0], colors[1], colors[2])
    label_panel(image, title, dark)
    draw = ImageDraw.Draw(image, "RGBA")
    ink = (242, 245, 247, 218) if dark else (63, 71, 77, 205)
    draw.text((image.width - 830, image.height - 38), credit,
              font=font(15), fill=ink)
    return image.convert("RGB")


def create_gallery(args):
    output = args.output
    output.mkdir(parents=True, exist_ok=True)
    pavia = json.loads(args.adm3.read_text(encoding="utf-8"))
    pavia_feature = next(f for f in pavia["features"]
                         if f.get("properties", {}).get("shapeName") == "Pavia")
    pavia_paths = paths_for(pavia_feature)
    adm1 = json.loads(args.adm1.read_text(encoding="utf-8"))
    adm3_paths = [path for feature in pavia["features"] for path in paths_for(feature)]
    adm1_paths = [path for feature in adm1["features"] for path in paths_for(feature)]

    names = []
    # Selected atlas style: readable regional borders and a red focus area.
    image = Image.new("RGBA", (WIDTH, HEIGHT), (247, 245, 238, 255))
    draw_feature(image, adm1_paths, None, (132, 139, 136, 255), 3)
    draw_feature(image, pavia_paths, (255, 59, 48, 88), (255, 59, 48, 255), 3)
    label_panel(image, "02 · ATLANTE / CONFINI LEGGIBILI", dark=False)
    names.append(("02_vector_regional.webp", image.convert("RGB")))

    tile_specs = [
        ("04 · LIGHT / RIEMPIMENTO ROSSO", args.light, (255, 59, 48, 205), (255, 59, 48, 255), 3, False),
        ("06 · SATELLITE / GLOW ROSSO", args.satellite, (255, 59, 48, 75), (255, 59, 48, 255), 3, True),
    ]
    for title, path, fill, line, weight, dark in tile_specs:
        base = Image.open(path).convert("RGBA")
        image = style_map(base, pavia_paths, title, (fill, line, weight),
                          "Sources: Esri, Maxar, Earthstar Geographics, and the GIS User Community",
                          dark)
        names.append(("04_light.webp" if "LIGHT" in title else "06_satellite.webp", image))

    for filename, image in names:
        image.save(output / filename, "WEBP", quality=91, method=6)

    # Compact 3-up contact sheet; each sample is kept separately above.
    thumb_size = (640, 360)
    sheet = Image.new("RGB", (thumb_size[0] * 3, thumb_size[1]), (16, 22, 29))
    for i, (filename, _) in enumerate(names):
        thumb = Image.open(output / filename).convert("RGB").resize(thumb_size, Image.LANCZOS)
        sheet.paste(thumb, (i * thumb_size[0], 0))
    sheet.save(output / "map_styles_contact_sheet.webp", "WEBP", quality=90, method=6)
    return output / "map_styles_contact_sheet.webp"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adm1", type=Path, required=True)
    parser.add_argument("--adm3", type=Path, required=True)
    parser.add_argument("--light", type=Path, required=True)
    parser.add_argument("--dark", type=Path, required=True)
    parser.add_argument("--satellite", type=Path, required=True)
    parser.add_argument("--topo", type=Path, required=True)
    parser.add_argument("--natgeo", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    print(create_gallery(args))
