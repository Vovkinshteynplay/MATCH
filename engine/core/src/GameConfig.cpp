#include "match/core/GameConfig.hpp"

#include <stdexcept>

namespace match::core {

Json GameConfig::ToJson() const {
    Json json;
    json["display_mode"] = display_mode;
    json["resolution"] = {resolution[0], resolution[1]};
    json["rumble_on"] = rumble_on;
    return json;
}

GameConfig GameConfig::FromJson(const Json& json) {
    GameConfig config;
    if (json.contains("display_mode") && json["display_mode"].is_string()) {
        config.display_mode = json["display_mode"].get<std::string>();
    }
    if (json.contains("resolution") && json["resolution"].is_array() &&
        json["resolution"].size() == 2) {
        config.resolution[0] = json["resolution"][0].get<int>();
        config.resolution[1] = json["resolution"][1].get<int>();
    }
    if (json.contains("rumble_on") && json["rumble_on"].is_boolean()) {
        config.rumble_on = json["rumble_on"].get<bool>();
    }
    return config;
}

std::string GameConfig::Serialize() const {
    return ToJson().dump();
}

GameConfig GameConfig::Deserialize(const std::string& json_string) {
    auto json = Json::parse(json_string);
    return FromJson(json);
}

}  // namespace match::core

