#include "plex_model.h"

#include "plex_json.h"

namespace phicore::plex::ipc {

namespace v1 = phicore::adapter::v1;

v1::Device buildDevice(const PlexSettings &settings)
{
    v1::Device device;
    device.setExternalId(settings.deviceId());
    device.setName(settings.playerName.empty() ? settings.playerHost : settings.playerName);
    device.setDeviceClass(v1::DeviceClass::MediaPlayer);
    device.setManufacturer("Plex");
    device.setModel("Plex Companion Player");
    v1::MediaPlayerFeatures features = v1::MediaPlayerFeature::Play
        | v1::MediaPlayerFeature::Pause
        | v1::MediaPlayerFeature::Stop
        | v1::MediaPlayerFeature::Next
        | v1::MediaPlayerFeature::Previous
        | v1::MediaPlayerFeature::SeekTo;
    if (settings.hasServer() && !settings.serverMachineIdentifier.empty())
        features |= v1::MediaPlayerFeature::PlayMedia;
    device.setMediaPlayerFeatures(features);
    device.setMetaJson(dump(Json{
        {"playerClientIdentifier", settings.playerClientIdentifier},
        {"serverMachineIdentifier", settings.serverMachineIdentifier},
    }));
    return device;
}

} // namespace phicore::plex::ipc
