#pragma once

#include <SDL2/SDL.h>

#include <vector>

#include "match/platform/InputEvents.hpp"

namespace match::platform {

class SdlInput {
public:
    SdlInput() = default;
    ~SdlInput();

    bool Initialize();
    void Shutdown();
    std::vector<InputEvent> Poll();
    void RumbleControllers(float strength, Uint32 duration_ms);

private:
    struct ControllerEntry {
        SDL_JoystickID instance_id = -1;
        SDL_GameController* controller = nullptr;
        SDL_Haptic* haptic = nullptr;
    };

    void OpenController(int joystick_index);
    void CloseController(SDL_JoystickID instance_id);

    bool initialized_ = false;
    std::vector<ControllerEntry> controllers_;
};

}  // namespace match::platform

