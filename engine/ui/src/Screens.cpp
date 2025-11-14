#include "match/ui/Screens.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace match::ui {

namespace {

struct UiMetrics {
    int window_w = 0;
    int window_h = 0;
    float scale = 1.0f;
};

constexpr float kReferenceWidth = 1920.0f;
constexpr float kReferenceHeight = 1080.0f;

const SDL_Color kBackground{26, 27, 32, 255};
const SDL_Color kPanelFill{40, 42, 50, 240};
const SDL_Color kPanelBorder{84, 86, 96, 255};
const SDL_Color kButtonFill{48, 50, 60, 255};
const SDL_Color kButtonHighlight{255, 205, 64, 255};
const SDL_Color kButtonText{20, 22, 26, 255};
const SDL_Color kTextPrimary{236, 238, 244, 255};
const SDL_Color kTextSecondary{164, 170, 189, 255};
const SDL_Color kHintBarFill{20, 21, 24, 255};
const SDL_Color kHintBarBorder{86, 88, 96, 255};
constexpr int kAxisThreshold = 16000;
constexpr Uint32 kAxisRepeatDelayMs = 150;
constexpr int kOskLetterRows = 3;
constexpr int kOskMaxLettersPerRow = 9;

bool ShouldProcessAxisRepeat(int dir, int& stored_dir, Uint32& stored_tick) {
    Uint32 now = SDL_GetTicks();
    if (dir == 0) {
        stored_dir = 0;
        stored_tick = 0;
        return false;
    }
    if (dir != stored_dir || now - stored_tick >= kAxisRepeatDelayMs) {
        stored_dir = dir;
        stored_tick = now;
        return true;
    }
    return false;
}

bool PointInRect(const SDL_Rect& rect, int x, int y) {
    return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
}

int WrapIndex(int value, int count) {
    if (count <= 0) {
        return 0;
    }
    value %= count;
    if (value < 0) {
        value += count;
    }
    return value;
}

void MoveSelection(int& selected, int delta, int count) {
    if (count <= 0) {
        selected = 0;
        return;
    }
    selected = WrapIndex(selected + delta, count);
}

int ClampSelection(int selected, int count) {
    if (count <= 0) {
        return 0;
    }
    if (selected < 0) {
        return 0;
    }
    if (selected >= count) {
        return count - 1;
    }
    return selected;
}

UiMetrics ComputeUiMetrics(int window_w, int window_h) {
    UiMetrics metrics;
    metrics.window_w = std::max(window_w, 1);
    metrics.window_h = std::max(window_h, 1);
    const float scale_w = static_cast<float>(metrics.window_w) / kReferenceWidth;
    const float scale_h = static_cast<float>(metrics.window_h) / kReferenceHeight;
    metrics.scale = std::max(0.6f, std::min(scale_w, scale_h));
    return metrics;
}

int UiPx(const UiMetrics& metrics, float logical_px) {
    return static_cast<int>(std::round(logical_px * metrics.scale));
}

SDL_Rect PanelCentered(const UiMetrics& metrics,
                       float logical_width,
                       float logical_height,
                       float logical_offset_y = 0.0f) {
    const int width = UiPx(metrics, logical_width);
    const int height = UiPx(metrics, logical_height);
    const int offset_y = UiPx(metrics, logical_offset_y);
    SDL_Rect rect{};
    rect.w = width;
    rect.h = height;
    rect.x = (metrics.window_w - width) / 2;
    rect.y = ((metrics.window_h - height) / 2) + offset_y;
    return rect;
}

int TextWidth(TTF_Font* font, const std::string& text) {
    if (!font || text.empty()) {
        return 0;
    }
    int w = 0;
    int h = 0;
    if (TTF_SizeUTF8(font, text.c_str(), &w, &h) == 0) {
        return w;
    }
    return 0;
}

std::string FitTextToWidth(TTF_Font* font, const std::string& text, int max_width) {
    if (!font || text.empty() || max_width <= 0) {
        return text;
    }
    if (TextWidth(font, text) <= max_width) {
        return text;
    }
    static constexpr const char* kEllipsis = "...";
    const int ellipsis_width = TextWidth(font, kEllipsis);
    if (ellipsis_width >= max_width) {
        return ".";
    }
    std::string trimmed;
    for (char ch : text) {
        trimmed.push_back(ch);
        if (TextWidth(font, trimmed) + ellipsis_width > max_width) {
            trimmed.pop_back();
            break;
        }
    }
    if (trimmed.empty()) {
        return kEllipsis;
    }
    trimmed += kEllipsis;
    return trimmed;
}

void DrawPanel(SDL_Renderer* renderer,
               const SDL_Rect& rect,
               const SDL_Color& fill,
               const SDL_Color& border) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, &rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &rect);
}

void RenderText(SDL_Renderer* renderer,
                TTF_Font* font,
                int x,
                int y,
                const std::string& text,
                SDL_Color color) {
    if (!renderer || !font || text.empty()) {
        return;
    }
    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!surface) {
        return;
    }
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_Rect dst{x, y, surface->w, surface->h};
    SDL_FreeSurface(surface);
    if (!texture) {
        return;
    }
    SDL_RenderCopy(renderer, texture, nullptr, &dst);
    SDL_DestroyTexture(texture);
}

void RenderFittedText(SDL_Renderer* renderer,
                      TTF_Font* font,
                      int x,
                      int y,
                      int max_width,
                      const std::string& text,
                      SDL_Color color) {
    RenderText(renderer, font, x, y, FitTextToWidth(font, text, max_width), color);
}

bool CursorBlinkVisible() {
    return ((SDL_GetTicks() / 450) % 2) == 0;
}

int FontPixelHeight(TTF_Font* font) {
    return font ? TTF_FontHeight(font) : 0;
}

void DrawCaret(SDL_Renderer* renderer, int x, int y, int height, SDL_Color color) {
    if (!renderer || height <= 0) {
        return;
    }
    SDL_Rect caret{
        x,
        y,
        2,
        height,
    };
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, &caret);
}

void RenderTextWithCursor(SDL_Renderer* renderer,
                          TTF_Font* font,
                          int x,
                          int y,
                          int max_width,
                          const std::string& text,
                          SDL_Color color,
                          bool show_cursor,
                          int caret_offset_y,
                          int caret_height) {
    if (!font) {
        return;
    }
    std::string fitted = FitTextToWidth(font, text, max_width);
    RenderText(renderer, font, x, y, fitted, color);
    if (show_cursor && CursorBlinkVisible()) {
        int width = TextWidth(font, fitted);
        DrawCaret(renderer, x + width + 2, y + caret_offset_y, caret_height, color);
    }
}

void RenderTextCenteredInRect(SDL_Renderer* renderer,
                              TTF_Font* font,
                              const SDL_Rect& rect,
                              const std::string& text,
                              SDL_Color color) {
    if (!renderer || !font || text.empty()) {
        return;
    }
    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!surface) {
        return;
    }
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!texture) {
        SDL_FreeSurface(surface);
        return;
    }
    SDL_Rect dst;
    dst.w = surface->w;
    dst.h = surface->h;
    dst.x = rect.x + (rect.w - dst.w) / 2;
    dst.y = rect.y + (rect.h - dst.h) / 2;
    if (dst.x < rect.x) {
        dst.x = rect.x;
    }
    if (dst.y < rect.y) {
        dst.y = rect.y;
    }
    SDL_Rect prev_clip;
    SDL_bool had_clip = SDL_RenderIsClipEnabled(renderer);
    if (had_clip) {
        SDL_RenderGetClipRect(renderer, &prev_clip);
    }
    SDL_RenderSetClipRect(renderer, &rect);
    SDL_RenderCopy(renderer, texture, nullptr, &dst);
    if (had_clip) {
        SDL_RenderSetClipRect(renderer, &prev_clip);
    } else {
        SDL_RenderSetClipRect(renderer, nullptr);
    }
    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
}

void RenderCenteredText(SDL_Renderer* renderer,
                        TTF_Font* font,
                        int window_width,
                        int y,
                        const std::string& text,
                        SDL_Color color) {
    if (!font || text.empty()) {
        return;
    }
    int width = TextWidth(font, text);
    int x = (window_width - width) / 2;
    RenderText(renderer, font, x, y, text, color);
}

void DrawControlHint(SDL_Renderer* renderer,
                     const match::render::Fonts& fonts,
                     const UiMetrics& metrics,
                     const SDL_Rect& panel,
                     const std::string& text) {
    if (text.empty()) {
        return;
    }
    TTF_Font* font = fonts.small ? fonts.small : fonts.body;
    if (!font) {
        return;
    }
    const int max_width = panel.w - UiPx(metrics, 16.0f);
    RenderFittedText(renderer,
                     font,
                     panel.x + UiPx(metrics, 8.0f),
                     panel.y + panel.h + UiPx(metrics, 16.0f),
                     max_width,
                     text,
                     kTextSecondary);
}

void DrawInfoBar(SDL_Renderer* renderer,
                 const match::render::Fonts& fonts,
                 int window_width,
                 int window_height,
                 const std::string& text) {
    UiMetrics metrics = ComputeUiMetrics(window_width, window_height);
    const int height = UiPx(metrics, 56.0f);
    SDL_Rect bar{UiPx(metrics, 120.0f),
                 window_height - height - UiPx(metrics, 32.0f),
                 window_width - UiPx(metrics, 240.0f),
                 height};
    DrawPanel(renderer, bar, kHintBarFill, kHintBarBorder);
    TTF_Font* font = fonts.small ? fonts.small : fonts.body;
    if (font) {
        int w = 0;
        int h = 0;
        if (TTF_SizeUTF8(font, text.c_str(), &w, &h) != 0) {
            w = 0;
            h = TTF_FontHeight(font);
        }
        SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), kTextSecondary);
        if (surface) {
            SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
            if (texture) {
                SDL_Rect dst;
                dst.w = std::min(bar.w - UiPx(metrics, 32.0f), surface->w);
                dst.h = surface->h;
                dst.x = bar.x + (bar.w - dst.w) / 2;
                dst.y = bar.y + (bar.h - dst.h) / 2;
                SDL_RenderCopy(renderer, texture, nullptr, &dst);
                SDL_DestroyTexture(texture);
            }
            SDL_FreeSurface(surface);
        }
    }
}

void DrawScrollbar(SDL_Renderer* renderer,
                   const UiMetrics& metrics,
                   const SDL_Rect& area,
                   int total_items,
                   int visible_items,
                   int first_visible) {
    if (total_items <= visible_items || visible_items <= 0) {
        return;
    }
    SDL_Color track_color{30, 32, 38, 255};
    SDL_Color knob_color{255, 205, 64, 220};
    SDL_SetRenderDrawColor(renderer, kPanelBorder.r, kPanelBorder.g, kPanelBorder.b, kPanelBorder.a);
    SDL_RenderDrawRect(renderer, &area);
    SDL_Rect track{area.x + 1, area.y + 1, area.w - 2, area.h - 2};
    SDL_SetRenderDrawColor(renderer, track_color.r, track_color.g, track_color.b, track_color.a);
    SDL_RenderFillRect(renderer, &track);
    const float visible_ratio = static_cast<float>(visible_items) / static_cast<float>(total_items);
    int knob_h = std::max(UiPx(metrics, 20.0f), static_cast<int>(track.h * visible_ratio));
    knob_h = std::min(knob_h, track.h);
    const int max_offset = std::max(1, total_items - visible_items);
    float scroll_ratio = static_cast<float>(first_visible) / static_cast<float>(max_offset);
    int knob_y = track.y + static_cast<int>((track.h - knob_h) * scroll_ratio);
    SDL_Rect knob{track.x, knob_y, track.w, knob_h};
    SDL_SetRenderDrawColor(renderer, knob_color.r, knob_color.g, knob_color.b, knob_color.a);
    SDL_RenderFillRect(renderer, &knob);
}

}  // namespace

namespace {

MenuAction MenuActionForIndex(int index) {
    switch (index) {
        case 0:
            return MenuAction::StartPvC;
        case 1:
            return MenuAction::StartPvP;
        case 2:
            return MenuAction::StartTournament;
        case 3:
            return MenuAction::Settings;
        case 4:
            return MenuAction::Quit;
        default:
            return MenuAction::None;
    }
}

int MenuCount() {
    return 5;
}

void UpdateMenuSelection(MenuState& state, int delta) {
    MoveSelection(state.selected, delta, MenuCount());
}

}  // namespace

MenuAction HandleMenuEvent(MenuState& state,
                           const match::platform::InputEvent& evt,
                           bool /*using_controller*/,
                           int /*window_width*/,
                           int /*window_height*/) {
    state.selected = ClampSelection(state.selected, MenuCount());
    auto activate = [&]() -> MenuAction { return MenuActionForIndex(state.selected); };

    switch (evt.type) {
        case match::platform::InputEventType::MouseMove: {
            for (int i = 0; i < MenuCount(); ++i) {
                if (PointInRect(state.button_bounds[static_cast<std::size_t>(i)], evt.x, evt.y)) {
                    state.selected = i;
                    break;
                }
            }
            break;
        }
        case match::platform::InputEventType::MouseButtonDown: {
            if (evt.mouse_button == match::platform::MouseButton::Left) {
                for (int i = 0; i < MenuCount(); ++i) {
                    if (PointInRect(state.button_bounds[static_cast<std::size_t>(i)], evt.x, evt.y)) {
                        state.selected = i;
                        return activate();
                    }
                }
            } else if (evt.mouse_button == match::platform::MouseButton::Right) {
                return MenuAction::Quit;
            }
            break;
        }
        case match::platform::InputEventType::KeyDown: {
            using match::platform::KeyCode;
            if (evt.key == KeyCode::Down || evt.key == KeyCode::S) {
                UpdateMenuSelection(state, +1);
            } else if (evt.key == KeyCode::Up || evt.key == KeyCode::W) {
                UpdateMenuSelection(state, -1);
            } else if (evt.key == KeyCode::Enter) {
                return activate();
            } else if (evt.key == KeyCode::Escape) {
                return MenuAction::Quit;
            }
            break;
        }
        case match::platform::InputEventType::ControllerButtonDown: {
            using match::platform::ControllerButton;
            switch (evt.controller_button) {
                case ControllerButton::DPadDown:
                    UpdateMenuSelection(state, +1);
                    break;
                case ControllerButton::DPadUp:
                    UpdateMenuSelection(state, -1);
                    break;
                case ControllerButton::A:
                    return activate();
                case ControllerButton::B:
                case ControllerButton::Menu:
                    return MenuAction::Quit;
                default:
                    break;
            }
            break;
        }
        case match::platform::InputEventType::ControllerAxisMotion: {
            using match::platform::ControllerAxis;
            if (evt.controller_axis == ControllerAxis::LeftY) {
                int dir = 0;
                if (evt.axis_value > kAxisThreshold) {
                    dir = +1;
                } else if (evt.axis_value < -kAxisThreshold) {
                    dir = -1;
                }
                if (ShouldProcessAxisRepeat(dir, state.axis_vertical, state.axis_vertical_tick)) {
                    UpdateMenuSelection(state, dir);
                }
                if (dir == 0) {
                    state.axis_vertical = 0;
                    state.axis_vertical_tick = 0;
                }
            }
            break;
        }
        default:
            break;
    }

    return MenuAction::None;
}

