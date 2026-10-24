#pragma once

#include <cstdint>
#include <string>

#include "phi/adapter/net/http_client.h"
#include "phi/adapter/sdk/sidecar.h"

#include "plex_settings.h"

namespace phicore::plex::ipc {

struct CommandBuildResult {
    bool ok = false;
    phicore::adapter::net::HttpClient::Call call;
    phicore::adapter::v1::CmdStatus status = phicore::adapter::v1::CmdStatus::InvalidArgument;
    std::string error;
};

CommandBuildResult buildMediaCommand(const PlexSettings &settings,
                                     const phicore::adapter::sdk::MediaPlayerInvokeRequest &request,
                                     std::uint64_t commandId);

/// Reads either Plex JSON (`MediaContainer.machineIdentifier`) or the legacy
/// XML identity document. Empty means the response did not identify a server.
std::string machineIdentifierFromIdentity(const std::string &body);

phicore::adapter::v1::CmdStatus statusForHttp(
    const phicore::adapter::net::HttpClient::Result &result);
std::string errorForHttp(const phicore::adapter::net::HttpClient::Result &result,
                         const char *fallback);

} // namespace phicore::plex::ipc
