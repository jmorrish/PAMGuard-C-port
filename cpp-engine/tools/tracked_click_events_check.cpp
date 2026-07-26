#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <json.hpp>

#include "pamguard/project/BuiltInControlledUnits.h"
#include "pamguard/project/ControlledUnitRegistry.h"
#include "pamguard/service/TrackedClickEvents.h"

namespace {

using pamguard::service::TrackedClickEventStore;
using pamguard::service::TrackedClickLocalisationRunStatus;
using pamguard::service::TrackedClickLocalisationStatus;
using pamguard::service::TrackedClickLocaliserSettings;
using pamguard::service::TrackedClickNavigationSample;
using pamguard::service::TrackedClickObservation;
using pamguard::localisation::TrackedTargetMotionStatus;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

TrackedClickObservation click(
    const std::uint64_t uid,
    const std::int64_t sample,
    std::optional<double> bearing = std::nullopt) {
    return {
        uid,
        sample,
        1'700'000'000'000 + sample,
        3,
        bearing,
    };
}

TrackedClickObservation moving_click(
    const std::uint64_t uid,
    const std::int64_t sample,
    const double north_metres,
    const double height_metres,
    const double starboard_bearing_radians,
    const double port_bearing_radians) {
    TrackedClickObservation result =
        click(uid, sample, starboard_bearing_radians);
    result.origin_metres =
        std::array<double, 3>{
            0.0,
            north_metres,
            height_metres,
        };
    result.heading_radians = 2.4492935982947064e-16;
    result.earth_bearing_ambiguities_radians = {
        starboard_bearing_radians,
        port_bearing_radians,
    };
    result.navigation_reference_id = "fixture-navigation";
    return result;
}

std::vector<TrackedClickObservation> paired_moving_clicks() {
    return {
        moving_click(
            1,
            100,
            -240.0,
            -8.0,
            1.1902899496825317,
            -1.1902899496825317),
        moving_click(
            2,
            200,
            -120.0,
            -8.5,
            1.3352513460740334,
            -1.3352513460740334),
        moving_click(
            3,
            300,
            0.0,
            -9.0,
            1.4909663410826595,
            -1.4909663410826597),
        moving_click(
            4,
            400,
            120.0,
            -9.5,
            1.6506263125071340,
            4.6325589946724520),
        moving_click(
            5,
            500,
            240.0,
            -10.0,
            1.8063413075157600,
            4.4768439996638260),
    };
}

std::vector<TrackedClickNavigationSample> paired_navigation_track() {
    return {
        {10, {700.0, 60.0, 0.0}},
        {20, {-700.0, 60.0, 0.0}},
        {30, {5000.0, 5000.0, 0.0}},
    };
}

bool close(
    const double actual,
    const double expected,
    const double tolerance = 1.0e-9) {
    return std::abs(actual - expected) <=
        tolerance * std::max(1.0, std::abs(expected));
}

std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    for (const auto character : line) {
        if (character == ',') {
            fields.push_back(std::move(field));
            field.clear();
        }
        else {
            field.push_back(character);
        }
    }
    fields.push_back(std::move(field));
    return fields;
}

void check_click_loc_params_fixture(const std::string& path) {
    std::ifstream input(path);
    require(
        static_cast<bool>(input),
        "Could not open ClickLocParams Java fixture: " + path);
    std::string header;
    std::getline(input, header);
    require(
        header ==
            "stage,isSelected,maxRange,maxHeight,minHeight,maxTime,"
            "limitLocPoints,maxLocPoints,maxLocalisationPoints",
        "ClickLocParams Java fixture header changed");
    std::vector<std::vector<std::string>> rows;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty()) {
            rows.push_back(split_csv(line));
        }
    }
    require(
        rows.size() == 3 &&
            rows[0].size() == 9 &&
            rows[0][0] == "default" &&
            rows[0][1] == "true;false;false;false" &&
            std::stod(rows[0][2]) == 20000.0 &&
            std::stod(rows[0][3]) == 5.0 &&
            std::stod(rows[0][4]) == -5000.0 &&
            std::stoull(rows[0][5]) == 200 &&
            rows[0][6] == "false" &&
            std::stoull(rows[0][7]) == 30 &&
            std::stoull(rows[0][8]) ==
                static_cast<std::uint64_t>(INT32_MAX) &&
            rows[1][1] == "true;false;true;false" &&
            rows[2][6] == "true" &&
            std::stoull(rows[2][7]) == 17 &&
            std::stoull(rows[2][8]) == 17,
        "ClickLocParams defaults/lazy selections differ from pinned Java");

    pamguard::project::ControlledUnitRegistry registry;
    pamguard::project::register_builtin_controlled_units(registry);
    const auto* descriptor =
        registry.find_controlled_unit("pamguard.click-detector");
    require(
        descriptor != nullptr,
        "Click Detector controlled-unit descriptor is absent");
    const auto settings =
        nlohmann::json::parse(
            descriptor->settings.default_settings_json)
            .at("localisation")
            .at("trackedTrain");
    require(
        settings.at("isSelected") ==
                nlohmann::json::array(
                    {true, false, false, false}) &&
            settings.at("maxRangeM") == 20000.0 &&
            settings.at("maxHeightM") == 5.0 &&
            settings.at("minHeightM") == -5000.0 &&
            settings.at("maxTimeMilliseconds") == 200 &&
            settings.at("limitPoints") == false &&
            settings.at("maxPoints") == 30,
        "Portable trackedTrain defaults differ from ClickLocParams fixture");
}

