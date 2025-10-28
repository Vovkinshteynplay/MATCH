import json
import os
import io
import sys
import ctypes
import random
import re
import time
import pygame
from dataclasses import dataclass
from datetime import datetime

# --- Resource path helpers (PyInstaller-friendly) ---
BASE_PATH = getattr(sys, '_MEIPASS', os.path.dirname(__file__))
APP_DIR = os.path.dirname(os.path.abspath(sys.argv[0]))
SAVE_ROOT = os.path.join(APP_DIR, "saves")

_ASSET_ROOTS = None


def _detect_platform_assets_dir() -> str | None:
    plat = sys.platform.lower()
    if plat.startswith("win"):
        return "assets_win"
    if plat == "darwin":
        return "assets_mac"
    if plat.startswith("linux"):
        return "assets_linux"
    return None


def _asset_roots() -> list[str]:
    global _ASSET_ROOTS
    if _ASSET_ROOTS is not None:
        return _ASSET_ROOTS

    roots: list[str] = []
    # Assets bundled by PyInstaller (assets/ inside _MEIPASS)
    bundle_assets = os.path.join(BASE_PATH, "assets")
    if os.path.isdir(bundle_assets):
        roots.append(bundle_assets)

    # Development-time asset folders
    project_root = os.path.dirname(os.path.abspath(__file__))
    for candidate in (
        os.path.join(project_root, "assets"),  # legacy
        os.path.join(project_root, "assets_common"),
    ):
        if os.path.isdir(candidate) and candidate not in roots:
            roots.append(candidate)

    variant_dir_name = _detect_platform_assets_dir()
    if variant_dir_name:
        variant_path = os.path.join(project_root, variant_dir_name)
        if os.path.isdir(variant_path) and variant_path not in roots:
            roots.append(variant_path)

    _ASSET_ROOTS = roots
    return _ASSET_ROOTS


def _res(*parts):
    if not parts:
        raise ValueError("_res requires at least one path component")
    # Allow lookups like _res("assets", "file")
    if parts[0] == "assets":
        rel = os.path.join(*parts[1:]) if len(parts) > 1 else ""
        for root in _asset_roots():
            candidate = os.path.join(root, rel)
            if os.path.exists(candidate):
                return candidate
        # Fallback to first asset root even if the file is missing; keeps behaviour predictable
        roots = _asset_roots()
        if roots:
            return os.path.join(roots[0], rel)
    return os.path.join(BASE_PATH, *parts)

# ---------------------------- PARAMETERS ----------------------------

BASE_SIZE = (1920, 1080)
MENU_SIZE = BASE_SIZE
GAME_SIZE = BASE_SIZE
FPS = 60

COLS, ROWS = 20, 20
CELL = 48
GRID_TOPLEFT = (120, 80)
PANEL_X = GRID_TOPLEFT[0] + COLS * CELL + 80
PANEL_WIDTH = 600

COLOR_BG = (24, 26, 28)
COLOR_GRID = (0, 0, 0)
COLOR_GRID_THICK = (0, 0, 0)
COLOR_TEXT = (220, 225, 230)

TILE_COLORS = [
    (62, 191, 238),
    (238, 84, 76),
    (255, 207, 65),
    (97, 219, 112),
    (98, 142, 255),
    (177, 102, 235),
]

EMPTY = -1

SWAP_MS = 180
POP_MS = 200
FALL_MS_PER_CELL = 55
FALL_MS_MIN = 120

TURN_ANNOUNCE_MS = 900
ROUND_ANNOUNCE_MS = 1000
GAME_OVER_WAIT_MS = 1200

MIN_MOVES_PER_ROUND = 1
MAX_MOVES_PER_ROUND = 20
MIN_TOTAL_ROUNDS = 1
MAX_TOTAL_ROUNDS = 50
MAX_NAME_LEN = 16
MIN_PLAYERS = 2

# Global flags that mirror rule toggles for top-level helpers (AI, legal_swap)
BOMB_ENABLED_FOR_AI = False
COLOR_CHAIN_ENABLED_FOR_AI = False


@dataclass
class GameConfig:
    display_mode: str = "windowed"
    resolution: tuple[int, int] = BASE_SIZE
    rumble_on: bool = True


def _config_path():
    base = os.path.dirname(os.path.abspath(__file__))
    return os.path.join(base, "config.json")


def load_config():
    path = _config_path()
    if not os.path.exists(path):
        return GameConfig()
    try:
        with open(path, "r", encoding="utf-8") as fh:
            data = json.load(fh)
    except (OSError, json.JSONDecodeError):
        return GameConfig()
    cfg = GameConfig()
    cfg.display_mode = str(data.get("display_mode", cfg.display_mode))
    res = data.get("resolution", cfg.resolution)
    if isinstance(res, (list, tuple)) and len(res) == 2:
        try:
            cfg.resolution = (int(res[0]), int(res[1]))
        except (TypeError, ValueError):
            cfg.resolution = BASE_SIZE
    cfg.rumble_on = bool(data.get("rumble_on", cfg.rumble_on))
    return cfg


def save_config(config: GameConfig):
    path = _config_path()
    payload = {
        "display_mode": config.display_mode,
        "resolution": list(config.resolution),
        "rumble_on": bool(config.rumble_on),
    }
    try:
        with open(path, "w", encoding="utf-8") as fh:
            json.dump(payload, fh, indent=2)
    except OSError:
        pass


MODE_SAVE_DIRS = {
    "PvC": "PvC",
    "PvP": "PvP",
    "tournament": "tournament",
}


def _mode_save_dir(mode_key, create=False):
    sub = MODE_SAVE_DIRS.get(mode_key)
    if not sub:
        return None
    path = os.path.join(SAVE_ROOT, sub)
    if create:
        os.makedirs(path, exist_ok=True)
    return path


def _sanitize_save_name(name):
    if not isinstance(name, str):
        name = ""
    cleaned = re.sub(r"[^A-Za-z0-9 _\-]", "", name).strip()
    if not cleaned:
        cleaned = "save"
    filename = re.sub(r"\s+", "_", cleaned)
    return cleaned, filename


def _unique_save_path(mode_key, base_filename):
    directory = _mode_save_dir(mode_key, create=True)
    candidate = base_filename
    suffix = 1
    while True:
        path = os.path.join(directory, candidate + ".json")
        if not os.path.exists(path):
            return path
        suffix += 1
        candidate = f"{base_filename}_{suffix}"


def _format_timestamp(ts):
    try:
        return datetime.fromtimestamp(float(ts)).strftime("%Y-%m-%d %H:%M")
    except Exception:
        return ""


def list_saves_for_mode(mode_key):
    directory = _mode_save_dir(mode_key, create=False)
    if not directory or not os.path.isdir(directory):
        return []
    saves = []
    for fname in os.listdir(directory):
        if not fname.lower().endswith(".json"):
            continue
        path = os.path.join(directory, fname)
        try:
            with open(path, "r", encoding="utf-8") as fh:
                data = json.load(fh)
        except (OSError, json.JSONDecodeError):
            continue
        meta = data.get("meta", {})
        display_name = meta.get("name") or os.path.splitext(fname)[0]
        saves.append({
            "name": display_name,
            "path": path,
            "meta": meta,
            "payload": data,
        })
    saves.sort(key=lambda s: s["meta"].get("timestamp", 0), reverse=True)
    return saves


def in_bounds(c, r):
    return 0 <= c < COLS and 0 <= r < ROWS


def random_tile():
    return random.randrange(len(TILE_COLORS))


def cell_to_px(c, r):
    x0, y0 = GRID_TOPLEFT
    return x0 + c * CELL + 1, y0 + r * CELL + 1


def new_board():
    board = [[random_tile() for _ in range(ROWS)] for _ in range(COLS)]
    for c in range(COLS):
        for r in range(ROWS):
            tries = 0
            while has_match_at(board, c, r):
                board[c][r] = random_tile()
                tries += 1
                if tries > 20:
                    break
    changed = True
    iter_guard = 0
    while changed and iter_guard < 1000:
        iter_guard += 1
        changed = False
        for c in range(COLS - 1):
            for r in range(ROWS - 1):
                t = board[c][r]
                if (
                    t != EMPTY
                    and board[c + 1][r] == t
                    and board[c][r + 1] == t
                    and board[c + 1][r + 1] == t
                ):
                    board[c + 1][r + 1] = random_tile()
                    changed = True
    return board


def has_match_at(board, c, r):
    t = board[c][r]
    if t == EMPTY:
        return False
    cnt = 1
    i = c - 1
    while i >= 0 and board[i][r] == t:
        cnt += 1
        i -= 1
    i = c + 1
    while i < COLS and board[i][r] == t:
        cnt += 1
        i += 1
    if cnt >= 3:
        return True
    cnt = 1
    j = r - 1
    while j >= 0 and board[c][j] == t:
        cnt += 1
        j -= 1
    j = r + 1
    while j < ROWS and board[c][j] == t:
        cnt += 1
        j += 1
    return cnt >= 3


def find_all_matches(board):
    groups = []
    for r in range(ROWS):
        c = 0
        while c < COLS:
            t = board[c][r]
            if t == EMPTY:
                c += 1
                continue
            s = c
            while c + 1 < COLS and board[c + 1][r] == t:
                c += 1
            if c - s + 1 >= 3:
                groups.append({(x, r) for x in range(s, c + 1)})
            c += 1
    for c in range(COLS):
        r = 0
        while r < ROWS:
            t = board[c][r]
            if t == EMPTY:
                r += 1
                continue
            s = r
            while r + 1 < ROWS and board[c][r + 1] == t:
                r += 1
            if r - s + 1 >= 3:
                groups.append({(c, y) for y in range(s, r + 1)})
            r += 1
    merged = True
    while merged:
        merged = False
        out = []
        while groups:
            cur = groups.pop()
            expanded = True
            while expanded:
                expanded = False
                for i, g in enumerate(groups):
                    if cur & g:
                        cur |= g
                        groups.pop(i)
                        merged = True
                        expanded = True
                        break
            out.append(cur)
        groups = out
    return groups


def any_legal_moves(board):
    for c in range(COLS):
        for r in range(ROWS):
            for dc, dr in ((1, 0), (0, 1)):
                nc, nr = c + dc, r + dr
                if not in_bounds(nc, nr):
                    continue
                if legal_swap(board, (c, r), (nc, nr)):
                    return True
    return False


def swap_cells(board, a, b):
    (c1, r1), (c2, r2) = a, b
    board[c1][r1], board[c2][r2] = board[c2][r2], board[c1][r1]


def legal_swap(board, a, b):
    (c1, r1), (c2, r2) = a, b
    if abs(c1 - c2) + abs(r1 - r2) != 1:
        return False
    swap_cells(board, a, b)
    try:
        ok = bool(find_all_matches(board))
        if not ok and BOMB_ENABLED_FOR_AI:
            for c in range(COLS - 1):
                for r in range(ROWS - 1):
                    t = board[c][r]
                    if (
                        t != EMPTY
                        and board[c + 1][r] == t
                        and board[c][r + 1] == t
                        and board[c + 1][r + 1] == t
                    ):
                        ok = True
                        break
                if ok:
                    break
    finally:
        swap_cells(board, a, b)
    return ok


# ---------------------------- ANIMATIONS ----------------------------

@dataclass
class Anim:
    kind: str              # 'move' | 'pop'
    color: tuple
    x0: int
    y0: int
    x1: int
    y1: int
    size0: float = 1.0
    size1: float = 1.0
    a0: int = 255
    a1: int = 255
    ms: int = 200
    t: int = 0             # elapsed

    def step(self, dt):
        self.t += dt
        if self.t >= self.ms:
            self.t = self.ms
        k = self.t / max(1, self.ms)
        x = self.x0 + (self.x1 - self.x0) * k
        y = self.y0 + (self.y1 - self.y0) * k
        size = self.size0 + (self.size1 - self.size0) * k
        alpha = int(self.a0 + (self.a1 - self.a0) * k)
        return x, y, size, alpha

    @property
    def finished(self):
        return self.t >= self.ms

# ---------------------------- DRAWING HELPERS ----------------------------

def draw_text(surface, font, text, pos, color=COLOR_TEXT):
    surface.blit(font.render(text, True, color), pos)

def draw_grid(surf):
    x0, y0 = GRID_TOPLEFT
    w, h = COLS * CELL, ROWS * CELL
    pygame.draw.rect(surf, COLOR_GRID_THICK, (x0 - 2, y0 - 2, w + 4, h + 4), 2)
    for i in range(COLS + 1):
        x = x0 + i * CELL
        pygame.draw.line(surf, COLOR_GRID, (x, y0), (x, y0 + h))
    for j in range(ROWS + 1):
        y = y0 + j * CELL
        pygame.draw.line(surf, COLOR_GRID, (x0, y), (x0 + w, y))

def draw_board(surface, board, hidden=set(), selected=None, hover=None):
    """Draw tiles with optional hover/selection scaling."""
    x0, y0 = GRID_TOPLEFT
    highlight_scale = {}
    if hover and in_bounds(*hover):
        highlight_scale[hover] = max(1.0, highlight_scale.get(hover, 1.0), 1.20)
    if selected and in_bounds(*selected):
        highlight_scale[selected] = max(1.0, highlight_scale.get(selected, 1.0), 1.12)

    base_w = CELL - 2
    base_h = CELL - 2

    for c in range(COLS):
        for r in range(ROWS):
            if (c, r) in hidden:
                continue
            tval = board[c][r]
            if tval == EMPTY:
                continue
            rect = pygame.Rect(x0 + c * CELL + 1, y0 + r * CELL + 1, base_w, base_h)
            scale = highlight_scale.get((c, r), 1.0)
            if scale > 1.0:
                dw = int(round(rect.width * (scale - 1.0)))
                dh = int(round(rect.height * (scale - 1.0)))
                rect = rect.inflate(dw, dh)
            pygame.draw.rect(surface, TILE_COLORS[tval], rect)

def draw_anims(surface, anims):
    """Render active animations as moving/fading squares."""
    for anim in anims:
        x, y, size, alpha = anim.step(0)
        w = int((CELL - 2) * size)
        h = int((CELL - 2) * size)
        dx = (CELL - 2 - w) // 2
        dy = (CELL - 2 - h) // 2
        rect = pygame.Rect(int(x) + dx, int(y) + dy, w, h)
        sprite = pygame.Surface((w, h), pygame.SRCALPHA)
        sprite.fill((*anim.color, alpha))
        surface.blit(sprite, rect.topleft)

