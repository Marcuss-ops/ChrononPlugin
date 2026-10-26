# ChrononPlugin Effects

This plugin owns high-level image and video effect recipes. It builds
Chronon3D RenderPlans from the core's generic primitives; the core retains the
renderer, Vulkan kernels, and effect parameter contracts. No browser or CPU
pixel renderer is introduced here.

Public APIs: `chrononplugin/effects/presets.hpp` and
`chrononplugin/effects/analog_vintage.hpp`. The Vintage Paper recipe builds a
GPU native stack (warm radial backdrop, quantized line boil, RGB separation,
square scanline warp, two glow radii, vignette, and animated grain).
`make_film_dirt_overlay()` adds
deterministically placed white dust flecks and vertical scratches as small
screen-space layers whose opacity flickers at a configurable frame cadence.
The restrained canary glow recipe also lives here as `canary_black_glow()`;
Chronon3D retains only the generic `GlowParams` contract.

The `chrononplugin_effects` CMake target is header-only and consumes the
Chronon3D RenderPlan C++ headers from `CHRONON3D_SOURCE_DIR`. Link the
application with both the plugin target and the Chronon3D SDK/library.

To configure just this plugin without the maps plugin's OpenCV dependency:

```sh
cmake -S . -B build/effects -DCHRONONPLUGIN_BUILD_MAPS=OFF \
  -DCHRONONPLUGIN_BUILD_EFFECTS=ON \
  -DCHRONON3D_SOURCE_DIR=../Chronon3d
```
