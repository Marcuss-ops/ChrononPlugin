# ChrononPlugin

Livello C dell'architettura Chronon: il catalogo dei **plugin specializzati**.

L'architettura a tre livelli:

| Livello | Repo | Possiede |
|---|---|---|
| **A — Chronon3D Core** | `Chronon3d` | RenderGraph, compiler, executor, backend software/Vulkan, compositing, testo, immagini, geometrie fondamentali, risorse/cache/encoding, registry delle capability e API di estensione. **Non sa cosa sia una mappa, un grafico o un documentario.** |
| **B — ChrononMotion3D** | `ChrononMotion3D` | Movimento: curve, keyframe, camere, rigging, trasformazioni, sequenze temporali, baking. |
| **C — ChrononPlugin** | questo repo | Le funzionalità specialistiche: ogni plugin è una libreria/tool autonomo con dipendenze, test e interfaccia pubblica minima verso Chronon. |

## Layout

```
ChrononPlugin/
  CMakeLists.txt          # build standalone del catalogo (C++20 + CTest)
  plugins/
    maps/                 # plugin mappe (famiglia geo/documentary maps)
      include/chrononplugin/maps/   # contratto pubblico del plugin
      src/                          # rasterizzatore tile/plate, geo core
      tests/                        # test C++ del plugin
      tools/                        # tool Python (cartography, parità raster)
```

Famiglie previste dal catalogo (slot futuri, nessun codice ancora):
`charts`, `particles`, `cinematic`, `typography`, `transitions`, `documentary`.

## Regole

- **Il registry canonico resta in Chronon3D** (`ExtensionModule` /
  `ExtensionContext` / `ExtensionCatalog`). Questo repo **non duplica** i
  contratti di estensione: ogni plugin espone solo la sua interfaccia minima
  e si registra attraverso i meccanismi già esistenti.
- Un plugin può chiedere a ChrononMotion3D il movimento (es. il percorso di
  una camera) senza implementare il proprio interpolatore cinematico.
- Un plugin non possiede un renderer di pixel parallelo: la pipeline è
  `dominio → geometria → animazioni campionate → RenderPlan → Chronon3D`.
  (Il rasterizzatore di `plugins/maps` è la base di parità bit-exact del
  porting nativo mappe, non un secondo renderer di prodotto.)
- Ogni plugin è costruibile senza ricompilare Chronon3D.

## Build e test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Le dipendenze del plugin mappe (OpenCV headers, Clipper2, nlohmann_json)
sono risolte da `plugins/maps/CMakeLists.txt`; in assenza di un OpenCV di
sistema, `plugins/maps/tools/bootstrap_opencv_dev.sh` materializza gli header
in `plugins/maps/third_party/` (path locale, non versionato).
