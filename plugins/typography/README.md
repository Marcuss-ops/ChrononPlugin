# ChrononPlugin Typography

This module owns editorial text recipes and caption authoring helpers. Text
shaping, font resolution, glyph selection, animation evaluation, and rendering
remain in Chronon3D as shared runtime primitives.

Public recipes are available through `chrononplugin/typography.hpp`. The
`chrononplugin_typography` static library contains subtitle track authoring and
word emphasis recipe implementations. It consumes Chronon3D headers and links
with the Chronon3D SDK in applications.

`SubtitleTrackBuilder` accepts the current frame explicitly, so the plugin
does not need privileged access to `LayerBuilder` internals.
