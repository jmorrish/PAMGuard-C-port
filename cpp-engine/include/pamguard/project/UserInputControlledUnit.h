#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

#include "pamguard/project/ControlledUnitRegistry.h"

namespace pamguard::project {

inline constexpr std::string_view
    kUserInputControlledUnitTypeId = "pamguard.user-input";
inline constexpr std::string_view
    kUserInputDataType = "pamguard.user-input-data";
inline constexpr std::string_view
    kUserInputRuntimeSettingsAdapterId =
        "pamguard.user-input-settings.v1";

// Pinned Java UserInputController/UserInputProcess constants.
inline constexpr std::uint32_t kUserInputChannelBitmap = 0xFFFFu;
inline constexpr std::uint32_t
    kUserInputMaximumCommentUtf16CodeUnits = 2'550'000u;
inline constexpr std::uint64_t
    kUserInputNaturalLifetimeSeconds = 86'400u;
inline constexpr std::uint64_t
    kUserInputRequiredHistoryMilliseconds = 172'800'000u;

/**
 * Exact public data carried by one Java UserInputDataUnit.
 *
 * This deliberately does not reuse core::GraphOperatorEvent. Java User Input
 * records contain a timestamp and one user string; they do not contain a
 * category, label, numeric value, or independently editable notes field.
 */
struct UserInputDataEntry {
    std::int64_t time_milliseconds = 0;
    std::string user_string;
    std::uint32_t channel_bitmap = kUserInputChannelBitmap;

    bool operator==(const UserInputDataEntry&) const = default;
};

class UserInputProjectAdapterError final
    : public std::invalid_argument {
public:
    UserInputProjectAdapterError(
        std::string code,
        std::string message);

    [[nodiscard]] const std::string& code() const noexcept;

private:
    std::string code_;
};

/**
 * Java-authoritative User Input controlled-unit descriptor for
 * PAMGuard 2.02.18e.
 *
 * The descriptor remains unavailable until pamguard.user-input has a
 * dedicated UserInputDataEntry runtime payload. The existing generic
 * GraphOperatorEvent implementation is intentionally not accepted as parity.
 */
[[nodiscard]] ControlledUnitDescriptor
make_user_input_controlled_unit_descriptor();

/**
 * Validate Java's empty portable settings document and project the fixed
 * UserInputProcess/data-block constants required by a dedicated runtime.
 */
[[nodiscard]] std::string user_input_runtime_settings_json(
    std::string_view portable_settings_json,
    std::uint32_t settings_version);

/**
 * Convert the operator action {"comment":"..."} to one exact
 * UserInputDataUnit-shaped entry. The service supplies the timestamp; clients
 * cannot spoof it through the action document.
 *
 * Java's form prevents an empty submit. One entry is limited to
 * UserInputController.maxCommentLength Java UTF-16 code units. Storage-level
 * splitting into 255-character SQL rows is deliberately not part of this
 * public data model.
 */
[[nodiscard]] UserInputDataEntry
user_input_data_entry_from_submit_action_json(
    std::string_view action_json,
    std::int64_t time_milliseconds);

/** Serialize the dedicated runtime/public data shape canonically. */
[[nodiscard]] std::string user_input_data_entry_to_json(
    const UserInputDataEntry& entry);

} // namespace pamguard::project
