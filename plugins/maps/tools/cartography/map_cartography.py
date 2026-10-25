#!/usr/bin/env python3
"""
Map Cartography & Geodesy Engine for Chronon3d / ChrononMotion3D.

The raster half of the map pipeline: it downloads tiles, stitches and grades an
imagery plate, derives DEM relief for the *same* window, and records the
georeference that ChrononMotion3D's `MapWindow` turns into plane coordinates.

It lives on the content side (`Chronon3d/tools/`) rather than beside its
geodesy twin (`ChrononMotion3D/include/chrononmotion/geospatial/`) because a
tile downloader, a PNG decoder and a raster grader are exactly the content
machinery that tree's boundary guardrail keeps out of the motion engine — and
because the plates it writes are `Chronon3d/assets/maps`. The two sides agree
on the projection, and `--self-test` is where that agreement is checked.

Provides:
- Exact Web Mercator (EPSG:3857) projection math
- Multithreaded tile downloading (Esri Dark Gray / Satellite, OSM, CartoDB)
- High-contrast documentary grading & vignette application
- Real terrain relief: SRTM / Copernicus DEM (Terrarium tiles) -> hillside shading
- Sub-pixel georeferenced coordinate translation: (lat, lon) -> (screen_x, screen_y)

Usage
-----
  # an imagery plate for a window, with the DEM hillshade baked into it
  tools/cartography/map_cartography.py basemap 45.9763 7.6586 13 alps_basemap.png \\
      --provider esri_sat --no-vignette --relief

  # the DEM relief for the same window, as a standalone soft-light plate
  # (`--style shade` is what the shipped alps_relief.png is; the default
  # `drape` is the alpha-overlay variant a plan composites at render time)
  tools/cartography/map_cartography.py relief 45.9763 7.6586 13 alps_relief.png --style shade

  # prove the geodesy before trusting a georeference (offline, no tile fetch)
  tools/cartography/map_cartography.py --self-test
"""

from __future__ import annotations

import argparse
import math
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

# The raster stack is optional *at import time*, and that is deliberate: the
# geodesy (Web Mercator, window placement) is plain arithmetic, while the plate
# commands need numpy and Pillow. `--self-test` is registered as a build gate,
# and every Python gate in this tree runs on the interpreter CMake found with
# nothing installed into it (see tests/CMakeLists.txt). So the geodesy is
# asserted everywhere and the raster checks disclose their skip, rather than
# the whole gate failing on a host that can still produce and georeference a
# window. The plate commands say which module is missing.
try:
    import numpy as np
    from PIL import Image, ImageEnhance, ImageFilter, ImageDraw
    _RASTER_IMPORT_ERROR: str | None = None
except ImportError as _exc:  # pragma: no cover - depends on the host
    np = None  # type: ignore[assignment]
    Image = ImageEnhance = ImageFilter = ImageDraw = None  # type: ignore[assignment]
    _RASTER_IMPORT_ERROR = str(_exc)

try:
    from dynamic_tile_pyramid import (
        DynamicTilePyramid,
        compute_altitude_km,
        draw_hud_overlay,
        latlon_to_global_px,
        TILE_URLS as PYRAMID_TILE_URLS,
    )
except ImportError:
    try:
        from .dynamic_tile_pyramid import (
            DynamicTilePyramid,
            compute_altitude_km,
            draw_hud_overlay,
            latlon_to_global_px,
            TILE_URLS as PYRAMID_TILE_URLS,
        )
    except Exception:
        DynamicTilePyramid = compute_altitude_km = draw_hud_overlay = None
        latlon_to_global_px = PYRAMID_TILE_URLS = None

# The mathematical self-test does not need the raster engine. Keep CLI parser
# choices available when optional NumPy/OpenCV imports prevent loading it.
if PYRAMID_TILE_URLS is None:
    PYRAMID_TILE_URLS = {
        "esri_dark": "",
        "esri_sat": "",
        "osm": "",
        "carto_dark": "",
    }


# `assets/` is a bootstrap-only directory (see .gitignore), so this file
# resolves the maps its own build produces from its own location rather than
# from the working directory: `tools/cartography/` -> Chronon3d root.
CHRONON3D_ROOT = Path(__file__).resolve().parents[2]
MAPS_DIR = CHRONON3D_ROOT / "assets" / "maps"
CACHE_DIR = MAPS_DIR / "cache"
OUTPUT_DIR = MAPS_DIR

CACHE_DIR.mkdir(parents=True, exist_ok=True)
OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

