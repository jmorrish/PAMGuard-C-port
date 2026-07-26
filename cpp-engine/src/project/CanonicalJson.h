#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include <json.hpp>

namespace pamguard::project::detail {

inline constexpr std::size_t kMaximumProjectJsonBytes =
    16U * 1024U * 1024U;
inline constexpr std::size_t kMaximumEmbeddedSettingsBytes =
    1U * 1024U * 1024U;
inline constexpr std::size_t kMaximumAggregateEmbeddedSettingsBytes =
    8U * 1024U * 1024U;
inline constexpr std::size_t kMaximumJsonDepth = 64;
inline constexpr std::size_t kMaximumEmbeddedSettingsDepth = 32;

void validate_utf8(std::string_view value, std::string_view context);

[[nodiscard]] nlohmann::json parse_strict_json(
    std::string_view value,
    std::string_view context,
    std::size_t maximum_bytes,
    std::size_t maximum_depth);

void normalize_json_numbers(
    nlohmann::json& value,
    std::string_view context,
    std::size_t maximum_depth);

[[nodiscard]] std::string canonical_json_dump(nlohmann::json value);

[[nodiscard]] std::size_t utf8_scalar_count(
    std::string_view value,
    std::string_view context);

} // namespace pamguard::project::detail
