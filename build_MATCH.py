#!/usr/bin/env python3
"""Cross-platform PyInstaller build helper for MATCH."""

from __future__ import annotations

import argparse
import platform
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
DIST = ROOT / "dist"
BUILD = ROOT / "build"
ASSETS_STAGE = ROOT / "assets_stage"
ASSETS_COMMON = ROOT / "assets_common"

ASSET_VARIANTS = {
    "Windows": ROOT / "assets_win",
    "Darwin": ROOT / "assets_mac",
    "Linux": ROOT / "assets_linux",
}

ICONS = {
    "Windows": ROOT / "assets_win" / "icon.ico",
    "Darwin": ROOT / "assets_mac" / "icon.icns",
    "Linux": ROOT / "assets_linux" / "icon.png",
}


def run(cmd: list[str]) -> None:
    print(">>", " ".join(str(c) for c in cmd), flush=True)
    subprocess.check_call(cmd)


def prepare_stage(system: str) -> Path:
    """Create staged asset folder (common + per-platform)."""
    shutil.rmtree(ASSETS_STAGE, ignore_errors=True)
    if not ASSETS_COMMON.is_dir():
        raise SystemExit("assets_common/ not found; cannot build")
    shutil.copytree(ASSETS_COMMON, ASSETS_STAGE)

    variant_dir = ASSET_VARIANTS.get(system)
    if variant_dir and variant_dir.is_dir():
        shutil.copytree(variant_dir, ASSETS_STAGE, dirs_exist_ok=True)

    return ASSETS_STAGE


def ensure_dirs(clean: bool) -> None:
    if clean:
        shutil.rmtree(DIST, ignore_errors=True)
        shutil.rmtree(BUILD, ignore_errors=True)
    DIST.mkdir(parents=True, exist_ok=True)
    BUILD.mkdir(parents=True, exist_ok=True)


def build(args: argparse.Namespace) -> Path:
    system = platform.system()
    stage_dir = prepare_stage(system)
    sep = ";" if system == "Windows" else ":"
    add_data = f"{stage_dir}{sep}assets"

    cmd = [
        sys.executable,
        "-m",
        "PyInstaller",
        str(ROOT / "MATCH.py"),
        "--onedir",
        "--name",
        args.name,
        "--add-data",
        add_data,
        "--distpath",
        str(DIST),
        "--workpath",
        str(BUILD),
        "--specpath",
        str(BUILD),
    ]

    if args.windowed:
        cmd.append("--windowed")

    icon_path = ICONS.get(system)
    if icon_path and icon_path.exists():
        cmd.extend(["--icon", str(icon_path)])

    if args.log_level:
        cmd.extend(["--log-level", args.log_level])

    run(cmd)
    app_dir = DIST / args.name
    if not app_dir.exists():
        print("[build] dist contents:", list(DIST.glob("*")))
        raise SystemExit(f"Expected {app_dir} not found – PyInstaller may have failed.")
    return app_dir


def write_linux_readme(app_dir: Path, version: str | None) -> None:
    text = f"""==============================
 MATCH — Minimalist Puzzle Game
==============================

▶️  HOW TO RUN (Linux)
------------------------------
1. Extract the archive anywhere you like.
2. Make the game executable (if needed):
   chmod +x MATCH
3. Run the game:
   ./MATCH

💡  OPTIONAL — Add to Applications Menu
---------------------------------------
   mkdir -p ~/.local/share/applications
   cp match.desktop ~/.local/share/applications/
   chmod +x ~/.local/share/applications/match.desktop

🎧  SOUND
---------------------------------------
Using WSL2 + WSLg? Add to ~/.bashrc:
   export PULSE_SERVER=unix:/mnt/wslg/PulseServer
   export SDL_AUDIODRIVER=pulse

🕹  ABOUT
---------------------------------------
Version: {version or 'dev'}
Developer: Vovkinshteynplay
"""
    (app_dir / "readme.txt").write_text(text, encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description="Build MATCH via PyInstaller")
    parser.add_argument("--name", default="MATCH")
    parser.add_argument("--version", default=None)
    parser.add_argument("--clean", action="store_true")
    parser.add_argument("--windowed", action="store_true", default=True)
    parser.add_argument("--log-level", default=None)
    args = parser.parse_args()

    ensure_dirs(args.clean)
    app_dir = build(args)

    system = platform.system()
    if system == "Linux":
        write_linux_readme(app_dir, args.version)

    print("\n=== Build complete ===")
    print(f"System:       {system}")
    print(f"Output dir:   {app_dir}")
    if args.version:
        print(f"User version: {args.version}")
    print("======================")


if __name__ == "__main__":
    main()
