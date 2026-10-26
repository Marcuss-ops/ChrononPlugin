#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
maps_dir="$(cd -- "$script_dir/.." && pwd)"
prefix="$maps_dir/third_party/opencv-dev/prefix"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

command -v apt-get >/dev/null || { echo "apt-get is required" >&2; exit 1; }
command -v dpkg-deb >/dev/null || { echo "dpkg-deb is required" >&2; exit 1; }

# The distro static OpenCV is built against the TBB 2 task ABI, which
# libtbb.so.12 (TBB 2021) does not export. The plugin build links libtbb.so.2
# explicitly (see plugins/maps/CMakeLists.txt), so it must exist on the system.
if ! ldconfig -p 2>/dev/null | grep -q 'libtbb.so.2 ' && \
   [[ ! -e /usr/lib/x86_64-linux-gnu/libtbb.so.2 ]]; then
    echo "libtbb.so.2 not found: the static OpenCV bootstrap requires the TBB 2 ABI" >&2
    echo "(install libtbb2, or install a full system OpenCV instead of the bootstrap)" >&2
    exit 1
fi

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
