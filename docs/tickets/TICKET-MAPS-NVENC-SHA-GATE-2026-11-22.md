# TICKET-MAPS-NVENC-SHA-GATE-2026-11-22 — encode NVENC nativo e SHA MP4 invariato

**Stato:** `OPEN`
**Priorità:** P1
**Data:** 2026-11-22
**Owner:** maps encode
**Origine:** CPLG-AUDIT-01, gate NVENC/SHA residuo
**Padre:** [TICKET-MAPS-PLATE-HOTPATH-2026-11-22](TICKET-MAPS-PLATE-HOTPATH-2026-11-22.md)

## Problema

Il contratto accetta il profilo `h264_nvenc` certificato, ma il map plate
continua a encodare via `popen(ffmpeg)`. Il profilo NVENC cambia solo gli
argomenti della pipe: non usa un encoder NVENC nativo condiviso e non elimina
il costo CPU della pipe/`fwrite` RGB24.

Restano aperti quindi due gate del padre:

- encode NVENC nativo con qualità/bitrate nel contratto e speedup misurato;
- MP4/plate SHA invariato con cache e nuovo encoder.

## Evidenza

- `[CODICE]` `plugins/maps/src/map_plate_render_encoder.cpp`:
  `resolve_encoder_profile` certifica `h264_nvenc/gpu/p6/quality 15`;
  `map_plate_ffmpeg_command` costruisce comunque
  `/usr/bin/ffmpeg ... -f rawvideo -pix_fmt bgr24 ...`.
- `[CODICE]` `plugins/maps/src/map_plate_render_pipeline.cpp:377`:
  `std::FILE* encoder = ::popen(ffmpeg_cmd.c_str(), "w");`.
- `[CODICE]` `tools/map_gpu/map_gpu_renderer.cpp:457` ha un'altra pipe
  `popen`/ffmpeg NVENC.
- `[TEST]` `plugins/maps/tests/map_plate_render_test.cpp:224-241` esercita già
  il contratto NVENC a 320x180 (sotto il minimo NVENC 64x36 non è valido).
- `[AMBIENTE]` Questo host non espone una GPU NVENC utilizzabile; il gate
  stesso richiede un host GPU e va registrato come tale.

## Piano di chiusura

1. **Scegliere l'encoder condiviso.** Riutilizzare l'encoder NVENC nativo già
   usato dal job padre Chronon3d, senza introdurre una seconda authority di
   encoding.
2. **Branch esplicito.** In `map_plate_render_pipeline.cpp`:
   - `profile.gpu && codec == "h264_nvenc"` → encoder nativo;
   - `libx264` → pipe ffmpeg esistente come fallback CPU dichiarato.
3. **Formato frame.** Convertire il frame map plate (attualmente BGR24 verso la
   pipe) nel formato atteso dall'encoder nativo, preservando
   `bt709/tv`, profilo high 4.0, GOP 48, keyint 48, niente B-frame.
4. **Telemetria.** Nel summary scrivere `video_encoder`, `encode_wall_ms`,
   bitrate/quality e `encoder_path=native|pipe`.
5. **Test contratto.** Estendere `map_plate_render_test` per verificare che il
   profilo NVENC usi il path nativo e che il fallback CPU sia esplicito.
6. **SHA.** Generare lo stesso plate con encoder nativo e con il baseline
   certificato; confrontare SHA/bitstream e qualità. Se il bitstream NVENC non
   è bit-identico, il gate deve usare l'hash del frame decodificato o la
   tolleranza pixel già definita, non un'uguaglianza impossibile.

## Gate di accettazione

- [ ] Path NVENC certificato senza `popen`/ffmpeg.
- [ ] `popen` residuo solo nel fallback x264 esplicito.
- [ ] `map_plate_render_test` NVENC PASS a 320x180.
- [ ] Quality/bitrate nel contratto e nel summary.
- [ ] Speedup encode misurato su host GPU.
- [ ] MP4/plate SHA (o hash decodificato concordato) invariato rispetto al
      baseline; differenze solo se approvate e documentate.
- [ ] Fallback CPU disponibile e testato.

## Comandi di verifica

```bash
cd ChrononPlugin
rg -n 'popen|h264_nvenc|map_plate_ffmpeg_command' plugins/maps/src tools/map_gpu

ctest --test-dir build -R 'map_plate' --output-on-failure
# oppure il target maps che contiene map_plate_render_test

sha256sum <baseline.mp4> <nuovo.mp4>
```

## Rischi e rollback

- La conversione colore è il rischio principale: un frame BGR→NVENC sbagliato
  cambia i pixel. Il gate SHA/pixel è bloccante.
- Rollback: mantenere la pipe ffmpeg come fallback dietro
  `encoder_path=pipe`, senza cambiare il contratto del profilo.

## Definition of Done

Il ticket si chiude quando il profilo NVENC usa davvero l'encoder nativo, il
fallback CPU è esplicito, il test contratto passa e SHA/qualità sono
certificati su un host GPU. Sul presente host il ticket resta aperto con
evidenza “GPU NVENC non disponibile”.
