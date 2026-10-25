#!/usr/bin/env python3
"""Degrade contract for FastPlateSampler.sample_opencl (2026-10-24).

can_sample() documents that only the lo blend window must be covered and that
an overflow of the hi window degrades to the lo plate ("counted in
lo_only_blend_count ... instead of aborting the whole run"). The CPU paths
(sample/sample_subpixel) implement exactly that, plus a slow tile-engine
fallback when even the lo window leaves the plates. sample_opencl must honour
the same contract: the maps rewrite aborted a v28 render with "no prepared
plate at zoom 9" while coverage had been accepted.

Contracts pinned here:
  * hi window outside the prepared plates -> lo-only frame, counted in
    lo_only_blend_count, no abort, no tile fallback;
  * pose outside every prepared plate -> the slow tile engine serves the frame
    (fallback_count), no abort;
  * every pose can_sample() accepts -> sample_opencl returns a frame.
"""
from __future__ import annotations

import sys
import unittest
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import cv2

import dynamic_tile_pyramid as dyn
from fast_plate_sampler import FastPlateSampler

W, H = 192, 108


def synthetic_pyramid() -> dyn.DynamicTilePyramid:
    pyramid = dyn.DynamicTilePyramid()

    def synthetic_fetch(z, x, y):
        value = (z * 17 + x * 7 + y) % 255
        return (z, x, y), np.full((256, 256, 3), value, dtype=np.uint8)

    pyramid._fetch_single_tile = synthetic_fetch
    return pyramid


@unittest.skipIf(not cv2.ocl.haveOpenCL(), "OpenCL is unavailable on this host")
class SampleOpenCLDegradeTests(unittest.TestCase):
    def _prepared(self, anchors, z_min, z_max):
        sampler = FastPlateSampler(synthetic_pyramid(), W, H)
        sampler.plate_margin_factor = 2.5
        sampler.prepare(anchors, z_min, z_max)
        sampler.prepare_opencl()
        return sampler

    def test_hi_window_overflow_degrades_to_lo_only(self):
        sampler = self._prepared([(45.0, 7.0)], 9, 10)
        # Simulate the plate-budget case deterministically: the lo level keeps
        # its plate while the hi level has none (as in the per-anchor split).
        sampler.opencl_plates[10] = ()
        before = sampler.lo_only_blend_count
        frame = sampler.sample_opencl(45.0, 7.0, 9.35, W, H)
        self.assertIsNotNone(frame)
        self.assertEqual(sampler.lo_only_blend_count, before + 1)
        self.assertEqual(sampler.fallback_count, 0)

    def test_uncovered_pose_falls_back_to_tile_engine(self):
        sampler = self._prepared([(45.0, 7.0)], 9, 10)
        # Far outside the prepared plates (can_sample is False): the render
        # must still be served, by the slow reference engine, not aborted.
        self.assertFalse(sampler.can_sample(12.0, -40.0, 9.35, W, H))
        before = sampler.fallback_count
        frame = sampler.sample_opencl(12.0, -40.0, 9.35, W, H)
        self.assertIsNotNone(frame)
        self.assertGreater(sampler.fallback_count, before)
        self.assertEqual(sampler.lo_only_blend_count, 0)

    def test_accepted_coverage_never_aborts(self):
        # Per-anchor plate split: anchors kilometres apart force one plate per
        # anchor at fine zooms — the scenario of the v28 abort.
        sampler = self._prepared([(45.0, 7.0), (48.0, 12.0)], 8, 10)
        accepted = 0
        for lat, lon in ((45.0, 7.0), (45.05, 7.05), (48.0, 12.0), (47.95, 11.95)):
            for zoom in (8.35, 9.0, 9.35, 9.9):
                if not sampler.can_sample(lat, lon, zoom, W, H):
                    continue
                accepted += 1
                frame = sampler.sample_opencl(lat, lon, zoom, W, H)
                self.assertIsNotNone(frame, f"accepted pose aborted at {lat},{lon} z{zoom}")
        self.assertGreater(accepted, 0, "grid must exercise accepted poses")


if __name__ == "__main__":
    unittest.main(verbosity=2)
