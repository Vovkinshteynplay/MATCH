#define SDL_MAIN_HANDLED

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <limits>
#include <iomanip>
#include <filesystem>
#include <map>
#include <optional>
#include <random>
#include <numeric>
#include <set>
#include <string>
#include <sstream>
#include <utility>
#include <vector>

#include <SDL2/SDL.h>
#include <SDL2/SDL_main.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_mixer.h>
#include <SDL2/SDL_image.h>

#include "match/core/Board.hpp"
#include "match/core/AI.hpp"
#include "match/core/SavePayload.hpp"
#include "match/app/AssetFS.hpp"
#include "match/platform/AudioSystem.hpp"
#include "match/platform/SdlInput.hpp"
#include "match/platform/SdlSaveService.hpp"
#include "match/render/SceneRenderer.hpp"
#include "match/ui/Screens.hpp"

using match::app::AssetPath;
using match::app::FileExists;
using match::platform::AudioSystem;
using match::platform::SdlInput;
using match::platform::InputEventType;
using match::platform::MouseButton;
using match::platform::KeyCode;
using match::platform::ControllerButton;
using match::platform::ControllerAxis;
using match::render::LoadFonts;
using match::render::DestroyFonts;
using match::render::ComputeLayout;
using match::render::Animation;
using match::render::MakeSwapAnimation;
using match::render::MakePopAnimation;
using match::render::MakeFallAnimation;
using match::render::MakeSpawnAnimation;
using match::render::UpdateAnimations;
using match::render::DrawBoard;
using match::render::DrawAnimations;
using match::render::DrawPanel;
using match::render::BoardRenderData;
using match::render::PanelInfo;
using match::render::PanelPlayerEntry;
using Layout = match::render::Layout;
using match::render::kSwapDurationMs;
using match::render::kPopDurationMs;
using match::render::kFallDurationPerCellMs;
using match::render::kFallDurationMinMs;

namespace {

struct GameContext;

constexpr int kBoardCols = 20;
constexpr int kBoardRows = 20;

constexpr int kLogicalWidth = 1920;
constexpr int kLogicalHeight = 1080;

constexpr int kWindowWidth = kLogicalWidth;
constexpr int kWindowHeight = kLogicalHeight;

enum class InputMode { MouseKeyboard, Controller };

using Fonts = match::render::Fonts;

AudioSystem* g_audio = nullptr;
SdlInput* g_input = nullptr;

bool PointInRect(const SDL_Rect& rect, int x, int y) {
    return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
}

inline void PlaySwapSound() {
    if (g_audio) {
        g_audio->PlaySwap();
    }
}

inline void PlayMatchSound(bool cascade) {
    if (g_audio) {
        g_audio->PlayMatch(cascade);
    }
}

inline void PlayBombSound() {
    if (g_audio) {
        g_audio->PlayBomb();
    }
}

inline void PlayErrorSound() {
    if (g_audio) {
        g_audio->PlayError();
    }
}

inline void PlayClickSound() {
    if (g_audio) {
        g_audio->PlayClick();
    }
}

inline void TriggerRumble(float strength, Uint32 duration_ms) {
    if (g_input) {
        g_input->RumbleControllers(strength, duration_ms);
    }
}

struct WindowModeState {
    bool fullscreen = false;
    int windowed_width = kWindowWidth;
    int windowed_height = kWindowHeight;
    int windowed_x = SDL_WINDOWPOS_CENTERED;
    int windowed_y = SDL_WINDOWPOS_CENTERED;
    bool want_maximized = true;
};

inline float PlayNextTurnSound() {
    return g_audio ? g_audio->PlayNextTurn() : 0.0f;
}

inline float PlayNextRoundSound() {
    return g_audio ? g_audio->PlayNextRound() : 0.0f;
}

inline float PlayWinSound() {
    return g_audio ? g_audio->PlayWin() : 0.0f;
}

inline void PlayCountdownTickSound() {
    if (g_audio) {
        g_audio->PlayCountdownTick();
    }
}

inline void PlayCountdownFinalSound() {
    if (g_audio) {
        g_audio->PlayCountdownFinal();
    }
}

struct BannerOverlay {
    bool visible = false;
    bool block_input = false;
    bool persistent = false;
    std::string title;
    std::string subtitle;
    float timer_ms = 0.0f;
    float duration_ms = 0.0f;
    bool show_finish_hint = false;
    bool countdown_active = false;
    std::string countdown_text;
};

BannerOverlay g_banner;
bool g_win_announced = false;

struct BoardState;

void ResetBannerOverlay() {
    g_banner.visible = false;
    g_banner.block_input = false;
    g_banner.persistent = false;
    g_banner.title.clear();
    g_banner.subtitle.clear();
    g_banner.timer_ms = 0.0f;
    g_banner.duration_ms = 0.0f;
    g_banner.show_finish_hint = false;
    g_banner.countdown_active = false;
    g_banner.countdown_text.clear();
}

void ShowBannerMessage(const std::string& title,
                       const std::string& subtitle,
                       float duration_ms = 2400.0f,
                       bool block_input = true,
                       bool persistent = false,
                       bool finish_hint = false) {
    g_banner.visible = true;
    g_banner.block_input = block_input;
    g_banner.persistent = persistent;
    g_banner.title = title;
    g_banner.subtitle = subtitle;
    g_banner.duration_ms = std::max(60.0f, duration_ms);
    g_banner.timer_ms = g_banner.duration_ms;
    g_banner.show_finish_hint = finish_hint;
    g_banner.countdown_active = false;
    g_banner.countdown_text.clear();
}

void UpdateBannerOverlay(float delta_ms) {
    if (!g_banner.visible) {
        return;
    }
    if (g_banner.persistent) {
        return;
    }
    g_banner.timer_ms = std::max(0.0f, g_banner.timer_ms - delta_ms);
    if (g_banner.timer_ms <= 0.0f) {
        ResetBannerOverlay();
    }
}

inline bool BannerBlocksInput() {
    return g_banner.visible && g_banner.block_input;
}

std::pair<int, int> MeasureText(TTF_Font* font, const std::string& text) {
    if (!font || text.empty()) {
        return {0, 0};
    }
    int w = 0;
    int h = 0;
    if (TTF_SizeUTF8(font, text.c_str(), &w, &h) != 0) {
        return {0, 0};
    }
    return {w, h};
}

int DrawTextCentered(SDL_Renderer* renderer,
                     TTF_Font* font,
                     const std::string& text,
                     int center_x,
                     int y,
                     SDL_Color color) {
    if (!font || text.empty()) {
        return 0;
    }
    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!surface) {
        return 0;
    }
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!texture) {
        int height = surface->h;
        SDL_FreeSurface(surface);
        return height;
    }
    SDL_Rect dst;
    dst.w = surface->w;
    dst.h = surface->h;
    dst.x = center_x - dst.w / 2;
    dst.y = y;
    SDL_RenderCopy(renderer, texture, nullptr, &dst);
    int height = surface->h;
    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
    return height;
}

std::string ActivePlayerLabel(const GameContext& ctx);
int ActivePlayerMoves(const GameContext& ctx);
std::string WinnersSubtitle(const GameContext& ctx);
void AnnounceRoundStart(const GameContext& ctx);
void AnnounceTurnStart(const GameContext& ctx);
void AnnounceVictory(const GameContext& ctx);
void HandleStateFeedback(GameContext& ctx,
                         int previous_round,
                         int previous_player,
                         bool previous_game_over);
void StartBlitzTurn(GameContext& ctx, bool player_changed);
void UpdateBlitzTimers(BoardState& state, GameContext& ctx, float delta_ms);
void ForceTurnTimeout(BoardState& state, GameContext& ctx);

void DrawBanner(SDL_Renderer* renderer,
                const Fonts& fonts,
                const Layout& layout,
                const BannerOverlay& banner,
                bool using_controller) {
    if (!banner.visible || banner.duration_ms <= 0.0f) {
        return;
    }
    float alpha_norm = std::clamp(banner.timer_ms / banner.duration_ms, 0.0f, 1.0f);
    if (alpha_norm <= 0.0f) {
        return;
    }

    const float board_width = layout.cell_size * kBoardCols;
    const float board_height = layout.cell_size * kBoardRows;
    const float center_x = layout.board_left + board_width * 0.5f;
    const float center_y = layout.board_top + board_height * 0.5f;

    TTF_Font* title_font = fonts.heading ? fonts.heading : fonts.body;
    TTF_Font* subtitle_font = fonts.body ? fonts.body : fonts.heading;

    auto [title_w, title_h] = MeasureText(title_font, banner.title);
    auto [subtitle_w, subtitle_h] = MeasureText(subtitle_font, banner.subtitle);
    const int spacing = (title_h > 0 && subtitle_h > 0)
                            ? static_cast<int>(std::max(8.0f, layout.cell_size * 0.15f))
                            : 0;
    const int countdown_spacing =
        std::max(spacing * 2 + 6, static_cast<int>(std::max(16.0f, layout.cell_size * 0.35f)));
    const int content_width = std::max(title_w, subtitle_w);
    const int content_height = title_h + subtitle_h + spacing;
    const int padding = static_cast<int>(std::max(layout.cell_size * 0.6f, 28.0f));

    TTF_Font* countdown_font = fonts.heading ? fonts.heading : subtitle_font;
    int countdown_height = 0;
    if (banner.countdown_active && !banner.countdown_text.empty() && countdown_font) {
        countdown_height = MeasureText(countdown_font, banner.countdown_text).second;
    }

    std::string finish_hint_text;
    TTF_Font* hint_font = fonts.small ? fonts.small : subtitle_font;
    int finish_hint_height = 0;
    if (banner.show_finish_hint && hint_font) {
        finish_hint_text =
            using_controller ? "Press A to finish match" : "Press Enter to finish match";
        finish_hint_height = MeasureText(hint_font, finish_hint_text).second;
    }

    int dynamic_height = content_height;
    if (countdown_height > 0) {
        dynamic_height += countdown_spacing + countdown_height;
    }
    if (banner.show_finish_hint && finish_hint_height > 0) {
        dynamic_height += spacing * 5 + finish_hint_height;
    }

    SDL_Rect rect;
    rect.w = std::clamp(content_width + padding * 2,
                        static_cast<int>(board_width * 0.4f),
                        static_cast<int>(board_width * 0.9f));
    rect.h = std::max(dynamic_height + padding * 2,
                      static_cast<int>(layout.cell_size * 2.0f));
    rect.x = static_cast<int>(center_x - rect.w / 2.0f);
    rect.y = static_cast<int>(center_y - rect.h / 2.0f);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 16, 18, 24, static_cast<Uint8>(alpha_norm * 230.0f));
    SDL_RenderFillRect(renderer, &rect);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, static_cast<Uint8>(alpha_norm * 180.0f));
    SDL_RenderDrawRect(renderer, &rect);

    int text_y = rect.y + padding;
    if (!banner.title.empty()) {
        SDL_Color color{255, 255, 255, static_cast<Uint8>(alpha_norm * 255.0f)};
        text_y += DrawTextCentered(renderer, title_font, banner.title, rect.x + rect.w / 2, text_y, color);
    }
    if (!banner.subtitle.empty()) {
        text_y += spacing;
        SDL_Color color{220, 225, 233, static_cast<Uint8>(alpha_norm * 255.0f)};
        DrawTextCentered(renderer, subtitle_font, banner.subtitle, rect.x + rect.w / 2, text_y, color);
    }
    if (banner.countdown_active && !banner.countdown_text.empty() && countdown_font) {
        text_y += countdown_spacing;
        SDL_Color color{255, 214, 128, static_cast<Uint8>(alpha_norm * 255.0f)};
        DrawTextCentered(renderer, countdown_font, banner.countdown_text, rect.x + rect.w / 2, text_y, color);
    }
    if (banner.show_finish_hint) {
        text_y += spacing * 5;
        if (hint_font && !finish_hint_text.empty()) {
            SDL_Color hint_color{200, 205, 212, static_cast<Uint8>(alpha_norm * 255.0f)};
            DrawTextCentered(renderer, hint_font, finish_hint_text, rect.x + rect.w / 2, text_y, hint_color);
        }
    }
}

struct PauseMenuState {
    int selected = 0;
    std::array<SDL_Rect, 3> button_bounds{};
};

enum class PauseMenuAction { None, Resume, MainMenu, Quit };

PauseMenuAction HandlePauseMenuEvent(PauseMenuState& state,
                                     const match::platform::InputEvent& evt,
                                     bool /*using_controller*/) {
    constexpr int kOptionCount = 3;
    auto clamp_selection = [&]() {
        if (state.selected < 0) {
            state.selected = 0;
        } else if (state.selected >= kOptionCount) {
            state.selected = kOptionCount - 1;
        }
    };
    clamp_selection();

    auto activate = [&]() -> PauseMenuAction {
        switch (state.selected) {
            case 0:
                return PauseMenuAction::Resume;
            case 1:
                return PauseMenuAction::MainMenu;
            case 2:
                return PauseMenuAction::Quit;
            default:
                return PauseMenuAction::None;
        }
    };

    auto select_from_point = [&](int x, int y) -> bool {
        for (int i = 0; i < kOptionCount; ++i) {
            if (PointInRect(state.button_bounds[static_cast<std::size_t>(i)], x, y)) {
                state.selected = i;
                return true;
            }
        }
        return false;
    };

    switch (evt.type) {
        case match::platform::InputEventType::MouseMove:
            select_from_point(evt.x, evt.y);
            break;
        case match::platform::InputEventType::MouseButtonDown:
            if (evt.mouse_button == match::platform::MouseButton::Left) {
                if (select_from_point(evt.x, evt.y)) {
                    return activate();
                }
            } else if (evt.mouse_button == match::platform::MouseButton::Right) {
                return PauseMenuAction::Resume;
            }
            break;
        case match::platform::InputEventType::KeyDown: {
            using match::platform::KeyCode;
            if (evt.key == KeyCode::Up || evt.key == KeyCode::W) {
                --state.selected;
                clamp_selection();
            } else if (evt.key == KeyCode::Down || evt.key == KeyCode::S) {
                ++state.selected;
                clamp_selection();
            } else if (evt.key == KeyCode::Enter) {
                return activate();
            } else if (evt.key == KeyCode::Escape) {
                return PauseMenuAction::Resume;
            }
            break;
        }
        case match::platform::InputEventType::ControllerButtonDown: {
            using match::platform::ControllerButton;
            switch (evt.controller_button) {
                case ControllerButton::DPadUp:
                    --state.selected;
                    clamp_selection();
                    break;
                case ControllerButton::DPadDown:
                    ++state.selected;
                    clamp_selection();
                    break;
                case ControllerButton::A:
                    return activate();
                case ControllerButton::B:
                case ControllerButton::Menu:
                    return PauseMenuAction::Resume;
                default:
                    break;
            }
            break;
        }
        default:
            break;
    }
    return PauseMenuAction::None;
}

void RenderPauseMenu(SDL_Renderer* renderer,
                     const Fonts& fonts,
                     int window_width,
                     int window_height,
                     PauseMenuState& state) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 150);
    SDL_Rect fullscreen{0, 0, window_width, window_height};
    SDL_RenderFillRect(renderer, &fullscreen);

    const int panel_w = std::max(320, window_width / 3);
    const int panel_h = std::max(260, window_height / 2);
    SDL_Rect panel{window_width / 2 - panel_w / 2, window_height / 2 - panel_h / 2, panel_w, panel_h};
    SDL_SetRenderDrawColor(renderer, 24, 26, 32, 235);
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 180);
    SDL_RenderDrawRect(renderer, &panel);

    const char* kOptions[3] = {"Continue", "Main Menu", "Exit Game"};
    TTF_Font* title_font = fonts.heading ? fonts.heading : fonts.body;
    TTF_Font* body_font = fonts.body ? fonts.body : fonts.heading;
    if (title_font) {
        DrawTextCentered(renderer,
                         title_font,
                         std::string("Paused"),
                         window_width / 2,
                         panel.y + 20,
                         SDL_Color{255, 255, 255, 255});
    }

    const int button_width = panel_w - 80;
    const int button_height = 56;
    const int start_y = panel.y + 80;
    const int gap = 16;
    for (int i = 0; i < 3; ++i) {
        SDL_Rect rect{panel.x + (panel_w - button_width) / 2,
                      start_y + i * (button_height + gap),
                      button_width,
                      button_height};
        state.button_bounds[static_cast<std::size_t>(i)] = rect;
        bool selected = (i == state.selected);
        SDL_Color fill = selected ? SDL_Color{255, 205, 64, 255} : SDL_Color{48, 50, 60, 255};
        SDL_Color text = selected ? SDL_Color{20, 22, 26, 255} : SDL_Color{236, 238, 244, 255};
        SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
        SDL_RenderFillRect(renderer, &rect);
        SDL_SetRenderDrawColor(renderer, 80, 82, 94, 255);
        SDL_RenderDrawRect(renderer, &rect);
        if (body_font) {
            DrawTextCentered(renderer,
                             body_font,
                             std::string(kOptions[i]),
                             rect.x + rect.w / 2,
                             rect.y + 14,
                             text);
        }
    }
}


void ApplyWindowIcon(SDL_Window* window) {
    if (!window) {
        return;
    }
    const char* last_error = nullptr;
    auto try_load = [&](const std::string& name) -> SDL_Surface* {
        std::filesystem::path path = AssetPath(name);
        if (!FileExists(path)) {
            return nullptr;
        }
        SDL_Surface* surf = IMG_Load(path.string().c_str());
        if (!surf) {
            last_error = IMG_GetError();
        }
        return surf;
    };

    SDL_Surface* icon = try_load("icon.png");
    if (!icon) {
        icon = try_load("logo.PNG");
    }
    if (icon) {
                SDL_SetWindowIcon(window, icon);
                SDL_FreeSurface(icon);
    } else if (last_error) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to load window icon: %s", last_error);
    }
}

float ComputeUiScale(int window_w, int window_h) {
    if (window_w <= 0 || window_h <= 0) {
        return 1.0f;
    }
    const float scale_w = static_cast<float>(window_w) / static_cast<float>(kLogicalWidth);
    const float scale_h = static_cast<float>(window_h) / static_cast<float>(kLogicalHeight);
    const float scale = std::min(scale_w, scale_h);
    return std::clamp(scale, 0.6f, 3.0f);
}

bool CellFromPoint(const Layout& layout,
                   int cols,
                   int rows,
                   int x,
                   int y,
                   match::core::Cell& out) {
    const float rel_x = static_cast<float>(x) - layout.board_left;
    const float rel_y = static_cast<float>(y) - layout.board_top;
    if (rel_x < 0.0f || rel_y < 0.0f) {
        return false;
    }
    const float width = layout.cell_size * cols;
    const float height = layout.cell_size * rows;
    if (rel_x >= width || rel_y >= height) {
        return false;
    }
    int col = static_cast<int>(rel_x / layout.cell_size);
    int row = static_cast<int>(rel_y / layout.cell_size);
    if (col < 0 || col >= cols || row < 0 || row >= rows) {
        return false;
    }
    out = match::core::Cell{col, row};
    return true;
}

