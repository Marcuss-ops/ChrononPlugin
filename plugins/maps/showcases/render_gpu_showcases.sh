#!/usr/bin/env bash
set -euo pipefail

renderer=${1:-build-native-maps/plugins/maps/maps_gpu_render}
output_dir=${2:-out/map-showcases}
mkdir -p "$output_dir"

"$renderer" \
  --scene volumetric \
  --input plugins/maps/showcases/assets/jerusalem.geojson \
  --feature Jerusalem \
  --output "$output_dir/22_gpu_volumetric_scan.mp4" \
  --summary-output "$output_dir/22_gpu_volumetric_scan.gpu.json"

"$renderer" \
  --scene particles \
  --input plugins/maps/showcases/assets/jerusalem.geojson \
  --feature Jerusalem \
  --output "$output_dir/23_gpu_particle_flow.mp4" \
  --summary-output "$output_dir/23_gpu_particle_flow.gpu.json"