void check_java_membership_semantics() {
    TrackedClickEventStore store;
    store.reconcile({
        "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
        "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb",
        7,
    });

    const auto first = store.assign(
        {click(2, 200), click(1, 100)},
        std::nullopt);
    require(
        first.event_id == 1 &&
            first.comment == "Manual Click Train Detection" &&
            first.clicks.size() == 2 &&
            first.clicks[0].uid == 1 &&
            first.clicks[1].uid == 2,
        "New tracked event does not match TrackedClickLocaliser grouping");

    const auto unchanged = store.assign(
        {click(1, 100)},
        1);
    require(
        unchanged.clicks.size() == 2,
        "Assigning a click to its current event was not idempotent");

    const auto second = store.assign(
        {click(3, 300)},
        std::nullopt);
    require(
        second.event_id == 2 &&
            store.event_for_click(3) == 2,
        "New tracked event did not use max event ID plus one");

    const auto moved = store.assign(
        {click(1, 100)},
        2);
    require(
        moved.clicks.size() == 2 &&
            store.event_for_click(1) == 2 &&
            store.events().size() == 2,
        "Click reassignment did not update unique event membership");
    require(
        store.remove_click(2) &&
            store.events().size() == 1 &&
            !store.event_for_click(2),
        "Removing the last click did not delete its empty event");

    const auto third = store.assign(
        {click(4, 400)},
        std::nullopt);
    require(
        third.event_id == 3,
        "Event ID allocation did not retain Java max-plus-one behaviour");
    const auto combined = store.reassign_event(2, 3);
    require(
        combined.event_id == 3 &&
            combined.clicks.size() == 3 &&
            store.events().size() == 1 &&
            store.event_for_click(1) == 3 &&
            store.event_for_click(3) == 3,
        "Whole-event reassignment did not mirror ClickControl");
    require(
        store.delete_event(3) &&
            store.events().empty() &&
            !store.event_for_click(4),
        "Deleting a tracked event left child relationships behind");
}