void RenderMenu(SDL_Renderer* renderer,
                const match::render::Fonts& fonts,
                int window_width,
                int window_height,
                MenuState& state,
                bool using_controller) {
    state.selected = ClampSelection(state.selected, MenuCount());
    UiMetrics metrics = ComputeUiMetrics(window_width, window_height);
    SDL_SetRenderDrawColor(renderer, kBackground.r, kBackground.g, kBackground.b, 255);
    SDL_RenderClear(renderer);

    SDL_Rect panel = PanelCentered(metrics, 720.0f, 640.0f);
    DrawPanel(renderer, panel, kPanelFill, kPanelBorder);

    TTF_Font* title_font = fonts.heading ? fonts.heading : fonts.body;
    TTF_Font* body_font = fonts.body ? fonts.body : fonts.heading;

    if (title_font) {
        RenderCenteredText(renderer, title_font, window_width, panel.y - UiPx(metrics, 40.0f), "MATCH", kTextPrimary);
    }

    const std::array<std::string, 5> labels{
        "Player vs Computer",
        "Player vs Player",
        "Tournament",
        "Settings",
        "Quit"};
    const std::array<std::string, 5> descriptions{
        "Face an AI opponent with custom settings",
        "Play locally against another player",
        "Run a multi-player tournament",
        "Adjust display and gameplay options",
        "Close MATCH"};

    int y = panel.y + UiPx(metrics, 100.0f);
    const int button_height = UiPx(metrics, 82.0f);
    const int button_gap = UiPx(metrics, 24.0f);
    const int button_left = panel.x + UiPx(metrics, 48.0f);
    const int button_width = panel.w - UiPx(metrics, 96.0f);

    for (int i = 0; i < MenuCount(); ++i) {
        SDL_Rect rect{button_left, y, button_width, button_height};
        state.button_bounds[static_cast<std::size_t>(i)] = rect;
        const bool selected = state.selected == i;
        const SDL_Color fill = selected ? kButtonHighlight : kButtonFill;
        const SDL_Color text = selected ? kButtonText : kTextPrimary;
        DrawPanel(renderer, rect, fill, kPanelBorder);
        if (body_font) {
            RenderFittedText(renderer,
                             body_font,
                             rect.x + UiPx(metrics, 24.0f),
                             rect.y + UiPx(metrics, 18.0f),
                             rect.w - UiPx(metrics, 48.0f),
                             labels[static_cast<std::size_t>(i)],
                             text);
        }
        y += button_height + button_gap;
    }

    const std::string control_hint =
        using_controller ? "D-Pad/Stick move | A select | B/Menu back"
                         : "Mouse click | Enter select | Esc/Right-click back";
    DrawControlHint(renderer, fonts, metrics, panel, control_hint);

    DrawInfoBar(renderer, fonts, window_width, window_height,
                descriptions[static_cast<std::size_t>(state.selected)]);
}

void RenderTimeMode(SDL_Renderer* renderer,
                    const match::render::Fonts& fonts,
                    int window_width,
                    int window_height,
                    TimeModeState& state,
                    bool using_controller) {
    state.selected = std::clamp(state.selected, 0, 2);
    UiMetrics metrics = ComputeUiMetrics(window_width, window_height);
    SDL_SetRenderDrawColor(renderer, kBackground.r, kBackground.g, kBackground.b, 255);
    SDL_RenderClear(renderer);

    SDL_Rect panel = PanelCentered(metrics, 760.0f, 560.0f, -30.0f);
    DrawPanel(renderer, panel, kPanelFill, kPanelBorder);

    TTF_Font* title_font = fonts.heading ? fonts.heading : fonts.body;
    TTF_Font* body_font = fonts.body ? fonts.body : fonts.heading;
    if (title_font) {
        RenderCenteredText(renderer, title_font, window_width, panel.y - UiPx(metrics, 50.0f), "Select Time Mode",
                           kTextPrimary);
    }

    const int button_left = panel.x + UiPx(metrics, 36.0f);
    const int button_width = panel.w - UiPx(metrics, 72.0f);
    int y = panel.y + UiPx(metrics, 100.0f);
    const int button_height = UiPx(metrics, 86.0f);
    const int button_gap = UiPx(metrics, 18.0f);

    const std::array<std::string, 3> labels{"Classic", "Blitz", "Back"};
    const std::array<std::string, 3> descriptions{
        "Play without timers for a relaxed experience",
        "Enable timed turns with the blitz clock",
        "Return to the previous menu"};

    for (int i = 0; i < 3; ++i) {
        SDL_Rect button{button_left, y, button_width, button_height};
        state.button_bounds[static_cast<std::size_t>(i)] = button;
        const bool selected = state.selected == i;
        const SDL_Color fill = selected ? kButtonHighlight : kButtonFill;
        const SDL_Color text_color = selected ? kButtonText : kTextPrimary;
        DrawPanel(renderer, button, fill, kPanelBorder);

        if (body_font) {
            RenderFittedText(renderer,
                             body_font,
                             button.x + UiPx(metrics, 18.0f),
                             button.y + UiPx(metrics, 24.0f),
                             button.w - UiPx(metrics, 36.0f),
                             labels[static_cast<std::size_t>(i)],
                             text_color);
        }

        y += button_height + button_gap;
    }

    const std::string control_hint =
        using_controller ? "D-Pad/Stick move | A select | B back" : "Mouse click | Enter select | Esc back";
    DrawControlHint(renderer, fonts, metrics, panel, control_hint);
    DrawInfoBar(renderer,
                fonts,
                window_width,
                window_height,
                descriptions[static_cast<std::size_t>(state.selected)]);
}

TimeModeAction HandleTimeModeEvent(TimeModeState& state,
                                   const match::platform::InputEvent& evt,
                                   bool /*using_controller*/) {
    const int total = 3;
    state.selected = ClampSelection(state.selected, total);
    auto activate = [&]() -> TimeModeAction {
        switch (state.selected) {
            case 0:
                return TimeModeAction::Classic;
            case 1:
                return TimeModeAction::Blitz;
            default:
                return TimeModeAction::Back;
        }
    };

    switch (evt.type) {
        case match::platform::InputEventType::MouseMove: {
            for (int i = 0; i < total; ++i) {
                if (PointInRect(state.button_bounds[static_cast<std::size_t>(i)], evt.x, evt.y)) {
                    state.selected = i;
                    break;
                }
            }
            break;
        }
        case match::platform::InputEventType::MouseButtonDown: {
            if (evt.mouse_button == match::platform::MouseButton::Left) {
                for (int i = 0; i < total; ++i) {
                    if (PointInRect(state.button_bounds[static_cast<std::size_t>(i)], evt.x, evt.y)) {
                        state.selected = i;
                        return activate();
                    }
                }
            } else if (evt.mouse_button == match::platform::MouseButton::Right) {
                return TimeModeAction::Back;
            }
            break;
        }
        case match::platform::InputEventType::KeyDown: {
            using match::platform::KeyCode;
            if (evt.key == KeyCode::Down || evt.key == KeyCode::S) {
                MoveSelection(state.selected, +1, total);
            } else if (evt.key == KeyCode::Up || evt.key == KeyCode::W) {
                MoveSelection(state.selected, -1, total);
            } else if (evt.key == KeyCode::Enter) {
                return activate();
            } else if (evt.key == KeyCode::Escape) {
                return TimeModeAction::Back;
            }
            break;
        }
        case match::platform::InputEventType::ControllerButtonDown: {
            using match::platform::ControllerButton;
            switch (evt.controller_button) {
                case ControllerButton::DPadDown:
                    MoveSelection(state.selected, +1, total);
                    break;
                case ControllerButton::DPadUp:
                    MoveSelection(state.selected, -1, total);
                    break;
                case ControllerButton::A:
                    return activate();
                case ControllerButton::B:
                case ControllerButton::Menu:
                    return TimeModeAction::Back;
                default:
                    break;
            }
            break;
        }
        case match::platform::InputEventType::ControllerAxisMotion: {
            using match::platform::ControllerAxis;
            if (evt.controller_axis == ControllerAxis::LeftY) {
                int dir = 0;
                if (evt.axis_value > kAxisThreshold) {
                    dir = +1;
                } else if (evt.axis_value < -kAxisThreshold) {
                    dir = -1;
                }
                if (ShouldProcessAxisRepeat(dir, state.axis_vertical, state.axis_vertical_tick)) {
                    MoveSelection(state.selected, dir, total);
                }
                if (dir == 0) {
                    state.axis_vertical = 0;
                    state.axis_vertical_tick = 0;
                }
            }
            break;
        }
        default:
            break;
    }

    return TimeModeAction::None;
}

void RenderBlitzSettings(SDL_Renderer* renderer,
                         const match::render::Fonts& fonts,
                         int window_width,
                         int window_height,
                         BlitzSettingsState& state,
                         bool using_controller) {
    state.selected = std::clamp(state.selected, 0, 2);
    state.minutes = std::clamp(state.minutes, 1, 5);
    state.between_seconds = std::clamp(state.between_seconds, 0, 30);

    UiMetrics metrics = ComputeUiMetrics(window_width, window_height);
    SDL_SetRenderDrawColor(renderer, kBackground.r, kBackground.g, kBackground.b, 255);
    SDL_RenderClear(renderer);

    SDL_Rect panel = PanelCentered(metrics, 820.0f, 600.0f, -30.0f);
    DrawPanel(renderer, panel, kPanelFill, kPanelBorder);

    TTF_Font* title_font = fonts.heading ? fonts.heading : fonts.body;
    TTF_Font* body_font = fonts.body ? fonts.body : fonts.heading;
    if (title_font) {
        RenderCenteredText(renderer, title_font, window_width, panel.y - UiPx(metrics, 50.0f), "Blitz Settings",
                           kTextPrimary);
    }

    const int entry_left = panel.x + UiPx(metrics, 36.0f);
    const int entry_width = panel.w - UiPx(metrics, 72.0f);
    int y = panel.y + UiPx(metrics, 110.0f);
    const int entry_height = UiPx(metrics, 76.0f);
    const int entry_gap = UiPx(metrics, 20.0f);

    const std::array<std::string, 3> labels{
        "Minutes per turn",
        "Seconds between turns",
        "Continue"};
    const std::array<std::string, 3> descriptions{
        "Set the blitz timer for each player's turn",
        "Delay before the next player's timer starts",
        "Accept the blitz settings and continue"};

    for (int i = 0; i < 3; ++i) {
        SDL_Rect row{entry_left, y, entry_width, entry_height};
        state.button_bounds[static_cast<std::size_t>(i)] = row;
        const bool selected = state.selected == i;
        const SDL_Color fill = selected ? kButtonHighlight : kButtonFill;
        const SDL_Color text_color = selected ? kButtonText : kTextPrimary;
        DrawPanel(renderer, row, fill, kPanelBorder);

        if (body_font) {
            std::string label = labels[static_cast<std::size_t>(i)];
            if (i == 0) {
                label += ": " + std::to_string(state.minutes);
            } else if (i == 1) {
                label += ": " + std::to_string(state.between_seconds);
            }
            RenderFittedText(renderer,
                             body_font,
                             row.x + UiPx(metrics, 18.0f),
                             row.y + UiPx(metrics, 24.0f),
                             row.w - UiPx(metrics, 36.0f),
                             label,
                             text_color);
        }

        y += entry_height + entry_gap;
    }

    const std::string control_hint =
        using_controller ? "D-Pad/Stick move | X/Y adjust | A confirm | B back"
                         : "Mouse click | Left/Right click adjust | Enter confirm | Esc back";
    DrawControlHint(renderer, fonts, metrics, panel, control_hint);
    DrawInfoBar(renderer,
                fonts,
                window_width,
                window_height,
                descriptions[static_cast<std::size_t>(state.selected)]);
}

