#pragma once

#include <memory>

#include "phi/adapter/sdk/sidecar.h"

namespace phicore::plex::ipc {

std::unique_ptr<phicore::adapter::sdk::AdapterInstance> makeInstance();

} // namespace phicore::plex::ipc
