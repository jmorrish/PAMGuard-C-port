#pragma once

#include "pamguard/detectors/MatchedTemplateClassifier.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace pamguard::core {

/**
 * Portable operational fields from
 * matchedTemplateClassifer.MatchedTemplateParams.
 *
 * Java's dataSourceName/dataSourceIndex are represented by the controlled
 * unit's click binding. Symbol appearance is presentation-only. The dormant
 * enableFFTFilter/fftFilterParams members are not consumed by MTProcess and
 * are therefore not exposed as pretend runtime controls.
 */
struct MatchedTemplateSettings {
    /** Unsigned view of Java's persisted byte species/type flag. */
    int click_type = 101;
    /** Global click-waveform normalisation: 0 peak, 1 RMS, 2 none. */
    int normalisation_type = 1;
    bool peak_search = true;
    int peak_smoothing = 5;
    double length_db = 6.0;
    int restricted_bins = 2048;
    /** 0 requires every channel, 1 requires any one channel. */
    int channel_classification = 0;
    std::vector<detectors::MtTemplatePair> classifiers;

    bool operator==(const MatchedTemplateSettings&) const = default;
};

class MatchedTemplateSettingsError final : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

[[nodiscard]] MatchedTemplateSettings
matched_template_settings_from_json(
    std::string_view settings_json,
    std::uint32_t settings_version);

[[nodiscard]] std::string matched_template_settings_to_json(
    const MatchedTemplateSettings& settings,
    std::uint32_t settings_version = 1);

[[nodiscard]] MatchedTemplateSettings
matched_template_default_settings();

[[nodiscard]] std::string
matched_template_default_settings_json();

/** Project-settings to the Java-faithful low-level runtime contract. */
[[nodiscard]] std::string
matched_template_runtime_settings_json(
    const MatchedTemplateSettings& settings);

[[nodiscard]] std::string_view
matched_template_settings_schema_json() noexcept;

} // namespace pamguard::core
