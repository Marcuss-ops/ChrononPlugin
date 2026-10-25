#!/usr/bin/env python3
"""Render the documentary overlay assets the map-flyover demos consume.

Chronon-owned content plates, drawn once and checked in, that demonstrate the
documentary motion vocabulary end to end:

  * a glow-circle area highlight for the Matterhorn window, placed through the
    *same* geodesy `build_georeferenced_basemap` used to crop the plate under it;
  * a North-Atlantic overlay with two glow highlights (Rome and New York) and a
    route line that a flyover draws on from one to the other;
  * the wide Atlantic basemap the travelling flyover pans across;
  * a reticle that a target lock closes onto, and a leader rule that draws on.

The raster stack (numpy/Pillow) is only needed for these plate commands, exactly
as in `map_cartography.py`, so the geodesy stays importable without it.

Usage
-----
  Chronon3d/tools/cartography/make_documentary_assets.py

It writes into `Chronon3d/assets/maps/` (a bootstrap-only directory).
"""

import argparse
import sys
from pathlib import Path

import numpy as np

# The geodesy lives beside this file; running as a script must not depend on the
# working directory or on `tools` being an importable package.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from map_cartography import (GeoreferencedMap, build_georeferenced_basemap,  # noqa: E402
                             window_origin, _PixelSize)

from PIL import Image, ImageDraw  # noqa: E402

MAPS_DIR = Path(__file__).resolve().parents[2] / "assets" / "maps"

# The Matterhorn window the `alps` place is rendered for.
ALPS_CENTER_LAT = 45.9763
ALPS_CENTER_LON = 7.6586
ALPS_ZOOM = 13
ALPS_CANVAS = (1920, 1080)

# The wide North-Atlantic window the `italy_usa` place pans across. Rome and New
# York both sit inside it, which is what lets one plate carry both highlights.
ATLANTIC_CENTER_LAT = 41.0
ATLANTIC_CENTER_LON = -30.0
ATLANTIC_ZOOM = 4
ATLANTIC_CANVAS = (3200, 1080)

ROME = (41.9028, 12.4964)
NEW_YORK = (40.7128, -74.0060)

AREA_INK = (245, 158, 11)     # amber — the "this is the zone" accent
ROUTE_INK = (245, 158, 11)
RETICLE_INK = (56, 189, 248)  # cyan — the target-lock accent
LEADER_INK = (56, 189, 248)


def _window_map(center_lat, center_lon, zoom, canvas):
    """The georeferenced canvas for a window."""

    x_min, y_min = window_origin(center_lat, center_lon, zoom, canvas[0], canvas[1])
    return GeoreferencedMap(_PixelSize(*canvas), center_lat, center_lon, zoom, x_min, y_min)


def _glow_marker(size, color):
    """A soft glow disc with a crisp ring and a core dot."""

    y, x = np.mgrid[0:size, 0:size].astype(np.float32)
    c = (size - 1) / 2.0
    radius = np.sqrt((x - c) ** 2 + (y - c) ** 2)
    normalized = radius / c
    glow = np.clip(1.0 - np.clip((normalized - 0.30) / 0.70, 0.0, 1.0), 0.0, 1.0) ** 1.8
    glow[normalized > 1.0] = 0.0
    rgba = np.zeros((size, size, 4), np.uint8)
    rgba[..., 0], rgba[..., 1], rgba[..., 2] = color
    rgba[..., 3] = (glow * 170).astype(np.uint8)
    marker = Image.fromarray(rgba, "RGBA")

    draw = ImageDraw.Draw(marker)
    ring = c * 0.80
    width = max(2, int(size * 0.014))
    draw.ellipse([c - ring, c - ring, c + ring, c + ring], outline=color + (255,), width=width)
    core = c * 0.12
    draw.ellipse([c - core, c - core, c + core, c + core], fill=color + (255,))
    return marker


def _glow_sprite(size, color):
    """One soft glow dot, drawn many times along a route.

    No hard ring and no hard core: the whole point of the sprite is that a chain
    of them, spaced closer than their own radius, composites into a single
    seamless ribbon with a soft edge rather than a bar with a hard one.
    """

    y, x = np.mgrid[0:size, 0:size].astype(np.float32)
    c = (size - 1) / 2.0
    normalized = np.sqrt((x - c) ** 2 + (y - c) ** 2) / c
    halo = np.clip(1.0 - normalized, 0.0, 1.0) ** 1.6
    core = np.clip(1.0 - normalized / 0.38, 0.0, 1.0) ** 1.2
    rgba = np.zeros((size, size, 4), np.uint8)
    rgba[..., 0], rgba[..., 1], rgba[..., 2] = color
    rgba[..., 3] = (np.clip(halo * 0.55 + core * 0.80, 0.0, 1.0) * 255).astype(np.uint8)
    return Image.fromarray(rgba, "RGBA")


