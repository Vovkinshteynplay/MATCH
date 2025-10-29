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
    Version: 0.7.2
    Developer: Vovkinshteyn
    