# TICKET-MAPS-PLATE-HOTPATH-2026-11-22 — map plate hot path

**Stato:** `IN PROGRESS` (2026-11-22)
**Owner:** maps plugin
**Origine:** audit Chronon 2026-11-22, voce CPLG-AUDIT-01
**Ticket figlio (NVENC/SHA):** [TICKET-MAPS-NVENC-SHA-GATE-2026-11-22](TICKET-MAPS-NVENC-SHA-GATE-2026-11-22.md)
**File:** `plugins/maps/src/map_plate_render_pipeline.cpp`,
`plugins/maps/src/map_plate_raster.cpp`, `plugins/maps/src/map_plate_render_payload.cpp`

## Problemi

1. **Solo CPU.** Il renderer nativo accetta `sampler == "cpu"`; qualunque altro
   sampler viene rifiutato anche se `tools/map_gpu/map_gpu_renderer.cpp` esiste.
2. **GeoJSON riparsato per job.** `VectorPlateRaster` costruisce e parsa
   `ne_50m_admin_0_countries.geojson` e
   `geoBoundaries_ITA_ADM2.simplified.geojson` a ogni richiesta. Anche i
   percorsi territory/continental rifanno `ifstream` + `parse_geojson` nel
   pipeline.
3. **Encode via `::popen(ffmpeg)`.** Il default è `libx264`; non usa l'encoder
   NVENC nativo condiviso e paga una pipe CPU + `fwrite` RGB24.

## Piano

1. Cache process-wide dei GeoJSON parsati, keyed da path + mtime + size, con
   budget e invalidazione quando il file cambia. Esporre
   `load_geojson_cached(path)` e usarlo in tutti i call site del plate.
2. Preferire il renderer GPU/NVENC quando disponibile; il path CPU resta
   fallback certificato. Misurare encode wall e qualità/bitrate.
3. Tenere il contratto payload chiuso: nessun nuovo sampler implicito; il
   sampler dichiarato deve essere onorato o rifiutato fail-closed.

## Gate

- [x] `[OK 2026-11-22]` Due job consecutivi sullo stesso catalogo:
  `maps_map_plate_raster_test` verifica 2 parse al primo raster e cache hit sui
  successivi (count non incrementato).
- [x] `[OK 2026-11-22]` `maps_geo_core_test` verifica cache hit e invalidazione
  dopo modifica di path/size/mtime; chiave canonica per file.
- [ ] MP4/plate SHA invariato con cache attiva.
- [ ] Encode NVENC: qualità/bitrate nel contratto, speedup misurato, fallback
  CPU esplicito.
- [x] `[OK 2026-11-22]` Build plugin (`ChrononPlugin/build`, `-j1`) e test
  `maps_geo_core_test` + `maps_map_plate_raster_test` PASS (0.65 s).
