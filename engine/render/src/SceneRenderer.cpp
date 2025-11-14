#include "match/render/SceneRenderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>

#include "match/app/AssetFS.hpp"

namespace match::render {

namespace {

TTF_Font* LoadFontFromCandidates(const std::vector<std::filesystem::path>& candidates,
                                 int point_size) {
    for (const auto& candidate : candidates) {
        if (!match::app::FileExists(candidate)) {
            continue;
        }
        TTF_Font* font = TTF_OpenFont(candidate.string().c_str(), point_size);
        if (font) {
            TTF_SetFontHinting(font, TTF_HINTING_LIGHT);
            return font;
        }
    }
    return nullptr;
}

const std::array<Color, 6> kTileColors{{
    Color{62, 191, 238, 255},
    Color{238, 84, 76, 255},
    Color{255, 207, 65, 255},
    Color{97, 219, 112, 255},
    Color{98, 142, 255, 255},
    Color{177, 102, 235, 255},
}};

Color TileColor(int tile) {
    if (tile < 0 || tile >= static_cast<int>(kTileColors.size())) {
        return Color{64, 64, 64, 255};
    }
    return kTileColors[static_cast<std::size_t>(tile)];
}

struct SDLColorConverter {
    static SDL_Color Convert(Color color) {
        return SDL_Color{color.r, color.g, color.b, color.a};
    }
};

SDL_FPoint CellCenter(const Layout& layout, const match::core::Cell& cell) {
    SDL_FPoint point;
    point.x = layout.board_left + (cell.col + 0.5f) * layout.cell_size;
    point.y = layout.board_top + (cell.row + 0.5f) * layout.cell_size;
    return point;
}

SDL_FRect MakeRect(float center_x, float center_y, float half) {
    return SDL_FRect{center_x - half, center_y - half, half * 2.0f, half * 2.0f};
}

int RenderTextLine(SDL_Renderer* renderer,
                   TTF_Font* font,
                   int x,
                   int y,
                   const std::string& text,
                   SDL_Color color,
                   int* out_width = nullptr) {
    if (!font || text.empty()) {
        return 0;
    }
    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!surface) {
        return 0;
    }
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!texture) {
        int h = surface->h;
        SDL_FreeSurface(surface);
        return h;
    }
    SDL_Rect dst{x, y, surface->w, surface->h};
    SDL_RenderCopy(renderer, texture, nullptr, &dst);

    if (out_width) {
        *out_width = surface->w;
    }
    int height = surface->h;
    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
    return height;
}

int RenderWrappedText(SDL_Renderer* renderer,
                      TTF_Font* font,
                      int x,
                      int y,
                      const std::string& text,
                      SDL_Color color,
                      int wrap_width) {
    if (!font || text.empty()) {
        return 0;
    }
    SDL_Surface* surface =
        TTF_RenderUTF8_Blended_Wrapped(font, text.c_str(), color, wrap_width);
    if (!surface) {
        return 0;
    }
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!texture) {
        int h = surface->h;
        SDL_FreeSurface(surface);
        return h;
    }
    SDL_Rect dst{x, y, surface->w, surface->h};
    SDL_RenderCopy(renderer, texture, nullptr, &dst);
    int height = surface->h;
    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
    return height;
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

std::string FormatTimerString(float ms) {
    if (ms < 0.0f) {
        ms = 0.0f;
    }
    int total_seconds = static_cast<int>(std::round(ms / 1000.0f));
    int minutes = total_seconds / 60;
    int seconds = total_seconds % 60;
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d", minutes, seconds);
    return buffer;
}

}  // namespace

bool Animation::started() const {
    return elapsed_ms >= delay_ms;
}

bool Animation::finished() const {
    return elapsed_ms >= delay_ms + duration_ms;
}

float Animation::progress() const {
    if (!started()) {
        return 0.0f;
    }
    if (duration_ms <= 0.0f) {
        return 1.0f;
    }
    const float t = (elapsed_ms - delay_ms) / duration_ms;
    return std::clamp(t, 0.0f, 1.0f);
}

