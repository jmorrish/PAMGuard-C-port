#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace pamguard::core {

inline constexpr std::size_t kPamguardSignalChannelLimit = 32;

struct AmplifierChannelSettings {
    double gain_db = 0.0;
    bool invert = false;

    bool operator==(const AmplifierChannelSettings&) const = default;
};

/**
 * Portable form of amplifier.AmpParameters.
 *
 * Java persists one signed linear gain per absolute channel index. AmpDialog
 * exposes the magnitude as Gain (dB) and the sign as Invert, so those are the
 * canonical operator-facing values used by projects.
 */
struct SignalAmplifierSettings {
    std::array<
        AmplifierChannelSettings,
        kPamguardSignalChannelLimit>
        channel_settings{};

    [[nodiscard]] double signed_linear_gain(
        std::size_t channel) const;
    [[nodiscard]] std::vector<double> signed_linear_gains() const;

    bool operator==(const SignalAmplifierSettings&) const = default;
};

using PatchRoutingMatrix = std::array<
    std::array<bool, kPamguardSignalChannelLimit>,
    kPamguardSignalChannelLimit>;
using PatchGainMatrix = std::array<
    std::array<double, kPamguardSignalChannelLimit>,
    kPamguardSignalChannelLimit>;

/**
 * Portable form of patchPanel.PatchPanelParameters.
 *
 * PatchPanelDialog exposes a boolean input-by-output checkbox matrix. The
 * nullable advanced matrix is an explicit C++ extension retaining the earlier
 * arbitrary-gain capability; when present it replaces the unit coefficients
 * implied by routing_matrix.
 */
struct PatchPanelSettings {
    PatchRoutingMatrix routing_matrix{};
    std::optional<PatchGainMatrix> advanced_gain_matrix;

    [[nodiscard]] double coefficient(
        std::size_t input_channel,
        std::size_t output_channel) const;
    [[nodiscard]] std::vector<std::vector<double>>
    coefficient_matrix() const;

    bool operator==(const PatchPanelSettings&) const = default;
};

class SignalRoutingSettingsError final : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

[[nodiscard]] SignalAmplifierSettings
signal_amplifier_settings_from_json(
    std::string_view settings_json,
    std::uint32_t settings_version);

[[nodiscard]] PatchPanelSettings patch_panel_settings_from_json(
    std::string_view settings_json,
    std::uint32_t settings_version);

[[nodiscard]] std::string signal_amplifier_default_settings_json();
[[nodiscard]] std::string patch_panel_default_settings_json();

[[nodiscard]] std::string_view
signal_amplifier_settings_schema_json() noexcept;
[[nodiscard]] std::string_view
patch_panel_settings_schema_json() noexcept;

} // namespace pamguard::core