BlitzSettingsAction HandleBlitzSettingsEvent(BlitzSettingsState& state,
                                             const match::platform::InputEvent& evt,
                                             bool /*using_controller*/) {
    state.selected = ClampSelection(state.selected, 3);
    auto adjust = [&](int delta) {
        if (state.selected == 0) {
            state.minutes = std::clamp(state.minutes + delta, 1, 5);
        } else if (state.selected == 1) {
            state.between_seconds = std::clamp(state.between_seconds + delta, 0, 30);
        }
    };

    auto activate = [&]() -> BlitzSettingsAction {
        if (state.selected == 2) {
            return BlitzSettingsAction::Continue;
        }
        return BlitzSettingsAction::None;
    };

    switch (evt.type) {
        case match::platform::InputEventType::MouseMove: {
            for (int i = 0; i < 3; ++i) {
                if (PointInRect(state.button_bounds[static_cast<std::size_t>(i)], evt.x, evt.y)) {
                    state.selected = i;
                    break;
                }
            }
            break;
        }
        case match::platform::InputEventType::MouseButtonDown: {
            if (evt.mouse_button == match::platform::MouseButton::Left) {
                for (int i = 0; i < 3; ++i) {
                    if (PointInRect(state.button_bounds[static_cast<std::size_t>(i)], evt.x, evt.y)) {
                        state.selected = i;
                        if (state.selected <= 1) {
                            adjust(+1);
                            return BlitzSettingsAction::None;
                        }
                        return activate();
                    }
                }
            } else if (evt.mouse_button == match::platform::MouseButton::Right) {
                if (state.selected <= 1) {
                    adjust(-1);
                } else {
                    return BlitzSettingsAction::Back;
                }
            }
            break;
        }
        case match::platform::InputEventType::KeyDown: {
            using match::platform::KeyCode;
            if (evt.key == KeyCode::Down || evt.key == KeyCode::S) {
                MoveSelection(state.selected, +1, 3);
            } else if (evt.key == KeyCode::Up || evt.key == KeyCode::W) {
                MoveSelection(state.selected, -1, 3);
            } else if (evt.key == KeyCode::Left || evt.key == KeyCode::A) {
                adjust(-1);
            } else if (evt.key == KeyCode::Right || evt.key == KeyCode::D) {
                adjust(+1);
            } else if (evt.key == KeyCode::Enter) {
                return activate();
            } else if (evt.key == KeyCode::Escape) {
                return BlitzSettingsAction::Back;
            }
            break;
        }
        case match::platform::InputEventType::ControllerButtonDown: {
            using match::platform::ControllerButton;
            switch (evt.controller_button) {
                case ControllerButton::DPadDown:
                    MoveSelection(state.selected, +1, 3);
                    break;
                case ControllerButton::DPadUp:
                    MoveSelection(state.selected, -1, 3);
                    break;
                case ControllerButton::DPadLeft:
                case ControllerButton::X:
                    adjust(-1);
                    break;
                case ControllerButton::DPadRight:
                case ControllerButton::Y:
                    adjust(+1);
                    break;
                case ControllerButton::A:
                    return activate();
                case ControllerButton::B:
                    return BlitzSettingsAction::Back;
                default:
                    break;
            }
            break;
        }
        case match::platform::InputEventType::ControllerAxisMotion: {
            using match::platform::ControllerAxis;
            if (evt.controller_axis == ControllerAxis::LeftY) {
                int dir = 0;
                if (evt.axis_value > kAxisThreshold) {
                    dir = +1;
                } else if (evt.axis_value < -kAxisThreshold) {
                    dir = -1;
                }
                if (ShouldProcessAxisRepeat(dir, state.axis_vertical, state.axis_vertical_tick)) {
                    MoveSelection(state.selected, dir, 3);
                }
                if (dir == 0) {
                    state.axis_vertical = 0;
                    state.axis_vertical_tick = 0;
                }
            } else if (evt.controller_axis == ControllerAxis::LeftX) {
                int dir = 0;
                if (evt.axis_value > kAxisThreshold) {
                    dir = +1;
                } else if (evt.axis_value < -kAxisThreshold) {
                    dir = -1;
                }
                if (ShouldProcessAxisRepeat(dir, state.axis_horizontal, state.axis_horizontal_tick)) {
                    adjust(dir);
                }
                if (dir == 0) {
                    state.axis_horizontal = 0;
                    state.axis_horizontal_tick = 0;
                }
            }
            break;
        }
        default:
            break;
    }

    return BlitzSettingsAction::None;
}

NamePromptAction HandleNamePromptEvent(NamePromptState& state,
                                       const match::platform::InputEvent& evt,
                                       bool using_controller) {
    static constexpr std::size_t kMaxLength = 32;
    (void)using_controller;

    switch (evt.type) {
        case match::platform::InputEventType::TextInput: {
            if (!evt.text.empty()) {
                for (char ch : evt.text) {
                    if (std::iscntrl(static_cast<unsigned char>(ch))) {
                        continue;
                    }
                    if (state.input.size() < kMaxLength) {
                        state.input.push_back(ch);
                    }
                }
            }
            break;
        }
        case match::platform::InputEventType::KeyDown: {
            using match::platform::KeyCode;
            if (evt.key == KeyCode::Enter) {
                return NamePromptAction::Submit;
            }
            if (evt.key == KeyCode::Escape) {
                return NamePromptAction::Cancel;
            }
            if (evt.key == KeyCode::Backspace) {
                return NamePromptAction::Backspace;
            }
            break;
        }
        case match::platform::InputEventType::ControllerButtonDown: {
            using match::platform::ControllerButton;
            if (evt.controller_button == ControllerButton::A) {
                return NamePromptAction::Submit;
            }
            if (evt.controller_button == ControllerButton::B ||
                evt.controller_button == ControllerButton::Menu) {
                return NamePromptAction::Cancel;
            }
            if (evt.controller_button == ControllerButton::X) {
                return NamePromptAction::Backspace;
            }
            break;
        }
        case match::platform::InputEventType::MouseButtonDown: {
            if (evt.mouse_button == match::platform::MouseButton::Right) {
                return NamePromptAction::Cancel;
            }
            break;
        }
        default:
            break;
    }

    return NamePromptAction::None;
}

void RenderNamePrompt(SDL_Renderer* renderer,
                      const match::render::Fonts& fonts,
                      int window_width,
                      int window_height,
                      const NamePromptState& state) {
    UiMetrics metrics = ComputeUiMetrics(window_width, window_height);
    SDL_SetRenderDrawColor(renderer, kBackground.r, kBackground.g, kBackground.b, 255);
    SDL_RenderClear(renderer);

    SDL_Rect panel = PanelCentered(metrics, 820.0f, 420.0f, -30.0f);
    DrawPanel(renderer, panel, kPanelFill, kPanelBorder);

    TTF_Font* title_font = fonts.heading ? fonts.heading : fonts.body;
    TTF_Font* body_font = fonts.body ? fonts.body : fonts.heading;
    TTF_Font* hint_font = fonts.small ? fonts.small : fonts.body;

    if (title_font) {
        RenderCenteredText(renderer, title_font, window_width, panel.y - UiPx(metrics, 50.0f), "Save Name", kTextPrimary);
    }

    SDL_Rect input_rect{panel.x + UiPx(metrics, 32.0f),
                        panel.y + UiPx(metrics, 120.0f),
                        panel.w - UiPx(metrics, 64.0f),
                        UiPx(metrics, 80.0f)};
    SDL_SetRenderDrawColor(renderer, 32, 36, 46, 255);
    SDL_RenderFillRect(renderer, &input_rect);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 200);
    SDL_RenderDrawRect(renderer, &input_rect);

    if (body_font) {
        const int caret_height = std::max(8, FontPixelHeight(body_font) - UiPx(metrics, 12.0f));
        RenderTextWithCursor(renderer,
                             body_font,
                             input_rect.x + UiPx(metrics, 18.0f),
                             input_rect.y + UiPx(metrics, 20.0f),
                             input_rect.w - UiPx(metrics, 36.0f),
                             state.input,
                             kTextPrimary,
                             /*show_cursor=*/true,
                             UiPx(metrics, 4.0f),
                             caret_height);
    }

    const std::string control_hint =
        "Keyboard: Enter confirm | Backspace delete | Esc cancel   Controller: A confirm | X delete | B cancel";
    DrawControlHint(renderer, fonts, metrics, panel, control_hint);

    const std::string info = state.error.empty() ? "Enter a save name to keep your progress"
                                                 : ("Error: " + state.error);
    DrawInfoBar(renderer, fonts, window_width, window_height, info);
}

static int SaveSetupEntryCount(const SaveSetupState& state) {
    return static_cast<int>(state.summaries.size()) + 1;
}

void ClampSaveSetupScroll(SaveSetupState& state, int visible_count) {
    const int total = SaveSetupEntryCount(state);
    if (visible_count <= 0) {
        state.first_visible = 0;
        return;
    }
    state.selected_index = ClampSelection(state.selected_index, total);
    int max_first = std::max(0, total - visible_count);
    state.first_visible = std::clamp(state.first_visible, 0, max_first);
    if (state.selected_index < state.first_visible) {
        state.first_visible = state.selected_index;
    } else if (state.selected_index >= state.first_visible + visible_count) {
        state.first_visible = state.selected_index - visible_count + 1;
    }
}

int ComputeSaveSetupVisibleCount(const UiMetrics& metrics) {
    const int panel_height = UiPx(metrics, 720.0f);
    const int available = std::max(1, panel_height - UiPx(metrics, 240.0f));
    const int item_height = UiPx(metrics, 72.0f);
    const int item_gap = UiPx(metrics, 12.0f);
    return std::max(1, (available + item_gap) / (item_height + item_gap));
}

void RenderSaveSetup(SDL_Renderer* renderer,
                     const match::render::Fonts& fonts,
                     int window_width,
                     int window_height,
                     SaveSetupState& state,
                     bool using_controller) {
    UiMetrics metrics = ComputeUiMetrics(window_width, window_height);
    SDL_SetRenderDrawColor(renderer, kBackground.r, kBackground.g, kBackground.b, 255);
    SDL_RenderClear(renderer);

    SDL_Rect panel = PanelCentered(metrics, 1120.0f, 720.0f);
    DrawPanel(renderer, panel, kPanelFill, kPanelBorder);

    TTF_Font* title_font = fonts.heading ? fonts.heading : fonts.body;
    TTF_Font* body_font = fonts.body ? fonts.body : fonts.heading;
    TTF_Font* hint_font = fonts.small ? fonts.small : fonts.body;

    if (title_font) {
        RenderCenteredText(renderer,
                           title_font,
                           window_width,
                           panel.y - UiPx(metrics, 50.0f),
                           "Save Slots (" + ModeToString(state.mode) + ")",
                           kTextPrimary);
    }

    const int list_left = panel.x + UiPx(metrics, 32.0f);
    const int list_top = panel.y + UiPx(metrics, 96.0f);
    const int scrollbar_width = UiPx(metrics, 12.0f);
    const int list_width = panel.w - UiPx(metrics, 64.0f) - scrollbar_width;
    const int item_height = UiPx(metrics, 72.0f);
    const int item_gap = UiPx(metrics, 12.0f);
    const int list_height = std::max(1, panel.h - UiPx(metrics, 240.0f));

    const int total_entries = SaveSetupEntryCount(state);
    state.entry_bounds.resize(static_cast<std::size_t>(total_entries));
    std::fill(state.entry_bounds.begin(), state.entry_bounds.end(), SDL_Rect{0, 0, 0, 0});
    const int visible_entries = ComputeSaveSetupVisibleCount(metrics);
    ClampSaveSetupScroll(state, visible_entries);

    int y = list_top;
    for (int offset = 0; offset < visible_entries && state.first_visible + offset < total_entries; ++offset) {
        const int index = state.first_visible + offset;
        SDL_Rect rect{list_left, y, list_width, item_height};
        state.entry_bounds[static_cast<std::size_t>(index)] = rect;
        const bool selected = state.selected_index == index;
        const SDL_Color fill = selected ? kButtonHighlight : kButtonFill;
        const SDL_Color text_color = selected ? kButtonText : kTextPrimary;
        DrawPanel(renderer, rect, fill, kPanelBorder);

        if (body_font) {
            if (index == 0) {
                const char* label =
                    (state.mode == GameMode::Tournament) ? "Start a new tournament" : "Start a new save";
                RenderFittedText(renderer,
                                 body_font,
                                 rect.x + UiPx(metrics, 18.0f),
                                 rect.y + UiPx(metrics, 20.0f),
                                 rect.w - UiPx(metrics, 36.0f),
                                 label,
                                 text_color);
            } else {
                const auto& summary = state.summaries[static_cast<std::size_t>(index - 1)];
                RenderFittedText(renderer,
                                 body_font,
                                 rect.x + UiPx(metrics, 18.0f),
                                 rect.y + UiPx(metrics, 12.0f),
                                 rect.w - UiPx(metrics, 36.0f),
                                 summary.title.empty() ? summary.slot.name : summary.title,
                                 text_color);
            }
        }

        y += item_height + item_gap;
    }

    SDL_Rect scrollbar_area{list_left + list_width + UiPx(metrics, 8.0f),
                            list_top,
                            scrollbar_width,
                            list_height};
    DrawScrollbar(renderer, metrics, scrollbar_area, total_entries, visible_entries, state.first_visible);

    if (!state.error.empty() && hint_font) {
        RenderFittedText(renderer,
                         hint_font,
                         panel.x + UiPx(metrics, 32.0f),
                         panel.y + UiPx(metrics, 60.0f),
                         panel.w - UiPx(metrics, 64.0f),
                         "Error: " + state.error,
                         SDL_Color{255, 92, 92, 255});
    }

    const std::string control_hint =
        using_controller ? "D-Pad/Stick move | A/Enter open | B cancel"
                         : "Mouse hover/click | Enter double-click slot | Esc/Right-click cancel";
    DrawControlHint(renderer, fonts, metrics, panel, control_hint);

    std::string info_text;
    if (!state.error.empty()) {
        info_text = "Resolve the error above before continuing.";
    } else if (state.selected_index == 0) {
        info_text = (state.mode == GameMode::Tournament)
                        ? "Start a new tournament and choose its save name."
                        : "Start a new save file and choose its name.";
    } else if (state.selected_index - 1 < static_cast<int>(state.summaries.size())) {
        const auto& summary = state.summaries[static_cast<std::size_t>(state.selected_index - 1)];
        info_text = summary.title.empty() ? summary.slot.name : summary.title;
        if (!summary.detail_lines.empty()) {
            info_text += " — " + summary.detail_lines.front();
            for (std::size_t i = 1; i < summary.detail_lines.size(); ++i) {
                info_text += " | " + summary.detail_lines[i];
            }
        }
    }
    if (info_text.empty()) {
        info_text = "Select a slot and press Enter/A to view details.";
    }
    DrawInfoBar(renderer, fonts, window_width, window_height, info_text);
}

DisplaySettingsAction HandleDisplaySettingsEvent(DisplaySettingsState& state,
                                                 const match::platform::InputEvent& evt,
                                                 bool /*using_controller*/) {
    auto activate = [&]() { return DisplaySettingsAction::Toggle; };

    switch (evt.type) {
        case match::platform::InputEventType::MouseMove:
            if (PointInRect(state.button_bounds, evt.x, evt.y)) {
                state.selected = 0;
            }
            break;
        case match::platform::InputEventType::MouseButtonDown:
            if (evt.mouse_button == match::platform::MouseButton::Left) {
                if (PointInRect(state.button_bounds, evt.x, evt.y)) {
                    return activate();
                }
            } else if (evt.mouse_button == match::platform::MouseButton::Right) {
                return DisplaySettingsAction::Back;
            }
            break;
        case match::platform::InputEventType::KeyDown: {
            using match::platform::KeyCode;
            if (evt.key == KeyCode::Enter) {
                return activate();
            }
            if (evt.key == KeyCode::Escape) {
                return DisplaySettingsAction::Back;
            }
            break;
        }
        case match::platform::InputEventType::ControllerButtonDown: {
            using match::platform::ControllerButton;
            switch (evt.controller_button) {
                case ControllerButton::A:
                    return activate();
                case ControllerButton::B:
                case ControllerButton::Menu:
                    return DisplaySettingsAction::Back;
                default:
                    break;
            }
            break;
        }
        default:
            break;
    }
    return DisplaySettingsAction::None;
}