float Animation::ease() const {
    const float t = progress();
    return t * t * (3.0f - 2.0f * t);
}

Layout ComputeLayout(int window_w,
                     int window_h,
                     int cols,
                     int rows,
                     int panel_width_px,
                     int margin_px) {
    float width = static_cast<float>(window_w);
    float height = static_cast<float>(window_h);

    float margin =
        std::max(static_cast<float>(margin_px), std::min(width, height) * 0.025f);
    float panel_width = std::max(static_cast<float>(panel_width_px), width * 0.24f);

    float available_width = std::max(0.0f, width - panel_width - margin * 2.0f);
    float available_height = std::max(0.0f, height - margin * 2.0f);
    float cell_size = std::min(available_width / cols, available_height / rows);
    float board_width = cell_size * cols;
    float board_height = cell_size * rows;
    (void)board_height;

    float board_left = margin;
    float board_top = margin;
    float panel_left = board_left + board_width + margin;
    float panel_top = margin;

    Layout layout{};
    layout.board_left = board_left;
    layout.board_top = board_top;
    layout.cell_size = cell_size;
    layout.cell_inset = std::max(1.0f, cell_size * 0.04f);
    layout.grid_line_alpha = 35.0f;
    layout.panel_left = panel_left;
    layout.panel_top = panel_top;
    layout.panel_width = panel_width;
    layout.panel_wrap = panel_width - std::max(32.0f, panel_width * 0.08f);
    return layout;
}

namespace {

int ScaledFontSize(int base_size, float scale) {
    int scaled = static_cast<int>(std::lround(static_cast<double>(base_size) * scale));
    if (scaled <= 0) {
        scaled = base_size;
    }
    return std::max(12, scaled);
}

}  // namespace

Fonts LoadFonts(float scale) {
    std::vector<std::filesystem::path> search_paths = {
        "assets_common/fonts/SourceCodePro-Regular.ttf",
        "assets_common/fonts/RobotoMono-Regular.ttf",
        "assets_common/fonts/IBMPlexMono-Regular.ttf",
        "C:/Windows/Fonts/consola.ttf",
        "C:/Windows/Fonts/seguiemj.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
    };

    Fonts fonts;
    fonts.heading =
        LoadFontFromCandidates(search_paths, ScaledFontSize(30, scale));
    fonts.body = LoadFontFromCandidates(search_paths, ScaledFontSize(22, scale));
    fonts.small = LoadFontFromCandidates(search_paths, ScaledFontSize(18, scale));
    return fonts;
}

void DestroyFonts(Fonts& fonts) {
    if (fonts.heading) {
        TTF_CloseFont(fonts.heading);
        fonts.heading = nullptr;
    }
    if (fonts.body) {
        TTF_CloseFont(fonts.body);
        fonts.body = nullptr;
    }
    if (fonts.small) {
        TTF_CloseFont(fonts.small);
        fonts.small = nullptr;
    }
}

Animation MakeSwapAnimation(const Layout& layout,
                            const match::core::Cell& from,
                            const match::core::Cell& to,
                            int tile,
                            float duration_ms) {
    Animation anim;
    anim.type = Animation::Type::Swap;
    anim.duration_ms = duration_ms;
    anim.delay_ms = 0.0f;
    SDL_FPoint from_pt = CellCenter(layout, from);
    SDL_FPoint to_pt = CellCenter(layout, to);
    anim.start_x = from_pt.x;
    anim.start_y = from_pt.y;
    anim.end_x = to_pt.x;
    anim.end_y = to_pt.y;
    anim.color = TileColor(tile);
    anim.alpha_start = anim.alpha_end = 255.0f;
    anim.size_start = anim.size_end = 1.0f;
    return anim;
}

