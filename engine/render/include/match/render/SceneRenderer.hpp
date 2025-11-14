#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "match/core/Board.hpp"

namespace match::render {

struct Layout {
    float board_left{};
    float board_top{};
    float cell_size{};
    float cell_inset{};
    float grid_line_alpha{};
    float panel_left{};
    float panel_top{};
    float panel_width{};
    float panel_wrap{};
};

struct Color {
    Uint8 r{255};
    Uint8 g{255};
    Uint8 b{255};
    Uint8 a{255};
};

struct Animation {
    enum class Type { Swap, Pop, Fall, Spawn };

    Type type = Type::Pop;
    float duration_ms = 0.0f;
    float delay_ms = 0.0f;
    float elapsed_ms = 0.0f;
    float size_start = 1.0f;
    float size_end = 1.0f;
    float alpha_start = 255.0f;
    float alpha_end = 255.0f;
    float start_x = 0.0f;
    float start_y = 0.0f;
    float end_x = 0.0f;
    float end_y = 0.0f;
    Color color{};
    std::optional<match::core::Cell> reveal_cell;

    bool started() const;
    bool finished() const;
    float progress() const;
    float ease() const;
};

struct Fonts {
    TTF_Font* heading = nullptr;
    TTF_Font* body = nullptr;
    TTF_Font* small = nullptr;
};

struct BoardRenderData {
    const match::core::Board& board;
    const std::set<match::core::Cell>& hidden_cells;
    std::optional<match::core::Cell> selected;
    std::optional<match::core::Cell> hover;
    std::optional<match::core::Cell> controller_cursor;
};

struct PanelPlayerEntry {
    std::string name;
    int score = 0;
    bool active = false;
    int moves_left = -1;
};

struct PanelInfo {
    std::string mode;
    std::string round;
    std::string order;
    bool bombs_enabled = false;
    bool color_blast_enabled = false;
    int moves_left = 0;
    std::vector<PanelPlayerEntry> players;
    std::string status;
    std::vector<std::string> controls;
    bool show_turn_timer = false;
    float turn_timer_ms = 0.0f;
    float turn_timer_total_ms = 0.0f;
    bool show_pre_turn = false;
    int pre_turn_seconds = 0;
};

inline constexpr float kSwapDurationMs = 180.0f;
inline constexpr float kPopDurationMs = 200.0f;
inline constexpr float kFallDurationPerCellMs = 55.0f;
inline constexpr float kFallDurationMinMs = 120.0f;

Layout ComputeLayout(int window_w,
                     int window_h,
                     int cols,
                     int rows,
                     int panel_width_px = 420,
                     int margin_px = 60);

Fonts LoadFonts(float scale);
void DestroyFonts(Fonts& fonts);

Animation MakeSwapAnimation(const Layout& layout,
                            const match::core::Cell& from,
                            const match::core::Cell& to,
                            int tile,
                            float duration_ms);
Animation MakePopAnimation(const Layout& layout,
                           const match::core::Cell& cell,
                           int tile,
                           float duration_ms);
Animation MakeFallAnimation(const Layout& layout,
                            const match::core::Cell& from,
                            const match::core::Cell& to,
                            int tile,
                            float duration_ms);
Animation MakeSpawnAnimation(const Layout& layout,
                             const match::core::Cell& cell,
                             int tile,
                             int distance_cells,
                             float duration_ms);

void UpdateAnimations(std::vector<Animation>& animations,
                      std::set<match::core::Cell>& hidden_cells,
                      float delta_ms);

void DrawBoard(SDL_Renderer* renderer,
               const BoardRenderData& board_data,
               const Layout& layout);
void DrawAnimations(SDL_Renderer* renderer,
                    const std::vector<Animation>& animations,
                    const Layout& layout);
void DrawPanel(SDL_Renderer* renderer,
               const Layout& layout,
               const Fonts& fonts,
               const PanelInfo& panel);

}  // namespace match::render
