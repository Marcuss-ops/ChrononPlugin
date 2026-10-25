#!/usr/bin/env python3
"""Offline acceptance tests for exact FastPlateSampler tile prefetch."""
from __future__ import annotations

import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path
from unittest.mock import patch

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import dynamic_tile_pyramid as dyn
from fast_plate_sampler import FastPlateSampler


class ExactPlatePrefetchTests(unittest.TestCase):
    def test_prepare_prefetches_exact_coverage_before_compose_and_sample(self):
        pyramid = dyn.DynamicTilePyramid()
        original_fetch = pyramid._fetch_single_tile
        active = 0
        high_water = 0
        calls = []
        lock = threading.Lock()

        def synthetic_fetch(z, x, y):
            nonlocal active, high_water
            key = (z, x, y)
            with lock:
                active += 1
                high_water = max(high_water, active)
                calls.append(key)
            time.sleep(0.002)
            value = (z * 17 + x * 7 + y) % 255
            image = np.full((256, 256, 3), value, dtype=np.uint8)
            with lock:
                active -= 1
            return key, image

        pyramid._fetch_single_tile = synthetic_fetch
        sampler = FastPlateSampler(pyramid, 400, 240)
        # Pin the worker bound the test asserts against (the production
        # default is TILE_PREFETCH_WORKERS; the contract under test is that
        # prefetch respects whatever bound it is given).
        sampler.prefetch_workers = 12
        sampler.prepare([(41.89, 12.49), (41.90, 12.50)], 5, 6)

        expected = sampler.required_tile_keys()
        self.assertTrue(expected)
        self.assertEqual(set(pyramid.tile_cache), expected)
        self.assertEqual(len(calls), len(set(calls)), "normalized coverage must fetch each tile once")
        self.assertLessEqual(high_water, sampler.prefetch_workers,
                             "prefetch must respect its worker bound")
        self.assertEqual(pyramid.telemetry["late_tile_fetches"], 0)
        self.assertEqual(sampler.prepare_telemetry["required_tile_count"], len(expected))
        self.assertEqual(sampler.prepare_telemetry["prefetch_tiles_fetched"], len(expected))

        # The independent, slow reference path must be served from the same
        # complete tile set; the prepared path must not fall back or fetch.
        reference = dyn.DynamicTilePyramid()
        reference.tile_cache.update(pyramid.tile_cache)
        before = len(calls)
        actual = sampler.sample(41.89, 12.49, 5.35, 320, 180)
        wanted = reference.sample_continuous(41.89, 12.49, 5.35, 320, 180)
        delta = np.abs(actual.astype(np.int16) - wanted.astype(np.int16))
        self.assertLessEqual(int(delta.max()), 220)
        self.assertLessEqual(float(delta.mean()), 15.0)
        self.assertEqual(len(calls), before, "sampling after prepare must not fetch tiles")
        self.assertEqual(sampler.fallback_count, 0)
        self.assertTrue(sampler.can_sample(41.89, 12.49, 5.35, 320, 180))
        self.assertFalse(sampler.can_sample(41.89, 12.49, 7.35, 320, 180))
        self.assertEqual(pyramid.telemetry["late_tile_fetches"], 0)
        self.assertIsNotNone(original_fetch)

    def test_prefetch_normalizes_wrapped_x_and_bounds_y(self):
        pyramid = dyn.DynamicTilePyramid()
        fetched = []

        def synthetic_fetch(z, x, y):
            fetched.append((z, x, y))
            return (z, x, y), np.zeros((256, 256, 3), dtype=np.uint8)

        pyramid._fetch_single_tile = synthetic_fetch
        result = pyramid.prefetch_tiles(
            [(5, -1, 2), (5, 31, 2), (5, 32, 2), (5, 0, 2), (5, 0, -1)],
            max_workers=2,
        )
        self.assertEqual(result["requested"], 2)
        self.assertEqual(set(fetched), {(5, 31, 2), (5, 0, 2)})
        self.assertEqual(pyramid.telemetry["prefetch_tiles_requested"], 2)

    def test_disk_hit_network_fetch_and_fallback_are_counted(self):
        tile_bytes = dyn.cv2.imencode(".jpg", np.full((256, 256, 3), 80, np.uint8))[1].tobytes()
        with tempfile.TemporaryDirectory() as tmp:
            with patch.object(dyn, "CACHE_DIR", Path(tmp)):
                pyramid = dyn.DynamicTilePyramid()
                tile_path = Path(tmp) / pyramid.provider / "3" / "2" / "4.jpg"
                tile_path.parent.mkdir(parents=True)
                tile_path.write_bytes(tile_bytes)
                pyramid._fetch_single_tile(3, 2, 4)
                self.assertEqual(pyramid.telemetry["tile_disk_cache_hits"], 1)
                self.assertEqual(pyramid.telemetry["tile_disk_cache_misses"], 0)

                with patch.object(dyn, "_http_get", return_value=tile_bytes):
                    pyramid._fetch_single_tile(3, 3, 4)
                self.assertEqual(pyramid.telemetry["tile_disk_cache_misses"], 1)
                self.assertEqual(pyramid.telemetry["tile_network_fetches"], 1)
                self.assertEqual(pyramid.telemetry["tile_bytes_downloaded"], len(tile_bytes))

                with patch.object(dyn, "_http_get", side_effect=OSError("offline")), \
                     patch.object(dyn.time, "sleep"):
                    _, fallback = pyramid._fetch_single_tile(3, 4, 4)
                self.assertEqual(fallback.shape, (256, 256, 3))
                self.assertEqual(pyramid.telemetry["tile_disk_cache_misses"], 2)
                self.assertEqual(pyramid.telemetry["tile_fallbacks"], 1)


if __name__ == "__main__":
    unittest.main()