struct GameContext {
    std::string mode = "PvC";
    int players_count = 2;
    std::vector<std::string> player_names = {"Player 1", "Computer"};
    std::vector<int> player_scores = {0, 0};
    std::vector<int> moves_left_per_player = {3, 3};
    int round_current = 1;
    int round_total = 5;
    int moves_per_round_setting = 3;
    match::ui::TurnOrderOption turn_order_mode = match::ui::TurnOrderOption::Consecutive;
    std::string turn_order = "Consecutive";
    bool bombs_enabled = true;
    bool color_blast_enabled = true;
    int active_player = 0;
    int total_moves = 0;
    bool game_over = false;
    bool ai_pending = false;
    float ai_timer_ms = 0.0f;
    std::string status;
    InputMode last_input = InputMode::MouseKeyboard;
    std::string save_slot_display;
    std::string save_slot_file;
    bool autosave_enabled = false;
    bool autosave_dirty = false;
    float autosave_cooldown_ms = 0.0f;
    bool loaded_from_save = false;
    match::ui::TimeModeOption time_mode = match::ui::TimeModeOption::Classic;
    int blitz_turn_minutes = 2;
    int blitz_between_seconds = 10;
    float blitz_turn_total_ms = 0.0f;
    float blitz_turn_remaining_ms = 0.0f;
    float blitz_pre_turn_ms = 0.0f;
    int blitz_pre_turn_last_second = -1;
    bool blitz_pre_turn_active = false;
    bool blitz_turn_active = false;
    int last_player_index = -1;
    bool blitz_waiting_for_banner = false;
    bool blitz_pre_turn_queued = false;
    bool blitz_turn_start_queued = false;
};

struct TournamentMatch {
    int player_a = -1;
    int player_b = -1;
    int winner = -1;
};

struct TournamentRound {
    std::vector<TournamentMatch> matches;
};

struct TournamentState {
    bool active = false;
    match::ui::GameSettings base_settings;
    std::vector<std::string> players;
    std::vector<TournamentRound> rounds;
    int current_round = -1;
    int current_match = -1;
    std::array<int, 2> active_pair{-1, -1};
    bool awaiting_next_match = false;
    std::string last_summary;
};

TournamentState g_tournament;

void ResetTournamentState() {
    g_tournament = TournamentState{};
}

int DetermineMatchWinner(const GameContext& ctx);

int NextPowerOfTwo(int value) {
    int power = 1;
    while (power < value) {
        power <<= 1;
    }
    return power;
}

void PropagateWinner(TournamentState& state, int round_index, int match_index, int winner_index) {
    if (round_index < 0 || round_index >= static_cast<int>(state.rounds.size())) {
        return;
    }
    auto& match = state.rounds[static_cast<std::size_t>(round_index)].matches[static_cast<std::size_t>(match_index)];
    match.winner = winner_index;
    int next_round = round_index + 1;
    if (next_round >= static_cast<int>(state.rounds.size())) {
        state.last_summary = (winner_index >= 0 && winner_index < static_cast<int>(state.players.size()))
                                 ? state.players[static_cast<std::size_t>(winner_index)] + " wins the tournament"
                                 : "Tournament finished";
        return;
    }
    int target_index = match_index / 2;
    auto& next_match =
        state.rounds[static_cast<std::size_t>(next_round)].matches[static_cast<std::size_t>(target_index)];
    if (match_index % 2 == 0) {
        next_match.player_a = winner_index;
    } else {
        next_match.player_b = winner_index;
    }
}

void ResolveAutomaticAdvances(TournamentState& state) {
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t r = 0; r < state.rounds.size(); ++r) {
            auto& round = state.rounds[r];
            for (std::size_t m = 0; m < round.matches.size(); ++m) {
                auto& match = round.matches[m];
                if (match.winner != -1) {
                    continue;
                }
                const bool a_ready = match.player_a >= 0;
                const bool b_ready = match.player_b >= 0;
                if (a_ready && !b_ready) {
                    PropagateWinner(state, static_cast<int>(r), static_cast<int>(m), match.player_a);
                    changed = true;
                } else if (!a_ready && b_ready) {
                    PropagateWinner(state, static_cast<int>(r), static_cast<int>(m), match.player_b);
                    changed = true;
                }
            }
        }
    }
}

void BuildTournamentBracket(TournamentState& state) {
    if (state.players.size() < 2) {
        return;
    }
    std::vector<int> player_indices(state.players.size());
    std::iota(player_indices.begin(), player_indices.end(), 0);
    std::shuffle(player_indices.begin(), player_indices.end(), std::mt19937(std::random_device{}()));

    const int bracket_size = NextPowerOfTwo(static_cast<int>(state.players.size()));
    const int round_count = static_cast<int>(std::log2(bracket_size));
    state.rounds.clear();
    state.rounds.reserve(round_count);
    int matches_in_round = bracket_size / 2;

    std::vector<int> seeds(bracket_size, -1);
    for (std::size_t i = 0; i < player_indices.size() && i < seeds.size(); ++i) {
        seeds[i] = player_indices[i];
    }

    for (int round = 0; round < round_count; ++round) {
        TournamentRound bracket_round;
        bracket_round.matches.resize(static_cast<std::size_t>(matches_in_round));
        if (round == 0) {
            for (int match = 0; match < matches_in_round; ++match) {
                auto& entry = bracket_round.matches[static_cast<std::size_t>(match)];
                entry.player_a = seeds[match * 2];
                entry.player_b = seeds[match * 2 + 1];
            }
        }
        state.rounds.push_back(std::move(bracket_round));
        matches_in_round = std::max(1, matches_in_round / 2);
    }

    state.current_round = -1;
    state.current_match = -1;
    state.active_pair = {-1, -1};
    state.awaiting_next_match = true;
    state.last_summary.clear();
    ResolveAutomaticAdvances(state);
}

std::optional<std::pair<int, int>> NextReadyMatch(const TournamentState& state) {
    for (std::size_t r = 0; r < state.rounds.size(); ++r) {
        const auto& round = state.rounds[r];
        for (std::size_t m = 0; m < round.matches.size(); ++m) {
            const auto& match = round.matches[m];
            if (match.winner == -1 && match.player_a >= 0 && match.player_b >= 0) {
                return std::make_pair(static_cast<int>(r), static_cast<int>(m));
            }
        }
    }
    return std::nullopt;
}

bool TournamentHasReadyMatch(const TournamentState& state) {
    return NextReadyMatch(state).has_value();
}

bool TournamentComplete(const TournamentState& state) {
    if (state.rounds.empty()) {
        return false;
    }
    const auto& final_round = state.rounds.back();
    if (final_round.matches.empty()) {
        return false;
    }
    const auto& final_match = final_round.matches.back();
    return final_match.winner >= 0;
}

std::string TournamentStatusSummary(const TournamentState& state) {
    if (!state.active) {
        return {};
    }
    if (TournamentComplete(state)) {
        const int winner = state.rounds.back().matches.back().winner;
        if (winner >= 0 && winner < static_cast<int>(state.players.size())) {
            return "Winner: " + state.players[static_cast<std::size_t>(winner)];
        }
        return "Tournament complete";
    }
    auto next = NextReadyMatch(state);
    if (!next.has_value()) {
        return "Awaiting bracket updates";
    }
    const auto& match = state.rounds[static_cast<std::size_t>(next->first)].matches[static_cast<std::size_t>(next->second)];
    std::string a = (match.player_a >= 0 && match.player_a < static_cast<int>(state.players.size()))
                        ? state.players[static_cast<std::size_t>(match.player_a)]
                        : "TBD";
    std::string b = (match.player_b >= 0 && match.player_b < static_cast<int>(state.players.size()))
                        ? state.players[static_cast<std::size_t>(match.player_b)]
                        : "TBD";
    return "Next: " + a + " vs " + b;
}

match::ui::TournamentBracketView BuildTournamentBracketView(const TournamentState& state) {
    match::ui::TournamentBracketView view;
    int active_round = -1;
    int active_match = -1;
    bool current_in_progress = false;
    if (state.current_round >= 0 && state.current_match >= 0 &&
        state.current_round < static_cast<int>(state.rounds.size())) {
        const auto& match =
            state.rounds[static_cast<std::size_t>(state.current_round)]
                .matches[static_cast<std::size_t>(state.current_match)];
        if (match.winner == -1 && match.player_a >= 0 && match.player_b >= 0) {
            active_round = state.current_round;
            active_match = state.current_match;
            current_in_progress = true;
        }
    }
    for (std::size_t r = 0; r < state.rounds.size(); ++r) {
        std::vector<match::ui::BracketMatchView> column;
        const auto& round = state.rounds[r];
        for (std::size_t m = 0; m < round.matches.size(); ++m) {
            const auto& match = round.matches[m];
            match::ui::BracketMatchView row;
            if (match.player_a >= 0 && match.player_a < static_cast<int>(state.players.size())) {
                row.player_a = state.players[static_cast<std::size_t>(match.player_a)];
            } else {
                row.player_a = "TBD";
            }
            if (match.player_b >= 0 && match.player_b < static_cast<int>(state.players.size())) {
                row.player_b = state.players[static_cast<std::size_t>(match.player_b)];
            } else {
                row.player_b = "TBD";
            }
            row.player_a_won = (match.winner == match.player_a && match.player_a >= 0);
            row.player_b_won = (match.winner == match.player_b && match.player_b >= 0);
            row.active = false;
            column.push_back(row);
        }
        view.rounds.push_back(std::move(column));
    }
    auto next = NextReadyMatch(state);
    if (next.has_value()) {
        const auto& match =
            state.rounds[static_cast<std::size_t>(next->first)].matches[static_cast<std::size_t>(next->second)];
        bool ready = (match.player_a >= 0 && match.player_b >= 0 &&
                      match.player_a < static_cast<int>(state.players.size()) &&
                      match.player_b < static_cast<int>(state.players.size()));
        if (ready) {
            view.next_match_ready = true;
            view.next_match_label = "Next: " + state.players[static_cast<std::size_t>(match.player_a)] + " vs " +
                                    state.players[static_cast<std::size_t>(match.player_b)] + " (Round " +
                                    std::to_string(next->first + 1) + ")";
            if (!current_in_progress) {
                active_round = next->first;
                active_match = next->second;
            }
        }
    }
    if (active_round >= 0 && active_round < static_cast<int>(view.rounds.size()) && active_match >= 0 &&
        active_match < static_cast<int>(view.rounds[static_cast<std::size_t>(active_round)].size())) {
        view.rounds[static_cast<std::size_t>(active_round)][static_cast<std::size_t>(active_match)].active = true;
    }
    view.status = state.last_summary.empty() ? TournamentStatusSummary(state) : state.last_summary;
    return view;
}

void RecordTournamentResult(GameContext& ctx) {
    if (!g_tournament.active) {
        return;
    }
    const int round_idx = g_tournament.current_round;
    const int match_idx = g_tournament.current_match;
    if (round_idx < 0 || match_idx < 0 || round_idx >= static_cast<int>(g_tournament.rounds.size())) {
        return;
    }
    auto& match = g_tournament.rounds[static_cast<std::size_t>(round_idx)].matches[static_cast<std::size_t>(match_idx)];
    if (match.winner != -1) {
        return;
    }
    int winner_local = DetermineMatchWinner(ctx);
    int winner_global = -1;
    if (winner_local == -1) {
        // treat draw as first player advance
        winner_global = g_tournament.active_pair[0];
    } else {
        if (winner_local >= 0 && winner_local < 2) {
            winner_global = g_tournament.active_pair[winner_local];
        }
    }
    if (winner_global < 0) {
        winner_global = g_tournament.active_pair[0];
    }
    PropagateWinner(g_tournament, round_idx, match_idx, winner_global);
    g_tournament.last_summary = TournamentStatusSummary(g_tournament);
    g_tournament.awaiting_next_match = true;
    g_tournament.active_pair = {-1, -1};
    if (ctx.autosave_enabled) {
        ctx.autosave_dirty = true;
    }
}

bool IsComputerPlayer(const GameContext& ctx, int index);

std::string TurnOrderToString(match::ui::TurnOrderOption order) {
    return (order == match::ui::TurnOrderOption::RoundRobin) ? "Round-robin" : "Consecutive";
}

match::ui::TurnOrderOption TurnOrderFromString(const std::string& value) {
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower.find("round") != std::string::npos) {
        return match::ui::TurnOrderOption::RoundRobin;
    }
    return match::ui::TurnOrderOption::Consecutive;
}

void SyncPlayerVectors(GameContext& ctx) {
    ctx.players_count = std::max(1, ctx.players_count);
    if (ctx.player_names.size() < static_cast<std::size_t>(ctx.players_count)) {
        for (std::size_t i = ctx.player_names.size(); i < static_cast<std::size_t>(ctx.players_count); ++i) {
            ctx.player_names.push_back(match::ui::MakeDefaultPlayerName(static_cast<int>(i)));
        }
    } else if (ctx.player_names.size() > static_cast<std::size_t>(ctx.players_count)) {
        ctx.player_names.resize(static_cast<std::size_t>(ctx.players_count));
    }
    if (ctx.player_scores.size() < static_cast<std::size_t>(ctx.players_count)) {
        ctx.player_scores.resize(static_cast<std::size_t>(ctx.players_count), 0);
    } else if (ctx.player_scores.size() > static_cast<std::size_t>(ctx.players_count)) {
        ctx.player_scores.resize(static_cast<std::size_t>(ctx.players_count));
    }
    if (ctx.moves_left_per_player.size() < static_cast<std::size_t>(ctx.players_count)) {
        ctx.moves_left_per_player.resize(static_cast<std::size_t>(ctx.players_count),
                                         ctx.moves_per_round_setting);
    } else if (ctx.moves_left_per_player.size() > static_cast<std::size_t>(ctx.players_count)) {
        ctx.moves_left_per_player.resize(static_cast<std::size_t>(ctx.players_count));
    }
    ctx.active_player = std::clamp(ctx.active_player, 0,
                                   std::max(0, ctx.players_count - 1));
}

void ResetMovesForNewRound(GameContext& ctx) {
    SyncPlayerVectors(ctx);
    std::fill(ctx.moves_left_per_player.begin(), ctx.moves_left_per_player.end(),
              ctx.moves_per_round_setting);
}

bool RoundComplete(const GameContext& ctx) {
    if (ctx.moves_left_per_player.empty()) {
        return true;
    }
    return std::all_of(ctx.moves_left_per_player.begin(), ctx.moves_left_per_player.end(),
                       [](int value) { return value <= 0; });
}

int NextPlayerWithMoves(const GameContext& ctx, int from_player) {
    if (ctx.moves_left_per_player.empty() || ctx.players_count <= 0) {
        return -1;
    }
    const int count = std::min<int>(ctx.players_count, static_cast<int>(ctx.moves_left_per_player.size()));
    for (int step = 1; step <= count; ++step) {
        int candidate = (from_player + step) % count;
        if (ctx.moves_left_per_player[static_cast<std::size_t>(candidate)] > 0) {
            return candidate;
        }
    }
    return -1;
}

std::string BuildGameOverStatus(const GameContext& ctx) {
    if (ctx.player_scores.empty() || ctx.player_names.empty()) {
        return "Game over";
    }
    int best = std::numeric_limits<int>::min();
    for (int score : ctx.player_scores) {
        best = std::max(best, score);
    }
    std::vector<std::string> winners;
    for (std::size_t i = 0; i < ctx.player_scores.size() && i < ctx.player_names.size(); ++i) {
        if (ctx.player_scores[i] == best) {
            winners.push_back(ctx.player_names[i]);
        }
    }
    std::string status = "Game over — ";
    status += (winners.size() > 1) ? "Winners: " : "Winner: ";
    for (std::size_t i = 0; i < winners.size(); ++i) {
        status += winners[i];
        if (i + 1 < winners.size()) {
            status += ", ";
        }
    }
    status += " (Score " + std::to_string(best) + ")";
    return status;
}

std::string BuildTurnStatus(const GameContext& ctx) {
    if (ctx.game_over) {
        return BuildGameOverStatus(ctx);
    }
    std::string player = (ctx.active_player >= 0 &&
                          ctx.active_player < static_cast<int>(ctx.player_names.size()))
                             ? ctx.player_names[static_cast<std::size_t>(ctx.active_player)]
                             : "Player";
    int moves_left = (ctx.active_player >= 0 &&
                      ctx.active_player < static_cast<int>(ctx.moves_left_per_player.size()))
                         ? ctx.moves_left_per_player[static_cast<std::size_t>(ctx.active_player)]
                         : 0;
    return "Round " + std::to_string(std::max(1, ctx.round_current)) + " — " + player +
           " (" + std::to_string(std::max(0, moves_left)) + " moves left)";
}

void SyncTurnOrderLabel(GameContext& ctx) {
    ctx.turn_order = TurnOrderToString(ctx.turn_order_mode);
}

void UpdateAiPending(GameContext& ctx) {
    if (ctx.game_over) {
        ctx.ai_pending = false;
        ctx.ai_timer_ms = 0.0f;
        return;
    }
    ctx.ai_pending = IsComputerPlayer(ctx, ctx.active_player);
    ctx.ai_timer_ms = 0.0f;
    if (ctx.ai_pending) {
        ctx.status = "Computer is thinking...";
    }
}

bool IsComputerPlayer(const GameContext& ctx, int index) {
    if (index < 0 || index >= static_cast<int>(ctx.player_names.size())) {
        return false;
    }
    std::string name = ctx.player_names[static_cast<std::size_t>(index)];
    for (auto& ch : name) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return name.find("computer") != std::string::npos;
}

bool ActivePlayerIsHuman(const GameContext& ctx) {
    return !IsComputerPlayer(ctx, ctx.active_player);
}

bool ActivePlayerHasMoves(const GameContext& ctx) {
    if (ctx.game_over) {
        return false;
    }
    if (ctx.moves_left_per_player.empty() || ctx.players_count <= 0) {
        return true;
    }
    if (ctx.active_player < 0 ||
        ctx.active_player >= static_cast<int>(ctx.moves_left_per_player.size())) {
        return false;
    }
    return ctx.moves_left_per_player[static_cast<std::size_t>(ctx.active_player)] > 0;
}

int DetermineMatchWinner(const GameContext& ctx) {
    if (ctx.player_scores.size() < 2) {
        return -1;
    }
    int a = ctx.player_scores[0];
    int b = ctx.player_scores[1];
    if (a == b) {
        return -1;
    }
    return (a > b) ? 0 : 1;
}

std::string TournamentWinnersSummary() {
    if (g_tournament.rounds.empty() || g_tournament.rounds.back().matches.empty()) {
        return "Tournament complete";
    }
    const auto& final_match = g_tournament.rounds.back().matches.back();
    int winner = final_match.winner;
    if (winner >= 0 && winner < static_cast<int>(g_tournament.players.size())) {
        return g_tournament.players[static_cast<std::size_t>(winner)] + " wins the tournament";
    }
    return "Tournament finished with no winner";
}


