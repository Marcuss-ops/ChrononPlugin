# Runtime map animation previews

The runtime renderer exposes a small set of geographic animations. This folder
contains three restrained previews: a slow camera approach with a Pavia
boundary reveal, a soft radius glow around Lake Como, and a slowly revealed
Pavia–Milan route. Each animation is driven by geographic features or pins and
uses the same camera zoom timeline as the map plate.

Older visual experiments remain under `archive/` as reference material. Their
composition identifiers are intentionally absent from the runtime catalog and
are rejected by `maps_dynamic_map_render`.

Render the current runtime set from the ChrononPlugin directory:

```sh
plugins/maps/showcases/render_all.sh
```

The outputs are written under `out/map-showcases/` as MP4s, telemetry JSON,
and final-frame PNG previews. Esri imagery requires network access on a cold
cache; the Pavia boundary comes from the bundled geographic catalog. The soft
circle and route are illustrations based on the supplied radius and stops,
not measured buffers or road-network routes.
