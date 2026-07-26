#include "pamguard/project/AlarmEffortControlledUnits.h"

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

InstanceRulesDescriptor at_most_one_in_all_modes() {
    return {
        0,
        std::optional<std::size_t>{1},
        {
            RunMode::Normal,
            RunMode::Mixed,
            RunMode::Viewer,
        },
        {},
    };
}

PublicDataRoleDescriptor alarm_input() {
    return {
        "events",
        "Alarm data source",
        DataRoleDirection::Input,
        std::string(kAlarmSourceDataType),
        RoleCardinality::ExactlyOne,
        {},
        "PamguardMVC.PamDataUnit",
        std::nullopt,
    };
}

PublicDataRoleDescriptor alarm_output() {
    return {
        "alarms",
        "Alarm events",
        DataRoleDirection::Output,
        std::string(kAlarmStateDataType),
        RoleCardinality::ExactlyOne,
        {
            "events",
            "alarms",
            "history",
        },
        {},
        std::nullopt,
    };
}

PublicDataRoleDescriptor effort_output() {
    return {
        "effort",
        "Scroll Effort",
        DataRoleDirection::Output,
        std::string(kScrollEffortDataType),
        RoleCardinality::ExactlyOne,
        {
            "events",
            "effort",
            "scroll-history",
        },
        {},
        std::nullopt,
    };
}

SettingsDescriptor alarm_settings() {
    return {
        1,
        {
            "alarm.AlarmParameters",
        },
        {
            "src/alarm/AlarmControl.java",
            "src/alarm/AlarmParameters.java",
            "src/alarm/AlarmDialog.java",
            "src/alarm/AlarmProcess.java",
            "src/alarm/AlarmDataUnit.java",
            "src/alarm/AlarmCounter.java",
            "src/alarm/SimpleAlarmCounter.java",
        },
        R"({"countType":"simple","countIntervalSeconds":10,"minimumAlarmIntervalSeconds":2,"triggerCounts":[0,0],"holdSeconds":3600,"enabledActions":[]})",
        {
            {
                "menu.detection",
                {
                    "<unit name> settings...",
                    "Offline Processing ... (viewer)",
                },
            },
        },
        {},
        SettingsChangePolicy::StopRequired,
        "not-claimed",
        R"({
            "$schema":"https://json-schema.org/draft/2020-12/schema",
            "type":"object",
            "additionalProperties":false,
            "properties":{
                "countType":{
                    "type":"string",
                    "enum":["simple","scores","singles"]
                },
                "countIntervalSeconds":{
                    "type":"number",
                    "exclusiveMinimum":0
                },
                "minimumAlarmIntervalSeconds":{
                    "type":"number",
                    "minimum":0
                },
                "triggerCounts":{
                    "type":"array",
                    "minItems":2,
                    "maxItems":2,
                    "items":{"type":"number","minimum":0}
                },
                "holdSeconds":{
                    "type":"integer",
                    "minimum":0
                },
                "enabledActions":{
                    "type":"array",
                    "items":{"type":"boolean"}
                }
            },
            "required":[
                "countType",
                "countIntervalSeconds",
                "minimumAlarmIntervalSeconds",
                "triggerCounts",
                "holdSeconds",
                "enabledActions"
            ]
        })",
    };
}