Animation MakePopAnimation(const Layout& layout,
                           const match::core::Cell& cell,
                           int tile,
                           float duration_ms) {
    Animation anim;
    anim.type = Animation::Type::Pop;
    anim.duration_ms = duration_ms;
    anim.delay_ms = 0.0f;
    SDL_FPoint pos = CellCenter(layout, cell);
    anim.start_x = pos.x;
    anim.start_y = pos.y;
    anim.end_x = pos.x;
    anim.end_y = pos.y;
    anim.size_start = 1.0f;
    anim.size_end = 0.4f;
    anim.alpha_start = 255.0f;
    anim.alpha_end = 0.0f;
    anim.color = TileColor(tile);
    anim.reveal_cell = cell;
    return anim;
}

Animation MakeFallAnimation(const Layout& layout,
                            const match::core::Cell& from,
                            const match::core::Cell& to,
                            int tile,
                            float duration_ms) {
    Animation anim;
    anim.type = Animation::Type::Fall;
    anim.duration_ms = duration_ms;
    anim.delay_ms = 0.0f;
    SDL_FPoint from_pt = CellCenter(layout, from);
    SDL_FPoint to_pt = CellCenter(layout, to);
    anim.start_x = from_pt.x;
    anim.start_y = from_pt.y;
    anim.end_x = to_pt.x;
    anim.end_y = to_pt.y;
    anim.size_start = anim.size_end = 1.0f;
    anim.alpha_start = anim.alpha_end = 255.0f;
    anim.color = TileColor(tile);
    anim.reveal_cell = to;
    return anim;
}

Animation MakeSpawnAnimation(const Layout& layout,
                             const match::core::Cell& cell,
                             int tile,
                             int distance_cells,
                             float duration_ms) {
    Animation anim;
    anim.type = Animation::Type::Spawn;
    anim.duration_ms = duration_ms;
    anim.delay_ms = 0.0f;
    SDL_FPoint target = CellCenter(layout, cell);
    anim.start_x = target.x;
    anim.start_y = target.y - distance_cells * layout.cell_size;
    anim.end_x = target.x;
    anim.end_y = target.y;
    anim.size_start = anim.size_end = 1.0f;
    anim.alpha_start = 200.0f;
    anim.alpha_end = 255.0f;
    anim.color = TileColor(tile);
    anim.reveal_cell = cell;
    return anim;
}

void UpdateAnimations(std::vector<Animation>& animations,
                      std::set<match::core::Cell>& hidden_cells,
                      float delta_ms) {
    for (auto& anim : animations) {
        anim.elapsed_ms += delta_ms;
    }

    auto it = animations.begin();
    while (it != animations.end()) {
        if (it->finished()) {
            if (it->reveal_cell) {
                hidden_cells.erase(*it->reveal_cell);
            }
            it = animations.erase(it);
        } else {
            ++it;
        }
    }
}