bool ActivePlayerCanHumanAct(const GameContext& ctx) {
    return ActivePlayerIsHuman(ctx) && ActivePlayerHasMoves(ctx);
}

void RandomizePlayerOrder(GameContext& ctx) {
    SyncPlayerVectors(ctx);
    if (ctx.players_count <= 1) {
        return;
    }
    std::vector<int> order(static_cast<std::size_t>(ctx.players_count));
    std::iota(order.begin(), order.end(), 0);
    std::mt19937 rng(std::random_device{}());
    std::shuffle(order.begin(), order.end(), rng);

    auto reorder_strings = [&](std::vector<std::string>& vec) {
        std::vector<std::string> copy;
        copy.reserve(order.size());
        for (int idx : order) {
            if (idx >= 0 && idx < static_cast<int>(vec.size())) {
                copy.push_back(vec[static_cast<std::size_t>(idx)]);
            }
        }
        vec = std::move(copy);
    };
    auto reorder_ints = [&](std::vector<int>& vec) {
        std::vector<int> copy;
        copy.reserve(order.size());
        for (int idx : order) {
            if (idx >= 0 && idx < static_cast<int>(vec.size())) {
                copy.push_back(vec[static_cast<std::size_t>(idx)]);
            }
        }
        vec = std::move(copy);
    };

    reorder_strings(ctx.player_names);
    reorder_ints(ctx.player_scores);
    reorder_ints(ctx.moves_left_per_player);
    ctx.active_player = 0;
}

std::string EnsureComputerName(std::string name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower.find("computer") == std::string::npos) {
        if (!name.empty()) {
            name += ' ';
        }
        name += "(Computer)";
    }
    return name;
}

struct SaveNameComponents {
    std::string display;
    std::string filename;
};

struct BoardState;

SaveNameComponents SanitizeSaveName(const std::string& input) {
    SaveNameComponents result;
    std::string cleaned;
    cleaned.reserve(input.size());
    bool last_was_space = false;
    for (unsigned char ch : input) {
        if (std::isalnum(ch)) {
            cleaned.push_back(static_cast<char>(ch));
            last_was_space = false;
        } else if (ch == ' ' || ch == '_' || ch == '-') {
            if (!cleaned.empty() && !last_was_space) {
                cleaned.push_back(' ');
                last_was_space = true;
            }
        }
    }
    while (!cleaned.empty() && std::isspace(static_cast<unsigned char>(cleaned.back()))) {
        cleaned.pop_back();
    }
    if (cleaned.empty()) {
        cleaned = "Save";
    }
    result.display = cleaned;
    result.filename = cleaned;
    std::replace(result.filename.begin(), result.filename.end(), ' ', '_');
    if (result.filename.empty()) {
        result.filename = "Save";
    }
    return result;
}

std::string GenerateDefaultSaveName(match::ui::GameMode mode,
                                    const std::vector<match::platform::SaveSlotInfo>& slots) {
    std::string base;
    switch (mode) {
        case match::ui::GameMode::PvC:
            base = "PvC Save";
            break;
        case match::ui::GameMode::PvP:
            base = "PvP Save";
            break;
        case match::ui::GameMode::Tournament:
            base = "Tournament Save";
            break;
    }
    int counter = 1;
    while (counter < 1000) {
        std::string candidate = base + " " + std::to_string(counter);
        bool exists = false;
        for (const auto& slot : slots) {
            if (slot.name == candidate) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            return candidate;
        }
        ++counter;
    }
    return base;
}

void SortSlotsNewestFirst(std::vector<match::platform::SaveSlotInfo>& slots) {
    std::sort(slots.begin(), slots.end(),
              [](const match::platform::SaveSlotInfo& a,
                 const match::platform::SaveSlotInfo& b) { return a.modified_time > b.modified_time; });
}

std::string ModeFolder(match::ui::GameMode mode) {
    switch (mode) {
        case match::ui::GameMode::PvC:
            return "PvC";
        case match::ui::GameMode::PvP:
            return "PvP";
        case match::ui::GameMode::Tournament:
            return "Tournament";
    }
    return "Unknown";
}

std::string GameModeToken(match::ui::GameMode mode) {
    switch (mode) {
        case match::ui::GameMode::PvC:
            return "pvc";
        case match::ui::GameMode::PvP:
            return "pvp";
        case match::ui::GameMode::Tournament:
            return "tournament";
    }
    return "unknown";
}

match::ui::GameMode GameModeFromToken(const std::string& token) {
    std::string lower = token;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower.find("tournament") != std::string::npos) {
        return match::ui::GameMode::Tournament;
    }
    if (lower.find("pvp") != std::string::npos) {
        return match::ui::GameMode::PvP;
    }
    return match::ui::GameMode::PvC;
}

match::core::Json SerializeGameSettings(const match::ui::GameSettings& settings) {
    match::core::Json json = match::core::Json::object();
    json["mode"] = GameModeToken(settings.mode);
    json["player_count"] = settings.player_count;
    json["player_names"] = settings.player_names;
    json["turn_order"] = TurnOrderToString(settings.turn_order);
    json["moves_per_round"] = settings.moves_per_round;
    json["total_rounds"] = settings.total_rounds;
    json["bombs_enabled"] = settings.bombs_enabled;
    json["color_blast_enabled"] = settings.color_blast_enabled;
    json["time_mode"] =
        (settings.time_mode == match::ui::TimeModeOption::Classic) ? "classic" : "blitz";
    json["blitz_turn_minutes"] = settings.blitz_turn_minutes;
    json["blitz_between_seconds"] = settings.blitz_between_seconds;
    return json;
}

match::ui::GameSettings DeserializeGameSettings(const match::core::Json& json,
                                                const match::ui::GameSettings& defaults) {
    match::ui::GameSettings settings = defaults;
    if (json.contains("mode") && json["mode"].is_string()) {
        settings.mode = GameModeFromToken(json["mode"].get<std::string>());
    }
    settings.player_count = json.value("player_count", settings.player_count);
    if (json.contains("player_names") && json["player_names"].is_array()) {
        settings.player_names = json["player_names"].get<std::vector<std::string>>();
    }
    std::string order =
        json.value("turn_order", TurnOrderToString(settings.turn_order));
    settings.turn_order = TurnOrderFromString(order);
    settings.moves_per_round = json.value("moves_per_round", settings.moves_per_round);
    settings.total_rounds = json.value("total_rounds", settings.total_rounds);
    settings.bombs_enabled = json.value("bombs_enabled", settings.bombs_enabled);
    settings.color_blast_enabled =
        json.value("color_blast_enabled", settings.color_blast_enabled);
    std::string time_mode =
        json.value("time_mode",
                   (settings.time_mode == match::ui::TimeModeOption::Classic) ? "classic"
                                                                              : "blitz");
    settings.time_mode =
        (time_mode == "blitz") ? match::ui::TimeModeOption::Blitz
                               : match::ui::TimeModeOption::Classic;
    settings.blitz_turn_minutes =
        json.value("blitz_turn_minutes", settings.blitz_turn_minutes);
    settings.blitz_between_seconds =
        json.value("blitz_between_seconds", settings.blitz_between_seconds);
    settings.EnsureConstraints();
    return settings;
}


std::filesystem::path DetermineStandaloneSaveRoot() {
    std::filesystem::path base_dir;
    if (char* base = SDL_GetBasePath()) {
        base_dir = base;
        SDL_free(base);
    }
    if (base_dir.empty()) {
        base_dir = std::filesystem::current_path();
    }
    std::filesystem::path saves_dir = base_dir / "saves";
    std::error_code ec;
    std::filesystem::create_directories(saves_dir, ec);
    return saves_dir;
}

std::string FormatTimestamp(std::time_t value) {
    if (value <= 0) {
        return "Unknown";
    }
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &value);
#else
    localtime_r(&value, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M");
    return oss.str();
}

std::string TimeModeToString(match::ui::TimeModeOption mode) {
    return (mode == match::ui::TimeModeOption::Classic) ? "Classic" : "Blitz";
}

constexpr float kIntroMinimumDurationMs = 2000.0f;

struct IntroState {
    bool active = true;
    SDL_Texture* logo_texture = nullptr;
    int logo_w = 0;
    int logo_h = 0;
    float elapsed_ms = 0.0f;
    float min_duration_ms = 0.0f;
};

void DestroyIntroResources(IntroState& state) {
    if (state.logo_texture) {
        SDL_DestroyTexture(state.logo_texture);
        state.logo_texture = nullptr;
    }
}

void TryLoadIntroLogo(SDL_Renderer* renderer, IntroState& state) {
    std::filesystem::path logo_path = AssetPath("logo.png");
    if (!FileExists(logo_path)) {
        state.active = false;
        return;
    }
    SDL_Surface* surface = IMG_Load(logo_path.string().c_str());
    if (!surface) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to load logo.png: %s", IMG_GetError());
        state.active = false;
        return;
    }
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    state.logo_w = surface->w;
    state.logo_h = surface->h;
    SDL_FreeSurface(surface);
    if (!texture) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to create logo texture: %s", SDL_GetError());
        state.active = false;
        return;
    }
    state.logo_texture = texture;
}

void UpdateIntroState(IntroState& state, float delta_ms) {
    if (!state.active) {
        return;
    }
    state.elapsed_ms += delta_ms;
    if (state.elapsed_ms >= state.min_duration_ms) {
        state.active = false;
    }
}

