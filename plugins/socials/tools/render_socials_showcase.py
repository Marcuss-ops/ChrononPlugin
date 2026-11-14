#!/usr/bin/env python3
"""Render, verify and upload the Socials showcase.

The four plans are authored in C++ by
`chrononplugin_emit_socials_showcase` (Socials plugin). This script only
orchestrates: validate each RenderPlan V3, render through Chronon3D's strict
GPU lane, verify 1920x1080 / 30 fps / frame count / stills, then upload the
MP4s and the upload manifest to the configured Drive folder.

Usage:
    python3 ChrononPlugin/plugins/socials/tools/render_socials_showcase.py \
        --emit --render --upload
"""

from __future__ import annotations

import sys as _velox_sys
from pathlib import Path as _VeloxPath
_velox_sys.path.insert(0, str(_VeloxPath(__file__).resolve().parents[4] / "tools"))
from stack_config import overlay_root as _overlay_root  # noqa: E402

import argparse
import hashlib
import json
import os
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

from PIL import Image, ImageChops, ImageStat

PLUGIN = Path(__file__).resolve().parents[1]
REPO = PLUGIN.parents[2]
OUT = REPO / "RenderingGen" / "UploadDrive" / "socials_structures_20261109"
SERIES = OUT / "plans" / "series.json"
CLI = REPO / "Chronon3d/build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli"
ASSETS = REPO / "Chronon3d"
EMITTER = REPO / "ChrononPlugin/build/plugins/socials/chrononplugin_emit_socials_showcase"
UPLOADER = REPO / "RenderingGen" / "bin" / "drive-upload"
CREDENTIALS = Path(os.environ.get(
    "VELOX_CREDENTIALS_FILE", "~/.config/velox/credentials.json")).expanduser()
TOKEN = Path(os.environ.get(
    "VELOX_TOKEN_FILE", "~/.config/velox/token.json")).expanduser()
DRIVE_FOLDER = _overlay_root()
DRIVE_URL = f"https://drive.google.com/drive/u/1/folders/{DRIVE_FOLDER}"
MANIFEST_NAME = "Socials_Structures_1920x1080_UPLOAD-MANIFEST.md"

WIDTH, HEIGHT, FPS = 1920, 1080, 30


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run_emitter(path: Path) -> None:
    if not path.is_file():
        raise SystemExit(f"emitter not found: {path}")
    result = subprocess.run(
        [str(path), str(OUT)],
        text=True,
        capture_output=True,
        env={**os.environ, "VELOX_DRIVE_OVERLAY_ROOT": DRIVE_FOLDER},
    )
    if result.returncode != 0:
        raise SystemExit(f"emitter failed:\n{result.stdout}\n{result.stderr}")
    print(result.stdout.strip())