void RenderDisplaySettings(SDL_Renderer* renderer,
                           const match::render::Fonts& fonts,
                           int window_width,
                           int window_height,
                           DisplaySettingsState& state,
                           bool using_controller) {
    UiMetrics metrics = ComputeUiMetrics(window_width, window_height);
    SDL_SetRenderDrawColor(renderer, kBackground.r, kBackground.g, kBackground.b, 255);
    SDL_RenderClear(renderer);

    SDL_Rect panel = PanelCentered(metrics, 720.0f, 520.0f, -20.0f);
    DrawPanel(renderer, panel, kPanelFill, kPanelBorder);

    TTF_Font* title_font = fonts.heading ? fonts.heading : fonts.body;
    TTF_Font* body_font = fonts.body ? fonts.body : fonts.heading;

    if (title_font) {
        RenderCenteredText(renderer, title_font, window_width, panel.y - UiPx(metrics, 40.0f), "Display Settings",
                           kTextPrimary);
    }

    const int button_width = panel.w - UiPx(metrics, 96.0f);
    const int button_height = UiPx(metrics, 100.0f);
    SDL_Rect button{panel.x + UiPx(metrics, 48.0f),
                    panel.y + panel.h / 2 - button_height / 2,
                    button_width,
                    button_height};
    state.button_bounds = button;

    bool selected = true;
    SDL_Color fill = selected ? kButtonHighlight : kButtonFill;
    SDL_Color text_color = selected ? kButtonText : kTextPrimary;
    DrawPanel(renderer, button, fill, kPanelBorder);

    std::string label =
        state.fullscreen ? "Switch to Windowed Mode" : "Switch to Fullscreen Mode";
    if (body_font) {
        RenderFittedText(renderer,
                         body_font,
                         button.x + UiPx(metrics, 24.0f),
                         button.y + UiPx(metrics, 28.0f),
                         button.w - UiPx(metrics, 48.0f),
                         label,
                         text_color);
    }

    const std::string status = state.fullscreen ? "Current mode: Fullscreen" : "Current mode: Windowed";
    DrawInfoBar(renderer, fonts, window_width, window_height, status);

    const std::string control_hint =
        using_controller ? "A toggle | B/Menu back" : "Enter toggle | Esc/Right-click back";
    DrawControlHint(renderer, fonts, metrics, panel, control_hint);
}

SaveSetupAction HandleSaveSetupEvent(SaveSetupState& state,
                                     const match::platform::InputEvent& evt,
                                     bool /*using_controller*/,
                                     int window_width,
                                     int window_height) {
    const int total = static_cast<int>(state.summaries.size()) + 1;
    if (total <= 0) {
        return SaveSetupAction::None;
    }
    UiMetrics metrics = ComputeUiMetrics(window_width, window_height);
    const int visible_count = ComputeSaveSetupVisibleCount(metrics);
    ClampSaveSetupScroll(state, visible_count);

    auto set_selection_from_point = [&](int x, int y) -> bool {
        if (state.entry_bounds.size() != static_cast<std::size_t>(total)) {
            return false;
        }
        for (int i = 0; i < total; ++i) {
            if (PointInRect(state.entry_bounds[static_cast<std::size_t>(i)], x, y)) {
                state.selected_index = i;
                ClampSaveSetupScroll(state, visible_count);
                return true;
            }
        }
        return false;
    };

    auto activate_selection = [&]() -> SaveSetupAction {
        return SaveSetupAction::OpenEntry;
    };

    switch (evt.type) {
        case match::platform::InputEventType::MouseMove:
            set_selection_from_point(evt.x, evt.y);
            break;
        case match::platform::InputEventType::MouseWheel:
            if (evt.wheel_y > 0) {
                state.first_visible = std::max(0, state.first_visible - 1);
            } else if (evt.wheel_y < 0) {
                state.first_visible =
                    std::min(state.first_visible + 1, std::max(0, total - visible_count));
            }
            ClampSaveSetupScroll(state, visible_count);
            break;
        case match::platform::InputEventType::MouseButtonDown:
            if (evt.mouse_button == match::platform::MouseButton::Left) {
                if (set_selection_from_point(evt.x, evt.y)) {
                    return activate_selection();
                }
            } else if (evt.mouse_button == match::platform::MouseButton::Right) {
                return SaveSetupAction::Cancel;
            }
            break;
        case match::platform::InputEventType::KeyDown: {
            using match::platform::KeyCode;
            if (evt.key == KeyCode::Down || evt.key == KeyCode::S) {
                MoveSelection(state.selected_index, +1, total);
                ClampSaveSetupScroll(state, visible_count);
            } else if (evt.key == KeyCode::Up || evt.key == KeyCode::W) {
                MoveSelection(state.selected_index, -1, total);
                ClampSaveSetupScroll(state, visible_count);
            } else if (evt.key == KeyCode::Enter) {
                return activate_selection();
            } else if (evt.key == KeyCode::Escape) {
                return SaveSetupAction::Cancel;
            }
            break;
        }
        case match::platform::InputEventType::ControllerButtonDown: {
            using match::platform::ControllerButton;
            switch (evt.controller_button) {
                case ControllerButton::DPadDown:
                    MoveSelection(state.selected_index, +1, total);
                    ClampSaveSetupScroll(state, visible_count);
                    break;
                case ControllerButton::DPadUp:
                    MoveSelection(state.selected_index, -1, total);
                    ClampSaveSetupScroll(state, visible_count);
                    break;
                case ControllerButton::A:
                    return activate_selection();
                case ControllerButton::B:
                case ControllerButton::Menu:
                    return SaveSetupAction::Cancel;
                default:
                    break;
            }
            break;
        }
        case match::platform::InputEventType::ControllerAxisMotion: {
            using match::platform::ControllerAxis;
            if (evt.controller_axis == ControllerAxis::LeftY) {
                int dir = 0;
                if (evt.axis_value > kAxisThreshold) {
                    dir = +1;
                } else if (evt.axis_value < -kAxisThreshold) {
                    dir = -1;
                }
                if (ShouldProcessAxisRepeat(dir, state.axis_vertical, state.axis_vertical_tick)) {
                    MoveSelection(state.selected_index, dir, total);
                    ClampSaveSetupScroll(state, visible_count);
                }
                if (dir == 0) {
                    state.axis_vertical = 0;
                    state.axis_vertical_tick = 0;
                }
            }
            break;
        }
        default:
            break;
    }

    return SaveSetupAction::None;
}

