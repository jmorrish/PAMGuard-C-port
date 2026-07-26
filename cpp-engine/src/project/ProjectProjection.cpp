#include "pamguard/project/ProjectProjection.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <json.hpp>

#include "pamguard/core/FilterDecimatorSettings.h"
#include "pamguard/core/ClipGeneratorSettings.h"
#include "pamguard/core/IshmaelSettings.h"
#include "pamguard/core/LevelMeterSettings.h"
#include "pamguard/core/MatchedTemplateSettings.h"
#include "pamguard/core/MhtClickTrainSettings.h"
#include "pamguard/core/NoiseLtsaSettings.h"
#include "pamguard/core/SignalRoutingSettings.h"
#include "pamguard/core/SoundRecorderSettings.h"
#include "pamguard/core/WhistleMoanSettings.h"
#include "pamguard/project/ClickDetectorControlledUnit.h"
#include "pamguard/project/ClipGeneratorControlledUnit.h"
#include "pamguard/project/GlobalSettingsAdapters.h"
#include "pamguard/project/ProjectJson.h"
#include "pamguard/project/SoundRecorderControlledUnit.h"
#include "pamguard/project/SoundOutputControlledUnit.h"

namespace pamguard::project {

namespace {

using Json = nlohmann::json;

class ProjectionFailure : public std::invalid_argument {
public:
    ProjectionFailure(std::string code, std::string message)
        : std::invalid_argument(std::move(message)),
          code_(std::move(code)) {}

