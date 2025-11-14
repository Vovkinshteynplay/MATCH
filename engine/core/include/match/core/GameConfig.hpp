#pragma once

#include <array>
#include <string>

#include "match/core/Json.hpp"

namespace match::core {

struct GameConfig {
    std::string display_mode = "windowed";
    std::array<int, 2> resolution{{1920, 1080}};
    bool rumble_on = true;

    Json ToJson() const;
    static GameConfig FromJson(const Json& json);

    std::string Serialize() const;
    static GameConfig Deserialize(const std::string& json_string);
};

}  // namespace match::core