TILE_URLS = {
    "esri_dark": "https://server.arcgisonline.com/ArcGIS/rest/services/Canvas/World_Dark_Gray_Base/MapServer/tile/{z}/{y}/{x}",
    "esri_sat": "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}",
    "esri_topo": "https://server.arcgisonline.com/ArcGIS/rest/services/World_Topo_Map/MapServer/tile/{z}/{y}/{x}",
    "esri_natgeo": "https://server.arcgisonline.com/ArcGIS/rest/services/NatGeo_World_Map/MapServer/tile/{z}/{y}/{x}",
    "esri_ocean": "https://server.arcgisonline.com/ArcGIS/rest/services/Ocean/World_Ocean_Base/MapServer/tile/{z}/{y}/{x}",
    "esri_light": "https://server.arcgisonline.com/ArcGIS/rest/services/Canvas/World_Light_Gray_Base/MapServer/tile/{z}/{y}/{x}",
    "osm": "https://tile.openstreetmap.org/{z}/{x}/{y}.png",
    "carto_dark": "https://a.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}.png",
    # SRTM / Copernicus DEM, AWS Open Data: height in metres packed into the
    # RGB channels, h = (R * 256 + G + B / 256) - 32768. No key, no registration.
    "terrarium": "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/{z}/{x}/{y}.png"
}

# Web Mercator metres per pixel at the equator, at zoom 0.
MERCATOR_EQUATOR_METRES_PER_PIXEL = 156543.03392

def latlon_to_mercator_px(lat: float, lon: float, zoom: int) -> tuple[float, float]:
    """Convert WGS84 (lat, lon) to global Mercator pixel coordinates at given zoom level."""
    lat = max(-85.05112878, min(85.05112878, lat))
    lon = max(-180.0, min(180.0, lon))
    scale = 256.0 * (2 ** zoom)
    
    x = (lon + 180.0) / 360.0 * scale
    
    lat_rad = math.radians(lat)
    y = (1.0 - math.log(math.tan(lat_rad) + 1.0 / math.cos(lat_rad)) / math.pi) * 0.5 * scale
    return x, y

def mercator_px_to_latlon(x: float, y: float, zoom: int) -> tuple[float, float]:
    """Convert global Mercator pixel coordinates back to WGS84 (lat, lon)."""
    scale = 256.0 * (2 ** zoom)
    lon = (x / scale) * 360.0 - 180.0
    
    n = math.pi - 2.0 * math.pi * (y / scale)
    lat = math.degrees(math.atan(math.sinh(n)))
    return lat, lon

def download_tile(provider: str, z: int, x: int, y: int) -> Image.Image:
    """Download a single map tile with disk caching."""
    max_idx = 2 ** z
    x = x % max_idx
    if y < 0 or y >= max_idx:
        return Image.new("RGBA", (256, 256), (10, 15, 25, 255))
        
    cache_file = CACHE_DIR / f"{provider}_{z}_{x}_{y}.png"
    if cache_file.exists():
        try:
            return Image.open(cache_file).convert("RGBA")
        except Exception:
            pass

    url_template = TILE_URLS.get(provider, TILE_URLS["carto_dark"])
    url = url_template.format(z=z, x=x, y=y)
    headers = {"User-Agent": "VeloxEditing-Chronon3d/2.0 (Geospatial Render Engine)"}
    req = urllib.request.Request(url, headers=headers)
    
    for attempt in range(3):
        try:
            with urllib.request.urlopen(req, timeout=8) as resp:
                data = resp.read()
                with open(cache_file, "wb") as f:
                    f.write(data)
                return Image.open(cache_file).convert("RGBA")
        except Exception as e:
            if attempt == 2:
                print(f"[Warning] Failed to fetch tile {url}: {e}")
                return Image.new("RGBA", (256, 256), (10, 15, 25, 255))

def apply_vignette(im: Image.Image, intensity: float = 0.65) -> Image.Image:
    """Apply documentary-style smooth radial vignette darkening."""
    w, h = im.size
    vignette = Image.new("L", (w, h), 255)
    draw = ImageDraw.Draw(vignette)
    
    cx, cy = w / 2.0, h / 2.0
    max_radius = math.sqrt(cx * cx + cy * cy)
    num_steps = 40
    
    for i in range(num_steps):
        r = max_radius * (0.45 + 0.55 * (i / num_steps))
        val = int(255 * (1.0 - intensity * (i / num_steps) ** 1.8))
        draw.ellipse([cx - r, cy - r, cx + r, cy + r], fill=val)
        
    vignette = vignette.filter(ImageFilter.GaussianBlur(radius=32))
    
    # Dark overlay
    dark = Image.new("RGBA", (w, h), (0, 0, 0, 255))
    dark.putalpha(Image.eval(vignette, lambda p: 255 - p))
    
    out = im.copy()
    out.alpha_composite(dark)
    return out

class GeoreferencedMap:
    def __init__(self, image: Image.Image, center_lat: float, center_lon: float, zoom: int, x_min: float, y_min: float):
        self.image = image
        self.center_lat = center_lat
        self.center_lon = center_lon
        self.zoom = zoom
        self.x_min = x_min
        self.y_min = y_min
        self.width, self.height = image.size

    def to_screen(self, lat: float, lon: float) -> tuple[float, float]:
        """Mathematically convert WGS84 (lat, lon) to exact canvas pixel coordinates [0..width, 0..height]."""
        gx, gy = latlon_to_mercator_px(lat, lon, self.zoom)
        return round(gx - self.x_min, 2), round(gy - self.y_min, 2)

    def save(self, rel_filename: str) -> str:
        """Save to Chronon3d asset map directory and return relative logical asset path."""
        out_path = OUTPUT_DIR / rel_filename
        self.image.save(out_path, "PNG", optimize=True)
        print(f"[Map] Stitched & graded basemap saved: {out_path} ({self.width}x{self.height})")
        return f"assets/maps/{rel_filename}"

