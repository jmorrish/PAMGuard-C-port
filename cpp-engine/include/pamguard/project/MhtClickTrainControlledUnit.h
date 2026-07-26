#pragma once

#include "pamguard/project/ControlledUnitRegistry.h"

namespace pamguard::project {

/**
 * Java-authoritative descriptor for
 * clickTrainDetector.ClickTrainControl in PAMGuard 2.02.18e.
 */
[[nodiscard]] ControlledUnitDescriptor
make_mht_click_train_controlled_unit_descriptor();

} // namespace pamguard::project
