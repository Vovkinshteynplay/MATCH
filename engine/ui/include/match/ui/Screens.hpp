#pragma once

#include <SDL2/SDL.h>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "match/platform/InputEvents.hpp"
#include "match/platform/SdlSaveService.hpp"
#include "match/render/SceneRenderer.hpp"

namespace match::ui {

enum class GameMode { PvC, PvP, Tournament };
enum class TimeModeOption { Classic, Blitz };
enum class TurnOrderOption { Consecutive, RoundRobin };

inline constexpr int kMinPlayers = 2;
inline constexpr int kMinMovesPerRound = 1;
inline constexpr int kMaxMovesPerRound = 20;
inline constexpr int kMinTotalRounds = 1;
inline constexpr int kMaxTotalRounds = 50;

struct GameSettings {
    GameMode mode = GameMode::PvC;
    int player_count = 2;
    std::vector<std::string> player_names = {"Player 1", "Computer"};
    TurnOrderOption turn_order = TurnOrderOption::Consecutive;
    int moves_per_round = 3;
    int total_rounds = 5;
    bool bombs_enabled = true;
    bool color_blast_enabled = false;
    TimeModeOption time_mode = TimeModeOption::Classic;
    int blitz_turn_minutes = 2;
    int blitz_between_seconds = 10;

