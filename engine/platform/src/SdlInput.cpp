#include "match/platform/SdlInput.hpp"

#include <SDL2/SDL.h>

#include <algorithm>

namespace match::platform {

namespace {

MouseButton ToMouseButton(Uint8 button) {
    switch (button) {
        case SDL_BUTTON_LEFT:
            return MouseButton::Left;
        case SDL_BUTTON_RIGHT:
            return MouseButton::Right;
        case SDL_BUTTON_MIDDLE:
            return MouseButton::Middle;
        default:
            return MouseButton::Unknown;
    }
}

KeyCode ToKey(SDL_Keycode key) {
    switch (key) {
        case SDLK_ESCAPE:
            return KeyCode::Escape;
        case SDLK_UP:
            return KeyCode::Up;
        case SDLK_DOWN:
            return KeyCode::Down;
        case SDLK_LEFT:
            return KeyCode::Left;
        case SDLK_RIGHT:
            return KeyCode::Right;
        case SDLK_w:
            return KeyCode::W;
        case SDLK_a:
            return KeyCode::A;
        case SDLK_s:
            return KeyCode::S;
        case SDLK_d:
            return KeyCode::D;
        case SDLK_BACKSPACE:
            return KeyCode::Backspace;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            return KeyCode::Enter;
        default:
            return KeyCode::Unknown;
    }
}

ControllerButton ToControllerButton(Uint8 button) {
    switch (button) {
        case SDL_CONTROLLER_BUTTON_A:
            return ControllerButton::A;
        case SDL_CONTROLLER_BUTTON_B:
            return ControllerButton::B;
        case SDL_CONTROLLER_BUTTON_X:
            return ControllerButton::X;
        case SDL_CONTROLLER_BUTTON_Y:
            return ControllerButton::Y;
        case SDL_CONTROLLER_BUTTON_START:
            return ControllerButton::Menu;
        case SDL_CONTROLLER_BUTTON_GUIDE:
            return ControllerButton::Guide;
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            return ControllerButton::DPadUp;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            return ControllerButton::DPadDown;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            return ControllerButton::DPadLeft;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            return ControllerButton::DPadRight;
        default:
            return ControllerButton::Unknown;
    }
}

ControllerAxis ToControllerAxis(Uint8 axis) {
    switch (axis) {
        case SDL_CONTROLLER_AXIS_LEFTX:
            return ControllerAxis::LeftX;
        case SDL_CONTROLLER_AXIS_LEFTY:
            return ControllerAxis::LeftY;
        default:
            return ControllerAxis::Unknown;
    }
}

}  // namespace

SdlInput::~SdlInput() {
    Shutdown();
}

bool SdlInput::Initialize() {
    if (initialized_) {
        return true;
    }
    SDL_GameControllerEventState(SDL_ENABLE);
    const int joystick_count = SDL_NumJoysticks();
    for (int i = 0; i < joystick_count; ++i) {
        if (SDL_IsGameController(i)) {
            OpenController(i);
        }
    }
    initialized_ = true;
    return true;
}

void SdlInput::Shutdown() {
    for (auto& entry : controllers_) {
        if (entry.haptic) {
            SDL_HapticClose(entry.haptic);
            entry.haptic = nullptr;
        }
        if (entry.controller) {
            SDL_GameControllerClose(entry.controller);
            entry.controller = nullptr;
        }
    }
    controllers_.clear();
    initialized_ = false;
}

void SdlInput::OpenController(int joystick_index) {
    SDL_GameController* controller = SDL_GameControllerOpen(joystick_index);
    if (!controller) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to open controller %d: %s", joystick_index,
                    SDL_GetError());
        return;
    }
    SDL_Joystick* js = SDL_GameControllerGetJoystick(controller);
    SDL_JoystickID instance_id = SDL_JoystickInstanceID(js);

    SDL_Haptic* haptic = nullptr;
    if (SDL_JoystickIsHaptic(js)) {
        haptic = SDL_HapticOpenFromJoystick(js);
        if (haptic) {
            if (SDL_HapticRumbleInit(haptic) != 0) {
                SDL_HapticClose(haptic);
                haptic = nullptr;
            }
        }
    }

    controllers_.push_back(ControllerEntry{instance_id, controller, haptic});
}

