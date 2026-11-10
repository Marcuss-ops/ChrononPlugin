# Socials

Plugin di livello C per le **strutture social** renderizzabili da Chronon3D.
Non duplica il renderer: emette solo `chronon.render-plan.v3` e delega a
Chronon3D le superfici SDF native (`rounded_rect`, `ellipse`), il testo, il
compositing e l'encoder.

## Cosa contiene

| Struttura | Linguaggio visivo | Movimento |
|---|---|---|
| **X** | card scura, avatar, verificato, media panel | slide + scale con `out_back`, due righe a comparsa, progress line |
| **Reddit** | thread a tre livelli, upvote rail, thread line | cascata `0.12 s`, linee che crescono dall'alto, pulse upvote |
| **YouTube** | thumbnail 16:9 con play, progress rossa, comment highlight | barra che avanza, dimming e card commento in primo piano |
| **TikTok** | frame 9:16, action rail, disco audio | phone che entra, disco in rotazione, double-tap heart burst |

`motion.hpp` espone il vocabolario di animazione condiviso:

- `EaseOutExpo`, `EaseOutBack`, `DampedSpring`
- `AnimProperty` (tween a due chiavi in secondi)
- mapping verso i nomi wire RenderPlan (`out_expo`, `out_back`, `spring`)

`social_showcase.hpp` espone i modelli dati modificabili (`XPostSpec`,
`RedditThreadSpec`, `YouTubeSpec`, `TikTokSpec`) e i builder dei quattro piani.
I contenuti demo sono fictional e dichiarati `ILLUSTRATIVE MOCKUP`: nessuna
capture o credenziale di piattaforma viene usata.

I quattro piani restano entro **18 layer**: è il budget di superfici native che
il renderer attuale sostiene su una clip intera da 240 frame. Le decorazioni
sono ridotte all'osso e ogni glifo simbolico usa `DejaVuSans.ttf`, che copre
check, play, frecce e cuori senza fallback a runtime.

## Build e test

```bash
cmake -S ChrononPlugin -B ChrononPlugin/build -DCHRONONPLUGIN_BUILD_SOCIALS=ON
cmake --build ChrononPlugin/build --target chrononplugin_socials_tests \
  chrononplugin_emit_socials_showcase -j12
ctest --test-dir ChrononPlugin/build -R chrononplugin_socials --output-on-failure
```

## Mostra 1920x1080

```bash
ChrononPlugin/build/plugins/socials/chrononplugin_emit_socials_showcase \
  RenderingGen/UploadDrive/socials_structures_20261109

python3 ChrononPlugin/plugins/socials/tools/render_socials_showcase.py \
  --render --upload
```

Il driver valida i piani, renderizza con la lane Vulkan nativa, verifica
risoluzione/frame/durata, estrae tre still per clip e carica gli MP4 nella
cartella Drive indicata in `series.json` e nel manifest.
