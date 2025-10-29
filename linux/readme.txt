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
Version: main
Developer: Vovkinshteynplay