SettingsDescriptor effort_settings() {
    return {
        1,
        {
            "effortmonitor.EffortParams",
        },
        {
            "src/effortmonitor/EffortControl.java",
            "src/effortmonitor/EffortParams.java",
            "src/effortmonitor/EffortDataUnit.java",
            "src/effortmonitor/EffortDataBlock.java",
            "src/effortmonitor/swing/EffortDialog.java",
            "src/effortmonitor/swing/EffortSidePanel.java",
            "src/effortmonitor/swing/EffortDisplayProvider.java",
        },
        R"({"recentObservers":[],"recentObjectives":[],"outerScrollOnly":false})",
        {
            {
                "menu.detection",
                {
                    "Record Effort",
                    "Settings...",
                },
            },
            {
                "settings.dialog",
                {
                    "Observer scroller logging",
                    "Observer name or initials",
                    "Log all scroll actions",
                    "Log outer scroll only",
                    "Objective",
                },
            },
        },
        {},
        SettingsChangePolicy::StopRequired,
        "not-claimed",
        R"({
            "$schema":"https://json-schema.org/draft/2020-12/schema",
            "type":"object",
            "additionalProperties":false,
            "properties":{
                "recentObservers":{
                    "type":"array",
                    "maxItems":10,
                    "items":{"type":"string","minLength":1}
                },
                "recentObjectives":{
                    "type":"array",
                    "maxItems":10,
                    "items":{"type":"string"}
                },
                "outerScrollOnly":{"type":"boolean"}
            },
            "required":[
                "recentObservers",
                "recentObjectives",
                "outerScrollOnly"
            ]
        })",
    };
}

} // namespace

ControlledUnitDescriptor
make_alarm_controlled_unit_descriptor() {
    return {
        std::string(kAlarmControlledUnitTypeId),
        1,
        {
            "Alarm",
            "Utilities",
            "alarm.AlarmControl",
            "direct",
            "Alerts the operator when certain detections are made",
            "utilities/Alarms/docs/Alarms_Overview.html",
            {
                "src/PamModel/PamModel.java",
                "src/alarm/AlarmControl.java",
                "src/alarm/AlarmParameters.java",
                "src/alarm/AlarmDialog.java",
                "src/alarm/AlarmProcess.java",
                "src/alarm/AlarmDataUnit.java",
                "src/alarm/AlarmDisplayTable.java",
                "src/alarm/AlarmSidePanel.java",
                "src/alarm/actions/AlarmAction.java",
            },
        },
        unlimited_in_all_modes(),
        {
            alarm_input(),
            alarm_output(),
        },
        alarm_settings(),
        {
            1,
            {
                {
                    "alarm-process",
                    std::string(kAlarmControlledUnitTypeId),
                    {
                        "",
                        "pamguard.alarm-settings.v1",
                    },
                    true,
                    AvailabilityStatus::Unavailable,
                    "click-only-event-counter-is-not-java-alarm",
                },
            },
            {
                {
                    "events",
                    {
                        "alarm-process",
                        "input",
                    },
                },
                {
                    "alarms",
                    {
                        "alarm-process",
                        "alarms",
                    },
                },
            },
            {},
            {},
            "pamguard.alarm.runtime",
        },
        AvailabilityStatus::Unavailable,
        "experimental",
    };
}

ControlledUnitDescriptor
make_effort_controlled_unit_descriptor() {
    return {
        std::string(kEffortControlledUnitTypeId),
        1,
        {
            "Scroll Effort",
            "Utilities",
            "effortmonitor.EffortControl",
            "direct",
            "Enables an observer to enter their name and information about "
            "which displays are being monitored",
            {},
            {
                "src/PamModel/PamModel.java",
                "src/effortmonitor/EffortControl.java",
                "src/effortmonitor/EffortParams.java",
                "src/effortmonitor/EffortDataUnit.java",
                "src/effortmonitor/EffortDataBlock.java",
                "src/effortmonitor/EffortLogging.java",
                "src/effortmonitor/swing/EffortDialog.java",
                "src/effortmonitor/swing/EffortSidePanel.java",
                "src/effortmonitor/swing/EffortDisplayProvider.java",
            },
        },
        at_most_one_in_all_modes(),
        {
            effort_output(),
        },
        effort_settings(),
        {
            1,
            {
                {
                    "effort-process",
                    std::string(kEffortControlledUnitTypeId),
                    {
                        "",
                        "pamguard.scroll-effort-settings.v1",
                    },
                    true,
                    AvailabilityStatus::Unavailable,
                    "dedicated-display-scroller-effort-runtime-required",
                },
            },
            {
                {
                    "effort",
                    {
                        "effort-process",
                        "events",
                    },
                },
            },
            {},
            {},
            "pamguard.effort-monitor.runtime",
        },
        AvailabilityStatus::Unavailable,
        "experimental",
    };
}

} // namespace pamguard::project
