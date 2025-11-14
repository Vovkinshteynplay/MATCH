#include "match/core/SavePayload.hpp"

namespace match::core {

Json SavePayload::ToJson() const {
    Json json;
    json["version"] = version;
    json["mode"] = mode;
    json["meta"] = meta;
    json["data"] = data;
    return json;
}

SavePayload SavePayload::FromJson(const Json& json) {
    SavePayload payload;
    if (json.contains("version")) {
        payload.version = json["version"].get<int>();
    }
    if (json.contains("mode") && json["mode"].is_string()) {
        payload.mode = json["mode"].get<std::string>();
    }
    if (json.contains("meta")) {
        payload.meta = json["meta"];
    }
    if (json.contains("data")) {
        payload.data = json["data"];
    }
    return payload;
}

std::string SavePayload::Serialize() const {
    return ToJson().dump();
}

SavePayload SavePayload::Deserialize(const std::string& json_string) {
    auto json = Json::parse(json_string);
    return FromJson(json);
}

std::vector<std::uint8_t> SavePayload::SerializeBinary() const {
    return Json::to_msgpack(ToJson());
}

SavePayload SavePayload::DeserializeBinary(const std::vector<std::uint8_t>& bytes) {
    auto json = Json::from_msgpack(bytes);
    return FromJson(json);
}

}  // namespace match::core

