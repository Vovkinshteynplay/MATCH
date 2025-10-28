#!/usr/bin/env python3
import argparse
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent  # корень репо
DIST = ROOT / "dist"
BUILD = ROOT / "build"
SPEC = ROOT / "MATCH.spec"  # необязательно; собираем по .py
SRC_ENTRY = ROOT / "MATCH.py"  # точка входа
ASSETS_COMMON = ROOT / "assets_common"
ASSETS_STAGE = ROOT / "assets_stage"  # временная папка: попадёт в сборку как assets/

ICONS = {
    "Windows": ROOT / "assets_win" / "icon.ico",
    "Darwin":  ROOT / "assets_mac" / "icon.icns",
    "Linux":   ROOT / "assets_linux" / "icon.png",
}

def run(cmd: list[str]):
    print(">>", " ".join(str(c) for c in cmd))
    subprocess.check_call(cmd)

def main():
    p = argparse.ArgumentParser(description="Cross-platform PyInstaller build")
    p.add_argument("--name", default="MATCH", help="App name")
    p.add_argument("--version", default=None, help="User-visible version (only printed)")
    p.add_argument("--windowed", action="store_true", default=True, help="Windowed app")
    p.add_argument("--clean", action="store_true", help="Clean dist/build before build")
    p.add_argument("--pyinstaller", default="pyinstaller", help="PyInstaller executable")
    args = p.parse_args()

    system = platform.system()  # 'Windows' | 'Darwin' | 'Linux'
    if system not in ("Windows", "Darwin", "Linux"):
        print(f"Unsupported system: {system}", file=sys.stderr)
        sys.exit(2)

    # Подготовка
    if args.clean:
        shutil.rmtree(DIST, ignore_errors=True)
        shutil.rmtree(BUILD, ignore_errors=True)
    shutil.rmtree(ASSETS_STAGE, ignore_errors=True)

    # Cпуллим общие ассеты в stage → позже подмонтируем как 'assets'
    if ASSETS_COMMON.is_dir():
        shutil.copytree(ASSETS_COMMON, ASSETS_STAGE)
    else:
        print("WARNING: assets_common/ not found; continuing without assets", file=sys.stderr)

    add_data_sep = ";" if system == "Windows" else ":"
    add_data_arg = f"{ASSETS_STAGE}{add_data_sep}assets"

    icon = ICONS.get(system)
    if not icon or not icon.exists():
        print(f"WARNING: icon for {system} not found, proceeding without --icon", file=sys.stderr)
        icon = None

    # Базовая команда PyInstaller
    cmd = [
    sys.executable, "-m", "PyInstaller",   # ← вместо просто "pyinstaller"
    str(SRC_ENTRY),
    "--onedir",
    "--name", args.name,
    "--add-data", add_data_arg,            # на Windows: "C:\...\assets_stage;assets"
    "--windowed",
    ]
    if icon:
        cmd += ["--icon", str(icon)]


    # Запуск
    run(cmd)

    # Доп. шаги для *nix: права на бинарники
    app_dir = DIST / args.name
    if system in ("Darwin", "Linux"):
        # основной бинарь
        bin_path = app_dir / args.name
        if bin_path.exists():
            bin_path.chmod(0o755)
        # macOS .app
        mac_bin = app_dir / f"{args.name}.app" / "Contents" / "MacOS" / args.name
        if mac_bin.exists():
            mac_bin.chmod(0o755)

    print("\n=== Build complete ===")
    print(f"System:       {system}")
    print(f"Output dir:   {app_dir}")
    if args.version:
        print(f"User version: {args.version}")
    print("Stage assets: assets_common → dist/.../assets")
    print("======================\n")

if __name__ == "__main__":
    main()