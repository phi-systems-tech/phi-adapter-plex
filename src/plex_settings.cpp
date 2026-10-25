#include "plex_settings.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

#include "phi/adapter/v1/tlsconfig.h"
#include "phi/runtime/str.h"

namespace phicore::plex::ipc {

namespace net = phicore::adapter::net;
namespace str = phi::str;
namespace v1 = phicore::adapter::v1;
using namespace std::chrono_literals;

namespace {

std::string baseUrl(const std::string &host, int port, bool tls)
{
    if (host.empty())
        return {};
    return std::string(tls ? "https://" : "http://") + host + ":" + str::number(port);
}

void addPlexHeaders(net::HttpClient::Call &call, const PlexSettings &settings)
{
    call.addHeader("Accept", "application/json");
    call.addHeader("User-Agent", "phi-adapter-plex-ipc/1.0");
    call.addHeader("X-Plex-Product", "Phi Plex Adapter");
    call.addHeader("X-Plex-Version", "1.0.0");
    call.addHeader("X-Plex-Device", "phi-core");
    call.addHeader("X-Plex-Platform", "Linux");
    call.addHeader("X-Plex-Provides", "controller");
    call.addHeader("X-Plex-Client-Identifier", settings.controllerIdentifier);
    if (!settings.playerClientIdentifier.empty())
        call.addHeader("X-Plex-Target-Client-Identifier", settings.playerClientIdentifier);
    if (!settings.token.empty())
        call.addHeader("X-Plex-Token", settings.token);
    call.setTimeout(10s);
}

std::string metaOr(const Json &meta, const char *key, std::string fallback)
{
    return meta.contains(key) ? jsonString(meta, key) : std::move(fallback);
}

} // namespace

std::string PlexSettings::serverBaseUrl() const
{
    return baseUrl(serverHost, serverPort, serverUseTls);
}

std::string PlexSettings::playerBaseUrl() const
{
    return baseUrl(playerHost, playerPort, playerUseTls);
}

std::string PlexSettings::deviceId() const
{
    return playerClientIdentifier.empty() ? controllerIdentifier + ":player"
                                          : playerClientIdentifier;
}

bool PlexSettings::hasServer() const
{
    return !serverHost.empty() && serverPort > 0 && !token.empty();
}

bool PlexSettings::hasPlayer() const
{
    return !playerHost.empty() && playerPort > 0 && !playerClientIdentifier.empty();
}

PlexSettings settingsFromAdapter(const v1::Adapter &adapter, const Json &meta)
{
    PlexSettings out;
    out.serverHost = str::trimmed(adapter.ip().empty() ? adapter.host() : adapter.ip());
    out.serverPort = adapter.port() > 0 ? static_cast<int>(adapter.port()) : 32400;
    out.token = str::trimmed(adapter.token());
    out.serverUseTls = v1::hasFlag(adapter.options(), v1::AdapterOption::UseTls);
    out.controllerIdentifier = "phi-plex-" + str::trimmed(adapter.externalId());
    if (out.controllerIdentifier == "phi-plex-")
        out.controllerIdentifier = "phi-plex-adapter";

    out.serverHost = metaOr(meta, "serverHost", out.serverHost);
    out.serverPort = jsonInt(meta, "serverPort", out.serverPort);
    out.serverUseTls = jsonBool(meta, "serverUseTls", out.serverUseTls);
    out.token = metaOr(meta, "token", out.token);
    out.serverMachineIdentifier = jsonString(meta, "serverMachineIdentifier");
    out.playerHost = jsonString(meta, "playerHost");
    out.playerPort = jsonInt(meta, "playerPort", 32500);
    out.playerUseTls = jsonBool(meta, "playerUseTls", false);
    out.playerClientIdentifier = jsonString(meta, "playerClientIdentifier");
    out.playerName = jsonString(meta, "playerName");
    out.controllerIdentifier = metaOr(meta, "controllerIdentifier", out.controllerIdentifier);
    out.musicBitrate = std::clamp(jsonInt(meta, "musicBitrate", 192), 64, 320);
    out.artistCacheMinutes = std::clamp(jsonInt(meta, "artistCacheMinutes", 15), 1, 24 * 60);
    out.artistPickCount = std::clamp(jsonInt(meta, "artistPickCount", 10), 1, 50);
    for (const std::string &key : str::split(jsonString(meta, "musicSections"), ',')) {
        const std::string k = str::trimmed(key);
        if (!k.empty()) out.musicSections.push_back(k);
    }
    return out;
}

void applyProbeParams(const Json &params, PlexSettings &settings)
{
    if (!params.is_object())
        return;
    settings.serverHost = metaOr(params, "serverHost", settings.serverHost);
    settings.serverHost = metaOr(params, "host", settings.serverHost);
    settings.serverPort = jsonInt(params, "serverPort",
                                  jsonInt(params, "port", settings.serverPort));
    settings.serverUseTls = jsonBool(params, "serverUseTls", settings.serverUseTls);
    settings.token = metaOr(params, "token", settings.token);
}

std::string urlEncode(std::string_view value)
{
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    for (const unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[(c >> 4) & 0x0F]);
            out.push_back(hex[c & 0x0F]);
        }
    }
    return out;
}

net::HttpClient::Call serverCall(const PlexSettings &settings, std::string_view path)
{
    net::HttpClient::Call call(settings.serverBaseUrl() + (path.starts_with('/') ? "" : "/")
                               + std::string(path));
    addPlexHeaders(call, settings);
    call.tls().setEnabled(settings.serverUseTls);
    return call;
}

net::HttpClient::Call playerCall(const PlexSettings &settings, std::string_view path)
{
    net::HttpClient::Call call(settings.playerBaseUrl() + (path.starts_with('/') ? "" : "/")
                               + std::string(path));
    addPlexHeaders(call, settings);
    call.tls().setEnabled(settings.playerUseTls);
    return call;
}

} // namespace phicore::plex::ipc
