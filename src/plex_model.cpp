#include "plex_model.h"

#include "plex_json.h"

namespace phicore::plex::ipc {

namespace v1 = phicore::adapter::v1;

v1::Device buildDevice(const PlexSettings &settings)
{
    v1::Device device;
    device.externalId = settings.deviceId();
    device.name = settings.playerName.empty() ? settings.playerHost : settings.playerName;
    device.deviceClass = v1::DeviceClass::MediaPlayer;
    device.manufacturer = "Plex";
    device.model = "Plex Companion Player";
    device.mediaPlayerFeatures = v1::MediaPlayerFeature::Play
        | v1::MediaPlayerFeature::Pause
        | v1::MediaPlayerFeature::Stop
        | v1::MediaPlayerFeature::Next
        | v1::MediaPlayerFeature::Previous
        | v1::MediaPlayerFeature::SeekTo;
    if (settings.hasServer() && !settings.serverMachineIdentifier.empty())
        device.mediaPlayerFeatures |= v1::MediaPlayerFeature::PlayMedia;
    device.metaJson = dump(Json{
        {"playerClientIdentifier", settings.playerClientIdentifier},
        {"serverMachineIdentifier", settings.serverMachineIdentifier},
    });
    return device;
}

} // namespace phicore::plex::ipc