def stitch_tile_grid(provider: str, zoom: int, t_x0: int, t_x1: int,
                     t_y0: int, t_y1: int) -> tuple[Image.Image, float, float]:
    """Fetch and stitch a tile range. Returns (mosaic, origin_x, origin_y) where
    origin_* is the global Mercator pixel of the mosaic's (0, 0) corner."""
    tile_coords = []
    for ty in range(t_y0, t_y1 + 1):
        for tx in range(t_x0, t_x1 + 1):
            tile_coords.append((tx, ty))

    tiles = {}
    with ThreadPoolExecutor(max_workers=8) as executor:
        futures = {executor.submit(download_tile, provider, zoom, tx, ty): (tx, ty)
                   for (tx, ty) in tile_coords}
        for fut in futures:
            coord = futures[fut]
            tiles[coord] = fut.result()

    mosaic = Image.new("RGBA", ((t_x1 - t_x0 + 1) * 256, (t_y1 - t_y0 + 1) * 256))
    for (tx, ty), tile_img in tiles.items():
        mosaic.paste(tile_img, ((tx - t_x0) * 256, (ty - t_y0) * 256))
    return mosaic, t_x0 * 256.0, t_y0 * 256.0


def window_origin(center_lat: float, center_lon: float, zoom: int,
                  width: int, height: int) -> tuple[float, float]:
    """Global Mercator pixel of a centred window's top-left corner."""
    cx, cy = latlon_to_mercator_px(center_lat, center_lon, zoom)
    return cx - width / 2.0, cy - height / 2.0


def stitch_window(provider: str, zoom: int, x_min: float, y_min: float,
                  width: int, height: int) -> Image.Image:
    """Stitch exactly the global-pixel window [x_min, x_min+width) x [y_min, y_min+height)."""
    t_x0 = int(math.floor(x_min / 256.0))
    t_x1 = int(math.floor((x_min + width) / 256.0))
    t_y0 = int(math.floor(y_min / 256.0))
    t_y1 = int(math.floor((y_min + height) / 256.0))
    mosaic, _, _ = stitch_tile_grid(provider, zoom, t_x0, t_x1, t_y0, t_y1)
    crop_x0 = int(round(x_min - t_x0 * 256.0))
    crop_y0 = int(round(y_min - t_y0 * 256.0))
    return mosaic.crop((crop_x0, crop_y0, crop_x0 + width, crop_y0 + height))


def bilinear_sample(plane: np.ndarray, xs: np.ndarray, ys: np.ndarray) -> np.ndarray:
    """Sample a 2D array at fractional index coordinates, bilinearly."""
    x0 = np.floor(xs).astype(np.int64)
    y0 = np.floor(ys).astype(np.int64)
    fx = (xs - x0).astype(np.float32)
    fy = (ys - y0).astype(np.float32)
    x0 = np.clip(x0, 0, plane.shape[1] - 2)
    y0 = np.clip(y0, 0, plane.shape[0] - 2)
    x1 = x0 + 1
    y1 = y0 + 1
    top = plane[y0[:, None], x0[None, :]] * (1.0 - fx)[None, :] + \
        plane[y0[:, None], x1[None, :]] * fx[None, :]
    bottom = plane[y1[:, None], x0[None, :]] * (1.0 - fx)[None, :] + \
        plane[y1[:, None], x1[None, :]] * fx[None, :]
    return top * (1.0 - fy)[:, None] + bottom * fy[:, None]


def dem_window_heights(center_lat: float, center_lon: float, zoom: int, width: int,
                       height: int, dem_zoom: int) -> np.ndarray:
    """Terrain elevation in metres, one sample per output pixel of the plate window.

    The DEM is coarser than a big satellite plate (Copernicus GLO-30 is ~30 m, which
    is zoom 12-13 at mid latitudes), so the heightfield is sampled *at the plate's
    own pixel grid* rather than cropped and resized: a crop at the DEM zoom would
    cover a different window, and resizing it would place the terrain anywhere the
    two windows disagree. Same window, same pixel centres, bilinear in between.
    """
    if dem_zoom > zoom:
        raise ValueError("dem_zoom cannot exceed the plate zoom: the window is defined at the plate zoom")
    x_min, y_min = window_origin(center_lat, center_lon, zoom, width, height)
    scale = 2.0 ** (dem_zoom - zoom)                 # plate pixel -> DEM pixel
    gx0 = x_min * scale
    gy0 = y_min * scale

    t_x0 = int(math.floor(gx0 / 256.0))
    t_y0 = int(math.floor(gy0 / 256.0))
    t_x1 = int(math.floor((gx0 + width * scale) / 256.0))
    t_y1 = int(math.floor((gy0 + height * scale) / 256.0))
    mosaic, origin_x, origin_y = stitch_tile_grid("terrarium", dem_zoom, t_x0, t_x1, t_y0, t_y1)

    rgb = np.asarray(mosaic.convert("RGB"), dtype=np.float32)
    heights = rgb[..., 0] * 256.0 + rgb[..., 1] + rgb[..., 2] / 256.0 - 32768.0

    # Pixel centres, in the mosaic's own array coordinates.
    xs = (x_min + np.arange(width, dtype=np.float32) + 0.5) * scale - origin_x - 0.5
    ys = (y_min + np.arange(height, dtype=np.float32) + 0.5) * scale - origin_y - 0.5
    return bilinear_sample(heights, xs, ys)


