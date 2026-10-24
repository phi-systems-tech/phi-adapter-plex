#include "plex_protocol.h"

#include <algorithm>
#include <cctype>

#include "phi/runtime/str.h"

#include "plex_json.h"

namespace phicore::plex::ipc {

namespace net = phicore::adapter::net;
namespace sdk = phicore::adapter::sdk;
namespace str = phi::str;
namespace v1 = phicore::adapter::v1;

namespace {

std::string playbackType(std::string value)
{
    value = str::toLower(str::trimmed(value));
    if (value == "music" || value == "audio" || value == "track")
        return "music";
    if (value == "photo" || value == "image")
        return "photo";
    return "video";
}

std::string metadataKey(const std::string &mediaId)
{
    const std::string id = str::trimmed(mediaId);
    if (id.empty() || id.find("://") != std::string::npos || id.find("..") != std::string::npos)
        return {};
    if (std::all_of(id.begin(), id.end(), [](unsigned char c) { return std::isdigit(c); }))
        return "/library/metadata/" + id;
    constexpr std::string_view prefix = "/library/metadata/";
    if (id.starts_with(prefix) && id.size() > prefix.size())
        return id;
    return {};
}

CommandBuildResult invalid(std::string error, v1::CmdStatus status = v1::CmdStatus::InvalidArgument)
{
    CommandBuildResult out;
    out.status = status;
    out.error = std::move(error);
    return out;
}

std::string query(std::uint64_t commandId, const std::string &type)
{
    return "?commandID=" + std::to_string(commandId) + "&type=" + urlEncode(type);
}

} // namespace

CommandBuildResult buildMediaCommand(const PlexSettings &settings,
                                     const sdk::MediaPlayerInvokeRequest &request,
                                     std::uint64_t commandId)
{
    if (!settings.hasPlayer())
        return invalid("Plex player host and client identifier are required");

    const std::string type = playbackType(request.mediaType);
    std::string path;
    switch (request.action) {
    case v1::MediaPlayerAction::Play:
        path = "/player/playback/play" + query(commandId, type);
        break;
    case v1::MediaPlayerAction::Pause:
        path = "/player/playback/pause" + query(commandId, type);
        break;
    case v1::MediaPlayerAction::Stop:
        path = "/player/playback/stop" + query(commandId, type);
        break;
    case v1::MediaPlayerAction::Next:
        path = "/player/playback/skipNext" + query(commandId, type);
        break;
    case v1::MediaPlayerAction::Previous:
        path = "/player/playback/skipPrevious" + query(commandId, type);
        break;
    case v1::MediaPlayerAction::SeekTo:
        if (request.positionMs < 0)
            return invalid("Seek position must not be negative");
        path = "/player/playback/seekTo" + query(commandId, type)
            + "&offset=" + std::to_string(request.positionMs);
        break;
    case v1::MediaPlayerAction::SeekBy:
        return invalid("Relative seek is not supported by Plex Companion",
                       v1::CmdStatus::NotSupported);
    case v1::MediaPlayerAction::PlayMedia: {
        if (request.enqueue != v1::MediaPlayerEnqueue::Replace)
            return invalid("Plex queue mutation is not implemented", v1::CmdStatus::NotSupported);
        if (!settings.hasServer())
            return invalid("Plex server host, port and token are required for PlayMedia");
        if (settings.serverMachineIdentifier.empty())
            return invalid("Plex server identity has not been detected yet",
                           v1::CmdStatus::TemporarilyOffline);
        const std::string key = metadataKey(request.mediaId);
        if (key.empty())
            return invalid("mediaId must be a Plex ratingKey or /library/metadata/... key");
        const std::int64_t offset = std::max<std::int64_t>(0, request.positionMs);
        path = "/player/playback/playMedia?commandID=" + std::to_string(commandId)
            + "&type=" + urlEncode(type)
            + "&key=" + urlEncode(key)
            + "&offset=" + std::to_string(offset)
            + "&machineIdentifier=" + urlEncode(settings.serverMachineIdentifier)
            + "&address=" + urlEncode(settings.serverHost)
            + "&port=" + std::to_string(settings.serverPort)
            + "&protocol=" + std::string(settings.serverUseTls ? "https" : "http")
            + "&token=" + urlEncode(settings.token);
        break;
    }
    case v1::MediaPlayerAction::Unknown:
        return invalid("Unknown media player action", v1::CmdStatus::NotSupported);
    }

    CommandBuildResult out;
    out.ok = true;
    out.status = v1::CmdStatus::Success;
    out.call = playerCall(settings, path);
    return out;
}

std::string machineIdentifierFromIdentity(const std::string &body)
{
    const Json root = parseJson(body);
    const Json container = jsonValue(root, "MediaContainer");
    std::string id = jsonString(container, "machineIdentifier");
    if (id.empty())
        id = jsonString(root, "machineIdentifier");
    if (!id.empty())
        return id;

    constexpr std::string_view attribute = "machineIdentifier=";
    const std::size_t at = body.find(attribute);
    if (at == std::string::npos)
        return {};
    const std::size_t quoteAt = at + attribute.size();
    if (quoteAt >= body.size() || (body[quoteAt] != '"' && body[quoteAt] != '\''))
        return {};
    const char quote = body[quoteAt];
    const std::size_t end = body.find(quote, quoteAt + 1);
    return end == std::string::npos ? std::string() : body.substr(quoteAt + 1, end - quoteAt - 1);
}

v1::CmdStatus statusForHttp(const net::HttpClient::Result &result)
{
    if (result.unauthorized || result.status == 401 || result.status == 403)
        return v1::CmdStatus::NotAuthorized;
    if (result.status >= 400)
        return v1::CmdStatus::Failure;
    return v1::CmdStatus::TemporarilyOffline;
}

std::string errorForHttp(const net::HttpClient::Result &result, const char *fallback)
{
    if (!result.error.empty())
        return result.error;
    if (result.status > 0)
        return std::string(fallback) + " (HTTP " + std::to_string(result.status) + ")";
    return fallback;
}

} // namespace phicore::plex::ipc