void DrawBoard(SDL_Renderer* renderer,
               const BoardRenderData& board_data,
               const Layout& layout) {
    const int cols = board_data.board.cols();
    const int rows = board_data.board.rows();

    struct HighlightTile {
        SDL_FPoint center;
        SDL_Color color;
        float scale;
        bool draw_outline;
    };
    std::vector<HighlightTile> highlight_tiles;
    highlight_tiles.reserve(8);

    SDL_SetRenderDrawColor(renderer, 255, 255, 255,
                           static_cast<Uint8>(layout.grid_line_alpha));
    for (int c = 0; c <= cols; ++c) {
        const int x = static_cast<int>(layout.board_left + c * layout.cell_size);
        SDL_RenderDrawLine(renderer, x, static_cast<int>(layout.board_top),
                           x, static_cast<int>(layout.board_top + layout.cell_size * rows));
    }
    for (int r = 0; r <= rows; ++r) {
        const int y = static_cast<int>(layout.board_top + r * layout.cell_size);
        SDL_RenderDrawLine(renderer, static_cast<int>(layout.board_left), y,
                           static_cast<int>(layout.board_left + layout.cell_size * cols), y);
    }

    for (int c = 0; c < cols; ++c) {
        for (int r = 0; r < rows; ++r) {
            match::core::Cell cell{c, r};
            if (board_data.hidden_cells.find(cell) != board_data.hidden_cells.end()) {
                continue;
            }
            const int tile = board_data.board.get(c, r);
            if (tile == match::core::kEmptyCell) {
                continue;
            }

            const bool is_selected = board_data.selected && *board_data.selected == cell;
            const bool is_hover =
                !is_selected && board_data.hover && *board_data.hover == cell;
            const bool is_controller =
                !is_selected && board_data.controller_cursor &&
                *board_data.controller_cursor == cell;

            const float scale = (is_hover || is_controller) ? 1.2f : 1.0f;
            const SDL_FPoint center = CellCenter(layout, cell);
            const float base = layout.cell_size - 2.0f * layout.cell_inset;
            float half = 0.5f * base * scale;

            const float left_limit = layout.board_left + layout.cell_inset;
            const float right_limit =
                layout.board_left + cols * layout.cell_size - layout.cell_inset;
            const float top_limit = layout.board_top + layout.cell_inset;
            const float bottom_limit =
                layout.board_top + rows * layout.cell_size - layout.cell_inset;

            float max_half_x =
                std::min(center.x - left_limit, right_limit - center.x);
            float max_half_y =
                std::min(center.y - top_limit, bottom_limit - center.y);
            float allowed_half =
                std::max(0.0f, std::min(half, std::min(max_half_x, max_half_y)));
            if (allowed_half <= 0.0f) {
                continue;
            }

            SDL_Color color = SDLColorConverter::Convert(TileColor(tile));
            const bool highlight = is_selected || is_hover || is_controller;
            if (highlight) {
                highlight_tiles.push_back(HighlightTile{center, color, scale, true});
                continue;
            }

            SDL_FRect rect = MakeRect(center.x, center.y, allowed_half);
            SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 255);
            SDL_RenderFillRectF(renderer, &rect);
        }
    }

    if (!highlight_tiles.empty()) {
        const float board_left = layout.board_left;
        const float board_top = layout.board_top;
        const float board_right = layout.board_left + cols * layout.cell_size;
        const float board_bottom = layout.board_top + rows * layout.cell_size;
        const float base = layout.cell_size - 2.0f * layout.cell_inset;

        for (const auto& highlight : highlight_tiles) {
            float half = 0.5f * base * highlight.scale;
            float max_half_x =
                std::min(highlight.center.x - board_left, board_right - highlight.center.x);
            float max_half_y =
                std::min(highlight.center.y - board_top, board_bottom - highlight.center.y);
            float allowed_half =
                std::max(0.0f, std::min(half, std::min(max_half_x, max_half_y)));
            if (allowed_half <= 0.0f) {
                continue;
            }

            SDL_FRect rect =
                MakeRect(highlight.center.x, highlight.center.y, allowed_half);
            SDL_SetRenderDrawColor(renderer, highlight.color.r, highlight.color.g,
                                   highlight.color.b, 255);
            SDL_RenderFillRectF(renderer, &rect);

            if (highlight.draw_outline) {
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 200);
                SDL_RenderDrawRectF(renderer, &rect);
            }
        }
    }
}

void DrawAnimations(SDL_Renderer* renderer,
                    const std::vector<Animation>& animations,
                    const Layout& layout) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    const float base = layout.cell_size - 2.0f * layout.cell_inset;

    for (const auto& anim : animations) {
        if (!anim.started()) {
            continue;
        }
        const float ease = anim.ease();
        const float x = anim.start_x + (anim.end_x - anim.start_x) * ease;
        const float y = anim.start_y + (anim.end_y - anim.start_y) * ease;
        const float size = anim.size_start + (anim.size_end - anim.size_start) * ease;
        const float alpha_f =
            anim.alpha_start + (anim.alpha_end - anim.alpha_start) * ease;
        const float half = 0.5f * base * size;
        SDL_FRect rect = MakeRect(x, y, half);
        SDL_Color color = SDLColorConverter::Convert(anim.color);
        color.a = static_cast<Uint8>(std::clamp(alpha_f, 0.0f, 255.0f));

        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
        SDL_RenderFillRectF(renderer, &rect);
    }
}