def _paste_centred(canvas, marker, cx, cy):
    canvas.alpha_composite(marker, (int(round(cx - marker.width / 2.0)),
                                    int(round(cy - marker.height / 2.0))))


def roll_off_highlights(image, amount: float = 0.15):
    """Pull the top of the tonal range down, leaving the mids alone.

    A satellite plate is full of bright rock, snow and cloud. That detail is not
    the subject — the overlay drawn on top of it is — so the highlights come down
    a step while the mid tones stay exactly where the imagery put them. Applied
    as a ramp from 50% to 100%, so nothing below the midpoint moves at all.
    """

    rgb = np.asarray(image.convert("RGB"), dtype=np.float32) / 255.0
    ramp = np.clip((rgb - 0.5) / 0.5, 0.0, 1.0)
    graded = np.clip(rgb * (1.0 - amount * ramp), 0.0, 1.0)
    return Image.fromarray(np.rint(graded * 255.0).astype(np.uint8), "RGB")


def grade_plate(image, saturation: float = 0.65, highlight_amount: float = 0.15):
    """The documentary grade for an imagery plate: quieter colour, calmer highs.

    Overlays (amber glow, cyan reticle) only read against an image that has given
    up some of its own colour and contrast, which is why the grade is applied to
    the plate rather than to the overlays.
    """

    from PIL import ImageEnhance

    quiet = ImageEnhance.Color(image).enhance(saturation)
    return roll_off_highlights(quiet, highlight_amount)


def render_dim(out: Path, width: int = 1920, height: int = 1080) -> None:
    """The full-frame dim a spotlight falls back into: an opaque black plate.

    Its alpha is full on purpose. How far the map steps back is a *motion-side*
    decision (the layer's own opacity), so the asset carries the maximum and the
    flyover chooses the fraction of it the shot uses.
    """

    rgba = np.zeros((height, width, 4), np.uint8)
    rgba[..., 3] = 255
    Image.fromarray(rgba, "RGBA").save(out)
    print(f"{out.name}: dim plate {width}x{height}")


def render_vignette(out: Path, width: int = 1920, height: int = 1080) -> None:
    """A screen-space vignette: transparent centre, darkening towards the edges.

    Deliberately *not* baked into the plate. The plate is panned, so a vignette
    painted into its pixels would slide across the screen with it; a vignette is
    a property of the frame, so it is drawn as its own canvas-sized layer that
    does not move.
    """

    y, x = np.mgrid[0:height, 0:width].astype(np.float32)
    cx, cy = (width - 1) / 2.0, (height - 1) / 2.0
    radius = np.sqrt(((x - cx) / cx) ** 2 + ((y - cy) / cy) ** 2)
    normalized = radius / np.sqrt(2.0)
    # The inner 55% of the frame is untouched; the falloff is confined to the
    # outer band and tops out near half-strength at the extreme corner. A
    # vignette that reaches the middle of the frame is not a vignette, it is a
    # haze, and it reads as a broken render rather than as graded light.
    falloff = np.clip((normalized - 0.55) / 0.45, 0.0, 1.0) ** 1.8
    rgba = np.zeros((height, width, 4), np.uint8)
    rgba[..., 3] = (falloff * 120.0).astype(np.uint8)
    Image.fromarray(rgba, "RGBA").save(out)
    print(f"{out.name}: screen vignette {width}x{height}")


def render_area(out: Path) -> None:
    """The Matterhorn as a glowing circle, placed through the window's geodesy."""

    canvas = Image.new("RGBA", ALPS_CANVAS, (0, 0, 0, 0))
    geo = _window_map(ALPS_CENTER_LAT, ALPS_CENTER_LON, ALPS_ZOOM, ALPS_CANVAS)
    cx, cy = geo.to_screen(ALPS_CENTER_LAT, ALPS_CENTER_LON)
    _paste_centred(canvas, _glow_marker(460, AREA_INK), cx, cy)
    canvas.save(out)
    print(f"{out.name}: area glow circle {ALPS_CANVAS[0]}x{ALPS_CANVAS[1]}")


