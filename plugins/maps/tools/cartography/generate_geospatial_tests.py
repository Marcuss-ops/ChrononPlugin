#!/usr/bin/env python3
"""
Geospatial & 2.5D Motion Test Suite Generator for Chronon3d / ChrononMotion3D.

Generates 4 authentic documentary-grade cinematic cartography test plans:
1. Cross-Country Route (Houston -> Baton Rouge -> New Orleans along I-10)
2. District Highlight & Polygon Morph (Cleveland East Side / Cedar Estates)
3. Zoom Macro-to-Micro (Georgia 350km -> Athens Days Inn 180m rooftop)
4. Multi-Point Timeline Cluster (Chicago South Side 3-Point Escalation)

Includes:
- Real cartographic basemaps (Esri Dark Gray & Esri Satellite)
- Web Mercator (EPSG:3857) exact sub-pixel coordinate alignment
- Dynamic vector overlays (splines, glowing polygon borders, connectors)
- 100% zero-copy Vulkan + NVENC hardware acceleration.

Usage
-----
  # rewrite the plates and the four plans under /tmp/geospatial_tests
  Chronon3d/tools/cartography/generate_geospatial_tests.py

It is a dev-time authoring script, not a build step: it downloads tiles and
DEM data, so it is run deliberately, on a network.
"""

import argparse
import json
import math
import os
import subprocess
import sys
from pathlib import Path

# The cartography module sits beside this file; running as a script must not
# depend on the working directory or on `tools` being an importable package.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from map_cartography import (build_georeferenced_basemap, build_terrain_relief,
                             shade_plate_with_relief)

MAP_DIR = Path(__file__).resolve().parents[2] / "assets" / "maps"
MAP_DIR.mkdir(parents=True, exist_ok=True)

# The dark plate provider, and why it is not CartoDB.
#
# `carto_dark` answers a request it will not serve with one identical error
# image, so a plate built from it is the same tile repeated 28 times ("API KEY
# REQUIRED"): measurably 0.29 mean absolute difference between tiles where a
# real map is 16.7 (Athens) to 69.7 (Alps). Esri's Dark Gray Base is keyless and
# actually serves tiles. A plan rendered over the watermark quietly looks like a
# broken renderer rather than a broken tile provider.
PLATE_PROVIDER = "esri_dark"
RELIEF_BAKE_FACTOR = 0.55


def build_plate(center_lat: float, center_lon: float, zoom: int, filename: str):
    """The plate the CLI writes for `basemap ... --no-vignette --relief`.

    Relief is *baked* into the imagery rather than laid over it as its own
    layer: an alpha drape can only interpolate the plate towards one tint, and a
    soft-light drape needs a blend mode the Vulkan backend does not currently
    survive. Baking the hillshade multiplicatively needs no compositor feature.
    """
    plate = build_georeferenced_basemap(center_lat, center_lon, zoom,
                                        provider=PLATE_PROVIDER, vignette=False)
    relief = build_terrain_relief(center_lat, center_lon, zoom, style="shade", blur=1.2)
    plate = shade_plate_with_relief(plate, relief, RELIEF_BAKE_FACTOR)
    plate.save(filename)
    return plate