    void EnsureConstraints();
    bool player_is_cpu(int index) const;
};

inline std::string MakeDefaultPlayerName(int index) {
    return "Player " + std::to_string(index + 1);
}

inline void GameSettings::EnsureConstraints() {
    if (mode == GameMode::PvC) {
        player_count = 2;
    } else {
        player_count = std::max(kMinPlayers, player_count);
    }
    if (player_names.size() < static_cast<std::size_t>(player_count)) {
        const std::size_t target = static_cast<std::size_t>(player_count);
        for (std::size_t i = player_names.size(); i < target; ++i) {
            player_names.push_back(MakeDefaultPlayerName(static_cast<int>(i)));
        }
    } else if (player_names.size() > static_cast<std::size_t>(player_count)) {
        player_names.resize(static_cast<std::size_t>(player_count));
    }
    for (int i = 0; i < player_count; ++i) {
        auto& name = player_names[static_cast<std::size_t>(i)];
        if (name.empty()) {
            name = (mode == GameMode::PvC && i == player_count - 1)
                       ? "Computer"
                       : MakeDefaultPlayerName(i);
        }
        if (mode == GameMode::PvC && i == player_count - 1) {
            if (name.find("Computer") == std::string::npos) {
                name += " (Computer)";
            }
        } else {
            if (name.find("Computer") != std::string::npos) {
                name = MakeDefaultPlayerName(i);
            }
        }
    }
    moves_per_round = std::clamp(moves_per_round, kMinMovesPerRound, kMaxMovesPerRound);
    total_rounds = std::clamp(total_rounds, kMinTotalRounds, kMaxTotalRounds);
    blitz_turn_minutes = std::clamp(blitz_turn_minutes, 1, 5);
    blitz_between_seconds = std::clamp(blitz_between_seconds, 0, 30);
}

inline bool GameSettings::player_is_cpu(int index) const {
    return mode == GameMode::PvC && index == player_count - 1 && index >= 0;
}

struct MenuState {
    int selected = 0;
    std::array<SDL_Rect, 5> button_bounds{};
    int axis_vertical = 0;
    Uint32 axis_vertical_tick = 0;
};

enum class MenuAction {
    None,
    StartPvC,
    StartPvP,
    StartTournament,
    Settings,
    Quit
};

MenuAction HandleMenuEvent(MenuState& state,
                           const match::platform::InputEvent& evt,
                           bool using_controller,
                           int window_width,
                           int window_height);
void RenderMenu(SDL_Renderer* renderer,
                const match::render::Fonts& fonts,
                int window_width,
                int window_height,
                MenuState& state,
                bool using_controller);

struct SaveSummary {
    std::string title;
    std::vector<std::string> detail_lines;
    match::platform::SaveSlotInfo slot;
    bool is_new_entry = false;
    bool valid = true;
    std::string error;
};

struct SaveBrowserState {
    std::vector<SaveSummary> entries;
    int selected = 0;
    bool delete_enabled = false;
};

enum class SaveBrowserAction { None, StartNew, Load, Delete, Cancel };

SaveBrowserAction HandleSaveBrowserEvent(SaveBrowserState& state,
                                         const match::platform::InputEvent& evt,
                                         bool using_controller);
void RenderSaveBrowser(SDL_Renderer* renderer,
                       const match::render::Fonts& fonts,
                       int window_width,
                       int window_height,
                       const SaveBrowserState& state);

struct NamePromptState {
    std::string input;
    std::string error;
};

enum class NamePromptAction { None, Submit, Cancel, Character, Backspace };

NamePromptAction HandleNamePromptEvent(NamePromptState& state,
                                       const match::platform::InputEvent& evt,
                                       bool using_controller);
void RenderNamePrompt(SDL_Renderer* renderer,
                      const match::render::Fonts& fonts,
                      int window_width,
                      int window_height,
                      const NamePromptState& state);

struct TimeModeState {
    int selected = 0;
    std::array<SDL_Rect, 3> button_bounds{};
    int axis_vertical = 0;
    Uint32 axis_vertical_tick = 0;
};

enum class TimeModeAction { None, Classic, Blitz, Back };

TimeModeAction HandleTimeModeEvent(TimeModeState& state,
                                   const match::platform::InputEvent& evt,
                                   bool using_controller);
void RenderTimeMode(SDL_Renderer* renderer,
                    const match::render::Fonts& fonts,
                    int window_width,
                    int window_height,
                    TimeModeState& state,
                    bool using_controller);

struct BlitzSettingsState {
    int selected = 0;
    int minutes = 2;
    int between_seconds = 10;
    std::array<SDL_Rect, 3> button_bounds{};
    int axis_vertical = 0;
    int axis_horizontal = 0;
    Uint32 axis_vertical_tick = 0;
    Uint32 axis_horizontal_tick = 0;
};

enum class BlitzSettingsAction { None, Continue, Back };

BlitzSettingsAction HandleBlitzSettingsEvent(BlitzSettingsState& state,
                                             const match::platform::InputEvent& evt,
                                             bool using_controller);
void RenderBlitzSettings(SDL_Renderer* renderer,
                         const match::render::Fonts& fonts,
                         int window_width,
                         int window_height,
                         BlitzSettingsState& state,
                         bool using_controller);

struct SaveSetupState {
    GameMode mode = GameMode::PvC;
    std::vector<match::platform::SaveSlotInfo> slots;
    std::vector<SaveSummary> summaries;
    int selected_index = 0;
    int first_visible = 0;
    std::vector<SDL_Rect> entry_bounds;
    std::string error;
    int axis_vertical = 0;
    int axis_horizontal = 0;
    Uint32 axis_vertical_tick = 0;
};

enum class SaveSetupAction { None, OpenEntry, Cancel };

enum class SaveDetailAction { None, Confirm, Delete, Cancel };

struct SaveDetailState {
    bool is_new = false;
    SaveSummary summary;
    match::platform::SaveSlotInfo slot;
    std::vector<std::string> info_lines;
    int selected_button = 0;
    std::vector<std::string> buttons;
    std::vector<SaveDetailAction> button_actions;
    std::vector<SDL_Rect> button_bounds;
};

SaveSetupAction HandleSaveSetupEvent(SaveSetupState& state,
                                     const match::platform::InputEvent& evt,
                                     bool using_controller,
                                     int window_width,
                                     int window_height);
void RenderSaveSetup(SDL_Renderer* renderer,
                     const match::render::Fonts& fonts,
                     int window_width,
                     int window_height,
                     SaveSetupState& state,
                     bool using_controller);
SaveDetailAction HandleSaveDetailEvent(SaveDetailState& state,
                                       const match::platform::InputEvent& evt,
                                       bool using_controller);
void RenderSaveDetail(SDL_Renderer* renderer,
                      const match::render::Fonts& fonts,
                      int window_width,
                      int window_height,
                      SaveDetailState& state,
                      bool using_controller);

struct SettingsState {
    enum class EntryType {
        PlayerName,
        PlayerCount,
        MovesPerRound,
        TotalRounds,
        TurnOrder,
        Bombs,
        ColorBlast,
        StartGame,
        Back
    };

