#include "plex_schema.h"

#include <utility>

namespace phicore::plex::ipc {

namespace v1 = phicore::adapter::v1;

namespace {

v1::AdapterConfigField field(const char *key, v1::AdapterConfigFieldType type,
                             const char *label, const char *description,
                             v1::ScalarValue defaultValue = {})
{
    v1::AdapterConfigField out;
    out.key = key;
    out.type = type;
    out.label = label;
    out.description = description;
    out.defaultValue = std::move(defaultValue);
    return out;
}

v1::AdapterConfigSection section()
{
    using Type = v1::AdapterConfigFieldType;
    using Flag = v1::AdapterConfigFieldFlag;
    v1::AdapterConfigSection out;
    out.title = "Plex server and player";
    out.description = "The media server owns the library; the selected Plex player accepts Companion commands.";
    out.layout.columns = 2;

    auto serverHost = field("serverHost", Type::Hostname, "Server host",
                            "Plex Media Server hostname or IP address.");
    serverHost.flags = Flag::Required;
    auto serverPort = field("serverPort", Type::Port, "Server port",
                            "Plex Media Server HTTP port.", std::int64_t{32400});
    serverPort.layout.controlWidth = v1::AdapterConfigSize::Narrow;
    auto serverTls = field("serverUseTls", Type::Boolean, "Server HTTPS",
                           "Use HTTPS for the media server.", false);
    auto token = field("token", Type::Password, "Plex token",
                       "Access token used for server and Companion requests.");
    token.flags = Flag::Required | Flag::Secret;
    auto serverId = field("serverMachineIdentifier", Type::String, "Server identifier",
                          "Optional PMS machineIdentifier; detected from /identity when omitted.");

    auto playerHost = field("playerHost", Type::Hostname, "Player host",
                            "Hostname or IP address of the Plex Companion player.");
    playerHost.flags = Flag::Required;
    auto playerPort = field("playerPort", Type::Port, "Player port",
                            "Plex Companion control port (commonly 32500 or 32400).",
                            std::int64_t{32500});
    playerPort.layout.controlWidth = v1::AdapterConfigSize::Narrow;
    auto playerTls = field("playerUseTls", Type::Boolean, "Player HTTPS",
                           "Use HTTPS for Companion control.", false);
    auto playerId = field("playerClientIdentifier", Type::String, "Player identifier",
                          "The target player's Plex clientIdentifier.");
    playerId.flags = Flag::Required;
    auto playerName = field("playerName", Type::String, "Player name",
                            "Display name for this player.");
    auto controllerId = field("controllerIdentifier", Type::String, "Controller identifier",
                              "Stable X-Plex-Client-Identifier; normally left empty.");

    out.fields = {serverHost, serverPort, serverTls, token, serverId,
                  playerHost, playerPort, playerTls, playerId, playerName, controllerId};
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
    caps.required = v1::AdapterRequirement::Host | v1::AdapterRequirement::Port
        | v1::AdapterRequirement::Token;
    caps.optional = v1::AdapterRequirement::SupportsTls;
    caps.flags = v1::AdapterCapability::SupportsProbe;

    v1::AdapterActionDescriptor probe;
    probe.id = "probe";
    probe.label = "Test Plex server";
    probe.description = "Validate the Plex Media Server address and token.";
    caps.factoryActions.push_back(probe);
    return caps;
}

v1::AdapterConfigSchema configSchema()
{
    v1::AdapterConfigSchema schema;
    schema.factory = section();
    schema.instance = section();
    return schema;
}

} // namespace phicore::plex::ipc
