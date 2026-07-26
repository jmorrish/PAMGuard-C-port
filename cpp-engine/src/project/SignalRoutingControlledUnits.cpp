#include "pamguard/project/SignalRoutingControlledUnits.h"

#include "pamguard/core/SignalRoutingSettings.h"

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
                    "identity.v1",
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

} // namespace

ControlledUnitDescriptor
make_signal_amplifier_controlled_unit_descriptor() {
    return {
        "pamguard.amplifier",
        1,
        {
            "Signal Amplifier",
            "Sound Processing",
            "amplifier.AmpControl",
            "direct",
            "Amplifies (or attenuates) audio data",
            "sound_processing/amplifier/docs/amplifier.html",
            {
                "src/PamModel/PamModel.java",
                "src/amplifier/AmpControl.java",
                "src/amplifier/AmpParameters.java",
                "src/amplifier/AmpDialog.java",
                "src/amplifier/AmpProcess.java",
                "src/amplifier/AmplifiedDataBlock.java",
            },
        },
        unlimited_in_all_modes(),
        {
            raw_audio_input(),
            raw_audio_output(
                "amplifiedAudio",
                "Amplified raw audio"),
        },
        {
            1,
            {
                "amplifier.AmpParameters",
            },
            {
                "src/amplifier/AmpControl.java",
                "src/amplifier/AmpParameters.java",
                "src/amplifier/AmpDialog.java",
                "src/amplifier/AmpProcess.java",
            },
            core::signal_amplifier_default_settings_json(),
            {
                {
                    "settings.dialog",
                    {
                        "Raw Data input",
                        "Channel Gains",
                        "Gain (dB)",
                        "invert",
                    },
                },
            },
            {
                {
                    "/channelSettings/0/gainDb",
                    "gain[].magnitudeDb",
                    "0",
                    {},
                    "all 32 absolute channels",
                    "amplifier.AmpParameters#AmpParameters / amplifier.AmpDialog#getParams",
                },
                {
                    "/channelSettings/0/invert",
                    "gain[].invert",
                    "false",
                    {},
                    "all 32 absolute channels",
                    "amplifier.AmpParameters#AmpParameters / amplifier.AmpDialog#getParams",
                },
            },
            SettingsChangePolicy::StopRequired,
            "not-claimed",
            std::string(
                core::signal_amplifier_settings_schema_json()),
        },
        single_child_recipe(
            "pamguard.amplifier.runtime",
            "amplifier-process",
            "pamguard.amplifier",
            "amplifiedAudio"),
        AvailabilityStatus::Available,
        "partial",
    };
}

ControlledUnitDescriptor
make_patch_panel_controlled_unit_descriptor() {
    return {
        "pamguard.patch-panel",
        1,
        {
            "Patch Panel",
            "Sound Processing",
            "patchPanel.PatchPanelControl",
            "direct",
            "Reorganises and mixes audio data between channels",
            {},
            {
                "src/PamModel/PamModel.java",
                "src/patchPanel/PatchPanelControl.java",
                "src/patchPanel/PatchPanelParameters.java",
                "src/patchPanel/PatchPanelDialog.java",
                "src/patchPanel/PatchPanelProcess.java",
                "src/patchPanel/PatchPanelChannelList.java",
            },
        },
        unlimited_in_all_modes(),
        {
            raw_audio_input(),
            raw_audio_output(
                "patchedAudio",
                "Patched raw audio"),
        },
        {
            1,
            {
                "patchPanel.PatchPanelParameters",
            },
            {
                "src/patchPanel/PatchPanelControl.java",
                "src/patchPanel/PatchPanelParameters.java",
                "src/patchPanel/PatchPanelDialog.java",
                "src/patchPanel/PatchPanelProcess.java",
                "src/patchPanel/PatchPanelChannelList.java",
            },
            core::patch_panel_default_settings_json(),
            {
                {
                    "settings.dialog",
                    {
                        "Data Source",
                        "Channel Connections",
                        "Inputs",
                        "Outputs",
                    },
                },
                {
                    "settings.advanced",
                    {
                        "Gain matrix (C++ extension)",
                    },
                },
            },
            {
                {
                    "/routingMatrix/0/0",
                    "patches.diagonal",
                    "true",
                    {},
                    "all 32 diagonal input/output pairs",
                    "patchPanel.PatchPanelParameters#PatchPanelParameters / patchPanel.PatchPanelDialog#getParams",
                },
                {
                    "/routingMatrix/0/1",
                    "patches.offDiagonal",
                    "false",
                    {},
                    "all 992 off-diagonal input/output pairs",
                    "patchPanel.PatchPanelParameters#PatchPanelParameters / patchPanel.PatchPanelDialog#getParams",
                },
            },
            SettingsChangePolicy::StopRequired,
            "not-claimed",
            std::string(
                core::patch_panel_settings_schema_json()),
        },
        single_child_recipe(
            "pamguard.patch-panel.runtime",
            "patch-panel-process",
            "pamguard.patch-panel",
            "patchedAudio"),
        AvailabilityStatus::Available,
        "partial",
    };
}

} // namespace pamguard::project