void SdlInput::CloseController(SDL_JoystickID instance_id) {
    auto it = std::find_if(controllers_.begin(), controllers_.end(),
                           [&](const ControllerEntry& entry) { return entry.instance_id == instance_id; });
    if (it == controllers_.end()) {
        return;
    }
    if (it->haptic) {
        SDL_HapticClose(it->haptic);
    }
    if (it->controller) {
        SDL_GameControllerClose(it->controller);
    }
    controllers_.erase(it);
}

std::vector<InputEvent> SdlInput::Poll() {
    std::vector<InputEvent> events;
    SDL_Event sdl_event;
    while (SDL_PollEvent(&sdl_event)) {
        InputEvent evt;
        switch (sdl_event.type) {
            case SDL_QUIT:
                evt.type = InputEventType::Quit;
                events.push_back(evt);
                break;
            case SDL_MOUSEMOTION:
                evt.type = InputEventType::MouseMove;
                evt.x = sdl_event.motion.x;
                evt.y = sdl_event.motion.y;
                events.push_back(evt);
                break;
            case SDL_MOUSEBUTTONDOWN:
                evt.type = InputEventType::MouseButtonDown;
                evt.x = sdl_event.button.x;
                evt.y = sdl_event.button.y;
                evt.mouse_button = ToMouseButton(sdl_event.button.button);
                events.push_back(evt);
                break;
            case SDL_MOUSEBUTTONUP:
                evt.type = InputEventType::MouseButtonUp;
                evt.x = sdl_event.button.x;
                evt.y = sdl_event.button.y;
                evt.mouse_button = ToMouseButton(sdl_event.button.button);
                events.push_back(evt);
                break;
            case SDL_MOUSEWHEEL:
                evt.type = InputEventType::MouseWheel;
                evt.wheel_y = sdl_event.wheel.y;
                events.push_back(evt);
                break;
            case SDL_KEYDOWN:
                evt.type = InputEventType::KeyDown;
                evt.key = ToKey(sdl_event.key.keysym.sym);
                events.push_back(evt);
                break;
            case SDL_KEYUP:
                evt.type = InputEventType::KeyUp;
                evt.key = ToKey(sdl_event.key.keysym.sym);
                events.push_back(evt);
                break;
            case SDL_CONTROLLERBUTTONDOWN:
                evt.type = InputEventType::ControllerButtonDown;
                evt.controller_button = ToControllerButton(sdl_event.cbutton.button);
                events.push_back(evt);
                break;
        case SDL_CONTROLLERBUTTONUP:
            evt.type = InputEventType::ControllerButtonUp;
            evt.controller_button = ToControllerButton(sdl_event.cbutton.button);
            events.push_back(evt);
            break;
        case SDL_CONTROLLERAXISMOTION:
            evt.type = InputEventType::ControllerAxisMotion;
            evt.controller_axis = ToControllerAxis(sdl_event.caxis.axis);
            evt.axis_value = sdl_event.caxis.value;
            events.push_back(evt);
            break;
        case SDL_TEXTINPUT:
            evt.type = InputEventType::TextInput;
            evt.text = sdl_event.text.text ? sdl_event.text.text : "";
            events.push_back(evt);
            break;
        case SDL_CONTROLLERDEVICEADDED:
            OpenController(sdl_event.cdevice.which);
            break;
            case SDL_CONTROLLERDEVICEREMOVED:
                CloseController(sdl_event.cdevice.which);
                break;
            case SDL_WINDOWEVENT:
                if (sdl_event.window.event == SDL_WINDOWEVENT_RESTORED) {
                    evt.type = InputEventType::WindowRestored;
                    events.push_back(evt);
                }
                break;
            default:
                break;
        }
    }
    return events;
}

void SdlInput::RumbleControllers(float strength, Uint32 duration_ms) {
    if (controllers_.empty()) {
        return;
    }
    float clamped = std::clamp(strength, 0.0f, 1.0f);
    Uint16 low = static_cast<Uint16>(clamped * 0xFFFF);
    Uint16 high = low;
    for (auto& entry : controllers_) {
        if (entry.controller) {
            SDL_GameControllerRumble(entry.controller, low, high, duration_ms);
        } else if (entry.haptic) {
            SDL_HapticRumblePlay(entry.haptic, clamped, duration_ms / 1000.0f);
        }
    }
}

}  // namespace match::platform
