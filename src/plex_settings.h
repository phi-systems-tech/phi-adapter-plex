#pragma once

#include <string>
#include <vector>
#include <string_view>

#include "phi/adapter/net/http_client.h"
#include "phi/adapter/v1/schema.h"

#include "plex_json.h"

namespace phicore::plex::ipc {

struct PlexSettings {
    std::string serverHost;
    int serverPort = 32400;
    bool serverUseTls = false;
    std::string token;
    std::string serverMachineIdentifier;

    std::string playerHost;
    int playerPort = 32500;
    bool playerUseTls = false;
    std::string playerClientIdentifier;
    std::string playerName;
    std::string controllerIdentifier;

    /// What a search plays through the transcoder at, kbit/s.
    int musicBitrate = 192;
    /// How long the library's artist list is kept before it is read again.
    int artistCacheMinutes = 15;
    /// "Play <artist>": one of this many best-known tracks, at random.
    int artistPickCount = 10;
    /// Which music sections a search reads, by key; empty is all of them.
    std::vector<std::string> musicSections;

    [[nodiscard]] std::string serverBaseUrl() const;
    [[nodiscard]] std::string playerBaseUrl() const;
    [[nodiscard]] std::string deviceId() const;
    [[nodiscard]] bool hasServer() const;
    [[nodiscard]] bool hasPlayer() const;
};

PlexSettings settingsFromAdapter(const phicore::adapter::v1::Adapter &adapter, const Json &meta);
void applyProbeParams(const Json &params, PlexSettings &settings);

std::string urlEncode(std::string_view value);
phicore::adapter::net::HttpClient::Call serverCall(const PlexSettings &settings,
                                                    std::string_view path);
phicore::adapter::net::HttpClient::Call playerCall(const PlexSettings &settings,
                                                    std::string_view path);

} // namespace phicore::plex::ipc
