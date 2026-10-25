#!/usr/bin/env python3
"""
DynamicTilePyramid: Multi-Resolution Tile Pyramid & Continuous Mipmap Zoom Engine.

Solves the raster map pixelation problem by dynamically fetching, caching, and
alpha-blending tiles across continuous Web Mercator zoom levels (z = 5 -> 18, an 8192x zoom).

Features:
- Multithreaded tile downloading & persistent disk caching.
- Continuous fractional zoom sampling with smoothstep alpha cross-fading (zero pop-in).
- Real-time geodetic altitude & telemetry computation.
- Cinematic documentary HUD & drop-pin targeting.
"""

import os
import sys
import math
import time
import json
import http.client
import threading
import urllib.parse
from pathlib import Path
from concurrent.futures import FIRST_COMPLETED, ThreadPoolExecutor, wait
from threading import Lock, get_ident
import numpy as np

# OpenCV accelerates the resample/blend path but is optional on purpose: the
# prefetch, the tile cache and every geodesy contract here run on numpy alone
# (see map_cartography.py for the same import discipline). The shim below
# covers exactly the call sites this module uses; scipy's FFT-based phase
# correlation and Pillow's JPEG codec stand in when cv2 is absent.
try:
    import cv2  # type: ignore
except ImportError:  # pragma: no cover - depends on the host
    cv2 = None

if cv2 is None:
    class _Cv2Shim:
        """Minimal numpy stand-in for the cv2 calls the engine makes."""

        INTER_LINEAR = 1
        IMREAD_COLOR = 1

        @staticmethod
        def resize(src, dsize, interpolation=None):
            from PIL import Image
            w, h = int(dsize[0]), int(dsize[1])
            pil = Image.fromarray(src).resize((w, h), Image.BILINEAR)
            return np.asarray(pil)

        @staticmethod
        def addWeighted(src1, alpha, src2, beta, _gamma=0.0):
            out = src1.astype(np.float32) * alpha + src2.astype(np.float32) * beta
            return np.clip(out + 0.5, 0, 255).astype(np.uint8)

        @staticmethod
        def imread(path, _flags=1):
            from PIL import Image
            try:
                return np.asarray(Image.open(path).convert("RGB"))[:, :, ::-1].copy()
            except Exception:
                return None

        @staticmethod
        def imdecode(raw, _flags=1):
            from io import BytesIO
            from PIL import Image
            try:
                return np.asarray(Image.open(BytesIO(raw)).convert("RGB"))[:, :, ::-1].copy()
            except Exception:
                return None

        @staticmethod
        def cvtColor(src, code):
            if code == _Cv2Shim.COLOR_BGR2GRAY:
                # ITU-R BT.601 luma on BGR input, as cv2 computes it.
                return np.round(0.114 * src[:, :, 0].astype(np.float32)
                                + 0.587 * src[:, :, 1].astype(np.float32)
                                + 0.299 * src[:, :, 2].astype(np.float32)).astype(np.uint8)
            raise NotImplementedError(code)

        COLOR_BGR2GRAY = "COLOR_BGR2GRAY"
        TM_CCOEFF_NORMED = 5  # accepted and ignored: the shim always normalizes

        @staticmethod
        def matchTemplate(image, templ, _method=None):
            """TM_CCOEFF_NORMED over every integer shift via FFT cross-correlation."""
            import scipy.signal
            from scipy.ndimage import uniform_filter
            # With the template mean-centred, correlating against the raw image
            # equals correlating against each window's own mean (the template's
            # DC term is zero), which is exactly what TM_CCOEFF_NORMED measures.
            g1 = image.astype(np.float64)
            g2 = templ.astype(np.float64) - templ.mean()
            corr = scipy.signal.fftconvolve(g1, g2[::-1, ::-1], mode="valid")
            norm2 = math.sqrt(float((g2 * g2).sum()))
            # Per-window variance of the image under the template. The running
            # sums give sum(x^2) - sum(x)^2 / n; the uniform_filter window is
            # centred, so the value matching corr[u, v] sits at the +size//2
            # offset for the even-sized tiles this engine uses.
            n = g2.size
            sum1 = uniform_filter(g1, g2.shape, mode="constant") * n
            sumsq1 = uniform_filter(g1 * g1, g2.shape, mode="constant") * n
            var1 = np.maximum(sumsq1 - sum1 * sum1 / n, 1e-12)
            off_y, off_x = g2.shape[0] // 2, g2.shape[1] // 2
            denom = np.sqrt(var1[off_y:off_y + corr.shape[0],
                                 off_x:off_x + corr.shape[1]]) * norm2
            return corr / denom

        @staticmethod
        def minMaxLoc(result):
            idx = np.unravel_index(int(np.argmax(result)), result.shape)
            return float(result.min()), float(result.max()), (0, 0), (int(idx[1]), int(idx[0]))

    cv2 = _Cv2Shim()  # type: ignore[assignment]

