#include "plex_json.h"

#include "phi/runtime/str.h"

namespace phicore::plex::ipc {

namespace str = phi::str;

Json parseJson(std::string_view text)
{
    if (text.empty())
        return Json();
    const Json value = Json::parse(text, nullptr, false);
    return value.is_discarded() ? Json() : value;
}

Json parseObject(std::string_view text)
{
    const Json value = parseJson(text);
    return value.is_object() ? value : Json::object();
}

std::string dump(const Json &value)
{
    return value.dump(-1, ' ', false, Json::error_handler_t::replace);
}

Json jsonValue(const Json &obj, std::string_view key)
{
    if (!obj.is_object())
        return Json();
    const auto it = obj.find(key);
    return it == obj.end() ? Json() : *it;
}

std::string jsonString(const Json &obj, std::string_view key, bool trim)
{
    const Json value = jsonValue(obj, key);
    if (!value.is_string())
        return {};
    const std::string text = value.get<std::string>();
    return trim ? str::trimmed(text) : text;
}

int jsonInt(const Json &obj, std::string_view key, int fallback)
{
    const Json value = jsonValue(obj, key);
    if (value.is_number_integer())
        return value.get<int>();
    if (value.is_string()) {
        bool ok = false;
        const int parsed = str::toInt(str::trimmed(value.get<std::string>()), &ok);
        if (ok)
            return parsed;
    }
    return fallback;
}

bool jsonBool(const Json &obj, std::string_view key, bool fallback)
{
    const Json value = jsonValue(obj, key);
    return value.is_boolean() ? value.get<bool>() : fallback;
}

} // namespace phicore::plex::ipc