    struct Entry {
        EntryType type;
        int index = -1;
    };

    explicit SettingsState(GameSettings& s);

    void RefreshEntries();
    const Entry& EntryForIndex(int idx) const;

    GameSettings& settings;
    std::vector<Entry> entries;
    int selected = 0;
    int first_visible = 0;
    int pending_player_index = -1;
    std::vector<SDL_Rect> entry_bounds;
    int axis_vertical = 0;
    int axis_horizontal = 0;
    Uint32 axis_vertical_tick = 0;
    Uint32 axis_horizontal_tick = 0;
    bool last_activation_from_controller = false;
};

struct DisplaySettingsState {
    bool fullscreen = false;
    int selected = 0;
    SDL_Rect button_bounds{};
};

enum class DisplaySettingsAction { None, Toggle, Back };

DisplaySettingsAction HandleDisplaySettingsEvent(DisplaySettingsState& state,
                                                 const match::platform::InputEvent& evt,
                                                 bool using_controller);
void RenderDisplaySettings(SDL_Renderer* renderer,
                           const match::render::Fonts& fonts,
                           int window_width,
                           int window_height,
                           DisplaySettingsState& state,
                           bool using_controller);

enum class SettingsAction {
    None,
    Back,
    StartGame,
    EditPlayerName
};

SettingsAction HandleSettingsEvent(SettingsState& state,
                                   const match::platform::InputEvent& evt,
                                   bool using_controller,
                                   int window_width,
                                   int window_height);
struct OskState;

void RenderSettings(SDL_Renderer* renderer,
                    const match::render::Fonts& fonts,
                    int window_width,
                    int window_height,
                    SettingsState& state,
                    bool using_controller,
                    const OskState* text_input_state = nullptr);

struct OskState {
    std::string title;
    std::string buffer;
    std::string original;
    std::string* target = nullptr;
    std::size_t max_length = 12;
    int row = 0;
    int col = 0;
    std::vector<SDL_Rect> key_bounds;
    bool active = false;
    bool uppercase = true;
    bool show_keyboard = true;
    bool text_input_mode = false;
    int axis_vertical = 0;
    int axis_horizontal = 0;
    Uint32 axis_vertical_tick = 0;
    Uint32 axis_horizontal_tick = 0;
};

enum class OskAction { None, Commit, Cancel };

void BeginOsk(OskState& state,
              const std::string& title,
              std::string& target,
              std::size_t max_length = 12,
              bool show_keyboard = true);
OskAction HandleOskEvent(OskState& state,
                         const match::platform::InputEvent& evt,
                         bool using_controller,
                         int window_width,
                         int window_height);
void RenderOsk(SDL_Renderer* renderer,
               const match::render::Fonts& fonts,
               int window_width,
               int window_height,
               OskState& state,
               bool using_controller);

struct TournamentBracketState {
    int selected_button = 0;
    std::array<SDL_Rect, 2> button_bounds{};
    bool start_enabled = false;
};

struct BracketMatchView {
    std::string player_a;
    std::string player_b;
    bool player_a_won = false;
    bool player_b_won = false;
    bool active = false;
};

struct TournamentBracketView {
    std::vector<std::vector<BracketMatchView>> rounds;
    bool next_match_ready = false;
    std::string next_match_label;
    std::string status;
};

enum class TournamentBracketAction { None, StartMatch, Back };

TournamentBracketAction HandleTournamentBracketEvent(TournamentBracketState& state,
                                                     const match::platform::InputEvent& evt,
                                                     bool using_controller);
void RenderTournamentBracket(SDL_Renderer* renderer,
                             const match::render::Fonts& fonts,
                             int window_width,
                             int window_height,
                             TournamentBracketState& state,
                             const TournamentBracketView& view,
                             bool using_controller);

std::string ModeToString(GameMode mode);

}  // namespace match::ui