def draw_tooltip(surface, text, font):
    if not text:
        return
    width, height = surface.get_size()
    pad_x = 18
    bar_h = max(font.get_height() + 18, 42)
    rect = pygame.Rect(0, height - bar_h, width, bar_h)
    surface.fill(COLOR_BG, rect)
    pygame.draw.rect(surface, (255, 255, 255), rect, 2)
    label = render_text_fit(surface, text, rect.width - 2 * pad_x, font, color=COLOR_TEXT, min_size=14)
    surface.blit(label, (rect.centerx - label.get_width()//2, rect.centery - label.get_height()//2))

def render_text_fit(surface, text, max_width, base_font, color=COLOR_TEXT, min_size=14, max_size=None):
    default_family = "consolas,menlo,monospace"
    bold = base_font.get_bold()
    italic = base_font.get_italic()
    if max_size is None:
        try:
            max_size = max(12, base_font.get_height())
        except Exception:
            max_size = 32
        max_size = min(max_size, 48)
    cur_size = int(max_size)
    while cur_size >= min_size:
        f = pygame.font.SysFont(default_family, cur_size, bold=bold, italic=italic)
        surf = f.render(text, True, color)
        if surf.get_width() <= max_width:
            return surf
        cur_size -= 1
    f = pygame.font.SysFont(default_family, min_size, bold=bold, italic=italic)
    ell = "..."
    s = text
    while s and f.render(s+ell, True, color).get_width() > max_width:
        s = s[:-1]
    return f.render((s+ell) if s else ell, True, color)

def _blit_center(surface, surf, rect):
    text_rect = surf.get_rect(center=(rect.centerx, rect.centery))
    surface.blit(surf, text_rect.topleft)
    return text_rect

def _draw_caret(surface, text_rect, color):
    caret_x = text_rect.right + 2
    top = text_rect.top + 4
    bottom = text_rect.bottom - 4
    pygame.draw.line(surface, color, (caret_x, top), (caret_x, bottom), 2)

def draw_wrapped_text(surface, text, rect, font, color=COLOR_TEXT, line_gap=6):
    words = text.split()
    x, y = rect.topleft
    max_w = rect.width
    line = ""
    while words:
        w = words.pop(0)
        test = (line + " " + w).strip()
        img = font.render(test, True, color)
        if img.get_width() <= max_w:
            line = test
        else:
            if line:
                img = font.render(line, True, color)
                surface.blit(img, (x, y))
                y += img.get_height() + line_gap
            line = w
    if line:
        img = font.render(line, True, color)
        surface.blit(img, (x, y))
        y += img.get_height()
    return y

def draw_center_banner(surface, text, font, subtext=None):
    x0, y0 = GRID_TOPLEFT
    w, h = COLS*CELL, ROWS*CELL
    rect = pygame.Rect(x0+20, y0 + h//2 - 50, w-40, 120)
    overlay = pygame.Surface(rect.size, pygame.SRCALPHA)
    overlay.fill((0,0,0,160))
    surface.blit(overlay, rect.topleft)
    max_w = rect.width - 24
    title_surf = render_text_fit(surface, text, max_w, font, color=(255,255,255), min_size=14)
    surface.blit(title_surf, (rect.centerx - title_surf.get_width()//2, rect.top + 16))
    if subtext:
        f2 = pygame.font.SysFont("consolas,menlo,monospace", 20)
        sub_surf = render_text_fit(surface, subtext, max_w, f2, color=(220,220,220), min_size=12)
        surface.blit(sub_surf, (rect.centerx - sub_surf.get_width()//2, rect.top + 62))

# ---------------------------- AI ----------------------------

def all_scoring_swaps(board):
    moves = []
    for c in range(COLS):
        for r in range(ROWS):
            for dc, dr in ((1,0),(0,1),(-1,0),(0,-1)):
                nc, nr = c+dc, r+dr
                if not in_bounds(nc, nr): continue
                a, b = (c,r), (nc,nr)
                if abs(c-nc)+abs(r-nr)!=1: continue
                if not legal_swap(board, a, b): continue
                tmp = [col[:] for col in board]
                pts = simulate_full_chain(tmp, a, b)
                moves.append((pts, a, b))
    uniq = {}
    for pts, a, b in moves:
        key = tuple(sorted([a,b]))
        if key not in uniq or pts > uniq[key][0]:
            uniq[key] = (pts, a, b)
    return sorted(uniq.values(), key=lambda x:-x[0])

def simulate_full_chain(board, a, b):
    swap_cells(board, a, b)
    total_cleared = 0
    while True:
        groups = find_all_matches(board)
        base_groups = [set(g) for g in groups]
        chain_neighbors = set()
        if COLOR_CHAIN_ENABLED_FOR_AI and base_groups:
            matched_cells = set()
            for g in base_groups:
                matched_cells |= g
            for g in base_groups:
                if not g:
                    continue
                sample_c, sample_r = next(iter(g))
                if not in_bounds(sample_c, sample_r):
                    continue
                match_color = board[sample_c][sample_r]
                if match_color == EMPTY:
                    continue
                for (c, r) in g:
                    for dc, dr in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                        nc, nr = c + dc, r + dr
                        if not in_bounds(nc, nr):
                            continue
                        if (nc, nr) in matched_cells:
                            continue
                        if board[nc][nr] == match_color:
                            chain_neighbors.add((nc, nr))
        bombs_count = 0
        if BOMB_ENABLED_FOR_AI:
            for c in range(COLS-1):
                for r in range(ROWS-1):
                    t = board[c][r]
                    if t != EMPTY and board[c+1][r]==t and board[c][r+1]==t and board[c+1][r+1]==t:
                        # add 4x4 area around the 2x2 square
                        bomb_cells = set()
                        for x in range(c-1, c+3):
                            for y in range(r-1, r+3):
                                if 0 <= x < COLS and 0 <= y < ROWS:
                                    bomb_cells.add((x,y))
                        groups.append(bomb_cells)
                        bombs_count += 1
        if not groups and not chain_neighbors:
            break
        to_remove = set()
        for g in groups:
            to_remove |= g
        if chain_neighbors:
            to_remove |= chain_neighbors
        total_cleared += len(to_remove) + (2 * bombs_count)
        for (c,r) in to_remove:
            board[c][r] = EMPTY
        for c in range(COLS):
            write = ROWS-1
            for r in range(ROWS-1,-1,-1):
                if board[c][r]!=EMPTY:
                    board[c][write]=board[c][r]; write-=1
            for r in range(write,-1,-1):
                board[c][r]=random_tile()
    return (1 + (total_cleared - 3)) if total_cleared >= 3 else 0

# ---------------------------- MAIN LOOP ----------------------------
# --- Window icon helper (dev + PyInstaller-friendly) ---
def _load_window_icon():
    # Собираем набор вариантов путей: обычные и внутри PyInstaller-папки _internal
    candidates = [
        _res('assets', 'icon.png'),
        _res('assets', 'icon.ico'),
        _res('_internal', 'assets', 'icon.png'),
        _res('_internal', 'assets', 'icon.ico'),
        os.path.join(os.path.dirname(__file__), 'assets', 'icon.png'),
        os.path.join(os.path.dirname(__file__), 'assets', 'icon.ico'),
        os.path.join(os.path.dirname(__file__), 'icon.png'),
        os.path.join(os.path.dirname(__file__), 'icon.ico'),
        'assets/icon.png',
        'assets/icon.ico',
        'icon.png',
        'icon.ico',
    ]
    for p in candidates:
        try:
            if os.path.exists(p):
                try:
                    return pygame.image.load(p)
                except Exception:
                    try:
                        with open(p, "rb") as fh:
                            data = fh.read()
                        sig = data.find(b"\x89PNG\r\n\x1a\n")
                        if sig != -1:
                            return pygame.image.load(io.BytesIO(data[sig:]))
                    except Exception:
                        pass
        except Exception:
            pass
    return None

# --- Audio: load sounds ---
import os
def _load_sound(name, fallback=None):
    # Prefer assets/ relative to script or PyInstaller bundle
    try:
        return pygame.mixer.Sound(_res('assets', name))
    except Exception:
        pass
    # Fallback to same folder as script (backward compatibility)
    try:
        return pygame.mixer.Sound(os.path.join(os.path.dirname(__file__), name))
    except Exception:
        pass
    # Last resort: current working directory
    try:
        return pygame.mixer.Sound(name)
    except Exception:
        return fallback

SND_CLICK = None
SND_SWAP  = None
SND_MATCH = None
SND_COMBO = None
SND_ERROR = None
SND_WIN   = None
SND_BOMB  = None
SND_NEXT_TURN = None
SND_NEXT_ROUND = None
SND_COUNTDOWN = None
SND_COUNTDOWN_END = None
SND_LOGO = None
BG_MUSIC  = None

def _init_audio_assets():
    global SND_CLICK, SND_SWAP, SND_MATCH, SND_COMBO, SND_ERROR, SND_WIN, SND_BOMB
    global SND_NEXT_TURN, SND_NEXT_ROUND, SND_COUNTDOWN, SND_COUNTDOWN_END, SND_LOGO, BG_MUSIC
    try:
        SND_CLICK = _load_sound("click.wav")
        SND_SWAP  = _load_sound("swap.wav")
        SND_MATCH = _load_sound("match.wav")
        SND_COMBO = _load_sound("combo.wav")
        SND_ERROR = _load_sound("error.wav")
        SND_WIN   = _load_sound("win.wav")
        # Bomb sound (works in dev and in PyInstaller via _MEIPASS/_internal)
        SND_BOMB  = _load_sound("bomb.wav")
        SND_NEXT_TURN = _load_sound("next_turn.wav")
        SND_NEXT_ROUND = _load_sound("next_round.wav")
        SND_COUNTDOWN = _load_sound("countdown.wav")
        SND_COUNTDOWN_END = _load_sound("countdown_end.wav")
        SND_LOGO = _load_sound("Vovkinshteyn.wav")
        # Resolve bg path
        bg_local = os.path.join(os.path.dirname(__file__), "bg_loop.wav")
        BG_MUSIC = None
        # Prefer assets/bg_loop.ogg, then assets/bg_loop.wav
        bg_ogg = _res('assets', 'bg_loop.ogg')
        bg_wav = _res('assets', 'bg_loop.wav')
        if os.path.exists(bg_ogg):
            BG_MUSIC = bg_ogg
        elif os.path.exists(bg_wav):
            BG_MUSIC = bg_wav
        else:
            # Backward compatibility: same folder
            loc_ogg = os.path.join(os.path.dirname(__file__), 'bg_loop.ogg')
            loc_wav = os.path.join(os.path.dirname(__file__), 'bg_loop.wav')
            if os.path.exists(loc_ogg): BG_MUSIC = loc_ogg
            elif os.path.exists(loc_wav): BG_MUSIC = loc_wav
    except Exception:
        pass

def play_click(): 
    try:
        if SND_CLICK: SND_CLICK.play()
    except Exception:
        pass

def play_swap():
    try:
        if SND_SWAP: SND_SWAP.play()
    except Exception:
        pass

def play_match():
    try:
        if SND_MATCH: SND_MATCH.play()
    except Exception:
        pass

def play_combo():
    try:
        if SND_COMBO: SND_COMBO.play()
    except Exception:
        pass

def play_error():
    try:
        if SND_ERROR: SND_ERROR.play()
    except Exception:
        pass

def play_win():
    try:
        if SND_WIN: SND_WIN.play()
    except Exception:
        pass

def play_bomb():
    try:
        if SND_BOMB: SND_BOMB.play()
    except Exception:
        pass

def play_next_turn():
    try:
        if SND_NEXT_TURN: SND_NEXT_TURN.play()
    except Exception:
        pass

def play_next_round():
    try:
        if SND_NEXT_ROUND: SND_NEXT_ROUND.play()
    except Exception:
        pass

def play_countdown():
    try:
        if SND_COUNTDOWN: SND_COUNTDOWN.play()
    except Exception:
        pass

def play_countdown_end():
    try:
        if SND_COUNTDOWN_END: SND_COUNTDOWN_END.play()
    except Exception:
        pass

def play_logo():
    try:
        if SND_LOGO:
            SND_LOGO.play()
    except Exception:
        pass

def start_bg_music(volume=0.35):
    import pygame, os
    path = None
    try:
        path = BG_MUSIC
    except Exception:
        path = None
    if not path:
        ogg = _res('assets','bg_loop.ogg')
        wav = _res('assets','bg_loop.wav')
        path = ogg if os.path.exists(ogg) else (wav if os.path.exists(wav) else None)
    if not path:
        return
    try:
        pygame.mixer.music.load(path)
        pygame.mixer.music.set_volume(float(volume))
        pygame.mixer.music.play(-1)
        # In case it was paused somewhere else
        try: pygame.mixer.music.unpause()
        except Exception: pass
    except Exception:
        pass


def stop_bg_music():
    try:
        pygame.mixer.music.fadeout(250)
    except Exception:
        pass

def main():
    pygame.init()
    pygame.mixer.init(frequency=22050, size=-16, channels=1, buffer=512)
    _init_audio_assets()
    try:
        config = load_config()
        if not isinstance(config, GameConfig):
            config = GameConfig()
    except Exception:
        config = GameConfig()
    if getattr(config, "display_mode", None) not in {"windowed", "exclusive"}:
        config.display_mode = "windowed"
    if not getattr(config, "resolution", None):
        config.resolution = BASE_SIZE
    else:
        r = config.resolution
        if isinstance(r, (list, tuple)) and len(r) == 2:
            try:
                config.resolution = (int(r[0]), int(r[1]))
            except (TypeError, ValueError):
                config.resolution = BASE_SIZE
        else:
            config.resolution = BASE_SIZE
    config.rumble_on = bool(getattr(config, "rumble_on", True))

    # Gamepad init (optional)
    try:
        pygame.joystick.init()
        gp = pygame.joystick.Joystick(0) if pygame.joystick.get_count() > 0 else None
        if gp:
            gp.init()
    except Exception:
        gp = None
    gp_deadzone = 0.35
    gp_repeat_ms = 170
    gp_last_ms = 0
    gp_last_dir = (0, 0)

    input_mode = "mouse"  # current primary input source

    def set_input_mode(mode: str):
        nonlocal input_mode
        new_mode = "controller" if mode == "controller" else "mouse"
        if new_mode == input_mode:
            return
        input_mode = new_mode
        try:
            pygame.mouse.set_visible(new_mode == "mouse")
        except Exception:
            pass

    def ensure_gamepad(force=False):
        nonlocal gp
        try:
            count = pygame.joystick.get_count()
        except Exception:
            count = 0
        attached = False
        if gp:
            getter = getattr(gp, "get_attached", None)
            if callable(getter):
                try:
                    attached = getter()
                except Exception:
                    attached = False
            else:
                attached = True
        if not force and gp and attached:
            return
        if gp and not attached:
            gp = None
        if count <= 0:
            gp = None
            return
        try:
            pad = pygame.joystick.Joystick(0)
            pad.init()
            gp = pad
        except Exception:
            gp = None

    joy_added_event = getattr(pygame, "JOYDEVICEADDED", None)
    joy_removed_event = getattr(pygame, "JOYDEVICEREMOVED", None)
    ensure_gamepad(force=True)

    # Display mode state and helpers
    display_mode = getattr(config, "display_mode", "windowed")
    last_windowed_size = tuple(getattr(config, "resolution", BASE_SIZE))
    window_surf = None  # actual display surface
    present_scale = 1.0
    present_offset = (0, 0)

    def to_logical(pos):
        """Map window coordinates to logical BASE_SIZE space. Returns None if outside."""
        nonlocal present_scale, present_offset
        scale = present_scale
        if scale <= 0:
            return None
        x, y = pos
        ox, oy = present_offset
        lx = (x - ox) / scale
        ly = (y - oy) / scale
        if lx < 0 or ly < 0 or lx >= BASE_SIZE[0] or ly >= BASE_SIZE[1]:
            return None
        return (lx, ly)

    def maximize_window_if_needed():
        if display_mode != 'windowed':
            return
        if not sys.platform.startswith('win'):
            return
        try:
            info = pygame.display.get_wm_info()
            hwnd = info.get('window') if info else None
            if hwnd:
                ctypes.windll.user32.ShowWindow(hwnd, 3)  # SW_MAXIMIZE
        except Exception:
            pass

    def toggle_fullscreen():
        nonlocal display_mode
        if config.display_mode == 'exclusive':
            config.display_mode = 'windowed'
        else:
            config.display_mode = 'exclusive'
        display_mode = config.display_mode
        base = GAME_SIZE if state not in ("menu", "settings", "time_mode", "time_settings", "tournament", "rules", "controls") else MENU_SIZE
        apply_display_mode(base)
        return "Fullscreen enabled" if config.display_mode == 'exclusive' else "Windowed mode enabled"

    def apply_display_mode(base_size):
        nonlocal screen, display_mode, last_windowed_size, window_surf, present_scale, present_offset
        base_size = tuple(base_size)
        if display_mode == 'exclusive':
            info = pygame.display.Info()
            win_w = info.current_w or base_size[0]
            win_h = info.current_h or base_size[1]
            window_surf = pygame.display.set_mode((win_w, win_h), pygame.FULLSCREEN)
            config.display_mode = 'exclusive'
        else:
            if not last_windowed_size:
                last_windowed_size = base_size
            window_surf = pygame.display.set_mode(last_windowed_size, pygame.RESIZABLE)
            maximize_window_if_needed()
            try:
                last_windowed_size = pygame.display.get_window_size()
            except Exception:
                last_windowed_size = window_surf.get_size()
            config.display_mode = 'windowed'
            config.resolution = tuple(last_windowed_size)

        try:
            screen = pygame.Surface(BASE_SIZE).convert(window_surf)
        except Exception:
            screen = pygame.Surface(BASE_SIZE).convert()

        ww, wh = window_surf.get_size()
        sw, sh = BASE_SIZE
        if ww > 0 and wh > 0:
            scale = min(ww / sw, wh / sh)
            if scale <= 0:
                scale = 1.0
            present_scale = scale
            present_offset = ((ww - sw * scale) / 2.0, (wh - sh * scale) / 2.0)
        else:
            present_scale = 1.0
            present_offset = (0, 0)

    def load_intro_logo():
        candidates = [
            _res('assets', 'logo.png'),
            _res('assets', 'logo.PNG'),
            os.path.join(os.path.dirname(__file__), 'logo.png'),
            os.path.join(os.path.dirname(__file__), 'logo.PNG'),
            _res('assets', 'Vovkinshteyn_logo.png'),
            _res('assets', 'Vovkinshteyn_logo.PNG'),
            os.path.join(os.path.dirname(__file__), 'Vovkinshteyn_logo.png'),
            os.path.join(os.path.dirname(__file__), 'Vovkinshteyn_logo.PNG'),
        ]
        for path in candidates:
            try:
                if os.path.exists(path):
                    surf = pygame.image.load(path).convert_alpha()
                    bg = surf.get_at((0, 0))[:3]
                    return surf, bg
            except Exception:
                continue
        return None, COLOR_BG

    def show_intro():
        nonlocal present_scale, present_offset, screen
        logo_surf, bg_color = load_intro_logo()
        duration_ms = 2500
        if SND_LOGO:
            try:
                duration_ms = max(duration_ms, int(SND_LOGO.get_length() * 1000))
            except Exception:
                pass
            play_logo()
        start_ms = pygame.time.get_ticks()
        intro_clock = pygame.time.Clock()
        intro_done = False
        while not intro_done:
            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    pygame.quit()
                    sys.exit()
            elapsed = pygame.time.get_ticks() - start_ms
            screen.fill(bg_color)
            if logo_surf:
                rect = logo_surf.get_rect()
                rect.center = (BASE_SIZE[0] // 2, BASE_SIZE[1] // 2)
                screen.blit(logo_surf, rect)

            ww, wh = window_surf.get_size()
            sw, sh = screen.get_size()
            scale = min(ww / sw, wh / sh) if ww > 0 and wh > 0 else 1.0
            if scale <= 0:
                scale = 1.0
            dw, dh = int(sw * scale), int(sh * scale)
            dx = (ww - dw) // 2
            dy = (wh - dh) // 2
            present_scale = scale
            present_offset = (dx, dy)
            if hasattr(window_surf, 'fill'):
                window_surf.fill(bg_color)
            if dw > 0 and dh > 0:
                if hasattr(pygame.transform, 'smoothscale'):
                    frame = pygame.transform.smoothscale(screen, (dw, dh))
                else:
                    frame = pygame.transform.scale(screen, (dw, dh))
                window_surf.blit(frame, (dx, dy))
            pygame.display.flip()

            if elapsed >= duration_ms:
                intro_done = True
            intro_clock.tick(FPS)

    # Установить иконку до создания окна — так надежнее на некоторых платформах
    try:
        _icon = _load_window_icon()
        if _icon is not None:
            pygame.display.set_icon(_icon)
    except Exception:
        pass

    pygame.display.set_caption("MATCH")
    screen = None
    apply_display_mode(BASE_SIZE)
    try:
        pygame.mouse.set_visible(True)
    except Exception:
        pass
    show_intro()
    try:
        start_bg_music(0.35)
    except Exception:
        pass
    clock = pygame.time.Clock()
    font = pygame.font.SysFont("consolas,menlo,monospace", 26)
    font_big = pygame.font.SysFont("consolas,menlo,monospace", 40, bold=True)
    font_title = pygame.font.SysFont("consolas,menlo,monospace", 64, bold=True)

    # Game state
    board = new_board()
    mode = None  # 'PvC' or 'PvP'
    players_count = 2
    player_names = ["Player 1", "Player 2"]
    scores = [0,0]
    turn = 0
    message = ""

    # Round settings
    total_rounds = 5
    moves_per_round = 3
    turn_order_mode = "consecutive"  # "consecutive" or "round_robin"
    
    # Time controls (pre-settings)
    time_mode = "classic"            # "classic" or "blitz"
    blitz_turn_minutes = 2           # 1..5
    blitz_between_seconds = 10       # seconds between turns banners
    turn_time_left_ms = 0            # current turn countdown for blitz
    between_left_s = 0               # seconds left before next turn for blitz
    between_accum_ms = 0             # accumulator for 1s ticks
    time_next_state = None           # "settings" | "tournament"

    current_round = 1
    moves_left_per_player = [moves_per_round]*players_count
    fixed_order = []  # list of indices 0..players_count-1 determining order for whole game

    # States
    state = "menu"  # menu | save_setup | time_mode | time_settings | settings | round_announce | announce | idle | swap | pop | fall | game_over | rules | tournament
    anims = []
    hidden = set()
    chain_owner = 0
    move_cleared_total = 0  # total tiles cleared during the current move (including cascades)
    cascade_depth = 0       # number of pop phases triggered in the current move (0 = first match)
    current_move_is_human = True
    announce_t = 0
    round_announce_t = 0
    round_announce_wait_ms = ROUND_ANNOUNCE_MS
    game_over_t = 0

    menu_idx = 0
    menu_options = [
        "Start: Player vs Computer",
        "Start: Player vs Player",
        "Tournament",
        "Rules",
        "Controls",
        "Toggle Fullscreen",
        "Quit",
    ]
    menu_count = len(menu_options)
    show_controls_overlay = False
    tooltip_text = None
    # Confirm-exit overlay state
    confirm_exit = False
    confirm_idx = 1  # 0 = Yes, 1 = No (default No)
    prev_state_for_confirm = None
    confirm_prompt = "Return to main menu?"
    confirm_options = ["Yes", "No"]
    confirm_base_prompt = confirm_prompt
    confirm_base_options = list(confirm_options)
    confirm_allow_save = False
    save_prompt_active = False
    save_name_holder = [""]
    save_prompt_error = ""
    save_prompt_from_controller = False
    save_prompt_focus = "input"  # "input" or "buttons"
    save_prompt_input_rect = None
    current_save_path = None
    current_save_mode = None
    current_save_name = None
    load_mode = None
    load_saves = []
    confirm_hitboxes = []
    load_hitboxes = []  # list of (pygame.Rect, index) for load menu entries
    load_detail_hitboxes = []
    load_detail_buttons = []
    load_detail_buttons = []
    load_saves = []
    load_menu_idx = 0
    load_view = "list"   # "list" or "detail"
    menu_hitboxes = []              # list of (pygame.Rect, action_index)
    settings_hitboxes = []          # list of (pygame.Rect, (kind, idx))
    t_settings_hitboxes = []        # list of (pygame.Rect, (kind, idx)))
    time_hitboxes = []              # list of (pygame.Rect, index) for time_mode/time_settings
    load_hitboxes = []
    load_detail_hitboxes = []
    settings_scrollbar = {"track": None, "thumb": None}
    t_settings_scrollbar = {"track": None, "thumb": None}
    rules_scrollbar = {"track": None, "thumb": None}
    dragging_scroll = None          # "settings" | "t_settings" | "rules" | None
    drag_offset_y = 0
    rules_scroll = 0
    rules_content_h = 0
    rules_view_h = 0

    settings_idx = 0
    editing_name = None
    osk_active = False
    osk_row = 0
    osk_col = 0
    osk_case_select = None
    pre_save_holder = [""]
    pre_save_error = ""
    pre_save_from_controller = False
    pre_save_hitboxes = []
    pre_save_input_rect = None
    pre_save_btn_idx = 0
    pre_save_buttons = ["Continue", "Cancel"]
    pending_new_session = None
    pre_game_save_name = ""

    def _osk_input_active():
        """True when the on-screen keyboard should intercept input."""
        return (
            osk_active
            and editing_name is not None
            and save_prompt_focus == "input"
            and (
                state == "settings"
                or (state == "tournament" and t_state == "t_settings")
                or (state == "confirm_exit" and save_prompt_active)
                or state == "save_setup"
            )
        )
    def _osk_target_list():
        if state == "settings":
            return player_names
        if state == "tournament" and t_state == "t_settings":
            return tournament_players
        if state == "confirm_exit" and save_prompt_active:
            return save_name_holder
        if state == "save_setup":
            return pre_save_holder
        return None

    def _handle_osk_closed():
        if state == "save_setup":
            cancel_save_setup()
        elif state == "confirm_exit" and save_prompt_active:
            cancel_save_naming()

    def _hint_text(mouse_text, controller_text):
        return controller_text if input_mode == "controller" else mouse_text

    def _blink_on():
        return (pygame.time.get_ticks() // 400) % 2 == 0

    def _osk_rows():
        letters = list("ABCDEFGHIJKLMNOPQRSTUVWXYZ")
        digits = list("0123456789")
        row1 = letters[:13]
        row2 = letters[13:]
        row3 = digits
        row4 = ['-', '_', 'Space', 'Backspace', 'Done']
        return [row1, row2, row3, row4]

    def _osk_append_char(ch):
        target = _osk_target_list()
        if target is None or editing_name is None:
            return
        if len(target[editing_name]) < MAX_NAME_LEN:
            target[editing_name] += ch

    def _osk_move(dx, dy):
        nonlocal osk_row, osk_col
        rows = _osk_rows()
        osk_row = max(0, min(len(rows) - 1, osk_row + dy))
        cols = len(rows[osk_row]) if rows else 0
        if cols > 0:
            osk_col = max(0, min(cols - 1, osk_col + dx))
        else:
            osk_col = 0

    def _osk_layout():
        rows = _osk_rows()
        win_w, win_h = screen.get_size()
        max_w = max(200, win_w - 40)
        bw = min(max_w, max(360, min(900, win_w - 80)))
        max_h = max(240, win_h - 80)
        bh = min(max_h, max(360, min(520, win_h - 160)))
        box = pygame.Rect(win_w//2 - bw//2, win_h//2 - bh//2, bw, bh)
        inner = pygame.Rect(box.left + 24, box.top + 156, box.width - 48, box.height - 212)
        gap_y = 12
        layout = []
        if rows:
            row_count = len(rows)
            available_h = max(0, inner.height - gap_y * (row_count - 1))
            base_h = (available_h // row_count) if row_count else 0
            extra_h = available_h - base_h * row_count if row_count else 0
            y = inner.top
            for ri, row in enumerate(rows):
                cols = len(row)
                key_h = base_h + (1 if ri < extra_h else 0)
                if row_count and key_h <= 0:
                    key_h = max(1, available_h // max(1, row_count))
                if cols <= 0:
                    y += key_h + gap_y
                    continue
                gap_x = 10
                available_w = max(0, inner.width - gap_x * (cols - 1))
                base_w = (available_w // cols) if cols else 0
                extra_w = available_w - base_w * cols if cols else 0
                x = inner.left
                for ci, label in enumerate(row):
                    key_w = base_w + (1 if ci < extra_w else 0)
                    if cols and key_w <= 0:
                        key_w = max(1, available_w // max(1, cols))
                    rect = pygame.Rect(x, y, key_w, key_h)
                    layout.append((rect, label, ri, ci))
                    x += key_w + gap_x
                y += key_h + gap_y
        return box, inner, layout

    def _osk_case_option_rects(box, anchor):
        letter = (osk_case_select or {}).get('letter', 'A')
        panel_w = max(220, min(320, box.width - 80))
        panel_h = max(96, min(140, box.height - 200))
        panel = pygame.Rect(0, 0, panel_w, panel_h)
        if anchor:
            panel.centerx = anchor.centerx
            panel.bottom = anchor.top - 10
        else:
            panel.centerx = box.centerx
            panel.top = box.top + 70
        if panel.left < box.left + 20:
            panel.left = box.left + 20
        if panel.right > box.right - 20:
            panel.right = box.right - 20
        if panel.top < box.top + 70:
            panel.top = box.top + 70
        if panel.bottom > box.bottom - 20:
            panel.bottom = box.bottom - 20
        btn_gap = 12
        btn_w = (panel.width - btn_gap * 3) // 2
        btn_h = panel.height - 32
        upper_rect = pygame.Rect(panel.left + btn_gap, panel.top + 16, btn_w, btn_h)
        lower_rect = pygame.Rect(panel.right - btn_w - btn_gap, panel.top + 16, btn_w, btn_h)
        options = [
            (upper_rect, 0, letter.upper()),
            (lower_rect, 1, letter.lower())
        ]
        return panel, options

    def _osk_activate_selected():
        nonlocal osk_active, osk_case_select, editing_name
        rows = _osk_rows()
        if not rows:
            return
        label = rows[osk_row][osk_col]
        if label == 'Backspace':
            target = _osk_target_list()
            if target is not None and editing_name is not None:
                target[editing_name] = target[editing_name][:-1]
            return
        if label == 'Done':
            osk_active = False
            osk_case_select = None
            editing_name = None
            return
        if label == 'Space':
            _osk_append_char(' ')
            return
        if len(label) == 1 and label.isalpha():
            osk_case_select = {'letter': label, 'choice': 0, 'row': osk_row, 'col': osk_col}
            return
        _osk_append_char(label)

    def _osk_commit_case():
        nonlocal osk_case_select, osk_active, editing_name
        target = _osk_target_list()
        if target is None or editing_name is None or osk_case_select is None:
            osk_case_select = None
            return
        letter = osk_case_select.get('letter', 'A')
        choice = osk_case_select.get('choice', 0)
        ch = letter.lower() if choice == 1 else letter.upper()
        _osk_append_char(ch)
        osk_case_select = None

    def draw_osk_overlay():
        win_w, win_h = screen.get_size()
        overlay = pygame.Surface((win_w, win_h), pygame.SRCALPHA)
        overlay.fill((0, 0, 0, 180))
        screen.blit(overlay, (0, 0))
        box, inner, layout = _osk_layout()
        pygame.draw.rect(screen, (30, 30, 30), box)
        pygame.draw.rect(screen, (220, 220, 220), box, 2)
        title = font_big.render("Enter Name", True, COLOR_TEXT)
        screen.blit(title, (box.centerx - title.get_width()//2, box.top + 28))
        target = _osk_target_list()
        current_text = ""
        if target is not None and editing_name is not None and 0 <= editing_name < len(target):
            current_text = target[editing_name]
        name_box = pygame.Rect(box.left + 40, box.top + 80, box.width - 80, 44)
        pygame.draw.rect(screen, (45, 45, 45), name_box)
        pygame.draw.rect(screen, (100, 100, 100), name_box, 2)
        text_surf = font.render(current_text or " ", True, COLOR_TEXT)
        screen.blit(text_surf, (name_box.centerx - text_surf.get_width()//2, name_box.centery - text_surf.get_height()//2))
        anchor_rect = None
        target_row = target_col = None
        if osk_case_select:
            target_row = osk_case_select.get('row')
            target_col = osk_case_select.get('col')
        for rect, label, ri, ci in layout:
            if osk_case_select and anchor_rect is None and ri == target_row and ci == target_col:
                anchor_rect = rect
            is_anchor = osk_case_select is not None and ri == target_row and ci == target_col
            selected = (ri == osk_row and ci == osk_col and osk_case_select is None)
            outline_color = (255, 255, 255) if selected or is_anchor else (120, 120, 120)
            fill_color = (255, 220, 60) if selected else ((80, 80, 120) if is_anchor else (200, 200, 200))
            pygame.draw.rect(screen, fill_color, rect)
            pygame.draw.rect(screen, outline_color, rect, 2)
            lbl = '␣' if label == 'Space' else label
            base_font = font_big if (selected or is_anchor) else font
            surf = base_font.render(lbl, True, (0, 0, 0))
            max_w = max(1, rect.width - 12)
            max_h = max(1, rect.height - 8)
            if surf.get_width() > max_w or surf.get_height() > max_h:
                surf = font.render(lbl, True, (0, 0, 0))
            if surf.get_width() > max_w or surf.get_height() > max_h:
                scale = min(max_w / max(1, surf.get_width()), max_h / max(1, surf.get_height()))
                if scale < 1.0:
                    new_size = (
                        max(1, int(surf.get_width() * scale)),
                        max(1, int(surf.get_height() * scale))
                    )
                    if new_size[0] > 0 and new_size[1] > 0:
                        surf = pygame.transform.smoothscale(surf, new_size)
            screen.blit(surf, (rect.centerx - surf.get_width()//2, rect.centery - surf.get_height()//2))
        if osk_case_select:
            panel, options = _osk_case_option_rects(box, anchor_rect)
            pygame.draw.rect(screen, (20, 20, 20), panel)
            pygame.draw.rect(screen, (200, 200, 200), panel, 2)
            info = font.render("Choose letter case", True, COLOR_TEXT)
            screen.blit(info, (panel.centerx - info.get_width()//2, panel.top + 4))
            for rect, choice, text in options:
                is_selected = (osk_case_select.get('choice', 0) == choice)
                fill = (255, 220, 60) if is_selected else (200, 200, 200)
                outline = (255, 255, 255) if is_selected else (120, 120, 120)
                pygame.draw.rect(screen, fill, rect)
                pygame.draw.rect(screen, outline, rect, 2)
                surf = font_big.render(text, True, (0, 0, 0))
                max_w = max(1, rect.width - 12)
                max_h = max(1, rect.height - 8)
                if surf.get_width() > max_w or surf.get_height() > max_h:
                    surf = font.render(text, True, (0, 0, 0))
                if surf.get_width() > max_w or surf.get_height() > max_h:
                    scale = min(max_w / max(1, surf.get_width()), max_h / max(1, surf.get_height()))
                    if scale < 1.0:
                        new_size = (
                            max(1, int(surf.get_width() * scale)),
                            max(1, int(surf.get_height() * scale))
                        )
                        if new_size[0] > 0 and new_size[1] > 0:
                            surf = pygame.transform.smoothscale(surf, new_size)
                screen.blit(surf, (rect.centerx - surf.get_width()//2, rect.centery - surf.get_height()//2))

    def _osk_handle_event(event):
        nonlocal osk_row, osk_col, osk_case_select, osk_active, editing_name
        if not osk_active or editing_name is None:
            return False
        consumed = False
        if event.type == pygame.MOUSEWHEEL:
            return True
        box = inner = None
        layout = None
        def ensure_layout():
            nonlocal box, inner, layout
            if layout is None:
                box, inner, layout = _osk_layout()
            return box, inner, layout
        if event.type in (pygame.MOUSEMOTION, pygame.MOUSEBUTTONDOWN, pygame.MOUSEBUTTONUP):
            if not hasattr(event, "pos"):
                return True
            pos = to_logical(event.pos)
            consumed = True
            if pos is None:
                return True
            if osk_case_select:
                ensure_layout()
                target_row = osk_case_select.get('row')
                target_col = osk_case_select.get('col')
                anchor = None
                for rect, label, ri, ci in layout:
                    if ri == target_row and ci == target_col:
                        anchor = rect
                        break
                panel, options = _osk_case_option_rects(box, anchor)
                if event.type == pygame.MOUSEMOTION:
                    for rect, choice, _ in options:
                        if rect.collidepoint(pos):
                            osk_case_select['choice'] = choice
                            break
                elif event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
                    for rect, choice, _ in options:
                        if rect.collidepoint(pos):
                            osk_case_select['choice'] = choice
                            _osk_commit_case()
                            break
                elif event.type == pygame.MOUSEBUTTONDOWN and event.button == 3:
                    osk_case_select = None
                return True
            ensure_layout()
            if event.type == pygame.MOUSEMOTION:
                for rect, label, ri, ci in layout:
                    if rect.collidepoint(pos):
                        osk_row, osk_col = ri, ci
                        break
            elif event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
                for rect, label, ri, ci in layout:
                    if rect.collidepoint(pos):
                        osk_row, osk_col = ri, ci
                        _osk_activate_selected()
                        break
            elif event.type == pygame.MOUSEBUTTONDOWN and event.button == 3:
                target = _osk_target_list()
                if target is not None and editing_name is not None and target[editing_name]:
                    target[editing_name] = target[editing_name][:-1]
            return True
        if event.type == pygame.KEYDOWN:
            consumed = True
            if event.key == pygame.K_ESCAPE:
                osk_active = False
                osk_case_select = None
                editing_name = None
            elif event.key == pygame.K_BACKSPACE:
                target = _osk_target_list()
                if target is not None and editing_name is not None and target[editing_name]:
                    target[editing_name] = target[editing_name][:-1]
            elif event.key == pygame.K_RETURN:
                if osk_case_select:
                    _osk_commit_case()
                else:
                    osk_active = False
                    osk_case_select = None
                    editing_name = None
            else:
                ch = event.unicode
                if ch:
                    if ch.isalpha():
                        _osk_append_char(ch[0])
                    elif ch.isdigit() or ch in "-_ ":
                        _osk_append_char(ch[0])
            return consumed
        return consumed
    # Settings scroll state
    settings_scroll = 0
    settings_view_h = 0
    settings_content_h = 0

    # --- Tournament state ---
    tournament_active = False
    t_state = None            # "t_settings" | "t_bracket" | "t_playing" | "t_end"
    tournament_players = []   # list of names
    tournament_cfg = {"moves_per_round": 3, "rounds_per_match": 5, "order": "consecutive", "color_chain": False}
    bracket_rounds = []       # list of rounds; each round: list of (a,b) indices (None for BYE)
    bracket_results = {}      # (round_idx, match_idx) -> winner_index
    t_round_idx = 0
    t_match_idx = 0
    eliminated_set = set()
    
    # Optional: Bomb rule toggle
    bomb_enabled = False
    color_chain_enabled = False
    # Track if the last end_move was a forced pass (e.g., blitz timer expired)
    forced_pass = False



    def ensure_moves_or_shuffle():
        nonlocal board, message
        tries = 0
        while not any_legal_moves(board):
            vals = [board[c][r] for c in range(COLS) for r in range(ROWS)]
            random.shuffle(vals)
            k=0
            for c in range(COLS):
                for r in range(ROWS):
                    board[c][r] = vals[k]; k+=1
            tries += 1
            if tries > 20:
                board = new_board(); break
        message = f"Reshuffled x{tries}" if tries else ""

    def start_new_game(selected_mode):
        nonlocal mode, state, players_count, player_names, scores, osk_active, osk_case_select, current_save_path, current_save_mode, current_save_name
        mode = selected_mode
        current_save_path = None
        current_save_mode = None
        current_save_name = None
        if mode == "PvC":
            players_count = 2
            player_names = ["Player 1", "computer"]
        else:
            if players_count < 2: players_count = 2
                        # Ensure names length
            if len(player_names) < players_count:
                for i in range(len(player_names), players_count):
                    player_names.append(f"Player {i+1}")
            else:
                player_names[:] = player_names[:players_count]
        scores = [0]*players_count
        # Go to time control selection before settings
        apply_display_mode(MENU_SIZE)
        state = "time_mode"
        time_next_state = "settings"
        osk_active = False
        osk_case_select = None

    def randomize_fixed_order_and_apply():
        """Randomize player order once for entire game and reorder names/scores/moves accordingly."""
        nonlocal fixed_order, player_names, scores, moves_left_per_player, turn
        fixed_order = list(range(len(player_names)))
        random.shuffle(fixed_order)
        # Reorder arrays to follow fixed order
        player_names = [player_names[i] for i in fixed_order]
        scores = [scores[i] for i in fixed_order]
        moves_left_per_player = [moves_left_per_player[i] for i in fixed_order]
        turn = 0  # start from first in fixed order every round

    def confirm_settings_and_begin():
        nonlocal board, scores, turn, message, state, screen, current_round, moves_left_per_player, between_left_s, between_accum_ms, osk_active, osk_case_select
        # sanitize names
        for i in range(players_count):
            if not player_names[i].strip():
                player_names[i] = f"Player {i+1}"
        apply_display_mode(GAME_SIZE)
        scores = [0]*players_count
        turn = 0
        message = ""
        current_round = 1
        moves_left_per_player = [moves_per_round]*players_count
        board[:] = new_board()
        ensure_moves_or_shuffle()
        randomize_fixed_order_and_apply()
        start_round_banner()
        # Initialize time state
        if time_mode == "blitz":
            between_left_s = max(0, int(blitz_between_seconds))
            between_accum_ms = 0
        # Mirror bomb setting for top-level helpers (AI, legal_swap)
        try:
            globals()["BOMB_ENABLED_FOR_AI"] = bool(bomb_enabled)
            globals()["COLOR_CHAIN_ENABLED_FOR_AI"] = bool(color_chain_enabled)
        except Exception:
            pass
        osk_active = False
        osk_case_select = None

    def start_round_banner():
        nonlocal state, round_announce_t, round_announce_wait_ms, turn, moves_left_per_player
        state = "round_announce"
        round_announce_t = 0
        turn = 0
        moves_left_per_player = [moves_per_round]*players_count
        wait_ms = ROUND_ANNOUNCE_MS
        if SND_NEXT_ROUND:
            try:
                wait_ms = max(wait_ms, int(SND_NEXT_ROUND.get_length() * 1000))
            except Exception:
                pass
            play_next_round()
        round_announce_wait_ms = wait_ms

    def start_announce():
        nonlocal state, announce_t, between_left_s, between_accum_ms, turn_time_left_ms
        # Enter announce before every turn. In blitz, we set the per-turn timer now,
        # but the between-turn banner duration is handled in the update loop.
        state = "announce"
        announce_t = 0
        try:
            if turn_order_mode == "round_robin":
                play_next_turn()
            else:
                if 0 <= turn < len(moves_left_per_player) and moves_left_per_player[turn] == moves_per_round:
                    play_next_turn()
        except Exception:
            pass
        if time_mode == "blitz":
            between_left_s = max(0, int(blitz_between_seconds))
            between_accum_ms = 0
            # prepare next turn timer
            turn_time_left_ms = int(max(1, blitz_turn_minutes) * 60_000)


    def turn_name(idx):
        return player_names[idx]

    # --- Bomb helpers ---
    def is_bomb_top_left(board, c, r):
        if c+1 >= COLS or r+1 >= ROWS:
            return False
        t = board[c][r]
        if t == EMPTY:
            return False
        return board[c+1][r] == t and board[c][r+1] == t and board[c+1][r+1] == t

    def bomb_explosion_cells(c, r):
        # 4x4 area around the 2x2 square with top-left (c,r)
        cells = set()
        for x in range(c-1, c+3):
            for y in range(r-1, r+3):
                if in_bounds(x, y):
                    cells.add((x, y))
        return cells

    # --- Tournament helpers ---
    def build_bracket(players_count):
        """Return list of rounds; each round is list of (a,b) indices or None for BYE."""
        order = list(range(players_count))
        random.shuffle(order)
        # pad to power of two
        n = 1
        while n < len(order): n <<= 1
        while len(order) < n: order.append(None)
        rounds = []
        # round 0
        r0 = []
        for i in range(0, len(order), 2):
            r0.append((order[i], order[i+1]))
        rounds.append(r0)
        m = len(r0)
        while m > 1:
            rounds.append([(None, None) for _ in range(m//2)])
            m //= 2
        return rounds

    def bracket_advance(rounds, results):
        """Apply results to fill next rounds with winners where known."""
        b = [list(r) for r in rounds]
        for (ri, mi), w in results.items():
            if ri+1 < len(b):
                idx = mi // 2
                left = (mi % 2) == 0
                a, bb = b[ri+1][idx]
                b[ri+1][idx] = (w, bb) if left else (a, w)
        return b

    def auto_advance_byes(rounds):
        """Pre-fill results when a pair has a BYE (one side None)."""
        res = {}
        for ri, r in enumerate(rounds):
            for mi, (a,b) in enumerate(r):
                if (a is None) ^ (b is None):
                    res[(ri, mi)] = b if a is None else a
        return res

    def draw_bracket(surface, players, rounds, results, current=(0,0), eliminated=set(), blink=False):
        w, h = surface.get_size()
        margin_x = 60
        margin_y = 60

        cols = max(1, len(rounds))
        col_w = (w - 2*margin_x) // cols

        # card layout
        row_h    = 24                 # height of one name row
        inner_gap = 8                 # gap between two rows
        card_pad  = 8                 # padding inside card
        card_w    = min(220, col_w - 40)
        card_h    = row_h*2 + inner_gap + card_pad*2

        font_small = pygame.font.SysFont("consolas,menlo,monospace", 20)
        col_text   = (230,230,230)
        col_grey   = (140,140,140)
        col_card   = (40,40,40)
        col_card_border = (90,90,90)
        col_highlight   = (255, 220, 60)
        col_line  = (100,100,100)

        # apply known results to know who advances
        filled = bracket_advance(rounds, results)

        # --- compute y-centers per column ---
        y_centers = []
        # round 0 evenly spaced
        r0 = filled[0] if filled else []
        if r0:
            total = len(r0)
            avail = max(1, h - 2*margin_y)
            gap   = avail / total
            y0 = [margin_y + (i+0.5)*gap for i in range(total)]
        else:
            y0 = []
        y_centers.append(y0)

        # other rounds: average of children
        for ri in range(1, len(filled)):
            prev = y_centers[ri-1]
            cur  = []
            for mi in range(len(filled[ri])):
                li = 2*mi
                ri_i = 2*mi+1
                if ri_i < len(prev):
                    yc = (prev[li] + prev[ri_i]) / 2.0
                elif li < len(prev):
                    yc = prev[li]
                else:
                    # fallback evenly if something odd
                    avail = max(1, h - 2*margin_y)
                    gap   = avail / max(1, len(filled[ri]))
                    yc    = margin_y + (mi+0.5)*gap
                cur.append(yc)
            y_centers.append(cur)

        def card_rect(ri, yc):
            cx = margin_x + ri*col_w + (col_w - card_w)//2
            top = int(yc - card_h/2)
            return pygame.Rect(cx, top, card_w, card_h)

        def draw_card(rect, a_idx, b_idx, is_current):
            # background + border
            pygame.draw.rect(surface, col_card, rect)
            pygame.draw.rect(surface, col_card_border, rect, 2)
            if is_current and blink:
                pygame.draw.rect(surface, col_highlight, rect.inflate(8,8), 2)

            # text rows
            def name_and_color(idx):
                if idx is None: return "—", col_grey
                name = players[idx]
                return name, (col_grey if idx in eliminated else col_text)

            a_name, ca = name_and_color(a_idx)
            b_name, cb = name_and_color(b_idx)

            ta = font_small.render(a_name, True, ca)
            tb = font_small.render(b_name, True, cb)

            # vertical layout inside card
            inner_top = rect.top + card_pad
            a_y = inner_top
            b_y = inner_top + row_h + inner_gap

            surface.blit(ta, (rect.left + card_pad, a_y))
            surface.blit(tb, (rect.left + card_pad, b_y))

        # --- draw all cards first and remember their centers for connectors ---
        cards = {}  # (ri, mi) -> (rect, center)
        for ri, matches in enumerate(filled):
            for mi, (a, b) in enumerate(matches):
                yc = y_centers[ri][mi]
                r  = card_rect(ri, yc)
                cards[(ri,mi)] = (r, (r.right, r.centery))  # right edge center used as line start
                is_cur = (ri, mi) == current
                draw_card(r, a, b, is_cur)

        # --- connectors: ONE line per parent to its child ---
        # from parent (ri-1, child_index) -> current (ri, mi)
        for ri in range(1, len(filled)):
            for mi, _ in enumerate(filled[ri]):
                # children indices in previous round
                left_idx  = 2*mi
                right_idx = 2*mi + 1
                # connector goes to the center-left of current card
                curr_rect, _ = cards[(ri, mi)]
                target = (curr_rect.left, curr_rect.centery)

                # left child line
                if (ri-1, left_idx) in cards:
                    prev_rect, prev_right_center = cards[(ri-1, left_idx)]
                    pygame.draw.line(surface, col_line, prev_right_center, target, 2)
                # right child line
                if (ri-1, right_idx) in cards:
                    prev_rect, prev_right_center = cards[(ri-1, right_idx)]
                    pygame.draw.line(surface, col_line, prev_right_center, target, 2)


    def start_swap(a, b):
        nonlocal state, anims, hidden, chain_owner, move_cleared_total, cascade_depth, current_move_is_human
        move_cleared_total = 0
        cascade_depth = 0
        if mode == "PvC" and 0 <= turn < len(player_names):
            current_move_is_human = str(player_names[turn]).lower() != "computer"
        else:
            current_move_is_human = True
        try: play_swap()
        except Exception: pass
        (c1,r1), (c2,r2) = a, b
        t1, t2 = board[c1][r1], board[c2][r2]
        x1,y1 = cell_to_px(c1,r1)
        x2,y2 = cell_to_px(c2,r2)
        anims = [
            Anim('move', TILE_COLORS[t1], x1,y1, x2,y2, ms=SWAP_MS),
            Anim('move', TILE_COLORS[t2], x2,y2, x1,y1, ms=SWAP_MS),
        ]
        hidden = {a,b}
        swap_cells(board, a, b)
        state = "swap"

    def after_swap():
        nonlocal state
        state = "pop"
        start_pop()

    def start_pop():
        try: play_match()
        except Exception: pass
        nonlocal hidden, state, move_cleared_total, cascade_depth
        groups = find_all_matches(board)
        base_groups = [set(g) for g in groups]
        chain_neighbors = set()
        matched_cells = set()
        if color_chain_enabled:
            for g in base_groups:
                matched_cells |= g
            for g in base_groups:
                if not g:
                    continue
                sample_c, sample_r = next(iter(g))
                if not in_bounds(sample_c, sample_r):
                    continue
                match_color = board[sample_c][sample_r]
                if match_color == EMPTY:
                    continue
                for (c, r) in g:
                    for dc, dr in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                        nc, nr = c + dc, r + dr
                        if not in_bounds(nc, nr):
                            continue
                        if (nc, nr) in matched_cells:
                            continue
                        if board[nc][nr] == match_color:
                            chain_neighbors.add((nc, nr))
        # If bombs are enabled, include their explosion areas
        bombs_count = 0
        if bomb_enabled:
            for c in range(COLS-1):
                for r in range(ROWS-1):
                    if is_bomb_top_left(board, c, r):
                        groups.append(bomb_explosion_cells(c, r))
                        bombs_count += 1
        if bombs_count > 0:
            try: play_bomb()
            except Exception: pass
        if not groups:
            end_move()
            return
        maybe_rumble(is_cascade=(cascade_depth > 0))
        cascade_depth += 1
        to_remove = set()
        for g in groups:
            to_remove |= g
        if color_chain_enabled and chain_neighbors:
            to_remove |= chain_neighbors
        move_cleared_total += len(to_remove)
        # Bonus so each bomb is worth 16 points total
        if bomb_enabled and bombs_count > 0:
            move_cleared_total += 2 * bombs_count
        anims.clear()
        hidden = to_remove.copy()
        for (c,r) in to_remove:
            t = board[c][r]
            x,y = cell_to_px(c,r)
            anims.append(Anim('pop', TILE_COLORS[t], x,y, x,y, size0=1.0, size1=0.4, a0=255, a1=0, ms=POP_MS))
            board[c][r] = EMPTY
        state = "pop"

    def after_pop():
        start_fall()

    def start_fall():
        nonlocal hidden, state, board
        anims.clear()
        hidden = set()
        for c in range(COLS):
            col_vals = [board[c][r] for r in range(ROWS) if board[c][r] != EMPTY]
            holes = ROWS - len(col_vals)

            old_positions = []
            for r in range(ROWS):
                if board[c][r] != EMPTY:
                    old_positions.append((r, board[c][r]))

            for r in range(ROWS-1, -1, -1):
                if r >= holes:
                    board[c][r] = col_vals.pop() if col_vals else random_tile()
                else:
                    board[c][r] = random_tile()

            write_r = ROWS-1
            for r_old, val in reversed(old_positions):
                dest = write_r
                write_r -= 1
                x0,y0 = cell_to_px(c, r_old)
                x1,y1 = cell_to_px(c, dest)
                dist = abs(dest - r_old)
                ms = max(FALL_MS_MIN, dist * FALL_MS_PER_CELL)
                anims.append(Anim('move', TILE_COLORS[val], x0,y0, x1,y1, ms=ms))
                hidden.add((c, dest))

            for i in range(holes):
                dest_r = i
                val = board[c][dest_r]
                x1,y1 = cell_to_px(c, dest_r)
                x0,y0 = x1, y1 - holes*CELL
                dist_cells = holes - i
                ms = max(FALL_MS_MIN, dist_cells * FALL_MS_PER_CELL)
                anims.append(Anim('move', TILE_COLORS[val], x0,y0, x1,y1, ms=ms, a0=200, a1=255))
                hidden.add((c, dest_r))

        state = "fall"

    def after_fall():
        groups = find_all_matches(board)
        bombs_pending = False
        if bomb_enabled:
            for c in range(COLS-1):
                for r in range(ROWS-1):
                    if is_bomb_top_left(board, c, r):
                        bombs_pending = True
                        break
                if bombs_pending:
                    break
        if groups or bombs_pending:
            if groups:
                try: play_combo()
                except Exception: pass
            start_pop()
        else:
            end_move()

    def all_zero(arr):
        return all(v <= 0 for v in arr)

    def end_move():
        nonlocal state, turn, hidden, current_round, game_over_t, moves_left_per_player, move_cleared_total, chain_owner, turn_time_left_ms, forced_pass
        hidden = set()
        current_turn = turn

        # Award points for the whole move (including cascades): 1 for 3, +1 for each extra tile
        if move_cleared_total >= 3:
            scores[chain_owner] += 1 + (move_cleared_total - 3)
        move_cleared_total = 0

        if forced_pass:
            moves_left_per_player[current_turn] = 0
        else:
            moves_left_per_player[current_turn] = max(0, moves_left_per_player[current_turn] - 1)

        def maybe_finish_round():
            nonlocal state, current_round, forced_pass
            if not all_zero(moves_left_per_player):
                return False
            current_round += 1
            forced_pass = False
            if current_round > total_rounds:
                try: play_win()
                except Exception: pass
                state = "game_over"; game_over_t = 0
                delete_current_save()
            else:
                start_round_banner()
                if not tournament_active:
                    autosave_current("round_start")
            return True

        if maybe_finish_round():
            return

        if turn_order_mode == "consecutive":
            if forced_pass:
                forced_pass = False
                turn = (current_turn + 1) % players_count
                if maybe_finish_round():
                    return
                start_announce()
                return
            if moves_left_per_player[current_turn] > 0:
                if time_mode == "blitz":
                    state = "idle"
                    turn_time_left_ms = int(max(1, blitz_turn_minutes) * 60_000)
                else:
                    start_announce()
                return
            turn = (current_turn + 1) % players_count
            if maybe_finish_round():
                return
            start_announce()
            return
        else:  # round_robin
            forced_pass = False
            for i in range(1, players_count+1):
                cand = (current_turn + i) % players_count
                if moves_left_per_player[cand] > 0:
                    turn = cand
                    start_announce()
                    return
            if not maybe_finish_round():
                start_round_banner()

    def step_anims(dt):
        nonlocal anims, state
        finished_all = True
        for a in anims:
            a.step(dt)
            if not a.finished:
                finished_all = False
        if finished_all:
            anims.clear()
            if state == "swap":
                after_swap()
            elif state == "pop":
                after_pop()
            elif state == "fall":
                after_fall()

    def board_from_mouse(mx, my):
        x0,y0 = GRID_TOPLEFT
        c = int((mx - x0) // CELL)
        r = int((my - y0) // CELL)
        if in_bounds(c,r):
            return (c,r)
        return None

    def draw_panel():
        screen.fill(COLOR_BG, (PANEL_X-40, 0, PANEL_WIDTH, GAME_SIZE[1]))
        draw_text(screen, font_big, "MATCH-3", (PANEL_X, 60))
        draw_text(screen, font, f"MODE: {mode if mode else '-'}", (PANEL_X, 130))
        draw_text(screen, font, f"ROUND: {current_round}/{total_rounds}", (PANEL_X, 170))
        draw_text(screen, font, f"ORDER: {'Round-robin' if turn_order_mode=='round_robin' else 'Consecutive'}", (PANEL_X, 210))
        draw_text(screen, font, f"BOMBS: {'On' if bomb_enabled else 'Off'}", (PANEL_X, 250))
        draw_text(screen, font, f"COLOR BLAST: {'On' if color_chain_enabled else 'Off'}", (PANEL_X, 290))

        y = 360
        if turn_order_mode == "round_robin":
            draw_text(screen, font, "Moves left:", (PANEL_X, y)); y += 40
            for i in range(players_count):
                draw_text(screen, font, f"{player_names[i]}: {moves_left_per_player[i]}", (PANEL_X, y)); y += 32
        else:
            draw_text(screen, font, f"MOVES LEFT: {moves_left_per_player[turn]}", (PANEL_X, y)); y += 40

        y += 24
        draw_text(screen, font, "Scores:", (PANEL_X, y)); y += 36
        for i in range(players_count):
            text = f"{player_names[i]}: {scores[i]}"
            surf = font.render(text, True, COLOR_TEXT)
            pos = (PANEL_X, y)
            if i == turn:
                pad_x, pad_y = 8, 6
                rect = pygame.Rect(pos[0]-pad_x, pos[1]-pad_y, surf.get_width()+2*pad_x, surf.get_height()+2*pad_y)
                pygame.draw.rect(screen, (255,255,255), rect, 2)
            screen.blit(surf, pos)
            y += 32

        y += 40
        draw_text(screen, font, "Controls:", (PANEL_X, y)); y += 36
        for s in ["Mouse: click two neighbors",
                  "Arrows + Enter",
                  "N: settings   R: reshuffle",
                  "1: PvC   2: PvP",
                  "Esc: menu"]:
            draw_text(screen, font, s, (PANEL_X, y)); y += 32

        if message:
            draw_text(screen, font, message, (PANEL_X, y+10))

    
    def _board_snapshot():
        return [[board[c][r] for r in range(ROWS)] for c in range(COLS)]

    def _restore_board(snapshot):
        nonlocal board
        if not isinstance(snapshot, list):
            snapshot = []
        for c in range(COLS):
            col = snapshot[c] if c < len(snapshot) and isinstance(snapshot[c], list) else []
            for r in range(ROWS):
                val = random_tile()
                if r < len(col):
                    cell = col[r]
                    if isinstance(cell, int):
                        val = cell
                board[c][r] = val

    def _build_game_save_payload(display_name):
        meta = {
            "name": display_name,
            "timestamp": time.time(),
            "players": list(player_names),
            "scores": list(scores),
            "round": current_round,
            "total_rounds": total_rounds,
            "turn": turn,
            "time_mode": time_mode,
            "color_chain": bool(color_chain_enabled),
            "bombs": bool(bomb_enabled),
        }
        data = {
            "players_count": players_count,
            "player_names": list(player_names),
            "scores": list(scores),
            "board": _board_snapshot(),
            "current_round": current_round,
            "total_rounds": total_rounds,
            "moves_per_round": moves_per_round,
            "turn_order_mode": turn_order_mode,
            "bomb_enabled": bool(bomb_enabled),
            "color_chain_enabled": bool(color_chain_enabled),
            "turn": turn,
            "moves_left_per_player": list(moves_left_per_player),
            "fixed_order": list(fixed_order),
            "time_mode": time_mode,
            "blitz_turn_minutes": blitz_turn_minutes,
            "blitz_between_seconds": blitz_between_seconds,
            "turn_time_left_ms": turn_time_left_ms,
            "between_left_s": between_left_s,
            "between_accum_ms": between_accum_ms,
            "forced_pass": forced_pass,
            "message": message,
            "cursor": cursor,
        }
        return {
            "version": 1,
            "mode": mode,
            "meta": meta,
            "data": data,
        }

    def _serialize_bracket_results():
        return [[ri, mi, winner] for (ri, mi), winner in bracket_results.items()]

    def _build_tournament_save_payload(display_name):
        safe_state = t_state if t_state != "t_playing" else "t_bracket"
        meta = {
            "name": display_name,
            "timestamp": time.time(),
            "players": list(tournament_players),
            "round_idx": t_round_idx,
            "match_idx": t_match_idx,
            "state": safe_state,
        }
        data = {
            "players": list(tournament_players),
            "cfg": dict(tournament_cfg),
            "rounds": bracket_rounds,
            "results": _serialize_bracket_results(),
            "round_idx": t_round_idx,
            "match_idx": t_match_idx,
            "t_state": safe_state,
            "eliminated": list(eliminated_set),
        }
        return {
            "version": 1,
            "mode": "tournament",
            "meta": meta,
            "data": data,
        }

    def save_current_session(save_name, overwrite_path=None):
        nonlocal current_save_path, current_save_mode, current_save_name
        mode_key = "tournament" if tournament_active else mode
        if not mode_key:
            return False, "No active game to save"
        display_name, base_filename = _sanitize_save_name(save_name)
        if mode_key == "tournament":
            payload = _build_tournament_save_payload(display_name)
        else:
            payload = _build_game_save_payload(display_name)
        if overwrite_path:
            path = overwrite_path
        else:
            path = _unique_save_path(mode_key, base_filename)
        payload.setdefault("meta", {})["filename"] = os.path.basename(path)
        try:
            with open(path, "w", encoding="utf-8") as fh:
                json.dump(payload, fh, ensure_ascii=False, indent=2)
        except OSError as exc:
            return False, str(exc)
        current_save_path = path
        current_save_mode = mode_key
        current_save_name = display_name
        return True, path

    def autosave_current(reason=None):
        nonlocal message, current_save_path, current_save_name
        name_candidate = current_save_name or pre_game_save_name or save_name_holder[0]
        if not name_candidate:
            return False, "No save name"
        overwrite = current_save_path if current_save_path else None
        ok, result = save_current_session(name_candidate, overwrite_path=overwrite)
        if not ok:
            message = f"Autosave failed: {result}"
        return ok, result

    def _load_game_payload(payload):
        nonlocal mode, players_count, player_names, scores, board, current_round, total_rounds
        nonlocal moves_per_round, turn_order_mode, bomb_enabled, color_chain_enabled
        nonlocal turn, moves_left_per_player, fixed_order, time_mode
        nonlocal blitz_turn_minutes, blitz_between_seconds, turn_time_left_ms
        nonlocal between_left_s, between_accum_ms, forced_pass, message, cursor
        game = payload.get("data", {})
        mode = payload.get("mode") or mode
        players_count = int(game.get("players_count", players_count))
        player_names[:] = list(game.get("player_names", player_names))
        scores[:] = list(game.get("scores", scores))
        _restore_board(game.get("board"))
        current_round = int(game.get("current_round", current_round))
        total_rounds = int(game.get("total_rounds", total_rounds))
        moves_per_round = int(game.get("moves_per_round", moves_per_round))
        turn_order_mode = game.get("turn_order_mode", turn_order_mode)
        bomb_enabled = bool(game.get("bomb_enabled", bomb_enabled))
        color_chain_enabled = bool(game.get("color_chain_enabled", color_chain_enabled))
        turn = int(game.get("turn", turn))
        ml = list(game.get("moves_left_per_player", moves_left_per_player))
        if len(ml) < players_count:
            ml.extend([moves_per_round] * (players_count - len(ml)))
        moves_left_per_player = ml[:players_count]
        fo = list(game.get("fixed_order", fixed_order))
        fixed_order[:] = fo[:players_count]
        time_mode = game.get("time_mode", time_mode)
        blitz_turn_minutes = int(game.get("blitz_turn_minutes", blitz_turn_minutes))
        blitz_between_seconds = int(game.get("blitz_between_seconds", blitz_between_seconds))
        turn_time_left_ms = int(game.get("turn_time_left_ms", turn_time_left_ms))
        between_left_s = int(game.get("between_left_s", between_left_s))
        between_accum_ms = int(game.get("between_accum_ms", between_accum_ms))
        forced_pass = bool(game.get("forced_pass", False))
        message = game.get("message", "")
        cursor = tuple(game.get("cursor", cursor))
        try:
            globals()["BOMB_ENABLED_FOR_AI"] = bool(bomb_enabled)
            globals()["COLOR_CHAIN_ENABLED_FOR_AI"] = bool(color_chain_enabled)
        except Exception:
            pass

    def _load_tournament_payload(payload):
        nonlocal tournament_active, t_state, tournament_players, tournament_cfg, color_chain_enabled
        nonlocal bracket_rounds, bracket_results, t_round_idx, t_match_idx, eliminated_set
        data = payload.get("data", {})
        tournament_active = True
        t_state = data.get("t_state", "t_bracket")
        tournament_players = list(data.get("players", tournament_players))
        tournament_cfg.update(data.get("cfg", tournament_cfg))
        color_chain_enabled = bool(tournament_cfg.get("color_chain", color_chain_enabled))
        try:
            globals()["COLOR_CHAIN_ENABLED_FOR_AI"] = bool(color_chain_enabled)
        except Exception:
            pass
        bracket_rounds = data.get("rounds", bracket_rounds)
        result_items = data.get("results", [])
        bracket_results = {}
        for item in result_items:
            if not isinstance(item, (list, tuple)) or len(item) != 3:
                continue
            ri, mi, winner = item
            try:
                bracket_results[(int(ri), int(mi))] = winner
            except Exception:
                continue
        t_round_idx = int(data.get("round_idx", t_round_idx))
        t_match_idx = int(data.get("match_idx", t_match_idx))
        eliminated_set = set(data.get("eliminated", []))

    def load_session_from_path(path):
        nonlocal current_save_path, current_save_mode, current_save_name
        try:
            with open(path, "r", encoding="utf-8") as fh:
                payload = json.load(fh)
        except (OSError, json.JSONDecodeError) as exc:
            return False, str(exc)
        mode_key = payload.get("mode")
        if mode_key in ("PvC", "PvP"):
            _load_game_payload(payload)
            current_save_path = path
            current_save_mode = mode_key
            current_save_name = payload.get("meta", {}).get("name")
            return True, mode_key
        if mode_key == "tournament":
            _load_tournament_payload(payload)
            current_save_path = path
            current_save_mode = mode_key
            current_save_name = payload.get("meta", {}).get("name")
            return True, mode_key
        return False, "Unknown save type"

    def cancel_confirm_exit():
        nonlocal confirm_exit, save_prompt_active, save_prompt_error, confirm_prompt, confirm_options, confirm_idx, state, osk_active, osk_case_select, editing_name, confirm_hitboxes, save_prompt_focus
        confirm_exit = False
        save_prompt_active = False
        save_prompt_error = ""
        confirm_prompt = confirm_base_prompt
        confirm_options = list(confirm_base_options)
        confirm_idx = 1 if len(confirm_options) > 1 else 0
        osk_active = False
        osk_case_select = None
        editing_name = None
        save_prompt_focus = "input"
        confirm_hitboxes = []
        if prev_state_for_confirm:
            state = prev_state_for_confirm

    def _return_to_menu():
        nonlocal state, confirm_exit, prev_state_for_confirm, tournament_active, mode, osk_active, osk_case_select, editing_name, save_prompt_active, save_prompt_focus
        confirm_exit = False
        save_prompt_active = False
        osk_active = False
        osk_case_select = None
        editing_name = None
        save_prompt_focus = "input"
        apply_display_mode(MENU_SIZE)
        state = "menu"
        anims.clear()
        hidden.clear()
        tournament_active = False
        mode = None
        prev_state_for_confirm = None
        reset_load_view()

    def exit_without_saving():
        cancel_confirm_exit()
        _return_to_menu()

    def enter_save_naming():
        nonlocal save_prompt_active, confirm_prompt, confirm_options, confirm_idx, save_prompt_error, save_prompt_from_controller, editing_name, osk_active, osk_case_select, osk_row, osk_col, save_prompt_focus
        save_prompt_active = True
        confirm_prompt = "Save game as:"
        confirm_options = ["Save", "Cancel"]
        confirm_idx = 0
        save_prompt_error = ""
        save_prompt_from_controller = (input_mode == "controller")
        osk_row = 0
        osk_col = 0
        if save_prompt_from_controller:
            osk_active = True
        focus_save_prompt_input()

    def cancel_save_naming():
        nonlocal save_prompt_active, confirm_prompt, confirm_options, confirm_idx, save_prompt_error, save_prompt_from_controller, osk_active, osk_case_select, editing_name, save_prompt_focus
        save_prompt_active = False
        save_prompt_error = ""
        save_prompt_from_controller = False
        confirm_prompt = confirm_base_prompt
        confirm_options = list(confirm_base_options)
        confirm_idx = 0
        osk_active = False
        osk_case_select = None
        editing_name = None
        save_prompt_focus = "input"

    def perform_save_and_exit():
        nonlocal save_prompt_error, message, tournament_active, mode, current_save_path, current_save_mode, current_save_name
        name = save_name_holder[0].strip()
        if not name:
            save_prompt_error = "Enter a name"
            return False
        display_name, _ = _sanitize_save_name(name)
        mode_key = "tournament" if tournament_active else mode
        overwrite = None
        if current_save_path and current_save_mode == mode_key and current_save_name == display_name:
            overwrite = current_save_path
        ok, result = save_current_session(name, overwrite_path=overwrite)
        if not ok:
            save_prompt_error = f"Save failed: {result}"
            return False
        cancel_save_naming()
        _return_to_menu()
        message = f"Saved as {current_save_name}" if current_save_name else "Game saved"
        return True

    def focus_save_prompt_input():
        nonlocal save_prompt_focus, editing_name, osk_active, osk_case_select
        save_prompt_focus = "input"
        if save_prompt_from_controller:
            editing_name = 0
            osk_active = True
            osk_case_select = None

    def focus_save_prompt_buttons():
        nonlocal save_prompt_focus, editing_name, osk_active, osk_case_select
        if osk_active:
            return
        save_prompt_focus = "buttons"
        editing_name = None
        osk_active = False
        osk_case_select = None

    def begin_save_setup(target):
        nonlocal pending_new_session, state, pre_save_error, pre_save_holder, pre_save_from_controller, pre_save_btn_idx
        nonlocal osk_active, osk_case_select, osk_row, osk_col, editing_name
        pending_new_session = target
        pre_save_error = ""
        pre_save_holder[0] = pre_game_save_name or ""
        pre_save_btn_idx = 0
        pre_save_hitboxes.clear()
        pre_save_from_controller = (input_mode == "controller")
        state = "save_setup"
        editing_name = 0 if pre_save_from_controller else None
        osk_active = pre_save_from_controller
        osk_case_select = None
        osk_row = 0
        osk_col = 0

    def cancel_save_setup():
        nonlocal state, pending_new_session, pre_save_error, osk_active, osk_case_select, editing_name
        pending_new_session = None
        pre_save_error = ""
        osk_active = False
        osk_case_select = None
        editing_name = None
        state = "menu"

    def confirm_save_setup():
        nonlocal pre_save_error, pre_game_save_name, pending_new_session, state, save_name_holder
        nonlocal osk_active, osk_case_select, editing_name, time_next_state, menu_idx, message
        name = pre_save_holder[0].strip()
        if not name:
            pre_save_error = "Enter a name"
            return False
        display_name, _ = _sanitize_save_name(name)
        pre_game_save_name = display_name
        pre_save_holder[0] = display_name
        save_name_holder[0] = display_name
        pre_save_error = ""
        osk_active = False
        osk_case_select = None
        editing_name = None
        target = pending_new_session or {}
        pending_new_session = None
        kind = target.get("kind")
        if kind == "game":
            start_new_game(target.get("mode", "PvC"))
        elif kind == "tournament":
            message = ""
            state = "time_mode"
            time_next_state = "tournament"
            menu_idx = 0
        else:
            state = "menu"
        return True

    def handle_confirm_selection(idx=None):
        nonlocal confirm_idx
        if idx is not None:
            confirm_idx = idx
        if save_prompt_active:
            if confirm_idx == 0:
                if not perform_save_and_exit():
                    return
            else:
                cancel_save_naming()
        else:
            if confirm_allow_save:
                if confirm_idx == 0:
                    enter_save_naming()
                elif confirm_idx == 1:
                    exit_without_saving()
                else:
                    cancel_confirm_exit()
            else:
                if confirm_idx == 0:
                    exit_without_saving()
                else:
                    cancel_confirm_exit()

    def open_exit_prompt():
        nonlocal confirm_exit, confirm_idx, prev_state_for_confirm, state, confirm_prompt, confirm_options, confirm_base_prompt, confirm_base_options, confirm_allow_save, save_prompt_active, save_prompt_error, save_name_holder, save_prompt_from_controller, save_prompt_focus
        if confirm_exit:
            return
        if state == "game_over":
            return
        prev_state_for_confirm = state
        confirm_allow_save = False
        save_prompt_active = False
        save_prompt_error = ""
        save_prompt_from_controller = False
        save_prompt_focus = "input"
        can_save = False
        if tournament_active:
            if t_state != "t_end":
                can_save = True
        elif mode in ("PvC", "PvP") and state != "game_over":
            can_save = True
        confirm_prompt = "Return to main menu?"
        if can_save:
            confirm_options = ["Save and Exit", "Exit without Saving", "Cancel"]
        else:
            confirm_options = ["Exit", "Cancel"]
        confirm_base_prompt = confirm_prompt
        confirm_base_options = list(confirm_options)
        confirm_idx = 0
        default_base = current_save_name or ("tournament" if tournament_active else (mode or "game"))
        timestamp = datetime.now().strftime("%Y%m%d_%H%M")
        save_name_holder[0] = f"{default_base}_{timestamp}" if can_save else ""
        confirm_allow_save = can_save
        confirm_exit = True
        state = "confirm_exit"

    def delete_current_save():
        nonlocal current_save_path, current_save_mode, current_save_name
        if current_save_path and os.path.exists(current_save_path):
            try:
                os.remove(current_save_path)
            except OSError:
                pass
        current_save_path = None
        current_save_mode = None
        current_save_name = None

    def reset_load_view():
        nonlocal load_mode, load_saves, load_menu_idx, load_view, load_selected, load_detail_idx, load_hitboxes, load_detail_hitboxes
        load_mode = None
        load_saves.clear()
        load_menu_idx = 0
        load_view = "list"
        load_selected = None
        load_detail_idx = 0
        load_hitboxes = []
        load_detail_hitboxes = []

    def start_new_from_load():
        nonlocal message, state, time_next_state, menu_idx
        lm = load_mode
        reset_load_view()
        message = ""
        if lm == "PvC":
            begin_save_setup({"kind": "game", "mode": "PvC"})
        elif lm == "PvP":
            begin_save_setup({"kind": "game", "mode": "PvP"})
        elif lm == "tournament":
            begin_save_setup({"kind": "tournament"})

    def load_selected_save():
        nonlocal state, announce_t, between_left_s, between_accum_ms, round_announce_t, message
        nonlocal anims, hidden, cascade_depth, move_cleared_total, hover, cursor, forced_pass
        nonlocal t_state, settings_idx, menu_idx, tournament_active
        if not load_selected:
            return False, "No save selected"
        ok, result = load_session_from_path(load_selected["path"])
        if not ok:
            message = f"Failed to load save: {result}"
            reset_load_view()
            state = "menu"
            return False, result
        if current_save_mode == "tournament":
            apply_display_mode(MENU_SIZE)
            state = "tournament"
            if t_state == "t_playing":
                t_state = "t_bracket"
            settings_idx = 0
            menu_idx = 0
            tournament_active = True
        else:
            apply_display_mode(GAME_SIZE)
            anims.clear()
            hidden.clear()
            cascade_depth = 0
            move_cleared_total = 0
            announce_t = 0
            between_accum_ms = 0
            round_announce_t = 0
            hover = None
            cursor = cursor if isinstance(cursor, tuple) else (COLS//2, ROWS//2)
            forced_pass = False
            message = ""
            state = "idle"
        reset_load_view()
        return True, None

    def delete_selected_save():
        nonlocal load_saves, load_selected, load_view, load_menu_idx, load_detail_idx, message, state
        if not load_selected:
            return False, "No save selected"
        path = load_selected.get("path")
        if not path:
            message = "Save data missing path."
            return False, "Missing path"
        try:
            if os.path.exists(path):
                os.remove(path)
        except OSError as exc:
            message = f"Failed to delete save: {exc}"
            return False, str(exc)
        remaining = list_saves_for_mode(load_mode) if load_mode else []
        load_saves[:] = remaining
        load_selected = None
        load_detail_idx = 0
        if remaining:
            load_view = "list"
            total_entries = len(load_saves) + 1
            load_menu_idx = min(max(1, load_menu_idx), total_entries - 1)
            message = "Save deleted"
            return True, None
        reset_load_view()
        message = "Save deleted"
        state = "menu"
        return True, None

    def draw_menu():
        nonlocal menu_hitboxes, menu_idx, config
        menu_hitboxes = []
        win_w, win_h = screen.get_size()
        screen.fill(COLOR_BG)
        t_surf = font_title.render("MATCH-3", True, COLOR_TEXT)
        screen.blit(t_surf, (win_w//2 - t_surf.get_width()//2, 30))

        box_w = int(win_w * 0.68)
        box_h = int(win_h * 0.66)
        box = pygame.Rect(win_w//2 - box_w//2, win_h//2 - box_h//2 + 20, box_w, box_h)
        pygame.draw.rect(screen, (0,0,0), box, 2)

        inner_margin = 30
        max_line_w = box.width - inner_margin*2
        yy = box.top + 40
        for i, item in enumerate(menu_options):
            label = item
            if item == "Toggle Fullscreen":
                current = "Fullscreen" if config.display_mode == "exclusive" else "Windowed"
                label = f"{item} ({current})"
            surf = render_text_fit(screen, label, max_line_w - 40, font_big if i==menu_idx else font)
            h = max(54, surf.get_height() + 24)
            btn_rect = pygame.Rect(box.left + inner_margin, yy, max_line_w, h)
            selected = (i == menu_idx)
            fill = (255, 220, 60) if selected else (40, 40, 40)
            outline = (255, 255, 255) if selected else (120, 120, 120)
            pygame.draw.rect(screen, fill, btn_rect)
            pygame.draw.rect(screen, outline, btn_rect, 2)
            text_color = (0, 0, 0) if selected else COLOR_TEXT
            text_surf = render_text_fit(screen, label, btn_rect.width - 40, font_big if i==menu_idx else font, color=text_color)
            screen.blit(text_surf, (btn_rect.centerx - text_surf.get_width()//2, btn_rect.centery - text_surf.get_height()//2))
            menu_hitboxes.append((btn_rect, i))
            yy += h + 12

        hint_lines = [
            _hint_text(
                "Click items or use ↑/↓ + Enter. Esc to quit.",
                "Use D-Pad/Stick to move, press A to select. Press B to quit."
            ),
            f"Display mode: {'Fullscreen' if config.display_mode == 'exclusive' else 'Windowed'}",
        ]
        offset = 0
        for line in hint_lines:
            hint_surf = render_text_fit(screen, line, max_line_w, font)
            screen.blit(hint_surf, (box.left + inner_margin, box.bottom - 60 + offset))
            offset += 26

    def menu_tooltip(idx):
        tips = {
            0: "Play vs computer (simple AI)",
            1: "Play vs another local player",
            2: "Create and play a knockout bracket",
            3: "Read the basic rules of the game",
            4: "See controls and hotkeys",
            5: "Switch between windowed and fullscreen modes",
            6: "Exit the game",
        }
        return tips.get(idx)

    def draw_load_select():
        nonlocal load_hitboxes, load_detail_hitboxes, load_detail_buttons, load_view, load_menu_idx, load_detail_idx
        win_w, win_h = screen.get_size()
        screen.fill(COLOR_BG)
        title_map = {
            "PvC": "Player vs Computer Saves",
            "PvP": "Player vs Player Saves",
            "tournament": "Tournament Saves",
        }
        title_text = title_map.get(load_mode, "Saved Games")
        title_surf = font_title.render(title_text, True, COLOR_TEXT)
        screen.blit(title_surf, (win_w//2 - title_surf.get_width()//2, 30))

        box_w = int(win_w * 0.7)
        box_h = int(win_h * 0.7)
        box = pygame.Rect(win_w//2 - box_w//2, win_h//2 - box_h//2 + 20, box_w, box_h)
        pygame.draw.rect(screen, (30, 30, 30), box)
        pygame.draw.rect(screen, (220, 220, 220), box, 2)

        def summary_for(save):
            payload = save.get("payload", {})
            meta = save.get("meta", {})
            mode_key = payload.get("mode")
            parts = []
            players = meta.get("players") or []
            scores = meta.get("scores") or []
            timestamp = _format_timestamp(meta.get("timestamp"))
            if mode_key in ("PvC", "PvP"):
                if players:
                    parts.append(" vs ".join(players[:2]))
                if players and scores and len(scores) >= len(players):
                    score_line = " / ".join(f"{p}: {scores[i]}" for i, p in enumerate(players[:len(scores)]))
                    parts.append(score_line)
                round_idx = meta.get("round")
                total_rounds_meta = meta.get("total_rounds")
                if round_idx is not None and total_rounds_meta:
                    parts.append(f"Round {round_idx}/{total_rounds_meta}")
                time_mode_meta = meta.get("time_mode")
                if time_mode_meta:
                    parts.append(f"{time_mode_meta.capitalize()} mode")
            elif mode_key == "tournament":
                if players:
                    parts.append(f"{len(players)} players")
                round_idx = meta.get("round_idx", 0)
                parts.append(f"Round {round_idx + 1}")
                state_meta = meta.get("state")
                if state_meta:
                    parts.append(state_meta.replace("_", " ").title())
            if timestamp:
                parts.append(f"Saved {timestamp}")
            return " | ".join(parts)

        if load_view == "list":
            load_hitboxes = []
            entries = []
            new_label = "Start New Game" if load_mode in ("PvC", "PvP") else "New Tournament"
            entries.append({"kind": "new", "title": new_label, "subtitle": "", "save": None})
            for save in load_saves:
                entries.append({
                    "kind": "save",
                    "title": save.get("name", "Save"),
                    "subtitle": summary_for(save),
                    "save": save,
                })
            load_menu_idx = max(0, min(load_menu_idx, len(entries) - 1))
            inner_margin = 40
            inner_width = box.width - 2 * inner_margin
            y = box.top + 60
            button_gap = 18
            base_height = 72
            for idx, entry in enumerate(entries):
                is_sel = (idx == load_menu_idx)
                fill = (255, 220, 60) if is_sel else (40, 40, 40)
                outline = (255, 255, 255) if is_sel else (120, 120, 120)
                title_color = (0, 0, 0) if is_sel else COLOR_TEXT
                title_font = font_big if is_sel else font
                title_surf = render_text_fit(screen, entry["title"], inner_width - 40, title_font, color=title_color)
                subtitle_surf = render_text_fit(screen, entry["subtitle"], inner_width - 40, font, color=(200, 200, 200)) if entry["subtitle"] else None
                height = max(base_height, title_surf.get_height() + 32 + (subtitle_surf.get_height() + 12 if subtitle_surf else 0))
                rect = pygame.Rect(box.left + inner_margin, y, inner_width, height)
                pygame.draw.rect(screen, fill, rect)
                pygame.draw.rect(screen, outline, rect, 2)
                screen.blit(title_surf, (rect.left + 20, rect.top + 16))
                if subtitle_surf:
                    screen.blit(subtitle_surf, (rect.left + 20, rect.bottom - subtitle_surf.get_height() - 16))
                load_hitboxes.append((rect, idx))
                y += height + button_gap
            if len(load_saves) == 0:
                info = "No saved games found."
                info_surf = font.render(info, True, COLOR_TEXT)
                screen.blit(info_surf, (box.left + inner_margin, box.bottom - 90))
            hint = _hint_text("Enter: select   Esc: back to menu", "A: select   B: back to menu")
            hint_surf = font.render(hint, True, COLOR_TEXT)
            screen.blit(hint_surf, (box.left + inner_margin, box.bottom - 50))
            return

        # Detail view
        if not load_selected:
            load_view = "list"
            return draw_load_select()

        load_detail_hitboxes = []
        load_detail_buttons = []
        save = load_selected
        payload = save.get("payload", {})
        meta = save.get("meta", {})
        mode_key = payload.get("mode")

        content = box.inflate(-60, -120)

        title_rect = pygame.Rect(box.left + 40, box.top + 40, box.width - 80, 50)
        title = render_text_fit(screen, save.get("name", "Save"), title_rect.width, font_big, color=COLOR_TEXT)
        screen.blit(title, (title_rect.left, title_rect.top))

        info_lines = []
        if mode_key in ("PvC", "PvP"):
            players = meta.get("players") or []
            scores = meta.get("scores") or []
            if players:
                info_lines.append(("Players", " vs ".join(players)))
            if players and scores and len(scores) >= len(players):
                score_line = ", ".join(f"{players[i]}: {scores[i]}" for i in range(len(players)))
                info_lines.append(("Scores", score_line))
            round_idx = meta.get("round")
            total_rounds_meta = meta.get("total_rounds")
            if round_idx is not None and total_rounds_meta:
                info_lines.append(("Round", f"{round_idx}/{total_rounds_meta}"))
            turn_index = meta.get("turn")
            if turn_index is not None and players:
                info_lines.append(("Next turn", players[turn_index % len(players)]))
            time_mode_meta = meta.get("time_mode")
            if time_mode_meta:
                info_lines.append(("Time mode", time_mode_meta.capitalize()))
            info_lines.append(("Bombs", "On" if meta.get("bombs") else "Off"))
            info_lines.append(("Color blast", "On" if meta.get("color_chain") else "Off"))
        elif mode_key == "tournament":
            players = meta.get("players") or []
            info_lines.append(("Tournament players", str(len(players))))
            info_lines.append(("Current round", str(meta.get("round_idx", 0) + 1)))
            info_lines.append(("Next match index", str(meta.get("match_idx", 0) + 1)))
            state_meta = meta.get("state")
            if state_meta:
                info_lines.append(("Stage", state_meta.replace("_", " ").title()))
            cfg = payload.get("data", {}).get("cfg", {})
            info_lines.append(("Moves per round", str(cfg.get("moves_per_round", '-'))))
            info_lines.append(("Rounds per match", str(cfg.get("rounds_per_match", '-'))))
            info_lines.append(("Turn order", "Round-robin" if cfg.get("order") == "round_robin" else "Consecutive"))
            info_lines.append(("Color blast", "On" if cfg.get("color_chain") else "Off"))
        timestamp = _format_timestamp(meta.get("timestamp"))
        if timestamp:
            info_lines.append(("Saved", timestamp))

        info_area = pygame.Rect(content.left, title_rect.bottom + 20, content.width, content.height - 140)
        y = info_area.top
        for heading, value in info_lines:
            text = f"{heading}: {value}"
            line = render_text_fit(screen, text, info_area.width, font, color=COLOR_TEXT)
            screen.blit(line, (info_area.left, y))
            y += line.get_height() + 14

        buttons = [("Load", 0), ("Delete", 1), ("Back", 2)]
        load_detail_idx = max(0, min(load_detail_idx, len(buttons) - 1))
        btn_gap = 32
        btn_w = 200
        btn_h = 56
        total_btn_w = len(buttons) * btn_w + (len(buttons) - 1) * btn_gap
        start_x = box.centerx - total_btn_w // 2
        y_btn = box.bottom - 100
        for idx, (label, _) in enumerate(buttons):
            rect = pygame.Rect(start_x + idx * (btn_w + btn_gap), y_btn, btn_w, btn_h)
            selected = (idx == load_detail_idx)
            fill = (255, 220, 60) if selected else (40, 40, 40)
            outline = (255, 255, 255) if selected else (120, 120, 120)
            text_color = (0, 0, 0) if selected else COLOR_TEXT
            pygame.draw.rect(screen, fill, rect)
            pygame.draw.rect(screen, outline, rect, 2)
            label_surf = render_text_fit(screen, label, rect.width - 40, font_big if selected else font, color=text_color)
            screen.blit(label_surf, (rect.centerx - label_surf.get_width()//2, rect.centery - label_surf.get_height()//2))
            load_detail_hitboxes.append((rect, idx))
            load_detail_buttons.append(rect)

        hint = _hint_text("Enter: activate button   Esc: back", "A: activate   B: back")
        hint_surf = render_text_fit(screen, hint, box.width - 80, font)
        hint_y = y_btn + btn_h + 24
        screen.blit(hint_surf, (box.centerx - hint_surf.get_width()//2, min(box.bottom - hint_surf.get_height() - 16, hint_y)))

    def activate_menu_selection():
        nonlocal state, menu_idx, time_next_state, running, message, load_mode, load_menu_idx, load_view, load_selected, load_detail_idx, load_hitboxes, load_detail_hitboxes, rules_scroll
        if menu_idx == 0:
            existing = list_saves_for_mode("PvC")
            if existing:
                message = ""
                load_mode = "PvC"
                load_saves[:] = existing
                load_menu_idx = 0
                load_view = "list"
                load_selected = None
                load_detail_idx = 0
                load_hitboxes = []
                load_detail_hitboxes = []
                state = "load_select"
            else:
                message = ""
                begin_save_setup({"kind": "game", "mode": "PvC"})
        elif menu_idx == 1:
            existing = list_saves_for_mode("PvP")
            if existing:
                message = ""
                load_mode = "PvP"
                load_saves[:] = existing
                load_menu_idx = 0
                load_view = "list"
                load_selected = None
                load_detail_idx = 0
                load_hitboxes = []
                load_detail_hitboxes = []
                state = "load_select"
            else:
                message = ""
                begin_save_setup({"kind": "game", "mode": "PvP"})
        elif menu_idx == 2:
            existing = list_saves_for_mode("tournament")
            if existing:
                message = ""
                load_mode = "tournament"
                load_saves[:] = existing
                load_menu_idx = 0
                load_view = "list"
                load_selected = None
                load_detail_idx = 0
                load_hitboxes = []
                load_detail_hitboxes = []
                state = "load_select"
            else:
                message = ""
                begin_save_setup({"kind": "tournament"})
        elif menu_idx == 3:
            message = ""
            rules_scroll = 0
            state = "rules"
        elif menu_idx == 4:
            message = ""
            state = "controls"
        elif menu_idx == 5:
            message = toggle_fullscreen()
        elif menu_idx == 6:
            running = False

    def settings_tooltip(kind):
        tips = {
            "name": "Edit player name",
            "players": "Number of players in PvP mode",
            "moves": "How many moves each player gets per round",
            "rounds": "Total number of rounds in the game",
            "order": "Turn order: consecutive or round-robin",
            "bomb": "Enable 2x2 bomb rule (4x4 explosion)",
            "color": "Enable Color Blast to clear adjacent tiles of the same color",
            "start": "Apply settings and start the game",
            "back": "Return to main menu",
        }
        return tips.get(kind)

    def t_settings_tooltip(kind):
        tips = {
            "tp_count": "Add or remove tournament participants",
            "tp_name": "Edit participant name",
            "tp_moves": "Moves per player in each round of a match",
            "tp_rounds": "Rounds required to win a match",
            "tp_order": "Turn order during matches",
            "tp_color": "Enable Color Blast for tournaments",
            "tp_start": "Generate bracket and start tournament",
            "tp_back": "Back to main menu",
        }
        return tips.get(kind)

    def time_tooltip(page, idx):
        if page == "mode":
            tips = [
                "Classic: no timers.",
                "Blitz: per-move timer and between-turn countdown.",
                "Back to main menu.",
            ]
            return tips[idx] if 0 <= idx < len(tips) else None
        else:  # settings
            tips = [
                "Minutes allowed for each move (1..5).",
                "Seconds between turns (banner countdown).",
                "Confirm and continue.",
            ]
            return tips[idx] if 0 <= idx < len(tips) else None

    def current_focus_tooltip():
        if state == "menu":
            return menu_tooltip(menu_idx)
        if state == "time_mode":
            return time_tooltip("mode", menu_idx)
        if state == "time_settings":
            return time_tooltip("settings", menu_idx)
        if state == "settings" and idx_map:
            safe_idx = settings_idx % len(idx_map)
            kind, _ = idx_map[safe_idx]
            return settings_tooltip(kind)
        if state == "tournament" and t_state == "t_settings" and idx_map:
            safe_idx = settings_idx % len(idx_map)
            kind, _ = idx_map[safe_idx]
            return t_settings_tooltip(kind)
        return None

    def draw_time_mode():
        nonlocal time_hitboxes
        win_w, win_h = screen.get_size()
        screen.fill(COLOR_BG)
        t_surf = font_title.render("Time Controls", True, COLOR_TEXT)
        screen.blit(t_surf, (win_w//2 - t_surf.get_width()//2, 30))
        box_w = int(win_w * 0.6)
        box_h = int(win_h * 0.5)
        box = pygame.Rect(win_w//2 - box_w//2, win_h//2 - box_h//2, box_w, box_h)
        pygame.draw.rect(screen, (0,0,0), box, 2)
        opts = ["Classic (no timer)", "Blitz (set timers)", "Back"]
        inner_margin = 30
        y = box.top + 60
        time_hitboxes = []
        for i, s in enumerate(opts):
            surf = render_text_fit(screen, s, box.width - inner_margin*2 - 40, font_big if i==menu_idx else font)
            h = max(54, surf.get_height()+24)
            r = pygame.Rect(box.left+inner_margin, y, box.width-inner_margin*2, h)
            selected = (i == menu_idx)
            fill = (255, 220, 60) if selected else (40,40,40)
            outline = (255, 255, 255) if selected else (120,120,120)
            pygame.draw.rect(screen, fill, r)
            pygame.draw.rect(screen, outline, r, 2)
            label_color = (0,0,0) if selected else COLOR_TEXT
            surf = render_text_fit(screen, s, r.width - 40, font_big if i==menu_idx else font, color=label_color)
            screen.blit(surf, (r.centerx - surf.get_width()//2, r.centery - surf.get_height()//2))
            time_hitboxes.append((r, i))
            y += h + 12

    def draw_time_settings():
        nonlocal time_hitboxes
        win_w, win_h = screen.get_size()
        screen.fill(COLOR_BG)
        t_surf = font_title.render("Blitz Settings", True, COLOR_TEXT)
        screen.blit(t_surf, (win_w//2 - t_surf.get_width()//2, 30))
        box_w = int(win_w * 0.6)
        box_h = int(win_h * 0.55)
        box = pygame.Rect(win_w//2 - box_w//2, win_h//2 - box_h//2, box_w, box_h)
        pygame.draw.rect(screen, (0,0,0), box, 2)
        inner_margin = 30
        y = box.top + 60
        items = [
            f"Turn time (minutes): {blitz_turn_minutes}",
            f"Between turns (sec): {blitz_between_seconds}",
            "Continue",
        ]
        time_hitboxes = []
        for i, s in enumerate(items):
            surf = render_text_fit(screen, s, box.width - inner_margin*2 - 40, font_big if i==menu_idx else font)
            h = max(54, surf.get_height()+24)
            r = pygame.Rect(box.left+inner_margin, y, box.width-inner_margin*2, h)
            selected = (i == menu_idx)
            fill = (255, 220, 60) if selected else (40,40,40)
            outline = (255, 255, 255) if selected else (120,120,120)
            pygame.draw.rect(screen, fill, r)
            pygame.draw.rect(screen, outline, r, 2)
            label_color = (0,0,0) if selected else COLOR_TEXT
            surf = render_text_fit(screen, s, r.width - 40, font_big if i==menu_idx else font, color=label_color)
            screen.blit(surf, (r.centerx - surf.get_width()//2, r.centery - surf.get_height()//2))
            time_hitboxes.append((r, i))
            y += h + 12

    
    def draw_settings():
        nonlocal settings_view_h, settings_content_h, settings_scroll, settings_idx, settings_hitboxes, settings_scrollbar, editing_name
        win_w, win_h = screen.get_size()
        screen.fill(COLOR_BG)
        t_surf = font_title.render("Round Settings", True, COLOR_TEXT)
        screen.blit(t_surf, (win_w//2 - t_surf.get_width()//2, 30))

        box_w = int(win_w * 0.72)
        box_h = int(win_h * 0.74)
        box = pygame.Rect(win_w//2 - box_w//2, win_h//2 - box_h//2 + 10, box_w, box_h)
        pygame.draw.rect(screen, (0,0,0), box, 2)

        inner_x = box.left + 40
        inner_y = box.top + 50
        inner_w = box.width - 80
        view_rect = pygame.Rect(inner_x, inner_y, inner_w, box.bottom - 90 - inner_y)
        settings_view_h = view_rect.height

        # Build labels
        labels = []
        for i in range(players_count):
            label = f"Player {i+1} name: {player_names[i]}"
            if mode == "PvC" and i == 1:
                label += " (fixed)"
            labels.append(("name", i, label))
        if mode == "PvP":
            labels.append(("players", None, f"Players: {players_count}"))
        labels.extend([
            ("moves", None, f"Moves per player per round: {moves_per_round}"),
            ("rounds", None, f"Total rounds: {total_rounds}"),
            ("order", None, f"Turn order: {'Round-robin' if turn_order_mode=='round_robin' else 'Consecutive'}"),
            ("bomb", None, f"Bombs: {'On' if bomb_enabled else 'Off'}"),
            ("color", None, f"Color blast: {'On' if color_chain_enabled else 'Off'}"),
            ("start", None, "START GAME"),
            ("back", None, "BACK"),
        ])

        # Build idx_to_item and measure content height
        idx_to_item = []
        y = 0
        base_line_h = 54
        gap = 12
        caret_on = _blink_on()
        measurements = []
        for kind, idx, text in labels:
            idx_to_item.append((kind, idx))
            is_sel = (settings_idx == len(idx_to_item)-1 and editing_name is None)
            editing_here = (state == "settings" and editing_name is not None and kind == 'name' and editing_name == idx)
            label_prefix = "(editing) " if editing_here else ""
            label_text = label_prefix + text
            font_use = font_big if is_sel else font
            temp = render_text_fit(screen, label_text or " ", inner_w - 40, font_use)
            block_h = max(base_line_h, temp.get_height() + 24)
            measurements.append((y, block_h, label_text, kind, idx, is_sel, editing_here))
            y += block_h + gap
        settings_content_h = y

        # Clamp scroll to content
        max_scroll = max(0, settings_content_h - settings_view_h)
        settings_scroll = max(0, min(settings_scroll, max_scroll))

        # Reset hitboxes & scrollbar
        settings_hitboxes = []
        settings_scrollbar["track"] = None
        settings_scrollbar["thumb"] = None

        # Clip & draw items with scroll offset
        prev_clip = screen.get_clip()
        screen.set_clip(view_rect)
        for base_y, block_h, label_text, kind, idx, is_sel, editing_here in measurements:
            draw_y = inner_y + base_y - settings_scroll
            if draw_y + block_h < inner_y - 10 or draw_y > view_rect.bottom + 10:
                continue
            text_color = (0,0,0) if is_sel else COLOR_TEXT
            surf = render_text_fit(screen, label_text or " ", inner_w - 40, font_big if is_sel else font, color=text_color)
            hit_rect = pygame.Rect(inner_x, draw_y, inner_w, block_h)
            settings_hitboxes.append((hit_rect, (kind, idx)))
            if is_sel:
                pygame.draw.rect(screen, (255,220,60), hit_rect)
                pygame.draw.rect(screen, (255,255,255), hit_rect, 2)
            else:
                pygame.draw.rect(screen, (40,40,40), hit_rect)
                pygame.draw.rect(screen, (120,120,120), hit_rect, 2)
            text_rect = surf.get_rect(center=hit_rect.center)
            screen.blit(surf, text_rect.topleft)
            if editing_here and caret_on:
                _draw_caret(screen, text_rect, text_color)
        screen.set_clip(prev_clip)

        # Draw scrollbar + store geometry
        if settings_content_h > settings_view_h:
            bar_x = view_rect.right + 6
            bar_y = view_rect.top
            bar_h = view_rect.height
            track_rect = pygame.Rect(bar_x, bar_y, 8, bar_h)
            pygame.draw.rect(screen, (0,0,0), track_rect, 1)
            thumb_h = max(20, int(bar_h * settings_view_h / settings_content_h))
            thumb_y = bar_y + int((bar_h - thumb_h) * (settings_scroll / max_scroll)) if max_scroll else bar_y
            thumb_rect = pygame.Rect(bar_x+1, thumb_y+1, 6, thumb_h-2)
            pygame.draw.rect(screen, (220,220,220), thumb_rect)
            settings_scrollbar["track"] = track_rect
            settings_scrollbar["thumb"] = thumb_rect

        hint = _hint_text(
            "Click items or use ↑/↓  ←/→  Enter. Wheel scroll. Esc back.",
            "Use D-Pad/Stick to move, A to adjust, B to go back."
        )
        hint_surf = render_text_fit(screen, hint, inner_w, font)
        screen.blit(hint_surf, (inner_x, box.bottom - 50))
        return idx_to_item

    def draw_controls_page():
        win_w, win_h = screen.get_size()
        screen.fill(COLOR_BG)
        title_s = font_title.render("Controls", True, COLOR_TEXT)
        screen.blit(title_s, (win_w//2 - title_s.get_width()//2, 30))
        rect_w = int(win_w * 0.78)
        rect_h = int(win_h * 0.72)
        rect = pygame.Rect(win_w//2 - rect_w//2, win_h//2 - rect_h//2 + 10, rect_w, rect_h)
        pygame.draw.rect(screen, (0,0,0), rect, 2)
        inner = pygame.Rect(rect.left + 30, rect.top + 60, rect.width - 60, rect.height - 90)
        paragraphs = [
            'Keyboard/Mouse:',
            '- Click a tile, then a neighboring tile to swap.',
            '- Arrow Keys move cursor; Enter selects/swaps.',
            '- N: settings during game; R: reshuffle; Esc: back.',
            '',
            'Controller:',
            '- D-Pad / Left Stick: move in menus and on board.',
            '- A: select/confirm (menu Enter; in-game select/swap).',
            '- B: back/cancel (menu Esc; deselect in-game).',
            '- Start: open settings (same as N).',
        ]
        y = inner.top
        for paragraph in paragraphs:
            y = draw_wrapped_text(screen, paragraph, pygame.Rect(inner.left, y, inner.width, 10**6), font)
            y += 8

    def draw_save_setup():
        nonlocal pre_save_hitboxes, pre_save_input_rect
        win_w, win_h = screen.get_size()
        screen.fill(COLOR_BG)
        title = font_title.render("Name Your Save", True, COLOR_TEXT)
        screen.blit(title, (win_w//2 - title.get_width()//2, 40))
        box_w = min(win_w - 160, 720)
        box_h = 360
        box = pygame.Rect(win_w//2 - box_w//2, win_h//2 - box_h//2, box_w, box_h)
        pygame.draw.rect(screen, (30, 30, 30), box)
        pygame.draw.rect(screen, (220, 220, 220), box, 2)
        desc = "Enter a save name for this session before configuring the game."
        desc_rect = pygame.Rect(box.left + 40, box.top + 70, box.width - 80, 80)
        draw_wrapped_text(screen, desc, desc_rect, font)
        input_rect = pygame.Rect(box.left + 40, box.top + 170, box.width - 80, 58)
        pre_save_input_rect = input_rect
        pygame.draw.rect(screen, (45, 45, 45), input_rect)
        border_col = (255, 220, 60) if _osk_input_active() else (120, 120, 120)
        pygame.draw.rect(screen, border_col, input_rect, 2)
        text_value = pre_save_holder[0]
        text_surf = render_text_fit(screen, text_value or " ", input_rect.width - 20, font_big, color=COLOR_TEXT, min_size=18)
        text_rect = text_surf.get_rect(center=input_rect.center)
        screen.blit(text_surf, text_rect.topleft)
        if state == "save_setup" and _blink_on():
            _draw_caret(screen, text_rect, COLOR_TEXT)
        caret_active = (state == "save_setup" and editing_name == 0 and _blink_on())
        if caret_active:
            _draw_caret(screen, text_rect, COLOR_TEXT)
        hint = _hint_text(
            "Type a name, press Enter to continue. Esc to cancel.",
            "Use on-screen keyboard (A to type). Press B to close."
        )
        hint_surf = render_text_fit(screen, hint, input_rect.width, font)
        screen.blit(hint_surf, (input_rect.left, input_rect.bottom + 12))
        if pre_save_error:
            err_surf = font.render(pre_save_error, True, (255, 120, 120))
            screen.blit(err_surf, (input_rect.left, input_rect.bottom + 36))
        btn_y = box.bottom - 90
        btn_gap = 30
        btn_w = 200
        btn_h = 54
        total = len(pre_save_buttons)
        total_w = total * btn_w + (total - 1) * btn_gap
        start_x = box.centerx - total_w // 2
        pre_save_hitboxes = []
        for idx, label in enumerate(pre_save_buttons):
            rect = pygame.Rect(start_x + idx * (btn_w + btn_gap), btn_y, btn_w, btn_h)
            selected = (idx == pre_save_btn_idx)
            fill = (255, 220, 60) if selected else (200, 200, 200)
            outline = (255, 255, 255) if selected else (120, 120, 120)
            pygame.draw.rect(screen, fill, rect)
            pygame.draw.rect(screen, outline, rect, 2)
            label_surf = render_text_fit(screen, label, rect.width - 16, font_big, color=(0, 0, 0), min_size=16)
            screen.blit(label_surf, (rect.centerx - label_surf.get_width()//2, rect.centery - label_surf.get_height()//2))
            pre_save_hitboxes.append((rect, idx))

    def draw_rules():
        nonlocal rules_scroll, rules_scrollbar, rules_content_h, rules_view_h
        win_w, win_h = screen.get_size()
        screen.fill(COLOR_BG)
        draw_text(screen, font_title, "Rules", (win_w//2 - font_title.size("Rules")[0]//2, 30))
        box = pygame.Rect(int(win_w*0.1), 100, int(win_w*0.8), int(win_h*0.75))
        pygame.draw.rect(screen, (0,0,0), box, 2)
        inner = pygame.Rect(box.left+30, box.top+30, box.width-90, box.height-90)
        rules_view_h = inner.height
        sections = [
            ("Goal", "Swap adjacent tiles to create lines of three or more matching colors. Long chains award more points during cascades."),
            ("Valid Matches", "Horizontal, vertical, L-shaped, T-shaped, and plus-shaped (+) lines all count as one combined clear."),
            ("Bomb Rule", "When Bombs are enabled, matching a 2x2 square detonates a 4x4 area, clearing every tile inside and granting bonus points."),
            ("Color Blast", "With Color Blast enabled, any tile touching the matched group and sharing its color also clears. Build wide matches to sweep nearby clusters."),
            ("Turn Flow", "Every swap must create at least one match or special effect. Cascades continue automatically and all cleared tiles add to the current player's score."),
        ]
        lines = []
        y = 0
        for title, paragraph in sections:
            title_surf = font_big.render(title, True, COLOR_TEXT)
            lines.append((title_surf, y))
            y += title_surf.get_height() + 6
            words = paragraph.split()
            line = ""
            wrapped = []
            for w in words:
                test = (line + " " + w).strip()
                img = font_big.render(test, True, COLOR_TEXT)
                if img.get_width() <= inner.width:
                    line = test
                else:
                    wrapped.append(line)
                    line = w
            if line:
                wrapped.append(line)
            for text_line in wrapped:
                img = font_big.render(text_line, True, COLOR_TEXT)
                lines.append((img, y))
                y += img.get_height() + 4
            y += 18
        rules_content_h = y
        max_scroll = max(0, rules_content_h - rules_view_h)
        rules_scroll = max(0, min(rules_scroll, max_scroll))
        prev_clip = screen.get_clip()
        screen.set_clip(inner)
        for surf, base_y in lines:
            draw_y = inner.top + base_y - rules_scroll
            if draw_y + surf.get_height() < inner.top - 10 or draw_y > inner.bottom + 10:
                continue
            screen.blit(surf, (inner.left, draw_y))
        screen.set_clip(prev_clip)
        if rules_content_h > rules_view_h:
            track = pygame.Rect(inner.right + 20, inner.top, 10, inner.height)
            rules_scrollbar["track"] = track
            thumb_h = max(24, int(track.height * (rules_view_h / rules_content_h)))
            if max_scroll > 0:
                thumb_y = track.top + int((rules_scroll / max_scroll) * (track.height - thumb_h))
            else:
                thumb_y = track.top
            thumb_rect = pygame.Rect(track.left, min(track.bottom - thumb_h, thumb_y), track.width, thumb_h)
            rules_scrollbar["thumb"] = thumb_rect
            pygame.draw.rect(screen, (80,80,80), track)
            pygame.draw.rect(screen, (200,200,200), thumb_rect)
        else:
            rules_scrollbar["track"] = None
            rules_scrollbar["thumb"] = None
        hint_text = _hint_text("Esc to return", "Press B to return")
        hint = font.render(hint_text, True, COLOR_TEXT)
        screen.blit(hint, (win_w//2 - hint.get_width()//2, box.bottom + 10))

    def draw_confirm_exit():
        nonlocal confirm_hitboxes, confirm_idx, save_prompt_input_rect
        win_w, win_h = screen.get_size()
        overlay = pygame.Surface((win_w, win_h), pygame.SRCALPHA)
        overlay.fill((0, 0, 0, 160))
        screen.blit(overlay, (0, 0))
        bw = min(640, win_w - 120)
        base_height = 260 if not save_prompt_active else 340
        bh = min(base_height, win_h - 160)
        box = pygame.Rect(win_w//2 - bw//2, win_h//2 - bh//2, bw, bh)
        pygame.draw.rect(screen, (30, 30, 30), box)
        pygame.draw.rect(screen, (220, 220, 220), box, 2)
        title = font_big.render(confirm_prompt, True, COLOR_TEXT)
        screen.blit(title, (box.centerx - title.get_width()//2, box.top + 24))
        confirm_hitboxes = []
        content_y = box.top + 90
        if save_prompt_active:
            save_prompt_input_rect = None
            input_rect = pygame.Rect(box.left + 50, content_y, box.width - 100, 54)
            pygame.draw.rect(screen, (45, 45, 45), input_rect)
            border_col = (255, 220, 60) if osk_active else (120, 120, 120)
            pygame.draw.rect(screen, border_col, input_rect, 2)
            display_text = save_name_holder[0] or " "
            text_surf = font_big.render(display_text or " ", True, COLOR_TEXT)
            text_rect = text_surf.get_rect(center=input_rect.center)
            screen.blit(text_surf, text_rect.topleft)
            if save_prompt_focus == "input" and not _osk_input_active() and _blink_on():
                _draw_caret(screen, text_rect, COLOR_TEXT)
            hint = _hint_text(
                "Use keyboard to edit name. Enter to confirm, Esc to cancel.",
                "Use on-screen keyboard (A to type). Press B to close."
            )
            hint_surf = render_text_fit(screen, hint, input_rect.width, font)
            screen.blit(hint_surf, (input_rect.left, input_rect.bottom + 12))
            save_prompt_input_rect = input_rect
            if save_prompt_error:
                err = font.render(save_prompt_error, True, (255, 120, 120))
                screen.blit(err, (input_rect.left, input_rect.bottom + 38))
        else:
            save_prompt_input_rect = None
        by = box.bottom - 80
        count = max(1, len(confirm_options))
        gap = 32
        btn_w = max(150, min(240, (box.width - 100 - gap * (count - 1)) // count))
        btn_h = 52
        total_w = count * btn_w + (count - 1) * gap
        x = box.centerx - total_w//2
        if count > 0:
            confirm_idx = max(0, min(confirm_idx, count - 1))
        for idx, label in enumerate(confirm_options):
            rect = pygame.Rect(x, by, btn_w, btn_h)
            selected = (idx == confirm_idx)
            fill = (255, 220, 60) if selected else (200, 200, 200)
            outline = (255, 255, 255) if selected else (120, 120, 120)
            pygame.draw.rect(screen, fill, rect)
            pygame.draw.rect(screen, outline, rect, 2)
            label_surf = render_text_fit(screen, label, rect.width - 12, font_big, color=(0, 0, 0), min_size=14)
            screen.blit(label_surf, (rect.centerx - label_surf.get_width()//2, rect.centery - label_surf.get_height()//2))
            confirm_hitboxes.append((rect, idx))
            x += btn_w + gap

    def draw_game_over():
        screen.fill(COLOR_BG)
        draw_grid(screen)
        draw_board(screen, board, hidden=set(), selected=None, hover=None)
        draw_panel()
        # Winner
        best = max(scores)
        winners = [player_names[i] for i, s in enumerate(scores) if s == best]
        if len(winners) == 1:
            title = f"Game Over — Winner: {winners[0]}"
        else:
            title = "Game Over — Draw"
        sub = "Final Score — " + " | ".join([f"{player_names[i]}: {scores[i]}" for i in range(players_count)])
        end_hint = _hint_text("Press Enter to return to Menu", "Press B to return to Menu")
        draw_center_banner(screen, title, font_big, f"{sub}   ({end_hint})")

    def maybe_rumble(is_cascade: bool):
        if not gp or not config.rumble_on:
            return
        if input_mode != "controller":
            return
        if not current_move_is_human:
            return
        if mode == "PvC" and 0 <= chain_owner < len(player_names) and player_names[chain_owner].lower() == "computer":
            return
        try:
            if hasattr(gp, "rumble"):
                strength = 0.75 if not is_cascade else 0.55
                duration = 220 if not is_cascade else 150
                gp.rumble(strength, strength, duration)
        except Exception:
            pass

    cursor = (COLS//2, ROWS//2)

    running = True
    idx_map = []

    while running:
        dt = clock.tick(FPS)
        ensure_gamepad()
        if input_mode == "mouse":
            raw_mouse = pygame.mouse.get_pos()
            logical_mouse = to_logical(raw_mouse)
            if logical_mouse is not None:
                mx, my = logical_mouse
                hover = board_from_mouse(mx, my)
            else:
                mx = my = None
                hover = None
        else:
            raw_mouse = None
            logical_mouse = None
            mx = my = None
            hover = None
        for event in pygame.event.get():
            if event.type == pygame.MOUSEMOTION:
                set_input_mode("mouse")
            if _osk_input_active():
                if _osk_handle_event(event):
                    continue
            if event.type == pygame.QUIT:
                running = False

            elif event.type in (pygame.VIDEORESIZE, getattr(pygame, 'WINDOWRESIZED', pygame.NOEVENT)):
                if display_mode == 'windowed':
                    try:
                        last_windowed_size = (event.w, event.h)
                    except Exception:
                        last_windowed_size = getattr(event, 'size', last_windowed_size)
                    try:
                        window_surf = pygame.display.set_mode(last_windowed_size, pygame.RESIZABLE)
                        screen = pygame.Surface(BASE_SIZE).convert(window_surf)
                    except Exception:
                        screen = pygame.Surface(BASE_SIZE).convert()
                    ww, wh = window_surf.get_size() if window_surf else BASE_SIZE
                    sw, sh = BASE_SIZE
                    if ww > 0 and wh > 0:
                        scale = min(ww / sw, wh / sh)
                        if scale <= 0:
                            scale = 1.0
                        present_scale = scale
                        present_offset = ((ww - sw * scale) / 2.0, (wh - sh * scale) / 2.0)
                    else:
                        present_scale = 1.0
                        present_offset = (0, 0)
                continue

            elif event.type == pygame.MOUSEWHEEL:
                if input_mode != "mouse":
                    continue
                # Scroll in tournament/settings lists
                if state == "tournament" and t_state == "t_settings":
                    step = 60
                    max_scroll = max(0, settings_content_h - settings_view_h)
                    settings_scroll = min(max_scroll, max(0, settings_scroll - event.y*step))
                elif state == "settings":
                    step = 60
                    max_scroll = max(0, settings_content_h - settings_view_h)
                    settings_scroll = min(max_scroll, max(0, settings_scroll - event.y*step))
                elif state == "rules":
                    step = 60
                    max_scroll = max(0, rules_content_h - rules_view_h)
                    rules_scroll = min(max_scroll, max(0, rules_scroll - event.y*step))
                continue
            
            elif event.type in (pygame.MOUSEMOTION, pygame.MOUSEBUTTONDOWN, pygame.MOUSEBUTTONUP) and (state == "menu" or state == "settings" or state == "time_mode" or state == "time_settings" or state == "load_select" or state == "confirm_exit" or state == "rules" or state == "save_setup" or (state == "tournament" and t_state == "t_settings")):
                # Mouse interactions for menus and scroll areas
                if event.type == pygame.MOUSEMOTION:
                    set_input_mode("mouse")
                elif input_mode != "mouse":
                    continue
                pos = to_logical(event.pos) if hasattr(event, "pos") else None
                if state == "menu":
                    if event.type == pygame.MOUSEMOTION:
                        if pos is None:
                            tooltip_text = None
                        else:
                            for i,(r, idx) in enumerate(menu_hitboxes):
                                if r.collidepoint(pos):
                                    menu_idx = idx
                                    tooltip_text = menu_tooltip(idx)
                                    break
                            else:
                                tooltip_text = None
                    if event.type == pygame.MOUSEBUTTONDOWN and event.button == 1 and pos is not None:
                        for r, idx in menu_hitboxes:
                            if r.collidepoint(pos):
                                try: play_click()
                                except Exception: pass
                                menu_idx = idx
                                activate_menu_selection()
                                break
                    continue

                if state == "load_select":
                    if load_view == "list":
                        if event.type == pygame.MOUSEMOTION and pos is not None:
                            for rect, idx in load_hitboxes:
                                if rect.collidepoint(pos):
                                    load_menu_idx = idx
                                    break
                        if event.type == pygame.MOUSEBUTTONDOWN and event.button == 1 and pos is not None:
                            clicked = next((idx for rect, idx in load_hitboxes if rect.collidepoint(pos)), None)
                            if clicked is not None:
                                load_menu_idx = clicked
                                try:
                                    play_click()
                                except Exception:
                                    pass
                                if clicked == 0:
                                    start_new_from_load()
                                else:
                                    if clicked - 1 < len(load_saves):
                                        load_selected = load_saves[clicked - 1]
                                        load_view = "detail"
                                        load_detail_idx = 0
                                        load_detail_hitboxes = []
                    else:
                        if event.type == pygame.MOUSEMOTION and pos is not None:
                            for rect, idx in load_detail_hitboxes:
                                if rect.collidepoint(pos):
                                    load_detail_idx = idx
                                    break
                        if event.type == pygame.MOUSEBUTTONDOWN and event.button == 1 and pos is not None:
                            clicked = next((idx for rect, idx in load_detail_hitboxes if rect.collidepoint(pos)), None)
                            if clicked is not None:
                                load_detail_idx = clicked
                                try:
                                    play_click()
                                except Exception:
                                    pass
                                if clicked == 0:
                                    ok, _ = load_selected_save()
                                    if not ok:
                                        state = "menu"
                                elif clicked == 1:
                                    delete_selected_save()
                                else:
                                    load_view = "list"
                                    load_selected = None
                                    load_detail_idx = 0
                        elif event.type == pygame.MOUSEBUTTONDOWN and event.button == 3:
                            load_view = "list"
                            load_selected = None
                            load_detail_idx = 0
                    if event.type == pygame.MOUSEBUTTONDOWN and event.button == 3 and load_view == "list":
                        reset_load_view()
                        state = "menu"
                    continue

                if state == "rules":
                    if event.type == pygame.MOUSEBUTTONDOWN and event.button == 1 and pos is not None:
                        if rules_scrollbar["thumb"] and rules_scrollbar["thumb"].collidepoint(pos):
                            dragging_scroll = "rules"
                            drag_offset_y = pos[1] - rules_scrollbar["thumb"].y
                        elif rules_scrollbar["track"] and rules_scrollbar["track"].collidepoint(pos):
                            track = rules_scrollbar["track"]
                            thumb = rules_scrollbar["thumb"]
                            rel = (pos[1] - track.top) / max(1, track.height - (thumb.height if thumb else 0) if thumb else track.height)
                            max_scroll = max(0, rules_content_h - rules_view_h)
                            rules_scroll = int(max_scroll * min(1.0, max(0.0, rel)))
                    continue

                if state == "save_setup":
                    if event.type == pygame.MOUSEMOTION and pos is not None:
                        for rect, idx in pre_save_hitboxes:
                            if rect.collidepoint(pos):
                                pre_save_btn_idx = idx
                                break
                    if event.type == pygame.MOUSEBUTTONDOWN and pos is not None:
                        if event.button == 1:
                            clicked = next((idx for rect, idx in pre_save_hitboxes if rect.collidepoint(pos)), None)
                            if clicked is not None:
                                try:
                                    play_click()
                                except Exception:
                                    pass
                                pre_save_btn_idx = clicked
                                if clicked == 0:
                                    confirm_save_setup()
                                else:
                                    cancel_save_setup()
                            elif pre_save_input_rect and pre_save_input_rect.collidepoint(pos):
                                pre_save_from_controller = False
                                osk_active = False
                                osk_case_select = None
                                editing_name = None
                        elif event.button == 3:
                            cancel_save_setup()
                    continue

                if state == "confirm_exit":
                    if event.type == pygame.MOUSEMOTION and pos is not None:
                        for rect, idx in confirm_hitboxes:
                            if rect.collidepoint(pos):
                                confirm_idx = idx
                                break
                    if event.type == pygame.MOUSEBUTTONDOWN and event.button == 1 and pos is not None:
                        if save_prompt_active and save_prompt_input_rect and save_prompt_input_rect.collidepoint(pos):
                            focus_save_prompt_input()
                            continue
                        clicked = next((idx for rect, idx in confirm_hitboxes if rect.collidepoint(pos)), None)
                        if clicked is not None:
                            try:
                                play_click()
                            except Exception:
                                pass
                            handle_confirm_selection(clicked)
                    if event.type == pygame.MOUSEBUTTONDOWN and event.button == 3:
                        if save_prompt_active:
                            cancel_save_naming()
                        else:
                            cancel_confirm_exit()
                    continue

                if state == "confirm_exit":
                    options_count = max(1, len(confirm_options))
                    if save_prompt_active:
                        if event.key in (pygame.K_DOWN, pygame.K_s):
                            if save_prompt_focus == "input":
                                if not osk_active:
                                    focus_save_prompt_buttons()
                                continue
                        elif event.key in (pygame.K_UP, pygame.K_w):
                            if save_prompt_focus == "buttons":
                                focus_save_prompt_input()
                                continue
                        elif save_prompt_focus == "buttons":
                            if event.key not in (pygame.K_LEFT, pygame.K_RIGHT, pygame.K_a, pygame.K_d, pygame.K_RETURN, pygame.K_SPACE, pygame.K_ESCAPE):
                                focus_save_prompt_input()
                    if not save_prompt_active or save_prompt_focus == "buttons":
                        if event.key in (pygame.K_LEFT, pygame.K_a):
                            confirm_idx = (confirm_idx - 1) % options_count
                            continue
                        elif event.key in (pygame.K_RIGHT, pygame.K_d):
                            confirm_idx = (confirm_idx + 1) % options_count
                            continue
                    if event.key in (pygame.K_LEFT, pygame.K_a):
                        continue
                    elif event.key in (pygame.K_RETURN, pygame.K_SPACE):
                        handle_confirm_selection()
                        continue
                    elif event.key == pygame.K_ESCAPE:
                        if save_prompt_active:
                            cancel_save_naming()
                        else:
                            cancel_confirm_exit()
                        continue

                if state == "time_mode":
                    if event.type == pygame.MOUSEMOTION and time_hitboxes:
                        if pos is None:
                            tooltip_text = None
                        else:
                            for (r, idx) in time_hitboxes:
                                if r.collidepoint(pos):
                                    menu_idx = idx
                                    tooltip_text = time_tooltip("mode", idx)
                                    break
                            else:
                                tooltip_text = None
                    if event.type == pygame.MOUSEBUTTONDOWN and event.button == 1 and pos is not None:
                        for (r, idx) in time_hitboxes:
                            if r.collidepoint(pos):
                                play_click()
                                menu_idx = idx
                                if idx == 0:
                                    time_mode = "classic"
                                    if time_next_state == "tournament":
                                        start_tournament_settings()
                                    else:
                                        state = "settings"
                                elif idx == 1:
                                    time_mode = "blitz"; menu_idx = 0; state = "time_settings"
                                else:
                                    state = "menu"
                                break
                    continue

                if state == "time_settings":
                    if event.type == pygame.MOUSEMOTION and time_hitboxes:
                        if pos is None:
                            tooltip_text = None
                        else:
                            for (r, idx) in time_hitboxes:
                                if r.collidepoint(pos):
                                    menu_idx = idx
                                    tooltip_text = time_tooltip("settings", idx)
                                    break
                            else:
                                tooltip_text = None
                    if event.type == pygame.MOUSEBUTTONDOWN and event.button == 1 and time_hitboxes and pos is not None:
                        for (r, idx) in time_hitboxes:
                            if r.collidepoint(pos):
                                play_click()
                                menu_idx = idx
                                if idx == 0: blitz_turn_minutes = min(5, blitz_turn_minutes + 1)
                                elif idx == 1: blitz_between_seconds = min(60, blitz_between_seconds + 1)
                                elif idx == 2:
                                    if time_next_state == "tournament":
                                        start_tournament_settings(); menu_idx = 0
                                    else:
                                        state = "settings"; menu_idx = 0
                                    time_next_state = None
                                break
                    if event.type == pygame.MOUSEBUTTONDOWN and event.button == 3 and time_hitboxes and pos is not None:
                        for (r, idx) in time_hitboxes:
                            if r.collidepoint(pos):
                                play_click()
                                menu_idx = idx
                                if idx == 0: blitz_turn_minutes = max(1, blitz_turn_minutes - 1)
                                elif idx == 1: blitz_between_seconds = max(0, blitz_between_seconds - 1)
                                break
                    continue


                # TIME MODE selection
                if state == "time_mode":
                    if event.key in (pygame.K_UP, pygame.K_w):
                        menu_idx = (menu_idx - 1) % 3
                    elif event.key in (pygame.K_DOWN, pygame.K_s):
                        menu_idx = (menu_idx + 1) % 3
                    elif event.key == pygame.K_RETURN:
                        try: play_click()
                        except Exception: pass
                        if menu_idx == 0:
                            time_mode = "classic"
                            if time_next_state == "tournament":
                                start_tournament_settings()
                            else:
                                state = "settings"
                        elif menu_idx == 1:
                            time_mode = "blitz"
                            menu_idx = 0
                            state = "time_settings"
                        else:
                            state = "menu"
                    elif event.key == pygame.K_ESCAPE:
                        state = "menu"
                    continue

                if state == "time_settings":
                    if event.key in (pygame.K_UP, pygame.K_w):
                        menu_idx = (menu_idx - 1) % 3
                    elif event.key in (pygame.K_DOWN, pygame.K_s):
                        menu_idx = (menu_idx + 1) % 3
                    elif event.key in (pygame.K_LEFT, pygame.K_a):
                        if menu_idx == 0:
                            blitz_turn_minutes = max(1, blitz_turn_minutes - 1)
                        elif menu_idx == 1:
                            blitz_between_seconds = max(0, blitz_between_seconds - 1)
                    elif event.key in (pygame.K_RIGHT, pygame.K_d):
                        if menu_idx == 0:
                            blitz_turn_minutes = min(5, blitz_turn_minutes + 1)
                        elif menu_idx == 1:
                            blitz_between_seconds = min(60, blitz_between_seconds + 1)
                    elif event.key == pygame.K_RETURN:
                        try: play_click()
                        except Exception: pass
                        if time_next_state == "tournament":
                            start_tournament_settings()
                            menu_idx = 0
                        else:
                            state = "settings"
                            menu_idx = 0
                        time_next_state = None
                    elif event.key == pygame.K_ESCAPE:
                        state = "time_mode"; menu_idx = 0
                    continue

                if state == "settings":
                    if event.type == pygame.MOUSEMOTION and settings_hitboxes:
                        if pos is None:
                            tooltip_text = None
                        else:
                            for (r, kv) in settings_hitboxes:
                                if r.collidepoint(pos):
                                    # move selection for visual feedback
                                    try:
                                        settings_idx = idx_map.index(kv)
                                    except Exception:
                                        pass
                                    tooltip_text = settings_tooltip(kv[0])
                                    break
                            else:
                                tooltip_text = None
                    if event.type == pygame.MOUSEBUTTONDOWN and event.button == 1 and pos is not None:
                        # Scrollbar interactions first
                        if settings_scrollbar["thumb"] and settings_scrollbar["thumb"].collidepoint(pos):
                            dragging_scroll = "settings"; drag_offset_y = pos[1] - settings_scrollbar["thumb"].y
                            continue
                        if settings_scrollbar["track"] and settings_scrollbar["track"].collidepoint(pos):
                            track = settings_scrollbar["track"]; thumb = settings_scrollbar["thumb"]
                            rel = (pos[1] - track.top) / max(1, track.height - (thumb.height if thumb else 0))
                            settings_scroll = int(rel * max(0, settings_content_h - settings_view_h))
                            continue
                        # Click items
                        clicked = next(((r,kv) for (r,kv) in settings_hitboxes if r.collidepoint(pos)), None)
                        if clicked:
                            play_click()
                            _, (kind, idx) = clicked
                            if kind == "name":
                                if mode == "PvP" or (mode == "PvC" and idx == 0):
                                    if player_names[idx].startswith("Player ") and player_names[idx].endswith(str(idx+1)):
                                        player_names[idx] = ""
                                    editing_name = idx
                                    osk_case_select = None
                                    osk_row = 0
                                    osk_col = 0
                                    osk_active = False
                            elif kind == "players" and mode == "PvP":
                                players_count += 1
                                while len(player_names) < players_count:
                                    player_names.append(f"Player {len(player_names)+1}")
                            elif kind == "moves":
                                moves_per_round = min(MAX_MOVES_PER_ROUND, moves_per_round + 1)
                            elif kind == "rounds":
                                total_rounds = min(MAX_TOTAL_ROUNDS, total_rounds + 1)
                            elif kind == "order":
                                turn_order_mode = "consecutive" if turn_order_mode == "round_robin" else "round_robin"
                            elif kind == "bomb":
                                bomb_enabled = not bomb_enabled
                            elif kind == "color":
                                color_chain_enabled = not color_chain_enabled
                            elif kind == "color":
                                color_chain_enabled = not color_chain_enabled
                            elif kind == "start":
                                confirm_settings_and_begin()
                            elif kind == "back":
                                state = "menu"
                            continue
                    
                    elif event.type == pygame.MOUSEBUTTONDOWN and event.button == 3 and pos is not None:
                        play_click()
                        # Right-click: decrease numeric values
                        clicked = next(((r,kv) for (r,kv) in settings_hitboxes if r.collidepoint(pos)), None)
                        if clicked:
                            play_click()
                            _, (kind, idx) = clicked
                            if kind == "players" and mode == "PvP":
                                if players_count > 2:
                                    players_count -= 1
                                    if len(player_names) > players_count:
                                        player_names[:] = player_names[:players_count]
                            elif kind == "moves":
                                moves_per_round = max(MIN_MOVES_PER_ROUND, moves_per_round - 1)
                            elif kind == "rounds":
                                total_rounds = max(MIN_TOTAL_ROUNDS, total_rounds - 1)
                            # no right-click action for order/start/back/bomb
                            continue
                    if event.type == pygame.MOUSEBUTTONUP and event.button == 1 and dragging_scroll == "settings":
                        dragging_scroll = None
                        continue
                    if event.type == pygame.MOUSEMOTION and dragging_scroll == "settings":
                        track = settings_scrollbar["track"]; thumb = settings_scrollbar["thumb"]
                        if track and thumb:
                            if pos is None:
                                continue
                            new_thumb_y = max(track.top+1, min(track.bottom - thumb.height - 1, pos[1] - drag_offset_y))
                            rel = (new_thumb_y - (track.top+1)) / max(1, (track.height - thumb.height - 2))
                            settings_scroll = int(rel * max(0, settings_content_h - settings_view_h))
                        continue

                # Tournament settings mouse hover tooltips
                if state == "tournament" and t_state == "t_settings":
                    if event.type == pygame.MOUSEMOTION and t_settings_hitboxes:
                        if pos is None:
                            tooltip_text = None
                        else:
                            for (r, kv) in t_settings_hitboxes:
                                if r.collidepoint(pos):
                                    tooltip_text = t_settings_tooltip(kv[0])
                                    break
                            else:
                                tooltip_text = None

                if state == "tournament" and t_state == "t_settings":
                    if event.type == pygame.MOUSEMOTION and t_settings_hitboxes:
                        if pos is not None:
                            for (r, kv) in t_settings_hitboxes:
                                if r.collidepoint(pos):
                                    try:
                                        settings_idx = idx_map.index(kv)
                                    except Exception:
                                        pass
                                    break
                    if event.type == pygame.MOUSEBUTTONDOWN and event.button == 1 and pos is not None:
                        if t_settings_scrollbar["thumb"] and t_settings_scrollbar["thumb"].collidepoint(pos):
                            dragging_scroll = "t_settings"; drag_offset_y = pos[1] - t_settings_scrollbar["thumb"].y
                            continue
                        if t_settings_scrollbar["track"] and t_settings_scrollbar["track"].collidepoint(pos):
                            track = t_settings_scrollbar["track"]; thumb = t_settings_scrollbar["thumb"]
                            rel = (pos[1] - track.top) / max(1, track.height - (thumb.height if thumb else 0))
                            settings_scroll = int(rel * max(0, settings_content_h - settings_view_h))
                            continue
                        clicked = next(((r,kv) for (r,kv) in t_settings_hitboxes if r.collidepoint(pos)), None)
                        if clicked:
                            play_click()
                            _, (kind, idx) = clicked
                            if kind == "tp_name":
                                editing_name = idx
                                osk_case_select = None
                                osk_row = 0
                                osk_col = 0
                                osk_active = False
                            elif kind == "tp_count":
                                tournament_players.append(f"Player {len(tournament_players)+1}")
                            elif kind == "tp_moves":
                                tournament_cfg["moves_per_round"] = min(MAX_MOVES_PER_ROUND, tournament_cfg["moves_per_round"] + 1)
                            elif kind == "tp_rounds":
                                tournament_cfg["rounds_per_match"] = min(MAX_TOTAL_ROUNDS, tournament_cfg["rounds_per_match"] + 1)
                            elif kind == "tp_order":
                                tournament_cfg["order"] = "consecutive" if tournament_cfg["order"] == "round_robin" else "round_robin"
                            elif kind == "tp_color":
                                tournament_cfg["color_chain"] = not tournament_cfg.get("color_chain", False)
                            elif kind == "tp_start":
                                start_tournament()
                            elif kind == "tp_back":
                                tournament_active = False; state = "menu"
                            continue
                    
                    elif event.type == pygame.MOUSEBUTTONDOWN and event.button == 3 and pos is not None:
                        play_click()
                        # Right-click: decrease numeric values in tournament settings
                        clicked = next(((r,kv) for (r,kv) in t_settings_hitboxes if r.collidepoint(pos)), None)
                        if clicked:
                            play_click()
                            _, (kind, idx) = clicked
                            if kind == "tp_count":
                                if len(tournament_players) > 2:
                                    tournament_players.pop()
                            elif kind == "tp_moves":
                                tournament_cfg["moves_per_round"] = max(MIN_MOVES_PER_ROUND, tournament_cfg["moves_per_round"] - 1)
                            elif kind == "tp_rounds":
                                tournament_cfg["rounds_per_match"] = max(MIN_TOTAL_ROUNDS, tournament_cfg["rounds_per_match"] - 1)
                            # no right-click for order/start/back
                            continue
                    if event.type == pygame.MOUSEBUTTONUP and event.button == 1 and dragging_scroll == "t_settings":
                        dragging_scroll = None
                        continue
                    if event.type == pygame.MOUSEMOTION and dragging_scroll == "t_settings":
                        track = t_settings_scrollbar["track"]; thumb = t_settings_scrollbar["thumb"]
                        if track and thumb:
                            if pos is None:
                                continue
                            new_thumb_y = max(track.top+1, min(track.bottom - thumb.height - 1, pos[1] - drag_offset_y))
                            rel = (new_thumb_y - (track.top+1)) / max(1, (track.height - thumb.height - 2))
                            settings_scroll = int(rel * max(0, settings_content_h - settings_view_h))
                            continue
                    if event.type == pygame.MOUSEBUTTONUP and event.button == 1 and dragging_scroll == "rules":
                        dragging_scroll = None
                        continue
                    if event.type == pygame.MOUSEMOTION and dragging_scroll == "rules":
                        track = rules_scrollbar["track"]; thumb = rules_scrollbar["thumb"]
                        if track and thumb:
                            if pos is None:
                                continue
                            new_thumb_y = max(track.top, min(track.bottom - thumb.height, pos[1] - drag_offset_y))
                            rel = (new_thumb_y - track.top) / max(1, track.height - thumb.height)
                            max_scroll = max(0, rules_content_h - rules_view_h)
                            rules_scroll = int(rel * max_scroll)
                        continue
            elif joy_added_event and event.type == joy_added_event:
                ensure_gamepad(force=True)
                continue
            elif joy_removed_event and event.type == joy_removed_event:
                if gp:
                    get_id = getattr(gp, "get_instance_id", None)
                    if callable(get_id):
                        try:
                            if getattr(event, "instance_id", None) == get_id():
                                gp = None
                        except Exception:
                            gp = None
                    else:
                        gp = None
                ensure_gamepad(force=True)
                continue
            elif event.type == pygame.JOYHATMOTION:
                hx, hy = event.value
                if hx != 0 or hy != 0:
                    set_input_mode("controller")
                    hover = None
                    tooltip_text = None
                if _osk_input_active():
                    if state == "confirm_exit" and save_prompt_active and hy == -1 and not osk_active:
                        focus_save_prompt_buttons()
                        continue
                    if osk_case_select:
                        if hx == -1:
                            osk_case_select['choice'] = 0
                        elif hx == 1:
                            osk_case_select['choice'] = 1
                    else:
                        if hx == -1: _osk_move(-1, 0)
                        elif hx == 1: _osk_move(1, 0)
                        if hy == 1: _osk_move(0, -1)
                        elif hy == -1: _osk_move(0, 1)
                    continue
                if state == "menu":
                    if hy == 1:
                        menu_idx = (menu_idx - 1) % menu_count
                    elif hy == -1:
                        menu_idx = (menu_idx + 1) % menu_count
                elif state == "load_select":
                    if load_view == "list":
                        total_entries = len(load_saves) + 1
                        if total_entries == 0:
                            total_entries = 1
                        if hy == 1:
                            load_menu_idx = (load_menu_idx - 1) % total_entries
                        elif hy == -1:
                            load_menu_idx = (load_menu_idx + 1) % total_entries
                    else:
                        options = max(1, len(load_detail_hitboxes)) if load_detail_hitboxes else 3
                        if hx == -1:
                            load_detail_idx = (load_detail_idx - 1) % options
                        elif hx == 1:
                            load_detail_idx = (load_detail_idx + 1) % options
                elif state == "idle":
                    c, r = cursor
                    if hx == -1: cursor = (max(0, c-1), r)
                    elif hx == 1: cursor = (min(COLS-1, c+1), r)
                    if hy == 1: cursor = (c, max(0, r-1))
                    elif hy == -1: cursor = (c, min(ROWS-1, r+1))
                elif state in ("time_mode", "time_settings"):
                    if hy == 1: menu_idx = (menu_idx - 1) % 3
                    elif hy == -1: menu_idx = (menu_idx + 1) % 3
                elif state == "settings":
                    if idx_map:
                        if hy == 1: settings_idx = (settings_idx - 1) % len(idx_map)
                        elif hy == -1: settings_idx = (settings_idx + 1) % len(idx_map)
                elif state == "rules":
                    step = 60
                    max_scroll = max(0, rules_content_h - rules_view_h)
                    if hy == 1:
                        rules_scroll = max(0, rules_scroll - step)
                    elif hy == -1:
                        rules_scroll = min(max_scroll, rules_scroll + step)
                elif state == "save_setup":
                    if hx == -1:
                        pre_save_btn_idx = max(0, pre_save_btn_idx - 1)
                    elif hx == 1:
                        pre_save_btn_idx = min(len(pre_save_buttons) - 1, pre_save_btn_idx + 1)
                elif state == "tournament" and t_state == "t_settings":
                    if hy == 1: settings_idx = max(0, settings_idx - 1)
                    elif hy == -1: settings_idx = settings_idx + 1
                elif state == "confirm_exit":
                    options_count = max(1, len(confirm_options))
                    if save_prompt_active:
                        if save_prompt_focus == "input" and hy == -1:
                            if not osk_active:
                                focus_save_prompt_buttons()
                            continue
                        if save_prompt_focus == "buttons" and hy == 1:
                            focus_save_prompt_input()
                            continue
                    if not save_prompt_active or save_prompt_focus == "buttons":
                        if hx == -1 or hy == 1:
                            confirm_idx = (confirm_idx - 1) % options_count
                        elif hx == 1 or hy == -1:
                            confirm_idx = (confirm_idx + 1) % options_count
                continue
            elif event.type == pygame.JOYAXISMOTION and gp:
                try:
                    x = gp.get_axis(0); y = gp.get_axis(1)
                except Exception:
                    x = y = 0.0
                dx = -1 if x < -gp_deadzone else (1 if x > gp_deadzone else 0)
                dy = -1 if y < -gp_deadzone else (1 if y > gp_deadzone else 0)
                d = (dx, dy)
                if d != (0,0):
                    set_input_mode("controller")
                    hover = None
                    tooltip_text = None
                    now = pygame.time.get_ticks()
                    if d != gp_last_dir or now - gp_last_ms >= gp_repeat_ms:
                        gp_last_dir = d; gp_last_ms = now
                        if _osk_input_active():
                            if state == "confirm_exit" and save_prompt_active and dy == 1:
                                if not osk_active:
                                    focus_save_prompt_buttons()
                                    continue
                            if osk_case_select:
                                if dx == -1:
                                    osk_case_select['choice'] = 0
                                elif dx == 1:
                                    osk_case_select['choice'] = 1
                            else:
                                if dx != 0: _osk_move(dx, 0)
                                if dy != 0: _osk_move(0, dy)
                        elif state == "menu":
                            if dy == -1:
                                menu_idx = (menu_idx - 1) % menu_count
                            elif dy == 1:
                                menu_idx = (menu_idx + 1) % menu_count
                        elif state == "load_select":
                            total_entries = len(load_saves) + 1
                            if total_entries == 0:
                                total_entries = 1
                            if load_view == "list":
                                if dy == -1:
                                    load_menu_idx = (load_menu_idx - 1) % total_entries
                                elif dy == 1:
                                    load_menu_idx = (load_menu_idx + 1) % total_entries
                            else:
                                options = max(1, len(load_detail_hitboxes)) if load_detail_hitboxes else 3
                                if dx == -1:
                                    load_detail_idx = (load_detail_idx - 1) % options
                                elif dx == 1:
                                    load_detail_idx = (load_detail_idx + 1) % options
                        elif state == "idle":
                            c, r = cursor
                            if dx == -1: c = max(0, c-1)
                            elif dx == 1: c = min(COLS-1, c+1)
                            if dy == -1: r = max(0, r-1)
                            elif dy == 1: r = min(ROWS-1, r+1)
                            cursor = (c, r)
                        elif state in ("time_mode", "time_settings"):
                            if dy == -1: menu_idx = (menu_idx - 1) % 3
                            elif dy == 1: menu_idx = (menu_idx + 1) % 3
                        elif state == "settings" and idx_map:
                            if dy == -1: settings_idx = (settings_idx - 1) % len(idx_map)
                            elif dy == 1: settings_idx = (settings_idx + 1) % len(idx_map)
                        elif state == "rules":
                            step = 60
                            max_scroll = max(0, rules_content_h - rules_view_h)
                            if dy == -1:
                                rules_scroll = max(0, rules_scroll - step)
                            elif dy == 1:
                                rules_scroll = min(max_scroll, rules_scroll + step)
                        elif state == "save_setup":
                            if dx == -1:
                                pre_save_btn_idx = max(0, pre_save_btn_idx - 1)
                            elif dx == 1:
                                pre_save_btn_idx = min(len(pre_save_buttons) - 1, pre_save_btn_idx + 1)
                        elif state == "confirm_exit":
                            options_count = max(1, len(confirm_options))
                            if save_prompt_active:
                                if save_prompt_focus == "input" and dy == 1:
                                    if not osk_active:
                                        focus_save_prompt_buttons()
                                    continue
                                if save_prompt_focus == "buttons" and dy == -1:
                                    focus_save_prompt_input()
                                    continue
                            if not save_prompt_active or save_prompt_focus == "buttons":
                                if dx == -1 or dy == -1:
                                    confirm_idx = (confirm_idx - 1) % options_count
                                elif dx == 1 or dy == 1:
                                    confirm_idx = (confirm_idx + 1) % options_count
                else:
                    gp_last_dir = (0,0)
                continue
            elif event.type == pygame.JOYBUTTONDOWN:
                set_input_mode("controller")
                hover = None
                tooltip_text = None
                btn = event.button
                if _osk_input_active():
                    target = _osk_target_list()
                    if osk_case_select:
                        if btn == 0:
                            _osk_commit_case()
                        elif btn == 1:
                            osk_case_select = None
                        elif btn in (7, 9):
                            osk_case_select = None
                            osk_active = False
                            editing_name = None
                            _handle_osk_closed()
                    else:
                        if btn == 0:
                            _osk_activate_selected()
                        elif btn == 1:
                            osk_active = False
                            osk_case_select = None
                            editing_name = None
                            _handle_osk_closed()
                        elif btn in (7, 9):
                            osk_active = False
                            osk_case_select = None
                            editing_name = None
                            _handle_osk_closed()
                    continue
                if btn == 0:
                    try: play_click()
                    except Exception: pass
                    if state == "load_select":
                        if load_view == "list":
                            if load_menu_idx == 0:
                                start_new_from_load()
                            else:
                                if load_menu_idx - 1 < len(load_saves):
                                    load_selected = load_saves[load_menu_idx - 1]
                                    load_view = "detail"
                                    load_detail_idx = 0
                                    load_detail_hitboxes = []
                        else:
                            if load_detail_idx == 0:
                                ok, _ = load_selected_save()
                                if not ok:
                                    state = "menu"
                            elif load_detail_idx == 1:
                                delete_selected_save()
                            else:
                                load_view = "list"
                                load_selected = None
                                load_detail_idx = 0
                        continue
                    if state == "save_setup":
                        confirm_save_setup()
                        continue
                    if state == "menu":
                        activate_menu_selection()
                    elif state == "idle":
                        if not hasattr(main, "_selected"): main._selected = None
                        if main._selected is None:
                            main._selected = cursor
                        else:
                            a, b = main._selected, cursor
                            if abs(a[0]-b[0]) + abs(a[1]-b[1]) == 1 and legal_swap(board, a, b):
                                chain_owner = turn
                                start_swap(a, b)
                            main._selected = None
                    elif state == "time_mode":
                        if menu_idx == 0:
                            time_mode = "classic"; state = time_next_state if time_next_state else "settings"
                        elif menu_idx == 1:
                            time_mode = "blitz"; menu_idx = 0; state = "time_settings"
                        else:
                            state = "menu"
                    elif state == "time_settings":
                        if menu_idx == 2:
                            if time_next_state == "tournament":
                                start_tournament_settings()
                                menu_idx = 0
                            else:
                                state = "settings"; menu_idx = 0
                            time_next_state = None
                    elif state == "settings":
                        if idx_map:
                            kind, idx = idx_map[settings_idx]
                            if kind == "name":
                                if mode == "PvP" or (mode == "PvC" and idx == 0):
                                    if player_names[idx].startswith("Player ") and player_names[idx].endswith(str(idx+1)):
                                        player_names[idx] = ""
                                    editing_name = idx
                                    osk_row = 0
                                    osk_col = 0
                                    osk_case_select = None
                                    osk_active = (input_mode == "controller")
                            elif kind == "players" and mode == "PvP":
                                players_count += 1
                                while len(player_names) < players_count:
                                    player_names.append(f"Player {len(player_names)+1}")
                            elif kind == "moves":
                                moves_per_round = min(MAX_MOVES_PER_ROUND, moves_per_round + 1)
                            elif kind == "rounds":
                                total_rounds = min(MAX_TOTAL_ROUNDS, total_rounds + 1)
                            elif kind == "order":
                                turn_order_mode = "consecutive" if turn_order_mode == "round_robin" else "round_robin"
                            elif kind == "bomb":
                                bomb_enabled = not bomb_enabled
                            elif kind == "color":
                                color_chain_enabled = not color_chain_enabled
                            elif kind == "start":
                                confirm_settings_and_begin()
                            elif kind == "back":
                                state = "menu"
                    elif state in ("rules", "controls"):
                        state = "menu"
                    elif state == "tournament" and t_state == "t_bracket":
                        start_next_tournament_match()
                    elif state == "tournament" and t_state == "t_settings":
                        if idx_map:
                            kind, idx = idx_map[settings_idx]
                            if kind == "tp_name":
                                if tournament_players[idx].startswith("Player ") and tournament_players[idx].endswith(str(idx+1)):
                                    tournament_players[idx] = ""
                                editing_name = idx
                                osk_row = 0
                                osk_col = 0
                                osk_case_select = None
                                osk_active = (input_mode == "controller")
                            elif kind == "tp_color":
                                tournament_cfg["color_chain"] = not tournament_cfg.get("color_chain", False)
                    elif state == "confirm_exit":
                        handle_confirm_selection()
                        continue
                elif btn == 1:
                    if state == "load_select":
                        if load_view == "detail":
                            load_view = "list"
                            load_selected = None
                            load_detail_idx = 0
                        else:
                            reset_load_view()
                            state = "menu"
                        continue
                    if state == "save_setup":
                        cancel_save_setup()
                        continue
                    if state == "game_over":
                        apply_display_mode(MENU_SIZE)
                        state = "menu"
                        continue
                    if state in ("rules", "controls", "time_mode", "time_settings"):
                        state = "menu"
                    elif state == "idle":
                        if hasattr(main, "_selected") and main._selected is not None:
                            main._selected = None
                    elif state == "settings":
                        state = "menu"
                    elif state == "confirm_exit":
                        if save_prompt_active:
                            cancel_save_naming()
                        else:
                            cancel_confirm_exit()
                        continue
                elif btn in (7,9):
                    if state == "confirm_exit":
                        cancel_confirm_exit()
                    elif state == "save_setup":
                        cancel_save_setup()
                    elif state == "game_over":
                        apply_display_mode(MENU_SIZE)
                        state = "menu"
                    else:
                        open_exit_prompt()
                    continue
                elif btn == 2:  # X -> decrement
                    if state == "time_settings":
                        if menu_idx == 0: blitz_turn_minutes = max(1, blitz_turn_minutes - 1)
                        elif menu_idx == 1: blitz_between_seconds = max(0, blitz_between_seconds - 1)
                    elif state == "settings" and idx_map:
                        kind, idx = idx_map[settings_idx]
                        if kind == "players" and mode == "PvP" and players_count > 2:
                            players_count -= 1
                            if len(player_names) > players_count:
                                player_names[:] = player_names[:players_count]
                        elif kind == "moves":
                            moves_per_round = max(MIN_MOVES_PER_ROUND, moves_per_round - 1)
                        elif kind == "rounds":
                            total_rounds = max(MIN_TOTAL_ROUNDS, total_rounds - 1)
                    elif state == "tournament" and t_state == "t_settings":
                        # Decrease tournament numbers
                        if settings_idx >= 0:
                            # just handle moves/rounds kinds via idx_map if available
                            try:
                                kind, idx = idx_map[settings_idx]
                            except Exception:
                                kind = None
                            if kind == "tp_moves":
                                tournament_cfg["moves_per_round"] = max(MIN_MOVES_PER_ROUND, tournament_cfg["moves_per_round"] - 1)
                            elif kind == "tp_rounds":
                                tournament_cfg["rounds_per_match"] = max(MIN_TOTAL_ROUNDS, tournament_cfg["rounds_per_match"] - 1)
                            elif kind == "tp_count" and len(tournament_players) > 2:
                                tournament_players.pop()
                elif btn == 3:  # Y -> increment
                    if state == "time_settings":
                        if menu_idx == 0: blitz_turn_minutes = min(5, blitz_turn_minutes + 1)
                        elif menu_idx == 1: blitz_between_seconds = min(60, blitz_between_seconds + 1)
                    elif state == "settings" and idx_map:
                        kind, idx = idx_map[settings_idx]
                        if kind == "players" and mode == "PvP":
                            players_count += 1
                            while len(player_names) < players_count:
                                player_names.append(f"Player {len(player_names)+1}")
                        elif kind == "moves":
                            moves_per_round = min(MAX_MOVES_PER_ROUND, moves_per_round + 1)
                        elif kind == "rounds":
                            total_rounds = min(MAX_TOTAL_ROUNDS, total_rounds + 1)
                    elif state == "tournament" and t_state == "t_settings":
                        try:
                            kind, idx = idx_map[settings_idx]
                        except Exception:
                            kind = None
                        if kind == "tp_moves":
                            tournament_cfg["moves_per_round"] = min(MAX_MOVES_PER_ROUND, tournament_cfg["moves_per_round"] + 1)
                        elif kind == "tp_rounds":
                            tournament_cfg["rounds_per_match"] = min(MAX_TOTAL_ROUNDS, tournament_cfg["rounds_per_match"] + 1)
                        elif kind == "tp_count":
                            tournament_players.append(f"Player {len(tournament_players)+1}")
                continue
            elif event.type == pygame.KEYDOWN:
                set_input_mode("mouse")
                tooltip_text = None
                # Editing player name in settings
                # Editing player name in settings/tournament settings
                if editing_name is not None and (state == "settings" or (state == "tournament" and t_state=="t_settings")):
                    if event.key == pygame.K_RETURN or event.key == pygame.K_ESCAPE:
                        editing_name = None
                        osk_active = False
                        osk_case_select = None
                    elif event.key == pygame.K_BACKSPACE:
                        if state=="settings":
                            player_names[editing_name] = player_names[editing_name][:-1]
                        else:
                            tournament_players[editing_name] = tournament_players[editing_name][:-1]
                    else:
                        ch = event.unicode
                        if ch and ch.isprintable() and ch not in ["\r", "\n"]:
                            if state=="settings":
                                if len(player_names[editing_name]) < MAX_NAME_LEN:
                                    player_names[editing_name] += ch
                            else:
                                if len(tournament_players[editing_name]) < MAX_NAME_LEN:
                                    tournament_players[editing_name] += ch
                    continue

                if state == "save_setup":
                    pre_save_from_controller = False
                    if event.key == pygame.K_RETURN:
                        if pre_save_btn_idx == 0:
                            confirm_save_setup()
                        else:
                            cancel_save_setup()
                    elif event.key == pygame.K_ESCAPE:
                        cancel_save_setup()
                    elif event.key == pygame.K_LEFT:
                        pre_save_btn_idx = max(0, pre_save_btn_idx - 1)
                    elif event.key == pygame.K_RIGHT:
                        pre_save_btn_idx = min(len(pre_save_buttons) - 1, pre_save_btn_idx + 1)
                    elif event.key == pygame.K_BACKSPACE:
                        pre_save_holder[0] = pre_save_holder[0][:-1]
                        pre_save_error = ""
                    else:
                        ch = event.unicode
                        if ch and ch.isprintable() and ch not in ["\r", "\n"]:
                            if len(pre_save_holder[0]) < MAX_NAME_LEN:
                                pre_save_holder[0] += ch
                                pre_save_error = ""
                    continue

                if state == "rules":
                    step = 60
                    max_scroll = max(0, rules_content_h - rules_view_h)
                    if event.key in (pygame.K_DOWN, pygame.K_s, pygame.K_PAGEDOWN):
                        rules_scroll = min(max_scroll, rules_scroll + step)
                    elif event.key in (pygame.K_UP, pygame.K_w, pygame.K_PAGEUP):
                        rules_scroll = max(0, rules_scroll - step)
                    elif event.key == pygame.K_HOME:
                        rules_scroll = 0
                    elif event.key == pygame.K_END:
                        rules_scroll = max_scroll
                    else:
                        # allow Esc to fall through to global handler
                        pass
                    if event.key in (pygame.K_DOWN, pygame.K_s, pygame.K_PAGEDOWN, pygame.K_UP, pygame.K_w, pygame.K_PAGEUP, pygame.K_HOME, pygame.K_END):
                        continue

                if state == "load_select":
                    if load_view == "list":
                        total_entries = len(load_saves) + 1
                        if total_entries == 0:
                            total_entries = 1
                        if event.key in (pygame.K_UP, pygame.K_w):
                            load_menu_idx = (load_menu_idx - 1) % total_entries
                        elif event.key in (pygame.K_DOWN, pygame.K_s):
                            load_menu_idx = (load_menu_idx + 1) % total_entries
                        elif event.key in (pygame.K_RETURN, pygame.K_SPACE):
                            try:
                                play_click()
                            except Exception:
                                pass
                            if load_menu_idx == 0:
                                start_new_from_load()
                            else:
                                if load_menu_idx - 1 < len(load_saves):
                                    load_selected = load_saves[load_menu_idx - 1]
                                    load_view = "detail"
                                    load_detail_idx = 0
                                    load_detail_hitboxes = []
                        elif event.key == pygame.K_ESCAPE:
                            reset_load_view()
                            state = "menu"
                        continue
                    else:
                        detail_options = max(1, len(load_detail_hitboxes)) if load_detail_hitboxes else 3
                        if event.key in (pygame.K_LEFT, pygame.K_a):
                            load_detail_idx = (load_detail_idx - 1) % detail_options
                        elif event.key in (pygame.K_RIGHT, pygame.K_d):
                            load_detail_idx = (load_detail_idx + 1) % detail_options
                        elif event.key in (pygame.K_RETURN, pygame.K_SPACE):
                            try:
                                play_click()
                            except Exception:
                                pass
                            if load_detail_idx == 0:
                                ok, _ = load_selected_save()
                                if not ok:
                                    state = "menu"
                            elif load_detail_idx == 1:
                                delete_selected_save()
                            else:
                                load_view = "list"
                                load_selected = None
                                load_detail_idx = 0
                        elif event.key == pygame.K_ESCAPE:
                            load_view = "list"
                            load_selected = None
                            load_detail_idx = 0
                        continue

                if state == "confirm_exit" and save_prompt_active:
                    if osk_active:
                        if event.key == pygame.K_ESCAPE:
                            cancel_save_naming()
                            continue
                        if event.key == pygame.K_BACKSPACE:
                            save_name_holder[0] = save_name_holder[0][:-1]
                            continue
                        if event.key == pygame.K_RETURN:
                            continue
                        ch = event.unicode
                        if ch and ch.isprintable() and ch not in ["\r", "\n"]:
                            if len(save_name_holder[0]) < MAX_NAME_LEN:
                                save_name_holder[0] += ch
                        continue
                    else:
                        if event.key == pygame.K_RETURN:
                            if perform_save_and_exit():
                                continue
                        elif event.key == pygame.K_ESCAPE:
                            cancel_save_naming()
                            continue
                        elif event.key == pygame.K_BACKSPACE:
                            save_name_holder[0] = save_name_holder[0][:-1]
                            continue
                        else:
                            ch = event.unicode
                            if ch and ch.isprintable() and ch not in ["\r", "\n"]:
                                if len(save_name_holder[0]) < MAX_NAME_LEN:
                                    save_name_holder[0] += ch
                        continue
                        continue
                        save_name_holder[0] += ch
                        continue

                if event.key == pygame.K_ESCAPE:
                    if state == "menu":
                        pass
                    elif state == "settings":
                        state = "menu"
                        osk_active = False
                        osk_case_select = None
                    elif state == "game_over":
                        apply_display_mode(MENU_SIZE)
                        state = "menu"
                        continue
                    elif state == "confirm_exit":
                        cancel_confirm_exit()
                        continue
                    elif state in ("idle","swap","pop","fall","announce","round_announce"):
                        open_exit_prompt()
                        continue
                    elif state in ("rules", "controls", "time_mode", "time_settings"):
                        state = "menu"
                        continue
                    elif state == "load_select":
                        reset_load_view()
                        state = "menu"
                        continue
                # (removed duplicate game_over handler)


                # MENU navigation
                if state == "menu":
                    if event.key in (pygame.K_UP, pygame.K_w):
                        menu_idx = (menu_idx - 1) % menu_count
                    elif event.key in (pygame.K_DOWN, pygame.K_s):
                        menu_idx = (menu_idx + 1) % menu_count
                    elif event.key == pygame.K_RETURN:
                        try: play_click()
                        except Exception: pass
                        activate_menu_selection()
                    elif event.key == pygame.K_1:
                        message = ""
                        begin_save_setup({"kind": "game", "mode": "PvC"})
                    elif event.key == pygame.K_2:
                        message = ""
                        begin_save_setup({"kind": "game", "mode": "PvP"})
                    continue

                # RULES page
                if state == "rules":
                    if event.key in (pygame.K_ESCAPE, pygame.K_RETURN):
                        state = "menu"
                    continue
                if state == "controls":
                    if event.key in (pygame.K_ESCAPE, pygame.K_RETURN):
                        state = "menu"
                    continue
                if state == "confirm_exit":
                    options_count = max(1, len(confirm_options))
                    if event.key in (pygame.K_LEFT, pygame.K_a):
                        confirm_idx = (confirm_idx - 1) % options_count
                        continue
                    elif event.key in (pygame.K_RIGHT, pygame.K_d):
                        confirm_idx = (confirm_idx + 1) % options_count
                        continue
                    elif event.key in (pygame.K_RETURN, pygame.K_SPACE):
                        handle_confirm_selection()
                        continue
                    elif event.key == pygame.K_ESCAPE:
                        if save_prompt_active:
                            cancel_save_naming()
                        else:
                            cancel_confirm_exit()
                        continue

                # CONTROLS page
                if state == "controls":
                    if event.key in (pygame.K_ESCAPE, pygame.K_RETURN):
                        state = "menu"
                    continue


                # TOURNAMENT states
                if state == "tournament":
                    if t_state == "t_settings":
                        idx_map = draw_tournament_settings()
                        if event.key in (pygame.K_UP, pygame.K_w):
                            settings_idx = (settings_idx - 1) % len(idx_map)
                        # keep selected item visible
                        target_y = settings_idx * 46
                        max_scroll = max(0, settings_content_h - settings_view_h)
                        if target_y - settings_scroll < 0:
                            settings_scroll = max(0, target_y)
                        elif target_y + 46 - settings_scroll > settings_view_h:
                            settings_scroll = min(max_scroll, target_y + 46 - settings_view_h)
                        elif event.key in (pygame.K_DOWN, pygame.K_s):
                            settings_idx = (settings_idx + 1) % len(idx_map)
                        # keep selected item visible
                        target_y = settings_idx * 46
                        max_scroll = max(0, settings_content_h - settings_view_h)
                        if target_y - settings_scroll < 0:
                            settings_scroll = max(0, target_y)
                        elif target_y + 46 - settings_scroll > settings_view_h:
                            settings_scroll = min(max_scroll, target_y + 46 - settings_view_h)
                        elif event.key in (pygame.K_LEFT, pygame.K_a):
                            kind, idx = idx_map[settings_idx]
                            if kind == "tp_count":
                                if len(tournament_players) > 2:
                                    tournament_players.pop()
                            elif kind == "tp_moves":
                                tournament_cfg["moves_per_round"] = max(MIN_MOVES_PER_ROUND, tournament_cfg["moves_per_round"] - 1)
                            elif kind == "tp_rounds":
                                tournament_cfg["rounds_per_match"] = max(MIN_TOTAL_ROUNDS, tournament_cfg["rounds_per_match"] - 1)
                            elif kind == "tp_order":
                                tournament_cfg["order"] = "consecutive" if tournament_cfg["order"] == "round_robin" else "round_robin"
                            elif kind == "tp_color":
                                tournament_cfg["color_chain"] = not tournament_cfg.get("color_chain", False)
                        elif event.key in (pygame.K_RIGHT, pygame.K_d):
                            kind, idx = idx_map[settings_idx]
                            if kind == "tp_count":
                                tournament_players.append(f"Player {len(tournament_players)+1}")
                            elif kind == "tp_moves":
                                tournament_cfg["moves_per_round"] = min(MAX_MOVES_PER_ROUND, tournament_cfg["moves_per_round"] + 1)
                            elif kind == "tp_rounds":
                                tournament_cfg["rounds_per_match"] = min(MAX_TOTAL_ROUNDS, tournament_cfg["rounds_per_match"] + 1)
                            elif kind == "tp_order":
                                tournament_cfg["order"] = "consecutive" if tournament_cfg["order"] == "round_robin" else "round_robin"
                            elif kind == "tp_color":
                                tournament_cfg["color_chain"] = not tournament_cfg.get("color_chain", False)
                        elif event.key == pygame.K_RETURN:
                            kind, idx = idx_map[settings_idx]
                            if kind == "tp_name":
                                editing_name = idx
                            elif kind == "tp_color":
                                tournament_cfg["color_chain"] = not tournament_cfg.get("color_chain", False)
                            elif kind == "tp_start":
                                start_tournament()
                            elif kind == "tp_back":
                                tournament_active = False
                                state = "menu"
                        elif event.key == pygame.K_ESCAPE:
                            tournament_active = False
                            state = "menu"
                        continue
                    elif t_state == "t_bracket":
                        if event.key in (pygame.K_RETURN, pygame.K_ESCAPE):
                            # Если турнир уже завершён — уходим в меню по Enter/ESC
                            filled = bracket_advance(bracket_rounds, bracket_results)
                            finished = (t_round_idx >= len(filled)) or (t_round_idx < len(filled) and t_match_idx >= len(filled[t_round_idx]))
                            if finished:
                                tournament_active = False
                                state = "menu"
                            elif event.key == pygame.K_RETURN:
                                start_next_tournament_match()
                            # ESC при незавершённом турнире — просто обратно в меню (как сейчас)
                            elif event.key == pygame.K_ESCAPE:
                                tournament_active = False
                                state = "menu"
                        continue


                # SETTINGS navigation
                if state == "settings":
                    if event.key in (pygame.K_UP, pygame.K_w):
                        settings_idx = (settings_idx - 1) % len(idx_map)
                    elif event.key in (pygame.K_DOWN, pygame.K_s):
                        settings_idx = (settings_idx + 1) % len(idx_map)
                    elif event.key in (pygame.K_LEFT, pygame.K_a):
                        kind, idx = idx_map[settings_idx]
                        prev_key = (kind, idx)
                        if kind == "players" and mode == "PvP":
                            players_count = max(MIN_PLAYERS, players_count - 1)
                            if len(player_names) > players_count:
                                player_names = player_names[:players_count]
                        elif kind == "moves":
                            moves_per_round = max(MIN_MOVES_PER_ROUND, moves_per_round - 1)
                        elif kind == "rounds":
                            total_rounds = max(MIN_TOTAL_ROUNDS, total_rounds - 1)
                        elif kind == "order":
                            turn_order_mode = "consecutive" if turn_order_mode == "round_robin" else "round_robin"
                        elif kind == "bomb":
                            bomb_enabled = not bomb_enabled
                        # Rebuild & keep focus
                        idx_map = draw_settings()
                        if prev_key[0] == "players":
                            for i, (k, v) in enumerate(idx_map):
                                if k == "players":
                                    settings_idx = i
                                    break
                        else:
                            target = prev_key
                            if target[0] == "name" and (target[1] is None or target[1] >= players_count):
                                target = ("name", max(0, players_count - 1))
                            for i, kv in enumerate(idx_map):
                                if kv == target:
                                    settings_idx = i
                                    break
                    elif event.key in (pygame.K_RIGHT, pygame.K_d):
                        kind, idx = idx_map[settings_idx]
                        prev_key = (kind, idx)
                        if kind == "players" and mode == "PvP":
                            players_count = players_count + 1
                            while len(player_names) < players_count:
                                player_names.append(f"Player {len(player_names)+1}")
                        elif kind == "moves":
                            try:
                                moves_per_round = min(MAX_MOVES_PER_ROUND, moves_per_round + 1)
                            except NameError:
                                moves_per_round += 1
                        elif kind == "rounds":
                            try:
                                total_rounds = min(MAX_TOTAL_ROUNDS, total_rounds + 1)
                            except NameError:
                                total_rounds += 1
                        elif kind == "order":
                            turn_order_mode = "consecutive" if turn_order_mode == "round_robin" else "round_robin"
                        elif kind == "bomb":
                            bomb_enabled = not bomb_enabled
                        # Rebuild & keep focus
                        idx_map = draw_settings()
                        if prev_key[0] == "players":
                            for i, (k, v) in enumerate(idx_map):
                                if k == "players":
                                    settings_idx = i
                                    break
                        else:
                            target = prev_key
                            if target[0] == "name" and (target[1] is None or target[1] >= players_count):
                                target = ("name", max(0, players_count - 1))
                            for i, kv in enumerate(idx_map):
                                if kv == target:
                                    settings_idx = i
                                    break
                    elif event.key == pygame.K_RETURN:
                        kind, idx = idx_map[settings_idx]
                        if kind == "name":
                            if mode == "PvP" or (mode == "PvC" and idx == 0):
                                if player_names[idx].startswith("Player ") and player_names[idx].endswith(str(idx+1)):
                                    player_names[idx] = ""
                                editing_name = idx
                        elif kind == "start":
                            scores = [0]*players_count
                            moves_left_per_player = [moves_per_round]*players_count
                            confirm_settings_and_begin()
                        elif kind == "back":
                            state = "menu"
                    continue

                # GAME OVER
                # (removed duplicate game_over handler)


                # GAME_OVER controls
                # GAME_OVER controls
                if state == "game_over":
                    # Турнир: Enter / Esc — назад в сетку; после финального матча — сразу в меню
                    if tournament_active:
                        if event.key in (pygame.K_RETURN, pygame.K_ESCAPE):
                            # Если уже не в активном матче (например, после финала) — просто в меню
                            if t_state != "t_playing":
                                tournament_active = False
                                state = "menu"
                                apply_display_mode(MENU_SIZE)
                            else:
                                # Определяем победителя этого матча (ничья — рандом)
                                if scores[0] == scores[1]:
                                    winner_local = random.choice([0, 1])
                                else:
                                    winner_local = 0 if scores[0] > scores[1] else 1

                                filled = bracket_advance(bracket_rounds, bracket_results)
                                # Защита от выхода за пределы (вдруг уже финал закрыли)
                                if 0 <= t_round_idx < len(filled) and 0 <= t_match_idx < len(filled[t_round_idx]):
                                    a, b = filled[t_round_idx][t_match_idx]
                                    winner_index = a if winner_local == 0 else b
                                    advance_tournament_after_result(winner_index)
                                    # Возврат к сетке
                                    t_state = "t_bracket"
                                    state = "tournament"
                                    apply_display_mode(MENU_SIZE)
                                else:
                                    # Подстраховка: в меню
                                    tournament_active = False
                                    state = "menu"
                                    apply_display_mode(MENU_SIZE)
                        continue
                    else:
                        # Обычный режим (не турнир): Enter — в меню
                        if event.key == pygame.K_RETURN:
                            apply_display_mode(MENU_SIZE)
                            state = "menu"
                        continue


                # IN-GAME hotkeys & keyboard control
                if event.key == pygame.K_n and state in ("idle", "announce", "round_announce"):
                    apply_display_mode(MENU_SIZE)
                    state = "settings"
                elif event.key == pygame.K_r and state in ("idle", "announce", "round_announce"):
                    ensure_moves_or_shuffle()
                    state = "announce"; announce_t = 0
                elif event.key in (pygame.K_LEFT, pygame.K_RIGHT, pygame.K_UP, pygame.K_DOWN, pygame.K_RETURN) and state == "idle":
                    c, r = cursor
                    if event.key == pygame.K_LEFT:
                        cursor = (max(0, c-1), r)
                    elif event.key == pygame.K_RIGHT:
                        cursor = (min(COLS-1, c+1), r)
                    elif event.key == pygame.K_UP:
                        cursor = (c, max(0, r-1))
                    elif event.key == pygame.K_DOWN:
                        cursor = (c, min(ROWS-1, r+1))
                    elif event.key == pygame.K_RETURN:
                        try: play_click()
                        except Exception: pass
                        if not hasattr(main, "_selected"):
                            main._selected = None
                        if main._selected is None:
                            main._selected = cursor
                        else:
                            a, b = main._selected, cursor
                            if abs(a[0]-b[0]) + abs(a[1]-b[1]) == 1 and legal_swap(board, a, b):
                                chain_owner = turn
                                start_swap(a, b)
                            main._selected = None

            elif event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
                if input_mode != "mouse":
                    continue
                if state == "idle":
                    logical = to_logical(event.pos)
                    if logical is None:
                        continue
                    mx, my = logical
                    c = int((mx - GRID_TOPLEFT[0]) // CELL)
                    r = int((my - GRID_TOPLEFT[1]) // CELL)
                    if 0 <= c < COLS and 0 <= r < ROWS:
                        if not hasattr(main, "_selected"):
                            main._selected = None
                        if main._selected is None:
                            play_click()
                            main._selected = (c, r)
                        else:
                            a, b = main._selected, (c, r)
                            if abs(a[0]-b[0]) + abs(a[1]-b[1]) == 1 and legal_swap(board, a, b):
                                chain_owner = turn
                                play_swap()
                                start_swap(a, b)
                            else:
                                try: play_error()
                                except Exception: pass
                            main._selected = None

            elif event.type == pygame.MOUSEWHEEL and state == "settings":
                if input_mode != "mouse":
                    continue
                # Scroll the settings list with mouse wheel
                max_scroll = max(0, settings_content_h - settings_view_h)
                settings_scroll = max(0, min(settings_scroll - event.y*40, max_scroll))

        
        # --- Tournament screens & flow ---
        def start_tournament_settings():
            nonlocal tournament_active, t_state, tournament_players, tournament_cfg, state, settings_idx, editing_name
            tournament_active = True
            t_state = "t_settings"
            if not tournament_players:
                tournament_players = [f"Player {i+1}" for i in range(4)]
            # carry over defaults
            tournament_cfg["moves_per_round"] = moves_per_round
            tournament_cfg["rounds_per_match"] = total_rounds
            tournament_cfg["order"] = turn_order_mode
            tournament_cfg["color_chain"] = color_chain_enabled
            apply_display_mode(MENU_SIZE)
            state = "tournament"
            settings_idx = 0
            editing_name = None

        
        def draw_tournament_settings():
            nonlocal settings_view_h, settings_content_h, settings_scroll, t_settings_hitboxes, t_settings_scrollbar, settings_idx, editing_name
            win_w, win_h = screen.get_size()
            screen.fill(COLOR_BG)
            t_surf = font_title.render("Tournament Settings", True, COLOR_TEXT)
            screen.blit(t_surf, (win_w//2 - t_surf.get_width()//2, 30))
            box_w = int(win_w * 0.72); box_h = int(win_h * 0.74)
            box = pygame.Rect(win_w//2 - box_w//2, win_h//2 - box_h//2 + 10, box_w, box_h)
            pygame.draw.rect(screen, (0,0,0), box, 2)
            inner_x = box.left + 40; inner_y = box.top + 50; inner_w = box.width - 80
            view_rect = pygame.Rect(inner_x, inner_y, inner_w, box.bottom - 90 - inner_y)
            settings_view_h = view_rect.height

            labels = []
            labels.append(("tp_count", None, f"Tournament players: {len(tournament_players)}"))
            for i, name in enumerate(tournament_players):
                labels.append(("tp_name", i, f"Player {i+1} name: {name}"))
            labels.extend([
                ("tp_moves", None, f"Moves per player per round: {tournament_cfg['moves_per_round']}"),
                ("tp_rounds", None, f"Total rounds per match: {tournament_cfg['rounds_per_match']}"),
                ("tp_order", None, f"Turn order: {'Round-robin' if tournament_cfg['order']=='round_robin' else 'Consecutive'}"),
                ("tp_color", None, f"Color blast: {'On' if tournament_cfg.get('color_chain') else 'Off'}"),
                ("tp_start", None, "START TOURNAMENT"),
                ("tp_back", None, "BACK"),
            ])

            idx_to_item = []
            y_cursor = 0
            base_line_h = 54
            gap = 12
            measured = []
            for kind, idx, text in labels:
                idx_to_item.append((kind, idx))
                is_sel = (settings_idx == len(idx_to_item)-1 and editing_name is None)
                surf = render_text_fit(screen, text, inner_w - 40, font_big if is_sel else font)
                block_h = max(base_line_h, surf.get_height() + 24)
                measured.append((y_cursor, kind, idx, text, block_h))
                y_cursor += block_h + gap
            settings_content_h = y_cursor

            max_scroll = max(0, settings_content_h - settings_view_h)
            settings_scroll = max(0, min(settings_scroll, max_scroll))

            # reset hitboxes & scrollbar
            t_settings_hitboxes = []
            t_settings_scrollbar["track"] = None
            t_settings_scrollbar["thumb"] = None

            prev_clip = screen.get_clip(); screen.set_clip(view_rect)
            for base_y, kind, idx, text, block_h in measured:
                draw_y = inner_y + base_y - settings_scroll
                if draw_y + block_h < inner_y - 10 or draw_y > view_rect.bottom + 10:
                    continue
                is_sel = (idx_to_item.index((kind, idx)) == settings_idx and editing_name is None)
                shown = (("(editing) " if editing_name is not None and kind=='tp_name' and editing_name==idx else "") + text)
                text_color = (0,0,0) if is_sel else COLOR_TEXT
                surf = render_text_fit(screen, shown, inner_w - 40, font_big if is_sel else font, color=text_color)
                hit_rect = pygame.Rect(inner_x, draw_y, inner_w, block_h)
                if kind != "header":
                    t_settings_hitboxes.append((hit_rect, (kind, idx)))
                    if is_sel:
                        pygame.draw.rect(screen, (255,220,60), hit_rect)
                        pygame.draw.rect(screen, (255,255,255), hit_rect, 2)
                    else:
                        pygame.draw.rect(screen, (40,40,40), hit_rect)
                        pygame.draw.rect(screen, (120,120,120), hit_rect, 2)
                screen.blit(surf, (hit_rect.centerx - surf.get_width()//2, hit_rect.centery - surf.get_height()//2))
            screen.set_clip(prev_clip)

            if settings_content_h > settings_view_h:
                bar_x = view_rect.right + 6; bar_y = view_rect.top; bar_h = view_rect.height
                track_rect = pygame.Rect(bar_x, bar_y, 8, bar_h)
                pygame.draw.rect(screen, (0,0,0), track_rect, 1)
                thumb_h = max(20, int(bar_h * settings_view_h / settings_content_h))
                thumb_y = bar_y + int((bar_h - thumb_h) * (settings_scroll / max_scroll)) if max_scroll else bar_y
                thumb_rect = pygame.Rect(bar_x+1, thumb_y+1, 6, thumb_h-2)
                pygame.draw.rect(screen, (220,220,220), thumb_rect)
                t_settings_scrollbar["track"] = track_rect
                t_settings_scrollbar["thumb"] = thumb_rect

            hint = _hint_text(
                "Click items or use ↑/↓  ←/→  Enter. Wheel scroll. Esc back.",
                "Use D-Pad/Stick to move, A to adjust, B to go back."
            )
            hint_surf = render_text_fit(screen, hint, inner_w, font)
            screen.blit(hint_surf, (inner_x, box.bottom - 50))
            return idx_to_item

        def start_tournament():
            nonlocal bracket_rounds, bracket_results, t_state, t_round_idx, t_match_idx, eliminated_set
            bracket_rounds = build_bracket(len(tournament_players))
            bracket_results = auto_advance_byes(bracket_rounds)
            t_state = "t_bracket"; t_round_idx = 0; t_match_idx = 0
            eliminated_set = set()

        def draw_tournament_bracket():
            screen.fill(COLOR_BG)
            title = font_title.render("Tournament Bracket", True, COLOR_TEXT)
            screen.blit(title, (screen.get_width()//2 - title.get_width()//2, 20))
            current = (t_round_idx, t_match_idx)
            blink = int(pygame.time.get_ticks()/300) % 2 == 0
            draw_bracket(screen, tournament_players, bracket_rounds, bracket_results, current=current, eliminated=eliminated_set, blink=blink)
            sub_text = _hint_text("Enter: play next match   Esc: back to menu", "A: play next match   B: back to menu")
            sub = font.render(sub_text, True, COLOR_TEXT)
            screen.blit(sub, (screen.get_width()//2 - sub.get_width()//2, screen.get_height()-40))

        def start_next_tournament_match():
            nonlocal state, t_state, players_count, player_names, moves_per_round, total_rounds, turn_order_mode, scores, moves_left_per_player, color_chain_enabled
            filled = bracket_advance(bracket_rounds, bracket_results)
            a, b = filled[t_round_idx][t_match_idx]
            if a is None or b is None:
                winner = b if a is None else a
                advance_tournament_after_result(winner)
                return
            # apply config to PvP match
            players_count = 2
            player_names[0] = tournament_players[a]; player_names[1] = tournament_players[b]
            moves_per_round = tournament_cfg["moves_per_round"]
            total_rounds = tournament_cfg["rounds_per_match"]
            turn_order_mode = tournament_cfg["order"]
            color_chain_enabled = bool(tournament_cfg.get("color_chain"))
            # reset game state and begin
            scores = [0]*players_count
            moves_left_per_player = [moves_per_round]*players_count
            confirm_settings_and_begin()
            t_state = "t_playing"

        def advance_tournament_after_result(winner_index):
            nonlocal t_round_idx, t_match_idx, t_state, bracket_results, eliminated_set
            filled = bracket_advance(bracket_rounds, bracket_results)
            a, b = filled[t_round_idx][t_match_idx]
            loser = b if winner_index == a else a
            if loser is not None:
                eliminated_set.add(loser)
            bracket_results[(t_round_idx, t_match_idx)] = winner_index
            autosave_current("tournament_match")
            t_match_idx += 1
            if t_match_idx >= len(bracket_rounds[t_round_idx]):
                t_match_idx = 0; t_round_idx += 1
                if t_round_idx >= len(bracket_rounds):
                    t_state = "t_end"
                    delete_current_save()
                    return
            t_state = "t_bracket"

        # --- STATE UPDATES ---
        if state == "settings":
            idx_map = draw_settings()
            # Keep selection visible even when content changes
            line_h = 46
            sel_top = settings_idx * line_h
            sel_bottom = sel_top + line_h
            max_scroll = max(0, settings_content_h - settings_view_h)
            if sel_top < settings_scroll:
                settings_scroll = max(0, sel_top)
            elif sel_bottom > settings_scroll + settings_view_h:
                settings_scroll = min(max_scroll, sel_bottom - settings_view_h)

        if state == "round_announce":
            round_announce_t += dt
            if round_announce_t >= round_announce_wait_ms:
                # Use the helper to enter announce so blitz timers initialize
                start_announce()
        elif state == "announce":
            if time_mode == "blitz":
                # For PvC computer turns: show banner without countdown for TURN_ANNOUNCE_MS
                if mode == "PvC" and player_names[turn] == "computer":
                    announce_t += dt
                    if announce_t >= TURN_ANNOUNCE_MS:
                        state = "idle"
                else:
                    between_accum_ms += dt
                    while between_accum_ms >= 1000 and state == "announce":
                        between_accum_ms -= 1000
                        if between_left_s > 0:
                            if between_left_s == 1:
                                play_countdown_end()
                            else:
                                play_countdown()
                            between_left_s = max(0, between_left_s - 1)
                        if between_left_s <= 0:
                            state = "idle"
                            break
                    if between_left_s <= 0 and state == "announce":
                        state = "idle"
            else:
                announce_t += dt
                if announce_t >= TURN_ANNOUNCE_MS:
                    state = "idle"

        # Computer move (any player named 'computer')
        if state == "idle" and time_mode == "blitz":
            # Decrease per-turn timer; if runs out, auto end move (forced pass)
            turn_time_left_ms = max(0, turn_time_left_ms - dt)
            if turn_time_left_ms == 0:
                forced_pass = True
                end_move()
                forced_pass = False
                continue
        if state == "idle" and "computer" in player_names and player_names[turn] == "computer":
            pygame.time.delay(150)
            moves = all_scoring_swaps(board)
            if not moves:
                ensure_moves_or_shuffle()
                state = "announce"
                announce_t = 0
            else:
                _pts, a, b = moves[0]
                chain_owner = turn
                start_swap(a, b)

        # animations
        if anims and state != "confirm_exit":
            step_anims(dt)

        # --- RENDER ---
        if state == "menu":
            draw_menu()
        elif state == "load_select":
            draw_load_select()
        elif state == "tournament":
            if t_state == "t_settings":
                idx_map = draw_tournament_settings()
            elif t_state == "t_bracket":
                draw_tournament_bracket()
            elif t_state == "t_end":
                screen.fill(COLOR_BG)
                title = font_title.render("Tournament Winner", True, COLOR_TEXT)
                screen.blit(title, (screen.get_width()//2 - title.get_width()//2, 40))
                if bracket_results:
                    ri = max(r for (r, m) in bracket_results.keys()) if bracket_results else 0
                    winners = [w for (r, m), w in bracket_results.items() if r == ri]
                    if winners:
                        win_name = tournament_players[winners[-1]]
                        win_surf = font_big.render(win_name, True, COLOR_TEXT)
                        screen.blit(win_surf, (screen.get_width()//2 - win_surf.get_width()//2, 140))
                hint_text = _hint_text("Enter: back to menu", "A: back to menu")
                hint = font.render(hint_text, True, COLOR_TEXT)
                screen.blit(hint, (screen.get_width()//2 - hint.get_width()//2, screen.get_height()-40))
        elif state == "settings":
            # draw_settings already rendered above
            pass
        elif state == "game_over":
            draw_game_over()
        else:
            screen.fill(COLOR_BG)
            draw_grid(screen)
            # Prefer mouse hover; if none, show controller/keyboard cursor as hover
            hover_cell = hover if hover is not None else (cursor if state == "idle" else None)
            draw_board(screen, board, hidden=set(), selected=(getattr(main, "_selected", None) if state=="idle" else None), hover=hover_cell)
            draw_anims(screen, anims)
            draw_panel()
            if state == "round_announce":
                draw_center_banner(screen, f"Round {current_round}/{total_rounds}", font_big, "Get ready...")
            if state == "announce":
                if time_mode == "blitz":
                    if mode == "PvC" and player_names[turn] == "computer":
                        sub = (f"Moves left this round: {moves_left_per_player[turn]}" if turn_order_mode=="round_robin" else f"Moves left: {moves_left_per_player[turn]}")
                        draw_center_banner(screen, f"{player_names[turn]}'s Turn", font_big, sub)
                    else:
                        draw_center_banner(screen, f"Next turn: {player_names[turn]}", font_big, f"in {between_left_s}s")
                else:
                    sub = (f"Moves left this round: {moves_left_per_player[turn]}" if turn_order_mode=="round_robin" else f"Moves left: {moves_left_per_player[turn]}")
                    draw_center_banner(screen, f"{player_names[turn]}'s Turn", font_big, sub)
            # Turn label and per-turn timer under the board, on the right
            turn_label_y = GRID_TOPLEFT[1] + ROWS*CELL + 30
            max_turn_label_y = BASE_SIZE[1] - 140
            if turn_label_y > max_turn_label_y:
                turn_label_y = max_turn_label_y
            x_label = PANEL_X
            draw_text(screen, font, f"TURN: {turn_name(turn)}", (x_label, turn_label_y))
            if time_mode == "blitz" and state in ("idle","swap","pop","fall"):
                # format remaining time mm:ss
                secs = max(0, int((turn_time_left_ms + 999)//1000))
                mm = secs // 60; ss = secs % 60
                draw_text(screen, font, f"TIME: {mm:02d}:{ss:02d}", (x_label, turn_label_y + 40))

        # Menu-like states rendering for additional pages
        if state == "save_setup":
            draw_save_setup()
        elif state == "time_mode":
            draw_time_mode()
        elif state == "time_settings":
            draw_time_settings()
        elif state == "rules":
            draw_rules()
        elif state == "controls":
            draw_controls_page()
        elif state == "controls":
            draw_controls_page()

        # Draw tooltip overlay on menu-like screens
        if state in ("menu", "settings", "time_mode", "time_settings", "load_select") or (state == "tournament" and t_state == "t_settings"):
            tip_text = tooltip_text or current_focus_tooltip()
            if tip_text:
                draw_tooltip(screen, tip_text, font)
        # Draw confirm-exit overlay on top of current frame
        if state == "confirm_exit":
            draw_confirm_exit()
        # On-screen keyboard overlay
        if _osk_input_active():
            draw_osk_overlay()

        if window_surf is not None and screen is not None:
            ww, wh = window_surf.get_size()
            sw, sh = screen.get_size()
            if ww > 0 and wh > 0:
                scale = min(ww / sw, wh / sh)
                if scale <= 0:
                    scale = 1.0
            else:
                scale = 1.0
            dw, dh = int(sw * scale), int(sh * scale)
            dx = (ww - dw) // 2
            dy = (wh - dh) // 2
            present_scale = scale
            present_offset = (dx, dy)
            if hasattr(window_surf, 'fill'):
                window_surf.fill(COLOR_BG)
            if dw > 0 and dh > 0:
                if hasattr(pygame.transform, 'smoothscale'):
                    frame = pygame.transform.smoothscale(screen, (dw, dh))
                else:
                    frame = pygame.transform.scale(screen, (dw, dh))
                window_surf.blit(frame, (dx, dy))
        pygame.display.flip()
    # Clean shutdown
    save_config(config)
    pygame.quit()
    return

if __name__ == "__main__":
    main()
