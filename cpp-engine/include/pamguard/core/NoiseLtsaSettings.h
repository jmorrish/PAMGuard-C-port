#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "pamguard/detectors/NoiseBandMonitor.h"

namespace pamguard::core {

/**
 * Portable settings for noiseMonitor.NoiseSettings and its persisted
 * NoiseMeasurementBand rows. The Java dataSource member is represented by
 * the controlled unit's public FFT binding.
 */
struct FftNoiseMeasurementBandSettings {
    std::string name;
    double low_frequency_hz = 0.0;
    double high_frequency_hz = 0.0;
    /**
     * null is Java's user-defined band. The four non-null values are the
     * standard band families exposed by NoiseDialog.
     */
    std::optional<detectors::NoiseBandType> band_type;

    bool operator==(
        const FftNoiseMeasurementBandSettings&) const = default;
};

struct FftNoiseMonitorSettings {
    std::uint32_t channel_bitmap = 1;
    int measurement_interval_seconds = 60;
    int n_measures = 100;
    bool use_all = true;
    std::vector<FftNoiseMeasurementBandSettings> bands;

    bool operator==(const FftNoiseMonitorSettings&) const = default;
};

enum class NoiseBandFilterType {
    Butterworth,
    FirWindow,
};

/**
 * Portable scientific settings for noiseBandMonitor.NoiseBandSettings.
 * Java's source selection and plot/display preferences deliberately live
 * outside this object.
 */
struct NoiseBandMonitorSettings {
    std::uint32_t channel_bitmap = 1;
    detectors::NoiseBandType band_type =
        detectors::NoiseBandType::ThirdOctave;
    NoiseBandFilterType filter_type =
        NoiseBandFilterType::Butterworth;
    int iir_order = 6;
    int fir_order = 7;
    double fir_gamma = 2.5;
    int output_interval_seconds = 10;
    double minimum_frequency_hz = 1.7925856629456591;
    double maximum_frequency_hz = 1133.6866687924667;
    double reference_frequency_hz = 1000.0;

    bool operator==(const NoiseBandMonitorSettings&) const = default;
};

/**
 * Portable settings for ltsa.LtsaParameters. longer_factor is retained as
 * Java-authoritative persisted state even though the pinned Java process has
 * its longer-average output commented out.
 */
struct LtsaSettings {
    std::uint32_t channel_bitmap = 0;
    int interval_seconds = 60;
    int longer_factor = 10;

    bool operator==(const LtsaSettings&) const = default;
};

class NoiseLtsaSettingsError final : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

[[nodiscard]] FftNoiseMonitorSettings
fft_noise_monitor_settings_from_json(
    std::string_view settings_json,
    std::uint32_t settings_version);

[[nodiscard]] NoiseBandMonitorSettings
noise_band_monitor_settings_from_json(
    std::string_view settings_json,
    std::uint32_t settings_version);

[[nodiscard]] LtsaSettings ltsa_settings_from_json(
    std::string_view settings_json,
    std::uint32_t settings_version);

[[nodiscard]] std::string fft_noise_monitor_settings_to_json(
    const FftNoiseMonitorSettings& settings,
    std::uint32_t settings_version);

[[nodiscard]] std::string noise_band_monitor_settings_to_json(
    const NoiseBandMonitorSettings& settings,
    std::uint32_t settings_version);

[[nodiscard]] std::string ltsa_settings_to_json(
    const LtsaSettings& settings,
    std::uint32_t settings_version);

/** Pure canonical-controlled-settings to low-level-runtime projections. */
[[nodiscard]] std::string fft_noise_monitor_runtime_settings_json(
    const FftNoiseMonitorSettings& settings);
[[nodiscard]] std::string noise_band_monitor_runtime_settings_json(
    const NoiseBandMonitorSettings& settings);
[[nodiscard]] std::string ltsa_runtime_settings_json(
    const LtsaSettings& settings);

[[nodiscard]] std::string
fft_noise_monitor_default_settings_json();
[[nodiscard]] std::string
noise_band_monitor_default_settings_json();
[[nodiscard]] std::string ltsa_default_settings_json();

[[nodiscard]] std::string_view
fft_noise_monitor_settings_schema_json() noexcept;
[[nodiscard]] std::string_view
noise_band_monitor_settings_schema_json() noexcept;
[[nodiscard]] std::string_view
ltsa_settings_schema_json() noexcept;

} // namespace pamguard::core