void DrawPanel(SDL_Renderer* renderer,
               const Layout& layout,
               const Fonts& fonts,
               const PanelInfo& panel) {
    const SDL_Color heading_color{210, 215, 225, 255};
    const SDL_Color value_color{235, 240, 245, 255};
    const SDL_Color detail_color{170, 180, 190, 255};
    const SDL_Color highlight_color{255, 255, 255, 160};

    TTF_Font* heading_font = fonts.heading ? fonts.heading : fonts.body;
    TTF_Font* body_font = fonts.body ? fonts.body : fonts.small;
    TTF_Font* small_font =
        fonts.small ? fonts.small : (fonts.body ? fonts.body : fonts.heading);

    int x = static_cast<int>(layout.panel_left);
    int y = static_cast<int>(layout.panel_top);
    const int wrap_width = std::max(40, static_cast<int>(layout.panel_wrap));

    auto add_line = [&](TTF_Font* font, const std::string& text, SDL_Color color, int spacing) {
        if (!font || text.empty()) {
            return;
        }
        int height = RenderTextLine(renderer, font, x, y, text, color);
        y += height + spacing;
    };

    auto label_value = [&](const std::string& label, const std::string& value) {
        if (!body_font) {
            return;
        }
        std::string text = label + " " + value;
        add_line(body_font, text, value_color, 12);
    };

    label_value("MODE:", panel.mode);
    auto section_heading = [&](const std::string& text, int spacing) {
        add_line(heading_font, text, heading_color, spacing);
    };

    if (panel.show_turn_timer) {
        section_heading("Turn Timer:", 6);
        add_line(body_font, FormatTimerString(panel.turn_timer_ms), value_color, 18);
        y += 6;
    }

    section_heading("Round Info:", 6);
    label_value("ROUND:", panel.round);
    label_value("ORDER:", panel.order);
    label_value("MOVES LEFT:", std::to_string(panel.moves_left));
    y += 12;

    section_heading("Settings:", 6);
    label_value("MODE:", panel.mode);
    label_value("BOMBS:", panel.bombs_enabled ? "On" : "Off");
    label_value("COLOR BLAST:", panel.color_blast_enabled ? "On" : "Off");

    y += 12;
    section_heading("Scores:", 8);

    for (const auto& entry : panel.players) {
        std::string line = entry.name + ": " + std::to_string(entry.score);
        if (entry.moves_left >= 0) {
            line += "  (Moves: " + std::to_string(entry.moves_left) + ")";
        }
        auto [line_width, line_height] = MeasureText(body_font, line);
        if (entry.active && line_width > 0 && line_height > 0) {
            SDL_Rect box{x - 6, y - 4, line_width + 12, line_height + 8};
            SDL_SetRenderDrawColor(renderer, highlight_color.r, highlight_color.g,
                                   highlight_color.b, highlight_color.a);
            SDL_RenderDrawRect(renderer, &box);
        }
        add_line(body_font, line, value_color, 8);
    }

    y += 12;
    section_heading("Status:", 6);
    std::string status_text = panel.status.empty() ? "Ready" : panel.status;
    if (body_font) {
        int height = RenderWrappedText(renderer, body_font, x, y, status_text, value_color, wrap_width);
        y += height + 16;
    }
    if (panel.show_pre_turn) {
        add_line(heading_font, "Starting In:", heading_color, 6);
        add_line(body_font,
                 std::to_string(std::max(0, panel.pre_turn_seconds)) + " s",
                 value_color,
                 16);
    }
    add_line(heading_font, "Controls:", heading_color, 6);
    for (const auto& line : panel.controls) {
        if (!small_font) {
            break;
        }
        int height = RenderWrappedText(renderer, small_font, x, y, line, detail_color, wrap_width);
        y += height + 6;
    }
}

// Helper function defined above must be declared before use.
}  // namespace match::render
