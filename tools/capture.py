#!/usr/bin/env python3
"""Render a deterministic MoonBase screenshot with its hidden validation engine."""
import argparse
from pathlib import Path
import platform
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--units", type=int, default=8192)
    p.add_argument("--frames", type=int, default=600)
    p.add_argument("--capture-frame", type=int, default=120)
    p.add_argument("--width", type=int, default=1440)
    p.add_argument("--height", type=int, default=900)
    p.add_argument("--camera-x", type=float, default=0)
    p.add_argument("--camera-z", type=float, default=0)
    p.add_argument("--span", type=float, default=920)
    p.add_argument("--smoke-test", action="store_true")
    p.add_argument("--engine", type=Path)
    p.add_argument("--output", type=Path, default=ROOT / "screenshots/overview.png")
    args = p.parse_args()
    if not 1 <= args.capture_frame < args.frames:
        p.error("capture-frame must be positive and earlier than frames")
    host = "arm64-macos" if platform.machine() == "arm64" else "x86_64-macos"
    engine = args.engine or ROOT / f"build/{host}/dmengine_offscreen"
    if not engine.is_file():
        p.error("build the hidden engine first with tools/build.py --offscreen")
    output = args.output.expanduser().resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.suffix.lower() != ".ppm":
        try:
            from PIL import Image
        except ImportError:
            p.error("PNG conversion requires Pillow; use --output capture.ppm instead")
    with tempfile.TemporaryDirectory(prefix="moonbase-capture-") as temp:
        capture = Path(temp) / "frame.ppm"
        settings = {"display.focus_on_show": 0, "engine.run_while_iconified": 1,
                    "moonbase.offscreen": 1, "moonbase.units": args.units,
                    "moonbase.offscreen_width": args.width, "moonbase.offscreen_height": args.height,
                    "moonbase.exit_after_frames": args.frames, "moonbase.capture_frame": args.capture_frame,
                    "moonbase.capture_path": capture, "moonbase.smoke_test": int(args.smoke_test),
                    "moonbase.camera_x": args.camera_x, "moonbase.camera_z": args.camera_z,
                    "moonbase.camera_span": args.span}
        command = [str(engine), *(f"--config={key}={value}" for key, value in settings.items()),
                   str(ROOT / "build/default/game.projectc")]
        result = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        log = output.with_suffix(".log")
        log.write_text(result.stdout)
        if result.returncode or "ERROR:" in result.stdout or not capture.is_file():
            raise SystemExit(f"Capture failed (exit {result.returncode}); see {log}")
        if output.suffix.lower() == ".ppm":
            output.write_bytes(capture.read_bytes())
        else:
            Image.open(capture).save(output)
        print(f"Saved {output}")
        for line in result.stdout.splitlines():
            if "smoke:" in line:
                print(line)


if __name__ == "__main__":
    main()
