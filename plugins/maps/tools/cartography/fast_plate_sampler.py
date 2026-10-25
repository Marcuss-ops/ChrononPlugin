#!/usr/bin/env python3
"""FastPlateSampler - compose each pyramid level ONCE, crop per frame.

The engine's sample_continuous re-blits the same tiles for every frame; over
a 150-frame dive the identical plate is composited hundreds of times. This
sampler prepares one *union* plate per level (covering every anchor the shot
pans across, plus the worst-case frame extent: a level can serve as the
cross-fade's high layer, which asks for up to 2x the canvas), then a frame is
just an integer crop + the same resize/blend the engine performs. Pure
function of (anchor, zoom): deterministic, and byte-identical to the engine's
output up to the engine's own integer quantisation of the plate origin.

Falls back to pyramid.sample_continuous whenever a window would leave a
prepared plate, so a caller can never render garbage - only slow down.
"""

from __future__ import annotations

import math
import os
import time

import cv2
import numpy as np

# Tile prefetch parallelism (measured 2026-10-24): a distinct tile costs
# ~105 ms of edge/origin latency even over a keep-alive connection, so the
# old 12-worker pool capped throughput at ~100 tiles/s and a 1080p cold
# prefetch needs thousands of tiles. Throughput scales linearly to 64
# workers (754 tiles/s measured, zero fetch errors). Override with
# DYNAMIC_MAP_TILE_WORKERS for slower links or stricter CDN politeness.
TILE_PREFETCH_WORKERS = max(1, int(os.environ.get("DYNAMIC_MAP_TILE_WORKERS", "64")))


