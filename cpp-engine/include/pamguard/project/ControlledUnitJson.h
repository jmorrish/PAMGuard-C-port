#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

#include "pamguard/project/ControlledUnitRegistry.h"

namespace pamguard::project {

inline constexpr std::uint32_t kControlledUnitCatalogueSchemaVersion = 1;
inline constexpr std::string_view kControlledUnitDescriptorSetId =
    "pamguard-2.02.18e";
inline constexpr std::uint32_t kControlledUnitDescriptorSetVersion = 1;
inline constexpr std::string_view kControlledUnitAuthorityCommit =
    "dca55c81ef6f1498a8a3b926c69e7182afb915ee";

class ControlledUnitJsonError : public std::invalid_argument {
public:
    explicit ControlledUnitJsonError(const std::string& message)
        : std::invalid_argument(message) {}
};

/**
 * Serialize the operator-visible controlled-unit catalogue contract.
 *
 * The compact form is deterministic. Settings defaults and schemas are parsed
 * as strict embedded JSON and emitted as JSON values, never escaped strings.
 * Invalid registries, schemas, or defaults throw ControlledUnitJsonError.
 */
[[nodiscard]] std::string controlled_unit_catalogue_to_json(
    const ControlledUnitRegistry& registry,
    bool pretty = false);

} // namespace pamguard::project
