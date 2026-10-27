# Native documentary map showcases

Each payload below renders one distinct C++/OpenCV animation with Esri World
Imagery. Rendered MP4s and review PNGs are written under `out/map-showcases/`.

| Payload | Animation |
| --- | --- |
| `01_spy_satellite_dive_lock.json` | Exponential 7.0→18.2 dive, target reticle, live coordinate/altitude/GSD HUD and radial speed blur. |
| `02_crime_scene_radar.json` | Cold satellite grade, geodetic 1.5 km radius, hatch, dashed perimeter and pulsing radar rings. |
| `03_multi_leg_flight.json` | Roma–Firenze–Bologna–Venezia great-circle route, narration-time progress, bloom and altitude stems. |
| `04_territorial_polygon_wipe.json` | Pavia GeoJSON perimeter wipe with outside darkening, translucent fill and neon outline. |

The additional native concept gallery has one payload per animation:

| Payload | Animation |
| --- | --- |
| `05_cell_tower_triangulation.json` | BTS sectors and common overlap estimate. |
| `06_isochrone_travel_bubble.json` | Irregular 5/10/15 minute bubble style. |
| `07_timeline_suspect_trail.json` | Timed movement trail. |
| `08_thermal_heatmap_overlay.json` | Inferno density overlay. |
| `09_night_vision_infrared.json` | Green phosphor, scanlines and reticle. |
| `10_frontline_fluid_corridor.json` | Schematic opposing corridor. |
| `11_naval_chokepoint_exclusion.json` | Pulsing maritime exclusion rings. |
| `12_missile_arc_ballistic.json` | Animated parabolic arc and ground projection. |
| `13_multi_camera_split_matrix.json` | Four-panel viewport preview. |
| `14_air_defense_interception_dome.json` | Nested coverage envelope. |
| `15_subsea_cable_glow_network.json` | Glowing route and moving head. |
| `16_supply_chain_artery_pulse.json` | Wide flow artery. |
| `17_great_circle_polar_route.json` | Global route preview. |
| `18_port_hub_radial_burst.json` | Hub-to-destination route burst. |
| `19_pipeline_energy_flow.json` | Animated dashed energy route. |
| `20_satellite_before_after_split.json` | Grayscale/color split-slider treatment. |
| `21_topographic_contour_rise.json` | Image-edge contour treatment. |
| `22_flood_sea_level_inundation.json` | Rising illustrative waterline. |
| `23_wildfire_burn_scar_expansion.json` | Expanding burn-scar treatment. |
| `24_cinematic_anchor_pitch_tilt.json` | Perspective image-warp preview. |

Several concepts are explicitly style previews because the native map source
does not yet contain their required evidence data: historical imagery, a DEM,
road-network isochrones, verified incident samples, cable/pipeline routes or
operational telemetry. The HUD marks those limits in-frame; do not present
their procedural shapes as measured geographic analysis. The four-panel,
pitch/tilt and great-circle examples are also viewport/image-space previews,
not independent pitched-camera terrain renders.

Every render applies a precomputed screen-space vignette and deterministic
frame grain. The route's `start_us`/`travel_us` schedule uses the same render
timebase as an accompanying narration or soundtrack.

```sh
build/plugins/maps/maps_dynamic_map_render \
  --input plugins/maps/showcases/01_spy_satellite_dive_lock.json \
  --output out/map-showcases/01_spy_satellite_dive_lock.mp4
```

The camera remains a nadir Web Mercator view, so the tile cover is the exact
viewport rectangle at each sampled zoom. The C++ modules stay small and
library-independent. Design references are [MapLibre Native tile
cover](https://github.com/maplibre/maplibre-native/blob/main/src/mln/util/tile_cover.cpp)
for future pitched-camera coverage, [Tangram ES](https://github.com/tangrams/tangram-es)
for asynchronous tile loading and in-flight deduplication,
[GDAL/OGR geometry](https://github.com/OSGeo/gdal/blob/master/ogr/ogrgeometryfactory.cpp)
for geographic geometry handling, and
[Mapnik label placement](https://github.com/mapnik/mapnik/tree/master/src/text)
for future label-collision work. No upstream implementation code or large
dependency is embedded here.