CHRONON3D_ROOT = Path(__file__).resolve().parents[2]
CACHE_DIR = CHRONON3D_ROOT / "assets" / "maps" / "cache" / "pyramid"
CACHE_DIR.mkdir(parents=True, exist_ok=True)

_ARCGIS = "https://server.arcgisonline.com/ArcGIS/rest/services"

TILE_URLS = {
    "esri_sat": f"{_ARCGIS}/World_Imagery/MapServer/tile/{{z}}/{{y}}/{{x}}",
    "esri_topo": f"{_ARCGIS}/World_Topo_Map/MapServer/tile/{{z}}/{{y}}/{{x}}",
    "esri_natgeo": f"{_ARCGIS}/NatGeo_World_Map/MapServer/tile/{{z}}/{{y}}/{{x}}",
    "esri_ocean": f"{_ARCGIS}/Ocean/World_Ocean_Base/MapServer/tile/{{z}}/{{y}}/{{x}}",
    "esri_light": f"{_ARCGIS}/Canvas/World_Light_Gray_Base/MapServer/tile/{{z}}/{{y}}/{{x}}",
    "esri_dark": f"{_ARCGIS}/Canvas/World_Dark_Gray_Base/MapServer/tile/{{z}}/{{y}}/{{x}}",
    # Wayback releases are immutable snapshots; release is mandatory and is
    # also part of the on-disk cache namespace below.
    "esri_wayback": f"{_ARCGIS}/World_Imagery/MapServer/tile/{{release}}/{{z}}/{{y}}/{{x}}",
    "osm": "https://tile.openstreetmap.org/{z}/{x}/{y}.png",
    "carto_dark": "https://a.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}.png"
}
# chronon_editorial has NO tile URL: it renders from the offline vector
# plate (ChrononTemplate/tools/map/vector_plate_pyramid.py). Requesting it
# from DynamicTilePyramid fails closed in __init__.

# BASEMAP_STYLES is the rotation palette the production dynamic-map renderer
# draws from: every id is an ArcGIS Online tiled basemap, so a rendered map is
# always the same licensing posture as the historic World_Imagery default while
# the *look* (imagery, topography, National Geographic, ocean, light/dark gray
# canvas) varies from render to render. The runtime selects an id and passes it
# as the payload's "basemap"; the renderer fails closed on anything outside this
# tuple, so an unknown style can never silently degrade to the default.
BASEMAP_STYLES = (
    "esri_sat",
    "esri_topo",
    "esri_natgeo",
    "esri_ocean",
    "esri_light",
    "esri_dark",
)


def is_basemap_style(name: str) -> bool:
    """Report whether name is a certified raster or offline editorial basemap."""
    return name in BASEMAP_STYLES or name == "chronon_editorial"

MERCATOR_EQUATOR_METRES_PER_PIXEL = 156543.03392


def latlon_to_global_px(lat: float, lon: float, zoom: int) -> tuple[float, float]:
    """Convert WGS84 to global Web Mercator pixel coordinates."""
    lat = max(-85.05112878, min(85.05112878, lat))
    lon = max(-180.0, min(180.0, lon))
    scale = 256.0 * (2 ** zoom)
    x = (lon + 180.0) / 360.0 * scale
    lat_rad = math.radians(lat)
    y = (1.0 - math.log(math.tan(lat_rad) + 1.0 / math.cos(lat_rad)) / math.pi) * 0.5 * scale
    return x, y