def render_atlantic_dual(out: Path) -> None:
    """Both ends of the route, highlighted, on the wide Atlantic window."""

    canvas = Image.new("RGBA", ATLANTIC_CANVAS, (0, 0, 0, 0))
    geo = _window_map(ATLANTIC_CENTER_LAT, ATLANTIC_CENTER_LON, ATLANTIC_ZOOM, ATLANTIC_CANVAS)
    for place in (ROME, NEW_YORK):
        cx, cy = geo.to_screen(*place)
        _paste_centred(canvas, _glow_marker(300, AREA_INK), cx, cy)
    canvas.save(out)
    print(f"{out.name}: dual highlight {ATLANTIC_CANVAS[0]}x{ATLANTIC_CANVAS[1]}")


def render_route_sprite(out: Path, size: int = 160) -> None:
    """The single glow sprite the flyover repeats along the great circle.

    The flyover walks the arc and drops one of these at each step; because the
    chain spacing is smaller than the sprite's soft radius, the repeated sprites
    merge into one soft, curved route with no visible beading and no hard edge.
    """

    _glow_sprite(size, ROUTE_INK).save(out)
    print(f"{out.name}: route glow sprite {size}x{size}")


def render_atlantic_basemap(out: Path) -> None:
    """The wide plate the travelling flyover pans across, graded for overlays.

    No baked vignette: the plate is panned, so the vignette belongs to the frame
    and is authored as its own screen-space layer instead (`render_vignette`).
    """

    plate = build_georeferenced_basemap(ATLANTIC_CENTER_LAT, ATLANTIC_CENTER_LON,
                                        ATLANTIC_ZOOM, ATLANTIC_CANVAS[0], ATLANTIC_CANVAS[1],
                                        provider="esri_dark", contrast=1.1, vignette=False)
    grade_plate(plate.image).save(out)
    print(f"{out.name}: Atlantic plate {ATLANTIC_CANVAS[0]}x{ATLANTIC_CANVAS[1]} (graded)")


def render_graded_copy(source: Path, out: Path) -> None:
    """Write a graded copy of an existing plate, leaving the original intact.

    The Matterhorn plate was rendered by `map_cartography.py` with the relief
    already baked in; re-deriving it here would mean re-fetching the DEM. Grading
    the file that exists keeps the terrain the DEM produced and only quietens it.
    """

    if not source.exists():
        print(f"{out.name}: skipped (no {source.name} to grade)")
        return
    with Image.open(source) as plate:
        grade_plate(plate).save(out)
    print(f"{out.name}: graded copy of {source.name}")


def render_reticle(out: Path, size: int = 256) -> None:
    """A ring with four corner ticks: a lock-on reticle."""

    image = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    centre = size / 2.0
    radius = size * 0.38
    width = max(2, int(size * 0.018))
    ink = RETICLE_INK + (235,)

    draw.ellipse([centre - radius, centre - radius, centre + radius, centre + radius],
                 outline=ink, width=width)
    inner, outer = size * 0.24, size * 0.06
    for (ax, ay, bx, by) in [(centre, outer, centre, inner), (centre, size - inner, centre, size - outer),
                             (outer, centre, inner, centre), (size - inner, centre, size - outer, centre)]:
        draw.line([ax, ay, bx, by], fill=ink, width=width)

    image.save(out)
    print(f"{out.name}: reticle {size}x{size}")


def render_leader(out: Path, width: int = 1040, height: int = 14) -> None:
    """A rule with a round head: a leader line that draws on."""

    image = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    mid = height / 2.0
    draw.rounded_rectangle([height / 2.0, mid - height / 4.0, width, mid + height / 4.0],
                           radius=height / 4.0, fill=LEADER_INK + (220,))
    draw.ellipse([0, 0, height, height], fill=RETICLE_INK + (255,))

    image.save(out)
    print(f"{out.name}: leader {width}x{height}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    parser.add_argument("--out-dir", default=str(MAPS_DIR))
    parser.add_argument("--skip-network", action="store_true",
                        help="skip the Atlantic basemap (needs tile downloads)")
    args = parser.parse_args(argv)

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    render_area(out_dir / "doc_area_matterhorn.png")
    render_reticle(out_dir / "doc_reticle.png")
    render_leader(out_dir / "doc_leader.png")
    render_atlantic_dual(out_dir / "doc_atlantic_dual.png")
    render_route_sprite(out_dir / "doc_glow_dot.png")
    render_vignette(out_dir / "doc_vignette.png")
    render_dim(out_dir / "doc_dim.png")
    render_graded_copy(out_dir / "alps_basemap.png", out_dir / "alps_basemap_doc.png")
    if not args.skip_network:
        render_atlantic_basemap(out_dir / "italy_usa_basemap.png")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
