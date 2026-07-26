#pragma once

#include "pamguard/project/ControlledUnitRegistry.h"

namespace pamguard::project {

/**
 * Java-authoritative Filters.FilterControl descriptor. The selected
 * FilterParameters_2.rawDataSource is represented by the public rawAudio
 * binding.
 */
[[nodiscard]] ControlledUnitDescriptor
make_standalone_filter_controlled_unit_descriptor();

/**
 * Java-authoritative decimator.DecimatorControl descriptor. Viewer-only
 * offline WAV storage is deliberately outside the runtime child recipe.
 */
[[nodiscard]] ControlledUnitDescriptor
make_decimator_controlled_unit_descriptor();

} // namespace pamguard::project