void check_localisation_prerequisite_reporting() {
    TrackedClickEventStore store;
    store.reconcile({
        "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
        "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb",
        11,
    });
    const auto event =
        store.assign({click(1, 100)}, std::nullopt);
    TrackedClickLocaliserSettings settings;
    auto assessment = store.assess_localisation(
        event.event_id,
        settings,
        "normal");
    require(
        assessment.status ==
            TrackedClickLocalisationStatus::NotEnoughClicks,
        "One-click event did not retain Java's two-click threshold");

    (void)store.assign({click(2, 200)}, event.event_id);
    assessment = store.assess_localisation(
        event.event_id,
        settings,
        "normal");
    require(
        assessment.status ==
                TrackedClickLocalisationStatus::MissingBearing &&
            !assessment.algorithms[0].available,
        "Missing ordered earth-frame ambiguities were not reported");

    auto bearing_only_1 = click(1, 100, 0.5);
    bearing_only_1.earth_bearing_ambiguities_radians = {0.5, -0.5};
    auto bearing_only_2 = click(2, 200, 0.6);
    bearing_only_2.earth_bearing_ambiguities_radians = {0.6, -0.6};
    (void)store.assign(
        {bearing_only_1, bearing_only_2},
        event.event_id);
    assessment = store.assess_localisation(
        event.event_id,
        settings,
        "normal");
    require(
        assessment.status ==
                TrackedClickLocalisationStatus::
                    MovingArrayOriginUnavailable &&
            assessment.algorithms.size() == 4 &&
            assessment.algorithms[0].java_name == "Least Squares" &&
            !assessment.algorithms[0].available &&
            assessment.algorithms[3].java_name == "MCMC",
        "Least Squares did not expose its missing per-click origin/heading");

    bearing_only_1.origin_metres =
        std::array<double, 3>{0.0, 0.0, -5.0};
    bearing_only_1.heading_radians = 0.0;
    bearing_only_1.navigation_reference_id =
        "fixture-navigation";
    bearing_only_2.origin_metres =
        std::array<double, 3>{0.0, 100.0, -5.0};
    bearing_only_2.heading_radians = 0.0;
    bearing_only_2.navigation_reference_id =
        "fixture-navigation";
    bearing_only_2.earth_bearing_ambiguities_radians = {0.6};
    (void)store.assign(
        {bearing_only_1, bearing_only_2},
        event.event_id);
    assessment = store.assess_localisation(
        event.event_id,
        settings,
        "normal");
    require(
        assessment.status ==
            TrackedClickLocalisationStatus::MissingBearing,
        "Unequal ambiguity cardinality was accepted");

    settings.is_selected = {false, false, false, false};
    assessment = store.assess_localisation(
        event.event_id,
        settings,
        "normal");
    require(
        assessment.status ==
            TrackedClickLocalisationStatus::NoAlgorithmSelected,
        "Empty ClickLocParams selection was not preserved");

    settings.is_selected = {false, true, false, false};
    assessment = store.assess_localisation(
        event.event_id,
        settings,
        "normal");
    require(
        assessment.status ==
                TrackedClickLocalisationStatus::
                    SelectedAlgorithmUnavailable &&
            assessment.code == "selected_algorithm_unimplemented" &&
            assessment.algorithms[1].selected &&
            !assessment.algorithms[1].available,
        "Selected 2D Simplex did not report an explicit implementation "
        "boundary");

    settings.is_selected = {false, false, false, true};
    assessment = store.assess_localisation(
        event.event_id,
        settings,
        "normal");
    require(
        assessment.status ==
            TrackedClickLocalisationStatus::RunModeUnavailable,
        "MCMC was not restricted to Java Viewer mode");
    assessment = store.assess_localisation(
        event.event_id,
        settings,
        "viewer");
    require(
        assessment.status ==
            TrackedClickLocalisationStatus::
                TimeDelayOriginUnavailable,
        "Viewer MCMC did not report its missing time-delay/origin geometry");
}

void check_least_squares_run_and_filters() {
    TrackedClickEventStore store;
    store.reconcile({
        "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
        "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb",
        12,
    });
    const auto event =
        store.assign(paired_moving_clicks(), std::nullopt);
    TrackedClickLocaliserSettings settings;

    const auto assessment = store.assess_localisation(
        event.event_id,
        settings,
        "normal");
    require(
        assessment.status ==
                TrackedClickLocalisationStatus::Available &&
            assessment.algorithms.size() == 4 &&
            assessment.algorithms[0].selected &&
            assessment.algorithms[0].available &&
            !assessment.algorithms[1].available &&
            !assessment.algorithms[2].available &&
            !assessment.algorithms[3].available,
        "Complete moving-origin inputs did not make only Least Squares "
        "available");

    const auto missing_navigation = store.run_localisation(
        event.event_id,
        settings,
        "normal",
        {});
    require(
        missing_navigation.status ==
                TrackedClickLocalisationRunStatus::
                    NavigationTrackUnavailable &&
            missing_navigation.assessment.available() &&
            !missing_navigation.executed() &&
            missing_navigation.ambiguities.empty(),
        "A complete event silently invented a navigation-track filter input");

    const auto run = store.run_localisation(
        event.event_id,
        settings,
        "normal",
        paired_navigation_track());
    require(
        run.status ==
                TrackedClickLocalisationRunStatus::Executed &&
            run.executed() &&
            run.accepted() &&
            run.ambiguities.size() == 2,
        "Two-side Least Squares did not execute and pass Java filters");
    require(
        run.ambiguities[0].ambiguity_index == 0 &&
            run.ambiguities[1].ambiguity_index == 1 &&
            run.ambiguities[0].fit.status ==
                TrackedTargetMotionStatus::success &&
            run.ambiguities[1].fit.status ==
                TrackedTargetMotionStatus::success &&
            close(
                run.ambiguities[0].fit.position_metres[0],
                750.0) &&
            close(
                run.ambiguities[1].fit.position_metres[0],
                -750.0),
        "Least Squares changed the ordered PAMGuard ambiguity sides or "
        "solver results");
    require(
        run.ambiguities[0].beam_sample_time_ms == 10 &&
            run.ambiguities[1].beam_sample_time_ms == 20 &&
            run.ambiguities[0].beam_distance_metres &&
            run.ambiguities[1].beam_distance_metres &&
            close(*run.ambiguities[0].beam_distance_metres, 50.0) &&
            close(*run.ambiguities[1].beam_distance_metres, 50.0) &&
            run.ambiguities[0].filter_input &&
            close(
                run.ambiguities[0].filter_input->height_metres,
                -9.0) &&
            run.ambiguities[0].filter_assessment &&
            run.ambiguities[0].filter_assessment->accepted &&
            run.ambiguities[1].filter_assessment &&
            run.ambiguities[1].filter_assessment->accepted,
        "Nearest navigation sample, beam distance/time, height, or Java "
        "post-fit acceptance was not retained");

    settings.max_range_m = 49.0;
    const auto rejected = store.run_localisation(
        event.event_id,
        settings,
        "normal",
        paired_navigation_track());
    require(
        rejected.executed() &&
            !rejected.accepted() &&
            rejected.ambiguities.size() == 2 &&
            rejected.ambiguities[0].filter_assessment &&
            !rejected.ambiguities[0]
                 .filter_assessment->passes_configured_limits &&
            rejected.ambiguities[1].filter_assessment &&
            !rejected.ambiguities[1]
                 .filter_assessment->passes_configured_limits,
        "Configured max-range rejection did not use closest-track distance");
}

