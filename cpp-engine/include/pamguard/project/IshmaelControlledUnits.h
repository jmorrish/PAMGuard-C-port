#pragma once

#include "pamguard/project/ControlledUnitRegistry.h"

namespace pamguard::project {

[[nodiscard]] ControlledUnitDescriptor
make_ishmael_energy_sum_controlled_unit_descriptor();

[[nodiscard]] ControlledUnitDescriptor
make_ishmael_sgram_corr_controlled_unit_descriptor();

[[nodiscard]] ControlledUnitDescriptor
make_ishmael_match_filter_controlled_unit_descriptor();

} // namespace pamguard::project
