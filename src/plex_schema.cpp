#include "plex_schema.h"

#include <utility>

namespace phicore::plex::ipc {

namespace v1 = phicore::adapter::v1;

namespace {

v1::AdapterConfigField field(const char *key, v1::AdapterConfigFieldType type,
                             const char *label, const char *description,
                             v1::ScalarValue defaultValue = {})
{
    v1::AdapterConfigField out(key, type, label);
    out.setDescription(description).setDefaultValue(std::move(defaultValue));
    return out;
}

v1::AdapterConfigSection section()
{
    using Type = v1::AdapterConfigFieldType;
    using Flag = v1::AdapterConfigFieldFlag;
    v1::AdapterConfigSection out;
    out.setTitle("Plex server and player");
    out.setDescription("The media server owns the library, which \"play ...\" searches. "
                       "A Plex player is optional: with one, the instance is also that player.");
    out.layout().setColumns(2);

    auto serverHost = field("serverHost", Type::Hostname, "Server host",
                            "Plex Media Server hostname or IP address.");
    serverHost.setFlags(Flag::Required);
    auto serverPort = field("serverPort", Type::Port, "Server port",
                            "Plex Media Server HTTP port.", std::int64_t{32400});
    serverPort.layout().setControlWidth(v1::AdapterConfigSize::Narrow);
    auto serverTls = field("serverUseTls", Type::Boolean, "Server HTTPS",
                           "Use HTTPS for the media server.", false);
    auto token = field("token", Type::Password, "Plex token",
                       "Access token used for server and Companion requests.");
    token.setFlags(Flag::Required | Flag::Secret);
    auto serverId = field("serverMachineIdentifier", Type::String, "Server identifier",
                          "Optional PMS machineIdentifier; detected from /identity when omitted.");

    auto playerHost = field("playerHost", Type::Hostname, "Player host",
                            "Hostname or IP address of the Plex Companion player; empty for a server-only instance.");
    auto playerPort = field("playerPort", Type::Port, "Player port",
                            "Plex Companion control port (commonly 32500 or 32400).",
                            std::int64_t{32500});
    playerPort.layout().setControlWidth(v1::AdapterConfigSize::Narrow);
    auto playerTls = field("playerUseTls", Type::Boolean, "Player HTTPS",
                           "Use HTTPS for Companion control.", false);
    auto playerId = field("playerClientIdentifier", Type::String, "Player identifier",
                          "The target player's Plex clientIdentifier; empty for a server-only instance.");
    auto playerName = field("playerName", Type::String, "Player name",
                            "Display name for this player.");
    auto controllerId = field("controllerIdentifier", Type::String, "Controller identifier",
                              "Stable X-Plex-Client-Identifier; normally left empty.");

    // How "play ..." searches and plays.
    auto searchHeading = field("searchHeading", Type::Section, "Search and playback",
                               "How a spoken \"play ...\" searches this library and what it plays.");
    auto bitrate = field("musicBitrate", Type::Integer, "Bitrate (kbit/s)",
                         "What the transcoder sends a satellite: 128 saves the network, 320 is as good as MP3 gets.",
                         std::int64_t{192});
    bitrate.setMinValue(64).setMaxValue(320).setStep(32);
    bitrate.layout().setControlWidth(v1::AdapterConfigSize::Narrow);
    auto cache = field("artistCacheMinutes", Type::Integer, "Artist list kept (minutes)",
                       "How long the library's artists are kept before they are read again.", std::int64_t{15});
    cache.setMinValue(1).setMaxValue(1440);
    cache.layout().setControlWidth(v1::AdapterConfigSize::Narrow);
    auto pick = field("artistPickCount", Type::Integer, "Best-known tracks to pick from",
                      "\"Play <artist>\" plays one of this many of the artist's best-known tracks, at random.",
                      std::int64_t{10});
    pick.setMinValue(1).setMaxValue(50);
    pick.layout().setControlWidth(v1::AdapterConfigSize::Narrow);
    auto sections = field("musicSections", Type::String, "Music libraries",
                          "Section keys to search, comma-separated; empty searches every music library.");

    out.setFields({serverHost, serverPort, serverTls, token, serverId,
                   playerHost, playerPort, playerTls, playerId, playerName, controllerId,
                   searchHeading, bitrate, cache, pick, sections});
    return out;
}

} // namespace

v1::Utf8String displayName() { return "Plex"; }
v1::Utf8String description() { return "Controls a Plex Companion player using a Plex Media Server library"; }

v1::Utf8String iconSvg()
{
    return "<svg width=\"24\" height=\"24\" viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\" role=\"img\" aria-label=\"Plex\">"
           "<rect x=\"2\" y=\"2\" width=\"20\" height=\"20\" rx=\"5\" fill=\"#1f1f1f\"/>"
           "<path d=\"M9 6h3.2L17 12l-4.8 6H9l4.8-6z\" fill=\"#e5a00d\"/>"
           "</svg>";
}

v1::AdapterCapabilities capabilities()
{
    v1::AdapterCapabilities caps;
    caps.setRequired(v1::AdapterRequirement::Host | v1::AdapterRequirement::Port
                     | v1::AdapterRequirement::Token);
    caps.setOptional(v1::AdapterRequirement::SupportsTls);
    caps.setFlags(v1::AdapterCapability::SupportsProbe);
    // The server is reached on the LAN; plex.tv only hands out the token.
    caps.setTopologies(v1::Topology::LocalCloudSetup);

    caps.addFactoryAction({"probe", "Test Plex server",
                           "Validate the Plex Media Server address and token."});
    // The convention core's cmd.media.play looks for (phi-adapter-sdk README,
    // "media.search"): {query} in, items with a playable mediaId out. Not a
    // button - hidden from the UI.
    v1::AdapterActionDescriptor search("media.search", "Search the library",
                                       "What the library has for a title, an artist or both.");
    search.setPlacement(v1::AdapterActionPlacement::Hidden).setTimeoutMs(15000);
    caps.addInstanceAction(search);
    // cmd.media.play with a step: {metadata, step} of an item answered
    // before in, its album's next or previous track out, as items.
    v1::AdapterActionDescriptor step("media.step", "Next on the album",
                                     "The album's next or previous track after one this library answered with.");
    step.setPlacement(v1::AdapterActionPlacement::Hidden).setTimeoutMs(15000);
    caps.addInstanceAction(step);
    return caps;
}

v1::AdapterConfigSchema configSchema()
{
    v1::AdapterConfigSchema schema;
    schema.setFactory(section());
    schema.setInstance(section());
    return schema;
}

} // namespace phicore::plex::ipc