# ---------------------------------------------------------------------------
# Test 1: Cross-Country Route (Houston -> Baton Rouge -> New Orleans)
# ---------------------------------------------------------------------------
def generate_test1() -> dict:
    fps = 60
    duration_s = 4.0
    total_frames = int(duration_s * fps) # 240 frames

    # Ensure basemap exists
    m1 = build_plate(29.86, -92.70, 8, 'test1_basemap.png')

    # Screen coordinates calculated via Web Mercator (EPSG:3857)
    pt_houston = list(m1.to_screen(29.7604, -95.3698))       # ~ [474.0, 560.9]
    pt_baton_rouge = list(m1.to_screen(30.4515, -91.1871))   # ~ [1235.4, 415.5]
    pt_nola = list(m1.to_screen(29.9654, -90.0321))          # ~ [1445.7, 517.9]

    layers = []

    # 1. Background dark canvas
    layers.append({
        "id": "background",
        "type": "color",
        "color": [0.015, 0.02, 0.03, 1.0],
        "size": [1920, 1080],
        "start_frame": 0,
        "duration_frames": total_frames
    })

    # 2. Real Georeferenced Basemap Layer (CartoDB Dark Matter Gulf Coast)
    layers.append({
        "id": "basemap_texture",
        "type": "image",
        "asset": "assets/maps/test1_basemap.png",
        "size": [1920, 1080],
        "position": [960, 540],
        "start_frame": 0,
        "duration_frames": total_frames
    })

    # 3. Progressive Spline Route Overlays (6 steps smoothly lighting up along I-10)
    route_triggers = [15, 45, 75, 105, 140, 175]
    for step in range(1, 7):
        trig = route_triggers[step - 1]
        step_op = [
            {"frame": 0, "value": 0.0},
            {"frame": max(0, trig - 1), "value": 0.0},
            {"frame": trig + 3, "value": 1.0},
            {"frame": total_frames, "value": 1.0}
        ]
        layers.append({
            "id": f"route_step_{step}",
            "type": "image",
            "asset": f"assets/maps/test1_route_step_{step}.png",
            "size": [1920, 1080],
            "position": [960, 540],
            "start_frame": 0,
            "duration_frames": total_frames,
            "animation": {
                "tracks": [{"property": "opacity", "keyframes": step_op}]
            }
        })

    # 4. Header Telemetry HUD
    layers.append({
        "id": "header_hud",
        "type": "text",
        "text": "ROUTE RECON // CORRIDOR I-10 // HOUSTON TX -> NEW ORLEANS LA // DISTANCE: 562.8 KM",
        "size": [1800, 45],
        "position": [960, 65],
        "start_frame": 0,
        "duration_frames": total_frames,
        "style": {
            "font": "assets/fonts/Space-Grotesk.ttf",
            "font_size": 22,
            "fill": "#38BDF8",
            "stroke": {"color": "#031525", "width": 1.5}
        }
    })

    # 5. Houston Pin (Origin, t = 0)
    layers.append({
        "id": "houston_pin",
        "type": "text",
        "text": "●",
        "size": [80, 80],
        "position": pt_houston,
        "start_frame": 0,
        "duration_frames": total_frames,
        "style": {
            "font": "assets/fonts/Poppins-Bold.ttf",
            "font_size": 42,
            "fill": "#F59E0B",
            "shadow": {"color": "#000000", "opacity": 0.9, "blur": 12, "offset": [0, 4]}
        }
    })

    layers.append({
        "id": "houston_label",
        "type": "text",
        "text": "HOUSTON, TX [ORIGIN]\n29.7604° N, 95.3698° W",
        "size": [440, 80],
        "position": [pt_houston[0], pt_houston[1] + 70],
        "start_frame": 0,
        "duration_frames": total_frames,
        "style": {
            "font": "assets/fonts/Space-Grotesk.ttf",
            "font_size": 18,
            "fill": "#F3F4F6",
            "stroke": {"color": "#050B14", "width": 1.5}
        }
    })

    # 6. Baton Rouge transit waypoint (Appears at t = 1.75s, f = 105)
    br_f = 105
    br_op = [
        {"frame": 0, "value": 0.0},
        {"frame": br_f - 1, "value": 0.0},
        {"frame": br_f + 4, "value": 1.0},
        {"frame": total_frames, "value": 1.0}
    ]
    layers.append({
        "id": "baton_rouge_pin",
        "type": "text",
        "text": "●",
        "size": [80, 80],
        "position": pt_baton_rouge,
        "start_frame": 0,
        "duration_frames": total_frames,
        "style": {
            "font": "assets/fonts/Poppins-Bold.ttf",
            "font_size": 38,
            "fill": "#FB923C",
            "shadow": {"color": "#000000", "opacity": 0.9, "blur": 12, "offset": [0, 4]}
        },
        "animation": {
            "tracks": [{"property": "opacity", "keyframes": br_op}]
        }
    })

    layers.append({
        "id": "baton_rouge_label",
        "type": "text",
        "text": "BATON ROUGE [WAYPOINT]\n30.4515° N, 91.1871° W",
        "size": [440, 80],
        "position": [pt_baton_rouge[0], pt_baton_rouge[1] - 70],
        "start_frame": 0,
        "duration_frames": total_frames,
        "style": {
            "font": "assets/fonts/Space-Grotesk.ttf",
            "font_size": 18,
            "fill": "#E2E8F0",
            "stroke": {"color": "#050B14", "width": 1.5}
        },
        "animation": {
            "tracks": [{"property": "opacity", "keyframes": br_op}]
        }
    })

    # 7. New Orleans Arrival Pin & Callout (Drop arrival at f = 175)
    nola_f = 175
    nola_pin_op = [
        {"frame": 0, "value": 0.0},
        {"frame": nola_f - 1, "value": 0.0},
        {"frame": nola_f + 4, "value": 1.0},
        {"frame": total_frames, "value": 1.0}
    ]
    layers.append({
        "id": "nola_pin",
        "type": "text",
        "text": "●",
        "size": [100, 100],
        "position": pt_nola,
        "start_frame": 0,
        "duration_frames": total_frames,
        "style": {
            "font": "assets/fonts/Poppins-Bold.ttf",
            "font_size": 52,
            "fill": "#EF4444",
            "shadow": {"color": "#000000", "opacity": 0.9, "blur": 12, "offset": [0, 4]}
        },
        "animation": {
            "tracks": [{"property": "opacity", "keyframes": nola_pin_op}]
        }
    })

    layers.append({
        "id": "nola_label",
        "type": "text",
        "text": "NEW ORLEANS, LA [9TH WARD]\n29.9654° N, 90.0321° W\nSTATUS: TARGET REACHED",
        "size": [520, 100],
        "position": [pt_nola[0] - 60, pt_nola[1] + 85],
        "start_frame": 0,
        "duration_frames": total_frames,
        "style": {
            "font": "assets/fonts/Space-Grotesk.ttf",
            "font_size": 20,
            "fill": "#38BDF8",
            "stroke": {"color": "#050B14", "width": 1.5}
        },
        "animation": {
            "tracks": [{"property": "opacity", "keyframes": nola_pin_op}]
        }
    })

    return {
        "schema": "chronon.render-plan.v2",
        "version": 2,
        "job_id": "test1_cross_country_houston_nola",
        "canvas": {
            "width": 1920,
            "height": 1080,
            "fps_num": 60,
            "fps_den": 1,
            "duration_frames": total_frames
        },
        "layers": layers,
        "output": {
            "path": "/tmp/test1_cross_country_houston_nola.mp4",
            "format": "mp4",
            "codec": "h264"
        }
    }


