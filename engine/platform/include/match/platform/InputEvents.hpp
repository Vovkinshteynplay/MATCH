#pragma once

#include <string>
#include <vector>

namespace match::platform {

enum class InputEventType {
    Quit,
    MouseMove,
    MouseButtonDown,
    MouseButtonUp,
    MouseWheel,
    KeyDown,
    KeyUp,
    ControllerButtonDown,
    ControllerButtonUp,
    ControllerAxisMotion,
    WindowRestored,
    TextInput
};

enum class MouseButton { Left, Right, Middle, Unknown };

enum class KeyCode {
    Escape,
    Up,
    Down,
    Left,
    Right,
    W,
    A,
    S,
    D,
    Backspace,
    Enter,
    Unknown
};

enum class ControllerButton {
    A,
    B,
    X,
    Y,
    Menu,
    Guide,
    DPadUp,
    DPadDown,
    DPadLeft,
    DPadRight,
    Unknown
};

enum class ControllerAxis { LeftX, LeftY, Unknown };

struct InputEvent {
    InputEventType type = InputEventType::Quit;
    int x = 0;
    int y = 0;
    MouseButton mouse_button = MouseButton::Unknown;
    KeyCode key = KeyCode::Unknown;
    ControllerButton controller_button = ControllerButton::Unknown;
    ControllerAxis controller_axis = ControllerAxis::Unknown;
    int wheel_y = 0;
    int axis_value = 0;
    std::string text;
};

}  // namespace match::platform