void RenderIntro(SDL_Renderer* renderer,
                 const Fonts& fonts,
                 int window_w,
                 int window_h,
                 const IntroState& state) {
    SDL_SetRenderDrawColor(renderer, 10, 10, 12, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    if (state.logo_texture) {
        const float max_w = window_w * 0.6f;
        const float max_h = window_h * 0.6f;
        float scale = 1.0f;
        if (state.logo_w > 0 && state.logo_h > 0) {
            scale = std::min(max_w / static_cast<float>(state.logo_w),
                             max_h / static_cast<float>(state.logo_h));
        }
        scale = std::max(scale, 0.1f);
        int dst_w = static_cast<int>(std::round(state.logo_w * scale));
        int dst_h = static_cast<int>(std::round(state.logo_h * scale));
        SDL_Rect dst{(window_w - dst_w) / 2, (window_h - dst_h) / 2, dst_w, dst_h};
        SDL_RenderCopy(renderer, state.logo_texture, nullptr, &dst);
    }
}

match::ui::SaveSummary BuildSaveSummary(match::platform::SdlSaveService& service,
                                        const match::platform::SaveSlotInfo& slot) {
    match::ui::SaveSummary summary;
    summary.slot = slot;
    summary.title = slot.name;
    summary.is_new_entry = false;
    std::vector<std::uint8_t> bytes;
    if (!service.Load(slot.name, bytes)) {
        summary.valid = false;
        summary.error = "Unable to read save file";
        return summary;
    }
    try {
        auto payload = match::core::SavePayload::DeserializeBinary(bytes);
        const auto& data = payload.data;
        const match::core::Json* tournament_json = nullptr;
        auto tour_it = data.find("tournament");
        if (tour_it != data.end() && tour_it->is_object()) {
            tournament_json = &(*tour_it);
        }
        bool is_tournament = (payload.mode == "Tournament");
        if (tournament_json) {
            is_tournament =
                is_tournament || tournament_json->value("active", false);
        }
        if (is_tournament && tournament_json) {
            summary.detail_lines.clear();
            summary.title = slot.name + " (Tournament)";
            auto players = tournament_json->value("players", std::vector<std::string>{});
            summary.detail_lines.push_back("Players: " + std::to_string(players.size()));
            auto rounds_json = tournament_json->value("rounds", match::core::Json::array());
            int round_count = rounds_json.is_array() ? static_cast<int>(rounds_json.size()) : 0;
            summary.detail_lines.push_back("Rounds: " + std::to_string(std::max(1, round_count)));
            auto name_for = [&](int idx) -> std::string {
                if (idx >= 0 && static_cast<std::size_t>(idx) < players.size()) {
                    return players[static_cast<std::size_t>(idx)];
                }
                return "TBD";
            };
            int next_a = -1;
            int next_b = -1;
            if (rounds_json.is_array()) {
                for (const auto& round_entry : rounds_json) {
                    if (!round_entry.is_array()) {
                        continue;
                    }
                    for (const auto& match_entry : round_entry) {
                        if (!match_entry.is_object()) {
                            continue;
                        }
                        int player_a = match_entry.value("player_a", -1);
                        int player_b = match_entry.value("player_b", -1);
                        int winner = match_entry.value("winner", -1);
                        if (winner == -1 && next_a == -1 && player_a >= 0 && player_b >= 0) {
                            next_a = player_a;
                            next_b = player_b;
                        }
                    }
                }
            }
            if (next_a >= 0 && next_b >= 0) {
                summary.detail_lines.push_back("Next: " + name_for(next_a) + " vs " + name_for(next_b));
            } else {
                int final_winner = -1;
                if (rounds_json.is_array() && !rounds_json.empty()) {
                    const auto& final_round = rounds_json.back();
                    if (final_round.is_array() && !final_round.empty()) {
                        const auto& final_match = final_round.back();
                        if (final_match.is_object()) {
                            final_winner = final_match.value("winner", -1);
                        }
                    }
                }
                if (final_winner >= 0) {
                    summary.detail_lines.push_back("Winner: " + name_for(final_winner));
                } else {
                    summary.detail_lines.push_back("Bracket pending");
                }
            }
            if (tournament_json->contains("last_summary") &&
                (*tournament_json)["last_summary"].is_string()) {
                std::string standings = (*tournament_json)["last_summary"].get<std::string>();
                if (!standings.empty()) {
                    summary.detail_lines.push_back(standings);
                }
            }
            summary.detail_lines.push_back("Saved " + FormatTimestamp(slot.modified_time));
            return summary;
        }
        auto names = data.value("player_names", std::vector<std::string>{});
        auto scores = data.value("player_scores", std::vector<int>{});
        int round = data.value("round_current", 1);
        int total_rounds = data.value("round_total", 1);
        std::string time_mode = data.value("time_mode", std::string("classic"));
        std::ostringstream line0;
        if (names.size() >= 2) {
            line0 << names[0] << " vs " << names[1];
        } else if (!names.empty()) {
            line0 << names.front();
        } else {
            line0 << "Unknown players";
        }
        summary.detail_lines.push_back(line0.str());

        std::ostringstream line1;
        if (scores.size() >= 2) {
            line1 << "Score: " << scores[0] << " / ";
            if (scores.size() > 1) {
                line1 << scores[1];
            } else {
                line1 << "0";
            }
        }
        line1 << "  Round " << round << "/" << total_rounds;
        summary.detail_lines.push_back(line1.str());

        std::ostringstream line2;
        line2 << (time_mode == "blitz" ? "Blitz mode" : "Classic mode") << "  Saved "
              << FormatTimestamp(slot.modified_time);
        summary.detail_lines.push_back(line2.str());
    } catch (const std::exception& ex) {
        summary.valid = false;
        summary.error = std::string("Corrupt save: ") + ex.what();
    }
    return summary;
}

PanelInfo BuildPanelInfo(const GameContext& ctx) {
    PanelInfo panel;
    panel.mode = ctx.mode;
    panel.round = std::to_string(ctx.round_current) + "/" + std::to_string(ctx.round_total);
    panel.order = ctx.turn_order;
    panel.bombs_enabled = ctx.bombs_enabled;
    panel.color_blast_enabled = ctx.color_blast_enabled;
    int moves_left_current = 0;
    if (ctx.active_player >= 0 &&
        ctx.active_player < static_cast<int>(ctx.moves_left_per_player.size())) {
        moves_left_current = ctx.moves_left_per_player[static_cast<std::size_t>(ctx.active_player)];
    }
    panel.moves_left = moves_left_current;
    panel.status = ctx.status.empty() ? "Ready" : ctx.status;

    panel.players.reserve(ctx.player_names.size());
    for (std::size_t i = 0; i < ctx.player_names.size(); ++i) {
        PanelPlayerEntry entry;
        entry.name = ctx.player_names[i];
        entry.score = (i < ctx.player_scores.size()) ? ctx.player_scores[i] : 0;
        entry.active = static_cast<int>(i) == ctx.active_player;
        if (i < ctx.moves_left_per_player.size()) {
            entry.moves_left = ctx.moves_left_per_player[i];
        }
        panel.players.push_back(entry);
    }

    if (ctx.last_input == InputMode::Controller) {
        panel.controls = {
            "Left stick or D-pad: move cursor",
            "A button: select / swap",
            "B button: cancel selection",
            "X button: decrease value",
            "Y button: increase value",
            "Menu button: exit game",
        };
    } else {
        panel.controls = {
            "Mouse: left click to select tile",
            "Click adjacent tile to swap",
            "Keyboard: ESC to exit game",
        };
    }
    if (ctx.time_mode == match::ui::TimeModeOption::Blitz) {
        if (ctx.blitz_pre_turn_active) {
            panel.show_pre_turn = true;
            panel.pre_turn_seconds =
                static_cast<int>(std::ceil(ctx.blitz_pre_turn_ms / 1000.0f));
        }
        if (ctx.blitz_turn_active) {
            panel.show_turn_timer = true;
            panel.turn_timer_ms = ctx.blitz_turn_remaining_ms;
            panel.turn_timer_total_ms = ctx.blitz_turn_total_ms;
        }
    }
    return panel;
}


struct CascadeState {
    enum class Phase { Idle, Swap, Pop, Fall };

    Phase phase = Phase::Idle;
    bool active = false;
    std::vector<match::core::SimulationResult::ChainEvent> chains;
    std::size_t chain_index = 0;
    match::core::Board working_board;
    match::core::Board final_board;
    match::core::Move move{};
    match::core::SimulationResult result{};
};

struct BoardState {
    match::core::Board board;
    std::optional<match::core::Cell> selected;
    std::optional<match::core::Cell> hover;
    std::optional<match::core::Cell> controller_cursor;
    std::vector<Animation> animations;
    std::set<match::core::Cell> hidden_cells;
    CascadeState cascade;
    Layout layout{};
    int controller_axis_horizontal = 0;
    int controller_axis_vertical = 0;
    Uint32 controller_axis_horizontal_tick = 0;
    Uint32 controller_axis_vertical_tick = 0;
};

std::string ActivePlayerLabel(const GameContext& ctx) {
    if (ctx.active_player >= 0 &&
        ctx.active_player < static_cast<int>(ctx.player_names.size())) {
        return ctx.player_names[static_cast<std::size_t>(ctx.active_player)];
    }
    if (ctx.active_player >= 0) {
        return "Player " + std::to_string(ctx.active_player + 1);
    }
    return "Player";
}

int ActivePlayerMoves(const GameContext& ctx) {
    if (ctx.active_player >= 0 &&
        ctx.active_player < static_cast<int>(ctx.moves_left_per_player.size())) {
        return ctx.moves_left_per_player[static_cast<std::size_t>(ctx.active_player)];
    }
    return 0;
}

std::string WinnersSubtitle(const GameContext& ctx) {
    if (ctx.player_scores.empty() || ctx.player_names.empty()) {
        return "Game over";
    }
    int best = std::numeric_limits<int>::min();
    for (int score : ctx.player_scores) {
        best = std::max(best, score);
    }
    std::vector<std::string> winners;
    for (std::size_t i = 0; i < ctx.player_scores.size() && i < ctx.player_names.size(); ++i) {
        if (ctx.player_scores[i] == best) {
            winners.push_back(ctx.player_names[i]);
        }
    }
    if (winners.empty()) {
        return "Game over";
    }
    std::string text = (winners.size() > 1) ? "Winners: " : "Winner: ";
    for (std::size_t i = 0; i < winners.size(); ++i) {
        text += winners[i];
        if (i + 1 < winners.size()) {
            text += ", ";
        }
    }
    text += " — Score " + std::to_string(best);
    return text;
}

void AnnounceRoundStart(const GameContext& ctx) {
    std::string title = "Round " + std::to_string(std::max(1, ctx.round_current)) + "/" +
                        std::to_string(std::max(1, ctx.round_total));
    std::string subtitle = ActivePlayerLabel(ctx) + " begins";
    float sound_ms = PlayNextRoundSound();
    float duration = std::max(2600.0f, sound_ms + 250.0f);
    ShowBannerMessage(title, subtitle, duration, true, false);
}

void AnnounceTurnStart(const GameContext& ctx) {
    std::string title = ActivePlayerLabel(ctx);
    int moves = std::max(0, ActivePlayerMoves(ctx));
    std::string subtitle = "Round " + std::to_string(std::max(1, ctx.round_current)) + "/" +
                           std::to_string(std::max(1, ctx.round_total)) + " — " +
                           std::to_string(moves) + ((moves == 1) ? " move left" : " moves left");
    float sound_ms = PlayNextTurnSound();
    float duration = std::max(2000.0f, sound_ms + 200.0f);
    ShowBannerMessage(title, subtitle, duration, true, false);
}

void AnnounceVictory(const GameContext& ctx) {
    if (g_win_announced) {
        return;
    }
    float sound_ms = PlayWinSound();
    float duration = std::max(3600.0f, sound_ms + 500.0f);
    ShowBannerMessage("Match Complete", WinnersSubtitle(ctx), duration, true, true, true);
    g_win_announced = true;
}

void BeginBlitzPreTurnBanner(GameContext& ctx) {
    ctx.blitz_pre_turn_active = true;
    ctx.blitz_turn_active = false;
    ctx.blitz_pre_turn_last_second = -1;
    std::string title = ActivePlayerLabel(ctx);
    std::string subtitle = "Prepare for your turn";
    float duration = std::max(ctx.blitz_pre_turn_ms, 1500.0f);
    ShowBannerMessage(title, subtitle, duration, true, true);
    g_banner.countdown_active = true;
    g_banner.countdown_text = std::to_string(std::max(0, ctx.blitz_between_seconds));
    g_banner.block_input = true;
    g_banner.show_finish_hint = false;
}

void BeginBlitzTurnTimer(GameContext& ctx) {
    ctx.blitz_pre_turn_active = false;
    ctx.blitz_turn_active = true;
    g_banner.countdown_active = false;
    g_banner.countdown_text.clear();
    g_banner.block_input = false;
    g_banner.persistent = false;
}

void MaybeActivateBlitzQueue(GameContext& ctx) {
    if (ctx.time_mode != match::ui::TimeModeOption::Blitz || ctx.game_over) {
        ctx.blitz_pre_turn_queued = false;
        ctx.blitz_turn_start_queued = false;
        ctx.blitz_waiting_for_banner = false;
        return;
    }
    if (!ctx.blitz_pre_turn_queued && !ctx.blitz_turn_start_queued) {
        return;
    }
    if (ctx.blitz_waiting_for_banner && g_banner.visible) {
        return;
    }
    ctx.blitz_waiting_for_banner = false;
    if (ctx.blitz_pre_turn_queued) {
        ctx.blitz_pre_turn_queued = false;
        BeginBlitzPreTurnBanner(ctx);
        return;
    }
    if (ctx.blitz_turn_start_queued) {
        ctx.blitz_turn_start_queued = false;
        BeginBlitzTurnTimer(ctx);
    }
}

void StartBlitzTurn(GameContext& ctx, bool player_changed) {
    if (ctx.time_mode != match::ui::TimeModeOption::Blitz || ctx.game_over) {
        ctx.blitz_pre_turn_active = false;
        ctx.blitz_turn_active = false;
        ctx.blitz_pre_turn_ms = 0.0f;
        ctx.blitz_pre_turn_queued = false;
        ctx.blitz_turn_start_queued = false;
        ctx.blitz_waiting_for_banner = false;
        return;
    }
    ctx.blitz_turn_total_ms = ctx.blitz_turn_minutes * 60.0f * 1000.0f;
    ctx.blitz_turn_remaining_ms = ctx.blitz_turn_total_ms;
    ctx.last_player_index = ctx.active_player;
    ctx.blitz_pre_turn_ms = 0.0f;
    ctx.blitz_pre_turn_active = false;
    ctx.blitz_turn_active = false;
    ctx.blitz_pre_turn_queued = false;
    ctx.blitz_turn_start_queued = false;
    ctx.blitz_waiting_for_banner = g_banner.visible;
    if (player_changed && ctx.blitz_between_seconds > 0) {
        ctx.blitz_pre_turn_ms = ctx.blitz_between_seconds * 1000.0f;
        ctx.blitz_pre_turn_queued = true;
    } else {
        ctx.blitz_turn_start_queued = true;
    }
    MaybeActivateBlitzQueue(ctx);
}

void ForceTurnTimeout(BoardState& state, GameContext& ctx) {
    (void)state;
    if (ctx.active_player < 0) {
        return;
    }
    int previous_round = ctx.round_current;
    int previous_player = ctx.active_player;
    bool previous_game_over = ctx.game_over;
    SyncPlayerVectors(ctx);
    if (ctx.active_player >= 0 &&
        ctx.active_player < static_cast<int>(ctx.moves_left_per_player.size())) {
        ctx.moves_left_per_player[static_cast<std::size_t>(ctx.active_player)] = 0;
    }
    ctx.status = "Time expired";
    if (RoundComplete(ctx)) {
        ctx.round_current += 1;
        if (ctx.round_current > ctx.round_total) {
            ctx.game_over = true;
            ctx.status = BuildGameOverStatus(ctx);
        } else {
            ResetMovesForNewRound(ctx);
            ctx.active_player = 0;
            ctx.status = BuildTurnStatus(ctx);
        }
    } else {
        int next = NextPlayerWithMoves(ctx, ctx.active_player);
        if (next >= 0) {
            ctx.active_player = next;
        }
        if (!ctx.game_over) {
            ctx.status = BuildTurnStatus(ctx);
        }
    }
    HandleStateFeedback(ctx, previous_round, previous_player, previous_game_over);
    UpdateAiPending(ctx);
}

void UpdateBlitzTimers(BoardState& state, GameContext& ctx, float delta_ms) {
    if (ctx.time_mode != match::ui::TimeModeOption::Blitz || ctx.game_over) {
        return;
    }
    MaybeActivateBlitzQueue(ctx);
    if (ctx.blitz_pre_turn_active) {
        float previous_ms = ctx.blitz_pre_turn_ms;
        ctx.blitz_pre_turn_ms = std::max(0.0f, ctx.blitz_pre_turn_ms - delta_ms);
        int seconds_left =
            static_cast<int>(std::ceil(ctx.blitz_pre_turn_ms / 1000.0f));
        if (ctx.blitz_pre_turn_ms <= 0.0f) {
            ctx.blitz_pre_turn_active = false;
            ctx.blitz_turn_active = true;
            g_banner.countdown_active = false;
            g_banner.countdown_text.clear();
            g_banner.block_input = false;
            g_banner.persistent = false;
            if (!g_banner.show_finish_hint) {
                ResetBannerOverlay();
            }
        } else if (seconds_left != ctx.blitz_pre_turn_last_second) {
            ctx.blitz_pre_turn_last_second = seconds_left;
            if (seconds_left == 1) {
                PlayCountdownFinalSound();
            } else {
                PlayCountdownTickSound();
            }
            g_banner.countdown_active = true;
            g_banner.countdown_text = std::to_string(seconds_left);
        }
        return;
    }
    if (!ctx.blitz_turn_active) {
        return;
    }
    if (!state.cascade.active && state.animations.empty() && !BannerBlocksInput()) {
        ctx.blitz_turn_remaining_ms =
            std::max(0.0f, ctx.blitz_turn_remaining_ms - delta_ms);
    }
    if (ctx.blitz_turn_remaining_ms <= 0.0f) {
        ctx.blitz_turn_active = false;
        ForceTurnTimeout(state, ctx);
    }
}

void HandleStateFeedback(GameContext& ctx,
                         int previous_round,
                         int previous_player,
                         bool previous_game_over) {
    if (!previous_game_over && ctx.game_over) {
        AnnounceVictory(ctx);
        if (ctx.mode == "Tournament") {
            RecordTournamentResult(ctx);
        }
        return;
    }
    if (ctx.game_over) {
        return;
    }
    bool player_changed = ctx.active_player != previous_player;
    if (ctx.round_current != previous_round) {
        AnnounceRoundStart(ctx);
        if (ctx.time_mode == match::ui::TimeModeOption::Blitz) {
            StartBlitzTurn(ctx, true);
        }
        return;
    }
    if (player_changed) {
        AnnounceTurnStart(ctx);
    }
    if (ctx.time_mode == match::ui::TimeModeOption::Blitz) {
        StartBlitzTurn(ctx, player_changed);
    } else {
        ctx.blitz_pre_turn_active = false;
        ctx.blitz_turn_active = false;
    }
}

match::core::SavePayload BuildSavePayload(const BoardState& state, const GameContext& ctx) {
    match::core::SavePayload payload;
    payload.version = 1;
    payload.mode = ctx.mode;
    payload.meta = match::core::Json::object();
    payload.meta["timestamp"] = static_cast<std::int64_t>(std::time(nullptr));
    payload.meta["save_name"] = ctx.save_slot_display;
    payload.meta["players"] = ctx.player_names;

    match::core::Json data = match::core::Json::object();
    match::core::Json board_json = match::core::Json::object();
    board_json["cols"] = state.board.cols();
    board_json["rows"] = state.board.rows();
    board_json["tile_types"] = state.board.tileTypes();
    board_json["bombs_enabled"] = state.board.bombsEnabled();
    board_json["color_chain_enabled"] = state.board.colorChainEnabled();

    match::core::Json rows = match::core::Json::array();
    for (int r = 0; r < state.board.rows(); ++r) {
        match::core::Json row = match::core::Json::array();
        for (int c = 0; c < state.board.cols(); ++c) {
            row.push_back(state.board.get(c, r));
        }
        rows.push_back(row);
    }
    board_json["cells"] = rows;
    data["board"] = board_json;

    data["player_names"] = ctx.player_names;
    data["player_scores"] = ctx.player_scores;
    data["player_count"] =
        ctx.players_count > 0 ? ctx.players_count
                              : static_cast<int>(ctx.player_names.size());
    data["active_player"] = ctx.active_player;
    data["moves_left_per_player"] = ctx.moves_left_per_player;
    data["round_current"] = ctx.round_current;
    data["round_total"] = ctx.round_total;
    data["total_moves"] = ctx.total_moves;
    data["turn_order"] = ctx.turn_order;
    data["bombs_enabled"] = ctx.bombs_enabled;
    data["color_blast_enabled"] = ctx.color_blast_enabled;
    data["moves_per_round"] = ctx.moves_per_round_setting;
    data["status"] = ctx.status;
    data["time_mode"] = (ctx.time_mode == match::ui::TimeModeOption::Classic) ? "classic" : "blitz";
    data["blitz_turn_minutes"] = ctx.blitz_turn_minutes;
    data["blitz_between_seconds"] = ctx.blitz_between_seconds;
    data["blitz_turn_remaining_ms"] = ctx.blitz_turn_remaining_ms;
    data["blitz_turn_total_ms"] = ctx.blitz_turn_total_ms;
    data["blitz_pre_turn_ms"] = ctx.blitz_pre_turn_ms;
    data["blitz_pre_turn_active"] = ctx.blitz_pre_turn_active;
    data["blitz_turn_active"] = ctx.blitz_turn_active;
    data["game_over"] = ctx.game_over;
    if (ctx.mode == "Tournament" || g_tournament.active) {
        match::core::Json tour = match::core::Json::object();
        tour["active"] = g_tournament.active;
        tour["players"] = g_tournament.players;
        match::core::Json rounds = match::core::Json::array();
        for (const auto& round : g_tournament.rounds) {
            match::core::Json round_entry = match::core::Json::array();
            for (const auto& match : round.matches) {
                match::core::Json obj = match::core::Json::object();
                obj["player_a"] = match.player_a;
                obj["player_b"] = match.player_b;
                obj["winner"] = match.winner;
                round_entry.push_back(obj);
            }
            rounds.push_back(round_entry);
        }
        tour["rounds"] = rounds;
        tour["current_round"] = g_tournament.current_round;
        tour["current_match"] = g_tournament.current_match;
        match::core::Json active_pair = match::core::Json::array();
        active_pair.push_back(g_tournament.active_pair[0]);
        active_pair.push_back(g_tournament.active_pair[1]);
        tour["active_pair"] = active_pair;
        tour["awaiting_next_match"] = g_tournament.awaiting_next_match;
        tour["last_summary"] = g_tournament.last_summary;
        match::ui::GameSettings serialized_settings = g_tournament.base_settings;
        if (serialized_settings.player_names.empty()) {
            serialized_settings.player_names = ctx.player_names;
            serialized_settings.player_count = ctx.players_count;
            serialized_settings.mode = match::ui::GameMode::Tournament;
            serialized_settings.turn_order = ctx.turn_order_mode;
            serialized_settings.moves_per_round = ctx.moves_per_round_setting;
            serialized_settings.total_rounds = ctx.round_total;
            serialized_settings.bombs_enabled = ctx.bombs_enabled;
            serialized_settings.color_blast_enabled = ctx.color_blast_enabled;
            serialized_settings.time_mode = ctx.time_mode;
            serialized_settings.blitz_turn_minutes = ctx.blitz_turn_minutes;
            serialized_settings.blitz_between_seconds = ctx.blitz_between_seconds;
            serialized_settings.EnsureConstraints();
        }
        tour["base_settings"] = SerializeGameSettings(serialized_settings);
        data["tournament"] = tour;
    }
    payload.data = data;
    return payload;
}

bool ApplySavePayload(const match::core::SavePayload& payload,
                      BoardState& state,
                      GameContext& ctx,
                      match::ui::GameSettings& ui_settings) {
    try {
        const match::core::Json& data = payload.data;
        if (!data.contains("board")) {
            return false;
        }
        const match::core::Json& board_json = data["board"];
        match::core::Board::Rules rules;
        rules.cols = board_json.value("cols", state.board.cols());
        rules.rows = board_json.value("rows", state.board.rows());
        rules.tile_types = board_json.value("tile_types", state.board.tileTypes());
        rules.bombs_enabled = board_json.value("bombs_enabled", ctx.bombs_enabled);
        rules.color_chain_enabled = board_json.value("color_chain_enabled", ctx.color_blast_enabled);

        match::core::Board board(rules);
        if (board_json.contains("cells") && board_json["cells"].is_array()) {
            const auto& rows = board_json["cells"];
            for (int r = 0; r < rules.rows && r < static_cast<int>(rows.size()); ++r) {
                const auto& row = rows[r];
                for (int c = 0; c < rules.cols && c < static_cast<int>(row.size()); ++c) {
                    int value = row[c].get<int>();
                    board.set(c, r, value);
                }
            }
        }

        state.board = board;
        state.cascade = CascadeState{};
        state.animations.clear();
        state.hidden_cells.clear();
        state.selected.reset();
        state.hover.reset();
        state.controller_cursor.reset();

        ctx.mode = payload.mode.empty() ? ctx.mode : payload.mode;
        ctx.player_names = data.value("player_names", ctx.player_names);
        ctx.player_scores = data.value("player_scores", ctx.player_scores);
        ctx.players_count =
            data.value("player_count",
                       ctx.player_names.empty() ? ctx.players_count
                                                : static_cast<int>(ctx.player_names.size()));
        ctx.active_player = data.value("active_player", ctx.active_player);
        ctx.last_player_index = ctx.active_player;
        ctx.round_current = data.value("round_current", ctx.round_current);
        ctx.round_total = data.value("round_total", ctx.round_total);
        ctx.total_moves = data.value("total_moves", ctx.total_moves);
        ctx.turn_order = data.value("turn_order", ctx.turn_order);
        ctx.turn_order_mode = TurnOrderFromString(ctx.turn_order);
        ctx.bombs_enabled = data.value("bombs_enabled", ctx.bombs_enabled);
        ctx.color_blast_enabled = data.value("color_blast_enabled", ctx.color_blast_enabled);
        ctx.moves_per_round_setting = data.value("moves_per_round", ctx.moves_per_round_setting);
        ctx.moves_left_per_player = data.value("moves_left_per_player", ctx.moves_left_per_player);
        ctx.status = "Resumed game";
        std::string time_mode_str = data.value("time_mode", std::string("classic"));
        ctx.time_mode = (time_mode_str == "blitz") ? match::ui::TimeModeOption::Blitz
                                                   : match::ui::TimeModeOption::Classic;
        ctx.blitz_turn_minutes = data.value("blitz_turn_minutes", ctx.blitz_turn_minutes);
        ctx.blitz_between_seconds = data.value("blitz_between_seconds", ctx.blitz_between_seconds);
        ctx.blitz_turn_total_ms =
            data.value("blitz_turn_total_ms", ctx.blitz_turn_minutes * 60.0f * 1000.0f);
        ctx.blitz_turn_remaining_ms =
            data.value("blitz_turn_remaining_ms", ctx.blitz_turn_total_ms);
        ctx.blitz_pre_turn_ms = data.value("blitz_pre_turn_ms", 0.0f);
        ctx.blitz_pre_turn_active = data.value("blitz_pre_turn_active", false);
        ctx.blitz_turn_active = data.value("blitz_turn_active", false);
        ctx.game_over = data.value("game_over", ctx.game_over);
        SyncPlayerVectors(ctx);
        if (ctx.moves_left_per_player.empty()) {
            ctx.moves_left_per_player.assign(ctx.players_count, ctx.moves_per_round_setting);
        } else if (ctx.moves_left_per_player.size() < static_cast<std::size_t>(ctx.players_count)) {
            ctx.moves_left_per_player.resize(static_cast<std::size_t>(ctx.players_count),
                                             ctx.moves_per_round_setting);
        }
        SyncTurnOrderLabel(ctx);
        ctx.ai_pending = false;
        ctx.ai_timer_ms = 0.0f;
        ctx.autosave_enabled = true;
        ctx.autosave_dirty = false;
        ctx.autosave_cooldown_ms = 0.0f;
        ctx.loaded_from_save = true;

        if (payload.meta.contains("save_name") && payload.meta["save_name"].is_string()) {
            ctx.save_slot_display = payload.meta["save_name"].get<std::string>();
        }

        if (ctx.player_scores.size() != ctx.player_names.size()) {
            ctx.player_scores.assign(ctx.player_names.size(), 0);
        }

        if (ctx.mode == "PvP") {
            ui_settings.mode = match::ui::GameMode::PvP;
        } else if (ctx.mode == "Tournament") {
            ui_settings.mode = match::ui::GameMode::Tournament;
        } else {
            ui_settings.mode = match::ui::GameMode::PvC;
        }
        ui_settings.player_count = ctx.players_count;
        ui_settings.player_names = ctx.player_names;
        ui_settings.turn_order = ctx.turn_order_mode;
        ui_settings.moves_per_round = ctx.moves_per_round_setting;
        ui_settings.total_rounds = ctx.round_total;
        ui_settings.bombs_enabled = ctx.bombs_enabled;
        ui_settings.color_blast_enabled = ctx.color_blast_enabled;
        ui_settings.time_mode = ctx.time_mode;
        ui_settings.blitz_turn_minutes = ctx.blitz_turn_minutes;
        ui_settings.blitz_between_seconds = ctx.blitz_between_seconds;
        ui_settings.EnsureConstraints();

        ResetTournamentState();
        auto tour_it = data.find("tournament");
        if (tour_it != data.end() && tour_it->is_object()) {
            const auto& tour = *tour_it;
            g_tournament.active = tour.value("active", payload.mode == "Tournament");
            g_tournament.players = tour.value("players", ctx.player_names);
            g_tournament.rounds.clear();
            if (tour.contains("rounds") && tour["rounds"].is_array()) {
                for (const auto& round_entry : tour["rounds"]) {
                    if (!round_entry.is_array()) {
                        continue;
                    }
                    TournamentRound round;
                    for (const auto& match_entry : round_entry) {
                        if (!match_entry.is_object()) {
                            continue;
                        }
                        TournamentMatch match;
                        match.player_a = match_entry.value("player_a", -1);
                        match.player_b = match_entry.value("player_b", -1);
                        match.winner = match_entry.value("winner", -1);
                        round.matches.push_back(match);
                    }
                    g_tournament.rounds.push_back(std::move(round));
                }
            }
            g_tournament.current_round = tour.value("current_round", -1);
            g_tournament.current_match = tour.value("current_match", -1);
            g_tournament.active_pair = {-1, -1};
            if (tour.contains("active_pair") && tour["active_pair"].is_array()) {
                const auto& arr = tour["active_pair"];
                if (arr.size() >= 2) {
                    g_tournament.active_pair[0] = arr[0].get<int>();
                    g_tournament.active_pair[1] = arr[1].get<int>();
                }
            }
            g_tournament.awaiting_next_match = tour.value("awaiting_next_match", false);
            g_tournament.last_summary = tour.value("last_summary", std::string{});
            if (tour.contains("base_settings") && tour["base_settings"].is_object()) {
                g_tournament.base_settings =
                    DeserializeGameSettings(tour["base_settings"], ui_settings);
            } else {
                g_tournament.base_settings = ui_settings;
            }
            g_tournament.base_settings.mode = match::ui::GameMode::Tournament;
            if (g_tournament.players.empty()) {
                g_tournament.players = g_tournament.base_settings.player_names;
            }
            ui_settings = g_tournament.base_settings;
            ui_settings.mode = match::ui::GameMode::Tournament;
        } else {
            ResetTournamentState();
        }

        ctx.status = ctx.game_over ? BuildGameOverStatus(ctx) : BuildTurnStatus(ctx);
        UpdateAiPending(ctx);
        return true;
    } catch (...) {
        return false;
    }
}

bool WriteAutomaticSave(match::platform::SdlSaveService& service,
                        const BoardState& state,
                        GameContext& ctx) {
    if (!ctx.autosave_enabled || ctx.save_slot_file.empty()) {
        return false;
    }
    match::core::SavePayload payload = BuildSavePayload(state, ctx);
    std::vector<std::uint8_t> bytes = payload.SerializeBinary();
    bool ok_primary = service.Save(ctx.save_slot_file, bytes);
    if (ok_primary) {
        ctx.autosave_dirty = false;
        ctx.autosave_cooldown_ms = 0.0f;
    }
    return ok_primary;
}

bool BeginPlayerMove(BoardState& state, GameContext& ctx, const match::core::Move& move);

void EnsureControllerCursor(BoardState& state) {
    if (!state.controller_cursor) {
        match::core::Cell cell{state.board.cols() / 2, state.board.rows() / 2};
        cell.col = std::clamp(cell.col, 0, state.board.cols() - 1);
        cell.row = std::clamp(cell.row, 0, state.board.rows() - 1);
        state.controller_cursor = cell;
    }
}

void MoveControllerCursor(BoardState& state, int dc, int dr) {
    if (dc == 0 && dr == 0) {
        return;
    }
    EnsureControllerCursor(state);
    if (!state.controller_cursor) {
        return;
    }
    match::core::Cell cursor = *state.controller_cursor;
    cursor.col = std::clamp(cursor.col + dc, 0, state.board.cols() - 1);
    cursor.row = std::clamp(cursor.row + dr, 0, state.board.rows() - 1);
    state.controller_cursor = cursor;
}

void ControllerSelect(BoardState& state, GameContext& ctx) {
    if (BannerBlocksInput()) {
        ctx.status = "Please wait...";
        PlayErrorSound();
        return;
    }
    if (!ActivePlayerCanHumanAct(ctx)) {
        ctx.status = ctx.ai_pending ? "Computer is thinking..." : BuildTurnStatus(ctx);
        PlayErrorSound();
        return;
    }
    if (!state.controller_cursor) {
        return;
    }
    const match::core::Cell cursor = *state.controller_cursor;
    if (!state.selected) {
        state.selected = cursor;
        ctx.status = "Select target";
        PlayClickSound();
        return;
    }
    if (*state.selected == cursor) {
        state.selected.reset();
        ctx.status = "Select a tile";
        PlayClickSound();
        return;
    }

    const int dist =
        std::abs(state.selected->col - cursor.col) + std::abs(state.selected->row - cursor.row);
    if (dist != 1) {
        state.selected = cursor;
        ctx.status = "Select target";
        PlayClickSound();
        return;
    }

    match::core::Move move{*state.selected, cursor};
    state.selected.reset();
    if (!BeginPlayerMove(state, ctx, move)) {
        ctx.status = "Illegal swap";
        PlayErrorSound();
    }
}

void ControllerCancel(BoardState& state, GameContext& ctx) {
    if (state.selected) {
        state.selected.reset();
        ctx.status = "Select a tile";
        PlayClickSound();
    }
}

void UpdateWindowTitle(SDL_Window* window, const GameContext& ctx) {
    int total_score = 0;
    for (int score : ctx.player_scores) {
        total_score += score;
    }
    std::string active_name =
        (ctx.active_player >= 0 && ctx.active_player < static_cast<int>(ctx.player_names.size()))
            ? ctx.player_names[static_cast<std::size_t>(ctx.active_player)]
            : "Player";
    std::string title = "MATCH SDL — Moves: " + std::to_string(ctx.total_moves) +
                        " | Total Score: " + std::to_string(total_score) +
                        " | Active: " + active_name;
    SDL_SetWindowTitle(window, title.c_str());
}

void FinishCascade(BoardState& state, GameContext& ctx) {
    auto& cascade = state.cascade;
    const int previous_round = ctx.round_current;
    const int previous_player = ctx.active_player;
    const bool previous_game_over = ctx.game_over;
    state.board = cascade.final_board;
    state.animations.clear();
    state.hidden_cells.clear();
    cascade.active = false;
    cascade.phase = CascadeState::Phase::Idle;
    cascade.chains.clear();
    if (!ctx.player_scores.empty()) {
        ctx.player_scores[static_cast<std::size_t>(
            std::clamp(ctx.active_player, 0,
                       static_cast<int>(ctx.player_scores.size()) - 1))] +=
            cascade.result.score;
    }

    SyncPlayerVectors(ctx);
    if (ctx.active_player < 0 ||
        ctx.active_player >= static_cast<int>(ctx.moves_left_per_player.size())) {
        ctx.active_player = 0;
    }
    if (!ctx.moves_left_per_player.empty()) {
        auto& moves = ctx.moves_left_per_player[static_cast<std::size_t>(ctx.active_player)];
        moves = std::max(0, moves - 1);
    }

    bool round_finished = RoundComplete(ctx);
    if (round_finished) {
        ctx.round_current += 1;
        if (ctx.round_current > ctx.round_total) {
            ctx.game_over = true;
            ctx.status = BuildGameOverStatus(ctx);
        } else {
            ResetMovesForNewRound(ctx);
            ctx.active_player = 0;
            ctx.status = BuildTurnStatus(ctx);
        }
    } else {
        bool stay_on_player = (ctx.turn_order_mode == match::ui::TurnOrderOption::Consecutive &&
                               ctx.active_player >= 0 &&
                               ctx.active_player <
                                   static_cast<int>(ctx.moves_left_per_player.size()) &&
                               ctx.moves_left_per_player[static_cast<std::size_t>(ctx.active_player)] >
                                   0);
        if (!stay_on_player) {
            int next = NextPlayerWithMoves(ctx, ctx.active_player);
            if (next >= 0) {
                ctx.active_player = next;
            }
        }
        if (!ctx.game_over) {
            ctx.status = BuildTurnStatus(ctx);
        }
    }

    UpdateAiPending(ctx);

    if (ctx.autosave_enabled) {
        ctx.autosave_dirty = true;
    }

    HandleStateFeedback(ctx, previous_round, previous_player, previous_game_over);
}

void StartCascadePop(BoardState& state) {
    auto& cascade = state.cascade;
    const Layout& layout = state.layout;
    if (cascade.chain_index >= cascade.chains.size()) {
        return;
    }

    const auto& chain = cascade.chains[cascade.chain_index];
    state.animations.clear();
    state.hidden_cells.clear();

    bool has_clear = false;
    bool triggered_bomb = false;
    for (const auto& evt : chain.clears) {
        if (!evt.cells.empty()) {
            has_clear = true;
        }
        if (evt.via_bomb) {
            triggered_bomb = true;
        }
    }
    if (has_clear) {
        PlayMatchSound(cascade.chain_index > 0);
        TriggerRumble(triggered_bomb ? 0.85f : 0.6f,
                      static_cast<Uint32>(triggered_bomb ? 320 : 200));
    }
    if (triggered_bomb) {
        PlayBombSound();
    }

    std::set<match::core::Cell> unique;
    for (const auto& evt : chain.clears) {
        for (const auto& cell : evt.cells) {
            if (!unique.insert(cell.position).second) {
                continue;
            }
            state.hidden_cells.insert(cell.position);
            cascade.working_board.set(cell.position.col, cell.position.row,
                                      match::core::kEmptyCell);
            state.animations.push_back(
                MakePopAnimation(layout, cell.position, cell.tile, kPopDurationMs));
        }
    }

    cascade.phase = CascadeState::Phase::Pop;
    state.board = cascade.working_board;
}
bool StartCascadeFall(BoardState& state) {
    auto& cascade = state.cascade;
    const Layout& layout = state.layout;
    if (cascade.chain_index >= cascade.chains.size()) {
        return false;
    }

    const auto& chain = cascade.chains[cascade.chain_index];
    state.animations.clear();
    state.hidden_cells.clear();

    bool has_animation = false;
    for (const auto& fall : chain.falls) {
        cascade.working_board.set(fall.from.col, fall.from.row, match::core::kEmptyCell);
        cascade.working_board.set(fall.to.col, fall.to.row, fall.tile);
        state.hidden_cells.insert(fall.to);
        const int distance = std::abs(fall.to.row - fall.from.row);
        const float duration =
            std::max(kFallDurationMinMs, distance * kFallDurationPerCellMs);
        state.animations.push_back(
            MakeFallAnimation(layout, fall.from, fall.to, fall.tile, duration));
        has_animation = true;
    }

    std::map<int, std::vector<const match::core::SimulationResult::SpawnEvent*>> by_column;
    for (const auto& spawn : chain.spawns) {
        by_column[spawn.position.col].push_back(&spawn);
        cascade.working_board.set(spawn.position.col, spawn.position.row, spawn.tile);
        state.hidden_cells.insert(spawn.position);
    }

    for (auto& [col, spawns] : by_column) {
        std::sort(spawns.begin(), spawns.end(),
                  [](const auto* a, const auto* b) { return a->position.row < b->position.row; });
        const int total = static_cast<int>(spawns.size());
        for (int idx = 0; idx < total; ++idx) {
            const auto* spawn = spawns[static_cast<std::size_t>(idx)];
            const int distance_cells = std::max(1, total - idx);
            const float duration =
                std::max(kFallDurationMinMs, distance_cells * kFallDurationPerCellMs);
            state.animations.push_back(
                MakeSpawnAnimation(layout, spawn->position, spawn->tile, distance_cells, duration));
            has_animation = true;
        }
    }

    cascade.phase = CascadeState::Phase::Fall;
    if (!has_animation) {
        state.hidden_cells.clear();
    }
    state.board = cascade.working_board;
    return has_animation;
}

void AdvanceCascade(BoardState& state, GameContext& ctx) {
    auto& cascade = state.cascade;
    if (!cascade.active) {
        return;
    }
    if (!state.animations.empty()) {
        return;
    }

    switch (cascade.phase) {
        case CascadeState::Phase::Swap: {
            state.board.swapCells(cascade.move);
            cascade.working_board = state.board;
            if (!cascade.chains.empty()) {
                cascade.chain_index = 0;
                StartCascadePop(state);
            } else {
                cascade.final_board = state.board;
                FinishCascade(state, ctx);
            }
            break;
        }
        case CascadeState::Phase::Pop: {
            if (!StartCascadeFall(state)) {
                cascade.chain_index++;
                if (cascade.chain_index < cascade.chains.size()) {
                    StartCascadePop(state);
                } else {
                    FinishCascade(state, ctx);
                }
            }
            break;
        }
        case CascadeState::Phase::Fall: {
            cascade.chain_index++;
            if (cascade.chain_index < cascade.chains.size()) {
                StartCascadePop(state);
            } else {
                FinishCascade(state, ctx);
            }
            break;
        }
        case CascadeState::Phase::Idle:
            cascade.active = false;
            break;
    }
}

bool BeginPlayerMove(BoardState& state, GameContext& ctx, const match::core::Move& move) {
    if (ctx.game_over) {
        ctx.status = BuildGameOverStatus(ctx);
        PlayErrorSound();
        return false;
    }
    if (BannerBlocksInput()) {
        ctx.status = "Please wait...";
        PlayErrorSound();
        return false;
    }
    SyncPlayerVectors(ctx);
    if (ctx.active_player < 0 ||
        ctx.active_player >= static_cast<int>(ctx.moves_left_per_player.size())) {
        ctx.active_player = 0;
    }
    if (ctx.moves_left_per_player.empty() ||
        ctx.moves_left_per_player[static_cast<std::size_t>(ctx.active_player)] <= 0) {
        ctx.status = "No moves left this round";
        PlayErrorSound();
        return false;
    }

    match::core::Board validation_board = state.board;
    if (!match::core::LegalSwap(validation_board, move)) {
        ctx.status = "Illegal swap";
        PlayErrorSound();
        return false;
    }

    match::core::Board simulation_board = state.board;
    match::core::SimulationResult result = match::core::SimulateFullChain(simulation_board, move);
    if (result.chain_events.empty()) {
        ctx.status = "No match";
        PlayErrorSound();
        return false;
    }

    ctx.total_moves += 1;
    ctx.status = "Resolving...";

    state.cascade.active = true;
    state.cascade.phase = CascadeState::Phase::Swap;
    state.cascade.chains = result.chain_events;
    state.cascade.chain_index = 0;
    state.cascade.working_board = state.board;
    state.cascade.final_board = simulation_board;
    state.cascade.result = result;
    state.cascade.move = move;

    state.animations.clear();
    state.hidden_cells.clear();
    state.hidden_cells.insert(move.a);
    state.hidden_cells.insert(move.b);

    auto anim_ab =
        MakeSwapAnimation(state.layout, move.a, move.b, state.board.get(move.a.col, move.a.row), kSwapDurationMs);
    anim_ab.reveal_cell = move.b;
    state.animations.push_back(anim_ab);
    auto anim_ba =
        MakeSwapAnimation(state.layout, move.b, move.a, state.board.get(move.b.col, move.b.row), kSwapDurationMs);
    anim_ba.reveal_cell = move.a;
    state.animations.push_back(anim_ba);
    PlaySwapSound();
    TriggerRumble(0.5f, 180);
    return true;
}

void UpdateHoverCell(BoardState& state, int mouse_x, int mouse_y) {
    match::core::Cell cell;
    if (CellFromPoint(state.layout, state.board.cols(), state.board.rows(), mouse_x, mouse_y, cell)) {
        state.hover = cell;
    } else {
        state.hover.reset();
    }
}

bool HandleMouseDown(BoardState& state, GameContext& ctx, int mouse_x, int mouse_y) {
    if (BannerBlocksInput()) {
        ctx.status = "Please wait...";
        PlayErrorSound();
        return false;
    }
    if (!ActivePlayerCanHumanAct(ctx)) {
        ctx.status = ctx.ai_pending ? "Computer is thinking..." : BuildTurnStatus(ctx);
        PlayErrorSound();
        return false;
    }
    UpdateHoverCell(state, mouse_x, mouse_y);

    match::core::Cell clicked;
    if (!CellFromPoint(state.layout, state.board.cols(), state.board.rows(), mouse_x, mouse_y, clicked)) {
        state.selected.reset();
        return false;
    }
    if (!state.selected) {
        state.selected = clicked;
        ctx.status = "Select target";
        PlayClickSound();
        return false;
    }

    if (*state.selected == clicked) {
        state.selected.reset();
        ctx.status = "Select a tile";
        PlayClickSound();
        return false;
    }

    const int dist =
        std::abs(state.selected->col - clicked.col) + std::abs(state.selected->row - clicked.row);
    if (dist != 1) {
        state.selected = clicked;
        ctx.status = "Select target";
        PlayClickSound();
        return false;
    }

    match::core::Move move{*state.selected, clicked};
    state.selected.reset();
    return BeginPlayerMove(state, ctx, move);
}

}  // namespace

