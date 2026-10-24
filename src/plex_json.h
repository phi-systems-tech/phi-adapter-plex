#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace phicore::plex::ipc {

using Json = nlohmann::json;

Json parseJson(std::string_view text);
Json parseObject(std::string_view text);
std::string dump(const Json &value);
Json jsonValue(const Json &obj, std::string_view key);
std::string jsonString(const Json &obj, std::string_view key, bool trim = true);
int jsonInt(const Json &obj, std::string_view key, int fallback);
bool jsonBool(const Json &obj, std::string_view key, bool fallback = false);

} // namespace phicore::plex::ipc
