#pragma once

#include "pamguard/project/ControlledUnitRegistry.h"

namespace pamguard::project {

/** Register the first Java-authoritative controlled-unit registry slice. */
void register_builtin_controlled_units(ControlledUnitRegistry& registry);

} // namespace pamguard::project
