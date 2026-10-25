#include "plex_server.h"

#include "plex_json.h"

namespace phicore::plex::ipc {

namespace v1 = phicore::adapter::v1;

namespace {

std::int64_t number(const Json &obj, const char *key)
{
    return obj.is_object() && obj.contains(key) && obj[key].is_number() ? obj[key].get<std::int64_t>() : 0;
}

v1::Channel readOnly(const char *id, const char *name, v1::ChannelKind kind, v1::ChannelDataType type,
                     bool history = true)
{
    v1::ChannelFlags flags = v1::ChannelFlag::Readable | v1::ChannelFlag::Reportable;
    if (!history) flags = flags | v1::ChannelFlag::NoHistory;
    return v1::Channel(id, name, kind, type, flags);
}

} // namespace

ServerInfo parseServerRoot(const std::string &body)
{
    const Json mc = jsonValue(parseJson(body), "MediaContainer");
    return ServerInfo{jsonString(mc, "friendlyName"), jsonString(mc, "version"), jsonString(mc, "machineIdentifier")};
}

SessionsSummary parseSessions(const std::string &body)
{
    SessionsSummary out;
    const Json metadata = jsonValue(jsonValue(parseJson(body), "MediaContainer"), "Metadata");
    if (!metadata.is_array()) return out;
    const Json *chosen = nullptr;
    for (const Json &item : metadata) {
        if (!item.is_object()) continue;
        ++out.count;
        const bool playing = jsonString(jsonValue(item, "Player"), "state") == "playing";
        if (chosen == nullptr || (playing && jsonString(jsonValue(*chosen, "Player"), "state") != "playing"))
            chosen = &item;
    }
    if (chosen == nullptr) return out;
    const Json &item = *chosen;
    const std::string type = jsonString(item, "type");
    out.title = jsonString(item, "title");
    // A track's artist; an episode's show; a film has none.
    if (type == "track" || type == "episode") out.artist = jsonString(item, "grandparentTitle");
    const Json player = jsonValue(item, "Player");
    out.playedOn = jsonString(player, "title");
    if (out.playedOn.empty()) out.playedOn = jsonString(player, "product");
    out.playing = jsonString(player, "state") == "playing";
    out.positionS = number(item, "viewOffset") / 1000;
    out.durationS = number(item, "duration") / 1000;
    return out;
}

std::string serverRootPath()
{
    return "/";
}

std::string sessionsPath()
{
    return "/status/sessions";
}

v1::Device buildServerDevice(const ServerInfo &info)
{
    v1::Device device;
    device.setExternalId(kServerDeviceId);
    device.setName(info.name.empty() ? std::string("Plex Media Server") : "Plex " + info.name);
    device.setDeviceClass(v1::DeviceClass::Gateway);
    device.setManufacturer("Plex");
    device.setModel("Plex Media Server");
    device.setFirmware(info.version);
    return device;
}

v1::ChannelList serverChannels()
{
    v1::ChannelList out;
    out.push_back(readOnly(serverch::Connectivity, "Connectivity", v1::ChannelKind::ConnectivityStatus, v1::ChannelDataType::Enum));
    out.push_back(readOnly(serverch::Sessions, "Streams", v1::ChannelKind::MediaSessions, v1::ChannelDataType::Int));
    out.push_back(readOnly(serverch::Title, "Playing", v1::ChannelKind::MediaTitle, v1::ChannelDataType::String));
    out.push_back(readOnly(serverch::Artist, "Artist", v1::ChannelKind::MediaArtist, v1::ChannelDataType::String));
    out.push_back(readOnly(serverch::PlayedOn, "Played on", v1::ChannelKind::MediaPlayedOn, v1::ChannelDataType::String));
    v1::Channel position = readOnly(serverch::Position, "Position", v1::ChannelKind::MediaPosition, v1::ChannelDataType::Int, false);
    position.setUnit("s");
    out.push_back(position);
    v1::Channel duration = readOnly(serverch::Duration, "Duration", v1::ChannelKind::MediaDuration, v1::ChannelDataType::Int, false);
    duration.setUnit("s");
    out.push_back(duration);
    return out;
}

} // namespace phicore::plex::ipc