def merge_existing(entries: list[dict[str, Any]]) -> None:
    if not SERIES.is_file():
        return
    try:
        previous = json.loads(SERIES.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return
    by_id = {clip["id"]: clip for clip in previous.get("clips", [])}
    for entry in entries:
        old = by_id.get(entry["id"])
        if not old:
            continue
        for key in (
            "width", "height", "fps", "frames", "duration_s", "bytes", "sha256",
            "verified", "verified_at", "foreground_ratio", "foreground_ratios",
            "stills", "uploaded", "drive_id", "drive_url",
        ):
            if key in old:
                entry[key] = old[key]


def load_series() -> tuple[list[dict[str, Any]], dict[str, Any]]:
    if not SERIES.is_file():
        raise SystemExit(
            f"missing {SERIES}\n"
            "run the C++ emitter first (or pass --emit):\n"
            f"  {EMITTER} {OUT}"
        )
    try:
        series = json.loads(SERIES.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise SystemExit(f"invalid {SERIES}: {error}") from error
    if series.get("generator") != "chrononplugin_emit_socials_showcase":
        raise SystemExit(f"{SERIES} was not emitted by the Socials plugin")
    canvas = series.get("canvas", {})
    if (canvas.get("width"), canvas.get("height"), canvas.get("fps")) != (
        WIDTH, HEIGHT, FPS
    ):
        raise SystemExit(f"unexpected series canvas: {canvas}")

    entries = series.get("clips", [])
    if len(entries) != 4:
        raise SystemExit(f"expected 4 clips in {SERIES}, found {len(entries)}")

    seen: set[str] = set()
    for entry in entries:
        clip_id = entry.get("id", "")
        if not clip_id or clip_id in seen:
            raise SystemExit(f"missing/duplicate clip id: {clip_id!r}")
        seen.add(clip_id)
        plan = OUT / entry["plan"]
        if not plan.is_file():
            raise SystemExit(f"missing C++ plan for {clip_id}: {plan}")
        if not 1 <= int(entry.get("frames", 0)) <= 10 * FPS:
            raise SystemExit(f"{clip_id}: duration is outside 1..10 s")
        if not entry.get("mp4") or not entry.get("drive_name"):
            raise SystemExit(f"incomplete series entry for {clip_id}")
        if not str(entry["mp4"]).endswith("_1920x1080_8s.mp4"):
            raise SystemExit(f"{clip_id}: unexpected output name {entry['mp4']}")
        try:
            entry["_plan"] = json.loads(plan.read_text(encoding="utf-8"))
        except json.JSONDecodeError as error:
            raise SystemExit(f"invalid plan {plan}: {error}") from error

    merge_existing(entries)
    print(f"SERIES {SERIES.relative_to(REPO)} ({len(entries)} clips from C++)")
    return entries, series


def validate(plan: Path) -> None:
    result = subprocess.run(
        [str(CLI), "validate", "--plan", str(plan), "--assets-root", str(ASSETS)],
        cwd=REPO,
        text=True,
        capture_output=True,
    )
    if result.returncode != 0:
        tail = (result.stdout + result.stderr).strip().splitlines()[-12:]
        raise SystemExit(f"validation failed for {plan.name}: " + " | ".join(tail))


def render(entry: dict[str, Any]) -> None:
    plan = OUT / entry["plan"]
    video = OUT / entry["mp4"]
    video.parent.mkdir(parents=True, exist_ok=True)
    log = OUT / "logs" / f"{entry['id']}.log"
    log.parent.mkdir(parents=True, exist_ok=True)
    command = [
        str(CLI), "render",
        "--plan", str(plan),
        "--assets-root", str(ASSETS),
        "--backend", "vulkan",
        "--gpu-hot-path-mode", "require_gpu_native",
        "-o", str(video),
    ]
    validate(plan)
    started = time.time()
    with log.open("w", encoding="utf-8") as handle:
        handle.write(" ".join(command) + "\n\n")
        handle.flush()
        # Run beside the mounted assets so Chronon3D can discover the bundled
        # font fallback directory (assets/fonts) for symbol coverage.
        result = subprocess.run(command, cwd=ASSETS, stdout=handle, stderr=subprocess.STDOUT)
    if result.returncode != 0:
        tail = log.read_text(encoding="utf-8", errors="replace").strip().splitlines()[-12:]
        raise SystemExit(f"render failed for {entry['id']}: " + " | ".join(tail))
    print(f"RENDER {entry['id']} in {time.time() - started:.1f}s")


def probe(video: Path) -> dict[str, Any]:
    result = subprocess.run(
        ["ffprobe", "-v", "error", "-count_frames", "-select_streams", "v:0",
         "-show_entries", "stream=width,height,nb_read_frames,r_frame_rate:format=duration,size",
         "-of", "json", str(video)],
        check=True, text=True, capture_output=True,
    )
    data = json.loads(result.stdout)
    stream = data["streams"][0]
    return {
        "width": int(stream["width"]),
        "height": int(stream["height"]),
        "fps": stream.get("r_frame_rate", "30/1"),
        "frames": int(stream.get("nb_read_frames") or 0),
        "duration_s": round(float(data["format"]["duration"]), 3),
        "bytes": int(data["format"]["size"]),
    }


def still(video: Path, path: Path, seconds: float) -> Image.Image:
    path.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        ["ffmpeg", "-y", "-v", "error", "-ss", f"{seconds:.3f}", "-i", str(video),
         "-frames:v", "1", "-update", "1", str(path)],
        check=True, cwd=REPO,
    )
    with Image.open(path) as image:
        return image.convert("RGB").copy()


def backdrop_rgb(plan: dict[str, Any]) -> tuple[int, int, int]:
    color = plan["layers"][0]["color"]
    return tuple(int(round(channel * 255)) for channel in color[:3])


def verify(entry: dict[str, Any]) -> None:
    video = OUT / entry["mp4"]
    info = probe(video)
    if (info["width"], info["height"]) != (WIDTH, HEIGHT):
        raise SystemExit(
            f"{entry['id']}: expected 1920x1080, got {info['width']}x{info['height']}"
        )
    if info["frames"] != entry["frames"]:
        raise SystemExit(
            f"{entry['id']}: expected {entry['frames']} frames, got {info['frames']}"
        )
    expected = entry["frames"] / FPS
    if abs(info["duration_s"] - expected) > 0.15:
        raise SystemExit(
            f"{entry['id']}: expected {expected:.2f}s, got {info['duration_s']:.2f}s"
        )

    background = Image.new("RGB", (WIDTH, HEIGHT), backdrop_rgb(entry["_plan"]))
    ratios = []
    for tag, fraction in (("early", 0.18), ("mid", 0.55), ("late", 0.88)):
        frame = still(
            video, OUT / "stills" / f"{entry['id']}_{tag}.png", expected * fraction
        )
        diff = ImageChops.difference(frame, background).convert("L")
        changed = sum(diff.histogram()[40:]) / float(WIDTH * HEIGHT)
        if changed < 0.002:
            raise SystemExit(
                f"{entry['id']}: empty frame at {fraction:.0%} ({changed:.4%})"
            )
        if max(ImageStat.Stat(frame).stddev) < 2.0:
            raise SystemExit(f"{entry['id']}: flat frame at {fraction:.0%}")
        ratios.append(round(changed, 6))

    entry.update(
        width=info["width"],
        height=info["height"],
        fps=info["fps"],
        frames=info["frames"],
        duration_s=info["duration_s"],
        bytes=info["bytes"],
        sha256=sha256(video),
        verified=True,
        verified_at=time.strftime("%Y-%m-%dT%H:%M:%S"),
        foreground_ratio=ratios[1],
        foreground_ratios=ratios,
        stills=[f"stills/{entry['id']}_{tag}.png" for tag in ("early", "mid", "late")],
    )
    print(f"VERIFY {entry['id']} {info['width']}x{info['height']} {info['frames']}f fg={ratios}")


def upload(local: Path, name: str) -> dict[str, str]:
    for required in (UPLOADER, CREDENTIALS, TOKEN):
        if not required.exists():
            raise SystemExit(f"missing Drive prerequisite: {required}")
    result = subprocess.run(
        [str(UPLOADER), "-credentials", str(CREDENTIALS), "-token", str(TOKEN),
         "-folder", DRIVE_FOLDER, "-file", str(local), "-name", name,
         "-sha256", sha256(local)],
        text=True,
        capture_output=True,
        timeout=600,
    )
    output = (result.stdout + "\n" + result.stderr).strip()
    (OUT / "upload_logs").mkdir(parents=True, exist_ok=True)
    (OUT / "upload_logs" / f"upload_{name}.log").write_text(
        output + "\n", encoding="utf-8"
    )
    if result.returncode != 0 or "DRIVE_UPLOAD_PASS" not in output:
        raise SystemExit(f"Drive refused {name}: {output[-1200:]}")
    fields = {
        part.split("=", 1)[0]: part.split("=", 1)[1]
        for part in output.split()
        if "=" in part
    }
    return {"id": fields.get("id", ""), "link": fields.get("link", "")}


def write_manifest(entries: list[dict[str, Any]], manifest_url: str = "") -> None:
    manifest = {
        "schema": "chrononplugin.socials-showcase.v1",
        "date": time.strftime("%Y-%m-%d"),
        "generator": "chrononplugin_emit_socials_showcase",
        "canvas": {"width": WIDTH, "height": HEIGHT, "fps": FPS},
        "stack": {
            "authoring": "C++ RenderPlan V3 social layouts",
            "surfaces": "native SDF rounded_rect / ellipse layers (18-layer budget)",
            "motion": "opacity, position, scale, rotation_z, progress tracks",
            "typography": "native Inter text + DejaVu symbol glyphs",
            "backend": "vulkan / require_gpu_native / nvenc",
        },
        "drive": {"folder_id": DRIVE_FOLDER, "folder_url": DRIVE_URL, "placement": "root"},
        "clips": [{key: value for key, value in entry.items() if key != "_plan"}
                  for entry in entries],
    }
    if manifest_url:
        manifest["upload_manifest_url"] = manifest_url
    (OUT / "manifest.json").write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )

    lines = [
        "# Socials structures - 1920x1080",
        "",
        "Quattro strutture social autorate in C++ come RenderPlan V3:",
        "X, Reddit, YouTube e TikTok. Superfici SDF native, testo Inter +",
        "simboli DejaVu, animazioni opacity / position / scale / progress.",
        "",
        f"Drive: {DRIVE_URL}",
        "",
        "| # | Struttura | Durata | Clip |",
        "|---|---|---|---|",
    ]
    for entry in entries:
        link = f"[apri]({entry['drive_url']})" if entry.get("drive_url") else "-"
        lines.append(
            f"| {entry['id'][:2]} | {entry['label']} | {entry['duration_s']:.0f}s | {link} |"
        )
    lines += [
        "",
        "## Ricetta",
        "",
        "```text",
        "canvas 1920x1080, 30 fps, 8 s, fade finale",
        "X: card out_back 94% -> 100%, due righe a comparsa, progress line",
        "Reddit: cascata a tre livelli, thread line scale_y con origine in alto, upvote pulse",
        "YouTube: play, progress rossa, dimming, comment card out_back in primo piano",
        "TikTok: phone 9:16, disco in rotazione, double-tap heart burst",
        "```",
        "",
        "Contenuti fictional, dichiarati ILLUSTRATIVE MOCKUP.",
        "",
    ]
    (OUT / "UPLOAD-MANIFEST.md").write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emit", action="store_true")
    parser.add_argument("--emitter", default=str(EMITTER))
    parser.add_argument("--render", action="store_true")
    parser.add_argument("--verify", action="store_true",
                        help="verify existing MP4s without re-rendering")
    parser.add_argument("--upload", action="store_true")
    parser.add_argument("--only", default="")
    args = parser.parse_args()

    if not CLI.is_file():
        raise SystemExit(f"chronon3d_cli not found: {CLI}")
    if args.emit:
        run_emitter(Path(args.emitter))

    entries, _ = load_series()
    selected = entries
    if args.only:
        wanted = [part.strip() for part in args.only.split(",") if part.strip()]
        selected = [e for e in entries if any(e["id"].startswith(w) for w in wanted)]
        if not selected:
            raise SystemExit("--only matched no clip")

    if args.verify:
        for entry in selected:
            video = OUT / entry["mp4"]
            if not video.is_file():
                raise SystemExit(f"missing video for {entry['id']}: {video}")
            verify(entry)
            write_manifest(entries)

    if args.render:
        for entry in selected:
            render(entry)
            verify(entry)
            write_manifest(entries)

    if args.upload:
        missing = [e["id"] for e in entries if not e.get("verified")]
        if missing:
            raise SystemExit(f"upload needs verified clips: {', '.join(missing)}")
        for entry in entries:
            if entry.get("uploaded") and entry.get("drive_url"):
                print(f"DRIVE_SKIP {entry['id']}")
                continue
            result = upload(OUT / entry["mp4"], entry["drive_name"])
            entry.update(uploaded=True, drive_id=result["id"], drive_url=result["link"])
            write_manifest(entries)
            print(f"DRIVE_PASS {entry['id']} {result['link']}")

        manifest_url = ""
        manifest_path = OUT / "UPLOAD-MANIFEST.md"
        write_manifest(entries)
        result = upload(manifest_path, MANIFEST_NAME)
        manifest_url = result["link"]
        print(f"DRIVE_PASS manifest {manifest_url}")

    write_manifest(entries)
    print(f"OUT {OUT.relative_to(REPO)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
