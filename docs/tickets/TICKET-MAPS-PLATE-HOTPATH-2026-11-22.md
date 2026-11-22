# TICKET-MAPS-PLATE-HOTPATH-2026-11-22 — map plate hot path

**Stato:** `OPEN` (2026-11-22)
**Owner:** maps plugin
**Origine:** audit Chronon 2026-11-22, voce CPLG-AUDIT-01
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

- [ ] Due job consecutivi sullo stesso catalogo: `parse_geojson` count = 1 per
  file, non 1 per job.
- [ ] Cache invalidata da un file modificato (mtime/size) e non condivisa tra
  cataloghi diversi.
- [ ] MP4/plate SHA invariato con cache attiva.
- [ ] Encode NVENC: qualità/bitrate nel contratto, speedup misurato, fallback
  CPU esplicito.
- [ ] Test `maps` e build del plugin; `[NOT RUN]` il 2026-11-22 per RAM
  occupata (nessun build lanciato).
