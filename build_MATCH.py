from pathlib import Path
import sys, subprocess, shutil, os, platform

ROOT  = Path(__file__).resolve().parent
DIST  = ROOT / "dist"
BUILD = ROOT / "build"
COMMON = ROOT / "assets_common"
STAGE  = ROOT / "assets_stage"

def run(cmd):
    print(">>", " ".join(map(str, cmd)), flush=True)
    subprocess.check_call(cmd)

def main():
    # чистим и создаём папки вывода
    shutil.rmtree(DIST,  ignore_errors=True)
    shutil.rmtree(BUILD, ignore_errors=True)
    shutil.rmtree(STAGE, ignore_errors=True)
    DIST.mkdir(parents=True, exist_ok=True)
    BUILD.mkdir(parents=True, exist_ok=True)

    # обязательно подготовим STAGE из COMMON
    if COMMON.is_dir():
        shutil.copytree(COMMON, STAGE)
    else:
        raise SystemExit("assets_common/ not found; cannot build")

    sep = ";" if platform.system() == "Windows" else ":"

    cmd = [
        sys.executable, "-m", "PyInstaller",
        str(ROOT / "MATCH.py"),
        "--onedir",
        "--name", "MATCH",
        "--windowed",
        "--add-data", f"{STAGE}{sep}assets",
        "--distpath", str(DIST),
        "--workpath", str(BUILD),
        "--specpath", str(BUILD),
        "--log-level", "DEBUG",
    ]
    # Если хочешь добавить иконку под Windows/macOS:
    win_ico = ROOT / "assets_win" / "icon.ico"
    mac_icns = ROOT / "assets_mac" / "icon.icns"
    if platform.system() == "Windows" and win_ico.exists():
        cmd += ["--icon", str(win_ico)]
    if platform.system() == "Darwin" and mac_icns.exists():
        cmd += ["--icon", str(mac_icns)]

    # для диагностики выведем, что реально в STAGE
    print("[diagnostics] STAGE exists:", STAGE.exists(), "count:", sum(1 for _ in STAGE.rglob('*')))
    run(cmd)

    out = DIST / "MATCH"
    print(f"[build] output dir: {out}  exists={out.exists()}", flush=True)
    if not out.exists():
        raise SystemExit("dist/MATCH not produced")

if __name__ == "__main__":
    main()
