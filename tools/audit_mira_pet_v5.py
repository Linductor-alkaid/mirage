#!/usr/bin/env python3
"""Validate the supplied v5 source and optionally render loop seam previews.

This is an offline development tool. It never changes source WebP files.
`--preview-dir` needs Pillow; the normal audit uses only the standard library.
`--preview-loops N` with N > 1 renders N repetitions of each loop segment at
the original 512x512 canvas into an mp4 (also needs ffmpeg) so the seam can be
watched repeatedly; GIF timing granularity makes long previews unwieldy.
"""

import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess


MANIFEST = (Path(__file__).resolve().parents[1] / "docs" / "compatibility" /
            "data" / "mira-pet-loop-candidates-v5-20261012.json")


def container(path: Path):
    data = path.read_bytes()
    if len(data) > 8 * 1024 * 1024 or data[:4] != b"RIFF" or data[8:12] != b"WEBP":
        raise ValueError(f"{path.name}: invalid or oversized WebP")
    if struct.unpack_from("<I", data, 4)[0] + 8 != len(data):
        raise ValueError(f"{path.name}: RIFF length mismatch")
    canvas = None
    loop_count = None
    durations = []
    offset = 12
    while offset + 8 <= len(data):
        kind = data[offset:offset + 4]
        size = struct.unpack_from("<I", data, offset + 4)[0]
        end = offset + 8 + size
        if end > len(data):
            raise ValueError(f"{path.name}: truncated {kind!r}")
        chunk = data[offset + 8:end]
        if kind == b"VP8X":
            canvas = (int.from_bytes(chunk[4:7], "little") + 1,
                      int.from_bytes(chunk[7:10], "little") + 1)
        elif kind == b"ANIM":
            loop_count = int.from_bytes(chunk[4:6], "little")
        elif kind == b"ANMF":
            durations.append(int.from_bytes(chunk[12:15], "little"))
        offset = end + size % 2
    if offset != len(data):
        raise ValueError(f"{path.name}: trailing bytes")
    return hashlib.sha256(data).hexdigest(), canvas, loop_count, durations


def check_clip(clip, source_dir: Path):
    path = source_dir / clip["file"]
    if path.is_symlink() or not path.is_file() or path.parent != source_dir:
        raise ValueError(f"{clip['state']}: invalid source path")
    digest, canvas, loop_count, durations = container(path)
    if (digest != clip["sha256"] or canvas != (512, 512) or loop_count != 0 or
            len(durations) != clip["frames"] or
            set(durations) != {clip["frame_duration_ms"]}):
        raise ValueError(f"{clip['state']}: source does not match approved v5")
    count = clip["frames"]
    if clip["playback"] == "once":
        if clip.get("once") != [0, count] or "loop" in clip:
            raise ValueError(f"{clip['state']}: invalid once range")
    elif clip["playback"] == "loop":
        intro, loop, outro = (clip[key] for key in ("intro", "loop", "outro"))
        if not (0 == intro[0] < intro[1] == loop[0] < loop[1] == outro[0]
                <= outro[1] == count):
            raise ValueError(f"{clip['state']}: invalid split range")
    else:
        raise ValueError(f"{clip['state']}: unknown playback")
    return path


def render_preview(clip, path: Path, preview_dir: Path, loops: int):
    from PIL import Image

    if clip["playback"] != "loop":
        return
    preview_dir.mkdir(parents=True, exist_ok=True)
    image = Image.open(path)
    begin, end = clip["loop"]
    if loops <= 1:
        frames = []
        for index in range(begin, end):
            image.seek(index)
            frame = image.convert("RGBA")
            frame.thumbnail((192, 192))
            background = Image.new("RGBA", (192, 192), "#e6e6e6")
            background.alpha_composite(frame, ((192 - frame.width) // 2,
                                               (192 - frame.height) // 2))
            frames.append(background.convert("RGB"))
        # GIF delays have 10 ms granularity; timing authority stays the WebP ANMF.
        frames[0].save(preview_dir / f"{clip['state']}_loop.gif", save_all=True,
                       append_images=frames[1:], duration=clip["frame_duration_ms"],
                       loop=0, optimize=False)
        return
    # Original-size multi-circle preview: mp4 keeps exact per-frame timing
    # (the container enforces one uniform ANMF duration per clip) and a
    # watchable file size; pixels are composited on the same light background.
    output = preview_dir / f"{clip['state']}_loop_x{loops}.mp4"
    encode = subprocess.Popen(
        ["ffmpeg", "-v", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgba",
         "-s", "512x512", "-r", f"1000/{clip['frame_duration_ms']}", "-i", "-",
         "-an", "-c:v", "libx264", "-crf", "12", "-preset", "medium",
         "-pix_fmt", "yuv420p", str(output)],
        stdin=subprocess.PIPE)
    for _ in range(loops):
        for index in range(begin, end):
            image.seek(index)
            frame = image.convert("RGBA")
            background = Image.new("RGBA", (512, 512), "#e6e6e6")
            background.alpha_composite(frame, ((512 - frame.width) // 2,
                                               (512 - frame.height) // 2))
            encode.stdin.write(background.tobytes())
    encode.stdin.close()
    if encode.wait() != 0:
        raise ValueError(f"{clip['state']}: ffmpeg preview encoding failed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", required=True, type=Path)
    parser.add_argument("--preview-dir", type=Path)
    parser.add_argument("--preview-loops", type=int, default=1,
                        help="repetitions of each loop segment in previews; "
                             ">1 renders original-size mp4 (needs ffmpeg)")
    args = parser.parse_args()
    source_dir = args.source_dir.resolve(strict=True)
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    states = [clip["state"] for clip in manifest["clips"]]
    if (manifest["schema_version"] != 1 or len(states) != 9 or len(set(states)) != 9 or
            set(states) != {"enter", "rest", "think", "look", "mouse", "typing",
                            "done", "fail", "exit"}):
        raise ValueError("invalid v5 clip manifest")
    for clip in manifest["clips"]:
        path = check_clip(clip, source_dir)
        if args.preview_dir:
            render_preview(clip, path, args.preview_dir, args.preview_loops)
        selected = clip.get("loop", clip.get("once"))
        print(f"{clip['state']:>6} {clip['frames']:>2} frames "
              f"{clip['frame_duration_ms']:>3} ms  {selected}  sha256 ok")


if __name__ == "__main__":
    main()
