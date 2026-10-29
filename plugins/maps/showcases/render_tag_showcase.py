#!/usr/bin/env python3
"""Render named geographic-label presets as a short, page-based MP4 gallery."""

import argparse
import json
import subprocess
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont


FONT_FILES = {
    "poppins": "Poppins-Bold.ttf",
    "inter": "Inter.ttf",
    "inter_bold": "Inter-Bold.ttf",
    "dejavu_sans": "DejaVuSans.ttf",
    "playfair_display_italic": "PlayfairDisplay-Italic.ttf",
    "bricolage_grotesque": "Bricolage-Grotesque.ttf",
    "oswald": "Oswald.ttf",
    "abril_fatface": "AbrilFatface-Regular.ttf",
}


def rgb(value: str) -> tuple[int, int, int]:
    value = value.lstrip("#")
    return tuple(int(value[i:i + 2], 16) for i in (0, 2, 4))


def draw_tag(page: Image.Image, label: str, preset_name: str,
             preset: dict, font_root: Path, row_top: int) -> None:
    width, _ = page.size
    row_h = 150
    row = Image.new("RGBA", (width, row_h), (0, 0, 0, 0))
    draw = ImageDraw.Draw(row)
    guide = ImageFont.truetype(str(font_root / FONT_FILES["inter_bold"]), 16)
    style_name = preset_name.replace("_", " ").upper()
    draw.text((320, 12), style_name, font=guide, fill=(152, 166, 180, 255))
    draw.line((320, 39, 1600, 39), fill=(255, 255, 255, 28), width=1)

    font = ImageFont.truetype(str(font_root / FONT_FILES[preset["font_family"]]),
                              round(float(preset.get("font_size_px", 36))))
    bounds = draw.textbbox((0, 0), label, font=font, stroke_width=1)
    text_w, text_h = bounds[2] - bounds[0], bounds[3] - bounds[1]
    background = preset.get("background")
    padding = background.get("padding", [20, 10]) if background else [0, 0]
    card_w = text_w + 2 * round(padding[0])
    card_h = text_h + 2 * round(padding[1])
    card_x = (width - card_w) // 2
    card_y = 58 + (76 - card_h) // 2

    if background:
        shadow = preset.get("shadow")
        if shadow:
            shadow_layer = Image.new("RGBA", row.size, (0, 0, 0, 0))
            shadow_draw = ImageDraw.Draw(shadow_layer)
            offset = shadow.get("offset", [0, 3])
            shadow_draw.rounded_rectangle(
                (card_x + offset[0], card_y + offset[1],
                 card_x + card_w + offset[0], card_y + card_h + offset[1]),
                radius=round(background.get("radius", 0)),
                fill=(*rgb(shadow["color"]), round(170 * shadow.get("opacity", 0.5))),
            )
            blur = min(12, round(float(shadow.get("blur", 0)) / 3))
            if blur:
                shadow_layer = shadow_layer.filter(ImageFilter.GaussianBlur(blur))
            row = Image.alpha_composite(row, shadow_layer)
            draw = ImageDraw.Draw(row)
        draw.rounded_rectangle(
            (card_x, card_y, card_x + card_w, card_y + card_h),
            radius=round(background.get("radius", 0)),
            fill=(*rgb(background["color"]), round(255 * background.get("opacity", 1))),
        )

    x = (width - text_w) // 2
    y = 58 + (76 - text_h) // 2 - bounds[1]
    glow = preset.get("glow")
    if glow:
        glow_mask = Image.new("L", row.size, 0)
        ImageDraw.Draw(glow_mask).text(
            (x, y), label, font=font,
            fill=round(255 * float(glow.get("intensity", 0.5))),
        )
        blur = min(18, round(float(glow.get("radius", 0)) / 3))
        if blur:
            glow_mask = glow_mask.filter(ImageFilter.GaussianBlur(blur))
        glow_layer = Image.new("RGBA", row.size, (*rgb(glow["color"]), 0))
        glow_layer.putalpha(glow_mask)
        row = Image.alpha_composite(row, glow_layer)
        draw = ImageDraw.Draw(row)
    stroke = preset.get("stroke")
    draw.text(
        (x, y), label, font=font, fill=(*rgb(preset["fill"]), 255),
        stroke_width=round(float(stroke.get("width", 0))) if stroke else 0,
        stroke_fill=rgb(stroke["color"]) if stroke else None,
    )
    page.alpha_composite(row, (0, row_top))


def render(config_path: Path, catalog_path: Path, output_path: Path) -> None:
    config = json.loads(config_path.read_text(encoding="utf-8"))
    catalog = json.loads(catalog_path.read_text(encoding="utf-8"))
    presets = {style["name"]: style for style in catalog["styles"]}
    tags = config["tags"]
    if len(tags) != 15:
        raise ValueError("the gallery config must contain exactly 15 tag samples")
    for tag in tags:
        if tag.get("style") not in presets:
            raise ValueError(f"unknown geographic tag preset: {tag.get('style')}")

    width = int(config.get("width", 1920))
    height = int(config.get("height", 1080))
    fps = int(config.get("fps", 30))
    duration = float(config.get("duration_seconds", 12))
    pages = [tags[i:i + 5] for i in range(0, len(tags), 5)]
    font_root = catalog_path.parent.parent.parent / "assets" / "fonts"
    output_path.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="geo-tag-showcase-") as temp:
        temp_path = Path(temp)
        for page_number, page_tags in enumerate(pages):
            page = Image.new("RGBA", (width, height), (17, 20, 26, 255))
            page_draw = ImageDraw.Draw(page)
            for y in range(height):
                shade = round(3 * y / max(1, height - 1))
                page_draw.line((0, y, width, y), fill=(17 + shade, 20 + shade, 26 + shade, 255))
            row_top = (height - 5 * 150) // 2
            for row, tag in enumerate(page_tags):
                draw_tag(page, tag["label"], tag["style"], presets[tag["style"]],
                         font_root, row_top + row * 150)
            page.convert("RGB").save(temp_path / f"{page_number:03d}.png")

        command = [
            "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
            "-framerate", f"1/{duration / len(pages):g}",
            "-i", str(temp_path / "%03d.png"), "-vf", f"fps={fps}",
            "-t", str(duration), "-c:v", "h264_nvenc", "-preset", "p6",
            "-cq", "15", "-pix_fmt", "yuv420p", "-movflags", "+faststart",
            str(output_path),
        ]
        subprocess.run(command, check=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--catalog", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    render(args.config, args.catalog, args.output)