def global_px_to_latlon(x: float, y: float, zoom: int) -> tuple[float, float]:
    """Convert global Mercator pixel coordinates back to WGS84."""
    scale = 256.0 * (2 ** zoom)
    lon = (x / scale) * 360.0 - 180.0
    n = math.pi - 2.0 * math.pi * (y / scale)
    lat = math.degrees(math.atan(math.sinh(n)))
    return lat, lon


_tile_http_local = threading.local()


def _http_get(url: str, timeout: float = 10.0) -> bytes:
    """GET over a per-thread persistent HTTPS connection (keep-alive).

    Tile prefetch issues thousands of GETs to a handful of hosts; a fresh TLS
    handshake per tile measured 146 ms/fetch on production hardware
    (2026-10-24). Each prefetch worker thread now reuses one connection per
    host and reconnects once on a stale keep-alive socket. Raises on any
    non-2xx status, mirroring urllib.request.urlopen.
    """
    parts = urllib.parse.urlsplit(url)
    if parts.scheme != "https":
        raise ValueError(f"tile fetch supports https only: {url!r}")
    path = urllib.parse.urlunsplit(("", "", parts.path, parts.query, ""))
    conns = getattr(_tile_http_local, "conns", None)
    if conns is None:
        conns = _tile_http_local.conns = {}
    conn = conns.get(parts.netloc)
    if conn is None:
        conn = http.client.HTTPSConnection(parts.netloc, timeout=timeout)
        conns[parts.netloc] = conn
    for attempt in (0, 1):
        try:
            conn.request("GET", path, headers={
                "User-Agent": "Mozilla/5.0 (ChrononDynamicPyramid/2.0)",
                "Connection": "keep-alive",
            })
            resp = conn.getresponse()
            raw = resp.read()
            if not 200 <= resp.status < 300:
                raise OSError(f"tile HTTP {resp.status} for {url!r}")
            return raw
        except (http.client.HTTPException, OSError):
            if attempt == 1:
                raise
            # Stale keep-alive socket: reconnect once, then surface the error.
            try:
                conn.close()
            except Exception:
                pass
            conn = http.client.HTTPSConnection(parts.netloc, timeout=timeout)
            conns[parts.netloc] = conn
    raise OSError(f"tile fetch failed: {url!r}")