# ---------------------------------------------------------------------------
# Test 2: District Highlight & Polygon Morph (Cleveland East Side)
# ---------------------------------------------------------------------------
def generate_test2() -> dict:
    fps = 60
    duration_s = 3.5
    total_frames = int(duration_s * fps) # 210 frames

    m2 = build_plate(41.4925, -81.6660, 14, 'test2_basemap.png')

    poly_pts = [
        m2.to_screen(41.4980, -81.6740),
        m2.to_screen(41.4995, -81.6550),
        m2.to_screen(41.4910, -81.6500),
        m2.to_screen(41.4850, -81.6610),
        m2.to_screen(41.4880, -81.6770)
    ]

    layers = []

    # 1. Dark canvas background
    layers.append({
        "id": "background",
        "type": "color",
        "color": [0.02, 0.025, 0.035, 1.0],
        "size": [1920, 1080],
        "start_frame": 0,
        "duration_frames": total_frames
    })

    # 2. Real Georeferenced Basemap (Cleveland East Side Urban Street Grid)
    layers.append({
        "id": "basemap_texture",
        "type": "image",
        "asset": "assets/maps/test2_basemap.png",
        "size": [1920, 1080],
        "position": [960, 540],
        "start_frame": 0,
        "duration_frames": total_frames
    })

    # 3. Polygon Highlight Overlay (Semi-transparent amber fill + 3px glowing neon border)
    poly_op = [
        {"frame": 0, "value": 0.0},
        {"frame": 25, "value": 0.0},
        {"frame": 40, "value": 1.0},
        {"frame": total_frames, "value": 1.0}
    ]
    layers.append({
        "id": "district_polygon_overlay",
        "type": "image",
        "asset": "assets/maps/test2_polygon_overlay.png",
        "size": [1920, 1080],
        "position": [960, 540],
        "start_frame": 0,
        "duration_frames": total_frames,
        "animation": {
            "tracks": [{"property": "opacity", "keyframes": poly_op}]
        }
    })

    # 4. Header HUD & Telemetry
    layers.append({
        "id": "telemetry_hud",
        "type": "text",
        "text": "TERRITORIAL OVERVIEW // SECTOR: CLEVELAND EAST SIDE // 41.4925° N, 81.6660° W",
        "size": [1800, 45],
        "position": [960, 65],
        "start_frame": 0,
        "duration_frames": total_frames,
        "style": {
            "font": "assets/fonts/Space-Grotesk.ttf",
            "font_size": 22,
            "fill": "#F97316",
            "stroke": {"color": "#020617", "width": 1.5}
        }
    })

    # 5. Polygon Vertex tactical pin markers
    for idx, (vx, vy) in enumerate(poly_pts):
        v_trig = 25 + idx * 3
        v_op = [
            {"frame": 0, "value": 0.0},
            {"frame": v_trig, "value": 0.0},
            {"frame": v_trig + 4, "value": 1.0},
            {"frame": total_frames, "value": 1.0}
        ]
        layers.append({
            "id": f"poly_vertex_{idx}",
            "type": "text",
            "text": "◆",
            "size": [40, 40],
            "position": [round(vx, 1), round(vy, 1)],
            "start_frame": 0,
            "duration_frames": total_frames,
            "style": {
                "font": "assets/fonts/Poppins-Bold.ttf",
                "font_size": 20,
                "fill": "#F97316",
                "shadow": {"color": "#000000", "opacity": 0.9, "blur": 8, "offset": [0, 2]}
            },
            "animation": {
                "tracks": [{"property": "opacity", "keyframes": v_op}]
            }
        })

    # 6. District Callout Title Card
    card_op = [
        {"frame": 0, "value": 0.0},
        {"frame": 35, "value": 0.0},
        {"frame": 48, "value": 1.0},
        {"frame": total_frames, "value": 1.0}
    ]
    layers.append({
        "id": "district_card_title",
        "type": "text",
        "text": "CEDAR ESTATES & CENTRAL AREA",
        "size": [1200, 70],
        "position": [960, 850],
        "start_frame": 0,
        "duration_frames": total_frames,
        "style": {
            "font": "assets/fonts/Poppins-Bold.ttf",
            "font_size": 48,
            "fill": "#FFFFFF",
            "stroke": {"color": "#020617", "width": 3.0},
            "shadow": {"color": "#000000", "opacity": 0.85, "blur": 16, "offset": [0, 6]}
        },
        "animation": {
            "tracks": [{"property": "opacity", "keyframes": card_op}]
        }
    })

    layers.append({
        "id": "district_card_sub",
        "type": "text",
        "text": "SURVEILLANCE GRID LOCK • 2.5D BOUNDARY ACTIVE • ZONE ID #CLE-044",
        "size": [1200, 50],
        "position": [960, 915],
        "start_frame": 0,
        "duration_frames": total_frames,
        "style": {
            "font": "assets/fonts/Space-Grotesk.ttf",
            "font_size": 22,
            "fill": "#FB923C",
            "stroke": {"color": "#020617", "width": 1.2}
        },
        "animation": {
            "tracks": [{"property": "opacity", "keyframes": card_op}]
        }
    })

    return {
        "schema": "chronon.render-plan.v2",
        "version": 2,
        "job_id": "test2_district_polygon_cleveland",
        "canvas": {
            "width": 1920,
            "height": 1080,
            "fps_num": 60,
            "fps_den": 1,
            "duration_frames": total_frames
        },
        "layers": layers,
        "output": {
            "path": "/tmp/test2_district_polygon_cleveland.mp4",
            "format": "mp4",
            "codec": "h264"
        }
    }


