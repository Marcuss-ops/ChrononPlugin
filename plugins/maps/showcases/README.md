# Runtime map animation previews

The runtime renderer exposes several distinct geographic animations. The first
four examples are territory boundary reveals. Seven more show radar location,
satellite scan, directional territory fill, a light-beam marker, city network,
territory extrusion and satellite-to-editorial morph. The seven new previews
render at 1920×1080, 30 fps and four seconds, with a quick camera settle and an
early graphic reveal followed by a clean hold. `route_line` provides route travel;
`exponential_dive` supplies the fast orbital-style camera descent.
`territory_extrusion` is a 2D silhouette depth effect, not terrain geometry.
Topographic contours are not in the runtime set because no elevation model is
bundled.

Three reference-based previews add a Taurus ridge callout, a Europe/Asia atlas
with Turkey highlighted, and a dark Duisburg timeline marked August 2007.
They also render as four-second 1080p/30 MP4s.

`16_jerusalem_tactical_fill.mp4` is the tactical-grid treatment: a dark
screen-space grid, green territory fill, red bloom, white boundary and a
two-line place label. It reads the payload-supplied
`territory_geojson` FeatureCollection (the demo asset is
`assets/jerusalem.geojson`, selected by its `shapeName` property) and accepts
`territory_subtitle` for the second label line. The four-second 1080p/30 MP4 is
encoded with NVENC (`h264_nvenc`, `p6`, CQ 15).

Five experimental renderer treatments extend that same Jerusalem geometry:
`17_volumetric_laser_scan` (2.5D height-field blocks and a moving laser plane),
`18_vector_particle_flow` (directional agents and trails), `19_dark_mirror_diorama`
(perspective map plate over a dark reflective floor), `20_voronoi_heat_contours`
(seeded Voronoi cells, heat colours and animated contour lines) and
`21_holographic_postfx` (barrel distortion, chromatic offsets, scanlines, bloom
and tilt-shift). They are CPU/OpenCV approximations, not GPU compute or
screen-space reflections: the particle flow is a single-threaded CPU agent pass and
the mirror uses a perspective warp plus a flipped, blurred reflection. All five
read the same GeoJSON and render as four-second 1920×1080/30 NVENC MP4s.

`15_tag_style_showcase.mp4` is a typography-only sample with 15 named geographic
label presets, shown in vertical groups of five. The text and style are
selectable in `15_tag_style_showcase.json`. The presets are shared with the
RenderingGen map-pin `label_style.preset` field; for example:

```json
"label_style": {"preset": "cyan_glow"}
```

Available names are `saffron_serif`, `ice_serif`, `coral_sans`, `cyan_glow`,
`mint_sans`, `amber_condensed`, `fatface_coral`, `poppins_placard`,
`outlined_editorial`, `night_cyan`, `rose_display`, `lime_locator`,
`slate_minimal`, `orange_oswald` and `royal_glass`. Individual `label_style`
fields can override preset defaults. The shared definitions live in
`RenderingGen/renderinggen/internal/overlay/geographic_tag_styles.v1.json`.

Render the MP4 with:

```sh
python3 plugins/maps/showcases/render_tag_showcase.py \
  --config plugins/maps/showcases/15_tag_style_showcase.json \
  --catalog ../RenderingGen/renderinggen/internal/overlay/geographic_tag_styles.v1.json \
  --output out/map-showcases/15_tag_style_showcase.mp4
```

The 12-second, 1080p/30 sample is encoded with NVENC. It shows only the tags and
their selectable preset names, without a map.

Older visual experiments remain under `archive/` as reference material. Their
composition identifiers are absent from the runtime catalog and rejected by
`maps_dynamic_map_render`.

Render the current runtime set from the ChrononPlugin directory:

```sh
plugins/maps/showcases/render_all.sh
```

The outputs are written under `out/map-showcases/` as MP4s, telemetry JSON,
and final-frame PNG previews. Esri imagery requires network access on a cold
cache. Territory examples use the bundled geographic catalog. Route and city
network lines connect the supplied stops; they do not follow road data.
