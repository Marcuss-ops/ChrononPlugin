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

`24_data_bars_europe.json` is the animated data-bars variant: the same
validated `data` object as the choropleth, but values grow as a staggered bar
chart beside the map.

`25_label_choreography_europe.json` demonstrates five label motions in one
clip: rise, pop, slide, typewriter and spread, anchored to five projected pins.

`26_paper_map_europe.json` is the folded paper atlas. The editorial plate is
printed in sepia on a cream trifold sheet, the sheet opens with crease shadows
and a folded-cover tag, and a soft scan wipe then hands the composition to the
live colour map. It uses the base plate as the printed map, so no extra source
is required.

`27_globe_dive_europe.json` is the orbital descent. The Natural Earth country
collection is projected orthographically onto a starfield globe, which rotates
from Asia toward Europe while growing past the frame edges; the live map is
revealed underneath and the Europe label lands at the end.

Three new native C++ scenes extend the gallery with grounded Italian locations:
`28_venezia_laguna_satellite.json` scans the Venice lagoon and its municipal
outline from satellite imagery; `29_costiera_amalfitana_route.json` animates a
three-stop coastal itinerary from Napoli through Amalfi to Salerno; and
`30_italia_paper_atlas.json` folds open a printed atlas plate focused on Italy.
All three are 1080p/30 NVENC clips rendered by `maps_dynamic_map_render`.

`23_country_spotlight_italy.json` is the native spotlight treatment: the
surrounding map is desaturated, the named country keeps a warm fill, a soft
halo and a traced border, and the camera performs a full-window push-in.

`22_data_choropleth_europe.json` is the native data-map treatment: every
country in `data.values` is tinted on a reds/blues/greens/heat/purples/
diverging scale, the terrain stays visible, and a legend plus the highest-value
name/value labels are added after a short staggered reveal. It uses the same
Natural Earth polygons and camera contract as the other native compositions;
the `data` object is fail-closed (unknown fields, non-finite values and unknown
schemes are rejected).

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

## Select one of the 12 Italy map presets from a script

Each approved map has a stable, descriptive `preset_id` in
`plugins/maps/map-presets-catalog.v1.json`. Put that ID in the dynamic map
payload as `map_preset`; the CLI resolves its `map_animation` and `basemap` and
defaults `map_place` to Italy. You can override `map_place` to render a
different supported territory. Do not send a conflicting `map_animation` or
`basemap` alongside a preset; the renderer rejects the mismatch.

```json
{
  "width": 1920,
  "height": 1080,
  "fps_num": 24,
  "fps_den": 1,
  "duration_us": 4000000,
  "map_preset": "italia_editoriale_paper_cutout"
}
```

The complete set of IDs is:

```text
italia_atlante_continentale_turchia
italia_mappa_storica_duisburg
italia_scansione_satellitare_pavia
italia_riempimento_tattico_gerusalemme
italia_editoriale_voxel
italia_editoriale_paper_cutout
italia_editoriale_radar_scuro
italia_editoriale_connessioni_ad_arco
italia_editoriale_isola_fluttuante
italia_basemap_vettoriale_regionale
italia_basemap_chiara
italia_basemap_regionale_toscana
```

The complete script payload example is
`plugins/maps/showcases/map_preset_script_example.json`.

Render the current runtime set from the ChrononPlugin directory:

```sh
plugins/maps/showcases/render_all.sh
```

The outputs are written under `out/map-showcases/` as MP4s, telemetry JSON,
and final-frame PNG previews. Esri imagery requires network access on a cold
cache. Territory examples use the bundled geographic catalog. Route and city
network lines connect the supplied stops; they do not follow road data.
