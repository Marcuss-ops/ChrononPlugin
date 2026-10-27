#!/usr/bin/env bash
set -euo pipefail

renderer=${1:-build/plugins/maps/maps_dynamic_map_render}
output_dir=${2:-out/map-showcases}
mkdir -p "$output_dir"

for payload in plugins/maps/showcases/*.json; do
  name=${payload##*/}
  name=${name%.json}
  video="$output_dir/$name.mp4"
  "$renderer" --input "$payload" --output "$video" \
    --summary-output "$output_dir/$name.telemetry.json"
  ffmpeg -hide_banner -loglevel error -y -sseof -0.05 -i "$video" \
    -frames:v 1 -update 1 "$output_dir/$name.png"
done
