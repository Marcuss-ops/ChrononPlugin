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
      src/                          # renderer nativo mappe, geo core
      tests/                        # test C++ del plugin
      tools/                        # tool Python (cartography) + map_native/
        map_native/                 # CLI maps_dynamic_map_render + gate di parità
    effects/              # ricette specialistiche immagine/video, renderizzate dal core
    typography/           # preset testuali e authoring delle caption
```

Le ricette d'effetto e glow vivono in `plugins/effects`; preset tipografici e
authoring delle caption vivono in `plugins/typography`. Chronon3D mantiene le
primitive di rendering, shaping e animazione generiche e le implementazioni
Vulkan condivise. Il dominio geo è mantenuto solo in `plugins/maps`.

## Regole

- **Il registry canonico resta in Chronon3D** (`ExtensionModule` /
  `ExtensionContext` / `ExtensionCatalog`). Questo repo **non duplica** i
  contratti di estensione: ogni plugin espone solo la sua interfaccia minima
  e si registra attraverso i meccanismi già esistenti.
- Un plugin può chiedere a ChrononMotion3D il movimento (es. il percorso di
  una camera) senza implementare il proprio interpolatore cinematico.
- Il plugin maps è il **renderer dinamico ufficiale** delle mappe
  (`maps_dynamic_map_render`, lane unica dal 2026-10-26): il renderer Python
  di ChrononTemplate è stato ritirato; build, runtime e test C++ predefiniti
  non richiedono Python. La pipeline è
  `dominio → geometria → animazioni campionate → renderer mappe → overlay`;
  il renderer espone il contratto CLI e la telemetria certificata
  (`chronon.dynamic-map-telemetry.v1`) senza duplicare i contratti di
  estensione. Le composizioni non ancora portate falliscono chiuso con errore
  tipizzato; le gate di parità bit-exact vivono in `plugins/maps/tools/map_native/`.
- Per le mappe satellitari `basemap: "esri_sat"` usa Esri World Imagery in
  runtime: le tile Web Mercator vengono scaricate in C++, decodificate e
  ricampionate con OpenCV, poi riutilizzate dalla cache
  `~/.cache/chrononplugin/maps/esri_sat`. Il renderer calcola prima l'unione
  esatta delle tile visibili lungo la traiettoria e scarica i cache miss con un
  batch concorrente (fino a 32 trasferimenti; HTTP/2 quando negoziato dal CDN),
  prima di avviare l'encoder. Un tuffo
  Roma 5.2→17.8 a 120 frame ha richiesto 855 tile distribuite ai livelli 5–18
  (24–77 per livello, non 855 allo zoom massimo): 15,6 MB scaricati in 1,0 s,
  2,37 s end-to-end a cache fredda e 1,76 s a cache calda. L'edge testato ha
  negoziato HTTP/1.1 anche con HTTP/2 richiesto. La variabile
  `CHRONON_ESRI_TILE_CACHE_DIR` permette di scegliere un'altra cache.
- Ogni plugin è costruibile senza ricompilare Chronon3D.

## Build e test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

I test di riferimento che confrontano in modo bit-exact il renderer nativo
con l'implementazione Python precedente sono opzionali e si attivano con
`-DCHRONONPLUGIN_ENABLE_PYTHON_MAP_REFERENCE_TESTS=ON`. Non fanno parte del
runtime di Chronon.

**Requisito**: `libtbb.so.2` (ABI TBB 2) quando si usa la OpenCV statica del
bootstrap; con un OpenCV di sistema completo il requisito non si applica.

Le dipendenze del plugin mappe (OpenCV headers, Clipper2, nlohmann_json)
sono risolte da `plugins/maps/CMakeLists.txt`; in assenza di un OpenCV di
sistema, `plugins/maps/tools/bootstrap_opencv_dev.sh` materializza gli header
in `plugins/maps/third_party/` (path locale, non versionato).

Per buildare senza OpenCV e usare il checkout Chronon3D vicino:

```bash
cmake -S . -B build/specialized -DCHRONONPLUGIN_BUILD_MAPS=OFF \
  -DCHRONONPLUGIN_BUILD_EFFECTS=ON -DCHRONONPLUGIN_BUILD_TYPOGRAPHY=ON \
  -DCHRONON3D_SOURCE_DIR=../Chronon3d
cmake --build build/specialized -j12
```