SaveDetailAction HandleSaveDetailEvent(SaveDetailState& state,
                                       const match::platform::InputEvent& evt,
                                       bool /*using_controller*/) {
    const int total = static_cast<int>(state.buttons.size());
    if (total == 0) {
        return SaveDetailAction::None;
    }

    auto clamp_selection = [&]() {
        if (state.selected_button < 0) {
            state.selected_button = 0;
        } else if (state.selected_button >= total) {
            state.selected_button = total - 1;
        }
    };
    clamp_selection();

    auto activate = [&]() -> SaveDetailAction {
        if (state.selected_button < 0 || state.selected_button >= total) {
            return SaveDetailAction::None;
        }
        if (state.button_actions.size() != state.buttons.size()) {
            return SaveDetailAction::None;
        }
        return state.button_actions[static_cast<std::size_t>(state.selected_button)];
    };

    auto select_from_point = [&](int x, int y) -> bool {
        if (state.button_bounds.size() != state.buttons.size()) {
            return false;
        }
        for (std::size_t i = 0; i < state.button_bounds.size(); ++i) {
            if (PointInRect(state.button_bounds[i], x, y)) {
                state.selected_button = static_cast<int>(i);
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
                return SaveDetailAction::Cancel;
            }
            break;
        case match::platform::InputEventType::KeyDown: {
            using match::platform::KeyCode;
            if (evt.key == KeyCode::Left || evt.key == KeyCode::Up || evt.key == KeyCode::W) {
                --state.selected_button;
                clamp_selection();
            } else if (evt.key == KeyCode::Right || evt.key == KeyCode::Down || evt.key == KeyCode::S) {
                ++state.selected_button;
                clamp_selection();
            } else if (evt.key == KeyCode::Enter) {
                return activate();
            } else if (evt.key == KeyCode::Escape) {
                return SaveDetailAction::Cancel;
            }
            break;
        }
        case match::platform::InputEventType::ControllerButtonDown: {
            using match::platform::ControllerButton;
            switch (evt.controller_button) {
                case ControllerButton::DPadLeft:
                case ControllerButton::DPadUp:
                    --state.selected_button;
                    clamp_selection();
                    break;
                case ControllerButton::DPadRight:
                case ControllerButton::DPadDown:
                    ++state.selected_button;
                    clamp_selection();
                    break;
                case ControllerButton::A:
                    return activate();
                case ControllerButton::B:
                case ControllerButton::Menu:
                    return SaveDetailAction::Cancel;
                default:
                    break;
            }
            break;
        }
        case match::platform::InputEventType::ControllerAxisMotion: {
            using match::platform::ControllerAxis;
            if (evt.controller_axis == ControllerAxis::LeftX || evt.controller_axis == ControllerAxis::LeftY) {
                int dir = 0;
                if (evt.axis_value > kAxisThreshold) {
                    dir = +1;
                } else if (evt.axis_value < -kAxisThreshold) {
                    dir = -1;
                }
                if (dir != 0) {
                    state.selected_button = std::clamp(state.selected_button + dir, 0, total - 1);
                }
            }
            break;
        }
        default:
            break;
    }

    return SaveDetailAction::None;
}

void RenderSaveDetail(SDL_Renderer* renderer,
                      const match::render::Fonts& fonts,
                      int window_width,
                      int window_height,
                      SaveDetailState& state,
                      bool using_controller) {
    UiMetrics metrics = ComputeUiMetrics(window_width, window_height);
    SDL_SetRenderDrawColor(renderer, kBackground.r, kBackground.g, kBackground.b, 255);
    SDL_RenderClear(renderer);

    SDL_Rect panel = PanelCentered(metrics, 960.0f, 600.0f, -10.0f);
    DrawPanel(renderer, panel, kPanelFill, kPanelBorder);

    TTF_Font* title_font = fonts.heading ? fonts.heading : fonts.body;
    TTF_Font* body_font = fonts.body ? fonts.body : fonts.heading;

    std::string title = state.is_new ? "Start New Game"
                                     : (!state.summary.title.empty() ? state.summary.title : state.slot.name);
    if (title_font) {
        RenderCenteredText(renderer, title_font, window_width, panel.y + UiPx(metrics, 30.0f), title, kTextPrimary);
    }

    int y = panel.y + UiPx(metrics, 120.0f);
    if (body_font) {
        const std::vector<std::string>& lines =
            !state.info_lines.empty() ? state.info_lines : state.summary.detail_lines;
        for (const std::string& line : lines) {
            RenderFittedText(renderer,
                             body_font,
                             panel.x + UiPx(metrics, 32.0f),
                             y,
                             panel.w - UiPx(metrics, 64.0f),
                             line,
                             kTextPrimary);
            y += UiPx(metrics, 34.0f);
        }
        if (!state.summary.error.empty()) {
            RenderFittedText(renderer,
                             body_font,
                             panel.x + UiPx(metrics, 32.0f),
                             y + UiPx(metrics, 10.0f),
                             panel.w - UiPx(metrics, 64.0f),
                             state.summary.error,
                             kTextSecondary);
        }
    }

    const int button_count = static_cast<int>(state.buttons.size());
    state.button_bounds.resize(button_count);
    const int button_width = UiPx(metrics, 220.0f);
    const int button_height = UiPx(metrics, 60.0f);
    const int button_gap = UiPx(metrics, 24.0f);
    const int row_width = button_count * button_width + std::max(0, button_count - 1) * button_gap;
    int button_x = panel.x + (panel.w - row_width) / 2;
    int button_y = panel.y + panel.h - UiPx(metrics, 120.0f);

    for (int i = 0; i < button_count; ++i) {
        SDL_Rect rect{button_x, button_y, button_width, button_height};
        state.button_bounds[static_cast<std::size_t>(i)] = rect;
        bool selected = (i == state.selected_button);
        SDL_Color fill = selected ? kButtonHighlight : kButtonFill;
        SDL_Color text_color = selected ? kButtonText : kTextPrimary;
        DrawPanel(renderer, rect, fill, kPanelBorder);
        if (body_font) {
            SDL_Rect text_rect{rect.x + UiPx(metrics, 12.0f),
                               rect.y + UiPx(metrics, 12.0f),
                               rect.w - UiPx(metrics, 24.0f),
                               rect.h - UiPx(metrics, 24.0f)};
            RenderTextCenteredInRect(renderer,
                                     body_font,
                                     text_rect,
                                     state.buttons[static_cast<std::size_t>(i)],
                                     text_color);
        }
        button_x += button_width + button_gap;
    }

    const std::string control_hint =
        using_controller ? "D-Pad move | A select | B cancel"
                         : "Arrow keys move | Enter select | Esc cancel";
    DrawControlHint(renderer, fonts, metrics, panel, control_hint);

    DrawInfoBar(renderer,
                fonts,
                window_width,
                window_height,
                state.is_new ? "Create a new save slot for this mode."
                             : "Review the save details and choose an action.");
}

SaveBrowserAction HandleSaveBrowserEvent(SaveBrowserState& state,
                                         const match::platform::InputEvent& evt,
                                         bool /*using_controller*/) {
    const int total = static_cast<int>(state.entries.size());
    if (total == 0) {
        return SaveBrowserAction::None;
    }
    auto clamp_sel = [&]() {
        if (state.selected < 0) state.selected = 0;
        if (state.selected >= total) state.selected = total - 1;
    };
    clamp_sel();
    auto activate = [&]() -> SaveBrowserAction {
        const auto& entry = state.entries[static_cast<std::size_t>(state.selected)];
        if (entry.is_new_entry) {
            return SaveBrowserAction::StartNew;
        }
        return SaveBrowserAction::Load;
    };

    switch (evt.type) {
        case match::platform::InputEventType::KeyDown: {
            using match::platform::KeyCode;
            if (evt.key == KeyCode::Down || evt.key == KeyCode::S) {
                ++state.selected;
                clamp_sel();
            } else if (evt.key == KeyCode::Up || evt.key == KeyCode::W) {
                --state.selected;
                clamp_sel();
            } else if (evt.key == KeyCode::Enter) {
                return activate();
            } else if (evt.key == KeyCode::Escape) {
                return SaveBrowserAction::Cancel;
            }
            break;
        }
        case match::platform::InputEventType::ControllerButtonDown: {
            using match::platform::ControllerButton;
            switch (evt.controller_button) {
                case ControllerButton::DPadDown:
                    ++state.selected;
                    clamp_sel();
                    break;
                case ControllerButton::DPadUp:
                    --state.selected;
                    clamp_sel();
                    break;
                case ControllerButton::A:
                    return activate();
                case ControllerButton::B:
                    return SaveBrowserAction::Cancel;
                default:
                    break;
            }
            break;
        }
        default:
            break;
    }

    return SaveBrowserAction::None;
}

void RenderSaveBrowser(SDL_Renderer* renderer,
                       const match::render::Fonts& fonts,
                       int window_width,
                       int window_height,
                       const SaveBrowserState& state) {
    UiMetrics metrics = ComputeUiMetrics(window_width, window_height);
    SDL_SetRenderDrawColor(renderer, kBackground.r, kBackground.g, kBackground.b, 255);
    SDL_RenderClear(renderer);

    SDL_Rect panel = PanelCentered(metrics, 800.0f, 640.0f);
    DrawPanel(renderer, panel, kPanelFill, kPanelBorder);

    TTF_Font* title_font = fonts.heading ? fonts.heading : fonts.body;
    TTF_Font* body_font = fonts.body ? fonts.body : fonts.heading;

    if (title_font) {
        RenderText(renderer,
                   title_font,
                   panel.x + UiPx(metrics, 32.0f),
                   panel.y + UiPx(metrics, 24.0f),
                   "Saves",
                   kTextPrimary);
    }

    if (!body_font) {
        return;
    }

    int y = panel.y + UiPx(metrics, 100.0f);
    for (std::size_t i = 0; i < state.entries.size(); ++i) {
        const auto& entry = state.entries[i];
        SDL_Rect rect{panel.x + UiPx(metrics, 32.0f),
                      y,
                      panel.w - UiPx(metrics, 64.0f),
                      UiPx(metrics, 64.0f)};
        SDL_Color fill = (static_cast<int>(i) == state.selected) ? kButtonHighlight : kButtonFill;
        SDL_Color text = (static_cast<int>(i) == state.selected) ? kButtonText : kTextPrimary;
        DrawPanel(renderer, rect, fill, kPanelBorder);
        RenderText(renderer,
                   body_font,
                   rect.x + UiPx(metrics, 18.0f),
                   rect.y + UiPx(metrics, 16.0f),
                   entry.title,
                   text);
        y += rect.h + UiPx(metrics, 12.0f);
        if (y > panel.y + panel.h - UiPx(metrics, 100.0f)) {
            break;
        }
    }
}

SettingsState::SettingsState(GameSettings& s) : settings(s) {
    RefreshEntries();
}

void SettingsState::RefreshEntries() {
    settings.EnsureConstraints();
    entries.clear();
    if (settings.mode == GameMode::PvP || settings.mode == GameMode::Tournament) {
        entries.push_back(Entry{EntryType::PlayerCount, -1});
    }
    for (int i = 0; i < settings.player_count; ++i) {
        entries.push_back(Entry{EntryType::PlayerName, i});
    }
    entries.push_back(Entry{EntryType::MovesPerRound, -1});
    entries.push_back(Entry{EntryType::TotalRounds, -1});
    entries.push_back(Entry{EntryType::TurnOrder, -1});
    entries.push_back(Entry{EntryType::Bombs, -1});
    entries.push_back(Entry{EntryType::ColorBlast, -1});
    entries.push_back(Entry{EntryType::StartGame, -1});
    entries.push_back(Entry{EntryType::Back, -1});
    entry_bounds.assign(entries.size(), SDL_Rect{0, 0, 0, 0});
    selected = ClampSelection(selected, static_cast<int>(entries.size()));
    first_visible = std::clamp(first_visible, 0, selected);
    axis_vertical_tick = 0;
    axis_horizontal_tick = 0;
}

const SettingsState::Entry& SettingsState::EntryForIndex(int idx) const {
    if (entries.empty()) {
        static Entry fallback{EntryType::Back, -1};
        return fallback;
    }
    idx = std::clamp(idx, 0, static_cast<int>(entries.size()) - 1);
    return entries[static_cast<std::size_t>(idx)];
}

namespace {

bool AdjustSetting(SettingsState& state, const SettingsState::Entry& entry, int delta) {
    bool changed = false;
    switch (entry.type) {
        case SettingsState::EntryType::PlayerCount: {
            int next = state.settings.player_count + delta;
            if (state.settings.mode == GameMode::PvC) {
                next = 2;
            } else {
                const int min_players =
                    (state.settings.mode == GameMode::Tournament) ? std::max(kMinPlayers, 4) : kMinPlayers;
                next = std::max(min_players, next);
            }
            if (next != state.settings.player_count) {
                state.settings.player_count = next;
                changed = true;
            }
            break;
        }
        case SettingsState::EntryType::MovesPerRound: {
            int next = std::clamp(state.settings.moves_per_round + delta, kMinMovesPerRound, kMaxMovesPerRound);
            if (next != state.settings.moves_per_round) {
                state.settings.moves_per_round = next;
                changed = true;
            }
            break;
        }
        case SettingsState::EntryType::TotalRounds: {
            int next = std::clamp(state.settings.total_rounds + delta, kMinTotalRounds, kMaxTotalRounds);
            if (next != state.settings.total_rounds) {
                state.settings.total_rounds = next;
                changed = true;
            }
            break;
        }
        case SettingsState::EntryType::TurnOrder:
            if (delta != 0) {
                state.settings.turn_order = (state.settings.turn_order == TurnOrderOption::RoundRobin)
                                                ? TurnOrderOption::Consecutive
                                                : TurnOrderOption::RoundRobin;
                changed = true;
            }
            break;
        case SettingsState::EntryType::Bombs:
            if (delta != 0) {
                state.settings.bombs_enabled = !state.settings.bombs_enabled;
                changed = true;
            }
            break;
        case SettingsState::EntryType::ColorBlast:
            if (delta != 0) {
                state.settings.color_blast_enabled = !state.settings.color_blast_enabled;
                changed = true;
            }
            break;
        default:
            break;
    }

    if (changed) {
        state.settings.EnsureConstraints();
        state.RefreshEntries();
    }
    return changed;
}

SettingsAction ActivateEntry(SettingsState& state, const SettingsState::Entry& entry) {
    switch (entry.type) {
        case SettingsState::EntryType::PlayerName:
            state.pending_player_index = entry.index;
            return SettingsAction::EditPlayerName;
        case SettingsState::EntryType::PlayerCount:
        case SettingsState::EntryType::MovesPerRound:
        case SettingsState::EntryType::TotalRounds:
        case SettingsState::EntryType::TurnOrder:
        case SettingsState::EntryType::Bombs:
        case SettingsState::EntryType::ColorBlast:
            AdjustSetting(state, entry, +1);
            return SettingsAction::None;
        case SettingsState::EntryType::StartGame:
            return SettingsAction::StartGame;
        case SettingsState::EntryType::Back:
            return SettingsAction::Back;
    }
    return SettingsAction::None;
}

bool EntryAcceptsAdjustment(const SettingsState::Entry& entry) {
    switch (entry.type) {
        case SettingsState::EntryType::PlayerCount:
        case SettingsState::EntryType::MovesPerRound:
        case SettingsState::EntryType::TotalRounds:
        case SettingsState::EntryType::TurnOrder:
        case SettingsState::EntryType::Bombs:
        case SettingsState::EntryType::ColorBlast:
            return true;
        default:
            return false;
    }
}

}  // namespace

std::string DescribeSettingsEntry(const SettingsState& state, const SettingsState::Entry& entry) {
    switch (entry.type) {
        case SettingsState::EntryType::PlayerName:
            if (entry.index >= 0 && entry.index < static_cast<int>(state.settings.player_names.size())) {
                return state.settings.player_is_cpu(entry.index) ? "Rename the computer opponent"
                                                                 : "Set the display name for player " +
                                                                       std::to_string(entry.index + 1);
            }
            return "Set player name";
        case SettingsState::EntryType::PlayerCount:
            return "Choose how many players will participate";
        case SettingsState::EntryType::MovesPerRound:
            return "Number of moves each player gets on their turn";
        case SettingsState::EntryType::TotalRounds:
            return "Total number of rounds to play";
        case SettingsState::EntryType::TurnOrder:
            return "Pick whether turns rotate or stay with one player";
        case SettingsState::EntryType::Bombs:
            return "Toggle bomb tiles in the match";
        case SettingsState::EntryType::ColorBlast:
            return "Toggle color blast power-ups";
        case SettingsState::EntryType::StartGame:
            return "Begin the match with current settings";
        case SettingsState::EntryType::Back:
            return "Return to the previous screen";
    }
    return {};
}

int ComputeSettingsVisibleCount(const UiMetrics& metrics) {
    const int panel_height = UiPx(metrics, 660.0f);
    const int top_padding = UiPx(metrics, 32.0f);
    const int bottom_padding = UiPx(metrics, 32.0f);
    const int button_height = UiPx(metrics, 56.0f);
    const int button_gap = UiPx(metrics, 12.0f);
    const int available = std::max(1, panel_height - top_padding - bottom_padding);
    return std::max(1, (available + button_gap) / (button_height + button_gap));
}

void ClampSettingsScroll(SettingsState& state, int visible_count) {
    const int total = static_cast<int>(state.entries.size());
    if (visible_count <= 0) {
        state.first_visible = 0;
        return;
    }
    state.selected = ClampSelection(state.selected, total);
    int max_first = std::max(0, total - visible_count);
    state.first_visible = std::clamp(state.first_visible, 0, max_first);
    if (state.selected < state.first_visible) {
        state.first_visible = state.selected;
    } else if (state.selected >= state.first_visible + visible_count) {
        state.first_visible = state.selected - visible_count + 1;
    }
}

SettingsAction HandleSettingsEvent(SettingsState& state,
                                   const match::platform::InputEvent& evt,
                                   bool using_controller,
                                   int window_width,
                                   int window_height) {
    const int total = static_cast<int>(state.entries.size());
    if (total == 0) {
        return SettingsAction::None;
    }
    UiMetrics metrics = ComputeUiMetrics(window_width, window_height);
    const int visible_count = ComputeSettingsVisibleCount(metrics);
    ClampSettingsScroll(state, visible_count);

    auto current_entry = [&]() -> const SettingsState::Entry& {
        return state.entries[static_cast<std::size_t>(state.selected)];
    };

    auto set_selection_from_point = [&](int x, int y) -> bool {
        if (state.entry_bounds.size() != state.entries.size()) {
            return false;
        }
        for (std::size_t i = 0; i < state.entry_bounds.size(); ++i) {
            if (PointInRect(state.entry_bounds[i], x, y)) {
                state.selected = static_cast<int>(i);
                ClampSettingsScroll(state, visible_count);
                return true;
            }
        }
        return false;
    };

    auto set_activation_source = [&](bool controller) {
        state.last_activation_from_controller = controller;
    };

    switch (evt.type) {
        case match::platform::InputEventType::MouseMove:
            set_selection_from_point(evt.x, evt.y);
            break;
        case match::platform::InputEventType::MouseWheel:
            if (evt.wheel_y > 0) {
                state.first_visible = std::max(0, state.first_visible - 1);
            } else if (evt.wheel_y < 0) {
                state.first_visible =
                    std::min(state.first_visible + 1, std::max(0, total - visible_count));
            }
            ClampSettingsScroll(state, visible_count);
            break;
        case match::platform::InputEventType::MouseButtonDown:
            if (evt.mouse_button == match::platform::MouseButton::Left) {
                if (set_selection_from_point(evt.x, evt.y)) {
                    const auto& entry = current_entry();
                    if (EntryAcceptsAdjustment(entry)) {
                        AdjustSetting(state, entry, +1);
                        return SettingsAction::None;
                    }
                    set_activation_source(false);
                    return ActivateEntry(state, entry);
                }
            } else if (evt.mouse_button == match::platform::MouseButton::Right) {
                const auto& entry = current_entry();
                if (EntryAcceptsAdjustment(entry)) {
                    AdjustSetting(state, entry, -1);
                } else {
                    set_activation_source(false);
                    return SettingsAction::Back;
                }
            }
            break;
        case match::platform::InputEventType::KeyDown: {
            using match::platform::KeyCode;
            if (evt.key == KeyCode::Down || evt.key == KeyCode::S) {
                MoveSelection(state.selected, +1, total);
                ClampSettingsScroll(state, visible_count);
            } else if (evt.key == KeyCode::Up || evt.key == KeyCode::W) {
                MoveSelection(state.selected, -1, total);
                ClampSettingsScroll(state, visible_count);
            } else if (evt.key == KeyCode::Left || evt.key == KeyCode::A) {
                AdjustSetting(state, current_entry(), -1);
            } else if (evt.key == KeyCode::Right || evt.key == KeyCode::D) {
                AdjustSetting(state, current_entry(), +1);
            } else if (evt.key == KeyCode::Enter) {
                set_activation_source(false);
                return ActivateEntry(state, current_entry());
            } else if (evt.key == KeyCode::Escape) {
                set_activation_source(false);
                return SettingsAction::Back;
            }
            break;
        }
        case match::platform::InputEventType::ControllerButtonDown: {
            using match::platform::ControllerButton;
            switch (evt.controller_button) {
                case ControllerButton::DPadDown:
                    MoveSelection(state.selected, +1, total);
                    ClampSettingsScroll(state, visible_count);
                    break;
                case ControllerButton::DPadUp:
                    MoveSelection(state.selected, -1, total);
                    ClampSettingsScroll(state, visible_count);
                    break;
                case ControllerButton::DPadLeft:
                case ControllerButton::X:
                    AdjustSetting(state, current_entry(), -1);
                    break;
                case ControllerButton::DPadRight:
                case ControllerButton::Y:
                    AdjustSetting(state, current_entry(), +1);
                    break;
                case ControllerButton::A:
                    set_activation_source(true);
                    return ActivateEntry(state, current_entry());
                case ControllerButton::B:
                    set_activation_source(true);
                    return SettingsAction::Back;
                default:
                    break;
            }
            break;
        }
        case match::platform::InputEventType::ControllerAxisMotion: {
            using match::platform::ControllerAxis;
            if (evt.controller_axis == ControllerAxis::LeftY) {
                int dir = 0;
                if (evt.axis_value > kAxisThreshold) {
                    dir = +1;
                } else if (evt.axis_value < -kAxisThreshold) {
                    dir = -1;
                }
                if (ShouldProcessAxisRepeat(dir, state.axis_vertical, state.axis_vertical_tick)) {
                    MoveSelection(state.selected, dir, total);
                    ClampSettingsScroll(state, visible_count);
                }
                if (dir == 0) {
                    state.axis_vertical = 0;
                    state.axis_vertical_tick = 0;
                }
            } else if (evt.controller_axis == ControllerAxis::LeftX) {
                int dir = 0;
                if (evt.axis_value > kAxisThreshold) {
                    dir = +1;
                } else if (evt.axis_value < -kAxisThreshold) {
                    dir = -1;
                }
                if (ShouldProcessAxisRepeat(dir, state.axis_horizontal, state.axis_horizontal_tick)) {
                    AdjustSetting(state, current_entry(), dir);
                }
                if (dir == 0) {
                    state.axis_horizontal = 0;
                    state.axis_horizontal_tick = 0;
                }
            }
            break;
        }
        default:
            break;
    }

    if (!using_controller) {
        state.last_activation_from_controller = false;
    }

    return SettingsAction::None;
}


void RenderSettings(SDL_Renderer* renderer,
                    const match::render::Fonts& fonts,
                    int window_width,
                    int window_height,
                    SettingsState& state,
                    bool using_controller,
                    const OskState* text_input_state) {
    auto& mutable_state = state;
    mutable_state.settings.EnsureConstraints();
    mutable_state.RefreshEntries();

    UiMetrics metrics = ComputeUiMetrics(window_width, window_height);
    SDL_SetRenderDrawColor(renderer, kBackground.r, kBackground.g, kBackground.b, 255);
    SDL_RenderClear(renderer);

    TTF_Font* title_font = fonts.heading ? fonts.heading : fonts.body;
    TTF_Font* entry_font = fonts.body ? fonts.body : fonts.heading;

    if (title_font) {
        RenderCenteredText(renderer, title_font, window_width, UiPx(metrics, 80.0f), "Round Settings", kTextPrimary);
    }

    SDL_Rect panel = PanelCentered(metrics, 900.0f, 660.0f, -20.0f);
    DrawPanel(renderer, panel, kPanelFill, kPanelBorder);

    const int top_padding = UiPx(metrics, 32.0f);
    const int bottom_padding = UiPx(metrics, 32.0f);
    const int button_height = UiPx(metrics, 56.0f);
    const int button_gap = UiPx(metrics, 12.0f);
    const int scrollbar_width = UiPx(metrics, 12.0f);
    const int button_left = panel.x + UiPx(metrics, 36.0f);
    const int button_width = panel.w - UiPx(metrics, 72.0f) - scrollbar_width;
    const int list_height = std::max(1, panel.h - top_padding - bottom_padding);
    const int visible_count = std::max(1, (list_height + button_gap) / (button_height + button_gap));
    mutable_state.entry_bounds.resize(mutable_state.entries.size());
    std::fill(mutable_state.entry_bounds.begin(), mutable_state.entry_bounds.end(), SDL_Rect{0, 0, 0, 0});
    ClampSettingsScroll(mutable_state, visible_count);

    int y = panel.y + top_padding;
    const int total_entries = static_cast<int>(mutable_state.entries.size());
    for (int offset = 0; offset < visible_count && mutable_state.first_visible + offset < total_entries; ++offset) {
        int index = mutable_state.first_visible + offset;
        const auto& entry = mutable_state.entries[static_cast<std::size_t>(index)];
        SDL_Rect button{button_left, y, button_width, button_height};
        mutable_state.entry_bounds[static_cast<std::size_t>(index)] = button;
        bool selected = index == mutable_state.selected;
        SDL_Color fill = selected ? kButtonHighlight : kButtonFill;
        SDL_Color text = selected ? kButtonText : kTextPrimary;
        SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
        SDL_RenderFillRect(renderer, &button);
        SDL_SetRenderDrawColor(renderer, kPanelBorder.r, kPanelBorder.g, kPanelBorder.b, 255);
        SDL_RenderDrawRect(renderer, &button);
        if (entry_font) {
            bool show_player_cursor = false;
            if (text_input_state && text_input_state->active && text_input_state->target &&
                entry.type == SettingsState::EntryType::PlayerName && entry.index >= 0 &&
                entry.index < static_cast<int>(mutable_state.settings.player_names.size())) {
                const std::string* target =
                    mutable_state.settings.player_names.empty()
                        ? nullptr
                        : &mutable_state.settings.player_names[static_cast<std::size_t>(entry.index)];
                show_player_cursor = (target == text_input_state->target);
            }
            std::string label;
            switch (entry.type) {
                case SettingsState::EntryType::PlayerName: {
                    std::string name =
                        (entry.index >= 0 && entry.index < static_cast<int>(mutable_state.settings.player_names.size()))
                            ? mutable_state.settings.player_names[static_cast<std::size_t>(entry.index)]
                            : match::ui::MakeDefaultPlayerName(entry.index);
                    if (mutable_state.settings.player_is_cpu(entry.index)) {
                        name += " (Computer)";
                    }
                    label = "Player " + std::to_string(entry.index + 1) + " name: " + name;
                    break;
                }
                case SettingsState::EntryType::PlayerCount:
                    label = "Players: " + std::to_string(mutable_state.settings.player_count);
                    break;
                case SettingsState::EntryType::MovesPerRound:
                    label = "Moves per player per round: " +
                            std::to_string(mutable_state.settings.moves_per_round);
                    break;
                case SettingsState::EntryType::TotalRounds:
                    label = "Total rounds: " + std::to_string(mutable_state.settings.total_rounds);
                    break;
                case SettingsState::EntryType::TurnOrder:
                    label = std::string("Turn order: ") +
                            (mutable_state.settings.turn_order == TurnOrderOption::RoundRobin ? "Round-robin" : "Consecutive");
                    break;
                case SettingsState::EntryType::Bombs:
                    label = std::string("Bombs: ") + (mutable_state.settings.bombs_enabled ? "On" : "Off");
                    break;
                case SettingsState::EntryType::ColorBlast:
                    label = std::string("Color blast: ") + (mutable_state.settings.color_blast_enabled ? "On" : "Off");
                    break;
                case SettingsState::EntryType::StartGame:
                    label = "START GAME";
                    break;
                case SettingsState::EntryType::Back:
                    label = "BACK";
                    break;
            }
            const int caret_height =
                std::max(8, FontPixelHeight(entry_font) - UiPx(metrics, 12.0f));
            RenderTextWithCursor(renderer,
                                 entry_font,
                                 button.x + UiPx(metrics, 20.0f),
                                 button.y + UiPx(metrics, 16.0f),
                                 button.w - UiPx(metrics, 24.0f),
                                 label,
                                 text,
                                 show_player_cursor,
                                 UiPx(metrics, 4.0f),
                                 caret_height);
        }
        y += button_height + button_gap;
    }

    SDL_Rect scrollbar_area{button_left + button_width + UiPx(metrics, 8.0f),
                            panel.y + top_padding,
                            scrollbar_width,
                            list_height};
    DrawScrollbar(renderer, metrics, scrollbar_area, total_entries, visible_count, mutable_state.first_visible);

    const std::string control_hint =
        using_controller ? "D-Pad/Stick move | A select | X/Y adjust | B back"
                         : "Mouse click | Enter select | Left/Right click adjust | Esc back";
    DrawControlHint(renderer, fonts, metrics, panel, control_hint);

    std::string info_text;
    if (!mutable_state.entries.empty()) {
        info_text = DescribeSettingsEntry(mutable_state,
                                          mutable_state.entries[static_cast<std::size_t>(mutable_state.selected)]);
    }
    if (info_text.empty()) {
        info_text = "Adjust match rules before starting the game.";
    }
    DrawInfoBar(renderer, fonts, window_width, window_height, info_text);
}

struct OskKey {
    enum class Type {
        Character,
        Space,
        Backspace,
        Enter,
        Shift,
        Cancel
    };

    std::string label;
    Type type = Type::Character;
    char value = '\0';
    float width_units = 1.0f;
    bool new_group = false;
};

OskKey LetterKey(char letter, float width = 1.0f) {
    return OskKey{std::string(1, static_cast<char>(std::toupper(letter))),
                  OskKey::Type::Character,
                  static_cast<char>(std::tolower(letter)),
                  width};
}

OskKey SymbolKey(char symbol, float width = 1.0f, bool new_group = false) {
    return OskKey{std::string(1, symbol), OskKey::Type::Character, symbol, width, new_group};
}

OskKey SpecialKey(const std::string& label, OskKey::Type type, float width = 1.0f, bool new_group = false) {
    return OskKey{label, type, '\0', width, new_group};
}

using KeyboardRow = std::vector<OskKey>;

const std::vector<KeyboardRow> kKeyboardRows{
    KeyboardRow{LetterKey('a'),
                LetterKey('b'),
                LetterKey('c'),
                LetterKey('d'),
                LetterKey('e'),
                LetterKey('f'),
                LetterKey('g'),
                LetterKey('h'),
                LetterKey('i')},
    KeyboardRow{LetterKey('j'),
                LetterKey('k'),
                LetterKey('l'),
                LetterKey('m'),
                LetterKey('n'),
                LetterKey('o'),
                LetterKey('p'),
                LetterKey('q'),
                LetterKey('r')},
    KeyboardRow{LetterKey('s'),
                LetterKey('t'),
                LetterKey('u'),
                LetterKey('v'),
                LetterKey('w'),
                LetterKey('x'),
                LetterKey('y'),
                LetterKey('z')},
    KeyboardRow{
        SpecialKey("Shift", OskKey::Type::Shift, 2.4f),
        SymbolKey('1'),
        SymbolKey('2'),
        SymbolKey('3'),
        SymbolKey('4'),
        SymbolKey('5'),
        SymbolKey('6'),
        SymbolKey('7'),
        SymbolKey('8'),
        SymbolKey('9'),
        SymbolKey('0'),
        SymbolKey('-', 1.0f),
        SymbolKey('_', 1.0f),
        SymbolKey('.', 1.0f),
        SpecialKey("Space", OskKey::Type::Space, 6.0f, true),
        SpecialKey("Backspace", OskKey::Type::Backspace, 3.0f, true),
        SpecialKey("Enter", OskKey::Type::Enter, 2.4f)}
};

bool IsAllowedOskChar(char ch) {
    unsigned char uch = static_cast<unsigned char>(ch);
    if (uch >= 0x80) {
        // Allow UTF-8 bytes so international names can be typed from hardware keyboards.
        return true;
    }
    if (std::isalnum(uch)) {
        return true;
    }
    return ch == ' ' || ch == '-' || ch == '_' || ch == '.';
}

void AppendCharacter(OskState& state, char ch) {
    if (state.buffer.size() >= state.max_length) {
        return;
    }
    state.buffer.push_back(ch);
    if (state.target) {
        *state.target = state.buffer;
    }
}



int TotalKeys() {

    int total = 0;

    for (const auto& row : kKeyboardRows) {

        total += static_cast<int>(row.size());

    }

    return total;

}



void ClampOskCursor(OskState& state) {

    if (state.row < 0) {

        state.row = 0;

    }

    if (state.row >= static_cast<int>(kKeyboardRows.size())) {

        state.row = static_cast<int>(kKeyboardRows.size()) - 1;

    }

    const auto& row = kKeyboardRows[static_cast<std::size_t>(state.row)];

    if (row.empty()) {

        state.col = 0;

        return;

    }

    if (state.col < 0) {

        state.col = 0;

    }

    if (state.col >= static_cast<int>(row.size())) {

        state.col = static_cast<int>(row.size()) - 1;

    }

}



OskAction ActivateKey(OskState& state, const OskKey& key) {

    switch (key.type) {

        case OskKey::Type::Character:

            if (state.buffer.size() < state.max_length) {

                char ch = key.value == '\0' && !key.label.empty() ? key.label[0] : key.value;
                unsigned char uc = static_cast<unsigned char>(ch);
                if (std::isalpha(uc)) {
                    ch = state.uppercase ? static_cast<char>(std::toupper(uc))
                                         : static_cast<char>(std::tolower(uc));
                }
                state.buffer.push_back(ch);

            }

            return OskAction::None;

        case OskKey::Type::Backspace:

            if (!state.buffer.empty()) {

                state.buffer.pop_back();
                if (state.target) {
                    *state.target = state.buffer;
                }

            }

            return OskAction::None;

        case OskKey::Type::Space:

            if (state.buffer.size() < state.max_length) {

                state.buffer.push_back(' ');
                if (state.target) {
                    *state.target = state.buffer;
                }

            }

            return OskAction::None;

        case OskKey::Type::Enter:

            if (state.target) {

                *state.target = state.buffer;

            }

            state.active = false;

            return OskAction::Commit;

        case OskKey::Type::Shift:

            state.uppercase = !state.uppercase;
            return OskAction::None;

        case OskKey::Type::Cancel:

            if (state.target) {

                *state.target = state.original;

            }

            state.buffer = state.original;

            state.active = false;

            return OskAction::Cancel;

    }

    return OskAction::None;

}



void BeginOsk(OskState& state,
              const std::string& title,
              std::string& target,
              std::size_t max_length,
              bool show_keyboard) {
    state.title = title;
    state.original = target;
    state.buffer = target;
    state.target = &target;
    state.max_length = max_length;
    state.row = 0;
    state.col = 0;
    if (show_keyboard) {
        state.key_bounds.assign(static_cast<std::size_t>(TotalKeys()), SDL_Rect{0, 0, 0, 0});
    } else {
        state.key_bounds.clear();
    }
    state.uppercase = true;
    state.axis_vertical = 0;
    state.axis_horizontal = 0;
    state.axis_vertical_tick = 0;
    state.axis_horizontal_tick = 0;
    state.show_keyboard = show_keyboard;
    state.text_input_mode = !show_keyboard;
    state.active = true;
}



OskAction HandleOskEvent(OskState& state,

                         const match::platform::InputEvent& evt,

                         bool using_controller,

                         int /*window_width*/,

                         int /*window_height*/) {

    if (!state.active) {

        return OskAction::None;

    }

    if (evt.type == match::platform::InputEventType::TextInput) {
        for (char ch : evt.text) {
            if (!IsAllowedOskChar(ch)) {
                continue;
            }
            AppendCharacter(state, ch);
        }
        return OskAction::None;
    }

    if (!state.show_keyboard) {
        switch (evt.type) {
            case match::platform::InputEventType::KeyDown:
                switch (evt.key) {
                    case match::platform::KeyCode::Enter:
                        if (state.target) {
                            *state.target = state.buffer;
                        }
                        state.active = false;
                        return OskAction::Commit;
                    case match::platform::KeyCode::Escape:
                        if (state.target) {
                            *state.target = state.original;
                        }
                        state.buffer = state.original;
                        state.active = false;
                        return OskAction::Cancel;
                    case match::platform::KeyCode::Backspace:
                        if (!state.buffer.empty()) {
                            state.buffer.pop_back();
                            if (state.target) {
                                *state.target = state.buffer;
                            }
                        }
                        break;
                    default:
                        break;
                }
                break;
            case match::platform::InputEventType::MouseButtonDown:
                if (evt.mouse_button == match::platform::MouseButton::Right) {
                    if (state.target) {
                        *state.target = state.original;
                    }
                    state.buffer = state.original;
                    state.active = false;
                    return OskAction::Cancel;
                }
                break;
            case match::platform::InputEventType::ControllerButtonDown:
                if (!using_controller) {
                    break;
                }
                switch (evt.controller_button) {
                    case match::platform::ControllerButton::A:
                        if (state.target) {
                            *state.target = state.buffer;
                        }
                        state.active = false;
                        return OskAction::Commit;
                    case match::platform::ControllerButton::B:
                    case match::platform::ControllerButton::Menu:
                        if (state.target) {
                            *state.target = state.original;
                        }
                        state.buffer = state.original;
                        state.active = false;
                        return OskAction::Cancel;
                    case match::platform::ControllerButton::X:
                        if (!state.buffer.empty()) {
                            state.buffer.pop_back();
                            if (state.target) {
                                *state.target = state.buffer;
                            }
                        }
                        break;
                    default:
                        break;
                }
                break;
            default:
                break;
        }
        return OskAction::None;
    }



    auto current_key = [&]() -> const OskKey& {

        ClampOskCursor(state);

        return kKeyboardRows[static_cast<std::size_t>(state.row)][static_cast<std::size_t>(state.col)];

    };



    auto move_with_mouse = [&](int x, int y) {
        if (state.key_bounds.empty()) {
            return;
        }

        int index = -1;

        for (std::size_t i = 0; i < state.key_bounds.size(); ++i) {

            const SDL_Rect& rect = state.key_bounds[i];

            if (x >= rect.x && x <= rect.x + rect.w && y >= rect.y && y <= rect.y + rect.h) {

                index = static_cast<int>(i);

                break;

            }

        }

        if (index >= 0) {

            int running = 0;

            for (std::size_t r = 0; r < kKeyboardRows.size(); ++r) {

                const auto& row = kKeyboardRows[r];

                if (index < running + static_cast<int>(row.size())) {

                    state.row = static_cast<int>(r);

                    state.col = index - running;

                    break;

                }

                running += static_cast<int>(row.size());

            }

        }

    };



    switch (evt.type) {

        case match::platform::InputEventType::MouseMove:

            move_with_mouse(evt.x, evt.y);

            break;

        case match::platform::InputEventType::MouseButtonDown:

            move_with_mouse(evt.x, evt.y);

            if (evt.mouse_button == match::platform::MouseButton::Left) {

                return ActivateKey(state, current_key());

            } else if (evt.mouse_button == match::platform::MouseButton::Right) {

                OskKey back{"Back", OskKey::Type::Backspace};

                return ActivateKey(state, back);

            }

            break;

        case match::platform::InputEventType::KeyDown:

            switch (evt.key) {

                case match::platform::KeyCode::Up:

                case match::platform::KeyCode::W:

                    state.row--;

                    ClampOskCursor(state);

                    break;

                case match::platform::KeyCode::Down:

                case match::platform::KeyCode::S:

                    state.row++;

                    ClampOskCursor(state);

                    break;

                case match::platform::KeyCode::Left:

                case match::platform::KeyCode::A:

                    state.col--;

                    ClampOskCursor(state);

                    break;

                case match::platform::KeyCode::Right:

                case match::platform::KeyCode::D:

                    state.col++;

                    ClampOskCursor(state);

                    break;

                case match::platform::KeyCode::Backspace: {

                    OskKey back{"Back", OskKey::Type::Backspace};

                    return ActivateKey(state, back);

                }

                case match::platform::KeyCode::Enter:

                    return ActivateKey(state, current_key());

                case match::platform::KeyCode::Escape: {

                    OskKey cancel{"Cancel", OskKey::Type::Cancel};

                    return ActivateKey(state, cancel);

                }

                default:

                    break;

            }

            break;

        case match::platform::InputEventType::ControllerButtonDown:

            if (!using_controller) {

                break;

            }
            if (!state.show_keyboard) {
                break;
            }

            switch (evt.controller_button) {

                case match::platform::ControllerButton::DPadUp:

                    state.row--;

                    ClampOskCursor(state);

                    break;

                case match::platform::ControllerButton::DPadDown:

                    state.row++;

                    ClampOskCursor(state);

                    break;

                case match::platform::ControllerButton::DPadLeft:

                    state.col--;

                    ClampOskCursor(state);

                    break;

                case match::platform::ControllerButton::DPadRight:

                    state.col++;

                    ClampOskCursor(state);

                    break;

                case match::platform::ControllerButton::A:

                    return ActivateKey(state, current_key());

                case match::platform::ControllerButton::B: {

                    OskKey cancel{"Cancel", OskKey::Type::Cancel};

                    return ActivateKey(state, cancel);

                }

                case match::platform::ControllerButton::X: {

                    OskKey back{"Back", OskKey::Type::Backspace};

                    return ActivateKey(state, back);

                }

                case match::platform::ControllerButton::Y: {

                    OskKey shift{"Shift", OskKey::Type::Shift};

                    return ActivateKey(state, shift);

                }

                default:

                    break;

            }

            break;
        case match::platform::InputEventType::ControllerAxisMotion:
            if (!using_controller) {
                break;
            }
            if (!state.show_keyboard) {
                break;
            }
            switch (evt.controller_axis) {
                case match::platform::ControllerAxis::LeftY: {
                    int dir = 0;
                    if (evt.axis_value > kAxisThreshold) {
                        dir = +1;
                    } else if (evt.axis_value < -kAxisThreshold) {
                        dir = -1;
                    }
                    if (ShouldProcessAxisRepeat(dir, state.axis_vertical, state.axis_vertical_tick)) {
                        state.row += dir;
                        ClampOskCursor(state);
                    }
                    if (dir == 0) {
                        state.axis_vertical = 0;
                        state.axis_vertical_tick = 0;
                    }
                    break;
                }
                case match::platform::ControllerAxis::LeftX: {
                    int dir = 0;
                    if (evt.axis_value > kAxisThreshold) {
                        dir = +1;
                    } else if (evt.axis_value < -kAxisThreshold) {
                        dir = -1;
                    }
                    if (ShouldProcessAxisRepeat(dir, state.axis_horizontal, state.axis_horizontal_tick)) {
                        state.col += dir;
                        ClampOskCursor(state);
                    }
                    if (dir == 0) {
                        state.axis_horizontal = 0;
                        state.axis_horizontal_tick = 0;
                    }
                    break;
                }
                default:
                    break;
            }
            break;
        default:

            break;

    }

    return OskAction::None;

}



void RenderOsk(SDL_Renderer* renderer,

               const match::render::Fonts& fonts,

               int window_width,

               int window_height,

               OskState& state,

               bool using_controller) {

    if (!state.active || !state.show_keyboard) {
        return;
    }



    UiMetrics metrics = ComputeUiMetrics(window_width, window_height);

    TTF_Font* title_font = fonts.heading ? fonts.heading : fonts.body;

    TTF_Font* key_font = fonts.small ? fonts.small : (fonts.body ? fonts.body : fonts.heading);



    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 160);

    SDL_Rect full{0, 0, window_width, window_height};

    SDL_RenderFillRect(renderer, &full);



    const float panel_height = state.show_keyboard ? 460.0f : 260.0f;
    const float panel_offset = state.show_keyboard ? 80.0f : 20.0f;
    SDL_Rect panel = PanelCentered(metrics, 840.0f, panel_height, panel_offset);
    DrawPanel(renderer, panel, kPanelFill, kPanelBorder);

    int x = panel.x + UiPx(metrics, 28.0f);
    int y = panel.y + UiPx(metrics, 28.0f);
    if (title_font) {
        RenderCenteredText(renderer, title_font, window_width, panel.y - UiPx(metrics, 40.0f), state.title, kTextPrimary);
        y += UiPx(metrics, 32.0f);
    }

    SDL_Rect input_box{x, y, panel.w - UiPx(metrics, 56.0f), UiPx(metrics, 60.0f)};

    SDL_SetRenderDrawColor(renderer, 32, 34, 40, 255);

    SDL_RenderFillRect(renderer, &input_box);

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 200);

    SDL_RenderDrawRect(renderer, &input_box);

    if (key_font) {
        SDL_Rect text_rect{input_box.x + UiPx(metrics, 10.0f),
                           input_box.y + UiPx(metrics, 10.0f),
                           input_box.w - UiPx(metrics, 20.0f),
                           input_box.h - UiPx(metrics, 20.0f)};
        RenderTextCenteredInRect(renderer, key_font, text_rect, state.buffer, kTextPrimary);
    }

    y += input_box.h + UiPx(metrics, 16.0f);



    const int cell_h = UiPx(metrics, 54.0f);
    const int key_gap = UiPx(metrics, 6.0f);
    const int group_gap = UiPx(metrics, 18.0f);
    const int row_left = panel.x + UiPx(metrics, 28.0f);
    const int row_width = panel.w - UiPx(metrics, 56.0f);
    const int letter_rows = kOskLetterRows;
    const int max_letters = kOskMaxLettersPerRow;
    const int letter_cell_w =
        std::max(UiPx(metrics, 36.0f),
                 static_cast<int>(std::round((row_width - key_gap * (max_letters - 1)) /
                                             static_cast<float>(max_letters))));

    auto render_key = [&](const OskKey& key, const SDL_Rect& rect, bool selected) {
        bool shift_active = (key.type == OskKey::Type::Shift && state.uppercase);
        SDL_Color fill = (selected || shift_active) ? kButtonHighlight : kButtonFill;
        SDL_Color text = (selected || shift_active) ? kButtonText : kTextPrimary;
        SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
        SDL_RenderFillRect(renderer, &rect);
        SDL_SetRenderDrawColor(renderer, kPanelBorder.r, kPanelBorder.g, kPanelBorder.b, 255);
        SDL_RenderDrawRect(renderer, &rect);
        if (key_font) {
            std::string label = key.label;
            if (key.type == OskKey::Type::Character && key.value != '\0' &&
                std::isalpha(static_cast<unsigned char>(key.value))) {
                unsigned char uc = static_cast<unsigned char>(key.value);
                char display = state.uppercase ? static_cast<char>(std::toupper(uc))
                                               : static_cast<char>(std::tolower(uc));
                label.assign(1, display);
            }
            SDL_Rect label_rect{rect.x + UiPx(metrics, 6.0f),
                                rect.y + UiPx(metrics, 6.0f),
                                rect.w - UiPx(metrics, 12.0f),
                                rect.h - UiPx(metrics, 12.0f)};
            RenderTextCenteredInRect(renderer, key_font, label_rect, label, text);
        }
    };

    if (state.show_keyboard) {
        state.key_bounds.assign(static_cast<std::size_t>(TotalKeys()), SDL_Rect{0, 0, 0, 0});
        int key_index = 0;
        const int rows = static_cast<int>(kKeyboardRows.size());

        for (int r = 0; r < rows; ++r) {
            const auto& row = kKeyboardRows[static_cast<std::size_t>(r)];
            if (row.empty()) {
                continue;
            }

            int key_x = row_left;
            if (r < letter_rows) {
                int row_letters = static_cast<int>(row.size());
                int offset = row_left;
                if (row_letters < max_letters) {
                    int total_gap = max_letters - row_letters;
                    offset += ((letter_cell_w + key_gap) * total_gap) / 2;
                }
                key_x = offset;
                for (int c = 0; c < row_letters; ++c) {
                    SDL_Rect rect{key_x, y, letter_cell_w, cell_h};
                    state.key_bounds[static_cast<std::size_t>(key_index++)] = rect;
                    bool selected = (r == state.row && c == state.col);
                    render_key(row[static_cast<std::size_t>(c)], rect, selected);
                    key_x += letter_cell_w + key_gap;
                }
            } else {
                float total_units = 0.0f;
                int group_count = 0;
                for (const auto& key : row) {
                    total_units += key.width_units;
                    if (key.new_group) {
                        group_count++;
                    }
                }
                const int segments = std::max(0, static_cast<int>(row.size()) - 1);
                const float available_width =
                    row_width - key_gap * segments - group_gap * std::max(0, group_count);
                float unit_px = available_width / std::max(1.0f, total_units);
                for (int c = 0; c < static_cast<int>(row.size()); ++c) {
                    const auto& key = row[static_cast<std::size_t>(c)];
                    if (key.new_group && c > 0) {
                        key_x += group_gap;
                    }
                    int key_w =
                        std::max(UiPx(metrics, 36.0f), static_cast<int>(std::round(unit_px * key.width_units)));
                    SDL_Rect rect{key_x, y, key_w, cell_h};
                    state.key_bounds[static_cast<std::size_t>(key_index++)] = rect;
                    bool selected = (r == state.row && c == state.col);
                    render_key(key, rect, selected);
                    key_x += key_w + key_gap;
                }
            }

            y += cell_h + UiPx(metrics, 8.0f);
        }
    } else if (key_font) {
        state.key_bounds.clear();
        const std::string prompt =
            "Use your keyboard to type. Press Enter to confirm, Esc to cancel, Backspace to delete.";
        RenderFittedText(renderer,
                         key_font,
                         row_left,
                         y + UiPx(metrics, 10.0f),
                         row_width,
                         prompt,
                         kTextPrimary);
        y += cell_h;
    }

    std::string control_hint;
    if (state.show_keyboard) {
        control_hint = using_controller ? "Stick move | A select key | X backspace | Y shift | B cancel"
                                        : "Mouse click keys | Enter commit | Esc cancel | Backspace delete";
    } else {
        control_hint = "Keyboard: type text | Enter confirm | Esc cancel | Backspace delete";
    }
    DrawControlHint(renderer, fonts, metrics, panel, control_hint);
    DrawInfoBar(renderer,
                fonts,
                window_width,
                window_height,
                state.show_keyboard ? "Use the on-screen keyboard to edit text. Select Shift (or press Y) to toggle case."
                                    : "Physical keyboard active: type to edit, Enter to commit, Esc to cancel.");
}



