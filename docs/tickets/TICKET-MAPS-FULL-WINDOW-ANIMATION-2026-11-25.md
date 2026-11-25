# TICKET-MAPS-FULL-WINDOW-ANIMATION-2026-11-25 — area glow e route fino all'ultimo frame

- **Stato:** CLOSED (2026-11-25)
- **Priorità:** P1
- **Owner:** maps animation / renderer nativo
- **Origine:** audit 2026-11-24 delle animazioni mappa; evidenza r19 con
  `area_glow_pulse` medio = fine.
- **Repo:** ChrononPlugin

## Problema

`draw_area_glow` usava `sin(2π * linear_progress)`. A progresso 0,5 e 1,0 il
valore è zero in entrambi i casi, quindi il respiro dell'alone si fermava e la
seconda metà del clip era statica. `route_line` e `multi_leg_flight` usavano
già il progresso lineare per tutta la finestra, ma nessun test lo dimostrava
sull'ultimo frame.

## Fix

- `area_glow_pulse_phase(linear_progress, duration_us)` in
  `plugins/maps/src/map_story_effects_territory.cpp`: un impulso ogni ~2 s, con
  offset di mezzo ciclo (minimo 1,5 cicli), così metà e fine non coincidono
  nemmeno su finestre da un numero intero di secondi.
- `plugins/maps/src/map_plate_render_frames.cpp` calcola la fase dalla durata
  reale della mappa.
- `draw_area_glow` riceve la fase in cicli, non un progresso 0..1.
- `plugins/maps/tests/map_plate_render_test.cpp` verifica:
  - fasi e primitive diverse a metà/fine;
  - video 2 s 320×180@30 con camera fissa: frame 30 contro frame 59, delta
    medio RGB > 0,1;
  - la stessa prova per `route_line` (2 pin) e `multi_leg_flight` (3 pin).

## Evidenza

```sh
cd ChrononPlugin
cmake --build build --target maps_map_plate_render_test -j4
ctest --test-dir build -R 'maps_map_plate_render_test|maps_renderer_catalog_test' --output-on-failure
```

Risultato 2026-11-25: 2/2 PASS. `maps_map_plate_render_test` 5,46 s
(ffmpeg/libx264 CPU, nessuna GPU richiesta); `maps_renderer_catalog_test`
0,00 s.

## Criteri di accettazione

- [x] `area_glow_pulse` non ha più metà = fine.
- [x] `route_line` e `multi_leg_flight` hanno movimento misurabile a fine
  finestra.
- [x] Catalogo invariato: 26 composizioni, 2 camera,
  `label_animations=["none"]`.
- [x] Test ripetibile senza GPU.

## Limiti dichiarati

- `pin_timing` per-pin resta non portato: il renderer rifiuta un payload non
  vuoto; questa chiusura non lo introduce.
- Le composizioni story-based completano l'intro e tengono la posa finale. Un
  motion continuo anche lì è una decisione separata, con la relativa parità
  pixel.
- NVENC/SHA e hotpath restano in
  `TICKET-MAPS-PLATE-HOTPATH-2026-11-22` e
  `TICKET-MAPS-NVENC-SHA-GATE-2026-11-22`.
