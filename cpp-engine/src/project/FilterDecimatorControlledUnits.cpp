#include "pamguard/project/FilterDecimatorControlledUnits.h"

#include "pamguard/core/FilterDecimatorSettings.h"

#include <optional>
#include <utility>

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

PublicDataRoleDescriptor raw_audio_output(
    std::string id,
    std::string name) {
    return {
        std::move(id),
        std::move(name),
        DataRoleDirection::Output,
        "pamguard.raw-audio",
        RoleCardinality::ExactlyOne,
        {"sampled"},
        {},
        std::nullopt,
    };
}

RuntimeExpansionRecipeDescriptor single_child_recipe(
    std::string recipe_id,
    std::string child_role,
    std::string runtime_type_id,
    std::string adapter_id,
    std::string output_public_role) {
    const auto role = child_role;
    return {
        1,
        {
            {
                std::move(child_role),
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
                "rawAudio",
                {
                    role,
                    "input",
                },
            },
            {
                std::move(output_public_role),
                {
                    role,
                    "output",
                },
            },
        },
        {},
        {},
        std::move(recipe_id),
    };
}

std::vector<SettingDefaultDescriptor>
filter_default_evidence(
    std::string pointer_prefix,
    std::string authority_prefix) {
    const auto pointer = [&](std::string suffix) {
        return pointer_prefix + std::move(suffix);
    };
    const auto path = [&](std::string suffix) {
        return authority_prefix + std::move(suffix);
    };
    return {
        {
            pointer("/type"),
            path(".filterType"),
            R"("butterworth")",
            {},
            {},
            "Filters.FilterParams#filterType",
        },
        {
            pointer("/band"),
            path(".filterBand"),
            R"("bandPass")",
            {},
            {},
            "Filters.FilterParams#filterBand",
        },
        {
            pointer("/order"),
            path(".filterOrder"),
            "4",
            {},
            {},
            "Filters.FilterParams#FilterParams",
        },
        {
            pointer("/lowPassFreqHz"),
            path(".lowPassFreq"),
            "20000",
            {},
            {},
            "Filters.FilterParams#FilterParams",
        },
        {
            pointer("/highPassFreqHz"),
            path(".highPassFreq"),
            "2000",
            {},
            {},
            "Filters.FilterParams#FilterParams",
        },
        {
            pointer("/passBandRippleDb"),
            path(".passBandRipple"),
            "2",
            {},
            {},
            "Filters.FilterParams#FilterParams",
        },
        {
            pointer("/stopBandRippleDb"),
            path(".stopBandRipple"),
            "2",
            {},
            {},
            "Filters.FilterParams#FilterParams",
        },
        {
            pointer("/chebyGamma"),
            path(".chebyGamma"),
            "3",
            {},
            {},
            "Filters.FilterParams#chebyGamma",
        },
        {
            pointer("/arbitraryFrequenciesHz"),
            path(".arbFreqs"),
            "[]",
            {},
            "Java null normalized to a portable empty array",
            "Filters.FilterParams#arbFreqs",
        },
        {
            pointer("/arbitraryGainsDb"),
            path(".arbGains"),
            "[]",
            {},
            "Java null normalized to a portable empty array",
            "Filters.FilterParams#arbGains",
        },
    };
}

} // namespace

ControlledUnitDescriptor
make_standalone_filter_controlled_unit_descriptor() {
    auto defaults =
        filter_default_evidence("", "filterParams");
    defaults.insert(
        defaults.begin(),
        {
            "/channelBitmap",
            "channelBitmap",
            "0",
            {},
            "before the operator selects source channels",
            "Filters.FilterParameters_2#channelBitmap",
        });

    return {
        "pamguard.filter",
        1,
        {
            "Filters (IIR and FIR)",
            "Sound Processing",
            "Filters.FilterControl",
            "direct",
            "Filters audio data",
            "sound_processing/FiltersHelp/Docs/Filters_filters.html",
            {
                "src/PamModel/PamModel.java",
                "src/Filters/FilterControl.java",
                "src/Filters/FilterParameters_2.java",
                "src/Filters/FilterParams.java",
                "src/Filters/FilterDialog.java",
                "src/Filters/FilterDialogPanel.java",
                "src/Filters/FilterProcess.java",
                "src/Filters/layoutFX/FilterSettingsPaneFX.java",
            },
        },
        unlimited_in_all_modes(),
        {
            raw_audio_input(),
            raw_audio_output(
                "filteredAudio",
                "Filtered raw audio"),
        },
        {
            1,
            {
                "Filters.FilterParameters_2",
                "Filters.FilterParams",
            },
            {
                "src/Filters/FilterControl.java",
                "src/Filters/FilterParameters_2.java",
                "src/Filters/FilterParams.java",
                "src/Filters/FilterDialog.java",
                "src/Filters/FilterDialogPanel.java",
                "src/Filters/FilterProcess.java",
            },
            core::standalone_filter_default_settings_json(),
            {
                {
                    "settings.dialog",
                    {
                        "Data input",
                        "Filter Type",
                        "Filter Response",
                        "Filter shape",
                        "Filter parameters",
                        "Pole/impulse response",
                        "Bode Plot",
                        "Log Scale",
                        "Linear Scale",
                    },
                },
            },
            std::move(defaults),
            SettingsChangePolicy::StopRequired,
            "not-claimed",
            std::string(
                core::standalone_filter_settings_schema_json()),
        },
        single_child_recipe(
            "pamguard.filter.runtime",
            "filter-process",
            "pamguard.filter",
            "pamguard.standalone-filter-settings.v1",
            "filteredAudio"),
        AvailabilityStatus::Available,
        "partial",
    };
}

