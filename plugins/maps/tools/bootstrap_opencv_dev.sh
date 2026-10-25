#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
maps_dir="$(cd -- "$script_dir/.." && pwd)"
prefix="$maps_dir/third_party/opencv-dev/prefix"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

command -v apt-get >/dev/null || { echo "apt-get is required" >&2; exit 1; }
command -v dpkg-deb >/dev/null || { echo "dpkg-deb is required" >&2; exit 1; }

cd "$work"
apt-get download libopencv-dev libopencv-core-dev libopencv-imgproc-dev
mkdir -p "$prefix"
for package in ./*.deb; do
    dpkg-deb -x "$package" "$prefix"
done

config="$prefix/usr/lib/x86_64-linux-gnu/cmake/opencv4/OpenCVConfig.cmake"
if [[ ! -f "$config" ]]; then
    echo "OpenCV CMake config not found after extraction: $config" >&2
    exit 1
fi
echo "OpenCV development prefix ready: $prefix"