TournamentBracketAction HandleTournamentBracketEvent(TournamentBracketState& state,
                                                     const match::platform::InputEvent& evt,
                                                     bool /*using_controller*/) {
    constexpr int kButtonCount = 2;
    auto clamp_selection = [&]() {
        if (state.selected_button < 0) {
            state.selected_button = 0;
        } else if (state.selected_button >= kButtonCount) {
            state.selected_button = kButtonCount - 1;
        }
    };
    clamp_selection();

    auto select_from_point = [&](int x, int y) -> bool {
        for (int i = 0; i < kButtonCount; ++i) {
            if (PointInRect(state.button_bounds[static_cast<std::size_t>(i)], x, y)) {
                state.selected_button = i;
                return true;
            }
        }
        return false;
    };

    auto activate = [&]() -> TournamentBracketAction {
        if (state.selected_button == 0) {
            return state.start_enabled ? TournamentBracketAction::StartMatch : TournamentBracketAction::None;
        }
        if (state.selected_button == 1) {
            return TournamentBracketAction::Back;
        }
        return TournamentBracketAction::None;
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
                return TournamentBracketAction::Back;
            }
            break;
        case match::platform::InputEventType::KeyDown: {
            using match::platform::KeyCode;
            if (evt.key == KeyCode::Left || evt.key == KeyCode::A || evt.key == KeyCode::Up || evt.key == KeyCode::W) {
                --state.selected_button;
                clamp_selection();
            } else if (evt.key == KeyCode::Right || evt.key == KeyCode::D || evt.key == KeyCode::Down ||
                       evt.key == KeyCode::S) {
                ++state.selected_button;
                clamp_selection();
            } else if (evt.key == KeyCode::Enter) {
                return activate();
            } else if (evt.key == KeyCode::Escape) {
                return TournamentBracketAction::Back;
            }
            break;
        }
        case match::platform::InputEventType::ControllerButtonDown: {
            using match::platform::ControllerButton;
            switch (evt.controller_button) {
                case ControllerButton::DPadLeft:
                case ControllerButton::DPadUp:
                    --state.selected_button;
                    clamp_selection();
                    break;
                case ControllerButton::DPadRight:
                case ControllerButton::DPadDown:
                    ++state.selected_button;
                    clamp_selection();
                    break;
                case ControllerButton::A:
                    return activate();
                case ControllerButton::B:
                case ControllerButton::Menu:
                    return TournamentBracketAction::Back;
                default:
                    break;
            }
            break;
        }
        default:
            break;
    }

    return TournamentBracketAction::None;
}

