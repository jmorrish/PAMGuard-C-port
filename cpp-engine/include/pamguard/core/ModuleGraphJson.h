#pragma once

#include <string>
#include <string_view>

#include "pamguard/core/ModuleGraph.h"

namespace pamguard::core {

[[nodiscard]] std::string module_graph_to_json(
    const ModuleGraphDocument& document,
    bool pretty = false);
[[nodiscard]] ModuleGraphDocument module_graph_from_json(std::string_view json);

} // namespace pamguard::core