def hillshade(heights: np.ndarray, cell_metres: float, azimuth_deg: float,
              sun_altitude_deg: float, z_factor: float) -> np.ndarray:
    """Horn (1981) slope shading, north-up, y increasing south."""
    z = np.pad(heights, 1, mode="edge")
    dzdx = ((z[:-2, 2:] + 2.0 * z[1:-1, 2:] + z[2:, 2:]) -
            (z[:-2, :-2] + 2.0 * z[1:-1, :-2] + z[2:, :-2])) / (8.0 * cell_metres)
    dzdy = ((z[2:, :-2] + 2.0 * z[2:, 1:-1] + z[2:, 2:]) -
            (z[:-2, :-2] + 2.0 * z[:-2, 1:-1] + z[:-2, 2:])) / (8.0 * cell_metres)
    slope = np.arctan(z_factor * np.hypot(dzdx, dzdy))
    aspect = np.arctan2(dzdy, -dzdx)
    azimuth = math.radians(azimuth_deg)
    altitude = math.radians(sun_altitude_deg)
    shade = (math.sin(altitude) * np.cos(slope) +
             math.cos(altitude) * np.sin(slope) * np.cos(azimuth - aspect))
    return np.clip(shade, 0.0, 1.0)


def build_terrain_relief(center_lat: float, center_lon: float, zoom: int,
                         width: int = 1920, height: int = 1080,
                         dem_zoom_max: int = 13,
                         azimuth: float = 315.0, sun_altitude: float = 45.0,
                         z_factor: float = 1.6, strength: float = 0.9,
                         blur: float = 1.2, style: str = "drape",
                         contrast: float = 0.8) -> GeoreferencedMap:
    """Terrain relief for *exactly* the window `build_georeferenced_basemap` uses.

    Returns the same GeoreferencedMap window as the plate, so the two land on each
    other pixel for pixel and one can be laid over the other as a drape.

    `style="drape"` is the default and the one a Chronon plan can render on any
    backend: a two-sided warm/cool overlay whose alpha follows the deviation from
    flat, with the tints chosen *symmetric about the plate's own mid tone* so the
    drape adds terrain without shifting the grade it is laid over.

    `style="shade"` is an opaque mid-grey luminance plate centred on 0.5 — the
    neutral value of a *soft-light* blend — for a compositor that will blend it.
    Prefer `drape` until every backend supports blend modes: asking a Vulkan
    render for a soft-light layer currently loses the device.
    """
    dem_zoom = min(zoom, dem_zoom_max)
    heights = dem_window_heights(center_lat, center_lon, zoom, width, height, dem_zoom)
    cell_metres = (MERCATOR_EQUATOR_METRES_PER_PIXEL * math.cos(math.radians(center_lat)) /
                   (2.0 ** dem_zoom))
    shade = hillshade(heights, cell_metres, azimuth, sun_altitude, z_factor)

    if style == "shade":
        # Centred on the shading's own mean, so the drape adds terrain and not a
        # global brightness change: flat ground is neutral whatever the sun angle.
        deviation = shade - float(shade.mean())
        shaped = np.clip(0.5 + deviation * contrast, 0.0, 1.0)
        grey = np.rint(shaped * 255.0).astype(np.uint8)
        rgba = np.dstack([grey, grey, grey, np.full_like(grey, 255)])
        image = Image.fromarray(rgba, "RGBA")
    elif style == "drape":
        deviation = np.clip((shade - 0.5) * 2.0, -1.0, 1.0)
        alpha = np.clip((np.abs(deviation) ** 0.85) * strength, 0.0, 1.0)
        lit = deviation >= 0.0
        # Warm light, cool shadow, both a short step from the base tone rather
        # than white and black: at full strength the drape moves a slope around
        # the plate's own mid tone instead of replacing it with newsprint.
        light_tint = np.array([142.0, 136.0, 126.0], dtype=np.float32)
        shadow_tint = np.array([58.0, 66.0, 80.0], dtype=np.float32)
        rgba = np.zeros((height, width, 4), dtype=np.uint8)
        rgba[..., :3] = np.where(lit[..., None], light_tint, shadow_tint).astype(np.uint8)
        rgba[..., 3] = np.rint(alpha * 255.0).astype(np.uint8)
        image = Image.fromarray(rgba, "RGBA")
    else:
        raise ValueError(f"unknown relief style '{style}', expected 'shade' or 'drape'")

    if blur > 0.0:
        image = image.filter(ImageFilter.GaussianBlur(blur))

    x_min, y_min = window_origin(center_lat, center_lon, zoom, width, height)
    covering = int(np.ceil(max(1, width * 2.0 ** (dem_zoom - zoom)) / 256.0)) * \
        int(np.ceil(max(1, height * 2.0 ** (dem_zoom - zoom)) / 256.0))
    print(f"[Map] Terrain relief ({style}) from DEM zoom {dem_zoom} ({cell_metres:.1f} m/px), "
          f"~{covering} DEM tiles, sun {azimuth:.0f} deg at {sun_altitude:.0f} deg, "
          f"shade mean {shade.mean():.3f}")
    return GeoreferencedMap(image, center_lat, center_lon, zoom, x_min, y_min)