ControlledUnitDescriptor
make_decimator_controlled_unit_descriptor() {
    auto defaults =
        filter_default_evidence("/filter", "filterParams");
    for (auto& value : defaults) {
        if (value.pointer == "/filter/band") {
            value.value_json = R"("lowPass")";
        }
        else if (value.pointer == "/filter/order") {
            value.value_json = "6";
        }
        else if (value.pointer ==
                 "/filter/lowPassFreqHz") {
            value.value_json = "1000";
        }
    }
    defaults.insert(
        defaults.begin(),
        {
            {
                "/outputSampleRateHz",
                "newSampleRate",
                "2000",
                {},
                {},
                "decimator.DecimatorParams#newSampleRate",
            },
            {
                "/channelBitmap",
                "channelMap",
                "0",
                {},
                "bare default before source/channel selection; the obsolete clone migration is excluded",
                "decimator.DecimatorParams#channelMap",
            },
            {
                "/interpolation",
                "interpolation",
                "0",
                {},
                {},
                "decimator.DecimatorParams#interpolation",
            },
        });

    return {
        "pamguard.decimator",
        1,
        {
            "Decimator",
            "Sound Processing",
            "decimator.DecimatorControl",
            "direct",
            "Decimates (reduces the frequency of) audio data",
            "sound_processing/decimatorHelp/docs/decimator_decimator.html",
            {
                "src/PamModel/PamModel.java",
                "src/decimator/DecimatorControl.java",
                "src/decimator/DecimatorParams.java",
                "src/decimator/DecimatorParamsDialog.java",
                "src/decimator/DecimatorProcessW.java",
                "src/decimator/DecimatorWorker.java",
                "src/decimator/layoutFX/DecimatorSettingsPane.java",
            },
        },
        unlimited_in_all_modes(),
        {
            raw_audio_input(),
            raw_audio_output(
                "decimatedAudio",
                "Decimated raw audio"),
        },
        {
            1,
            {
                "decimator.DecimatorParams",
                "Filters.FilterParams",
            },
            {
                "src/decimator/DecimatorControl.java",
                "src/decimator/DecimatorParams.java",
                "src/decimator/DecimatorParamsDialog.java",
                "src/decimator/DecimatorProcessW.java",
                "src/decimator/DecimatorWorker.java",
                "src/Filters/FilterDialog.java",
                "src/Filters/FilterDialogPanel.java",
            },
            core::decimator_default_settings_json(),
            {
                {
                    "settings.dialog",
                    {
                        "Input Data Source",
                        "Decimator settings",
                        "Source sample rate",
                        "Output sample rate",
                        "Anti-aliasing filter",
                        "Interpolation",
                    },
                },
                {
                    "settings.viewer-tabs",
                    {
                        "Offline Files",
                        "Runtime Settings",
                    },
                },
                {
                    "filter-settings.dialog",
                    {
                        "Filter Type",
                        "Filter Response",
                        "Filter shape",
                        "Filter parameters",
                        "Pole/impulse response",
                        "Bode Plot",
                        "Log Scale",
                        "Linear Scale",
                    },
                },
            },
            std::move(defaults),
            SettingsChangePolicy::StopRequired,
            "not-claimed",
            std::string(
                core::decimator_settings_schema_json()),
        },
        single_child_recipe(
            "pamguard.decimator.runtime",
            "decimator-process",
            "pamguard.decimator",
            "pamguard.decimator-settings.v1",
            "decimatedAudio"),
        AvailabilityStatus::Available,
        "partial",
    };
}

} // namespace pamguard::project
