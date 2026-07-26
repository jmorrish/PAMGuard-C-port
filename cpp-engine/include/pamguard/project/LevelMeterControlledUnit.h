#pragma once

#include "pamguard/project/ControlledUnitRegistry.h"

namespace pamguard::project {

[[nodiscard]] ControlledUnitDescriptor
make_level_meter_controlled_unit_descriptor();

[[nodiscard]] DisplayProviderDescriptor
make_level_meter_display_provider_descriptor();

} // namespace pamguard::project
