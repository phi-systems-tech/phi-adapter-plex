#pragma once

#include "phi/adapter/v1/device.h"
#include "plex_settings.h"

namespace phicore::plex::ipc {

phicore::adapter::v1::Device buildDevice(const PlexSettings &settings);

} // namespace phicore::plex::ipc