int main(int /*argc*/, char* /*argv*/[]) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");

    const int img_flags = IMG_INIT_PNG;
    int img_result = IMG_Init(img_flags);
    bool img_ready = (img_result & img_flags) == img_flags;
    if (!img_ready) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "IMG_Init failed: %s", IMG_GetError());
    }

    if (TTF_Init() != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TTF_Init failed: %s", TTF_GetError());
        if (img_result != 0) {
            IMG_Quit();
        }
        SDL_Quit();
        return 1;
    }

    AudioSystem audio;
    bool audio_ready = audio.Initialize();
    if (!audio_ready) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Audio disabled: %s", Mix_GetError());
    } else {
        g_audio = &audio;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");

    SDL_Rect usable_bounds;
    if (SDL_GetDisplayUsableBounds(0, &usable_bounds) != 0) {
        usable_bounds = SDL_Rect{0, 0, kWindowWidth, kWindowHeight};
    }
    const int base_width = std::min(usable_bounds.w, kWindowWidth);
    const int base_height = std::min(usable_bounds.h, kWindowHeight);

    Uint32 window_flags =
        SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE;
    SDL_Window* window =
        SDL_CreateWindow("MATCH SDL", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                         base_width, base_height, window_flags);
    if (!window) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateWindow failed: %s", SDL_GetError());
        if (audio_ready) {
            audio.Shutdown();
            g_audio = nullptr;
        }
        if (img_result != 0) {
            IMG_Quit();
        }
        SDL_Quit();
        return 1;
    }
    if (img_ready) {
        ApplyWindowIcon(window);
    }
    const int min_window_width = std::min(base_width, 1280);
    const int min_window_height = std::min(base_height, 720);
    SDL_SetWindowMinimumSize(window, min_window_width, min_window_height);
    SDL_SetWindowSize(window, base_width, base_height);
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_MaximizeWindow(window);

    WindowModeState window_mode;
    window_mode.fullscreen = false;
    window_mode.want_maximized = true;
    int init_w = 0;
    int init_h = 0;
    SDL_GetWindowSize(window, &init_w, &init_h);
    if (init_w <= 0) {
        init_w = base_width;
    }
    if (init_h <= 0) {
        init_h = base_height;
    }
    window_mode.windowed_width = std::max(min_window_width, init_w);
    window_mode.windowed_height = std::max(min_window_height, init_h);

    SDL_Renderer* renderer =
        SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateRenderer failed: %s", SDL_GetError());
        if (audio_ready) {
            audio.Shutdown();
            g_audio = nullptr;
        }
        if (img_result != 0) {
            IMG_Quit();
        }
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    auto apply_window_mode = [&](bool fullscreen) -> bool {
        if (fullscreen == window_mode.fullscreen) {
            return true;
        }
        if (fullscreen) {
            int cur_w = 0;
            int cur_h = 0;
            int cur_x = 0;
            int cur_y = 0;
            SDL_GetWindowSize(window, &cur_w, &cur_h);
            SDL_GetWindowPosition(window, &cur_x, &cur_y);
            Uint32 flags = SDL_GetWindowFlags(window);
            if (!(flags & SDL_WINDOW_MAXIMIZED)) {
                if (cur_w > 0 && cur_h > 0) {
                    window_mode.windowed_width = std::max(min_window_width, cur_w);
                    window_mode.windowed_height = std::max(min_window_height, cur_h);
                }
                if (cur_x > -32000 && cur_y > -32000) {
                    window_mode.windowed_x = cur_x;
                    window_mode.windowed_y = cur_y;
                }
            }
            if (SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN) != 0) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "Failed to enter fullscreen: %s",
                            SDL_GetError());
                return false;
            }
            window_mode.fullscreen = true;
        } else {
        if (SDL_SetWindowFullscreen(window, 0) != 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Failed to exit fullscreen: %s",
                        SDL_GetError());
            return false;
        }
        SDL_RestoreWindow(window);
        if (window_mode.want_maximized) {
            SDL_MaximizeWindow(window);
        } else {
            int target_w = std::clamp(window_mode.windowed_width, min_window_width, base_width);
            int target_h = std::clamp(window_mode.windowed_height, min_window_height, base_height);
            SDL_SetWindowSize(window, target_w, target_h);
            if (window_mode.windowed_x > -32000 && window_mode.windowed_y > -32000) {
                SDL_SetWindowPosition(window, window_mode.windowed_x, window_mode.windowed_y);
            } else {
                SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
            }
        }
        window_mode.fullscreen = false;
        }
        return true;
    };
    SDL_ShowCursor(SDL_ENABLE);
    float font_scale = ComputeUiScale(base_width, base_height);
    Fonts fonts = LoadFonts(font_scale);
    if (!fonts.heading || !fonts.body || !fonts.small) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Unable to load required fonts.");
        DestroyFonts(fonts);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        if (audio_ready) {
            audio.Shutdown();
            g_audio = nullptr;
        }
        if (img_result != 0) {
            IMG_Quit();
        }
        TTF_Quit();
        SDL_Quit();
        return 1;
    }
    IntroState intro_state;
    intro_state.min_duration_ms = kIntroMinimumDurationMs;
    bool music_started = false;
    if (audio_ready) {
        float intro_audio_ms = audio.PlayIntro();
        if (intro_audio_ms > 0.0f) {
            intro_state.min_duration_ms = std::max(intro_state.min_duration_ms, intro_audio_ms);
        }
    }
    if (img_ready) {
        TryLoadIntroLogo(renderer, intro_state);
    }

    match::ui::GameSettings ui_settings;
    ui_settings.EnsureConstraints();

    std::filesystem::path base_save_root = DetermineStandaloneSaveRoot();
    match::platform::SdlSaveService save_service(base_save_root / ModeFolder(ui_settings.mode));
    save_service.Initialize();
    bool save_slots_dirty = true;

    BoardState board_state;

    GameContext game_ctx;
    game_ctx.status = "Select a tile";
    match::ui::MenuState menu_state;
    match::ui::SaveSetupState save_setup_state;
    match::ui::SaveDetailState save_detail_state;
    PauseMenuState pause_menu_state;
    match::ui::TournamentBracketState tournament_bracket_state;
    bool pause_menu_active = false;
    bool text_input_active = false;
    auto start_text_input = [&]() {
        if (!text_input_active) {
            SDL_StartTextInput();
            text_input_active = true;
        }
    };
    auto stop_text_input = [&]() {
        if (text_input_active) {
            SDL_StopTextInput();
            text_input_active = false;
        }
    };
    match::ui::NamePromptState name_prompt_state;
    match::ui::TimeModeState time_mode_state;
    match::ui::BlitzSettingsState blitz_state;
    match::ui::SettingsState settings_state(ui_settings);
    match::ui::DisplaySettingsState display_settings_state;
    match::ui::OskState osk_state;
    auto deactivate_osk = [&]() {
        if (osk_state.text_input_mode) {
            stop_text_input();
            osk_state.text_input_mode = false;
        }
        osk_state.active = false;
    };
    std::string pending_save_display;
    std::string pending_save_filename;

    enum class SaveSetupIntent { ConfigureSettings, LaunchGameplay };

    enum class AppScreen {
        Intro,
        MainMenu,
        SaveSetup,
        SaveDetail,
        NamePrompt,
        TimeMode,
        BlitzSettings,
        Settings,
        GameSettings,
        TournamentBracket,
        Gameplay
    };
    AppScreen current_screen = AppScreen::Intro;
    SaveSetupIntent save_setup_intent = SaveSetupIntent::ConfigureSettings;
    AppScreen save_setup_cancel_target = AppScreen::MainMenu;
    AppScreen game_settings_return_screen = AppScreen::MainMenu;
    AppScreen name_prompt_cancel_target = AppScreen::MainMenu;

    InputMode last_input_mode = InputMode::MouseKeyboard;

    SdlInput input;
    if (!input.Initialize()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SdlInput initialization failed: %s", SDL_GetError());
    }
    g_input = &input;

    const int initial_panel_px =
        static_cast<int>(std::lround(420.0f * font_scale));
    const int initial_margin_px =
        static_cast<int>(std::lround(60.0f * font_scale));
    board_state.layout = ComputeLayout(base_width, base_height, board_state.board.cols(),
                                       board_state.board.rows(), initial_panel_px,
                                       initial_margin_px);

    bool running = true;
    Uint64 last_counter = SDL_GetPerformanceCounter();
    const double frequency = static_cast<double>(SDL_GetPerformanceFrequency());

    auto mark_mouse_keyboard = [&]() {
        last_input_mode = InputMode::MouseKeyboard;
        SDL_ShowCursor(SDL_ENABLE);
        if (current_screen == AppScreen::Gameplay) {
            game_ctx.last_input = InputMode::MouseKeyboard;
            board_state.controller_cursor.reset();
            board_state.controller_axis_horizontal = 0;
            board_state.controller_axis_vertical = 0;
        }
    };
    auto mark_controller = [&]() {
        last_input_mode = InputMode::Controller;
        SDL_ShowCursor(SDL_DISABLE);
        if (current_screen == AppScreen::Gameplay) {
            game_ctx.last_input = InputMode::Controller;
            board_state.hover.reset();
            EnsureControllerCursor(board_state);
            board_state.controller_axis_horizontal = 0;
            board_state.controller_axis_vertical = 0;
        }
    };
    auto set_window_title = [&](const std::string& text) { SDL_SetWindowTitle(window, text.c_str()); };
    auto set_main_menu_title = [&]() { set_window_title("MATCH — Main Menu"); };
    auto set_save_setup_title = [&]() { set_window_title("MATCH — Saves"); };
    auto set_save_detail_title = [&]() { set_window_title("MATCH — Save Details"); };
    auto set_name_prompt_title = [&]() { set_window_title("MATCH — Save Name"); };
    auto set_time_mode_title = [&]() { set_window_title("MATCH — Time Mode"); };
    auto set_blitz_settings_title = [&]() { set_window_title("MATCH — Blitz Settings"); };
    auto set_game_settings_title = [&]() { set_window_title("MATCH — Game Settings"); };
    auto set_tournament_bracket_title = [&]() { set_window_title("MATCH — Tournament Bracket"); };

    if (!intro_state.logo_texture) {
        intro_state.active = false;
    }
    if (!intro_state.active) {
        current_screen = AppScreen::MainMenu;
        set_main_menu_title();
    }
    auto start_new_game = [&]() {
        match::core::Board::Rules rules;
        rules.cols = kBoardCols;
        rules.rows = kBoardRows;
        rules.tile_types = 6;
        rules.bombs_enabled = ui_settings.bombs_enabled;
        rules.color_chain_enabled = ui_settings.color_blast_enabled;

        match::core::Board new_board = match::core::NewBoard(rules, std::random_device{}());
        board_state = BoardState{};
        board_state.board = new_board;
        board_state.cascade.working_board = new_board;
        board_state.cascade.final_board = new_board;

        ui_settings.EnsureConstraints();

        game_ctx = GameContext{};
        if (ui_settings.mode == match::ui::GameMode::PvC) {
            game_ctx.mode = "PvC";
        } else if (ui_settings.mode == match::ui::GameMode::PvP) {
            game_ctx.mode = "PvP";
        } else {
            game_ctx.mode = "Tournament";
        }
        game_ctx.time_mode = ui_settings.time_mode;
        game_ctx.blitz_turn_minutes = ui_settings.blitz_turn_minutes;
        game_ctx.blitz_between_seconds = ui_settings.blitz_between_seconds;
        game_ctx.round_current = 1;
        game_ctx.round_total = ui_settings.total_rounds;
        game_ctx.turn_order_mode = ui_settings.turn_order;
        SyncTurnOrderLabel(game_ctx);
        game_ctx.bombs_enabled = ui_settings.bombs_enabled;
        game_ctx.color_blast_enabled = ui_settings.color_blast_enabled;
        game_ctx.moves_per_round_setting = ui_settings.moves_per_round;
        game_ctx.player_names = ui_settings.player_names;
        game_ctx.players_count = static_cast<int>(game_ctx.player_names.size());
        game_ctx.player_scores.assign(game_ctx.players_count, 0);
        game_ctx.moves_left_per_player.assign(game_ctx.players_count, ui_settings.moves_per_round);
        SyncPlayerVectors(game_ctx);
        RandomizePlayerOrder(game_ctx);
        ResetMovesForNewRound(game_ctx);
        game_ctx.total_moves = 0;
        game_ctx.game_over = false;
        game_ctx.ai_pending = false;
        game_ctx.ai_timer_ms = 0.0f;
        game_ctx.status = BuildTurnStatus(game_ctx);
        game_ctx.last_input = InputMode::MouseKeyboard;
        game_ctx.save_slot_display = pending_save_display;
        game_ctx.save_slot_file = pending_save_filename;
        game_ctx.autosave_enabled = !game_ctx.save_slot_file.empty();
        game_ctx.autosave_dirty = true;
        game_ctx.autosave_cooldown_ms = 0.0f;
        game_ctx.loaded_from_save = false;
        game_ctx.blitz_turn_total_ms = ui_settings.blitz_turn_minutes * 60.0f * 1000.0f;
        game_ctx.blitz_turn_remaining_ms = game_ctx.blitz_turn_total_ms;
        game_ctx.blitz_pre_turn_ms = 0.0f;
        game_ctx.blitz_pre_turn_last_second = -1;
        game_ctx.blitz_pre_turn_active = false;
        game_ctx.blitz_turn_active = false;
        game_ctx.last_player_index = -1;

        board_state.selected.reset();
        board_state.hover.reset();
        board_state.controller_cursor.reset();
        board_state.controller_axis_horizontal = 0;
        board_state.controller_axis_vertical = 0;
        board_state.animations.clear();
        board_state.hidden_cells.clear();

        pause_menu_active = false;
        current_screen = AppScreen::Gameplay;
        game_settings_return_screen = AppScreen::MainMenu;
        last_input_mode = InputMode::MouseKeyboard;
        SDL_ShowCursor(SDL_ENABLE);
        deactivate_osk();
        UpdateWindowTitle(window, game_ctx);
        UpdateAiPending(game_ctx);
        g_win_announced = false;
        ResetBannerOverlay();
        AnnounceRoundStart(game_ctx);
        if (game_ctx.time_mode == match::ui::TimeModeOption::Blitz) {
            StartBlitzTurn(game_ctx, true);
        }
        save_slots_dirty = true;
    };

    auto start_next_tournament_match = [&]() -> bool {
        if (!g_tournament.active) {
            return false;
        }
        auto next = NextReadyMatch(g_tournament);
        if (!next.has_value()) {
            return false;
        }
        int round_idx = next->first;
        int match_idx = next->second;
        if (round_idx < 0 || match_idx < 0 ||
            round_idx >= static_cast<int>(g_tournament.rounds.size())) {
            return false;
        }
        auto& match = g_tournament.rounds[static_cast<std::size_t>(round_idx)].matches[static_cast<std::size_t>(match_idx)];
        if (match.player_a < 0 || match.player_b < 0 ||
            match.player_a >= static_cast<int>(g_tournament.players.size()) ||
            match.player_b >= static_cast<int>(g_tournament.players.size())) {
            return false;
        }
        g_tournament.current_round = round_idx;
        g_tournament.current_match = match_idx;
        g_tournament.active_pair = {match.player_a, match.player_b};
        ui_settings = g_tournament.base_settings;
        ui_settings.mode = match::ui::GameMode::Tournament;
        ui_settings.player_count = 2;
        ui_settings.player_names = {g_tournament.players[static_cast<std::size_t>(match.player_a)],
                                    g_tournament.players[static_cast<std::size_t>(match.player_b)]};
        start_new_game();
        std::string label = "Round " + std::to_string(round_idx + 1) + "/" +
                            std::to_string(std::max<int>(1, static_cast<int>(g_tournament.rounds.size()))) + ": " +
                            ui_settings.player_names[0] + " vs " + ui_settings.player_names[1];
        game_ctx.status = label;
        g_tournament.last_summary = TournamentStatusSummary(g_tournament);
        g_tournament.awaiting_next_match = false;
        return true;
    };

    auto begin_tournament_series = [&]() {
        ui_settings.EnsureConstraints();
        ResetTournamentState();
        g_tournament.active = true;
        g_tournament.base_settings = ui_settings;
        g_tournament.base_settings.mode = match::ui::GameMode::Tournament;
        g_tournament.players = ui_settings.player_names;
        if (g_tournament.players.size() < 2) {
            ResetTournamentState();
            return;
        }
        BuildTournamentBracket(g_tournament);
        g_tournament.last_summary = TournamentStatusSummary(g_tournament);
        g_tournament.awaiting_next_match = true;
        tournament_bracket_state.selected_button = 0;
        tournament_bracket_state.start_enabled = TournamentHasReadyMatch(g_tournament);
        SDL_ShowCursor(SDL_ENABLE);
        current_screen = AppScreen::TournamentBracket;
        set_tournament_bracket_title();
    };

    auto finish_tournament_series = [&]() {
        if (!g_tournament.active) {
            return;
        }
        match::ui::GameSettings restored_settings = g_tournament.base_settings;
        std::string subtitle = TournamentWinnersSummary();
        float sound_ms = PlayWinSound();
        ShowBannerMessage("Tournament Complete", subtitle, std::max(4200.0f, sound_ms + 500.0f), true, true);
        ResetTournamentState();
        ui_settings = restored_settings;
        current_screen = AppScreen::MainMenu;
        set_main_menu_title();
        pause_menu_active = false;
        SDL_ShowCursor(SDL_ENABLE);
    };

    auto finish_match = [&]() {
        pause_menu_active = false;
        ResetBannerOverlay();
        g_win_announced = false;
        if (game_ctx.mode == "Tournament" || g_tournament.active) {
            if (TournamentHasReadyMatch(g_tournament)) {
                g_tournament.awaiting_next_match = true;
                tournament_bracket_state.selected_button = 0;
                tournament_bracket_state.start_enabled = TournamentHasReadyMatch(g_tournament);
                current_screen = AppScreen::TournamentBracket;
                set_tournament_bracket_title();
                SDL_ShowCursor(SDL_ENABLE);
            } else {
                finish_tournament_series();
            }
        } else {
            current_screen = AppScreen::MainMenu;
            set_main_menu_title();
            SDL_ShowCursor(SDL_ENABLE);
        }
    };

    if (!intro_state.active) {
        set_main_menu_title();
        current_screen = AppScreen::MainMenu;
    }

    auto reinitialize_save_service = [&](match::ui::GameMode mode) {
        std::filesystem::path target = base_save_root / ModeFolder(mode);
        save_service = match::platform::SdlSaveService(target);
        save_service.Initialize();
        save_slots_dirty = true;
    };

    auto refresh_save_slots = [&]() {
        if (!save_slots_dirty) {
            return;
        }
        save_setup_state.slots = save_service.ListSlots();
        SortSlotsNewestFirst(save_setup_state.slots);
        save_setup_state.summaries.clear();
        for (const auto& slot : save_setup_state.slots) {
            save_setup_state.summaries.push_back(BuildSaveSummary(save_service, slot));
        }
        save_slots_dirty = false;
    };

    auto load_save_slot = [&](const match::platform::SaveSlotInfo& slot, bool use_controller) -> bool {
        std::vector<std::uint8_t> bytes;
        if (!save_service.Load(slot.name, bytes)) {
            save_setup_state.error = "Failed to load slot";
            return false;
        }
        match::core::SavePayload payload;
        try {
            payload = match::core::SavePayload::DeserializeBinary(bytes);
        } catch (const std::exception& ex) {
            save_setup_state.error = std::string("Corrupt save: ") + ex.what();
            return false;
        }
        if (!ApplySavePayload(payload, board_state, game_ctx, ui_settings)) {
            save_setup_state.error = "Unable to apply save";
            return false;
        }
        settings_state.RefreshEntries();
        pending_save_display = game_ctx.save_slot_display.empty() ? slot.name : game_ctx.save_slot_display;
        pending_save_filename = slot.name;
        game_ctx.save_slot_display = pending_save_display;
        game_ctx.save_slot_file = pending_save_filename;
        game_ctx.autosave_enabled = true;
        game_ctx.autosave_dirty = false;
        game_ctx.status = "Resumed game";
        board_state.animations.clear();
        board_state.hidden_cells.clear();
        board_state.cascade.active = false;
        board_state.cascade.phase = CascadeState::Phase::Idle;
        deactivate_osk();
        g_win_announced = game_ctx.game_over;
        ResetBannerOverlay();
        save_setup_intent = SaveSetupIntent::ConfigureSettings;
        save_setup_cancel_target = AppScreen::MainMenu;
        last_input_mode = use_controller ? InputMode::Controller : InputMode::MouseKeyboard;
        SDL_ShowCursor(use_controller ? SDL_DISABLE : SDL_ENABLE);
        current_screen = AppScreen::Gameplay;
        if (g_tournament.active && g_tournament.awaiting_next_match) {
            current_screen = AppScreen::TournamentBracket;
            set_tournament_bracket_title();
            tournament_bracket_state.selected_button = 0;
            tournament_bracket_state.start_enabled = TournamentHasReadyMatch(g_tournament);
            SDL_ShowCursor(SDL_ENABLE);
        } else {
            UpdateWindowTitle(window, game_ctx);
        }
        save_slots_dirty = true;
        pause_menu_active = false;
        save_setup_state.error.clear();
        return true;
    };

    auto delete_save_slot = [&](const match::platform::SaveSlotInfo& slot) -> bool {
        if (!save_service.Delete(slot.name)) {
            save_setup_state.error = "Failed to delete save";
            return false;
        }
        std::error_code ec;
        auto parent = slot.path.parent_path();
        if (!parent.empty()) {
            bool empty = std::filesystem::is_empty(parent, ec);
            if (!ec && empty) {
                std::filesystem::remove(parent, ec);
            }
        }
        save_slots_dirty = true;
        refresh_save_slots();
        save_setup_state.summaries.clear();
        for (const auto& entry : save_setup_state.slots) {
            save_setup_state.summaries.push_back(BuildSaveSummary(save_service, entry));
        }
        save_setup_state.selected_index =
            std::clamp(save_setup_state.selected_index, 0, static_cast<int>(save_setup_state.slots.size()));
        save_setup_state.error.clear();
        return true;
    };

    auto show_save_detail = [&](int selected_index, bool use_controller) {
        save_detail_state = match::ui::SaveDetailState{};
        if (selected_index <= 0) {
            save_detail_state.is_new = true;
            save_detail_state.summary.title = "Start New Game";
            save_detail_state.info_lines = {
                "Mode: " + match::ui::ModeToString(ui_settings.mode),
                std::string("Time: ") +
                    ((ui_settings.time_mode == match::ui::TimeModeOption::Classic) ? "Classic" : "Blitz"),
                std::string("Bombs: ") + (ui_settings.bombs_enabled ? "Enabled" : "Disabled"),
                std::string("Color blast: ") + (ui_settings.color_blast_enabled ? "Enabled" : "Disabled")};
            save_detail_state.buttons = {"Create", "Cancel"};
            save_detail_state.button_actions = {match::ui::SaveDetailAction::Confirm,
                                                match::ui::SaveDetailAction::Cancel};
        } else {
            int slot_index = selected_index - 1;
            if (slot_index < 0 || slot_index >= static_cast<int>(save_setup_state.slots.size())) {
                return;
            }
            save_detail_state.is_new = false;
            save_detail_state.slot = save_setup_state.slots[static_cast<std::size_t>(slot_index)];
            save_detail_state.summary = save_setup_state.summaries[static_cast<std::size_t>(slot_index)];
            save_detail_state.info_lines = save_detail_state.summary.detail_lines;
            if (!save_detail_state.summary.error.empty()) {
                save_detail_state.info_lines.push_back(save_detail_state.summary.error);
            }
            save_detail_state.buttons = {"Load", "Delete", "Cancel"};
            save_detail_state.button_actions = {match::ui::SaveDetailAction::Confirm,
                                                match::ui::SaveDetailAction::Delete,
                                                match::ui::SaveDetailAction::Cancel};
        }
        save_detail_state.selected_button = 0;
        last_input_mode = use_controller ? InputMode::Controller : InputMode::MouseKeyboard;
        SDL_ShowCursor(use_controller ? SDL_DISABLE : SDL_ENABLE);
        current_screen = AppScreen::SaveDetail;
        set_save_detail_title();
    };

    auto begin_name_prompt = [&](bool use_controller, AppScreen cancel_to) {
        std::string suggested = pending_save_display;
        if (suggested.empty()) {
            suggested = GenerateDefaultSaveName(ui_settings.mode, save_setup_state.slots);
        }
        name_prompt_state.input = suggested;
        name_prompt_state.error.clear();
        pending_save_display.clear();
        pending_save_filename.clear();
        name_prompt_cancel_target = cancel_to;
        current_screen = AppScreen::NamePrompt;
        set_name_prompt_title();
        if (use_controller) {
            match::ui::BeginOsk(osk_state, "Save Name", name_prompt_state.input, 16);
        } else {
            deactivate_osk();
            start_text_input();
        }
    };

    auto prepare_save_setup = [&](match::ui::GameMode mode,
                                  bool use_controller,
                                  AppScreen cancel_target,
                                  SaveSetupIntent intent) {
        save_setup_state.mode = mode;
        reinitialize_save_service(mode);
        refresh_save_slots();
        save_setup_state.selected_index = 0;
        save_setup_state.first_visible = 0;
        save_setup_state.error.clear();
        save_setup_state.entry_bounds.clear();
        save_setup_state.summaries.clear();
        for (const auto& slot : save_setup_state.slots) {
            save_setup_state.summaries.push_back(BuildSaveSummary(save_service, slot));
        }
        save_setup_cancel_target = cancel_target;
        save_setup_intent = intent;
        deactivate_osk();
        stop_text_input();

        if (save_setup_state.slots.empty()) {
            begin_name_prompt(use_controller, cancel_target);
            return;
        }

        current_screen = AppScreen::SaveSetup;
        set_save_setup_title();
    };
    while (running) {
        int current_w = 0;
        int current_h = 0;
        SDL_GetWindowSize(window, &current_w, &current_h);
        if (!window_mode.fullscreen) {
            Uint32 window_flags = SDL_GetWindowFlags(window);
            window_mode.want_maximized = (window_flags & SDL_WINDOW_MAXIMIZED) != 0;
            if (!window_mode.want_maximized && current_w > 0 && current_h > 0) {
                window_mode.windowed_width = std::max(min_window_width, current_w);
                window_mode.windowed_height = std::max(min_window_height, current_h);
                int pos_x = 0;
                int pos_y = 0;
                SDL_GetWindowPosition(window, &pos_x, &pos_y);
                if (pos_x > -32000 && pos_y > -32000) {
                    window_mode.windowed_x = pos_x;
                    window_mode.windowed_y = pos_y;
                }
            }
        }

        const float desired_font_scale = ComputeUiScale(current_w, current_h);
        if (std::abs(desired_font_scale - font_scale) > 0.05f) {
            Fonts new_fonts = LoadFonts(desired_font_scale);
            if (new_fonts.heading && new_fonts.body && new_fonts.small) {
                DestroyFonts(fonts);
                fonts = new_fonts;
                font_scale = desired_font_scale;
            } else {
                DestroyFonts(new_fonts);
            }
        }

        const int panel_px =
            static_cast<int>(std::lround(420.0f * font_scale));
        const int margin_px =
            static_cast<int>(std::lround(60.0f * font_scale));
        Layout layout = ComputeLayout(current_w, current_h, board_state.board.cols(),
                                      board_state.board.rows(), panel_px, margin_px);
        board_state.layout = layout;

        auto polled_events = input.Poll();
        for (const auto& evt : polled_events) {
            if (evt.type == InputEventType::Quit) {
                running = false;
                break;
            }
            if (evt.type == InputEventType::WindowRestored) {
                if (!window_mode.fullscreen) {
                    if (window_mode.want_maximized) {
                        SDL_MaximizeWindow(window);
                    } else {
                        int target_w = std::clamp(window_mode.windowed_width, min_window_width, base_width);
                        int target_h = std::clamp(window_mode.windowed_height, min_window_height, base_height);
                        SDL_SetWindowSize(window, target_w, target_h);
                    }
                }
                continue;
            }

            switch (evt.type) {
                case InputEventType::MouseMove:
                case InputEventType::MouseButtonDown:
                case InputEventType::MouseButtonUp:
                case InputEventType::MouseWheel:
                case InputEventType::KeyDown:
                case InputEventType::KeyUp:
                    mark_mouse_keyboard();
                    break;
                case InputEventType::ControllerButtonDown:
                case InputEventType::ControllerButtonUp:
                case InputEventType::ControllerAxisMotion:
                    mark_controller();
                    break;
                default:
                    break;
            }

            bool using_controller = (last_input_mode == InputMode::Controller);

            if (current_screen == AppScreen::Intro) {
                continue;
            }

            auto finish_prompt_active = [&]() {
                return current_screen == AppScreen::Gameplay && game_ctx.game_over && g_banner.visible &&
                       g_banner.show_finish_hint;
            };

            auto finish_input_triggered = [&]() -> bool {
                if (!finish_prompt_active()) {
                    return false;
                }
                if (last_input_mode == InputMode::Controller) {
                    return evt.type == InputEventType::ControllerButtonDown &&
                           (evt.controller_button == ControllerButton::A ||
                            evt.controller_button == ControllerButton::Menu);
                }
                if (evt.type == InputEventType::KeyDown && evt.key == KeyCode::Enter) {
                    return true;
                }
                return false;
            };

            if (finish_input_triggered()) {
                PlayClickSound();
                finish_match();
                continue;
            }

            auto wants_pause_toggle = [&](const match::platform::InputEvent& event) -> bool {
                if (event.type == InputEventType::ControllerButtonDown &&
                    event.controller_button == ControllerButton::Menu) {
                    return true;
                }
                if (event.type == InputEventType::KeyDown && event.key == KeyCode::Escape) {
                    return true;
                }
                return false;
            };

            if (current_screen == AppScreen::Gameplay && wants_pause_toggle(evt)) {
                if (pause_menu_active) {
                    pause_menu_active = false;
                    PlayClickSound();
                } else {
                    pause_menu_active = true;
                    pause_menu_state.selected = 0;
                    PlayClickSound();
                }
                continue;
            }

            switch (current_screen) {
                case AppScreen::MainMenu: {
                    auto action =
                        match::ui::HandleMenuEvent(menu_state, evt, using_controller, current_w, current_h);
                    if (action == match::ui::MenuAction::StartPvC ||
                        action == match::ui::MenuAction::StartPvP) {
                        PlayClickSound();
                        ResetTournamentState();
                        ui_settings.mode =
                            (action == match::ui::MenuAction::StartPvC)
                                ? match::ui::GameMode::PvC
                                : match::ui::GameMode::PvP;
                        prepare_save_setup(ui_settings.mode,
                                           using_controller,
                                           AppScreen::MainMenu,
                                           SaveSetupIntent::ConfigureSettings);
                        if (using_controller) {
                            last_input_mode = InputMode::Controller;
                            SDL_ShowCursor(SDL_DISABLE);
                        } else {
                            last_input_mode = InputMode::MouseKeyboard;
                            SDL_ShowCursor(SDL_ENABLE);
                        }
                    } else if (action == match::ui::MenuAction::StartTournament) {
                        PlayClickSound();
                        ui_settings.mode = match::ui::GameMode::Tournament;
                        ui_settings.player_count = std::max(4, ui_settings.player_count);
                        ui_settings.EnsureConstraints();
                        ResetTournamentState();
                        prepare_save_setup(ui_settings.mode,
                                           using_controller,
                                           AppScreen::MainMenu,
                                           SaveSetupIntent::ConfigureSettings);
                        if (using_controller) {
                            last_input_mode = InputMode::Controller;
                            SDL_ShowCursor(SDL_DISABLE);
                        } else {
                            last_input_mode = InputMode::MouseKeyboard;
                            SDL_ShowCursor(SDL_ENABLE);
                        }
                    } else if (action == match::ui::MenuAction::Settings) {
                        PlayClickSound();
                        deactivate_osk();
                        display_settings_state.fullscreen = window_mode.fullscreen;
                        display_settings_state.selected = 0;
                        current_screen = AppScreen::Settings;
                        set_window_title("MATCH — Settings");
                    } else if (action == match::ui::MenuAction::Quit) {
                        PlayClickSound();
                        running = false;
                    }
                    break;
                }
                case AppScreen::SaveSetup: {
                    auto action = match::ui::HandleSaveSetupEvent(save_setup_state, evt, using_controller,
                                                                  current_w, current_h);
                    switch (action) {
                        case match::ui::SaveSetupAction::None:
                            break;
                        case match::ui::SaveSetupAction::Cancel:
                            PlayClickSound();
                            deactivate_osk();
                            save_setup_intent = SaveSetupIntent::ConfigureSettings;
                            if (save_setup_cancel_target == AppScreen::GameSettings) {
                                current_screen = AppScreen::GameSettings;
                                set_game_settings_title();
                            } else {
                                current_screen = AppScreen::MainMenu;
                                set_main_menu_title();
                            }
                            break;
                        case match::ui::SaveSetupAction::OpenEntry:
                            PlayClickSound();
                            show_save_detail(save_setup_state.selected_index, using_controller);
                            break;
                    }
                    break;
                }
                case AppScreen::SaveDetail: {
                    auto action = match::ui::HandleSaveDetailEvent(save_detail_state, evt, using_controller);
                    switch (action) {
                        case match::ui::SaveDetailAction::None:
                            break;
                        case match::ui::SaveDetailAction::Cancel:
                            PlayClickSound();
                            current_screen = AppScreen::SaveSetup;
                            set_save_setup_title();
                            break;
                        case match::ui::SaveDetailAction::Confirm:
                            if (save_detail_state.is_new) {
                                PlayClickSound();
                                pending_save_display.clear();
                                pending_save_filename.clear();
                                begin_name_prompt(using_controller, AppScreen::SaveDetail);
                            } else {
                                if (load_save_slot(save_detail_state.slot, using_controller)) {
                                    PlayClickSound();
                                } else {
                                    PlayErrorSound();
                                    save_detail_state.summary.error = save_setup_state.error;
                                    save_detail_state.info_lines = {save_setup_state.error};
                                }
                            }
                            break;
                        case match::ui::SaveDetailAction::Delete:
                            if (save_detail_state.is_new) {
                                break;
                            }
                            if (!delete_save_slot(save_detail_state.slot)) {
                                PlayErrorSound();
                                save_detail_state.summary.error = save_setup_state.error;
                                save_detail_state.info_lines = {save_setup_state.error};
                                break;
                            }
                            PlayClickSound();
                            if (save_setup_state.slots.empty()) {
                                pending_save_display.clear();
                                pending_save_filename.clear();
                                begin_name_prompt(using_controller, AppScreen::SaveSetup);
                            } else {
                                current_screen = AppScreen::SaveSetup;
                                set_save_setup_title();
                            }
                            break;
                    }
                    break;
                }
                case AppScreen::NamePrompt: {
                    if (osk_state.active) {
                        auto osk_action =
                            match::ui::HandleOskEvent(osk_state, evt, using_controller, current_w, current_h);
                        if (osk_action == match::ui::OskAction::Commit) {
                            PlayClickSound();
                            deactivate_osk();
                            constexpr std::size_t kMaxName = 16;
                            if (name_prompt_state.input.size() > kMaxName) {
                                name_prompt_state.input.resize(kMaxName);
                            }
                        } else if (osk_action == match::ui::OskAction::Cancel) {
                            PlayErrorSound();
                            deactivate_osk();
                        }
                        break;
                    }
                    auto action =
                        match::ui::HandleNamePromptEvent(name_prompt_state, evt, using_controller);
                    switch (action) {
                        case match::ui::NamePromptAction::None:
                            break;
                        case match::ui::NamePromptAction::Backspace:
                            if (!name_prompt_state.input.empty()) {
                                name_prompt_state.input.pop_back();
                            }
                            break;
                        case match::ui::NamePromptAction::Cancel:
                            PlayClickSound();
                            stop_text_input();
                            deactivate_osk();
                            pending_save_display.clear();
                            pending_save_filename.clear();
                            current_screen = name_prompt_cancel_target;
                            if (current_screen == AppScreen::SaveSetup) {
                                set_save_setup_title();
                            } else {
                                set_main_menu_title();
                            }
                            break;
                        case match::ui::NamePromptAction::Submit: {
                            SaveNameComponents components = SanitizeSaveName(name_prompt_state.input);
                            if (components.filename.empty()) {
                                name_prompt_state.error = "Please enter a valid name";
                                PlayErrorSound();
                                break;
                            }
                            pending_save_display = components.display;
                            pending_save_filename = components.filename;
                            name_prompt_state.error.clear();
                            stop_text_input();
                            deactivate_osk();
                            const bool launching = (save_setup_intent == SaveSetupIntent::LaunchGameplay);
                            save_setup_intent = SaveSetupIntent::ConfigureSettings;
                            if (launching) {
                                if (ui_settings.mode == match::ui::GameMode::Tournament) {
                                    begin_tournament_series();
                                } else {
                                    start_new_game();
                                }
                            } else {
                                current_screen = AppScreen::TimeMode;
                                set_time_mode_title();
                                time_mode_state.selected =
                                    (ui_settings.time_mode == match::ui::TimeModeOption::Blitz) ? 1 : 0;
                            }
                            break;
                        }
                    }
                    break;
                }
                case AppScreen::TimeMode: {
                    auto action = match::ui::HandleTimeModeEvent(time_mode_state, evt, using_controller);
                    switch (action) {
                        case match::ui::TimeModeAction::None:
                            break;
                        case match::ui::TimeModeAction::Classic:
                            ui_settings.time_mode = match::ui::TimeModeOption::Classic;
                            game_settings_return_screen = AppScreen::TimeMode;
                            current_screen = AppScreen::GameSettings;
                            settings_state.selected = 0;
                                set_game_settings_title();
                            break;
                        case match::ui::TimeModeAction::Blitz:
                            ui_settings.time_mode = match::ui::TimeModeOption::Blitz;
                            blitz_state.minutes = ui_settings.blitz_turn_minutes;
                            blitz_state.between_seconds = ui_settings.blitz_between_seconds;
                            blitz_state.selected = 0;
                            current_screen = AppScreen::BlitzSettings;
                            set_blitz_settings_title();
                            break;
                        case match::ui::TimeModeAction::Back:
                            current_screen = AppScreen::NamePrompt;
                            set_name_prompt_title();
                            if (last_input_mode == InputMode::MouseKeyboard) {
                                start_text_input();
                            } else if (last_input_mode == InputMode::Controller) {
                                match::ui::BeginOsk(osk_state, "Save Name", name_prompt_state.input, 16);
                            }
                            break;
                    }
                    break;
                }
                case AppScreen::BlitzSettings: {
                    auto action =
                        match::ui::HandleBlitzSettingsEvent(blitz_state, evt, using_controller);
                    switch (action) {
                        case match::ui::BlitzSettingsAction::None:
                            break;
                        case match::ui::BlitzSettingsAction::Continue:
                            ui_settings.blitz_turn_minutes = blitz_state.minutes;
                            ui_settings.blitz_between_seconds = blitz_state.between_seconds;
                            game_settings_return_screen = AppScreen::TimeMode;
                            current_screen = AppScreen::GameSettings;
                            settings_state.selected = 0;
                                set_game_settings_title();
                            break;
                        case match::ui::BlitzSettingsAction::Back:
                            current_screen = AppScreen::TimeMode;
                            set_time_mode_title();
                            break;
                    }
                    break;
                }
                case AppScreen::Settings: {
                    auto action =
                        match::ui::HandleDisplaySettingsEvent(display_settings_state, evt, using_controller);
                    switch (action) {
                        case match::ui::DisplaySettingsAction::None:
                            break;
                        case match::ui::DisplaySettingsAction::Toggle:
                            PlayClickSound();
                            if (apply_window_mode(!window_mode.fullscreen)) {
                                display_settings_state.fullscreen = window_mode.fullscreen;
                            } else {
                                display_settings_state.fullscreen = window_mode.fullscreen;
                            }
                            break;
                        case match::ui::DisplaySettingsAction::Back:
                            PlayClickSound();
                            current_screen = AppScreen::MainMenu;
                            set_main_menu_title();
                            display_settings_state.selected = 0;
                            break;
                    }
                    break;
                }
                case AppScreen::GameSettings: {
                    if (osk_state.active) {
                        auto osk_action =
                            match::ui::HandleOskEvent(osk_state, evt, using_controller, current_w, current_h);
                        if (osk_action == match::ui::OskAction::Commit) {
                            PlayClickSound();
                            deactivate_osk();
                            ui_settings.EnsureConstraints();
                            settings_state.RefreshEntries();
                        } else if (osk_action == match::ui::OskAction::Cancel) {
                            PlayErrorSound();
                            deactivate_osk();
                        }
                        break;
                    }
                    auto action =
                        match::ui::HandleSettingsEvent(settings_state, evt, using_controller, current_w, current_h);
                    switch (action) {
                        case match::ui::SettingsAction::None:
                            break;
                        case match::ui::SettingsAction::Back:
                            PlayClickSound();
                            current_screen = game_settings_return_screen;
                            deactivate_osk();
                            if (current_screen == AppScreen::TimeMode) {
                                set_time_mode_title();
                            } else {
                                set_main_menu_title();
                            }
                            break;
                        case match::ui::SettingsAction::StartGame:
                            if (ui_settings.mode == match::ui::GameMode::Tournament) {
                                if (pending_save_filename.empty()) {
                                    PlayClickSound();
                                    prepare_save_setup(ui_settings.mode,
                                                       using_controller,
                                                       AppScreen::GameSettings,
                                                       SaveSetupIntent::LaunchGameplay);
                                    break;
                                }
                                PlayClickSound();
                                begin_tournament_series();
                                break;
                            }
                            if (pending_save_filename.empty()) {
                                PlayClickSound();
                                prepare_save_setup(ui_settings.mode,
                                                   using_controller,
                                                   AppScreen::GameSettings,
                                                   SaveSetupIntent::LaunchGameplay);
                                break;
                            }
                            PlayClickSound();
                            start_new_game();
                            break;
                        case match::ui::SettingsAction::EditPlayerName: {
                            PlayClickSound();
                            ui_settings.EnsureConstraints();
                            int index = settings_state.pending_player_index;
                            if (index < 0 ||
                                index >= static_cast<int>(ui_settings.player_names.size())) {
                                break;
                            }
                            deactivate_osk();
                            std::string title = ui_settings.player_is_cpu(index)
                                                    ? "Computer Name"
                                                    : ("Player " + std::to_string(index + 1) + " Name");
                            bool show_keyboard = settings_state.last_activation_from_controller;
                            match::ui::BeginOsk(osk_state,
                                                title,
                                                ui_settings.player_names[static_cast<std::size_t>(index)],
                                                12,
                                                show_keyboard);
                            if (show_keyboard) {
                                stop_text_input();
                            } else {
                                start_text_input();
                            }
                            break;
                        }
                    }
                    break;
                }
                case AppScreen::TournamentBracket: {
                    auto view = BuildTournamentBracketView(g_tournament);
                    tournament_bracket_state.start_enabled = view.next_match_ready;
                    auto action =
                        match::ui::HandleTournamentBracketEvent(tournament_bracket_state, evt, using_controller);
                    switch (action) {
                        case match::ui::TournamentBracketAction::None:
                            break;
                        case match::ui::TournamentBracketAction::StartMatch:
                            if (!view.next_match_ready) {
                                PlayErrorSound();
                                break;
                            }
                            if (start_next_tournament_match()) {
                                PlayClickSound();
                            } else {
                                PlayErrorSound();
                            }
                            break;
                        case match::ui::TournamentBracketAction::Back:
                            PlayClickSound();
                            if (TournamentComplete(g_tournament)) {
                                finish_tournament_series();
                            } else {
                                ResetTournamentState();
                                current_screen = AppScreen::MainMenu;
                                set_main_menu_title();
                                SDL_ShowCursor(SDL_ENABLE);
                            }
                            break;
                    }
                    break;
                }
                case AppScreen::Gameplay: {
                    if (pause_menu_active) {
                        PauseMenuAction pause_action = HandlePauseMenuEvent(pause_menu_state, evt, using_controller);
                        switch (pause_action) {
                            case PauseMenuAction::None:
                                break;
                            case PauseMenuAction::Resume:
                                pause_menu_active = false;
                                PlayClickSound();
                                break;
                            case PauseMenuAction::MainMenu:
                                pause_menu_active = false;
                                current_screen = AppScreen::MainMenu;
                                set_main_menu_title();
                                SDL_ShowCursor(SDL_ENABLE);
                                last_input_mode = InputMode::MouseKeyboard;
                                ResetBannerOverlay();
                                g_win_announced = false;
                                if (g_tournament.active) {
                                    ui_settings = g_tournament.base_settings;
                                }
                                ResetTournamentState();
                                PlayClickSound();
                                break;
                            case PauseMenuAction::Quit:
                                PlayClickSound();
                                running = false;
                                break;
                        }
                        break;
                    }
                    switch (evt.type) {
                        case InputEventType::MouseMove:
                            UpdateHoverCell(board_state, evt.x, evt.y);
                            break;
                        case InputEventType::MouseButtonDown:
                            if (evt.mouse_button == MouseButton::Left &&
                                !board_state.cascade.active && board_state.animations.empty()) {
                                HandleMouseDown(board_state, game_ctx, evt.x, evt.y);
                            }
                            break;
                        case InputEventType::MouseButtonUp:
                        case InputEventType::MouseWheel:
                            break;
                        case InputEventType::KeyDown:
                            if (evt.key == KeyCode::Escape) {
                                running = false;
                            }
                            break;
                        case InputEventType::ControllerButtonDown:
                            EnsureControllerCursor(board_state);
                            switch (evt.controller_button) {
                                case ControllerButton::DPadUp:
                                    MoveControllerCursor(board_state, 0, -1);
                                    break;
                                case ControllerButton::DPadDown:
                                    MoveControllerCursor(board_state, 0, 1);
                                    break;
                                case ControllerButton::DPadLeft:
                                    MoveControllerCursor(board_state, -1, 0);
                                    break;
                                case ControllerButton::DPadRight:
                                    MoveControllerCursor(board_state, 1, 0);
                                    break;
                                case ControllerButton::A:
                                    if (!board_state.cascade.active && board_state.animations.empty()) {
                                        ControllerSelect(board_state, game_ctx);
                                    }
                                    break;
                                case ControllerButton::B:
                                    ControllerCancel(board_state, game_ctx);
                                    break;
                                case ControllerButton::Menu:
                                    running = false;
                                    break;
                                default:
                                    break;
                            }
                            break;
                        case InputEventType::ControllerAxisMotion: {
                            const int threshold = 20000;
                            int direction = 0;
                            if (evt.axis_value > threshold) {
                                direction = 1;
                            } else if (evt.axis_value < -threshold) {
                                direction = -1;
                            }
                            auto can_step = [&](int dir, int& stored_dir, Uint32& stored_tick) {
                                Uint32 now = SDL_GetTicks();
                                if (dir == 0) {
                                    stored_dir = 0;
                                    stored_tick = 0;
                                    return false;
                                }
                                if (dir != stored_dir || now - stored_tick >= 150) {
                                    stored_dir = dir;
                                    stored_tick = now;
                                    return true;
                                }
                                return false;
                            };
                            if (evt.controller_axis == ControllerAxis::LeftX) {
                                if (can_step(direction,
                                             board_state.controller_axis_horizontal,
                                             board_state.controller_axis_horizontal_tick)) {
                                    EnsureControllerCursor(board_state);
                                    MoveControllerCursor(board_state, direction, 0);
                                }
                                if (direction == 0) {
                                    board_state.controller_axis_horizontal = 0;
                                    board_state.controller_axis_horizontal_tick = 0;
                                }
                            } else if (evt.controller_axis == ControllerAxis::LeftY) {
                                if (can_step(direction,
                                             board_state.controller_axis_vertical,
                                             board_state.controller_axis_vertical_tick)) {
                                    EnsureControllerCursor(board_state);
                                    MoveControllerCursor(board_state, 0, direction);
                                }
                                if (direction == 0) {
                                    board_state.controller_axis_vertical = 0;
                                    board_state.controller_axis_vertical_tick = 0;
                                }
                            }
                            break;
                        }
                        default:
                            break;
                    }
                    break;
                }
            }
        }

        if (!running) {
            break;
        }

        Uint64 now = SDL_GetPerformanceCounter();
        const float delta_ms =
            static_cast<float>((now - last_counter) * 1000.0 / frequency);
        last_counter = now;
        UpdateBannerOverlay(delta_ms);

        if (current_screen == AppScreen::Intro) {
            UpdateIntroState(intro_state, delta_ms);
            if (!intro_state.active) {
                current_screen = AppScreen::MainMenu;
                set_main_menu_title();
                if (audio_ready && !music_started) {
                    audio.StartMusicLoop();
                    music_started = true;
                }
            }
        }

        if (current_screen == AppScreen::Gameplay && !pause_menu_active) {
        UpdateAnimations(board_state.animations, board_state.hidden_cells, delta_ms);
        UpdateBlitzTimers(board_state, game_ctx, delta_ms);
            AdvanceCascade(board_state, game_ctx);

            bool banner_blocking = BannerBlocksInput();
            bool can_ai_move = !board_state.cascade.active && board_state.animations.empty() && !banner_blocking;
            if (IsComputerPlayer(game_ctx, game_ctx.active_player)) {
                if (banner_blocking) {
                    game_ctx.ai_pending = false;
                    game_ctx.ai_timer_ms = 0.0f;
                } else {
                    if (!game_ctx.ai_pending) {
                        game_ctx.ai_pending = true;
                        game_ctx.ai_timer_ms = 0.0f;
                    }
                    if (can_ai_move) {
                        game_ctx.ai_timer_ms += delta_ms;
                        if (game_ctx.ai_timer_ms >= 350.0f) {
                            auto best = match::core::ai::BestMove(board_state.board);
                            if (best.has_value()) {
                                if (BeginPlayerMove(board_state, game_ctx, best->move)) {
                                    mark_controller();
                                    game_ctx.status = "Computer move";
                                    board_state.selected.reset();
                                    board_state.hover.reset();
                                    board_state.controller_cursor = best->move.a;
                                } else {
                                    game_ctx.status = "Computer cannot move";
                                }
                            } else {
                                mark_controller();
                                const int previous_round = game_ctx.round_current;
                                const int previous_player = game_ctx.active_player;
                                const bool previous_game_over = game_ctx.game_over;
                                SyncPlayerVectors(game_ctx);
                                if (!game_ctx.moves_left_per_player.empty() &&
                                    game_ctx.active_player >= 0 &&
                                    game_ctx.active_player <
                                        static_cast<int>(game_ctx.moves_left_per_player.size())) {
                                    game_ctx.moves_left_per_player[static_cast<std::size_t>(
                                        game_ctx.active_player)] = 0;
                                }

                                if (RoundComplete(game_ctx)) {
                                    game_ctx.round_current += 1;
                                    if (game_ctx.round_current > game_ctx.round_total) {
                                        game_ctx.game_over = true;
                                        game_ctx.status = BuildGameOverStatus(game_ctx);
                                    } else {
                                        ResetMovesForNewRound(game_ctx);
                                        game_ctx.active_player = 0;
                                        game_ctx.status = BuildTurnStatus(game_ctx);
                                    }
                                } else {
                                    int next = NextPlayerWithMoves(game_ctx, game_ctx.active_player);
                                    if (next >= 0) {
                                        game_ctx.active_player = next;
                                    }
                                    if (!game_ctx.game_over) {
                                        game_ctx.status = BuildTurnStatus(game_ctx);
                                    }
                                }
                                HandleStateFeedback(game_ctx, previous_round, previous_player, previous_game_over);
                                UpdateAiPending(game_ctx);
                            }
                            game_ctx.ai_pending = false;
                            game_ctx.ai_timer_ms = 0.0f;
                        }
                    } else {
                        game_ctx.ai_timer_ms = 0.0f;
                    }
                }
            } else {
                game_ctx.ai_pending = false;
                game_ctx.ai_timer_ms = 0.0f;
            }
            UpdateWindowTitle(window, game_ctx);
            if (game_ctx.autosave_enabled && !board_state.cascade.active &&
                board_state.animations.empty()) {
                if (game_ctx.autosave_dirty) {
                    game_ctx.autosave_cooldown_ms += delta_ms;
                    if (game_ctx.autosave_cooldown_ms >= 300.0f) {
                        if (!WriteAutomaticSave(save_service, board_state, game_ctx)) {
                            game_ctx.status = "Autosave failed";
                        } else {
                            save_slots_dirty = true;
                        }
                    }
                } else {
                    game_ctx.autosave_cooldown_ms = 0.0f;
                }
            }
        }

        SDL_SetRenderDrawColor(renderer, 10, 10, 12, 255);
        SDL_RenderClear(renderer);

        const bool render_using_controller = (last_input_mode == InputMode::Controller);

        if (current_screen == AppScreen::Intro) {
            RenderIntro(renderer, fonts, current_w, current_h, intro_state);
            SDL_RenderPresent(renderer);
            continue;
        }

        if (current_screen == AppScreen::MainMenu) {
            match::ui::RenderMenu(renderer, fonts, current_w, current_h, menu_state,
                                  render_using_controller);
            SDL_RenderPresent(renderer);
            continue;
        }

        if (current_screen == AppScreen::SaveSetup) {
            refresh_save_slots();
            match::ui::RenderSaveSetup(renderer, fonts, current_w, current_h, save_setup_state,
                                       render_using_controller);
            if (osk_state.active && osk_state.show_keyboard) {
                match::ui::RenderOsk(renderer, fonts, current_w, current_h, osk_state, render_using_controller);
            }
            SDL_RenderPresent(renderer);
            continue;
        }

        if (current_screen == AppScreen::SaveDetail) {
            match::ui::RenderSaveDetail(renderer, fonts, current_w, current_h, save_detail_state,
                                        render_using_controller);
            SDL_RenderPresent(renderer);
            continue;
        }

        if (current_screen == AppScreen::NamePrompt) {
            match::ui::RenderNamePrompt(renderer, fonts, current_w, current_h, name_prompt_state);
            if (osk_state.active && osk_state.show_keyboard) {
                match::ui::RenderOsk(renderer, fonts, current_w, current_h, osk_state,
                                     last_input_mode == InputMode::Controller);
            }
            SDL_RenderPresent(renderer);
            continue;
        }

        if (current_screen == AppScreen::TimeMode) {
            match::ui::RenderTimeMode(renderer, fonts, current_w, current_h, time_mode_state, render_using_controller);
            SDL_RenderPresent(renderer);
            continue;
        }

        if (current_screen == AppScreen::BlitzSettings) {
            match::ui::RenderBlitzSettings(renderer, fonts, current_w, current_h, blitz_state, render_using_controller);
            SDL_RenderPresent(renderer);
            continue;
        }

        if (current_screen == AppScreen::Settings) {
            match::ui::RenderDisplaySettings(renderer, fonts, current_w, current_h, display_settings_state,
                                             render_using_controller);
            SDL_RenderPresent(renderer);
            continue;
        }

        if (current_screen == AppScreen::GameSettings) {
            match::ui::RenderSettings(renderer, fonts, current_w, current_h, settings_state,
                                      render_using_controller,
                                      (osk_state.active ? &osk_state : nullptr));
            if (osk_state.active && osk_state.show_keyboard) {
                match::ui::RenderOsk(renderer, fonts, current_w, current_h, osk_state, render_using_controller);
            }
            SDL_RenderPresent(renderer);
            continue;
        }

        if (current_screen == AppScreen::TournamentBracket) {
            auto view = BuildTournamentBracketView(g_tournament);
            tournament_bracket_state.start_enabled = view.next_match_ready;
            match::ui::RenderTournamentBracket(renderer, fonts, current_w, current_h, tournament_bracket_state, view,
                                               render_using_controller);
            SDL_RenderPresent(renderer);
            continue;
        }

        BoardRenderData board_render{
            board_state.board,
            board_state.hidden_cells,
            board_state.selected,
            board_state.hover,
            board_state.controller_cursor};
        DrawBoard(renderer, board_render, layout);
        DrawBoard(renderer, board_render, layout);
        DrawAnimations(renderer, board_state.animations, layout);
        PanelInfo panel = BuildPanelInfo(game_ctx);
        DrawPanel(renderer, layout, fonts, panel);
        DrawBanner(renderer, fonts, layout, g_banner, render_using_controller);
        if (pause_menu_active) {
            RenderPauseMenu(renderer, fonts, current_w, current_h, pause_menu_state);
        }

        SDL_RenderPresent(renderer);
    }

    input.Shutdown();
    g_input = nullptr;
    SDL_ShowCursor(SDL_ENABLE);

    DestroyIntroResources(intro_state);
    DestroyFonts(fonts);
    if (audio_ready) {
        audio.Shutdown();
        g_audio = nullptr;
    }
    if (img_result != 0) {
        IMG_Quit();
    }
    TTF_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