def build_georeferenced_basemap(center_lat: float, center_lon: float, zoom: int,
                                width: int = 1920, height: int = 1080,
                                provider: str = "esri_dark",
                                contrast: float = 1.15,
                                vignette: bool = True) -> GeoreferencedMap:
    """
    Build a pixel-perfect, georeferenced basemap canvas centered on (center_lat, center_lon) at zoom.
    """
    x_min, y_min = window_origin(center_lat, center_lon, zoom, width, height)
    cropped = stitch_window(provider, zoom, x_min, y_min, width, height)
    
    # Grading: high-contrast dark documentary look
    if provider == "esri_sat":
        # Desaturate satellite to moody dark-military/documentary tone
        enh_color = ImageEnhance.Color(cropped)
        cropped = enh_color.enhance(0.25) # 75% desaturated
        enh_bright = ImageEnhance.Brightness(cropped)
        cropped = enh_bright.enhance(0.65) # Darkened
        enh_con = ImageEnhance.Contrast(cropped)
        cropped = enh_con.enhance(1.4)
    elif contrast != 1.0:
        enh_con = ImageEnhance.Contrast(cropped)
        cropped = enh_con.enhance(contrast)
        
    if vignette:
        cropped = apply_vignette(cropped, intensity=0.72)
        
    return GeoreferencedMap(cropped, center_lat, center_lon, zoom, x_min, y_min)


def shade_plate_with_relief(plate: GeoreferencedMap, relief: GeoreferencedMap,
                            factor: float = 0.55) -> GeoreferencedMap:
    """Bake a DEM hillshade into an imagery plate.

    Multiplicative, symmetric about 1.0: the plate keeps every bit of its own
    structure and hue and only its luminance moves, which is how a printed relief
    map is built. It is the alternative to laying the hillshade on as a separate
    layer: an alpha layer can only interpolate the plate towards one tint (washing
    it), and a soft-light layer needs a blend mode the Vulkan backend does not
    survive. Baking needs no compositor feature at all.
    """
    if plate.image.size != relief.image.size:
        raise ValueError("relief must cover the same window and size as the plate: "
                         f"{plate.image.size} vs {relief.image.size}")
    if (plate.x_min, plate.y_min) != (relief.x_min, relief.y_min):
        raise ValueError("relief must share the plate's window origin")

    base = np.asarray(plate.image.convert("RGB"), dtype=np.float32) / 255.0
    shade = np.asarray(relief.image.convert("L"), dtype=np.float32) / 255.0
    gain = 1.0 + (shade - 0.5) * 2.0 * factor          # 0.5 = neutral, mean gain 1.0
    shaded = np.clip(base * gain[..., None], 0.0, 1.0)
    image = Image.fromarray(np.rint(shaded * 255.0).astype(np.uint8))
    print(f"[Map] Relief baked into the plate at strength {factor:.2f} "
          f"(gain {1.0 - factor:.2f}..{1.0 + factor:.2f}, mean "
          f"{float(gain.mean()):.3f})")
    return GeoreferencedMap(image, plate.center_lat, plate.center_lon, plate.zoom,
                            plate.x_min, plate.y_min)


# ---------------------------------------------------------------------------
# Self-test: the geodesy, offline and without a tile server.
# ---------------------------------------------------------------------------
#
# The Mercator rows below are the contract between this file and ChrononMotion3D.
# `ChrononMotion3D/tests/motion_geospatial.cpp` asserts the same four rows
# against its own `latLonToGlobalPixel`, so a drift on either side moves every
# marker off the pixel its geography maps to — and now breaks a build on both
# sides instead of a render on one. Nothing here touches the network: it is
# arithmetic over synthetic heightfields, so it runs on any host.

# (latitude, longitude, zoom, global pixel X, global pixel Y), as locked by
# ChrononMotion3D's tests/motion_geospatial.cpp.
MERCATOR_REFERENCE: tuple[tuple[float, float, int, float, float], ...] = (
    (41.890210, 12.492231, 14, 2242697.0401450666, 1558716.8764844453),  # Colosseo
    (29.86, -92.70, 8, 15892.48, 27067.93732941762),                     # Gulf coast
    (33.9598, -83.3768, 16, 4502967.491697778, 6704228.367772281),       # Athens, GA
    (0.0, 0.0, 0, 128.0, 128.0),                                         # the grid origin
)


