#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "pamguard/detectors/CtClassifiers.h"
#include "pamguard/detectors/MhtKernel.h"
#include "pamguard/detectors/MhtSimpleChi2Vars.h"
#include "pamguard/detectors/StandardMhtChi2.h"

namespace pamguard::core {

/**
 * Scientifically active subset of clickDetector.alarm.ClickAlarmParameters
 * used by ClickTrainControl's source DataSelector.
 *
 * An empty included_click_types array is the portable form of Java's null
 * useSpeciesList: every click type, including type zero, is accepted.
 */
struct MhtClickDataSelectorSettings {
    bool enabled = false;
    bool use_echoes = true;
    double minimum_amplitude_db = 0.0;
    std::vector<int> included_click_types;
};

struct MhtIdiVariableSettings {
    bool enabled = true;
    double error = 0.2;
    double min_error = 0.0005;
    double min_idi_seconds = 0.0005;
};

struct MhtAmplitudeVariableSettings {
    bool enabled = true;
    double error = 30.0;
    double min_error = 1.0;
    bool jump_enabled = true;
    double maximum_jump_db = 10.0;
};

struct MhtBearingVariableSettings {
    bool enabled = true;
    double error_radians =
        4.0 * 3.141592653589793238462643383279502884 /
        180.0;
    double min_error_radians =
        2.0 * 3.141592653589793238462643383279502884 /
        180.0;
    bool jump_enabled = false;
    double maximum_jump_radians =
        20.0 * 3.141592653589793238462643383279502884 /
        180.0;
    detectors::MhtBearingJumpDirection jump_direction =
        detectors::MhtBearingJumpDirection::Positive;
};

struct MhtCorrelationVariableSettings {
    bool enabled = true;
    double error = 1.0;
    double min_error = 0.01;
};

struct MhtTimeDelayVariableSettings {
    bool enabled = true;
    double error = 1.0 / 1E6;
    double min_error = 1.0 / 1E9;
};

struct MhtLengthVariableSettings {
    bool enabled = true;
    double error = 0.2;
    double min_error = 0.002;
};

struct MhtPeakFrequencyVariableSettings {
    bool enabled = true;
    double error = 30.0;
    double min_error = 1.0;
};

struct MhtChi2Settings {
    double maximum_ici_seconds = 0.4;
    double coast_penalty = 10.0;
    double new_track_penalty = 50.0;
    std::size_t new_track_clicks = 3;
    double long_track_exponent = 0.1;
    double low_ici_exponent = 0.1;
    bool electrical_noise_filter_enabled = false;
    double electrical_noise_minimum_chi2 = 0.00001;
    std::size_t electrical_noise_data_units = 30;
    MhtIdiVariableSettings idi;
    MhtAmplitudeVariableSettings amplitude;
    MhtBearingVariableSettings bearing;
    MhtCorrelationVariableSettings correlation;
    MhtTimeDelayVariableSettings time_delay;
    MhtLengthVariableSettings length;
    MhtPeakFrequencyVariableSettings peak_frequency;
};

struct MhtPreClassifierSettings {
    double chi2_threshold = 1500.0;
    std::size_t minimum_clicks = 5;
    /**
     * Java supports a per-pre-classifier DataSelector here. The current graph
     * can execute only the Java default (zero, which short-circuits it).
     */
    double minimum_selected_percentage = 0.0;
    double minimum_time_seconds = 0.0;
    int species_flag = 1;
};

struct MhtIdiClassifierSettings {
    bool enabled = false;
    bool use_median_idi = true;
    double minimum_median_idi = 0.0;
    double maximum_median_idi = 2.0;
    bool use_mean_idi = false;
    double minimum_mean_idi = 0.0;
    double maximum_mean_idi = 2.0;
    bool use_std_idi = false;
    double minimum_std_idi = 0.0;
    double maximum_std_idi = 100.0;
    int species_flag = 1;
};

struct MhtBearingClassifierSettings {
    bool enabled = false;
    // Exact Double.toString(Math.toRadians(85)) constructor value.
    double minimum_bearing_radians = 1.4835298641951802;
    double maximum_bearing_radians =
        95.0 * 3.141592653589793238462643383279502884 /
        180.0;
    bool use_mean = false;
    double minimum_mean_derivative =
        -0.005 * 3.141592653589793238462643383279502884 /
        180.0;
    double maximum_mean_derivative =
        0.005 * 3.141592653589793238462643383279502884 /
        180.0;
    bool use_median = true;
    double minimum_median_derivative =
        -0.005 * 3.141592653589793238462643383279502884 /
        180.0;
    double maximum_median_derivative =
        0.005 * 3.141592653589793238462643383279502884 /
        180.0;
    bool use_std = true;
    double minimum_std_derivative = 0.0;
    // Exact Double.toString(Math.toRadians(1.5)) constructor value.
    double maximum_std_derivative = 0.026179938779914945;
    int species_flag = -1;
};

struct MhtTemplateClassifierSettings {
    MhtTemplateClassifierSettings();