void check_point_limits_and_algorithm_availability() {
    TrackedClickEventStore store;
    store.reconcile({
        "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
        "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb",
        13,
    });
    const auto event =
        store.assign(paired_moving_clicks(), std::nullopt);
    TrackedClickLocaliserSettings settings;
    settings.limit_points = true;
    settings.max_points = 3;
    auto run = store.run_localisation(
        event.event_id,
        settings,
        "normal",
        paired_navigation_track());
    const std::vector<std::size_t> expected_indices{0, 2, 4};
    require(
        run.executed() &&
            run.ambiguities.size() == 2 &&
            run.ambiguities[0].fit.selected_observation_indices ==
                expected_indices &&
            run.ambiguities[1].fit.selected_observation_indices ==
                expected_indices,
        "ClickLocParams point limit did not preserve Java endpoint "
        "selection for every ambiguity");

    settings.limit_points = false;
    settings.max_points = 1;
    run = store.run_localisation(
        event.event_id,
        settings,
        "normal",
        paired_navigation_track());
    require(
        run.ambiguities[0].fit.selected_observation_indices.size() == 5,
        "Disabled point limiting did not map to Java's unlimited value");

    settings.limit_points = true;
    settings.max_points = 0;
    run = store.run_localisation(
        event.event_id,
        settings,
        "normal",
        paired_navigation_track());
    require(
        run.executed() &&
            !run.accepted() &&
            run.ambiguities[0].fit.status ==
                TrackedTargetMotionStatus::no_observations &&
            run.ambiguities[1].fit.status ==
                TrackedTargetMotionStatus::no_observations,
        "A zero-point pure-solver failure was promoted to success");

    settings = {};
    settings.is_selected = {true, true, true, true};
    const auto mixed = store.assess_localisation(
        event.event_id,
        settings,
        "normal");
    require(
        mixed.available() &&
            mixed.algorithms[0].available &&
            mixed.algorithms[0].selected &&
            mixed.algorithms[1].selected &&
            !mixed.algorithms[1].available &&
            mixed.algorithms[2].selected &&
            !mixed.algorithms[2].available &&
            mixed.algorithms[3].selected &&
            !mixed.algorithms[3].available,
        "An unavailable selected algorithm blocked available Least Squares "
        "or per-algorithm availability was lost");
    run = store.run_localisation(
        event.event_id,
        settings,
        "normal",
        paired_navigation_track());
    require(
        run.executed() && run.ambiguities.size() == 2,
        "Mixed selections did not execute the available Least Squares path");
}

void check_runtime_scope_reset() {
    TrackedClickEventStore store;
    store.reconcile({
        "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
        "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb",
        1,
    });
    (void)store.assign({click(1, 100)}, std::nullopt);
    store.reconcile({
        "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
        "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb",
        2,
    });
    require(
        store.events().empty(),
        "Tracked events survived replacement of their retained-click "
        "runtime revision");
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr
            << "Usage: tracked_click_events_check "
               "<tracked-click-params.csv>\n";
        return 2;
    }
    try {
        check_click_loc_params_fixture(argv[1]);
        check_java_membership_semantics();
        check_localisation_prerequisite_reporting();
        check_least_squares_run_and_filters();
        check_point_limits_and_algorithm_availability();
        check_runtime_scope_reset();
        std::cout
            << "TrackedClickLocaliser manual membership, reassignment, "
               "empty-event deletion, ClickLocParams algorithm ordering, "
               "moving-origin two-side Least Squares, closest-track filters, "
               "point limiting, scientific prerequisite reporting, and "
               "runtime-scope reset matched pinned PAMGuard semantics.\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