class _PixelSize:
    """The only thing `GeoreferencedMap` reads off its image is the pixel size.

    Window placement is geometry, not raster, so stand in for the plate here and
    keep it assertable on a host with no Pillow installed.
    """

    def __init__(self, width: int, height: int) -> None:
        self.size = (width, height)


def self_test() -> int:
    """Assert the geodesy and the raster contracts this file is the reference
    for. Returns a process exit code, so it is usable as a build gate.

    The geodesy runs on the standard library alone; the raster block (bilinear
    sampling, Horn hillshade, relief baking) needs numpy and Pillow and says so
    instead of failing the gate when they are absent.
    """
    checks = 0
    failures: list[str] = []

    def check(condition: bool, message: str) -> None:
        nonlocal checks
        checks += 1
        if not condition:
            failures.append(message)

    def check_near(actual: float, expected: float, tolerance: float, message: str) -> None:
        check(abs(actual - expected) <= tolerance,
              f"{message} (expected {expected}, got {actual})")

    # 1. The Mercator rows ChrononMotion3D asserts against. The projection is
    #    the whole georeference: if this drifts, every marker moves.
    for lat, lon, zoom, expected_x, expected_y in MERCATOR_REFERENCE:
        x, y = latlon_to_mercator_px(lat, lon, zoom)
        check_near(x, expected_x, 1e-6, f"global pixel X for ({lat}, {lon}) at zoom {zoom}")
        check_near(y, expected_y, 1e-6, f"global pixel Y for ({lat}, {lon}) at zoom {zoom}")

    # 2. And it is invertible, or a plate could not be georeferenced at all.
    for lat, lon, zoom, x, y in MERCATOR_REFERENCE:
        back_lat, back_lon = mercator_px_to_latlon(x, y, zoom)
        check_near(back_lat, lat, 1e-9, f"round-trip latitude for ({lat}, {lon}) at zoom {zoom}")
        check_near(back_lon, lon, 1e-9, f"round-trip longitude for ({lat}, {lon}) at zoom {zoom}")

    # 3. The domain edges are clamped, not wrapped or rejected: a window is
    #    allowed to run past the pole and the antimeridian.
    check(latlon_to_mercator_px(89.0, 0.0, 4) == latlon_to_mercator_px(85.05112878, 0.0, 4),
          "a latitude past the pole clamps to the Mercator limit")
    check(latlon_to_mercator_px(0.0, 200.0, 4) == latlon_to_mercator_px(0.0, 180.0, 4),
          "a longitude past the antimeridian clamps to 180")
    check_near(MERCATOR_EQUATOR_METRES_PER_PIXEL, 2.0 * math.pi * 6378137.0 / 256.0, 1e-4,
               "the equator resolution follows from the WGS84 semi-major axis")

    # 4. A window centred on a point puts that point on the middle pixel. This
    #    is the assumption every marker placement rests on.
    x_min, y_min = window_origin(29.86, -92.70, 8, 1920, 1080)
    centre_x, centre_y = latlon_to_mercator_px(29.86, -92.70, 8)
    check_near(x_min + 1920 / 2.0, centre_x, 1e-9, "a centred window straddles its centre in X")
    check_near(y_min + 1080 / 2.0, centre_y, 1e-9, "a centred window straddles its centre in Y")
    centred = GeoreferencedMap(_PixelSize(1920, 1080), 29.86, -92.70, 8, x_min, y_min)
    screen = centred.to_screen(29.86, -92.70)
    check_near(screen[0], 960.0, 0.01, "the window centre is the middle raster column")
    check_near(screen[1], 540.0, 0.01, "the window centre is the middle raster row")

    # 5. The dynamic multi-resolution tile pyramid is the authoritative map
    #    engine. Its geodesy must match this file's to the last bit, or a
    #    flyover and a basemap plate would disagree about where the ground is.
    if DynamicTilePyramid is None:
        print("SKIP: the dynamic tile pyramid engine could not be imported")
    else:
        for lat, lon, zoom, expected_x, expected_y in MERCATOR_REFERENCE:
            px_x, px_y = latlon_to_global_px(lat, lon, zoom)
            check_near(px_x, expected_x, 1e-6,
                       f"pyramid global pixel X for ({lat}, {lon}) at zoom {zoom}")
            check_near(px_y, expected_y, 1e-6,
                       f"pyramid global pixel Y for ({lat}, {lon}) at zoom {zoom}")

        # The optical-altitude model round-trips at the Colosseum's latitude:
        # this is the continuity contract the flyover's LOD selection rides on.
        anchor_lat = 41.890210
        alt_km = compute_altitude_km(12.0, anchor_lat)
        anchor_rad = math.radians(anchor_lat)
        zoom_back = math.log2(MERCATOR_EQUATOR_METRES_PER_PIXEL * math.cos(anchor_rad)
                              / (alt_km * 1000.0 / (1080.0 * 0.95)))
        check_near(zoom_back, 12.0, 1e-9,
                   "compute_altitude_km inverts to the zoom that produced it")

        # Static providers are shared between the plate and pyramid tools.
        # Wayback is intentionally pyramid-only because each request requires
        # an explicit immutable release id and has a separate cache namespace.
        shared_pyramid_providers = set(PYRAMID_TILE_URLS) - {"esri_wayback"}
        check(shared_pyramid_providers <= set(TILE_URLS),
              "shared pyramid providers are advertised by the plate tool")

    if _RASTER_IMPORT_ERROR is not None:
        # The check count and the disclosure both go to stdout, so a gate run
        # that silently lost the raster half is visible in its own log.
        print(f"SKIP: {_RASTER_IMPORT_ERROR}; the bilinear, hillshade and "
              f"relief-bake assertions need numpy and Pillow")
        print(f"all {checks} cartography geodesy assertions passed (raster half skipped)")
        return 0

    # 5. Bilinear sampling, which is how the DEM meets the plate's pixel grid.
    ramp = np.array([[0.0, 1.0], [2.0, 3.0]])
    check_near(float(bilinear_sample(ramp, np.array([0.5]), np.array([0.5]))[0, 0]), 1.5,
               1e-6, "bilinear sampling interpolates the centre of a quad")
    check_near(float(bilinear_sample(ramp, np.array([0.0]), np.array([0.0]))[0, 0]), 0.0,
               1e-6, "bilinear sampling hits an exact node")

    # 6. Horn (1981) shading, and the one convention it has to get right: the
    #    sun azimuth names the direction the light comes *from*, so a slope
    #    rising towards the north-west is lit by the default north-west sun and
    #    one rising towards the south-east is shadowed. Flat ground returns
    #    sin(altitude) and nothing else.
    grid_y, grid_x = np.meshgrid(np.arange(24), np.arange(24), indexing="ij")
    flat = np.zeros((24, 24))
    flat_shade = hillshade(flat, 1.0, 315.0, 45.0, 1.6)
    check_near(float(flat_shade.min()), math.sin(math.radians(45.0)), 1e-9,
               "flat ground is shaded by the sun altitude alone")
    check_near(float(flat_shade.max()), float(flat_shade.min()), 1e-9,
               "flat ground is shaded uniformly")
    nw_high = -(grid_x + grid_y) * 0.1
    se_high = (grid_x + grid_y) * 0.1
    lit = float(hillshade(nw_high, 1.0, 315.0, 45.0, 1.6).mean())
    shadowed = float(hillshade(se_high, 1.0, 315.0, 45.0, 1.6).mean())
    neutral = float(flat_shade.mean())
    check(lit > neutral > shadowed,
          "a north-west sun lights a north-west-facing slope and shadows the opposite one "
          f"({lit:.4f} > {neutral:.4f} > {shadowed:.4f})")
    # (Horn's edge padding halves the gradient at the border, so a sloped field
    # is only uniform away from the edges — flat ground has no such caveat.)
    zenith = hillshade(flat, 1.0, 315.0, 90.0, 1.6)
    check_near(float(zenith.min()), 1.0, 1e-9, "a sun at the zenith shades flat ground fully")
    check(float(hillshade(nw_high, 1.0, 315.0, 45.0, 1.6).min()) >= 0.0 and
          float(hillshade(se_high, 1.0, 315.0, 45.0, 1.6).max()) <= 1.0,
          "the shading stays inside [0, 1] where an image format needs it")

    # 7. Baking relief, which is the path a plan actually uses: mid grey is the
    #    neutral gain, and a zero factor leaves the plate untouched.
    plate = GeoreferencedMap(Image.new("RGB", (8, 8), (100, 100, 100)), 0.0, 0.0, 4, 0.0, 0.0)

    def bake(grey: int, factor: float) -> tuple[int, int, int]:
        relief = GeoreferencedMap(Image.new("L", (8, 8), grey), 0.0, 0.0, 4, 0.0, 0.0)
        return shade_plate_with_relief(plate, relief, factor).image.getpixel((0, 0))

    check(bake(128, 0.55) == (100, 100, 100), "a mid-grey hillshade is a gain of one")
    check(bake(0, 0.55) == (45, 45, 45) and bake(255, 0.55) == (155, 155, 155),
          "the gain spans 1 - factor .. 1 + factor around the plate's own tone")
    check(bake(255, 0.0) == (100, 100, 100), "a zero relief factor leaves the plate untouched")

    for bad_relief, message in (
        (GeoreferencedMap(Image.new("L", (4, 4), 128), 0.0, 0.0, 4, 0.0, 0.0),
         "a relief of another size is rejected"),
        (GeoreferencedMap(Image.new("L", (8, 8), 128), 0.0, 0.0, 4, 256.0, 0.0),
         "a relief of another window is rejected"),
    ):
        try:
            shade_plate_with_relief(plate, bad_relief, 0.55)
            check(False, message)
        except ValueError:
            check(True, message)

    for failure in failures:
        print(f"FAIL: {failure}")
    if failures:
        print(f"\n{len(failures)} of {checks} cartography assertion(s) failed")
        return 1
    print(f"all {checks} cartography assertions passed (Web Mercator, clamp, window "
          f"placement, bilinear, Horn hillshade, relief bake)")
    return 0