void RenderTournamentBracket(SDL_Renderer* renderer,
                             const match::render::Fonts& fonts,
                             int window_width,
                             int window_height,
                             TournamentBracketState& state,
                             const TournamentBracketView& view,
                             bool using_controller) {
    UiMetrics metrics = ComputeUiMetrics(window_width, window_height);
    SDL_SetRenderDrawColor(renderer, kBackground.r, kBackground.g, kBackground.b, 255);
    SDL_RenderClear(renderer);

    float layout_base_w = 1320.0f;
    float layout_base_h = 780.0f;
    SDL_Rect panel = PanelCentered(metrics, layout_base_w, layout_base_h, -60.0f);
    DrawPanel(renderer, panel, kPanelFill, kPanelBorder);

    TTF_Font* title_font = fonts.heading ? fonts.heading : fonts.body;
    TTF_Font* body_font = fonts.body ? fonts.body : fonts.heading;
    TTF_Font* small_font = fonts.small ? fonts.small : fonts.body;

    if (title_font) {
        RenderCenteredText(renderer,
                           title_font,
                           window_width,
                           panel.y - UiPx(metrics, 70.0f),
                           "Tournament Bracket",
                           kTextPrimary);
    }

    const int rounds = std::max<int>(1, static_cast<int>(view.rounds.size()));
    const float base_column_width = UiPx(metrics, 160.0f);
    const float base_column_gap = UiPx(metrics, 40.0f);
    const float base_top_margin = UiPx(metrics, 80.0f);
    const float base_bottom_margin = UiPx(metrics, 140.0f);
    const float base_match_height = UiPx(metrics, 80.0f);
    const float base_match_gap = UiPx(metrics, 24.0f);

    const std::vector<BracketMatchView> kEmptyMatches{};
    int first_round_matches =
        (!view.rounds.empty() && !view.rounds.front().empty())
            ? static_cast<int>(view.rounds.front().size())
            : 1;

    float base_total_width =
        rounds * base_column_width + std::max(0, rounds - 1) * base_column_gap;
    float base_total_height =
        base_top_margin + base_bottom_margin +
        first_round_matches * base_match_height +
        std::max(0, first_round_matches - 1) * base_match_gap;

    float scale_w = (base_total_width > 0.0f) ? static_cast<float>(panel.w) / base_total_width : 1.0f;
    float scale_h = (base_total_height > 0.0f) ? static_cast<float>(panel.h) / base_total_height : 1.0f;
    float global_scale = std::min(scale_w, scale_h);
    if (!std::isfinite(global_scale) || global_scale <= 0.0f) {
        global_scale = 1.0f;
    }

    SDL_Rect previous_viewport;
    SDL_RenderGetViewport(renderer, &previous_viewport);
    float previous_scale_x = 1.0f;
    float previous_scale_y = 1.0f;
    SDL_RenderGetScale(renderer, &previous_scale_x, &previous_scale_y);
    SDL_RenderSetViewport(renderer, &panel);
    SDL_RenderSetScale(renderer, global_scale, global_scale);

    const float local_panel_w = panel.w / global_scale;
    const float local_panel_h = panel.h / global_scale;
    int column_width = static_cast<int>(std::round(base_column_width));
    int column_gap = static_cast<int>(std::round(base_column_gap));
    const int match_height = static_cast<int>(std::round(base_match_height));
    const int match_gap = static_cast<int>(std::round(base_match_gap));
    const int top_margin = static_cast<int>(std::round(base_top_margin));
    const int bottom_margin = static_cast<int>(std::round(base_bottom_margin));
    int max_height = static_cast<int>(std::round(local_panel_h - (top_margin + bottom_margin)));
    if (max_height < match_height) {
        max_height = match_height;
    }

    int total_width = rounds * column_width + std::max(0, rounds - 1) * column_gap;
    const int edge_padding = UiPx(metrics, 24.0f);
    const float available_width = std::max(10.0f, local_panel_w - edge_padding * 2.0f);
    if (total_width > 0) {
        float width_fill = available_width / static_cast<float>(total_width);
        if (!std::isfinite(width_fill) || width_fill <= 0.0f) {
            width_fill = 1.0f;
        }
        column_width = std::max(20, static_cast<int>(std::round(column_width * width_fill)));
        column_gap = std::max(12, static_cast<int>(std::round(column_gap * width_fill)));
        total_width = rounds * column_width + std::max(0, rounds - 1) * column_gap;
        if (total_width > available_width) {
            float adjust = available_width / static_cast<float>(total_width);
            column_width = std::max(16, static_cast<int>(std::round(column_width * adjust)));
            column_gap = std::max(10, static_cast<int>(std::round(column_gap * adjust)));
            total_width = rounds * column_width + std::max(0, rounds - 1) * column_gap;
        }
    }
    int column_x = edge_padding;
    std::vector<std::vector<SDL_Rect>> rect_columns(static_cast<std::size_t>(rounds));
    auto draw_match = [&](const SDL_Rect& rect, const match::ui::BracketMatchView& view_match) {
        SDL_Color fill = kButtonFill;
        if (view_match.active) {
            fill = SDL_Color{255, 205, 64, 255};
        } else if (view_match.player_a_won || view_match.player_b_won) {
            fill = SDL_Color{46, 52, 64, 255};
        }
        DrawPanel(renderer, rect, fill, kPanelBorder);
        if (body_font) {
            SDL_Color text_a = view_match.player_a_won ? SDL_Color{255, 214, 102, 255} : kTextPrimary;
            SDL_Color text_b = view_match.player_b_won ? SDL_Color{255, 214, 102, 255} : kTextPrimary;
            RenderFittedText(renderer,
                             body_font,
                             rect.x + UiPx(metrics, 12.0f),
                             rect.y + UiPx(metrics, 10.0f),
                             rect.w - UiPx(metrics, 24.0f),
                             view_match.player_a,
                             text_a);
            RenderFittedText(renderer,
                             body_font,
                             rect.x + UiPx(metrics, 12.0f),
                             rect.y + UiPx(metrics, 40.0f),
                             rect.w - UiPx(metrics, 24.0f),
                             view_match.player_b,
                             text_b);
        }
    };

    for (int r = 0; r < rounds; ++r) {
        const auto& matches =
            (r < static_cast<int>(view.rounds.size())) ? view.rounds[static_cast<std::size_t>(r)] : kEmptyMatches;
        rect_columns[static_cast<std::size_t>(r)].resize(matches.size());
        if (r == 0) {
            int total_height = static_cast<int>(matches.size()) * match_height +
                               std::max(0, static_cast<int>(matches.size()) - 1) * match_gap;
            int y = top_margin;
            if (total_height < max_height) {
                y += (max_height - total_height) / 2;
            }
            for (std::size_t m = 0; m < matches.size(); ++m) {
                SDL_Rect rect{column_x, y, column_width, match_height};
                rect_columns[static_cast<std::size_t>(r)][m] = rect;
                draw_match(rect, matches[m]);
                y += match_height + match_gap;
            }
        } else {
            const auto& prev_rects = rect_columns[static_cast<std::size_t>(r - 1)];
            int fallback_y = top_margin;
            for (std::size_t m = 0; m < matches.size(); ++m) {
                SDL_Rect rect{column_x, fallback_y, column_width, match_height};
                int child_index = static_cast<int>(m) * 2;
                if (child_index < static_cast<int>(prev_rects.size())) {
                    int center_a = prev_rects[child_index].y + prev_rects[child_index].h / 2;
                    int center_b = center_a;
                    if (child_index + 1 < static_cast<int>(prev_rects.size())) {
                        center_b = prev_rects[child_index + 1].y + prev_rects[child_index + 1].h / 2;
                    }
                    int center = (center_a + center_b) / 2;
                    rect.y = center - rect.h / 2;
                }
                rect_columns[static_cast<std::size_t>(r)][m] = rect;
                draw_match(rect, matches[m]);
                fallback_y += match_height + match_gap;
            }
        }
        column_x += column_width + column_gap;
    }

    std::vector<std::pair<SDL_FPoint, SDL_FPoint>> connector_lines;
    auto to_screen = [&](float x, float y) -> SDL_FPoint {
        return SDL_FPoint{static_cast<float>(panel.x) + x * global_scale,
                          static_cast<float>(panel.y) + y * global_scale};
    };

    for (int r = 1; r < rounds; ++r) {
        const auto& prev_rects = rect_columns[static_cast<std::size_t>(r - 1)];
        const auto& cur_rects = rect_columns[static_cast<std::size_t>(r)];
        for (std::size_t m = 0; m < cur_rects.size(); ++m) {
            int child_index = static_cast<int>(m) * 2;
            if (child_index >= static_cast<int>(prev_rects.size())) {
                continue;
            }

            const SDL_Rect& parent = cur_rects[m];
            const SDL_Rect& child_a = prev_rects[child_index];
            SDL_Rect child_b = child_a;
            if (child_index + 1 < static_cast<int>(prev_rects.size())) {
                child_b = prev_rects[child_index + 1];
            }

            const float parent_center_y = static_cast<float>(parent.y + parent.h / 2);
            const float parent_inset_x = static_cast<float>(parent.x);
            const float base_child_x = static_cast<float>(child_a.x + child_a.w);
            const float available_gap = std::max(4.0f, parent_inset_x - base_child_x);
            const float join_x = parent_inset_x - std::max(8.0f, available_gap * 0.5f);

            const std::array<SDL_Point, 2> child_points{
                SDL_Point{child_a.x + child_a.w, child_a.y + child_a.h / 2},
                SDL_Point{child_b.x + child_b.w, child_b.y + child_b.h / 2}};

            for (const SDL_Point& child_point : child_points) {
                const float child_y = static_cast<float>(child_point.y);
                const float child_x = static_cast<float>(child_point.x);
                connector_lines.emplace_back(to_screen(child_x, child_y), to_screen(join_x, child_y));
                connector_lines.emplace_back(to_screen(join_x, child_y), to_screen(parent_inset_x, parent_center_y));
            }
        }
    }

    SDL_RenderSetScale(renderer, previous_scale_x, previous_scale_y);
    SDL_RenderSetViewport(renderer, &previous_viewport);

    SDL_SetRenderDrawColor(renderer, 120, 134, 150, 255);
    for (const auto& line : connector_lines) {
        SDL_RenderDrawLineF(renderer, line.first.x, line.first.y, line.second.x, line.second.y);
    }

    const char* labels[2] = {"Start Match", "Back"};
    const bool enabled[2] = {view.next_match_ready && state.start_enabled, true};
    const int button_width = UiPx(metrics, 260.0f);
    const int button_height = UiPx(metrics, 64.0f);
    const int spacing = UiPx(metrics, 32.0f);
    const int buttons_y = panel.y + panel.h + UiPx(metrics, 32.0f);
    int button_x = panel.x + (panel.w - (button_width * 2 + spacing)) / 2;

    for (int i = 0; i < 2; ++i) {
        SDL_Rect rect{button_x, buttons_y, button_width, button_height};
        state.button_bounds[static_cast<std::size_t>(i)] = rect;
        bool selected = (i == state.selected_button);
        SDL_Color fill = selected ? kButtonHighlight : kButtonFill;
        SDL_Color text_color = selected ? kButtonText : kTextPrimary;
        if (!enabled[i]) {
            fill = SDL_Color{48, 50, 60, 180};
            text_color = SDL_Color{160, 160, 160, 255};
        }
        DrawPanel(renderer, rect, fill, kPanelBorder);
        if (body_font) {
            RenderTextCenteredInRect(renderer, body_font, rect, labels[i], text_color);
        }
        button_x += button_width + spacing;
    }

    std::string info_line = view.status;
    if (info_line.empty()) {
        info_line = using_controller ? "D-Pad/Stick move | A confirm | B back"
                                     : "Mouse click | Enter confirm | Esc back";
    }
    DrawInfoBar(renderer, fonts, window_width, window_height, info_line);
    if (!view.next_match_label.empty() && small_font) {
        RenderCenteredText(renderer,
                           small_font,
                           window_width,
                           panel.y - UiPx(metrics, 32.0f),
                           view.next_match_label,
                           kTextSecondary);
    }
}

std::string ModeToString(GameMode mode) {

    switch (mode) {
        case GameMode::PvC:
            return "Player vs Computer";
        case GameMode::PvP:
            return "Player vs Player";
        case GameMode::Tournament:
            return "Tournament";
    }
    return "Unknown";

}



}  // namespace match::ui





