#include <algorithm>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <json.hpp>

#include "pamguard/project/AlarmEffortControlledUnits.h"

namespace {

using Json = nlohmann::json;
using pamguard::project::AvailabilityStatus;
using pamguard::project::ControlledUnitDescriptor;
using pamguard::project::ControlledUnitRegistry;
using pamguard::project::DataRoleDirection;
using pamguard::project::LowLevelPortContract;
using pamguard::project::LowLevelTypeContract;
using pamguard::project::RunMode;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void check_unavailable_recipe(
    const ControlledUnitDescriptor& descriptor,
    const std::string& runtime_type_id) {
    require(
        descriptor.descriptor_version == 1 &&
            descriptor.runtime_recipe.version == 1 &&
            descriptor.runtime_recipe.children.size() == 1 &&
            descriptor.runtime_recipe.children.front().runtime_type_id ==
                runtime_type_id &&
            descriptor.runtime_recipe.children.front().availability ==
                AvailabilityStatus::Unavailable &&
            descriptor.availability ==
                AvailabilityStatus::Unavailable &&
            descriptor.parity_status == "experimental",
        descriptor.id +
            " must remain an unavailable experimental foundation");
}

void check_alarm() {
    const auto descriptor =
        pamguard::project::make_alarm_controlled_unit_descriptor();
    require(
        descriptor.id == "pamguard.alarm-event-counter" &&
            descriptor.java_authority.registered_name == "Alarm" &&
            descriptor.java_authority.menu_group == "Utilities" &&
            descriptor.java_authority.class_name ==
                "alarm.AlarmControl" &&
            descriptor.java_authority.tooltip ==
                "Alerts the operator when certain detections are made" &&
            descriptor.java_authority.help_point ==
                "utilities/Alarms/docs/Alarms_Overview.html",
        "Alarm pinned Java registration identity changed");
    require(
        descriptor.instance_rules.minimum_instances == 0 &&
            !descriptor.instance_rules.maximum_instances &&
            descriptor.instance_rules.allowed_modes ==
                std::vector<RunMode>{
                    RunMode::Normal,
                    RunMode::Mixed,
                    RunMode::Viewer,
                },
        "Alarm Java instance rules changed");
    require(
        descriptor.public_roles.size() == 2 &&
            descriptor.public_roles.at(0).id == "events" &&
            descriptor.public_roles.at(0).direction ==
                DataRoleDirection::Input &&
            descriptor.public_roles.at(0).data_type ==
                "pamguard.data-unit" &&
            descriptor.public_roles.at(1).id == "alarms" &&
            descriptor.public_roles.at(1).data_type ==
                "pamguard.alarm-state",
        "Alarm must describe Java's selectable PamDataUnit source and "
        "alarm-event output");
    require(
        Json::parse(descriptor.settings.default_settings_json) ==
            Json{
                {"countType", "simple"},
                {"countIntervalSeconds", 10},
                {"minimumAlarmIntervalSeconds", 2},
                {"triggerCounts", {0, 0}},
                {"holdSeconds", 3600},
                {"enabledActions", Json::array()},
            } &&
            descriptor.settings.parity_status == "not-claimed",
        "Alarm experimental settings boundary changed");
    check_unavailable_recipe(
        descriptor,
        "pamguard.alarm-event-counter");

    auto available = descriptor;
    available.availability = AvailabilityStatus::Available;
    available.runtime_recipe.children.front().availability =
        AvailabilityStatus::Available;
    ControlledUnitRegistry registry;
    registry.register_controlled_unit(std::move(available));
    const std::vector<LowLevelTypeContract> click_only_runtime{
        {
            "pamguard.alarm-event-counter",
            {
                {
                    "input",
                    DataRoleDirection::Input,
                    "pamguard.click",
                    {"detections"},
                },
                {
                    "alarms",
                    DataRoleDirection::Output,
                    "pamguard.alarm-state",
                    {"events", "monitoring"},
                },
            },
        },
    };
    const auto mismatch =
        registry.validate_against(click_only_runtime);
    require(
        std::any_of(
            mismatch.issues.begin(),
            mismatch.issues.end(),
            [](const auto& issue) {
                return issue.code ==
                    "low-level-data-type-mismatch";
            }),
        "Click-only event counter was accepted as Java Alarm runtime");
}

void check_effort() {
    const auto descriptor =
        pamguard::project::make_effort_controlled_unit_descriptor();
    require(
        descriptor.id == "pamguard.effort-monitor" &&
            descriptor.java_authority.registered_name ==
                "Scroll Effort" &&
            descriptor.java_authority.menu_group == "Utilities" &&
            descriptor.java_authority.class_name ==
                "effortmonitor.EffortControl" &&
            descriptor.java_authority.tooltip ==
                "Enables an observer to enter their name and information "
                "about which displays are being monitored",
        "Scroll Effort pinned Java registration identity changed");
    require(
        descriptor.instance_rules.minimum_instances == 0 &&
            descriptor.instance_rules.maximum_instances ==
                std::optional<std::size_t>{1} &&
            descriptor.public_roles.size() == 1 &&
            descriptor.public_roles.front().id == "effort" &&
            descriptor.public_roles.front().direction ==
                DataRoleDirection::Output &&
            descriptor.public_roles.front().data_type ==
                "pamguard.scroll-effort" &&
            descriptor.public_roles.front().data_type !=
                "pamguard.operator-event",
        "Scroll Effort must retain its dedicated scroller-range data");
    require(
        Json::parse(descriptor.settings.default_settings_json) ==
            Json{
                {"recentObservers", Json::array()},
                {"recentObjectives", Json::array()},
                {"outerScrollOnly", false},
            } &&
            descriptor.settings.parity_status == "not-claimed",
        "Scroll Effort experimental settings boundary changed");
    check_unavailable_recipe(
        descriptor,
        "pamguard.effort-monitor");

    auto available = descriptor;
    available.availability = AvailabilityStatus::Available;
    available.runtime_recipe.children.front().availability =
        AvailabilityStatus::Available;
    ControlledUnitRegistry registry;
    registry.register_controlled_unit(std::move(available));
    const std::vector<LowLevelTypeContract> generic_logger_runtime{
        {
            "pamguard.effort-monitor",
            {
                {
                    "events",
                    DataRoleDirection::Output,
                    "pamguard.operator-event",
                    {"events", "annotations"},
                },
            },
        },
    };
    const auto mismatch =
        registry.validate_against(generic_logger_runtime);
    require(
        std::any_of(
            mismatch.issues.begin(),
            mismatch.issues.end(),
            [](const auto& issue) {
                return issue.code ==
                    "low-level-data-type-mismatch";
            }),
        "Generic annotation logger was accepted as Scroll Effort runtime");
}

} // namespace

int main() {
    try {
        check_alarm();
        check_effort();
        std::cout
            << "Alarm and Scroll Effort unavailable experimental "
               "controlled-unit boundaries passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr
            << "Alarm/Effort controlled-unit check failed: "
            << error.what() << '\n';
        return 1;
    }
}
