#include "pamguard/project/IshmaelControlledUnits.h"

#include <optional>
#include <utility>

#include "pamguard/core/IshmaelSettings.h"

namespace pamguard::project {

namespace {

InstanceRulesDescriptor unlimited_in_all_modes() {
    return {
        0,
        std::nullopt,
        {
            RunMode::Normal,
            RunMode::Mixed,
            RunMode::Viewer,
        },
        {},
    };
}

PublicDataRoleDescriptor fft_input() {
    return {
        "fft",
        "FFT data source",
        DataRoleDirection::Input,
        "pamguard.fft",
        RoleCardinality::ExactlyOne,
        {"frequency-domain"},
        "fftManager.FFTDataUnit",
        "pamguard.fft",
    };
}

PublicDataRoleDescriptor raw_audio_input() {
    return {
        "rawAudio",
        "Raw audio source",
        DataRoleDirection::Input,
        "pamguard.raw-audio",
        RoleCardinality::ExactlyOne,
        {"sampled"},
        "PamDetection.RawDataUnit",
        "pamguard.acquisition",
    };
}

PublicDataRoleDescriptor detection_function_output() {
    return {
        "detectionFunction",
        "Ishmael detection function",
        DataRoleDirection::Output,
        "pamguard.ishmael-detection-function",
        RoleCardinality::ExactlyOne,
        {"timeseries"},
        {},
        std::nullopt,
    };
}

PublicDataRoleDescriptor detections_output() {
    return {
        "detections",
        "Ishmael detections",
        DataRoleDirection::Output,
        "pamguard.ishmael-detection",
        RoleCardinality::ExactlyOne,
        {"detections", "overlay", "clip-trigger"},
        {},
        std::nullopt,
    };
}

std::vector<SettingDefaultDescriptor>
common_default_evidence() {
    return {
        {
            "/channelBitmap",
            "groupedSourceParmas.channelBitmap",
            "0",
            {},
            "before an operator selects channels or sequences",
            "PamView.GroupedSourceParameters#channelBitmap",
        },
        {
            "/groupingType",
            "groupedSourceParmas.groupingType",
            R"("all")",
            {},
            "Java GROUP_ALL integer 1 uses a stable portable name",
            "PamView.GroupedSourceParameters#groupingType",
        },
        {
            "/channelGroups",
            "groupedSourceParmas.channelGroups",
            "[]",
            {},
            "Java null normalized to a portable empty array",
            "PamView.GroupedSourceParameters#channelGroups",
        },
        {
            "/threshold",
            "thresh",
            "1",
            {},
            {},
            "IshmaelDetector.IshDetParams#thresh",
        },
        {
            "/minTimeSeconds",
            "minTime",
            "0",
            {},
            {},
            "IshmaelDetector.IshDetParams#minTime",
        },
        {
            "/maxTimeSeconds",
            "maxTime",
            "99999",
            {},
            {},
            "IshmaelDetector.IshDetParams#maxTime",
        },
        {
            "/refractoryTimeSeconds",
            "refractoryTime",
            "0",
            {},
            {},
            "IshmaelDetector.IshDetParams#refractoryTime",
        },
    };
}

RuntimeExpansionRecipeDescriptor one_detector_recipe(
    std::string runtime_type_id,
    std::string adapter_id,
    std::string public_input_role) {
    return {
        1,
        {
            {
                "detector",
                std::move(runtime_type_id),
                {
                    "",
                    std::move(adapter_id),
                },
                true,
                AvailabilityStatus::Available,
                "java-fixture-validated",
            },
        },
        {
            {
                std::move(public_input_role),
                {
                    "detector",
                    "input",
                },
            },
            {
                "detectionFunction",
                {
                    "detector",
                    "function",
                },
            },
            {
                "detections",
                {
                    "detector",
                    "detections",
                },
            },
        },
        {},
        {},
        {},
    };
}

std::vector<std::string> common_settings_classes(
    std::string concrete_class) {
    return {
        std::move(concrete_class),
        "IshmaelDetector.IshDetParams",
        "PamView.GroupedSourceParameters",
    };
}

std::vector<std::string> common_settings_sources(
    std::string control,
    std::string params,
    std::string process) {
    return {
        "src/PamModel/PamModel.java",
        std::move(control),
        std::move(params),
        "src/IshmaelDetector/IshDetParams.java",
        "src/IshmaelDetector/IshDetControl.java",
        "src/IshmaelDetector/IshDetFnProcess.java",
        std::move(process),
        "src/IshmaelDetector/IshPeakProcess.java",
        "src/PamView/GroupedSourceParameters.java",
        "src/PamView/dialog/GroupedSourcePanel.java",
    };
}

} // namespace

ControlledUnitDescriptor
make_ishmael_energy_sum_controlled_unit_descriptor() {
    auto defaults = common_default_evidence();
    defaults.insert(
        defaults.end(),
        {
            {
                "/f0Hz",
                "f0",
                "0",
                {},
                {},
                "IshmaelDetector.EnergySumParams#f0",
            },
            {
                "/f1Hz",
                "f1",
                "1000",
                {},
                {},
                "IshmaelDetector.EnergySumParams#f1",
            },
            {
                "/ratioF0Hz",
                "ratiof0",
                "1000",
                {},
                {},
                "IshmaelDetector.EnergySumParams#ratiof0",
            },
            {
                "/ratioF1Hz",
                "ratiof1",
                "2000",
                {},
                {},
                "IshmaelDetector.EnergySumParams#ratiof1",
            },
            {
                "/useRatio",
                "useRatio",
                "false",
                {},
                {},
                "IshmaelDetector.EnergySumParams#useRatio",
            },
            {
                "/adaptiveThreshold",
                "adaptiveThreshold",
                "false",
                {},
                {},
                "IshmaelDetector.EnergySumParams#adaptiveThreshold",
            },
            {
                "/longFilter",
                "longFilter",
                "0.0001",
                {},
                {},
                "IshmaelDetector.EnergySumParams#longFilter",
            },
            {
                "/useLog",
                "useLog",
                "false",
                {},
                {},
                "IshmaelDetector.EnergySumParams#useLog",
            },
            {
                "/spikeDecay",
                "spikeDecay",
                "100",
                {},
                {},
                "IshmaelDetector.EnergySumParams#spikeDecay",
            },
            {
                "/outputSmoothing",
                "outPutSmoothing",
                "false",
                {},
                {},
                "IshmaelDetector.EnergySumParams#outPutSmoothing",
            },
            {
                "/shortFilter",
                "shortFilter",
                "0.1",
                {},
                {},
                "IshmaelDetector.EnergySumParams#shortFilter",
            },
        });

    auto recipe = one_detector_recipe(
        "pamguard.ishmael-energy-sum",
        "pamguard.ishmael-energy-sum-settings.v1",
        "fft");
    recipe.id = "pamguard.ishmael-energy-sum.runtime";
    return {
        "pamguard.ishmael-energy-sum",
        1,
        {
            "Ishmael energy sum",
            "Detectors",
            "IshmaelDetector.EnergySumControl",
            "direct",
            "Detects sounds with energy in a specific frequency band",
            "detectors/ishmael/docs/ishmael_energysum.html",
            {
                "src/PamModel/PamModel.java",
                "src/IshmaelDetector/EnergySumControl.java",
            },
        },
        unlimited_in_all_modes(),
        {
            fft_input(),
            detection_function_output(),
            detections_output(),
        },
        {
            1,
            common_settings_classes(
                "IshmaelDetector.EnergySumParams"),
            common_settings_sources(
                "src/IshmaelDetector/EnergySumControl.java",
                "src/IshmaelDetector/EnergySumParams.java",
                "src/IshmaelDetector/EnergySumProcess.java"),
            core::ishmael_energy_sum_default_settings_json(),
            {
                {
                    "settings.source",
                    {
                        "FFT Data Source",
                        "Channel/Sequence list and grouping",
                    },
                },
                {
                    "settings.energy-sum",
                    {
                        "Lower Frequency Bound",
                        "Upper Frequency Bound",
                        "Use Energy Ratio",
                        "Lower Ratio Bound",
                        "Upper Ratio Bound",
                        "Use Adaptive Threshold",
                        "Long filter",
                        "Spike Threshold",
                        "Use Detector Smoothing",
                        "Short filter",
                        "Use log scale",
                    },
                },
                {
                    "settings.peak-detection",
                    {
                        "Threshold",
                        "Min time over threshold",
                        "Max time over threshold",
                        "Min IDI",
                    },
                },
            },
            std::move(defaults),
            SettingsChangePolicy::StopRequired,
            "java-fixture-validated",
            std::string(
                core::ishmael_energy_sum_settings_schema_json()),
        },
        std::move(recipe),
        AvailabilityStatus::Available,
        "partial",
    };
}

ControlledUnitDescriptor
make_ishmael_sgram_corr_controlled_unit_descriptor() {
    auto defaults = common_default_evidence();
    defaults.insert(
        defaults.end(),
        {
            {
                "/segments",
                "segment",
                "[]",
                {},
                "constructor default requires operator configuration",
                "IshmaelDetector.SgramCorrParams#segment",
            },
            {
                "/spreadHz",
                "spread",
                "100",
                {},
                {},
                "IshmaelDetector.SgramCorrParams#spread",
            },
            {
                "/useLog",
                "useLog",
                "false",
                {},
                "uninitialized Java boolean default",
                "IshmaelDetector.SgramCorrParams#useLog",
            },
        });

    auto recipe = one_detector_recipe(
        "pamguard.ishmael-sgram-corr",
        "pamguard.ishmael-sgram-corr-settings.v1",
        "fft");
    recipe.id = "pamguard.ishmael-sgram-corr.runtime";
    return {
        "pamguard.ishmael-sgram-corr",
        1,
        {
            "Ishmael spectrogram correlation",
            "Detectors",
            "IshmaelDetector.SgramCorrControl",
            "direct",
            "Detects sounds matching a user defined 'shape' on a spectrogram",
            "detectors/ishmael/docs/ishmael_speccorrelation.html",
            {
                "src/PamModel/PamModel.java",
                "src/IshmaelDetector/SgramCorrControl.java",
            },
        },
        unlimited_in_all_modes(),
        {
            fft_input(),
            detection_function_output(),
            detections_output(),
        },
        {
            1,
            common_settings_classes(
                "IshmaelDetector.SgramCorrParams"),
            common_settings_sources(
                "src/IshmaelDetector/SgramCorrControl.java",
                "src/IshmaelDetector/SgramCorrParams.java",
                "src/IshmaelDetector/SgramCorrProcess.java"),
            core::ishmael_sgram_corr_default_settings_json(),
            {
                {
                    "settings.source",
                    {
                        "FFT Data Source",
                        "Channel/Sequence list and grouping",
                    },
                },
                {
                    "settings.spectrogram-correlation",
                    {
                        "Segments (t0, f0, t1, f1)",
                        "Add Row",
                        "Remove Selected Row",
                        "Paste from Clipboard",
                        "Kernel Width, Hz",
                        "Use log-scaled spectrogram",
                        "Time-Frequency Contour",
                    },
                },
                {
                    "settings.peak-detection",
                    {
                        "Threshold",
                        "Min time over threshold",
                        "Max time over threshold",
                        "Min IDI",
                    },
                },
            },
            std::move(defaults),
            SettingsChangePolicy::StopRequired,
            "java-fixture-validated",
            std::string(
                core::ishmael_sgram_corr_settings_schema_json()),
        },
        std::move(recipe),
        AvailabilityStatus::Available,
        "partial",
    };
}

ControlledUnitDescriptor
make_ishmael_match_filter_controlled_unit_descriptor() {
    auto defaults = common_default_evidence();
    defaults.push_back({
        "/kernelFilenameList",
        "kernelFilenameList",
        "[]",
        {},
        "element zero becomes the active kernel; history is capped at ten",
        "IshmaelDetector.MatchFiltParams#kernelFilenameList",
    });
    defaults.push_back({
        "/kernelSamples",
        "MatchFiltProcess2.kernel",
        "[]",
        {},
        "portable embedded form of the active file's first channel",
        "IshmaelDetector.MatchFiltProcess2#prepareKernel",
    });

    auto recipe = one_detector_recipe(
        "pamguard.ishmael-match-filter",
        "pamguard.ishmael-match-filter-settings.v1",
        "rawAudio");
    recipe.id = "pamguard.ishmael-match-filter.runtime";
    return {
        "pamguard.ishmael-match-filter",
        1,
        {
            "Ishmael matched filtering",
            "Detectors",
            "IshmaelDetector.MatchFiltControl",
            "direct",
            "Detects sounds using a user defined matched filter",
            "detectors/ishmael/docs/ishmael_matchedfilter.html",
            {
                "src/PamModel/PamModel.java",
                "src/IshmaelDetector/MatchFiltControl.java",
            },
        },
        unlimited_in_all_modes(),
        {
            raw_audio_input(),
            detection_function_output(),
            detections_output(),
        },
        {
            1,
            common_settings_classes(
                "IshmaelDetector.MatchFiltParams"),
            common_settings_sources(
                "src/IshmaelDetector/MatchFiltControl.java",
                "src/IshmaelDetector/MatchFiltParams.java",
                "src/IshmaelDetector/MatchFiltProcess2.java"),
            core::ishmael_match_filter_default_settings_json(),
            {
                {
                    "settings.source",
                    {
                        "Data Source",
                        "Channel list and grouping",
                    },
                },
                {
                    "settings.matched-filter",
                    {
                        "Kernel sound file",
                        "Select another file...",
                    },
                },
                {
                    "settings.peak-picking",
                    {
                        "Threshold",
                        "Min time over threshold",
                        "Min time before next detection",
                    },
                },
            },
            std::move(defaults),
            SettingsChangePolicy::StopRequired,
            "java-fixture-validated",
            std::string(
                core::ishmael_match_filter_settings_schema_json()),
        },
        std::move(recipe),
        AvailabilityStatus::Available,
        "partial",
    };
}

} // namespace pamguard::project
