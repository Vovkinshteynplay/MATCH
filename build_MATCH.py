from pathlib import Path
import sys, subprocess, shutil

ROOT  = Path(__file__).resolve().parent
DIST  = ROOT / "dist"
BUILD = ROOT / "build"

def run(cmd):
    print(">>", " ".join(map(str, cmd)), flush=True)
    subprocess.check_call(cmd)

def main():
    # … твоя логика подготовки assets_stage …

    # чистим/создаём каталоги вывода
    shutil.rmtree(DIST,  ignore_errors=True)
    shutil.rmtree(BUILD, ignore_errors=True)
    DIST.mkdir(parents=True, exist_ok=True)
    BUILD.mkdir(parents=True, exist_ok=True)

    cmd = [
        sys.executable, "-m", "PyInstaller",
        str(ROOT / "MATCH.py"),
        "--onedir",
        "--name", "MATCH",
        "--windowed",
        "--add-data", f"{(ROOT/'assets_stage')};assets" if sys.platform.startswith("win") else f"{(ROOT/'assets_stage')}:assets",
        "--distpath", str(DIST),          # <<< ЯВНО
        "--workpath", str(BUILD),         # <<< ЯВНО
        "--specpath", str(BUILD),         # <<< ЯВНО
    ]
    # (при необходимости) добавить --icon …

    run(cmd)

    out = DIST / "MATCH"
    print(f"[build] output dir: {out}  exists={out.exists()}", flush=True)
    if not out.exists():
        # жёстко завершаем, чтобы upload-артефакт не шёл впустую
        raise SystemExit("PyInstaller did not produce dist/MATCH. See log above.")

if __name__ == "__main__":
    main()

