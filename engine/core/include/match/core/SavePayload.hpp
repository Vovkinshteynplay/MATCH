#pragma once

#include <string>
#include <vector>

#include "match/core/Json.hpp"

namespace match::core {

struct SavePayload {
    int version = 1;
    std::string mode;
    Json meta = Json::object();
    Json data = Json::object();

    Json ToJson() const;
    static SavePayload FromJson(const Json& json);

    std::string Serialize() const;
    static SavePayload Deserialize(const std::string& json_string);

    std::vector<std::uint8_t> SerializeBinary() const;
    static SavePayload DeserializeBinary(const std::vector<std::uint8_t>& bytes);
};

}  // namespace match::core