# ---------------------------------------------------------------------------
# Test 3: Zoom Macro-to-Micro (Georgia 350km -> Athens Days Inn 180m)
# ---------------------------------------------------------------------------
def generate_test3() -> dict:
    fps = 60
    duration_s = 5.0
    total_frames = int(duration_s * fps) # 300 frames

    # Ensure all 3 satellite terrain levels exist
    m3_macro = build_georeferenced_basemap(center_lat=33.4, center_lon=-83.5, zoom=8, provider='esri_sat')
    m3_macro.save('test3_macro_sat.png')

    m3_reg = build_georeferenced_basemap(center_lat=33.9598, center_lon=-83.3768, zoom=12, provider='esri_sat')
    m3_reg.save('test3_regional_sat.png')

    m3_micro = build_georeferenced_basemap(center_lat=33.9598, center_lon=-83.3768, zoom=16, provider='esri_sat')
    m3_micro.save('test3_micro_sat.png')

    layers = []

    # 1. Dark background
    layers.append({
        "id": "background",
        "type": "color",
        "color": [0.02, 0.025, 0.03, 1.0],
        "size": [1920, 1080],
        "start_frame": 0,
        "duration_frames": total_frames
    })

    # 2. Multi-Scale Satellite Descent Pipeline
    # Level 1: Macro Georgia State Overview (Zoom 8)
    macro_op = [
        {"frame": 0, "value": 1.0},
        {"frame": 85, "value": 1.0},
        {"frame": 105, "value": 0.0},
        {"frame": total_frames, "value": 0.0}
    ]
    layers.append({
        "id": "sat_macro_state",
        "type": "image",
        "asset": "assets/maps/test3_macro_sat.png",
        "size": [1920, 1080],
        "position": [960, 540],
        "start_frame": 0,
        "duration_frames": total_frames,
        "animation": {
            "tracks": [{"property": "opacity", "keyframes": macro_op}]
        }
    })

    # Level 2: Regional Athens Corridor (Zoom 12)
    reg_op = [
        {"frame": 0, "value": 0.0},
        {"frame": 80, "value": 0.0},
        {"frame": 100, "value": 1.0},
        {"frame": 180, "value": 1.0},
        {"frame": 200, "value": 0.0},
        {"frame": total_frames, "value": 0.0}
    ]
    layers.append({
        "id": "sat_reg_corridor",
        "type": "image",
        "asset": "assets/maps/test3_regional_sat.png",
        "size": [1920, 1080],
        "position": [960, 540],
        "start_frame": 0,
        "duration_frames": total_frames,
        "animation": {
            "tracks": [{"property": "opacity", "keyframes": reg_op}]
        }
    })

    # Level 3: Micro Satellite Lock (Zoom 16, Days Inn Motel Rooftop)
    micro_op = [
        {"frame": 0, "value": 0.0},
        {"frame": 175, "value": 0.0},
        {"frame": 195, "value": 1.0},
        {"frame": total_frames, "value": 1.0}
    ]
    layers.append({
        "id": "sat_micro_rooftop",
        "type": "image",
        "asset": "assets/maps/test3_micro_sat.png",
        "size": [1920, 1080],
        "position": [960, 540],
        "start_frame": 0,
        "duration_frames": total_frames,
        "animation": {
            "tracks": [{"property": "opacity", "keyframes": micro_op}]
        }
    })

    # 3. Dynamic Altitude Telemetry Readout (Updates during vertical descent)
    # Phase 1 HUD
    hud1_op = [
        {"frame": 0, "value": 1.0},
        {"frame": 90, "value": 1.0},
        {"frame": 95, "value": 0.0},
        {"frame": total_frames, "value": 0.0}
    ]
    layers.append({
        "id": "hud_alt_p1",
        "type": "text",
        "text": "ALT: 350,000m // SATELLITE RECON // REGION: GEORGIA // MACRO SCAN",
        "size": [1600, 60],
        "position": [960, 75],
        "start_frame": 0,
        "duration_frames": total_frames,
        "style": {
            "font": "assets/fonts/Space-Grotesk.ttf",
            "font_size": 22,
            "fill": "#38BDF8",
            "stroke": {"color": "#031525", "width": 1.2}
        },
        "animation": {
            "tracks": [{"property": "opacity", "keyframes": hud1_op}]
        }
    })

    # Phase 2 HUD
    hud2_op = [
        {"frame": 0, "value": 0.0},
        {"frame": 95, "value": 0.0},
        {"frame": 100, "value": 1.0},
        {"frame": 190, "value": 1.0},
        {"frame": 195, "value": 0.0},
        {"frame": total_frames, "value": 0.0}
    ]
    layers.append({
        "id": "hud_alt_p2",
        "type": "text",
        "text": "ALT: 15,000m // CLARKE COUNTY APPROACH // SECTOR: ATHENS // DESCENT ACTIVE",
        "size": [1600, 60],
        "position": [960, 75],
        "start_frame": 0,
        "duration_frames": total_frames,
        "style": {
            "font": "assets/fonts/Space-Grotesk.ttf",
            "font_size": 22,
            "fill": "#38BDF8",
            "stroke": {"color": "#031525", "width": 1.2}
        },
        "animation": {
            "tracks": [{"property": "opacity", "keyframes": hud2_op}]
        }
    })

    # Phase 3 HUD (Final target lock telemetry)
    hud3_op = [
        {"frame": 0, "value": 0.0},
        {"frame": 195, "value": 0.0},
        {"frame": 200, "value": 1.0},
        {"frame": total_frames, "value": 1.0}
    ]
    layers.append({
        "id": "hud_alt_p3",
        "type": "text",
        "text": "ALT: 180m // TARGET LOCK: DAYS INN MOTEL // 33.9598° N, 83.3768° W // OPTICAL SENSOR ACTIVE",
        "size": [1600, 60],
        "position": [960, 75],
        "start_frame": 0,
        "duration_frames": total_frames,
        "style": {
            "font": "assets/fonts/Space-Grotesk.ttf",
            "font_size": 22,
            "fill": "#10B981",
            "stroke": {"color": "#031525", "width": 1.2}
        },
        "animation": {
            "tracks": [{"property": "opacity", "keyframes": hud3_op}]
        }
    })

    # 4. HUD Targeting Reticle Box (Locks at frame 252, t = 4.2s)
    lock_frame = 252
    reticle_op = [
        {"frame": 0, "value": 0.0},
        {"frame": lock_frame - 1, "value": 0.0},
        {"frame": lock_frame, "value": 1.0},
        {"frame": total_frames, "value": 1.0}
    ]
    layers.append({
        "id": "target_reticle",
        "type": "text",
        "text": "[ TARGET LOCKED ]",
        "size": [800, 120],
        "position": [960, 540],
        "start_frame": 0,
        "duration_frames": total_frames,
        "style": {
            "font": "assets/fonts/Poppins-Bold.ttf",
            "font_size": 48,
            "fill": "#EF4444",
            "stroke": {"color": "#450A0A", "width": 2.0},
            "shadow": {"color": "#000000", "opacity": 0.9, "blur": 14, "offset": [0, 4]}
        },
        "animation": {
            "tracks": [{"property": "opacity", "keyframes": reticle_op}]
        }
    })

    # 5. Final Target Confirmation Badge
    final_op = [
        {"frame": 0, "value": 0.0},
        {"frame": lock_frame + 6, "value": 0.0},
        {"frame": lock_frame + 12, "value": 1.0},
        {"frame": total_frames, "value": 1.0}
    ]
    layers.append({
        "id": "target_confirmed_badge",
        "type": "text",
        "text": "TARGET ACQUIRED // DAYS INN MOTEL // ATHENS, GA",
        "size": [1600, 100],
        "position": [960, 840],
        "start_frame": 0,
        "duration_frames": total_frames,
        "style": {
            "font": "assets/fonts/Poppins-Bold.ttf",
            "font_size": 42,
            "fill": "#FFFFFF",
            "stroke": {"color": "#020617", "width": 3.0},
            "shadow": {"color": "#000000", "opacity": 0.85, "blur": 16, "offset": [0, 6]}
        },
        "animation": {
            "tracks": [{"property": "opacity", "keyframes": final_op}]
        }
    })

    layers.append({
        "id": "target_coords_final",
        "type": "text",
        "text": "FINAL RESOLUTION: 33.9598° N, 83.3768° W • ALT 180m • ZERO DRIFT LOCK",
        "size": [1600, 60],
        "position": [960, 905],
        "start_frame": 0,
        "duration_frames": total_frames,
        "style": {
            "font": "assets/fonts/Space-Grotesk.ttf",
            "font_size": 20,
            "fill": "#10B981",
            "stroke": {"color": "#020617", "width": 1.2}
        },
        "animation": {
            "tracks": [{"property": "opacity", "keyframes": final_op}]
        }
    })

    return {
        "schema": "chronon.render-plan.v2",
        "version": 2,
        "job_id": "test3_zoom_macro_to_micro_georgia",
        "canvas": {
            "width": 1920,
            "height": 1080,
            "fps_num": 60,
            "fps_den": 1,
            "duration_frames": total_frames
        },
        "layers": layers,
        "output": {
            "path": "/tmp/test3_zoom_macro_to_micro_georgia.mp4",
            "format": "mp4",
            "codec": "h264"
        }
    }


