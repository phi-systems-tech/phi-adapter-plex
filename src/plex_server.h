#pragma once
// The server as a device: what it is, whether it answers, and what its own
// players play.
//
// What Plex can say about a stream it serves: its sessions (/status/sessions)
// are the players that report a timeline - the Plex apps, Plexamp, a TV. A
// satellite fed by `media.search` is not one of them: the transcoder hands
// over a file and hears nothing more, so what a satellite plays is the
// satellite's own channels (phi-transport-voice), not this device's.
#include <cstdint>
#include <string>

#include "phi/adapter/v1/channel.h"
#include "phi/adapter/v1/device.h"
#include "plex_settings.h"

namespace phicore::plex::ipc {

/// `/` on the server: its name, version and identity.
struct ServerInfo {
    std::string name;
    std::string version;
    std::string machineIdentifier;
};
ServerInfo parseServerRoot(const std::string &body);

/// `/status/sessions` read down to what the device shows: how many streams,
/// and the one that matters - the first playing one, else the first.
struct SessionsSummary {
    int count = 0;
    std::string title;
    std::string artist;
    std::string playedOn;
    std::int64_t positionS = 0;
    std::int64_t durationS = 0;
    bool playing = false;
};
SessionsSummary parseSessions(const std::string &body);

std::string serverRootPath();
std::string sessionsPath();

/// The server device's external id, one per instance.
inline constexpr const char kServerDeviceId[] = "server";

namespace serverch {
inline constexpr const char *Connectivity = "connectivity";
inline constexpr const char *Sessions = "sessions";
inline constexpr const char *Playing = "nowPlaying";
inline constexpr const char *Title = "nowTitle";
inline constexpr const char *Artist = "nowArtist";
inline constexpr const char *PlayedOn = "nowPlayedOn";
inline constexpr const char *Position = "nowPosition";
inline constexpr const char *Duration = "nowDuration";
}

phicore::adapter::v1::Device buildServerDevice(const ServerInfo &info);
phicore::adapter::v1::ChannelList serverChannels();

} // namespace phicore::plex::ipc