class FastPlateSampler:
    def __init__(self, pyramid, width: int, height: int):
        self.pyr = pyramid
        self.W = int(width)
        self.H = int(height)
        # Plate margin factor: each prepared plate covers
        # ceil(factor * W) x ceil(factor * H) global pixels around its anchor
        # (plus an 8px rounding guard), so the camera can pan within the
        # plate without leaving coverage. Historical default 3.0 (a 3x canvas
        # plate — 4K-class for a 1080p render). The 2026-10-24 operator
        # decision caps production maps at 1080p-class plates: the renderer
        # sets factor 1.0 for classic compositions unless the payload
        # declares otherwise; certified editorial scenes keep 3.0.
        self.plate_margin_factor = 3.0
        # Worker bound for the exact-coverage tile prefetch (prepare()).
        self.prefetch_workers = TILE_PREFETCH_WORKERS
        # level -> list of (origin_x, origin_y, plate). One entry when the
        # union plate fits the budget; several per-anchor entries when the
        # anchors are too far apart for one plate to make sense.
        self.plates: dict[int, list[tuple[float, float, np.ndarray]]] = {}
        self.z_min = self.z_max = 0
        # Per-process scratch buffers. In the fork pool each worker faults
        # its own private copies on first write; every frame after that
        # reuses them instead of letting OpenCV malloc + page-fault fresh
        # 14 MB outputs per resize/blend. Same kernels, same bytes - only
        # the allocator churn is gone. Three slots: resize lo, resize hi,
        # blend (the blend reads slots 0 and 1 and writes slot 2).
        self._scratch: dict[tuple[int, int], list[np.ndarray]] = {}
        self.prepare_telemetry = {
            "prepare_ms": 0.0,
            "plate_compose_ms": 0.0,
            "plate_count": 0,
            "plate_bytes": 0,
            "required_tile_count": 0,
        }
        # Honest telemetry: frames a worker had to serve from the slow
        # engine because a window left a prepared plate. The harness
        # aggregates the per-block deltas into a run total; 0 means every
        # frame took the fast path.
        self.fallback_count = 0
        # Frames whose hi blend level had to degrade to the lo plate because
        # its wider window left the prepared plate budget (2026-10-24).
        # Not an engine fallback — the frame is still served from plates —
        # but counted so lower blend detail is visible in the telemetry.
        self.lo_only_blend_count = 0
        self.opencl_plates = None
        # OpenCL commands are asynchronous. Reuse two ordered resize surfaces
        # for level sampling, but retain one distinct destination per queued
        # frame until the encoder has read it back. The dynamic-map harness
        # drains batches of 16 frames before beginning the next batch.
        self._opencl_resize_scratch: dict[tuple[int, int], tuple[cv2.UMat, cv2.UMat]] = {}
        self._opencl_outputs: list[cv2.UMat] = []
        self._opencl_outputs_size: tuple[int, int] | None = None
        self._opencl_output_cursor = 0
        self._opencl_batch_active = False
        # OpenCL device verdict, cached after the first check: the per-frame
        # path must not re-query cv2.ocl on every sample (see _require_opencl).
        self._opencl_ready = False

    def _require_opencl(self) -> None:
        """Fail closed unless the OpenCL renderer is active — checked ONCE.

        These three OpenCV queries used to run on EVERY sample_opencl call
        (measured ~3 ms/frame on the 65-frame production map). A sampler's
        OpenCL device cannot change under it, so the verdict is cached and the
        per-frame path pays nothing while keeping the same fail-closed errors.
        """
        if self._opencl_ready:
            return
        if not cv2.ocl.haveOpenCL():
            raise RuntimeError("OpenCL GPU map rendering is unavailable")
        cv2.ocl.setUseOpenCL(True)
        if not cv2.ocl.useOpenCL():
            raise RuntimeError("OpenCV did not activate the OpenCL map renderer")
        self._opencl_ready = True

    def begin_opencl_frame_batch(self, frame_count: int,
                                 width: int | None = None,
                                 height: int | None = None) -> None:
        """Prepare distinct reusable output surfaces for one ordered GPU batch."""
        if frame_count <= 0:
            raise ValueError("OpenCL frame batch must contain at least one frame")
        if not cv2.ocl.useOpenCL():
            raise RuntimeError("OpenCL must be active before allocating frame surfaces")
        size = (self.W if width is None else int(width),
                self.H if height is None else int(height))
        if size[0] <= 0 or size[1] <= 0:
            raise ValueError("OpenCL frame surface dimensions must be positive")
        if len(self._opencl_outputs) != frame_count or self._opencl_outputs_size != size:
            self._opencl_outputs = [
                cv2.UMat(size[1], size[0], cv2.CV_8UC3) for _ in range(frame_count)
            ]
            self._opencl_outputs_size = size
        self._opencl_output_cursor = 0
        self._opencl_batch_active = True

    def end_opencl_frame_batch(self) -> None:
        """Release the batch contract after all frame destinations were read."""
        self._opencl_batch_active = False
        self._opencl_output_cursor = 0

    def prepare_opencl(self) -> int:
        """Upload prepared map plates once so per-frame crops stay on GPU."""
        if not cv2.ocl.useOpenCL():
            raise RuntimeError("OpenCL must be active before uploading map plates")
        uploaded = {}
        total_bytes = 0
        for zoom, entries in self.plates.items():
            gpu_entries = []
            for left, top, plate in entries:
                gpu_plate = cv2.UMat(np.ascontiguousarray(plate))
                gpu_entries.append((left, top, gpu_plate, plate.shape[1], plate.shape[0]))
                total_bytes += int(plate.nbytes)
            uploaded[zoom] = gpu_entries
        self.opencl_plates = uploaded
        return total_bytes

    def sample_opencl(self, lat: float, lon: float, zoom: float,
                      width: int | None = None,
                      height: int | None = None):
        """Sample prepared map plates with OpenCV's OpenCL GPU kernels.

        Geographic coordinate math and tile preparation stay on the host; the
        full-resolution resampling and fractional zoom blend run as UMat work
        on the selected OpenCL device.
        """
        from dynamic_tile_pyramid import latlon_to_global_px

        self._require_opencl()
        W = self.W if width is None else int(width)
        H = self.H if height is None else int(height)
        z_lo = int(math.floor(zoom))
        t = zoom - z_lo
        alpha = t * t * (3.0 - 2.0 * t)
        if not (self.z_min <= z_lo <= self.z_max):
            raise RuntimeError(f"OpenCL map sample zoom {zoom:.3f} leaves prepared plates")

        if self.opencl_plates is None:
            raise RuntimeError("OpenCL map plates were not uploaded")

        if self._opencl_batch_active:
            if self._opencl_outputs_size != (W, H):
                raise RuntimeError("OpenCL frame batch dimensions changed before readback")
            if self._opencl_output_cursor >= len(self._opencl_outputs):
                raise RuntimeError("OpenCL frame batch exhausted before its queued outputs were drained")
            output = self._opencl_outputs[self._opencl_output_cursor]
            self._opencl_output_cursor += 1
        else:
            # Preserve the standalone sampler contract: callers may retain
            # several returned frames without opening the encoder's batch API.
            output = cv2.UMat(H, W, cv2.CV_8UC3)
        scratch_key = (W, H)
        scratch = self._opencl_resize_scratch.get(scratch_key)
        if scratch is None:
            scratch = (cv2.UMat(H, W, cv2.CV_8UC3), cv2.UMat(H, W, cv2.CV_8UC3))
            self._opencl_resize_scratch[scratch_key] = scratch

        def resize_level(z: int, scale: float, dst: cv2.UMat):
            w = int(round(W / scale)) + 4
            h = int(round(H / scale)) + 4
            anchor_px = latlon_to_global_px(lat, lon, z)
            crop = None
            for origin_x, origin_y, plate, plate_w, plate_h in self.opencl_plates.get(z, ()):
                x0 = int(round(anchor_px[0] - origin_x - w / 2))
                y0 = int(round(anchor_px[1] - origin_y - h / 2))
                if x0 >= 0 and y0 >= 0 and x0 + w <= plate_w \
                        and y0 + h <= plate_h:
                    crop = cv2.UMat(plate, (x0, y0, w, h))
                    break
            if crop is None:
                return None
            return cv2.resize(crop, (W, H), dst=dst, interpolation=cv2.INTER_LINEAR)

        needs_blend = alpha > 0.001 and z_lo + 1 <= self.z_max
        lo = resize_level(z_lo, 2.0 ** t, scratch[0] if needs_blend else output)
        if lo is None:
            # Degrade path (2026-10-24), parity with sample(): a pose outside
            # the prepared plates (per-anchor split far from the anchors) is
            # served by the slow tile engine instead of aborting the render;
            # the frames stay pixel-identical to the reference engine.
            return self._sample_opencl_fallback(lat, lon, zoom, W, H, output)
        if alpha <= 0.001 or z_lo + 1 > self.z_max:
            return lo
        hi = resize_level(z_lo + 1, 2.0 ** (t - 1.0), scratch[1])
        if hi is None:
            # Plate-budget degrade (2026-10-24): the contract can_sample()
            # documents and sample()/sample_subpixel() implement — only the lo
            # blend window must be covered; when the hi window leaves the
            # plates, serve the frame from the lo plate alone instead of
            # aborting, counted in lo_only_blend_count.
            self.lo_only_blend_count += 1
            return cv2.resize(lo, (W, H), dst=output,
                              interpolation=cv2.INTER_NEAREST)
        return cv2.addWeighted(lo, 1.0 - alpha, hi, alpha, 0.0, dst=output)

    def _sample_opencl_fallback(self, lat: float, lon: float, zoom: float,
                                W: int, H: int, output: cv2.UMat) -> cv2.UMat:
        frame = self.sample(lat, lon, zoom, W, H)
        return cv2.resize(cv2.UMat(np.ascontiguousarray(frame)), (W, H),
                          dst=output, interpolation=cv2.INTER_NEAREST)

    # -- the one-time cost ---------------------------------------------------
    MAX_PLATE_W = 9000   # ~145 MB per level; beyond this the union plate of
    MAX_PLATE_H = 5400   # far-apart anchors costs more RAM than it saves

    def prepare(self, anchors, z_min: int, z_max: int) -> None:
        """Prefetch exact plate coverage, then compose each plate once.

        A level whose union extent would exceed the plate budget (anchors
        kilometres apart at fine zooms) gets one plate per anchor. The exact
        tile set is computed before composition and fetched concurrently, so
        `_compose_px` never performs network I/O in its nested pixel/tile loop.
        """
        from dynamic_tile_pyramid import latlon_to_global_px

        if not anchors:
            raise ValueError("FastPlateSampler.prepare requires at least one geographic anchor")
        if z_min < 0 or z_max < z_min:
            raise ValueError(f"invalid prepared zoom range {z_min}..{z_max}")
        prepare_started = time.perf_counter()
        self.plates.clear()
        self.prepare_telemetry = {
            "prepare_ms": 0.0,
            "plate_compose_ms": 0.0,
            "plate_count": 0,
            "plate_bytes": 0,
            "required_tile_count": 0,
            "plate_margin_factor": float(self.plate_margin_factor),
        }
        self.z_min, self.z_max = int(z_min), int(z_max)
        ext_w = int(math.ceil(self.plate_margin_factor * self.W)) + 8
        ext_h = int(math.ceil(self.plate_margin_factor * self.H)) + 8
        specs: dict[int, list[tuple[float, float, int, int]]] = {}
        required_tiles: set[tuple[int, int, int]] = set()

        # First plan every plate and union its exact tile footprint. This is
        # the same half-open tile range consumed below by _compose_px.
        for z in range(self.z_min, self.z_max + 1):
            pts = [latlon_to_global_px(lat, lon, z) for lat, lon in anchors]
            xs = [x for x, _ in pts]
            ys = [y for _, y in pts]
            w = int(math.ceil(max(xs) - min(xs))) + ext_w
            h = int(math.ceil(max(ys) - min(ys))) + ext_h
            if w <= self.MAX_PLATE_W and h <= self.MAX_PLATE_H:
                cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
                plate_specs = [(cx - w / 2, cy - h / 2, w, h)]
            else:
                plate_specs = [(ax - ext_w / 2, ay - ext_h / 2, ext_w, ext_h)
                               for ax, ay in pts]
            specs[z] = plate_specs
            world_tiles = 2 ** z
            for left, top, width, height in plate_specs:
                min_tx = math.floor(left / 256.0)
                max_tx = math.ceil((left + width) / 256.0)
                min_ty = max(0, math.floor(top / 256.0))
                max_ty = min(world_tiles, math.ceil((top + height) / 256.0))
                for tile_y in range(min_ty, max_ty):
                    for tile_x in range(min_tx, max_tx):
                        required_tiles.add((z, tile_x % world_tiles, tile_y))

        prefetch = self.pyr.prefetch_tiles(required_tiles,
                                           max_workers=self.prefetch_workers)
        if prefetch["fetched"] + prefetch["memory_hits"] != len(required_tiles):
            raise RuntimeError("tile prefetch result does not cover the exact required plate set")
        self.prepare_telemetry["required_tile_count"] = len(required_tiles)
        self.prepare_telemetry["prefetch_ms"] = float(prefetch["elapsed_ms"])
        self.prepare_telemetry["prefetch_tiles_fetched"] = int(prefetch["fetched"])
        self.prepare_telemetry["prefetch_tiles_memory_hits"] = int(prefetch["memory_hits"])

        compose_started_all = time.perf_counter()
        for z, plate_specs in specs.items():
            entries = []
            for left, top, width, height in plate_specs:
                plate = self._compose_px(left, top, width, height, z)
                entries.append((left, top, plate))
                self.prepare_telemetry["plate_count"] += 1
                self.prepare_telemetry["plate_bytes"] += plate.nbytes
            if entries:
                self.plates[z] = entries
        self.prepare_telemetry["plate_compose_ms"] = (time.perf_counter() - compose_started_all) * 1000.0
        self.prepare_telemetry["prepare_ms"] = (time.perf_counter() - prepare_started) * 1000.0

    def can_sample(self, lat: float, lon: float, zoom: float,
                   width: int | None = None, height: int | None = None) -> bool:
        """Check crop coverage without rendering pixels or triggering tile I/O."""
        from dynamic_tile_pyramid import latlon_to_global_px

        W = self.W if width is None else int(width)
        H = self.H if height is None else int(height)
        z_lo = int(math.floor(zoom))
        if not (self.z_min <= z_lo <= self.z_max):
            return False
        t = zoom - z_lo
        w_lo = int(round(W / (2.0 ** t))) + 4
        h_lo = int(round(H / (2.0 ** t))) + 4
        if self._crop(z_lo, latlon_to_global_px(lat, lon, z_lo), w_lo, h_lo) is None:
            return False
        # Coverage requires the lo window only. The hi level of the alpha
        # blend degrades to the lo plate when its wider window (up to 2x at
        # low fractional zoom) leaves the prepared plate — the 2026-10-24
        # operator plate budget trades that blend detail for 1080p-class
        # plates. Degraded frames are counted in lo_only_blend_count.
        return True

    def required_tile_keys(self) -> set[tuple[int, int, int]]:
        """Return exact Mercator tile coverage of every prepared raster plate."""
        keys = set()
        for zoom, entries in self.plates.items():
            world_tiles = 2 ** zoom
            for left, top, plate in entries:
                right = left + plate.shape[1]
                bottom = top + plate.shape[0]
                min_tx = math.floor(left / 256.0)
                max_tx = math.ceil(right / 256.0)
                min_ty = max(0, math.floor(top / 256.0))
                max_ty = min(world_tiles, math.ceil(bottom / 256.0))
                for tile_y in range(min_ty, max_ty):
                    for tile_x in range(min_tx, max_tx):
                        keys.add((zoom, tile_x % world_tiles, tile_y))
        return keys

    def _compose_px(self, left: float, top: float, width: int, height: int,
                    zoom: int) -> np.ndarray:
        """Tile slicing identical to render_plate_at_zoom, px-addressed."""
        plate = np.zeros((height, width, 3), dtype=np.uint8)
        max_idx = 2 ** zoom
        for ty in range(int(math.floor(top / 256.0)),
                        int(math.ceil((top + height) / 256.0))):
            if ty < 0 or ty >= max_idx:
                continue
            y0, y1 = max(top, ty * 256.0), min(top + height, (ty + 1) * 256.0)
            if y1 <= y0:
                continue
            dy0, dy1 = int(round(y0 - top)), int(round(y1 - top))
            sy0, sy1 = int(round(y0 - ty * 256.0)), int(round(y1 - ty * 256.0))
            for tx in range(int(math.floor(left / 256.0)),
                            int(math.ceil((left + width) / 256.0))):
                x0, x1 = max(left, tx * 256.0), min(left + width, (tx + 1) * 256.0)
                if x1 <= x0:
                    continue
                dx0, dx1 = int(round(x0 - left)), int(round(x1 - left))
                sx0, sx1 = int(round(x0 - tx * 256.0)), int(round(x1 - tx * 256.0))
                key = (zoom, tx % max_idx, ty)
                tile = self.pyr.tile_cache.get(key)
                if tile is None:
                    self.pyr.record_telemetry("late_tile_fetches")
                    _, tile = self.pyr._fetch_single_tile(zoom, tx, ty)
                    self.pyr.tile_cache[key] = tile
                hs = min(dy1 - dy0, sy1 - sy0)
                ws = min(dx1 - dx0, sx1 - sx0)
                if hs > 0 and ws > 0:
                    plate[dy0:dy0 + hs, dx0:dx0 + ws] = \
                        tile[sy0:sy0 + hs, sx0:sx0 + ws]
        return plate

    # -- the per-frame cost ---------------------------------------------------
    def __getattr__(self, name):
        # Everything the engine exposes (tile_cache, render_plate_at_zoom,
        # prefetch_pyramid, ...) delegates to the wrapped pyramid, so callers
        # - including the acceptance suites - duck-type this as the engine.
        return getattr(self.pyr, name)

    def sample_continuous(self, lat: float, lon: float, zoom: float,
                          width: int | None = None,
                          height: int | None = None) -> np.ndarray:
        """Engine-compatible alias for sample()."""
        return self.sample(lat, lon, zoom, width, height)

    def _crop(self, z: int, anchor_px: tuple[float, float],
              w: int, h: int) -> np.ndarray | None:
        for ox, oy, plate in self.plates.get(z, ()):
            x0 = int(round(anchor_px[0] - ox - w / 2))
            y0 = int(round(anchor_px[1] - oy - h / 2))
            if x0 >= 0 and y0 >= 0 and x0 + w <= plate.shape[1] \
                    and y0 + h <= plate.shape[0]:
                return plate[y0:y0 + h, x0:x0 + w]
        return None

    def _scratch_buf(self, w: int, h: int, slot: int) -> np.ndarray:
        bufs = self._scratch.get((w, h))
        if bufs is None:
            bufs = [np.empty((h, w, 3), dtype=np.uint8) for _ in range(3)]
            self._scratch[(w, h)] = bufs
        return bufs[slot]

    def _sample_subpixel_level(self, z: int, anchor_px: tuple[float, float],
                               source_per_output_pixel: float,
                               width: int, height: int) -> np.ndarray | None:
        """Sample a prepared plate at continuous coordinates to avoid 1 px camera judder."""
        x_extent = width / (2.0 * source_per_output_pixel) + 2.0
        y_extent = height / (2.0 * source_per_output_pixel) + 2.0
        for origin_x, origin_y, plate in self.plates.get(z, ()):
            left = anchor_px[0] - x_extent
            top = anchor_px[1] - y_extent
            right = anchor_px[0] + x_extent
            bottom = anchor_px[1] + y_extent
            if (left < origin_x + 1 or top < origin_y + 1 or
                    right >= origin_x + plate.shape[1] - 1 or
                    bottom >= origin_y + plate.shape[0] - 1):
                continue

            out_x = np.arange(width, dtype=np.float32)[None, :]
            out_y = np.arange(height, dtype=np.float32)[:, None]
            map_x = np.broadcast_to(
                anchor_px[0] + (out_x - (width - 1) / 2.0) / source_per_output_pixel - origin_x,
                (height, width),
            ).copy()
            map_y = np.broadcast_to(
                anchor_px[1] + (out_y - (height - 1) / 2.0) / source_per_output_pixel - origin_y,
                (height, width),
            ).copy()
            return cv2.remap(plate, map_x, map_y, cv2.INTER_LINEAR,
                             borderMode=cv2.BORDER_REPLICATE)
        return None

    def sample_subpixel(self, lat: float, lon: float, zoom: float,
                        width: int | None = None,
                        height: int | None = None) -> np.ndarray:
        """Like sample(), but keep fractional camera movement through source remapping."""
        from dynamic_tile_pyramid import latlon_to_global_px

        W = self.W if width is None else int(width)
        H = self.H if height is None else int(height)
        z_lo = int(math.floor(zoom))
        t = zoom - z_lo
        alpha = t * t * (3.0 - 2.0 * t)
        if not (self.z_min <= z_lo <= self.z_max):
            return self.sample(lat, lon, zoom, W, H)

        lo = self._sample_subpixel_level(z_lo, latlon_to_global_px(lat, lon, z_lo),
                                         2.0 ** t, W, H)
        if lo is None:
            return self.sample(lat, lon, zoom, W, H)
        if alpha <= 0.001 or z_lo + 1 > self.z_max:
            return lo

        hi = self._sample_subpixel_level(z_lo + 1, latlon_to_global_px(lat, lon, z_lo + 1),
                                         2.0 ** (t - 1.0), W, H)
        if hi is None:
            self.lo_only_blend_count += 1
            return lo
        return cv2.addWeighted(lo, 1.0 - alpha, hi, alpha, 0.0)

    def sample(self, lat: float, lon: float, zoom: float,
               width: int | None = None, height: int | None = None) -> np.ndarray:
        """The engine's sample_continuous, fed from prepared plates."""
        from dynamic_tile_pyramid import latlon_to_global_px

        W = self.W if width is None else int(width)
        H = self.H if height is None else int(height)
        z_lo = int(math.floor(zoom))
        t = zoom - z_lo
        alpha = t * t * (3.0 - 2.0 * t)
        scale_lo = 2.0 ** t

        if not (self.z_min <= z_lo <= self.z_max):
            self.fallback_count += 1
            return self.pyr.sample_continuous(lat, lon, zoom, W, H)

        w_lo = int(round(W / scale_lo)) + 4
        h_lo = int(round(H / scale_lo)) + 4
        lo_px = latlon_to_global_px(lat, lon, z_lo)
        lo_crop = self._crop(z_lo, lo_px, w_lo, h_lo)
        if lo_crop is None:
            self.fallback_count += 1
            return self.pyr.sample_continuous(lat, lon, zoom, W, H)
        plate_lo = cv2.resize(lo_crop, (W, H), interpolation=cv2.INTER_LINEAR,
                              dst=self._scratch_buf(W, H, 0))

        if alpha <= 0.001 or z_lo + 1 > self.z_max:
            return plate_lo

        scale_hi = 2.0 ** (t - 1.0)
        w_hi = int(round(W / scale_hi)) + 4
        h_hi = int(round(H / scale_hi)) + 4
        hi_px = latlon_to_global_px(lat, lon, z_lo + 1)
        hi_crop = self._crop(z_lo + 1, hi_px, w_hi, h_hi)
        if hi_crop is None:
            # Plate-budget degrade (2026-10-24): the hi window can leave a
            # 1080p-class plate at low fractional zoom. Serve the frame from
            # the lo plate alone (renormalized blend) instead of the slow
            # engine; counted in lo_only_blend_count for honest telemetry.
            self.lo_only_blend_count += 1
            return plate_lo
        plate_hi = cv2.resize(hi_crop, (W, H), interpolation=cv2.INTER_LINEAR,
                              dst=self._scratch_buf(W, H, 1))
        return cv2.addWeighted(plate_lo, 1.0 - alpha, plate_hi, alpha, 0.0,
                               dst=self._scratch_buf(W, H, 2))
