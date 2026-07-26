#pragma once

#include <string_view>

#include "pamguard/project/ControlledUnitRegistry.h"

namespace pamguard::project {

inline constexpr std::string_view
    kAlarmControlledUnitTypeId =
        "pamguard.alarm-event-counter";
inline constexpr std::string_view
    kAlarmSourceDataType = "pamguard.data-unit";
inline constexpr std::string_view
    kAlarmStateDataType = "pamguard.alarm-state";

inline constexpr std::string_view
    kEffortControlledUnitTypeId =
        "pamguard.effort-monitor";
inline constexpr std::string_view
    kScrollEffortDataType = "pamguard.scroll-effort";

/**
 * Java-authoritative catalogue foundations for PAMGuard 2.02.18e.
 *
 * Both remain unavailable. The existing low-level alarm is a click-only
 * rolling event counter, not Java AlarmControl's selectable PamDataUnit
 * source, scoring modes, two thresholds, actions, history, and offline
 * processing. The existing low-level effort module publishes generic operator
 * annotations, not EffortControl's display-scroller ranges.
 */
[[nodiscard]] ControlledUnitDescriptor
make_alarm_controlled_unit_descriptor();

[[nodiscard]] ControlledUnitDescriptor
make_effort_controlled_unit_descriptor();

} // namespace pamguard::project