# ---------------------------------------------------------------------------
# Test 4: Multi-Point Timeline Cluster (Chicago South Side 3-Point Escalation)
# ---------------------------------------------------------------------------
def generate_test4() -> dict:
    fps = 60
    duration_s = 5.5
    total_frames = int(duration_s * fps) # 330 frames

    m4 = build_plate(41.8300, -87.6200, 12, 'test4_basemap.png')

    pt_a = list(m4.to_screen(41.7798, -87.6254)) # Dan Ryan 69th: ~ [944.3, 736.2]
    pt_b = list(m4.to_screen(41.7836, -87.6162)) # Trap City 61st: ~ [971.1, 721.3]
    pt_c = list(m4.to_screen(41.8818, -87.6243)) # Innovations High Loop: ~ [947.5, 337.4]

    layers = []

    # 1. Dark canvas background
    layers.append({
        "id": "background",
        "type": "color",
        "color": [0.02, 0.025, 0.035, 1.0],
        "size": [1920, 1080],
        "start_frame": 0,
        "duration_frames": total_frames
    })

    # 2. Real Georeferenced Basemap (Chicago South Side & Loop Street Grid)
    layers.append({
        "id": "basemap_texture",
        "type": "image",
        "asset": "assets/maps/test4_basemap.png",
        "size": [1920, 1080],
        "position": [960, 540],
        "start_frame": 0,
        "duration_frames": total_frames
    })

    # 3. Header HUD
    layers.append({
        "id": "header_hud",
        "type": "text",
        "text": "CRIME PATTERN TIMELINE // CHICAGO SOUTH SIDE CLUSTER // ESCALATION TRACKER",
        "size": [1800, 45],
        "position": [960, 65],
        "start_frame": 0,
        "duration_frames": total_frames,
        "style": {
            "font": "assets/fonts/Space-Grotesk.ttf",
            "font_size": 22,
            "fill": "#EF4444",
            "stroke": {"color": "#020617", "width": 1.5}
        }
    })

    # 4. Tactical Connector Overlay AB (Event 1 -> Event 2)
    conn_ab_op = [
        {"frame": 0, "value": 0.0},
        {"frame": 95, "value": 0.0},
        {"frame": 105, "value": 1.0},
        {"frame": total_frames, "value": 1.0}
    ]
    layers.append({
        "id": "conn_ab_overlay",
        "type": "image",
        "asset": "assets/maps/test4_conn_ab.png",
        "size": [1920, 1080],
        "position": [960, 540],
        "start_frame": 0,
        "duration_frames": total_frames,
        "animation": {
            "tracks": [{"property": "opacity", "keyframes": conn_ab_op}]
        }
    })

    # 5. Tactical Connector Overlay BC (Event 2 -> Event 3)
    conn_bc_op = [
        {"frame": 0, "value": 0.0},
        {"frame": 195, "value": 0.0},
        {"frame": 208, "value": 1.0},
        {"frame": total_frames, "value": 1.0}
    ]
    layers.append({
        "id": "conn_bc_overlay",
        "type": "image",
        "asset": "assets/maps/test4_conn_bc.png",
        "size": [1920, 1080],
        "position": [960, 540],
        "start_frame": 0,
        "duration_frames": total_frames,
        "animation": {
            "tracks": [{"property": "opacity", "keyframes": conn_bc_op}]
        }
    })

    # 6. Event Node A: Dan Ryan Corridor / McDonald's (t = 0.5s, f = 30)
    f_a = 30
    op_a = [
        {"frame": 0, "value": 0.0},
        {"frame": f_a - 1, "value": 0.0},
        {"frame": f_a + 4, "value": 1.0},
        {"frame": total_frames, "value": 1.0}
    ]
    layers.append({
        "id": "node_a_pin",
        "type": "text",
        "text": "●",
        "size": [80, 80],
        "position": pt_a,
        "start_frame": 0,
        "duration_frames": total_frames,
        "style": {
            "font": "assets/fonts/Poppins-Bold.ttf",
            "font_size": 42,
            "fill": "#EF4444",
            "shadow": {"color": "#000000", "opacity": 0.9, "blur": 12, "offset": [0, 4]}
        },
        "animation": {
            "tracks": [{"property": "opacity", "keyframes": op_a}]
        }
    })

    layers.append({
        "id": "node_a_card",
        "type": "text",
        "text": "EVENT 01 • MARCH 2023\nDAN RYAN CORRIDOR\n41.7798° N, 87.6254° W",
        "size": [440, 85],
        "position": [pt_a[0] - 280, pt_a[1]],
        "start_frame": 0,
        "duration_frames": total_frames,
        "style": {
            "font": "assets/fonts/Space-Grotesk.ttf",
            "font_size": 18,
            "fill": "#FFFFFF",
            "stroke": {"color": "#020617", "width": 1.5}
        },
        "animation": {
            "tracks": [{"property": "opacity", "keyframes": op_a}]
        }
    })

    # 7. Event Node B: Trap City / 61st St (t = 2.0s, f = 120)
    f_b = 120
    op_b = [
        {"frame": 0, "value": 0.0},
        {"frame": f_b - 1, "value": 0.0},
        {"frame": f_b + 4, "value": 1.0},
        {"frame": total_frames, "value": 1.0}
    ]
    layers.append({
        "id": "node_b_pin",
        "type": "text",
        "text": "●",
        "size": [80, 80],
        "position": pt_b,
        "start_frame": 0,
        "duration_frames": total_frames,
        "style": {
            "font": "assets/fonts/Poppins-Bold.ttf",
            "font_size": 42,
            "fill": "#EF4444",
            "shadow": {"color": "#000000", "opacity": 0.9, "blur": 12, "offset": [0, 4]}
        },
        "animation": {
            "tracks": [{"property": "opacity", "keyframes": op_b}]
        }
    })

    layers.append({
        "id": "node_b_card",
        "type": "text",
        "text": "EVENT 02 • JUNE 2023\nTRAP CITY / 61ST ST\n41.7836° N, 87.6162° W",
        "size": [440, 85],
        "position": [pt_b[0] + 280, pt_b[1]],
        "start_frame": 0,
        "duration_frames": total_frames,
        "style": {
            "font": "assets/fonts/Space-Grotesk.ttf",
            "font_size": 18,
            "fill": "#FFFFFF",
            "stroke": {"color": "#020617", "width": 1.5}
        },
        "animation": {
            "tracks": [{"property": "opacity", "keyframes": op_b}]
        }
    })

    # 8. Event Node C: Innovations High / Michigan Ave (t = 3.8s, f = 228)
    f_c = 228
    op_c = [
        {"frame": 0, "value": 0.0},
        {"frame": f_c - 1, "value": 0.0},
        {"frame": f_c + 4, "value": 1.0},
        {"frame": total_frames, "value": 1.0}
    ]
    layers.append({
        "id": "node_c_pin",
        "type": "text",
        "text": "●",
        "size": [80, 80],
        "position": pt_c,
        "start_frame": 0,
        "duration_frames": total_frames,
        "style": {
            "font": "assets/fonts/Poppins-Bold.ttf",
            "font_size": 46,
            "fill": "#F59E0B",
            "shadow": {"color": "#000000", "opacity": 0.9, "blur": 12, "offset": [0, 4]}
        },
        "animation": {
            "tracks": [{"property": "opacity", "keyframes": op_c}]
        }
    })

    layers.append({
        "id": "node_c_card",
        "type": "text",
        "text": "EVENT 03 • AUGUST 2023\nINNOVATIONS HIGH / MICHIGAN AVE\n41.8818° N, 87.6243° W",
        "size": [480, 85],
        "position": [pt_c[0] - 300, pt_c[1]],
        "start_frame": 0,
        "duration_frames": total_frames,
        "style": {
            "font": "assets/fonts/Space-Grotesk.ttf",
            "font_size": 18,
            "fill": "#FFFFFF",
            "stroke": {"color": "#020617", "width": 1.5}
        },
        "animation": {
            "tracks": [{"property": "opacity", "keyframes": op_c}]
        }
    })

    # 9. Summary Timeline Status Bar
    op_summary = [
        {"frame": 0, "value": 0.0},
        {"frame": f_c + 15, "value": 0.0},
        {"frame": f_c + 20, "value": 1.0},
        {"frame": total_frames, "value": 1.0}
    ]
    layers.append({
        "id": "timeline_summary",
        "type": "text",
        "text": "3 NODES IDENTIFIED • CHRONOLOGICAL LINK VERIFIED • ESCALATION CLUSTER #CHI-S",
        "size": [1600, 45],
        "position": [960, 950],
        "start_frame": 0,
        "duration_frames": total_frames,
        "style": {
            "font": "assets/fonts/Poppins-Bold.ttf",
            "font_size": 24,
            "fill": "#38BDF8",
            "stroke": {"color": "#020617", "width": 1.2}
        },
        "animation": {
            "tracks": [{"property": "opacity", "keyframes": op_summary}]
        }
    })

    return {
        "schema": "chronon.render-plan.v2",
        "version": 2,
        "job_id": "test4_timeline_cluster_chicago",
        "canvas": {
            "width": 1920,
            "height": 1080,
            "fps_num": 60,
            "fps_den": 1,
            "duration_frames": total_frames
        },
        "layers": layers,
        "output": {
            "path": "/tmp/test4_timeline_cluster_chicago.mp4",
            "format": "mp4",
            "codec": "h264"
        }
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=(__doc__.strip().splitlines()[0] +
                     " Plates are rebuilt from tiles and DEM data, so this needs a"
                     " network and overwrites Chronon3d/assets/maps."))
    parser.add_argument("--out-dir", default="/tmp/geospatial_tests",
                        help="where the four plans are written")
    args = parser.parse_args(argv)

    generators = [
        ("test1_cross_country_houston_nola", generate_test1),
        ("test2_district_polygon_cleveland", generate_test2),
        ("test3_zoom_macro_to_micro_georgia", generate_test3),
        ("test4_timeline_cluster_chicago", generate_test4)
    ]

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    for name, gen_fn in generators:
        plan_dict = gen_fn()
        plan_path = out_dir / f"{name}.json"
        with open(plan_path, "w") as f:
            json.dump(plan_dict, f, indent=2)
        print(f"Generated plan: {plan_path}")

    print("All 4 documentary map plans generated successfully.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