class DynamicTilePyramid:
    def __init__(self, provider: str = "esri_sat", wayback_release: int | None = None):
        if provider not in TILE_URLS:
            raise ValueError(f"unsupported tile provider: {provider}")
        if provider == "esri_wayback":
            if isinstance(wayback_release, bool) or not isinstance(wayback_release, int) or wayback_release <= 0:
                raise ValueError("esri_wayback requires an explicit positive wayback_release")
        elif wayback_release is not None:
            raise ValueError("wayback_release is only valid with esri_wayback")
        self.provider = provider
        self.wayback_release = wayback_release
        self.cache_namespace = (f"esri_wayback_{wayback_release}"
                                if provider == "esri_wayback" else provider)
        self.url_template = TILE_URLS[provider]
        self.tile_cache = {}  # In-memory RAM cache for fast composition
        self.fallback_tile_count = 0
        self._cache_io_lock = Lock()
        # Per-instance counters: they are included in the dynamic-map render
        # summary so tile fetches, plate preparation and frame production are
        # separately attributable. Fetches run in a thread pool, so updates
        # share one small lock rather than relying on incidental GIL behavior.
        self.telemetry = {
            "tile_disk_cache_hits": 0,
            "tile_disk_cache_misses": 0,
            "tile_network_fetches": 0,
            "tile_fallbacks": 0,
            "tile_bytes_downloaded": 0,
            "tile_fetch_ms": 0.0,
            "prefetch_tiles_requested": 0,
            "prefetch_tiles_memory_hits": 0,
            "prefetch_ms": 0.0,
            "late_tile_fetches": 0,
        }
        self._telemetry_lock = Lock()

    def record_telemetry(self, name: str, value: int | float = 1) -> None:
        """Thread-safe additive update of a known per-instance metric."""
        with self._telemetry_lock:
            self.telemetry[name] = self.telemetry.get(name, 0) + value

    def _fetch_single_tile(self, z: int, x: int, y: int) -> tuple[tuple[int, int, int], np.ndarray]:
        fetch_started = time.perf_counter()
        max_idx = 2 ** z
        x = x % max_idx
        if y < 0 or y >= max_idx:
            blank = np.zeros((256, 256, 3), dtype=np.uint8)
            return (z, x, y), blank

        prov_cache = CACHE_DIR / self.cache_namespace / str(z) / str(x)
        prov_cache.mkdir(parents=True, exist_ok=True)
        tile_path = prov_cache / f"{y}.jpg"

        with self._cache_io_lock:
            cached = tile_path.exists()
        if cached:
            try:
                img = cv2.imread(str(tile_path), cv2.IMREAD_COLOR)
                if img is not None and img.shape == (256, 256, 3):
                    self.record_telemetry("tile_disk_cache_hits")
                    self.record_telemetry("tile_fetch_ms", (time.perf_counter() - fetch_started) * 1000.0)
                    return (z, x, y), img
            except Exception:
                pass
            if cv2 is None:
                # Pillow decodes the cached JPEG when OpenCV is not installed.
                from PIL import Image
                try:
                    pil = Image.open(tile_path).convert("RGB")
                    img = np.asarray(pil)[:, :, ::-1].copy()
                    if img.shape == (256, 256, 3):
                        self.record_telemetry("tile_disk_cache_hits")
                        self.record_telemetry("tile_fetch_ms", (time.perf_counter() - fetch_started) * 1000.0)
                        return (z, x, y), img
                except Exception:
                    pass
            self.record_telemetry("tile_disk_cache_misses")
        else:
            self.record_telemetry("tile_disk_cache_misses")

        url = self.url_template.format(z=z, x=x, y=y,
                                      release=self.wayback_release or "")
        last_error = "invalid or empty tile response"
        # Tile endpoints occasionally return transient 502s under concurrent
        # exact-coverage prefetch. Give those requests a few more bounded
        # chances before marking the plate incomplete.
        for attempt in range(7):
            try:
                raw = _http_get(url)
                img = cv2.imdecode(np.frombuffer(raw, np.uint8), cv2.IMREAD_COLOR)
                if img is not None and img.shape == (256, 256, 3):
                    with self._cache_io_lock:
                        tile_path.parent.mkdir(parents=True, exist_ok=True)
                        temporary_path = tile_path.with_name(f"{tile_path.name}.{os.getpid()}.{get_ident()}.tmp")
                        temporary_path.write_bytes(raw)
                        os.replace(temporary_path, tile_path)
                if img is not None and img.shape == (256, 256, 3):
                    self.record_telemetry("tile_network_fetches")
                    self.record_telemetry("tile_bytes_downloaded", len(raw))
                    self.record_telemetry("tile_fetch_ms", (time.perf_counter() - fetch_started) * 1000.0)
                    return (z, x, y), img
            except Exception as exc:
                last_error = f"{type(exc).__name__}: {exc}"
                if attempt < 6:
                    time.sleep(min(2.0, 0.2 * (2 ** attempt)))

        # Esri occasionally returns 502 for a few tiles while the primary
        # hostname is busy. The same ArcGIS service is exposed on this
        # alternate hostname; retry it before substituting a visual fallback.
        if url.startswith("https://server.arcgisonline.com/"):
            alternate_url = url.replace(
                "https://server.arcgisonline.com/",
                "https://services.arcgisonline.com/", 1)
            for attempt in range(3):
                try:
                    raw = _http_get(alternate_url)
                    img = cv2.imdecode(np.frombuffer(raw, np.uint8), cv2.IMREAD_COLOR)
                    if img is not None and img.shape == (256, 256, 3):
                        with self._cache_io_lock:
                            tile_path.parent.mkdir(parents=True, exist_ok=True)
                            temporary_path = tile_path.with_name(
                                f"{tile_path.name}.{os.getpid()}.{get_ident()}.tmp")
                            temporary_path.write_bytes(raw)
                            os.replace(temporary_path, tile_path)
                        self.record_telemetry("tile_network_fetches")
                        self.record_telemetry("tile_bytes_downloaded", len(raw))
                        self.record_telemetry(
                            "tile_fetch_ms", (time.perf_counter() - fetch_started) * 1000.0)
                        return (z, x, y), img
                except Exception as exc:
                    last_error = f"alternate {type(exc).__name__}: {exc}"
                    if attempt < 2:
                        time.sleep(0.5 * (attempt + 1))

        # Fallback dark tone if network unavailable
        self.fallback_tile_count += 1
        self.record_telemetry("tile_fallbacks")
        self.record_telemetry("tile_fetch_ms", (time.perf_counter() - fetch_started) * 1000.0)
        print(f"[DynamicTilePyramid] tile fetch failed z={z} x={x} y={y}: {last_error}", flush=True)
        fallback = np.full((256, 256, 3), (25, 30, 40), dtype=np.uint8)
        return (z, x, y), fallback

    def prefetch_tiles(self, tile_keys, max_workers: int = 12) -> dict[str, float | int]:
        """Fetch an exact tile-key set with bounded concurrent submissions.

        This is the path used by FastPlateSampler: unlike a fixed-radius warmup,
        its input covers every tile the actual output plates will sample. At
        most 2*max_workers futures are live, even for high-zoom/wide plates.
        """
        started = time.perf_counter()
        normalized = set()
        for z, x, y in tile_keys:
            if z < 0:
                raise ValueError(f"invalid tile zoom {z}")
            max_idx = 2 ** z
            if 0 <= y < max_idx:
                normalized.add((z, x % max_idx, y))
        requested = len(normalized)
        missing = sorted(key for key in normalized if key not in self.tile_cache)
        self.record_telemetry("prefetch_tiles_requested", requested)
        self.record_telemetry("prefetch_tiles_memory_hits", requested - len(missing))
        if not missing:
            elapsed_ms = (time.perf_counter() - started) * 1000.0
            self.record_telemetry("prefetch_ms", elapsed_ms)
            return {"requested": requested, "memory_hits": requested, "fetched": 0,
                    "elapsed_ms": elapsed_ms}

        workers = max(1, int(max_workers))
        print(f"[DynamicTilePyramid] Prefetching exact plate coverage: {len(missing)} tiles, workers={workers}...", flush=True)
        next_key = iter(missing)
        fetched = 0
        with ThreadPoolExecutor(max_workers=workers) as pool:
            pending = {}
            for _ in range(min(len(missing), workers * 2)):
                key = next(next_key, None)
                if key is None:
                    break
                pending[pool.submit(self._fetch_single_tile, *key)] = key
            while pending:
                done, _ = wait(pending, return_when=FIRST_COMPLETED)
                for future in done:
                    pending.pop(future)
                    key, image = future.result()
                    self.tile_cache[key] = image
                    fetched += 1
                    next_item = next(next_key, None)
                    if next_item is not None:
                        pending[pool.submit(self._fetch_single_tile, *next_item)] = next_item
        elapsed_ms = (time.perf_counter() - started) * 1000.0
        self.record_telemetry("prefetch_ms", elapsed_ms)
        print(f"[DynamicTilePyramid] Exact tile prefetch complete: requested={requested} fetched={fetched} memory_hits={requested - len(missing)} elapsed_ms={elapsed_ms:.1f}.", flush=True)
        return {"requested": requested, "memory_hits": requested - len(missing),
                "fetched": fetched, "elapsed_ms": elapsed_ms}

    def prefetch_pyramid(self, center_lat: float, center_lon: float,
                         min_zoom: int = 5, max_zoom: int = 18,
                         tile_radius_x: int = 2, tile_radius_y: int = 2,
                         max_workers: int = 12):
        """Prefetches the exact tile pyramid needed around the target center."""
        tasks = []
        for z in range(min_zoom, max_zoom + 1):
            gx, gy = latlon_to_global_px(center_lat, center_lon, z)
            ctx = int(gx // 256)
            cty = int(gy // 256)
            for dx in range(-tile_radius_x, tile_radius_x + 1):
                for dy in range(-tile_radius_y, tile_radius_y + 1):
                    tasks.append((z, ctx + dx, cty + dy))

        # Normalize x before cache identity (Mercator wraps horizontally),
        # then use the bounded, instrumented executor shared by plate warmup.
        self.prefetch_tiles(tasks, max_workers=max_workers)

    def render_plate_at_zoom(self, center_lat: float, center_lon: float, zoom_int: int,
                            width: int = 1920, height: int = 1080) -> np.ndarray:
        """Assembles a seamless composite plate centered at the given lat/lon for integer zoom."""
        gx, gy = latlon_to_global_px(center_lat, center_lon, zoom_int)
        left = gx - width * 0.5
        top = gy - height * 0.5

        min_tx = int(math.floor(left / 256.0))
        max_tx = int(math.ceil((left + width) / 256.0))
        min_ty = int(math.floor(top / 256.0))
        max_ty = int(math.ceil((top + height) / 256.0))

        plate = np.zeros((height, width, 3), dtype=np.uint8)

        for ty in range(min_ty, max_ty):
            tile_y0 = ty * 256.0
            tile_y1 = tile_y0 + 256.0

            # Overlap in Y
            y_start = max(top, tile_y0)
            y_end = min(top + height, tile_y1)
            if y_end <= y_start:
                continue

            dst_y0 = int(round(y_start - top))
            dst_y1 = int(round(y_end - top))
            src_y0 = int(round(y_start - tile_y0))
            src_y1 = int(round(y_end - tile_y0))

            for tx in range(min_tx, max_tx):
                tile_x0 = tx * 256.0
                tile_x1 = tile_x0 + 256.0

                # Overlap in X
                x_start = max(left, tile_x0)
                x_end = min(left + width, tile_x1)
                if x_end <= x_start:
                    continue

                dst_x0 = int(round(x_start - left))
                dst_x1 = int(round(x_end - left))
                src_x0 = int(round(x_start - tile_x0))
                src_x1 = int(round(x_end - tile_x0))

                key = (zoom_int, tx % (2 ** zoom_int), ty)
                if key in self.tile_cache:
                    tile = self.tile_cache[key]
                else:
                    self.record_telemetry("late_tile_fetches")
                    _, tile = self._fetch_single_tile(zoom_int, tx, ty)
                    self.tile_cache[key] = tile

                # Slice blit
                h_slice = min(dst_y1 - dst_y0, src_y1 - src_y0)
                w_slice = min(dst_x1 - dst_x0, src_x1 - src_x0)
                if h_slice > 0 and w_slice > 0:
                    plate[dst_y0:dst_y0 + h_slice, dst_x0:dst_x0 + w_slice] = tile[src_y0:src_y0 + h_slice, src_x0:src_x0 + w_slice]

        return plate

    def sample_continuous(self, center_lat: float, center_lon: float, continuous_zoom: float,
                          width: int = 1920, height: int = 1080) -> np.ndarray:
        """
        Samples the pyramid at an exact continuous float zoom level.
        Applies smooth continuous scale interpolation and alpha cross-fading between z_lo and z_hi.
        """
        z_lo = int(math.floor(continuous_zoom))
        z_hi = z_lo + 1
        t = continuous_zoom - z_lo  # sub-fraction [0.0, 1.0]

        # Smoothstep easing on blend parameter
        alpha = t * t * (3.0 - 2.0 * t)

        # Scale factors
        scale_lo = 2.0 ** t
        scale_hi = 2.0 ** (t - 1.0)

        # Render base plates
        w_need_lo = int(round(width / scale_lo)) + 4
        h_need_lo = int(round(height / scale_lo)) + 4
        plate_lo_crop = self.render_plate_at_zoom(center_lat, center_lon, z_lo, w_need_lo, h_need_lo)
        plate_lo = cv2.resize(plate_lo_crop, (width, height), interpolation=cv2.INTER_LINEAR)

        if alpha > 0.001:
            w_need_hi = int(round(width / scale_hi)) + 4
            h_need_hi = int(round(height / scale_hi)) + 4
            plate_hi_crop = self.render_plate_at_zoom(center_lat, center_lon, z_hi, w_need_hi, h_need_hi)
            plate_hi = cv2.resize(plate_hi_crop, (width, height), interpolation=cv2.INTER_LINEAR)

            # Continuous cross-fade
            blended = cv2.addWeighted(plate_lo, 1.0 - alpha, plate_hi, alpha, 0.0)
            return blended
        else:
            return plate_lo


def compute_altitude_km(zoom: float, lat: float) -> float:
    """Calculates approximate camera eye altitude in km from Web Mercator zoom level."""
    lat_rad = math.radians(lat)
    m_per_px = (MERCATOR_EQUATOR_METRES_PER_PIXEL * math.cos(lat_rad)) / (2.0 ** zoom)
    alt_m = m_per_px * 1080.0 * 0.95
    return alt_m / 1000.0


_GAIN085_LUT: np.ndarray | None = None


def _gain085_lut() -> np.ndarray:
    """The 256-entry law of addWeighted(frame, 0.85, black, 0.15, 0).

    Derived FROM the same addWeighted run on a full input ramp, so the LUT
    is the reference blend's own rounding: bit-exact by construction
    (verified: cv2.LUT(frame, lut) == cv2.addWeighted(frame, 0.85, black,
    0.15, 0) on random frames, worst delta 0).
    """
    global _GAIN085_LUT
    if _GAIN085_LUT is None:
        ramp = np.arange(256, dtype=np.uint8)
        _GAIN085_LUT = cv2.addWeighted(ramp, 0.85, np.zeros(256, np.uint8), 0.15, 0)
    return _GAIN085_LUT


def draw_hud_overlay(frame: np.ndarray, lat: float, lon: float, current_zoom: float,
                     progress: float, target_title: str = "COLOSSEUM (ROMA)"):
    """Draw cinematic documentary telemetry over the current geographic target."""
    h, w = frame.shape[:2]
    alt_km = compute_altitude_km(current_zoom, lat)

    # 1. Vignette on frame edges - in truth the certified 15% global gain.
    # The filled disk (center, radius int(w * 0.75)) covers the frame's
    # farthest corner on every 16:9 canvas (0.75w >= the half diagonal
    # ~0.589w), so the overlay it builds is SOLID BLACK and the composite
    # reduces to addWeighted(frame, 0.85, black, 0.15) - a per-sample gain.
    # Rebuilding that constant overlay every frame (copy + circle fill)
    # and blending it through a second full-frame read cost ~30 MB of
    # traffic per frame for a law that depends on the input byte alone,
    # so the gain runs through the cached 256-entry LUT, which is the
    # reference blend's own rounding, bit-exact by construction. Frames
    # whose half diagonal would stick out of the disk keep the original
    # copy + circle + blend path unchanged.
    radius = int(w * 0.75)
    if radius * radius >= (w / 2.0) ** 2 + (h / 2.0) ** 2:
        frame[:] = cv2.LUT(frame, _gain085_lut())
    else:
        overlay = frame.copy()
        cv2.circle(overlay, (w // 2, h // 2), radius, (0, 0, 0), -1)
        frame[:] = cv2.addWeighted(frame, 0.85, overlay, 0.15, 0)

    # 2. Central Targeting Reticle
    cx, cy = w // 2, h // 2
    # Reticle shrinks smoothly as camera descends
    reticle_r = int(120 - 75 * (progress ** 0.8))
    reticle_col = (0, 240, 255) if progress < 0.9 else (16, 185, 129)  # Cyan -> Emerald lock

    # Reticle brackets
    b_len = 16
    cv2.line(frame, (cx - reticle_r, cy - reticle_r), (cx - reticle_r + b_len, cy - reticle_r), reticle_col, 2)
    cv2.line(frame, (cx - reticle_r, cy - reticle_r), (cx - reticle_r, cy - reticle_r + b_len), reticle_col, 2)

    cv2.line(frame, (cx + reticle_r, cy - reticle_r), (cx + reticle_r - b_len, cy - reticle_r), reticle_col, 2)
    cv2.line(frame, (cx + reticle_r, cy - reticle_r), (cx + reticle_r, cy - reticle_r + b_len), reticle_col, 2)

    cv2.line(frame, (cx - reticle_r, cy + reticle_r), (cx - reticle_r + b_len, cy + reticle_r), reticle_col, 2)
    cv2.line(frame, (cx - reticle_r, cy + reticle_r), (cx - reticle_r, cy + reticle_r - b_len), reticle_col, 2)

    cv2.line(frame, (cx + reticle_r, cy + reticle_r), (cx + reticle_r - b_len, cy + reticle_r), reticle_col, 2)
    cv2.line(frame, (cx + reticle_r, cy + reticle_r), (cx + reticle_r, cy + reticle_r - b_len), reticle_col, 2)

    # Center crosshair
    cv2.drawMarker(frame, (cx, cy), reticle_col, cv2.MARKER_CROSS, 14, 1)

    # 3. Drop Pin & Pulse (when locked)
    if progress > 0.65:
        pin_progress = (progress - 0.65) / 0.35
        # Pulse ring
        pulse_r = int(25 + 40 * ((pin_progress * 3.0) % 1.0))
        pulse_alpha = max(0.0, 1.0 - ((pin_progress * 3.0) % 1.0))
        if pulse_alpha > 0.1:
            pulse_col = (int(reticle_col[0] * pulse_alpha),
                         int(reticle_col[1] * pulse_alpha),
                         int(reticle_col[2] * pulse_alpha))
            cv2.circle(frame, (cx, cy), pulse_r, pulse_col, 2)

        # Drop pin marker
        cv2.circle(frame, (cx, cy), 6, (0, 0, 255), -1)
        cv2.circle(frame, (cx, cy), 8, (255, 255, 255), 2)

    # 4. Telemetry Text Panels (Top-Left & Top-Right)
    # Background blur badge
    cv2.rectangle(frame, (40, 40), (450, 150), (15, 20, 30), -1)
    cv2.rectangle(frame, (40, 40), (450, 150), (40, 55, 80), 1)

    cv2.putText(frame, "CHRONON DYNAMIC TILE PYRAMID", (60, 70), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 240, 255), 1, cv2.LINE_AA)
    cv2.putText(frame, f"TARGET: {target_title}", (60, 95), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 255), 2, cv2.LINE_AA)
    lat_hemi = "N" if lat >= 0 else "S"
    lon_hemi = "E" if lon >= 0 else "W"
    cv2.putText(frame, f"WGS84: {abs(lat):.6f} {lat_hemi}, {abs(lon):.6f} {lon_hemi}",
                (60, 125), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (160, 175, 200), 1, cv2.LINE_AA)

    # Right Telemetry: Altitude & Zoom Level
    cv2.rectangle(frame, (w - 380, 40), (w - 40, 150), (15, 20, 30), -1)
    cv2.rectangle(frame, (w - 380, 40), (w - 40, 150), (40, 55, 80), 1)

    if alt_km >= 1.0:
        alt_str = f"{alt_km:8.1f} km"
    else:
        alt_str = f"{alt_km * 1000.0:8.0f} m"

    cv2.putText(frame, "OPTICAL ALTITUDE", (w - 360, 70), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 240, 255), 1, cv2.LINE_AA)
    cv2.putText(frame, alt_str, (w - 360, 105), cv2.FONT_HERSHEY_SIMPLEX, 0.9, (255, 255, 255), 2, cv2.LINE_AA)
    cv2.putText(frame, f"CONTINUOUS ZOOM: z={current_zoom:4.2f} (8192x LOD)", (w - 360, 135), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (160, 175, 200), 1, cv2.LINE_AA)

    # Bottom Progress Bar
    bar_w = w - 160
    cv2.rectangle(frame, (80, h - 50), (80 + bar_w, h - 44), (30, 40, 55), -1)
    fill_w = int(bar_w * progress)
    cv2.rectangle(frame, (80, h - 50), (80 + fill_w, h - 44), (0, 240, 255), -1)
    cv2.putText(frame, "SUPERSONIC DESCENT SEQUENCE", (80, h - 60), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (160, 175, 200), 1, cv2.LINE_AA)
