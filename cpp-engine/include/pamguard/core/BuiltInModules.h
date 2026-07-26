#pragma once

#include "pamguard/core/ModuleGraph.h"

namespace pamguard::core {

/** Register the composable module types exposed by the runtime and browser. */
void register_builtin_module_types(ModuleRegistry& registry);

} // namespace pamguard::core