    [[nodiscard]] const std::string& code() const noexcept {
        return code_;
    }

private:
    std::string code_;
};

void add_issue(
    ProjectProjectionResult& result,
    ProjectionIssueClass issue_class,
    std::string code,
    std::string message,
    std::string unit_id = {},
    std::string role_id = {},
    std::string display_id = {}) {
    result.issues.push_back({
        issue_class,
        std::move(code),
        std::move(message),
        std::move(unit_id),
        std::move(role_id),
        std::move(display_id),
    });
}

bool has_editor_errors(const ProjectProjectionResult& result) {
    return std::any_of(
        result.issues.begin(),
        result.issues.end(),
        [](const auto& issue) {
            return issue.issue_class ==
                ProjectionIssueClass::EditorInvalid;
        });
}

RunMode registry_mode(ProjectMode mode) {
    switch (mode) {
    case ProjectMode::Normal:
        return RunMode::Normal;
    case ProjectMode::Mixed:
        return RunMode::Mixed;
    case ProjectMode::Viewer:
        return RunMode::Viewer;
    }
    throw ProjectionFailure(
        "unsupported-run-mode",
        "Project run mode is not supported");
}

std::string ascii_case_fold(std::string value) {
    for (auto& character : value) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return value;
}

const PublicDataRoleDescriptor* find_role(
    const ControlledUnitDescriptor& descriptor,
    const std::string& role_id,
    DataRoleDirection direction) {
    const auto found = std::find_if(
        descriptor.public_roles.begin(),
        descriptor.public_roles.end(),
        [&](const auto& role) {
            return role.id == role_id &&
                role.direction == direction;
        });
    return found == descriptor.public_roles.end()
        ? nullptr
        : &*found;
}

const PublicDataRoleDescriptor* find_role(
    const DisplayProviderDescriptor& descriptor,
    const std::string& role_id,
    DataRoleDirection direction) {
    const auto found = std::find_if(
        descriptor.public_roles.begin(),
        descriptor.public_roles.end(),
        [&](const auto& role) {
            return role.id == role_id &&
                role.direction == direction;
        });
    return found == descriptor.public_roles.end()
        ? nullptr
        : &*found;
}

const PublicRoleMappingDescriptor* find_mapping(
    const RuntimeExpansionRecipeDescriptor& recipe,
    const std::string& role_id) {
    const auto found = std::find_if(
        recipe.public_role_mappings.begin(),
        recipe.public_role_mappings.end(),
        [&](const auto& mapping) {
            return mapping.public_role_id == role_id;
        });
    return found == recipe.public_role_mappings.end()
        ? nullptr
        : &*found;
}

const InputBinding* find_binding(
    const ControlledUnitInstance& unit,
    const std::string& input_role) {
    const auto found = std::find_if(
        unit.bindings.begin(),
        unit.bindings.end(),
        [&](const auto& binding) {
            return binding.input_role == input_role;
        });
    return found == unit.bindings.end() ? nullptr : &*found;
}

bool capabilities_include(
    const std::vector<std::string>& available,
    const std::vector<std::string>& required) {
    return std::all_of(
        required.begin(),
        required.end(),
        [&](const auto& capability) {
            return std::find(
                       available.begin(),
                       available.end(),
                       capability) != available.end();
        });
}

bool requires_at_least_one(RoleCardinality cardinality) noexcept {
    return cardinality == RoleCardinality::ExactlyOne ||
        cardinality == RoleCardinality::OneOrMany;
}

bool accepts_multiple(RoleCardinality cardinality) noexcept {
    return cardinality == RoleCardinality::ZeroOrMany ||
        cardinality == RoleCardinality::OneOrMany;
}

void require_exact_fields(
    const Json& value,
    std::initializer_list<std::string_view> fields,
    const std::string& context) {
    if (!value.is_object()) {
        throw ProjectionFailure(
            "invalid-settings",
            context + " must be a JSON object");
    }
    if (value.size() != fields.size()) {
        throw ProjectionFailure(
            "invalid-settings",
            context + " contains missing or unknown fields");
    }
    for (const auto field : fields) {
        if (!value.contains(std::string(field))) {
            throw ProjectionFailure(
                "invalid-settings",
                context + " omits '" + std::string(field) + "'");
        }
    }
}

std::uint64_t unsigned_integer(
    const Json& value,
    const std::string& context) {
    if (!value.is_number_integer()) {
        throw ProjectionFailure(
            "invalid-settings",
            context + " must be an integer");
    }
    if (value.is_number_unsigned()) {
        return value.get<std::uint64_t>();
    }
    const auto signed_value = value.get<std::int64_t>();
    if (signed_value < 0) {
        throw ProjectionFailure(
            "invalid-settings",
            context + " must be non-negative");
    }
    return static_cast<std::uint64_t>(signed_value);
}

double finite_number(
    const Json& value,
    const std::string& context) {
    if (!value.is_number()) {
        throw ProjectionFailure(
            "invalid-settings",
            context + " must be a number");
    }
    const auto number = value.get<double>();
    if (!std::isfinite(number)) {
        throw ProjectionFailure(
            "invalid-settings",
            context + " must be finite");
    }
    return number;
}

void require_boolean(
    const Json& value,
    const std::string& context) {
    if (!value.is_boolean()) {
        throw ProjectionFailure(
            "invalid-settings",
            context + " must be a boolean");
    }
}

void validate_acquisition_settings(
    const Json& settings,
    const core::ArrayConfiguration* array_geometry = nullptr) {
    require_exact_fields(
        settings,
        {
            "daqSystemType",
            "sampleRate",
            "nChannels",
            "hardwareChannelList",
            "hydrophoneList",
            "voltsPeak2Peak",
            "preamplifier",
            "subtractDC",
            "dcTimeConstantSeconds",
            "calibrationDbOffsetByChannel",
        },
        "Acquisition settings");
    if (!settings.at("daqSystemType").is_string() ||
        settings.at("daqSystemType").get_ref<const std::string&>().empty()) {
        throw ProjectionFailure(
            "invalid-settings",
            "Acquisition source type must be a non-empty string");
    }
    if (finite_number(
            settings.at("sampleRate"),
            "Acquisition sampleRate") <= 0.0) {
        throw ProjectionFailure(
            "invalid-settings",
            "Acquisition sampleRate must be positive");
    }
    const auto channels = unsigned_integer(
        settings.at("nChannels"),
        "Acquisition nChannels");
    if (channels == 0 || channels > 32) {
        throw ProjectionFailure(
            "invalid-settings",
            "Acquisition nChannels must be in 1..32");
    }

    const auto validate_channel_list =
        [channels](
            const Json& values,
            const std::string& context) {
            if (!values.is_array() ||
                values.size() != channels) {
                throw ProjectionFailure(
                    "invalid-settings",
                    context +
                        " must contain exactly one entry per active channel");
            }
            for (const auto& value : values) {
                if (unsigned_integer(value, context) >= 32) {
                    throw ProjectionFailure(
                        "invalid-settings",
                        context + " entries must be in 0..31");
                }
            }
        };
    validate_channel_list(
        settings.at("hardwareChannelList"),
        "Acquisition hardwareChannelList");
    validate_channel_list(
        settings.at("hydrophoneList"),
        "Acquisition hydrophoneList");
    if (array_geometry) {
        for (const auto& value :
             settings.at("hydrophoneList")) {
            const auto hydrophone = unsigned_integer(
                value,
                "Acquisition hydrophoneList");
            if (hydrophone >=
                array_geometry->hydrophones.size()) {
                throw ProjectionFailure(
                    "invalid-settings",
                    "Acquisition hydrophoneList refers to a hydrophone "
                    "which is absent from the global Array Manager");
            }
        }
    }

    if (finite_number(
            settings.at("voltsPeak2Peak"),
            "Acquisition voltsPeak2Peak") <= 0.0) {
        throw ProjectionFailure(
            "invalid-settings",
            "Acquisition voltsPeak2Peak must be positive");
    }
    const auto& preamplifier = settings.at("preamplifier");
    require_exact_fields(
        preamplifier,
        {"gainDb", "bandwidthHz"},
        "Acquisition preamplifier");
    (void) finite_number(
        preamplifier.at("gainDb"),
        "Acquisition preamplifier gainDb");
    const auto& bandwidth =
        preamplifier.at("bandwidthHz");
    if (!bandwidth.is_array() || bandwidth.size() != 2) {
        throw ProjectionFailure(
            "invalid-settings",
            "Acquisition preamplifier bandwidthHz must contain two numbers");
    }
    const auto bandwidth_low = finite_number(
        bandwidth.at(0),
        "Acquisition preamplifier bandwidthHz");
    const auto bandwidth_high = finite_number(
        bandwidth.at(1),
        "Acquisition preamplifier bandwidthHz");
    if (bandwidth_low < 0.0 ||
        bandwidth_high < bandwidth_low) {
        throw ProjectionFailure(
            "invalid-settings",
            "Acquisition preamplifier bandwidthHz must be ordered and "
            "non-negative");
    }
    require_boolean(
        settings.at("subtractDC"),
        "Acquisition subtractDC");
    if (finite_number(
            settings.at("dcTimeConstantSeconds"),
            "Acquisition dcTimeConstantSeconds") <= 0.0) {
        throw ProjectionFailure(
            "invalid-settings",
            "Acquisition dcTimeConstantSeconds must be positive");
    }

    const auto& calibration =
        settings.at("calibrationDbOffsetByChannel");
    if (!calibration.is_array() ||
        (!calibration.empty() &&
         calibration.size() != channels)) {
        throw ProjectionFailure(
            "invalid-settings",
            "Acquisition calibration must be empty or cover every channel");
    }
    for (const auto& value : calibration) {
        (void) finite_number(
            value,
            "Acquisition channel calibration");
    }
}

std::string runtime_window_name(std::uint64_t java_window) {
    static const std::vector<std::string> names{
        "Rectangular",
        "Hamming",
        "Hann",
        "Bartlett",
        "Blackman",
        "Blackman-Harris",
    };
    if (java_window >= names.size()) {
        throw ProjectionFailure(
            "invalid-settings",
            "FFT windowFunction must be Java WindowFunction 0..5");
    }
    return names[static_cast<std::size_t>(java_window)];
}

void validate_fft_bundle_settings(const Json& settings) {
    require_exact_fields(
        settings,
        {"fft", "spectralNoise"},
        "FFT controlled-unit settings");
}

void validate_fft_process_settings(const Json& settings) {
    require_exact_fields(
        settings,
        {
            "fftLength",
            "fftHop",
            "channelMap",
            "windowFunction",
            "clickRemoval",
            "clickThreshold",
            "clickPower",
        },
        "FFT process settings");
    const auto fft_length = unsigned_integer(
        settings.at("fftLength"),
        "FFT fftLength");
    if (fft_length < 2 ||
        (fft_length & (fft_length - 1)) != 0) {
        throw ProjectionFailure(
            "invalid-settings",
            "FFT fftLength must be a power of two of at least 2");
    }
    const auto fft_hop = unsigned_integer(
        settings.at("fftHop"),
        "FFT fftHop");
    if (fft_hop == 0 || fft_hop > fft_length) {
        throw ProjectionFailure(
            "invalid-settings",
            "FFT fftHop must be in 1..fftLength");
    }
    const auto channel_map = unsigned_integer(
        settings.at("channelMap"),
        "FFT channelMap");
    if (channel_map == 0 ||
        channel_map >
            std::numeric_limits<std::uint32_t>::max()) {
        throw ProjectionFailure(
            "invalid-settings",
            "FFT channelMap must select at least one of 32 channels");
    }
    (void) runtime_window_name(unsigned_integer(
        settings.at("windowFunction"),
        "FFT windowFunction"));
    require_boolean(
        settings.at("clickRemoval"),
        "FFT clickRemoval");
    (void) finite_number(
        settings.at("clickThreshold"),
        "FFT clickThreshold");
    const auto click_power = unsigned_integer(
        settings.at("clickPower"),
        "FFT clickPower");
    if (click_power < 2 || (click_power & 1U) != 0) {
        throw ProjectionFailure(
            "invalid-settings",
            "FFT clickPower must be an even integer of at least 2");
    }
}

void validate_spectral_noise_settings(const Json& settings) {
    require_exact_fields(
        settings,
        {
            "medianFilter",
            "medianFilterLength",
            "averageSubtraction",
            "updateConstant",
            "kernelSmoothing",
            "threshold",
            "thresholdDb",
            "finalOutput",
        },
        "Spectral-noise settings");
    require_boolean(
        settings.at("medianFilter"),
        "Spectral-noise medianFilter");
    if (unsigned_integer(
            settings.at("medianFilterLength"),
            "Spectral-noise medianFilterLength") == 0) {
        throw ProjectionFailure(
            "invalid-settings",
            "Spectral-noise medianFilterLength must be positive");
    }
    require_boolean(
        settings.at("averageSubtraction"),
        "Spectral-noise averageSubtraction");
    const auto update = finite_number(
        settings.at("updateConstant"),
        "Spectral-noise updateConstant");
    if (update <= 0.0 || update >= 1.0) {
        throw ProjectionFailure(
            "invalid-settings",
            "Spectral-noise updateConstant must be between 0 and 1");
    }
    require_boolean(
        settings.at("kernelSmoothing"),
        "Spectral-noise kernelSmoothing");
    require_boolean(
        settings.at("threshold"),
        "Spectral-noise threshold");
    (void) finite_number(
        settings.at("thresholdDb"),
        "Spectral-noise thresholdDb");
    if (unsigned_integer(
            settings.at("finalOutput"),
            "Spectral-noise finalOutput") > 2) {
        throw ProjectionFailure(
            "invalid-settings",
            "Spectral-noise finalOutput must be in 0..2");
    }
}

Json selected_settings(
    const Json& root,
    const std::string& pointer,
    const std::string& context) {
    try {
        if (pointer.empty()) {
            return root;
        }
        const Json::json_pointer json_pointer(pointer);
        if (!root.contains(json_pointer)) {
            throw ProjectionFailure(
                "invalid-settings",
                context + " settings pointer '" + pointer +
                    "' does not exist");
        }
        return root.at(json_pointer);
    }
    catch (const ProjectionFailure&) {
        throw;
    }
    catch (const std::exception& error) {
        throw ProjectionFailure(
            "invalid-settings",
            context + " has an invalid settings pointer: " +
                error.what());
    }
}

void validate_sound_output_settings(const Json& settings) {
    try {
        (void) sound_output_settings_from_json(
            settings.dump(),
            1);
    }
    catch (const SoundOutputSettingsError& error) {
        throw ProjectionFailure(
            "invalid-settings",
            error.what());
    }
}

Json project_child_settings(
    const RuntimeChildDescriptor& child,
    const Json& controlled_unit_settings,
    const core::ArrayConfiguration* array_geometry,
    std::optional<core::IshmaelFftGeometry>
        source_fft_geometry) {
    if (child.settings.adapter_id.rfind(
            "pamguard.click-",
            0) == 0) {
        if (!array_geometry) {
            throw ProjectionFailure(
                "missing-array-manager",
                "Click Detector runtime expansion requires the global "
                "Array Manager");
        }
        try {
            return Json::parse(
                click_detector_runtime_settings_json(
                    controlled_unit_settings.dump(),
                    1,
                    child.role_id,
                    *array_geometry));
        }
        catch (const ClickDetectorSettingsError& error) {
            throw ProjectionFailure(
                "invalid-settings",
                error.what());
        }
    }
    auto selected = selected_settings(
        controlled_unit_settings,
        child.settings.source_pointer,
        "Runtime child '" + child.role_id + "'");
    if (!selected.is_object()) {
        throw ProjectionFailure(
            "invalid-settings",
            "Runtime child '" + child.role_id +
                "' settings must select an object");
    }

    if (child.settings.adapter_id ==
        "pamguard.ishmael-energy-sum-settings.v1") {
        try {
            const auto decoded =
                core::ishmael_energy_sum_settings_from_json(
                    selected.dump(),
                    1);
            return Json::parse(
                core::ishmael_energy_sum_runtime_settings_json(
                    decoded,
                    source_fft_geometry));
        }
        catch (const core::IshmaelSettingsError& error) {
            throw ProjectionFailure(
                "invalid-settings",
                error.what());
        }
    }
    if (child.settings.adapter_id ==
        "pamguard.ishmael-sgram-corr-settings.v1") {
        try {
            const auto decoded =
                core::ishmael_sgram_corr_settings_from_json(
                    selected.dump(),
                    1);
            return Json::parse(
                core::ishmael_sgram_corr_runtime_settings_json(
                    decoded,
                    source_fft_geometry));
        }
        catch (const core::IshmaelSettingsError& error) {
            throw ProjectionFailure(
                "invalid-settings",
                error.what());
        }
    }
    if (child.settings.adapter_id ==
        "pamguard.ishmael-match-filter-settings.v1") {
        try {
            const auto decoded =
                core::ishmael_match_filter_settings_from_json(
                    selected.dump(),
                    1);
            return Json::parse(
                core::ishmael_match_filter_runtime_settings_json(
                    decoded));
        }
        catch (const core::IshmaelSettingsError& error) {
            throw ProjectionFailure(
                "invalid-settings",
                error.what());
        }
    }

    if (child.settings.adapter_id ==
        "pamguard.standalone-filter-settings.v1") {
        try {
            const auto decoded =
                core::standalone_filter_settings_from_json(
                    selected.dump(),
                    1);
            return Json::parse(
                core::standalone_filter_settings_to_json(
                    decoded,
                    1));
        }
        catch (const core::FilterDecimatorSettingsError& error) {
            throw ProjectionFailure(
                "invalid-settings",
                error.what());
        }
    }
    if (child.settings.adapter_id ==
        "pamguard.decimator-settings.v1") {
        try {
            const auto decoded =
                core::decimator_settings_from_json(
                    selected.dump(),
                    1);
            return Json::parse(
                core::decimator_settings_to_json(
                    decoded,
                    1));
        }
        catch (const core::FilterDecimatorSettingsError& error) {
            throw ProjectionFailure(
                "invalid-settings",
                error.what());
        }
    }
    if (child.settings.adapter_id ==
        "pamguard.fft-noise-settings.v1") {
        try {
            const auto decoded =
                core::fft_noise_monitor_settings_from_json(
                    selected.dump(),
                    1);
            return Json::parse(
                core::fft_noise_monitor_runtime_settings_json(
                    decoded));
        }
        catch (const core::NoiseLtsaSettingsError& error) {
            throw ProjectionFailure(
                "invalid-settings",
                error.what());
        }
    }
    if (child.settings.adapter_id ==
        "pamguard.noise-band-settings.v1") {
        try {
            const auto decoded =
                core::noise_band_monitor_settings_from_json(
                    selected.dump(),
                    1);
            return Json::parse(
                core::noise_band_monitor_runtime_settings_json(
                    decoded));
        }
        catch (const core::NoiseLtsaSettingsError& error) {
            throw ProjectionFailure(
                "invalid-settings",
                error.what());
        }
    }
    if (child.settings.adapter_id ==
        "pamguard.ltsa-settings.v1") {
        try {
            const auto decoded =
                core::ltsa_settings_from_json(
                    selected.dump(),
                    1);
            return Json::parse(
                core::ltsa_runtime_settings_json(decoded));
        }
        catch (const core::NoiseLtsaSettingsError& error) {
            throw ProjectionFailure(
                "invalid-settings",
                error.what());
        }
    }
    if (child.settings.adapter_id ==
            "pamguard.whistle-noise-settings.v1" ||
        child.settings.adapter_id ==
            "pamguard.whistle-contour-settings.v1") {
        try {
            const auto decoded =
                core::whistle_moan_settings_from_json(
                    selected.dump(),
                    1);
            return Json::parse(
                child.settings.adapter_id ==
                        "pamguard.whistle-noise-settings.v1"
                    ? core::
                          whistle_moan_noise_runtime_settings_json(
                              decoded)
                    : core::
                          whistle_moan_contour_runtime_settings_json(
                              decoded));
        }
        catch (const core::WhistleMoanSettingsError& error) {
            throw ProjectionFailure(
                "invalid-settings",
                error.what());
        }
    }
    if (child.settings.adapter_id ==
        "pamguard.mht-click-train-settings.v1") {
        try {
            const auto decoded =
                core::mht_click_train_settings_from_json(
                    selected.dump(),
                    1);
            return Json::parse(
                core::mht_click_train_runtime_settings_json(
                    decoded));
        }
        catch (const core::MhtClickTrainSettingsError& error) {
            throw ProjectionFailure(
                "invalid-settings",
                error.what());
        }
    }
    if (child.settings.adapter_id ==
        "pamguard.matched-template-settings.v1") {
        try {
            const auto decoded =
                core::matched_template_settings_from_json(
                    selected.dump(),
                    1);
            return Json::parse(
                core::matched_template_runtime_settings_json(
                    decoded));
        }
        catch (const core::MatchedTemplateSettingsError& error) {
            throw ProjectionFailure(
                "invalid-settings",
                error.what());
        }
    }
    if (child.settings.adapter_id ==
        "pamguard.level-meter-settings.v1") {
        try {
            (void) core::level_meter_settings_from_json(
                selected.dump(),
                1);
            // Java measures each RawDataUnit. The graph ingress uses
            // transport-sized AudioChunks, so publish a bounded 250 ms
            // operator update while retaining both exact peak and RMS values.
            return Json{
                {"intervalSeconds", 0.25},
                {
                    "channelBitmap",
                    std::numeric_limits<std::uint32_t>::max(),
                },
            };
        }
        catch (const core::LevelMeterSettingsError& error) {
            throw ProjectionFailure(
                "invalid-settings",
                error.what());
        }
    }
    if (child.settings.adapter_id ==
        kSoundRecorderRuntimeSettingsAdapterId) {
        try {
            const auto decoded =
                core::sound_recorder_settings_from_json(
                    selected.dump(),
                    1);
            return Json{
                {"startTransport", "off"},
                {
                    "settings",
                    Json::parse(
                        core::sound_recorder_settings_to_json(
                            decoded,
                            1)),
                },
            };
        }
        catch (const core::SoundRecorderSettingsError& error) {
            throw ProjectionFailure(
                "invalid-settings",
                error.what());
        }
    }
    if (child.settings.adapter_id == "identity.v1") {
        if (child.runtime_type_id ==
            "pamguard.spectrogram-noise") {
            validate_spectral_noise_settings(selected);
        }
        else if (child.runtime_type_id ==
                 "pamguard.sound-output") {
            validate_sound_output_settings(selected);
        }
        return selected;
    }
    if (child.settings.adapter_id ==
        "pamguard.acquisition-settings.v1") {
        validate_acquisition_settings(
            selected,
            array_geometry);
        Json calibration =
            selected.at("calibrationDbOffsetByChannel");
        if (calibration.empty()) {
            if (!array_geometry) {
                throw ProjectionFailure(
                    "missing-array-manager",
                    "Acquisition calibration requires the global "
                    "Array Manager");
            }
            const double voltage_term_db =
                20.0 * std::log10(
                    selected.at("voltsPeak2Peak")
                            .get<double>() /
                    2.0);
            const double acquisition_preamp_db =
                selected.at("preamplifier")
                    .at("gainDb")
                    .get<double>();
            calibration = Json::array();
            const auto& hydrophone_list =
                selected.at("hydrophoneList");
            for (std::size_t channel = 0;
                 channel < hydrophone_list.size();
                 ++channel) {
                const auto hydrophone_index =
                    unsigned_integer(
                        hydrophone_list.at(channel),
                        "Acquisition hydrophoneList");
                const auto& hydrophone =
                    array_geometry->hydrophones.at(
                        hydrophone_index);
                // AcquisitionProcess.rawAmplitude2dB:
                // 20*log10(raw * vp2p / 2) -
                // (hydrophone sensitivity + hydrophone preamp gain +
                // acquisition preamp gain). DataBlock consumers add this
                // offset to 20*log10(raw).
                calibration.push_back(
                    voltage_term_db -
                    (hydrophone.sensitivity_db +
                     hydrophone.preamp_gain_db +
                     acquisition_preamp_db));
            }
        }
        return Json{
            {"daqSystemType", selected.at("daqSystemType")},
            {"sampleRateHz", selected.at("sampleRate")},
            {"channelCount", selected.at("nChannels")},
            {"hardwareChannelList",
             selected.at("hardwareChannelList")},
            {"hydrophoneList",
             selected.at("hydrophoneList")},
            {"voltsPeak2Peak",
             selected.at("voltsPeak2Peak")},
            {"preamplifier",
             selected.at("preamplifier")},
            {"subtractDC",
             selected.at("subtractDC")},
            {"dcTimeConstantSeconds",
             selected.at("dcTimeConstantSeconds")},
            {"calibrationDbOffsetByChannel",
             std::move(calibration)},
        };
    }
    if (child.settings.adapter_id ==
        "pamguard.fft-settings.v1") {
        validate_fft_process_settings(selected);
        const auto bitmap = unsigned_integer(
            selected.at("channelMap"),
            "FFT channelMap");
        Json channels = Json::array();
        for (std::size_t channel = 0; channel < 32; ++channel) {
            if ((bitmap & (std::uint64_t{1} << channel)) != 0) {
                channels.push_back(channel);
            }
        }
        return Json{
            {"fftLength", selected.at("fftLength")},
            {"fftHop", selected.at("fftHop")},
            {"windowType",
             runtime_window_name(unsigned_integer(
                 selected.at("windowFunction"),
                 "FFT windowFunction"))},
            {"channels", std::move(channels)},
            {"clickRemoval", selected.at("clickRemoval")},
            {"clickThreshold", selected.at("clickThreshold")},
            {"clickPower", selected.at("clickPower")},
        };
    }
    throw ProjectionFailure(
        "unsupported-settings-adapter",
        "Runtime child '" + child.role_id +
            "' uses unsupported settings adapter '" +
            child.settings.adapter_id + "'");
}

void validate_controlled_settings_root(
    const ControlledUnitDescriptor& descriptor,
    const Json& settings,
    const core::ArrayConfiguration* array_geometry) {
    if (!settings.is_object()) {
        throw ProjectionFailure(
            "invalid-settings",
            "Controlled-unit settings must be an object");
    }
    if (descriptor.id == "pamguard.acquisition") {
        validate_acquisition_settings(
            settings,
            array_geometry);
    }
    else if (descriptor.id == "pamguard.fft") {
        validate_fft_bundle_settings(settings);
    }
    else if (descriptor.id == "pamguard.click-detector") {
        try {
            validate_click_detector_settings_json(
                settings.dump(),
                descriptor.settings.version);
        }
        catch (const ClickDetectorSettingsError& error) {
            throw ProjectionFailure(
                "invalid-settings",
                error.what());
        }
    }
    else if (descriptor.id ==
                 "pamguard.ishmael-energy-sum" ||
             descriptor.id ==
                 "pamguard.ishmael-sgram-corr" ||
             descriptor.id ==
                 "pamguard.ishmael-match-filter") {
        try {
            if (descriptor.id ==
                "pamguard.ishmael-energy-sum") {
                (void) core::
                    ishmael_energy_sum_settings_from_json(
                        settings.dump(),
                        descriptor.settings.version);
            }
            else if (descriptor.id ==
                     "pamguard.ishmael-sgram-corr") {
                (void) core::
                    ishmael_sgram_corr_settings_from_json(
                        settings.dump(),
                        descriptor.settings.version);
            }
            else {
                (void) core::
                    ishmael_match_filter_settings_from_json(
                        settings.dump(),
                        descriptor.settings.version);
            }
        }
        catch (const core::IshmaelSettingsError& error) {
            throw ProjectionFailure(
                "invalid-settings",
                error.what());
        }
    }
    else if (descriptor.id == "pamguard.sound-output") {
        validate_sound_output_settings(settings);
    }
    else if (descriptor.id == "pamguard.filter" ||
             descriptor.id == "pamguard.decimator") {
        try {
            if (descriptor.id == "pamguard.filter") {
                (void) core::standalone_filter_settings_from_json(
                    settings.dump(),
                    descriptor.settings.version);
            }
            else {
                (void) core::decimator_settings_from_json(
                    settings.dump(),
                    descriptor.settings.version);
            }
        }
        catch (const core::FilterDecimatorSettingsError& error) {
            throw ProjectionFailure(
                "invalid-settings",
                error.what());
        }
    }
    else if (descriptor.id == "pamguard.amplifier" ||
             descriptor.id == "pamguard.patch-panel") {
        try {
            if (descriptor.id == "pamguard.amplifier") {
                (void) core::signal_amplifier_settings_from_json(
                    settings.dump(),
                    descriptor.settings.version);
            }
            else {
                (void) core::patch_panel_settings_from_json(
                    settings.dump(),
                    descriptor.settings.version);
            }
        }
        catch (const core::SignalRoutingSettingsError& error) {
            throw ProjectionFailure(
                "invalid-settings",
                error.what());
        }
    }
    else if (descriptor.id == "pamguard.fft-noise-monitor" ||
             descriptor.id == "pamguard.noise-band-monitor" ||
             descriptor.id == "pamguard.ltsa") {
        try {
            if (descriptor.id == "pamguard.fft-noise-monitor") {
                (void) core::fft_noise_monitor_settings_from_json(
                    settings.dump(),
                    descriptor.settings.version);
            }
            else if (descriptor.id ==
                     "pamguard.noise-band-monitor") {
                (void) core::noise_band_monitor_settings_from_json(
                    settings.dump(),
                    descriptor.settings.version);
            }
            else {
                (void) core::ltsa_settings_from_json(
                    settings.dump(),
                    descriptor.settings.version);
            }
        }
        catch (const core::NoiseLtsaSettingsError& error) {
            throw ProjectionFailure(
                "invalid-settings",
                error.what());
        }
    }
    else if (descriptor.id ==
             "pamguard.whistles-moans") {
        try {
            (void) core::whistle_moan_settings_from_json(
                settings.dump(),
                descriptor.settings.version);
        }
        catch (const core::WhistleMoanSettingsError& error) {
            throw ProjectionFailure(
                "invalid-settings",
                error.what());
        }
    }
    else if (descriptor.id ==
             "pamguard.mht-click-train") {
        try {
            (void) core::mht_click_train_settings_from_json(
                settings.dump(),
                descriptor.settings.version);
        }
        catch (const core::MhtClickTrainSettingsError& error) {
            throw ProjectionFailure(
                "invalid-settings",
                error.what());
        }
    }
    else if (descriptor.id ==
             "pamguard.matched-template-classifier") {
        try {
            (void) core::matched_template_settings_from_json(
                settings.dump(),
                descriptor.settings.version);
        }
        catch (const core::MatchedTemplateSettingsError& error) {
            throw ProjectionFailure(
                "invalid-settings",
                error.what());
        }
    }
    else if (descriptor.id == "pamguard.level-meter") {
        try {
            (void) core::level_meter_settings_from_json(
                settings.dump(),
                descriptor.settings.version);
        }
        catch (const core::LevelMeterSettingsError& error) {
            throw ProjectionFailure(
                "invalid-settings",
                error.what());
        }
    }
    else if (descriptor.id == kClipGeneratorControlledUnitTypeId) {
        try {
            (void) core::clip_generator_settings_from_json(
                settings.dump(),
                descriptor.settings.version);
        }
        catch (const core::ClipGeneratorSettingsError& error) {
            throw ProjectionFailure(
                "invalid-settings",
                error.what());
        }
    }
    else if (descriptor.id == "pamguard.sound-recorder") {
        try {
            (void) core::sound_recorder_settings_from_json(
                settings.dump(),
                descriptor.settings.version);
        }
        catch (const core::SoundRecorderSettingsError& error) {
            throw ProjectionFailure(
                "invalid-settings",
                error.what());
        }
    }
    else if (descriptor.id == "pamguard.user-display" &&
             !settings.empty()) {
        throw ProjectionFailure(
            "invalid-settings",
            "User Display settings must currently be an empty object");
    }
}

void validate_spectrogram_settings(const Json& settings) {
    require_exact_fields(
        settings,
        {
            "nPanels",
            "channelList",
            "frequencyLimits",
            "amplitudeLimits",
            "colourMap",
            "wrapDisplay",
            "timeScaleFixed",
            "displayLength",
            "pixelsPerSlics",
            "showScale",
        },
        "Spectrogram settings");
    const auto panel_count = unsigned_integer(
        settings.at("nPanels"),
        "Spectrogram nPanels");
    if (panel_count == 0 || panel_count > 32) {
        throw ProjectionFailure(
            "invalid-display-settings",
            "Spectrogram nPanels must be between 1 and 32");
    }
    const auto& channel_list = settings.at("channelList");
    if (!channel_list.is_array() ||
        channel_list.size() != panel_count) {
        throw ProjectionFailure(
            "invalid-display-settings",
            "Spectrogram channelList length must equal nPanels");
    }
    for (const auto& channel : channel_list) {
        if (unsigned_integer(
                channel,
                "Spectrogram channelList entry") > 31) {
            throw ProjectionFailure(
                "invalid-display-settings",
                "Spectrogram channelList entries must be between 0 and 31");
        }
    }
    const auto validate_pair =
        [](const Json& value,
           const std::string& name,
           bool nonnegative) {
            if (!value.is_array() || value.size() != 2) {
                throw ProjectionFailure(
                    "invalid-display-settings",
                    name + " must contain exactly two numbers");
            }
            const auto low = finite_number(value.at(0), name);
            const auto high = finite_number(value.at(1), name);
            if (low > high || (nonnegative && low < 0.0)) {
                throw ProjectionFailure(
                    "invalid-display-settings",
                    name + " must be ordered" +
                        (nonnegative ? " and non-negative" : ""));
            }
        };
    validate_pair(
        settings.at("frequencyLimits"),
        "Spectrogram frequencyLimits",
        true);
    validate_pair(
        settings.at("amplitudeLimits"),
        "Spectrogram amplitudeLimits",
        false);
    static const std::set<std::string> colour_maps{
        "GREY",
        "REVERSEGREY",
        "BLUE",
        "GREEN",
        "RED",
        "HOT",
        "HSV",
        "FIRE",
        "PATRIOTIC",
    };
    if (!settings.at("colourMap").is_string() ||
        !colour_maps.contains(
            settings.at("colourMap").get<std::string>())) {
        throw ProjectionFailure(
            "invalid-display-settings",
            "Spectrogram colourMap is not a Java ColourArrayType value");
    }
    require_boolean(
        settings.at("wrapDisplay"),
        "Spectrogram wrapDisplay");
    require_boolean(
        settings.at("timeScaleFixed"),
        "Spectrogram timeScaleFixed");
    if (finite_number(
            settings.at("displayLength"),
            "Spectrogram displayLength") <= 0.0) {
        throw ProjectionFailure(
            "invalid-display-settings",
            "Spectrogram displayLength must be positive");
    }
    if (unsigned_integer(
            settings.at("pixelsPerSlics"),
            "Spectrogram pixelsPerSlics") == 0) {
        throw ProjectionFailure(
            "invalid-display-settings",
            "Spectrogram pixelsPerSlics must be positive");
    }
    require_boolean(
        settings.at("showScale"),
        "Spectrogram showScale");
}

Json parse_settings(
    const std::string& value,
    const std::string& context) {
    try {
        auto parsed = Json::parse(value);
        if (!parsed.is_object()) {
            throw ProjectionFailure(
                "invalid-settings",
                context + " must be a JSON object");
        }
        return parsed;
    }
    catch (const ProjectionFailure&) {
        throw;
    }
    catch (const std::exception& error) {
        throw ProjectionFailure(
            "invalid-settings",
            context + " is invalid JSON: " + error.what());
    }
}

std::vector<LowLevelTypeContract> low_level_contracts(
    const core::ModuleRegistry& runtime_registry) {
    std::vector<LowLevelTypeContract> result;
    for (const auto& type : runtime_registry.list()) {
        LowLevelTypeContract converted;
        converted.id = type.id;
        for (const auto& port : type.ports) {
            converted.ports.push_back({
                port.id,
                port.direction == core::PortDirection::Input
                    ? DataRoleDirection::Input
                    : DataRoleDirection::Output,
                port.data_type,
                port.capabilities,
            });
        }
        result.push_back(std::move(converted));
    }
    return result;
}

const ProjectedDataBlock* block_for_endpoint(
    const ProjectionIndex& index,
    const std::string& runtime_node_id,
    const std::string& port_id) {
    const auto found = std::find_if(
        index.data_blocks.begin(),
        index.data_blocks.end(),
        [&](const auto& block) {
            return block.runtime_node_id == runtime_node_id &&
                block.port_id == port_id;
        });
    return found == index.data_blocks.end() ? nullptr : &*found;
}

ProjectedPublicInput* find_projected_input(
    ProjectionIndex& index,
    const std::string& unit_id,
    const std::string& input_role) {
    const auto found = std::find_if(
        index.public_inputs.begin(),
        index.public_inputs.end(),
        [&](const auto& input) {
            return input.unit_id == unit_id &&
                input.input_role == input_role;
        });
    return found == index.public_inputs.end() ? nullptr : &*found;
}

bool compatible_public_roles(
    const PublicDataRoleDescriptor& source,
    const PublicDataRoleDescriptor& target) {
    return source.direction == DataRoleDirection::Output &&
        target.direction == DataRoleDirection::Input &&
        (source.data_type == target.data_type ||
         target.data_type == kClipGeneratorAcousticDataUnitType) &&
        capabilities_include(
            source.capabilities,
            target.capabilities);
}

void add_internal_connections(
    const ControlledUnitInstance& unit,
    const ControlledUnitDescriptor& descriptor,
    ProjectProjectionResult& result) {
    for (const auto& edge :
         descriptor.runtime_recipe.internal_edges) {
        const auto connection_id =
            projected_internal_connection_id(unit.id, edge.id);
        result.graph.connections.push_back({
            connection_id,
            {
                projected_runtime_node_id(
                    unit.id,
                    edge.source.child_role_id),
                edge.source.port_id,
            },
            {
                projected_runtime_node_id(
                    unit.id,
                    edge.target.child_role_id),
                edge.target.port_id,
            },
        });
        result.index.connections.push_back({
            connection_id,
            ProjectedConnectionKind::Internal,
            unit.id,
            edge.id,
            {},
            std::nullopt,
        });
    }
}

void add_public_role_index(
    const ControlledUnitInstance& unit,
    const ControlledUnitDescriptor& descriptor,
    ProjectProjectionResult& result) {
    for (const auto& role : descriptor.public_roles) {
        const auto* mapping =
            find_mapping(descriptor.runtime_recipe, role.id);
        if (!mapping) {
            throw ProjectionFailure(
                "invalid-registry",
                "Public role '" + role.id +
                    "' has no runtime mapping");
        }
        const auto runtime_node_id = projected_runtime_node_id(
            unit.id,
            mapping->runtime_endpoint.child_role_id);
        if (role.direction == DataRoleDirection::Output) {
            const auto* block = block_for_endpoint(
                result.index,
                runtime_node_id,
                mapping->runtime_endpoint.port_id);
            if (!block) {
                throw ProjectionFailure(
                    "invalid-registry",
                    "Public output '" + role.id +
                        "' does not map to a low-level output block");
            }
            result.index.public_outputs.push_back({
                unit.id,
                role.id,
                runtime_node_id,
                mapping->runtime_endpoint.port_id,
                block->block_id,
                role.data_type,
                role.capabilities,
            });
        }
        else {
            const auto* binding = find_binding(unit, role.id);
            result.index.public_inputs.push_back({
                unit.id,
                role.id,
                runtime_node_id,
                mapping->runtime_endpoint.port_id,
                role.data_type,
                role.cardinality,
                binding ? binding->sources
                        : std::vector<SourceReference>{},
                {},
            });
        }
    }
}

const ControlledUnitInstance* find_controlled_unit(
    const ProjectDocument& document,
    const std::string& unit_id) {
    const auto found = std::find_if(
        document.controlled_units.begin(),
        document.controlled_units.end(),
        [&](const auto& unit) {
            return unit.id == unit_id;
        });
    return found == document.controlled_units.end()
        ? nullptr
        : &*found;
}

std::optional<core::IshmaelFftGeometry>
resolve_ishmael_fft_geometry(
    const ProjectDocument& document,
    const std::unordered_map<
        std::string,
        const ControlledUnitDescriptor*>& descriptors,
    const std::unordered_map<std::string, Json>& settings,
    const SourceReference& source,
    std::unordered_set<std::string> visited) {
    const auto* source_unit =
        find_controlled_unit(document, source.unit_id);
    if (!source_unit ||
        !visited.emplace(source_unit->id).second) {
        return std::nullopt;
    }
    const auto descriptor_it =
        descriptors.find(source_unit->id);
    if (descriptor_it == descriptors.end()) {
        return std::nullopt;
    }
    const auto* descriptor = descriptor_it->second;
    const auto* output_role = find_role(
        *descriptor,
        source.output_role,
        DataRoleDirection::Output);
    if (!output_role ||
        output_role->data_type != "pamguard.fft") {
        return std::nullopt;
    }

    if (descriptor->id == "pamguard.fft") {
        const auto settings_it = settings.find(source_unit->id);
        if (settings_it == settings.end() ||
            !settings_it->second.contains("fft")) {
            return std::nullopt;
        }
        const auto& fft = settings_it->second.at("fft");
        return core::IshmaelFftGeometry{
            fft.at("fftLength").get<std::size_t>(),
            fft.at("fftHop").get<std::size_t>(),
        };
    }

    for (const auto& input_role : descriptor->public_roles) {
        if (input_role.direction !=
                DataRoleDirection::Input ||
            input_role.data_type != "pamguard.fft") {
            continue;
        }
        const auto* binding =
            find_binding(*source_unit, input_role.id);
        if (!binding) {
            continue;
        }
        for (const auto& upstream : binding->sources) {
            const auto geometry = resolve_ishmael_fft_geometry(
                document,
                descriptors,
                settings,
                upstream,
                visited);
            if (geometry) {
                return geometry;
            }
        }
    }
    return std::nullopt;
}

std::optional<core::IshmaelFftGeometry>
bound_ishmael_fft_geometry(
    const ProjectDocument& document,
    const std::unordered_map<
        std::string,
        const ControlledUnitDescriptor*>& descriptors,
    const std::unordered_map<std::string, Json>& settings,
    const ControlledUnitInstance& unit) {
    const auto* binding = find_binding(unit, "fft");
    if (!binding) {
        return std::nullopt;
    }
    for (const auto& source : binding->sources) {
        const auto geometry = resolve_ishmael_fft_geometry(
            document,
            descriptors,
            settings,
            source,
            {});
        if (geometry) {
            return geometry;
        }
    }
    return std::nullopt;
}

Json clip_generator_runtime_settings(
    const ProjectDocument& document,
    const std::unordered_map<
        std::string,
        const ControlledUnitDescriptor*>& descriptors,
    const ControlledUnitInstance& unit,
    const Json& canonical_settings) {
    std::vector<SourceReference> bound_sources;
    if (const auto* binding = find_binding(unit, "triggers")) {
        bound_sources = binding->sources;
    }

    std::vector<ClipGeneratorTriggerSourceCandidate> candidates;
    for (const auto& source_unit : document.controlled_units) {
        const auto descriptor_it = descriptors.find(source_unit.id);
        if (descriptor_it == descriptors.end()) {
            continue;
        }
        const auto* source_descriptor = descriptor_it->second;
        for (const auto& role : source_descriptor->public_roles) {
            if (role.direction != DataRoleDirection::Output) {
                continue;
            }
            const auto* mapping = find_mapping(
                source_descriptor->runtime_recipe,
                role.id);
            if (!mapping) {
                continue;
            }
            const auto runtime_node_id = projected_runtime_node_id(
                source_unit.id,
                mapping->runtime_endpoint.child_role_id);
            candidates.push_back({
                {source_unit.id, role.id},
                source_descriptor->id,
                projected_data_block_id(
                    runtime_node_id,
                    mapping->runtime_endpoint.port_id),
                role.data_type,
                role.capabilities,
            });
        }
    }

    try {
        const auto projection =
            project_clip_generator_runtime_settings(
                canonical_settings.dump(),
                1,
                bound_sources,
                candidates);
        return Json::parse(projection.settings_json);
    }
    catch (const ClipGeneratorProjectionError& error) {
        throw ProjectionFailure(error.code(), error.what());
    }
}

void expand_runtime_nodes(
    const ProjectDocument& document,
    const std::unordered_map<
        std::string,
        const ControlledUnitDescriptor*>& descriptors,
    const std::unordered_map<std::string, Json>& settings,
    const core::ModuleRegistry& runtime_registry,
    ProjectProjectionResult& result) {
    for (const auto& unit : document.controlled_units) {
        const auto* descriptor = descriptors.at(unit.id);
        const auto source_fft_geometry =
            bound_ishmael_fft_geometry(
                document,
                descriptors,
                settings,
                unit);
        for (const auto& child :
             descriptor->runtime_recipe.children) {
            const auto runtime_node_id =
                projected_runtime_node_id(unit.id, child.role_id);
            const auto runtime_settings =
                child.settings.adapter_id ==
                        kClipGeneratorRuntimeSettingsAdapterId
                ? clip_generator_runtime_settings(
                      document,
                      descriptors,
                      unit,
                      settings.at(unit.id))
                : project_child_settings(
                      child,
                      settings.at(unit.id),
                      result.array_geometry
                          ? &*result.array_geometry
                          : nullptr,
                      source_fft_geometry);
            result.graph.modules.push_back({
                runtime_node_id,
                child.runtime_type_id,
                unit.name + " [" + child.role_id + "]",
                true,
                runtime_settings.dump(),
            });
            result.index.runtime_nodes.push_back({
                unit.id,
                child.role_id,
                runtime_node_id,
                child.runtime_type_id,
            });

            const auto* runtime_type =
                runtime_registry.find(child.runtime_type_id);
            if (!runtime_type) {
                throw ProjectionFailure(
                    "invalid-registry",
                    "Runtime type '" + child.runtime_type_id +
                        "' is not registered");
            }
            for (const auto& port : runtime_type->ports) {
                if (port.direction != core::PortDirection::Output) {
                    continue;
                }
                result.index.data_blocks.push_back({
                    unit.id,
                    child.role_id,
                    runtime_node_id,
                    port.id,
                    projected_data_block_id(
                        runtime_node_id,
                        port.id),
                    port.data_type,
                    port.capabilities,
                });
            }
        }
        add_internal_connections(unit, *descriptor, result);
        add_public_role_index(unit, *descriptor, result);
    }
}

void validate_and_connect_bindings(
    const ProjectDocument& document,
    const std::unordered_map<
        std::string,
        const ControlledUnitInstance*>& units,
    const std::unordered_map<
        std::string,
        const ControlledUnitDescriptor*>& descriptors,
    ProjectProjectionResult& result) {
    std::unordered_set<std::string> connection_ids;
    for (const auto& connection : result.graph.connections) {
        connection_ids.emplace(connection.id);
    }

    for (const auto& unit : document.controlled_units) {
        const auto* descriptor = descriptors.at(unit.id);
        for (const auto& binding : unit.bindings) {
            if (!find_role(
                    *descriptor,
                    binding.input_role,
                    DataRoleDirection::Input)) {
                add_issue(
                    result,
                    ProjectionIssueClass::EditorInvalid,
                    "unknown-input-role",
                    "Binding refers to an unknown public input role",
                    unit.id,
                    binding.input_role);
            }
        }

        for (const auto& input_role :
             descriptor->public_roles) {
            if (input_role.direction !=
                DataRoleDirection::Input) {
                continue;
            }
            const auto* binding =
                find_binding(unit, input_role.id);
            const auto count =
                binding ? binding->sources.size() : 0;
            if (!accepts_multiple(input_role.cardinality) &&
                count > 1) {
                add_issue(
                    result,
                    ProjectionIssueClass::EditorInvalid,
                    "too-many-sources",
                    "Input role accepts at most one source",
                    unit.id,
                    input_role.id);
                continue;
            }
            if (requires_at_least_one(input_role.cardinality) &&
                count == 0) {
                add_issue(
                    result,
                    ProjectionIssueClass::NeedsConfiguration,
                    "missing-required-binding",
                    "Required input role has no source",
                    unit.id,
                    input_role.id);
                continue;
            }
            if (!binding) {
                continue;
            }

            auto* projected_input =
                find_projected_input(
                    result.index,
                    unit.id,
                    input_role.id);
            for (const auto& source : binding->sources) {
                const auto source_unit =
                    units.find(source.unit_id);
                if (source_unit == units.end()) {
                    add_issue(
                        result,
                        ProjectionIssueClass::EditorInvalid,
                        "unknown-source-unit",
                        "Binding source controlled unit does not exist",
                        unit.id,
                        input_role.id);
                    continue;
                }
                const auto* source_descriptor =
                    descriptors.at(source.unit_id);
                const auto* output_role = find_role(
                    *source_descriptor,
                    source.output_role,
                    DataRoleDirection::Output);
                if (!output_role) {
                    add_issue(
                        result,
                        ProjectionIssueClass::EditorInvalid,
                        "unknown-output-role",
                        "Binding source refers to an unknown public output role",
                        unit.id,
                        input_role.id);
                    continue;
                }
                if (!compatible_public_roles(
                        *output_role,
                        input_role)) {
                    add_issue(
                        result,
                        ProjectionIssueClass::EditorInvalid,
                        "incompatible-public-roles",
                        "Binding source type/capabilities are incompatible with the input",
                        unit.id,
                        input_role.id);
                    continue;
                }
                const auto* projected_output =
                    result.index.find_public_output(
                        source.unit_id,
                        source.output_role);
                if (!projected_output || !projected_input) {
                    add_issue(
                        result,
                        ProjectionIssueClass::EditorInvalid,
                        "unprojected-public-role",
                        "Compatible public role has no runtime projection",
                        unit.id,
                        input_role.id);
                    continue;
                }
                const auto connection_id =
                    projected_external_connection_id(
                        unit.id,
                        input_role.id,
                        source.unit_id,
                        source.output_role);
                if (!connection_ids.emplace(connection_id).second) {
                    add_issue(
                        result,
                        ProjectionIssueClass::EditorInvalid,
                        "duplicate-generated-connection",
                        "Public source references generated a duplicate connection ID",
                        unit.id,
                        input_role.id);
                    continue;
                }
                result.graph.connections.push_back({
                    connection_id,
                    {
                        projected_output->runtime_node_id,
                        projected_output->runtime_port_id,
                    },
                    {
                        projected_input->runtime_node_id,
                        projected_input->runtime_port_id,
                    },
                });
                projected_input->connection_ids.push_back(
                    connection_id);
                result.index.connections.push_back({
                    connection_id,
                    ProjectedConnectionKind::External,
                    unit.id,
                    {},
                    input_role.id,
                    source,
                });
            }
        }
    }
}

void validate_display_source(
    const DisplayInstance& display,
    const DisplayProviderDescriptor& provider,
    const std::unordered_map<
        std::string,
        const ControlledUnitDescriptor*>& descriptors,
    const ProjectionIndex& index,
    ProjectProjectionResult& result,
    ProjectedDisplayOwnership& ownership) {
    if (provider.public_roles.size() != 1 ||
        provider.public_roles.front().direction !=
            DataRoleDirection::Input) {
        add_issue(
            result,
            ProjectionIssueClass::EditorInvalid,
            "unsupported-display-source-contract",
            "Phase 1 display instances require exactly one public input role",
            display.owner.unit_id,
            {},
            display.id);
        return;
    }
    const auto& input_role = provider.public_roles.front();
    if (!display.source) {
        if (!provider.can_create_without_source &&
            requires_at_least_one(input_role.cardinality)) {
            add_issue(
                result,
                ProjectionIssueClass::NeedsConfiguration,
                "missing-display-source",
                "Display provider requires a compatible source",
                display.owner.unit_id,
                input_role.id,
                display.id);
        }
        return;
    }

    const auto descriptor =
        descriptors.find(display.source->unit_id);
    if (descriptor == descriptors.end()) {
        add_issue(
            result,
            ProjectionIssueClass::EditorInvalid,
            "unknown-display-source-unit",
            "Display source controlled unit does not exist",
            display.owner.unit_id,
            input_role.id,
            display.id);
        return;
    }
    const auto* output_role = find_role(
        *descriptor->second,
        display.source->output_role,
        DataRoleDirection::Output);
    if (!output_role ||
        !compatible_public_roles(*output_role, input_role)) {
        add_issue(
            result,
            ProjectionIssueClass::EditorInvalid,
            "incompatible-display-source",
            "Display source role is absent or incompatible",
            display.owner.unit_id,
            input_role.id,
            display.id);
        return;
    }
    const auto* projected_output = index.find_public_output(
        display.source->unit_id,
        display.source->output_role);
    if (!projected_output) {
        add_issue(
            result,
            ProjectionIssueClass::EditorInvalid,
            "unprojected-display-source",
            "Display source has no projected public data block",
            display.owner.unit_id,
            input_role.id,
            display.id);
        return;
    }
    ownership.source_block_id = projected_output->block_id;
}

void validate_displays(
    const ProjectDocument& document,
    const ControlledUnitRegistry& registry,
    const std::unordered_map<
        std::string,
        const ControlledUnitInstance*>& units,
    const std::unordered_map<
        std::string,
        const ControlledUnitDescriptor*>& descriptors,
    ProjectProjectionResult& result) {
    std::set<std::pair<std::string, std::string>>
        tab_owner_roles;
    std::unordered_map<std::string, std::size_t>
        provider_counts;
    std::unordered_map<std::string, std::size_t>
        tab_counts;

    for (const auto& tab : document.display_tabs) {
        const auto owner = units.find(tab.owner.unit_id);
        if (owner == units.end()) {
            add_issue(
                result,
                ProjectionIssueClass::EditorInvalid,
                "unknown-display-tab-owner",
                "Display tab owner does not exist",
                tab.owner.unit_id);
            continue;
        }
        const auto* owner_descriptor =
            descriptors.at(tab.owner.unit_id);
        if (owner_descriptor->runtime_recipe
                .display_provider_ids.empty()) {
            add_issue(
                result,
                ProjectionIssueClass::EditorInvalid,
                "invalid-display-tab-owner",
                "Display tab owner contributes no display providers",
                tab.owner.unit_id);
        }
        if (!tab_owner_roles.emplace(
                tab.owner.unit_id,
                tab.owner.role)
                 .second) {
            add_issue(
                result,
                ProjectionIssueClass::EditorInvalid,
                "duplicate-display-owner-role",
                "Display tab owner role must be unique per controlled unit",
                tab.owner.unit_id,
                tab.owner.role);
        }
        result.index.display_tabs.push_back({
            tab.id,
            tab.owner.unit_id,
            tab.owner.role,
        });
        ++tab_counts[tab.owner.unit_id];

        for (const auto& display : tab.displays) {
            const auto* provider =
                registry.find_display_provider(
                    display.provider_type_id);
            if (!provider) {
                add_issue(
                    result,
                    ProjectionIssueClass::EditorInvalid,
                    "unknown-display-provider",
                    "Display refers to an unknown provider",
                    display.owner.unit_id,
                    {},
                    display.id);
                continue;
            }
            if (provider->availability !=
                AvailabilityStatus::Available) {
                add_issue(
                    result,
                    ProjectionIssueClass::EditorInvalid,
                    "unavailable-display-provider",
                    "Display provider is unavailable",
                    display.owner.unit_id,
                    {},
                    display.id);
            }
            if (display.provider_version !=
                provider->descriptor_version) {
                add_issue(
                    result,
                    ProjectionIssueClass::EditorInvalid,
                    "display-provider-version-mismatch",
                    "Display provider descriptor version is unsupported",
                    display.owner.unit_id,
                    {},
                    display.id);
            }
            if (display.settings_version !=
                provider->settings.version) {
                add_issue(
                    result,
                    ProjectionIssueClass::EditorInvalid,
                    "display-settings-version-mismatch",
                    "Display settings version is unsupported",
                    display.owner.unit_id,
                    {},
                    display.id);
            }
            if (display.owner.unit_id != tab.owner.unit_id) {
                add_issue(
                    result,
                    ProjectionIssueClass::EditorInvalid,
                    "display-owner-tab-mismatch",
                    "Display owner must match its containing tab owner",
                    display.owner.unit_id,
                    {},
                    display.id);
            }
            const auto display_owner =
                units.find(display.owner.unit_id);
            if (display_owner == units.end()) {
                add_issue(
                    result,
                    ProjectionIssueClass::EditorInvalid,
                    "unknown-display-owner",
                    "Display owner controlled unit does not exist",
                    display.owner.unit_id,
                    {},
                    display.id);
                continue;
            }
            const auto* display_owner_descriptor =
                descriptors.at(display.owner.unit_id);
            if (display_owner_descriptor->id !=
                    provider->owner_controlled_unit_type_id ||
                std::find(
                    display_owner_descriptor->runtime_recipe
                        .display_provider_ids.begin(),
                    display_owner_descriptor->runtime_recipe
                        .display_provider_ids.end(),
                    provider->id) ==
                    display_owner_descriptor->runtime_recipe
                        .display_provider_ids.end()) {
                add_issue(
                    result,
                    ProjectionIssueClass::EditorInvalid,
                    "display-provider-owner-mismatch",
                    "Display provider is not contributed by its owner type",
                    display.owner.unit_id,
                    {},
                    display.id);
            }

            try {
                const auto settings = parse_settings(
                    display.settings_json,
                    "Display '" + display.id + "' settings");
                if (provider->id ==
                    "pamguard.spectrogram-display") {
                    validate_spectrogram_settings(settings);
                }
                else if (provider->id ==
                         "pamguard.click-display") {
                    try {
                        validate_click_display_settings_json(
                            settings.dump(),
                            provider->settings.version);
                    }
                    catch (const ClickDetectorSettingsError& error) {
                        throw ProjectionFailure(
                            "invalid-display-settings",
                            error.what());
                    }
                }
                else if (provider->id ==
                         "pamguard.level-meter-display") {
                    if (!settings.empty()) {
                        throw ProjectionFailure(
                            "invalid-display-settings",
                            "Level Meter display settings must be an "
                            "empty object; its scale belongs to the "
                            "controlled unit");
                    }
                }
            }
            catch (const ProjectionFailure& error) {
                add_issue(
                    result,
                    ProjectionIssueClass::EditorInvalid,
                    error.code(),
                    error.what(),
                    display.owner.unit_id,
                    {},
                    display.id);
            }

            ProjectedDisplayOwnership ownership{
                tab.id,
                display.id,
                display.owner.unit_id,
                display.owner.role,
                display.provider_type_id,
                display.source,
                std::nullopt,
            };
            validate_display_source(
                display,
                *provider,
                descriptors,
                result.index,
                result,
                ownership);
            result.index.displays.push_back(
                std::move(ownership));

            const auto count_key =
                display.owner.unit_id + "\n" + provider->id;
            ++provider_counts[count_key];
        }
    }

    for (const auto& unit : document.controlled_units) {
        const auto* descriptor = descriptors.at(unit.id);
        if (descriptor->id == "pamguard.user-display" &&
            tab_counts[unit.id] != 1) {
            add_issue(
                result,
                ProjectionIssueClass::EditorInvalid,
                "user-display-tab-ownership",
                "Each User Display controlled unit must own exactly one display tab",
                unit.id);
        }
        for (const auto& provider_id :
             descriptor->runtime_recipe.display_provider_ids) {
            const auto* provider =
                registry.find_display_provider(provider_id);
            if (!provider) {
                continue;
            }
            const auto count = provider_counts[
                unit.id + "\n" + provider_id];
            if (count < provider->minimum_instances ||
                (provider->maximum_instances &&
                 count > *provider->maximum_instances)) {
                add_issue(
                    result,
                    ProjectionIssueClass::EditorInvalid,
                    "display-provider-multiplicity",
                    "Display provider instance count violates its owner contract",
                    unit.id);
            }
        }
    }
}

bool missing_input_is_public_configuration(
    const ProjectionIndex& index,
    const std::string& runtime_node_id) {
    return std::any_of(
        index.public_inputs.begin(),
        index.public_inputs.end(),
        [&](const auto& input) {
            return input.runtime_node_id == runtime_node_id &&
                input.sources.empty() &&
                requires_at_least_one(input.cardinality);
        });
}

} // namespace

const ProjectedRuntimeNode* ProjectionIndex::find_runtime_node(
    std::string_view unit_id,
    std::string_view child_role_id) const noexcept {
    const auto found = std::find_if(
        runtime_nodes.begin(),
        runtime_nodes.end(),
        [&](const auto& node) {
            return node.owner_unit_id == unit_id &&
                node.child_role_id == child_role_id;
        });
    return found == runtime_nodes.end() ? nullptr : &*found;
}

const ProjectedDataBlock* ProjectionIndex::find_data_block(
    std::string_view block_id) const noexcept {
    const auto found = std::find_if(
        data_blocks.begin(),
        data_blocks.end(),
        [&](const auto& block) {
            return block.block_id == block_id;
        });
    return found == data_blocks.end() ? nullptr : &*found;
}

const ProjectedPublicOutput* ProjectionIndex::find_public_output(
    std::string_view unit_id,
    std::string_view output_role) const noexcept {
    const auto found = std::find_if(
        public_outputs.begin(),
        public_outputs.end(),
        [&](const auto& output) {
            return output.unit_id == unit_id &&
                output.output_role == output_role;
        });
    return found == public_outputs.end() ? nullptr : &*found;
}

const ProjectedPublicInput* ProjectionIndex::find_public_input(
    std::string_view unit_id,
    std::string_view input_role) const noexcept {
    const auto found = std::find_if(
        public_inputs.begin(),
        public_inputs.end(),
        [&](const auto& input) {
            return input.unit_id == unit_id &&
                input.input_role == input_role;
        });
    return found == public_inputs.end() ? nullptr : &*found;
}

const ProjectedDisplayOwnership* ProjectionIndex::find_display(
    std::string_view display_id) const noexcept {
    const auto found = std::find_if(
        displays.begin(),
        displays.end(),
        [&](const auto& display) {
            return display.display_id == display_id;
        });
    return found == displays.end() ? nullptr : &*found;
}

ProjectionStatus ProjectProjectionResult::status() const noexcept {
    if (!editor_valid()) {
        return ProjectionStatus::Invalid;
    }
    return std::any_of(
               issues.begin(),
               issues.end(),
               [](const auto& issue) {
                   return issue.issue_class ==
                       ProjectionIssueClass::NeedsConfiguration;
               })
        ? ProjectionStatus::NeedsConfiguration
        : ProjectionStatus::Runnable;
}

bool ProjectProjectionResult::editor_valid() const noexcept {
    return !std::any_of(
        issues.begin(),
        issues.end(),
        [](const auto& issue) {
            return issue.issue_class ==
                ProjectionIssueClass::EditorInvalid;
        });
}

bool ProjectProjectionResult::runnable() const noexcept {
    return status() == ProjectionStatus::Runnable;
}

bool ProjectProjectionResult::needs_configuration() const noexcept {
    return status() == ProjectionStatus::NeedsConfiguration;
}

std::string projected_runtime_node_id(
    std::string_view unit_id,
    std::string_view child_role_id) {
    return "rt:" + std::string(unit_id) + ":" +
        std::string(child_role_id);
}

std::string projected_internal_connection_id(
    std::string_view unit_id,
    std::string_view edge_role) {
    return "cx:" + std::string(unit_id) + ":internal:" +
        std::string(edge_role);
}

std::string projected_external_connection_id(
    std::string_view target_unit_id,
    std::string_view input_role,
    std::string_view source_unit_id,
    std::string_view output_role) {
    return "cx:" + std::string(target_unit_id) + ":" +
        std::string(input_role) + ":" +
        std::string(source_unit_id) + ":" +
        std::string(output_role);
}

std::string projected_data_block_id(
    std::string_view runtime_node_id,
    std::string_view port_id) {
    return "block:" + std::string(runtime_node_id) + ":" +
        std::string(port_id);
}

ProjectProjectionResult project_document_to_runtime_graph(
    const ProjectDocument& document,
    const ControlledUnitRegistry& controlled_unit_registry,
    const core::ModuleRegistry& runtime_registry) {
    ProjectProjectionResult result;
    result.graph.schema_version = 1;
    result.graph.revision = 0;

    ProjectDocument normalized;
    try {
        normalized = project_document_from_json(
            project_document_to_json(document));
    }
    catch (const std::exception& error) {
        add_issue(
            result,
            ProjectionIssueClass::EditorInvalid,
            "invalid-project-document",
            error.what());
        return result;
    }

    const auto registry_validation =
        controlled_unit_registry.validate_against(
            low_level_contracts(runtime_registry));
    for (const auto& issue : registry_validation.issues) {
        add_issue(
            result,
            ProjectionIssueClass::EditorInvalid,
            "registry-" + issue.code,
            issue.message);
    }
    if (has_editor_errors(result)) {
        return result;
    }

    if (normalized.descriptor_set.version != 1) {
        add_issue(
            result,
            ProjectionIssueClass::EditorInvalid,
            "descriptor-set-version-mismatch",
            "Phase 1 supports descriptor-set version 1");
    }

    std::unordered_set<std::string> global_types;
    for (const auto& component :
         normalized.global_settings.components) {
        global_types.emplace(component.type_id);
        const auto* descriptor =
            controlled_unit_registry.find_global_settings(
                component.type_id);
        if (!descriptor) {
            add_issue(
                result,
                ProjectionIssueClass::EditorInvalid,
                "unknown-global-settings-type",
                "Global settings type '" + component.type_id +
                    "' is not registered");
            continue;
        }
        if (descriptor->availability !=
            AvailabilityStatus::Available) {
            add_issue(
                result,
                ProjectionIssueClass::EditorInvalid,
                "unavailable-global-settings-type",
                "Global settings type '" + component.type_id +
                    "' is unavailable");
            continue;
        }
        if (component.settings_version !=
            descriptor->settings.version) {
            add_issue(
                result,
                ProjectionIssueClass::EditorInvalid,
                "global-settings-version-mismatch",
                "Global settings type '" + component.type_id +
                    "' uses an unsupported settings version");
            continue;
        }
        if (descriptor->adapter_id !=
            kArrayManagerSettingsAdapterId) {
            add_issue(
                result,
                ProjectionIssueClass::EditorInvalid,
                "unsupported-global-settings-adapter",
                "Global settings type '" + component.type_id +
                    "' uses unsupported adapter '" +
                    descriptor->adapter_id + "'");
            continue;
        }
        try {
            result.array_geometry =
                array_manager_settings_to_geometry(
                    component.settings_json,
                    component.settings_version);
        }
        catch (const std::exception& error) {
            add_issue(
                result,
                ProjectionIssueClass::EditorInvalid,
                "invalid-global-settings",
                error.what());
        }
    }
    for (const auto& descriptor :
         controlled_unit_registry.global_settings()) {
        if (descriptor.required &&
            !global_types.contains(descriptor.id)) {
            add_issue(
                result,
                ProjectionIssueClass::EditorInvalid,
                "missing-required-global-settings",
                "Required global settings type '" +
                    descriptor.id + "' is absent");
        }
    }
    if (has_editor_errors(result)) {
        return result;
    }

    const auto mode = registry_mode(normalized.mode);
    std::unordered_map<
        std::string,
        const ControlledUnitInstance*> units;
    std::unordered_map<
        std::string,
        const ControlledUnitDescriptor*> descriptors;
    std::unordered_map<std::string, std::size_t> type_counts;
    std::unordered_map<std::string, Json> settings;
    std::set<std::pair<std::string, std::string>>
        java_class_names;

    for (const auto& unit : normalized.controlled_units) {
        units.emplace(unit.id, &unit);
        const auto* descriptor =
            controlled_unit_registry.find_controlled_unit(
                unit.type_id);
        if (!descriptor) {
            add_issue(
                result,
                ProjectionIssueClass::EditorInvalid,
                "unknown-controlled-unit-type",
                "Controlled unit type is not registered",
                unit.id);
            continue;
        }
        descriptors.emplace(unit.id, descriptor);
        ++type_counts[descriptor->id];

        if (descriptor->availability !=
            AvailabilityStatus::Available) {
            add_issue(
                result,
                ProjectionIssueClass::EditorInvalid,
                "unavailable-controlled-unit-type",
                "Controlled unit type is unavailable",
                unit.id);
        }
        if (unit.descriptor_version !=
            descriptor->descriptor_version) {
            add_issue(
                result,
                ProjectionIssueClass::EditorInvalid,
                "descriptor-version-mismatch",
                "Controlled-unit descriptor version is unsupported",
                unit.id);
        }
        if (unit.settings_version !=
            descriptor->settings.version) {
            add_issue(
                result,
                ProjectionIssueClass::EditorInvalid,
                "settings-version-mismatch",
                "Controlled-unit settings version is unsupported",
                unit.id);
        }
        if (unit.recipe.id !=
                descriptor->runtime_recipe.id ||
            unit.recipe.version !=
                descriptor->runtime_recipe.version) {
            add_issue(
                result,
                ProjectionIssueClass::EditorInvalid,
                "recipe-version-mismatch",
                "Controlled-unit expansion recipe identity/version is unsupported",
                unit.id);
        }
        if (std::find(
                descriptor->instance_rules.allowed_modes.begin(),
                descriptor->instance_rules.allowed_modes.end(),
                mode) ==
            descriptor->instance_rules.allowed_modes.end()) {
            add_issue(
                result,
                ProjectionIssueClass::EditorInvalid,
                "unsupported-controlled-unit-run-mode",
                "Controlled unit is unavailable in the project run mode",
                unit.id);
        }

        const auto name_key = std::make_pair(
            ascii_case_fold(
                descriptor->java_authority.class_name),
            ascii_case_fold(unit.name));
        if (!java_class_names.emplace(name_key).second) {
            add_issue(
                result,
                ProjectionIssueClass::EditorInvalid,
                "duplicate-java-class-instance-name",
                "Instance names must be ASCII-case-insensitively unique within one Java controlled-unit class",
                unit.id);
        }

        try {
            auto parsed = parse_settings(
                unit.settings_json,
                "Controlled unit '" + unit.id + "' settings");
            validate_controlled_settings_root(
                *descriptor,
                parsed,
                result.array_geometry
                    ? &*result.array_geometry
                    : nullptr);
            if (descriptor->id == "pamguard.sound-output" &&
                parsed.at("channelBitmap").get<std::uint64_t>() == 0) {
                add_issue(
                    result,
                    ProjectionIssueClass::NeedsConfiguration,
                    "sound-output-no-channels",
                    "Sound Output requires at least one playback channel",
                    unit.id,
                    "audio");
            }
            if ((descriptor->id == "pamguard.filter" ||
                 descriptor->id == "pamguard.decimator") &&
                parsed.at("channelBitmap").get<std::uint64_t>() == 0) {
                add_issue(
                    result,
                    ProjectionIssueClass::NeedsConfiguration,
                    descriptor->id == "pamguard.filter"
                        ? "filter-no-channels"
                        : "decimator-no-channels",
                    descriptor->id == "pamguard.filter"
                        ? "Filter requires at least one selected channel"
                        : "Decimator requires at least one selected channel",
                    unit.id,
                    "rawAudio");
            }
            if ((descriptor->id ==
                     "pamguard.fft-noise-monitor" ||
                 descriptor->id ==
                     "pamguard.noise-band-monitor" ||
                 descriptor->id == "pamguard.ltsa") &&
                parsed.at("channelBitmap").get<std::uint64_t>() == 0) {
                const bool is_fft_noise =
                    descriptor->id ==
                    "pamguard.fft-noise-monitor";
                const bool is_noise_band =
                    descriptor->id ==
                    "pamguard.noise-band-monitor";
                add_issue(
                    result,
                    ProjectionIssueClass::NeedsConfiguration,
                    is_fft_noise
                        ? "noise-monitor-no-channels"
                        : is_noise_band
                            ? "noise-band-monitor-no-channels"
                            : "ltsa-no-channels",
                    is_fft_noise
                        ? "Noise Monitor requires at least one selected channel"
                        : is_noise_band
                            ? "Noise Band Monitor requires at least one selected channel"
                            : "LTSA requires at least one selected channel",
                    unit.id,
                    is_noise_band ? "rawAudio" : "fft");
            }
            if (descriptor->id ==
                    "pamguard.fft-noise-monitor" &&
                parsed.at("bands").empty()) {
                add_issue(
                    result,
                    ProjectionIssueClass::NeedsConfiguration,
                    "noise-monitor-no-measurement-bands",
                    "Noise Monitor requires at least one measurement band",
                    unit.id,
                    "fft");
            }
            if (descriptor->id ==
                    "pamguard.whistles-moans") {
                const auto decoded =
                    core::whistle_moan_settings_from_json(
                        parsed.dump(),
                        descriptor->settings.version);
                if (decoded.channel_bitmap == 0) {
                    add_issue(
                        result,
                        ProjectionIssueClass::NeedsConfiguration,
                        "whistle-moan-no-channels",
                        "Whistle and Moan Detector requires at least one selected FFT channel or sequence",
                        unit.id,
                        "fft");
                }
                if (!core::whistle_moan_local_noise_ready(
                        decoded)) {
                    add_issue(
                        result,
                        ProjectionIssueClass::NeedsConfiguration,
                        "whistle-moan-noise-chain",
                        "Whistle and Moan Detector requires Median Filter, Average Subtraction, and Thresholding for the supported standard FFT source path",
                        unit.id,
                        "fft");
                }
            }
            if (descriptor->id ==
                    "pamguard.mht-click-train") {
                const auto decoded =
                    core::mht_click_train_settings_from_json(
                        parsed.dump(),
                        descriptor->settings.version);
                if (!core::mht_click_train_has_channel_groups(
                        decoded)) {
                    add_issue(
                        result,
                        ProjectionIssueClass::NeedsConfiguration,
                        "mht-click-train-no-channel-groups",
                        "Click Train Detector requires at least one "
                        "non-overlapping channel group",
                        unit.id,
                        "clicks");
                }
                if (core::mht_click_train_requires_features(
                        decoded) &&
                    !find_binding(unit, "features")) {
                    add_issue(
                        result,
                        ProjectionIssueClass::NeedsConfiguration,
                        "mht-click-train-missing-features",
                        "Enabled peak-frequency chi2 requires the Click "
                        "Detector features output",
                        unit.id,
                        "features");
                }
                if (core::mht_click_train_requires_localisations(
                        decoded) &&
                    !find_binding(unit, "localisations")) {
                    add_issue(
                        result,
                        ProjectionIssueClass::NeedsConfiguration,
                        "mht-click-train-missing-localisations",
                        "Enabled time-delay chi2 requires the Click "
                        "Detector localisation output",
                        unit.id,
                        "localisations");
                }
                if (core::mht_click_train_requires_bearings(
                        decoded) &&
                    !find_binding(unit, "bearings")) {
                    add_issue(
                        result,
                        ProjectionIssueClass::NeedsConfiguration,
                        "mht-click-train-missing-bearings",
                        "Enabled bearing chi2 or classifier requires the "
                        "Click Detector bearing output",
                        unit.id,
                        "bearings");
                }
            }
            if (descriptor->id ==
                "pamguard.ishmael-energy-sum") {
                const auto decoded =
                    core::ishmael_energy_sum_settings_from_json(
                        parsed.dump(),
                        descriptor->settings.version);
                if (decoded.source.channel_bitmap == 0) {
                    add_issue(
                        result,
                        ProjectionIssueClass::
                            NeedsConfiguration,
                        "ishmael-energy-no-channels",
                        "Ishmael Energy Sum requires at least one selected FFT channel or sequence",
                        unit.id,
                        "fft");
                }
            }
            if (descriptor->id ==
                "pamguard.ishmael-sgram-corr") {
                const auto decoded =
                    core::ishmael_sgram_corr_settings_from_json(
                        parsed.dump(),
                        descriptor->settings.version);
                if (decoded.source.channel_bitmap == 0) {
                    add_issue(
                        result,
                        ProjectionIssueClass::
                            NeedsConfiguration,
                        "ishmael-sgram-no-channels",
                        "Ishmael Spectrogram Correlation requires at least one selected FFT channel or sequence",
                        unit.id,
                        "fft");
                }
                if (decoded.segments.empty()) {
                    add_issue(
                        result,
                        ProjectionIssueClass::
                            NeedsConfiguration,
                        "ishmael-sgram-no-segments",
                        "Ishmael Spectrogram Correlation requires at least one time-frequency segment",
                        unit.id,
                        "fft");
                }
            }
            if (descriptor->id ==
                "pamguard.ishmael-match-filter") {
                const auto decoded =
                    core::ishmael_match_filter_settings_from_json(
                        parsed.dump(),
                        descriptor->settings.version);
                if (!core::ishmael_match_filter_ready(
                        decoded)) {
                    add_issue(
                        result,
                        ProjectionIssueClass::
                            NeedsConfiguration,
                        "ishmael-match-no-kernel",
                        "Ishmael Matched Filtering requires an active kernel file and embedded first-channel samples",
                        unit.id,
                        "rawAudio");
                }
            }
            settings.emplace(unit.id, std::move(parsed));
        }
        catch (const ProjectionFailure& error) {
            add_issue(
                result,
                ProjectionIssueClass::EditorInvalid,
                error.code(),
                error.what(),
                unit.id);
        }
    }

    for (const auto& descriptor :
         controlled_unit_registry.controlled_units()) {
        const auto count = type_counts[descriptor.id];
        const auto instance_limits =
            effective_instance_limits(
                descriptor.instance_rules,
                mode);
        if (count < instance_limits.minimum_instances ||
            (instance_limits.maximum_instances &&
             count >
                 *instance_limits.maximum_instances)) {
            add_issue(
                result,
                ProjectionIssueClass::EditorInvalid,
                "controlled-unit-multiplicity",
                "Controlled-unit instance count violates its descriptor",
                {},
                descriptor.id);
        }
    }

    if (has_editor_errors(result)) {
        return result;
    }

    try {
        expand_runtime_nodes(
            normalized,
            descriptors,
            settings,
            runtime_registry,
            result);
    }
    catch (const ProjectionFailure& error) {
        add_issue(
            result,
            ProjectionIssueClass::EditorInvalid,
            error.code(),
            error.what());
        return result;
    }

    validate_and_connect_bindings(
        normalized,
        units,
        descriptors,
        result);
    validate_displays(
        normalized,
        controlled_unit_registry,
        units,
        descriptors,
        result);
    if (has_editor_errors(result)) {
        return result;
    }

    core::ModuleGraph graph_validator(runtime_registry);
    const auto graph_validation =
        graph_validator.validate(result.graph);
    for (const auto& issue : graph_validation.issues) {
        if (issue.code == "missing_required_input" &&
            missing_input_is_public_configuration(
                result.index,
                issue.module_id)) {
            continue;
        }
        add_issue(
            result,
            ProjectionIssueClass::EditorInvalid,
            "generated-graph-" + issue.code,
            issue.message,
            issue.module_id);
    }
    return result;
}

} // namespace pamguard::project