    bool enabled = false;
    std::string template_name = "Beaked Whale";
    double template_sample_rate_hz = 192000.0;
    std::vector<double> template_spectrum;
    double correlation_threshold = 0.5;
    int species_flag = 1;
};

struct MhtSpeciesClassifierSettings {
    bool run_classifier = false;
    MhtPreClassifierSettings pre;
    MhtIdiClassifierSettings idi;
    MhtBearingClassifierSettings bearing;
    MhtTemplateClassifierSettings spectrum_template;
};

/**
 * CTLocParams is retained in the canonical document so its Java defaults are
 * visible. enabled=true is rejected until the C++ graph owns a target-motion
 * click-train localisation process.
 */
struct MhtTrainLocalisationSettings {
    bool enabled = false;
    std::size_t minimum_data_units = 20;
    double minimum_angle_range_radians =
        30.0 * 3.141592653589793238462643383279502884 /
        180.0;
};

/**
 * Portable, strict, Java-authoritative ClickTrainControl settings.
 *
 * dataSourceName/dataSourceIndex are represented by the public click binding.
 * ctDetectorType is represented by the fixed "mht" algorithm name because
 * 2.02.18e registers no second ClickTrainAlgorithm.
 */
struct MhtClickTrainSettings {
    std::vector<std::uint32_t> channel_groups{1};
    MhtClickDataSelectorSettings data_selector;
    detectors::MhtKernelParams kernel;
    MhtChi2Settings chi2;
    MhtSpeciesClassifierSettings classifier;
    MhtTrainLocalisationSettings localisation;
};

class MhtClickTrainSettingsError final
    : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

[[nodiscard]] MhtClickTrainSettings
mht_click_train_settings_from_json(
    std::string_view settings_json,
    std::uint32_t settings_version);

[[nodiscard]] std::string mht_click_train_settings_to_json(
    const MhtClickTrainSettings& settings,
    std::uint32_t settings_version);

[[nodiscard]] std::string
mht_click_train_default_settings_json();

[[nodiscard]] std::string_view
mht_click_train_settings_schema_json() noexcept;

/**
 * Pure controlled-unit -> low-level MhtClickTrainNode settings adapter.
 */
[[nodiscard]] std::string
mht_click_train_runtime_settings_json(
    const MhtClickTrainSettings& settings);

/**
 * Exact readiness predicates used by project projection.
 */
[[nodiscard]] bool mht_click_train_has_channel_groups(
    const MhtClickTrainSettings& settings) noexcept;

[[nodiscard]] bool mht_click_train_requires_features(
    const MhtClickTrainSettings& settings) noexcept;

[[nodiscard]] bool mht_click_train_requires_localisations(
    const MhtClickTrainSettings& settings) noexcept;

[[nodiscard]] bool mht_click_train_requires_bearings(
    const MhtClickTrainSettings& settings) noexcept;

} // namespace pamguard::core
