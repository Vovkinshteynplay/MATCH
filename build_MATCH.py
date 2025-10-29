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

    # Определи имя папки в dist так же, как его передаёшь в PyInstaller
app_name = "MATCH"  # или возьми из аргумента: getattr(args, "name", "MATCH")
app_dir = DIST / app_name

# Диагностика на всякий случай
print(f"[build] app_dir: {app_dir} exists={app_dir.exists()}")
if not app_dir.exists():
    # Показать, что вообще лежит в dist (поможет понять, если имя другое)
    print("[build] dist listing:", list(DIST.glob("*")))
    raise SystemExit(f"Expected dist/{app_name} not found. PyInstaller may have failed or name differs.")

# === Linux-only readme ===
if platform.system() == "Linux":
    # гарантируем наличие каталога (на случай пустой сборки)
    app_dir.mkdir(parents=True, exist_ok=True)

    try:
        version_str = version  # если ранее задавал переменную version
    except NameError:
        version_str = "dev"

    readme_text = f"""\
==============================
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
To add MATCH to your Linux applications menu or WSL Start Menu:
   mkdir -p ~/.local/share/applications
   cp match.desktop ~/.local/share/applications/
   chmod +x ~/.local/share/applications/match.desktop
Then restart your session or WSL.

🎧  SOUND
---------------------------------------
On native Linux distributions sound works automatically.
If you are using WSL2 with WSLg and have no sound, add this to ~/.bashrc:
   export PULSE_SERVER=unix:/mnt/wslg/PulseServer
   export SDL_AUDIODRIVER=pulse
Then restart WSL.

🐧  ITCH.IO LAUNCHER
---------------------------------------
When using the itch.io app, just click “Launch” — it will automatically
set permissions and run the correct binary.
No manual configuration is required.

🕹  ABOUT
---------------------------------------
MATCH is a minimalist competitive puzzle game.
Version: {version_str}
Developer: Vovkinshteynplay
"""

    readme_path = app_dir / "readme.txt"
    readme_path.write_text(readme_text, encoding="utf-8")
    print(f"[build] (Linux) wrote {readme_path}")
    
    out = DIST / "MATCH"
    print(f"[build] output dir: {out}  exists={out.exists()}", flush=True)
    if not out.exists():
        raise SystemExit("dist/MATCH not produced")

if __name__ == "__main__":
    main()