def main(argv: list[str] | None = None) -> int:
    """Render the plates a map plan consumes: an imagery basemap, and the DEM
    relief for the same window that gets draped on it."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[1],
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--self-test", action="store_true",
                        help="assert the geodesy and raster contracts, offline, and exit")
    subparsers = parser.add_subparsers(dest="command")

    def add_window(sub):
        sub.add_argument("lat", type=float, help="window centre latitude (WGS84)")
        sub.add_argument("lon", type=float, help="window centre longitude (WGS84)")
        sub.add_argument("zoom", type=int)
        sub.add_argument("out", help="output file name inside Chronon3d/assets/maps")
        sub.add_argument("--width", type=int, default=1920)
        sub.add_argument("--height", type=int, default=1080)

    basemap = subparsers.add_parser("basemap", help="stitch and grade an imagery basemap")
    add_window(basemap)
    basemap.add_argument("--provider", default="esri_sat", choices=sorted(TILE_URLS))
    basemap.add_argument("--contrast", type=float, default=1.15)
    basemap.add_argument("--no-vignette", action="store_true")
    basemap.add_argument("--relief", action="store_true",
                         help="bake a DEM hillshade for this same window into the plate")
    basemap.add_argument("--relief-factor", type=float, default=0.55,
                         help="shading gain; 0 leaves the plate untouched")

    relief = subparsers.add_parser("relief", help="DEM hillshade for the same window")
    add_window(relief)
    relief.add_argument("--style", default="drape", choices=("shade", "drape"),
                        help="drape (alpha overlay, any backend) or shade (soft-light plate)")
    relief.add_argument("--dem-zoom-max", type=int, default=13)
    relief.add_argument("--azimuth", type=float, default=315.0)
    relief.add_argument("--sun-altitude", type=float, default=45.0)
    relief.add_argument("--z-factor", type=float, default=1.6)
    relief.add_argument("--contrast", type=float, default=0.8,
                        help="shading gain around the neutral 0.5 of a soft-light blend")
    relief.add_argument("--strength", type=float, default=0.9, help="drape style only")

    # Authoritative Dynamic Multi-Resolution Pyramid Engine
    pyr = subparsers.add_parser("pyramid", help="fetch and render authoritative dynamic multi-resolution tile pyramid")
    pyr.add_argument("lat", type=float, help="target latitude (WGS84)")
    pyr.add_argument("lon", type=float, help="target longitude (WGS84)")
    pyr.add_argument("--min-zoom", type=int, default=5)
    pyr.add_argument("--max-zoom", type=int, default=18)
    pyr.add_argument("--provider", default="esri_sat",
                     choices=sorted(PYRAMID_TILE_URLS),
                     help="imagery providers the pyramid engine actually serves")

    args = parser.parse_args(argv)
    if args.self_test:
        return self_test()
    if args.command is None:
        parser.error("a command is required (basemap, relief, pyramid) unless --self-test is given")
    if _RASTER_IMPORT_ERROR is not None:
        parser.error(f"the {args.command} command stitches and grades rasters: "
                     f"{_RASTER_IMPORT_ERROR} (pip install numpy pillow)")

    if args.command == "pyramid":
        if DynamicTilePyramid is None:
            parser.error("DynamicTilePyramid module could not be imported.")
        pyramid = DynamicTilePyramid(provider=args.provider)
        pyramid.prefetch_pyramid(args.lat, args.lon, min_zoom=args.min_zoom, max_zoom=args.max_zoom)
        print(f"[DynamicTilePyramid] Successfully cached multi-resolution pyramid for ({args.lat}, {args.lon}) z={args.min_zoom}..{args.max_zoom}")
        return 0

    if args.command == "basemap":
        plate = build_georeferenced_basemap(args.lat, args.lon, args.zoom, args.width,
                                            args.height, provider=args.provider,
                                            contrast=args.contrast,
                                            vignette=not args.no_vignette)
        if args.relief:
            relief = build_terrain_relief(args.lat, args.lon, args.zoom, args.width,
                                          args.height, style="shade",
                                          blur=1.2)
            plate = shade_plate_with_relief(plate, relief, args.relief_factor)
    else:
        plate = build_terrain_relief(args.lat, args.lon, args.zoom, args.width, args.height,
                                     dem_zoom_max=args.dem_zoom_max, azimuth=args.azimuth,
                                     sun_altitude=args.sun_altitude, z_factor=args.z_factor,
                                     strength=args.strength, style=args.style,
                                     contrast=args.contrast)
    print(plate.save(args.out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
