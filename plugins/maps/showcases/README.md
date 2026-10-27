# Native documentary map showcases

Each payload below renders one distinct C++/OpenCV animation with Esri World
Imagery. Rendered MP4s and review PNGs are written under `out/map-showcases/`.

| Payload | Animation |
| --- | --- |
| `01_spy_satellite_dive_lock.json` | Exponential 7.0→18.2 dive, target reticle, live coordinate/altitude/GSD HUD and radial speed blur. |
| `02_crime_scene_radar.json` | Cold satellite grade, geodetic 1.5 km radius, hatch, dashed perimeter and pulsing radar rings. |
| `03_multi_leg_flight.json` | Roma–Firenze–Bologna–Venezia great-circle route, narration-time progress, bloom and altitude stems. |
| `04_territorial_polygon_wipe.json` | Pavia GeoJSON perimeter wipe with outside darkening, translucent fill and neon outline. |

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
