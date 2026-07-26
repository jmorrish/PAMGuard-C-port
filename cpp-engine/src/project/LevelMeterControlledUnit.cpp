#include "pamguard/project/LevelMeterControlledUnit.h"

#include "pamguard/core/LevelMeterSettings.h"

#include <optional>

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

SettingsDescriptor level_meter_display_settings() {
    return {
        1,
        {
            "levelMeter.LevelMeterSidePanel",
        },
        {
            "src/levelMeter/LevelMeterSidePanel.java",
        },
        "{}",
        {},
        {},
        SettingsChangePolicy::LiveSafe,
        "browser-validated",
        R"({
            "$schema":"https://json-schema.org/draft/2020-12/schema",
            "type":"object",
            "additionalProperties":false,
            "properties":{}
        })",
    };
}

} // namespace

ControlledUnitDescriptor
make_level_meter_controlled_unit_descriptor() {
    return {
        "pamguard.level-meter",
        1,
        {
            "Level Meter",
            "Displays",
            "levelMeter.LevelMeterControl",
            "direct",
            "Shows signal level meters",
            "displays/LevelMeters/Docs/LevelMeters.html",
            {
                "src/PamModel/PamModel.java",
                "src/levelMeter/LevelMeterControl.java",
                "src/levelMeter/LevelMeterParams.java",
                "src/levelMeter/LevelMeterDialog.java",
                "src/levelMeter/LevelMeterSidePanel.java",
            },
        },
        unlimited_in_all_modes(),
        {
            {
                "rawAudio",
                "Raw Data Source",
                DataRoleDirection::Input,
                "pamguard.raw-audio",
                RoleCardinality::ExactlyOne,
                {"sampled"},
                "PamDetection.RawDataUnit",
                "pamguard.acquisition",
            },
            {
                "levels",
                "Level measurements",
                DataRoleDirection::Output,
                "pamguard.level-measurement",
                RoleCardinality::ExactlyOne,
                {"timeseries", "monitoring"},
                {},
                std::nullopt,
            },
        },
        {
            1,
            {
                "levelMeter.LevelMeterParams",
            },
            {
                "src/levelMeter/LevelMeterControl.java",
                "src/levelMeter/LevelMeterParams.java",
                "src/levelMeter/LevelMeterDialog.java",
                "src/levelMeter/LevelMeterSidePanel.java",
            },
            core::level_meter_default_settings_json(),
            {
                {
                    "settings.dialog",
                    {
                        "Raw Data Source",
                        "Scale selection",
                        "Peak",
                        "RMS",
                        "Scale range",
                    },
                },
                {
                    "scale-reference.order",
                    {
                        "Relative to full scale",
                        "Volts",
                        "Micropascal",
                    },
                },
            },
            {
                {
                    "/minLevel",
                    "minLevel",
                    "-80",
                    {},
                    {},
                    "levelMeter.LevelMeterParams#minLevel",
                },
                {
                    "/scaleReference",
                    "scaleReference",
                    "0",
                    {},
                    "0=full scale, 1=volts, 2=micropascal",
                    "levelMeter.LevelMeterParams#scaleReference",
                },
                {
                    "/scaleType",
                    "scaleType",
                    "0",
                    {},
                    "0=peak, 1=RMS",
                    "levelMeter.LevelMeterParams#scaleType",
                },
            },
            SettingsChangePolicy::LiveSafe,
            "not-claimed",
            std::string(core::level_meter_settings_schema_json()),
        },
        {
            1,
            {
                {
                    "level-meter-process",
                    "pamguard.level-meter",
                    {
                        "",
                        "pamguard.level-meter-settings.v1",
                    },
                    true,
                    AvailabilityStatus::Available,
                    "java-source-validated",
                },
            },
            {
                {
                    "rawAudio",
                    {
                        "level-meter-process",
                        "input",
                    },
                },
                {
                    "levels",
                    {
                        "level-meter-process",
                        "levels",
                    },
                },
            },
            {},
            {"pamguard.level-meter-display"},
            "pamguard.level-meter.runtime",
        },
        AvailabilityStatus::Available,
        "partial",
    };
}

DisplayProviderDescriptor
make_level_meter_display_provider_descriptor() {
    return {
        "pamguard.level-meter-display",
        1,
        "Level Meter",
        "levelMeter.LevelMeterControl#getSidePanel",
        "levelMeter.LevelMeterSidePanel",
        "pamguard.level-meter",
        1,
        std::optional<std::size_t>{1},
        false,
        {
            {
                "levels",
                "Level measurements",
                DataRoleDirection::Input,
                "pamguard.level-measurement",
                RoleCardinality::ExactlyOne,
                {"timeseries", "monitoring"},
                "PamDetection.RawDataUnit",
                std::nullopt,
            },
        },
        level_meter_display_settings(),
        {
            "pamguard.level-meter-display",
            {
                {"levels", "levels"},
            },
        },
        AvailabilityStatus::Available,
        "browser-validated",
        {
            "src/levelMeter/LevelMeterControl.java",
            "src/levelMeter/LevelMeterSidePanel.java",
        },
    };
}

} // namespace pamguard::project
