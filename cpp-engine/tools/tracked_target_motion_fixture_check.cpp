#include "pamguard/localisation/TrackedTargetMotion.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using pamguard::localisation::TrackedTargetMotionFilterInput;
using pamguard::localisation::TrackedTargetMotionLeastSquares;
using pamguard::localisation::TrackedTargetMotionLimits;
using pamguard::localisation::TrackedTargetMotionObservation;
using pamguard::localisation::TrackedTargetMotionResult;
using pamguard::localisation::TrackedTargetMotionStatus;

constexpr const char* expected_metadata =
        "# oracleVersion=2.02.18e,"
        "oracleCommit=dca55c81ef6f1498a8a3b926c69e7182afb915ee";
constexpr const char* expected_header =
        "case,ambiguityGroup,ambiguitySide,observationIndex,selected,"
        "maxLocalisationPoints,originXMetres,originYMetres,originZMetres,"
        "headingRadians,bearingRadians,javaSuccess,javaStatus,"
        "referenceObservationIndex,resultXMetres,resultYMetres,"
        "resultZMetres,rawChi2,reducedChi2,aic,perpendicularErrorMetres,"
        "parallelErrorMetres,errorAngleRadians,perpendicularDistanceMetres,"
        "heightMetres,maxRangeMetres,minHeightMetres,maxHeightMetres,"
        "passesRunawayGuard,passesConfiguredLimits,acceptedByJavaFilters";

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    for (const char character : line) {
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

bool boolean(const std::string& text) {
    require(text == "true" || text == "false", "Invalid fixture boolean");
    return text == "true";
}

struct FixtureCase {
    std::string name;
    std::string ambiguity_group;
    int ambiguity_side = 0;
    std::int32_t max_points = 0;
    std::vector<TrackedTargetMotionObservation> observations;
    std::vector<std::size_t> selected_indices;
    bool java_success = false;
    std::string java_status;
    std::int64_t reference_index = -1;
    std::array<double, 3> expected_position{};
    double raw_chi2 = 0.0;
    double reduced_chi2 = 0.0;
    double aic = 0.0;
    double perpendicular_error = 0.0;
    double parallel_error = 0.0;
    double error_angle = 0.0;
    TrackedTargetMotionFilterInput filter_input;
    TrackedTargetMotionLimits limits;
    bool passes_runaway = false;
    bool passes_configured = false;
    bool accepted = false;
};

bool equivalent_double(
        const double actual,
        const double expected,
        const double relative_tolerance = 2.0e-11) {
    if (std::isnan(expected)) {
        return std::isnan(actual);
    }
    if (std::isinf(expected)) {
        return std::isinf(actual)
                && std::signbit(actual) == std::signbit(expected);
    }
    const double scale = std::max(1.0, std::abs(expected));
    return std::abs(actual - expected) <= relative_tolerance * scale;
}

void require_double(
        const double actual,
        const double expected,
        const std::string& label,
        const std::string& case_name) {
    require(
            equivalent_double(actual, expected),
            case_name + " " + label + " mismatch: expected "
                    + std::to_string(expected) + ", got "
                    + std::to_string(actual));
}

std::vector<FixtureCase> read_fixture(const std::string& path) {
    std::ifstream input(path);
    require(
            static_cast<bool>(input),
            "Could not open tracked target-motion fixture: " + path);
    std::string metadata;
    std::string header;
    std::getline(input, metadata);
    std::getline(input, header);
    require(metadata == expected_metadata, "Oracle metadata changed");
    require(header == expected_header, "Fixture schema changed");

    std::vector<FixtureCase> cases;
    std::map<std::string, std::size_t> by_name;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        const auto fields = split_csv(line);
        require(fields.size() == 31, "Fixture row does not have 31 fields");
        auto found = by_name.find(fields[0]);
        if (found == by_name.end()) {
            FixtureCase fixture_case;
            fixture_case.name = fields[0];
            fixture_case.ambiguity_group = fields[1];
            fixture_case.ambiguity_side = std::stoi(fields[2]);
            fixture_case.max_points =
                    static_cast<std::int32_t>(std::stoll(fields[5]));
            fixture_case.java_success = boolean(fields[11]);
            fixture_case.java_status = fields[12];
            fixture_case.reference_index = std::stoll(fields[13]);
            fixture_case.expected_position = {
                    std::stod(fields[14]),
                    std::stod(fields[15]),
                    std::stod(fields[16])};
            fixture_case.raw_chi2 = std::stod(fields[17]);
            fixture_case.reduced_chi2 = std::stod(fields[18]);
            fixture_case.aic = std::stod(fields[19]);
            fixture_case.perpendicular_error = std::stod(fields[20]);
            fixture_case.parallel_error = std::stod(fields[21]);
            fixture_case.error_angle = std::stod(fields[22]);
            fixture_case.filter_input.perpendicular_distance_metres =
                    std::stod(fields[23]);
            fixture_case.filter_input.height_metres =
                    std::stod(fields[24]);
            fixture_case.limits.max_range_metres = std::stod(fields[25]);
            fixture_case.limits.min_height_metres = std::stod(fields[26]);
            fixture_case.limits.max_height_metres = std::stod(fields[27]);
            fixture_case.passes_runaway = boolean(fields[28]);
            fixture_case.passes_configured = boolean(fields[29]);
            fixture_case.accepted = boolean(fields[30]);
            cases.push_back(std::move(fixture_case));
            const auto index = cases.size() - 1;
            by_name.emplace(fields[0], index);
            found = by_name.find(fields[0]);
        }
        auto& fixture_case = cases[found->second];
        const auto observation_index =
                static_cast<std::size_t>(std::stoull(fields[3]));
        require(
                observation_index == fixture_case.observations.size(),
                fixture_case.name + " observation ordering changed");
        fixture_case.observations.push_back({
                {std::stod(fields[6]),
                 std::stod(fields[7]),
                 std::stod(fields[8])},
                std::stod(fields[9]),
                std::stod(fields[10])});
        if (boolean(fields[4])) {
            fixture_case.selected_indices.push_back(observation_index);
        }

        require(
                fixture_case.ambiguity_group == fields[1]
                        && fixture_case.ambiguity_side
                                == std::stoi(fields[2])
                        && fixture_case.max_points
                                == std::stoll(fields[5])
                        && fixture_case.java_success
                                == boolean(fields[11])
                        && fixture_case.java_status == fields[12],
                fixture_case.name + " repeated metadata differs");
    }
    return cases;
}

const FixtureCase& named(
        const std::vector<FixtureCase>& cases,
        const std::string& name) {
    const auto found = std::find_if(
            cases.begin(),
            cases.end(),
            [&name](const FixtureCase& fixture_case) {
                return fixture_case.name == name;
            });
    require(found != cases.end(), "Fixture case missing: " + name);
    return *found;
}

void check_result(
        const FixtureCase& fixture_case,
        const TrackedTargetMotionResult& result) {
    require(
            result.selected_observation_indices
                    == fixture_case.selected_indices,
            fixture_case.name
                    + " TMGroupLocInfo point selection mismatch");
    require(
            result.succeeded() == fixture_case.java_success,
            fixture_case.name + " Java success/failure mismatch");
    if (fixture_case.reference_index >= 0) {
        require(
                result.reference_observation_index
                        == static_cast<std::size_t>(
                                fixture_case.reference_index),
                fixture_case.name + " reference observation mismatch");
    }
    if (fixture_case.java_success) {
        for (std::size_t dimension = 0; dimension < 3; ++dimension) {
            require_double(
                    result.position_metres[dimension],
                    fixture_case.expected_position[dimension],
                    "position[" + std::to_string(dimension) + "]",
                    fixture_case.name);
        }
        require_double(
                result.raw_chi2,
                fixture_case.raw_chi2,
                "raw chi2",
                fixture_case.name);
        require_double(
                result.reduced_chi2,
                fixture_case.reduced_chi2,
                "reduced chi2",
                fixture_case.name);
        require_double(
                result.aic,
                fixture_case.aic,
                "AIC",
                fixture_case.name);
        require_double(
                result.perpendicular_error_metres,
                fixture_case.perpendicular_error,
                "perpendicular error",
                fixture_case.name);
        require_double(
                result.parallel_error_metres,
                fixture_case.parallel_error,
                "parallel error",
                fixture_case.name);
        require_double(
                result.error_angle_radians,
                fixture_case.error_angle,
                "error angle",
                fixture_case.name);
    }

    const auto filter = TrackedTargetMotionLeastSquares::assess_filters(
            result.succeeded(),
            fixture_case.filter_input,
            fixture_case.limits);
    require(
            filter.passes_runaway_guard
                            == fixture_case.passes_runaway
                    && filter.passes_configured_limits
                            == fixture_case.passes_configured
                    && filter.accepted == fixture_case.accepted,
            fixture_case.name + " Java post-fit filter mismatch");
}

void check_defensive_boundaries() {
    const std::vector<TrackedTargetMotionObservation> one{
            {{{0.0, 0.0, 0.0}}, 0.0, 1.0}};
    const auto negative_limit =
            TrackedTargetMotionLeastSquares::localise_side(one, -1);
    require(
            negative_limit.status
                    == TrackedTargetMotionStatus::invalid_point_limit,
            "Negative portable point limit was not rejected");

    auto non_finite = one;
    non_finite[0].bearing_radians =
            std::numeric_limits<double>::quiet_NaN();
    const auto invalid_input =
            TrackedTargetMotionLeastSquares::localise_side(non_finite);
    require(
            invalid_input.status
                    == TrackedTargetMotionStatus::non_finite_input,
            "Non-finite portable bearing was not rejected at the adapter "
            "boundary");

    const auto nan_filter =
            TrackedTargetMotionLeastSquares::assess_filters(
                    true,
                    {std::numeric_limits<double>::quiet_NaN(), 0.0});
    require(
            nan_filter.passes_runaway_guard
                    && !nan_filter.passes_configured_limits
                    && !nan_filter.accepted,
            "NaN filter comparisons do not retain Java ordering");
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr
                << "Usage: tracked_target_motion_fixture_check "
                   "<tracked-target-motion.csv>\n";
        return 2;
    }

    try {
        const auto cases = read_fixture(argv[1]);
        require(cases.size() == 13, "Fixture case catalogue changed");
        for (const auto& fixture_case : cases) {
            const auto result =
                    TrackedTargetMotionLeastSquares::localise_side(
                            fixture_case.observations,
                            fixture_case.max_points);
            check_result(fixture_case, result);
        }

        require(
                named(cases, "divergent-bearing-failure")
                                .java_success
                        == false
                        && named(cases, "single-bearing-failure")
                                   .java_success
                                == false
                        && named(cases, "one-point-limit-failure")
                                   .java_success
                                == false
                        && named(cases, "zero-point-limit-failure")
                                   .java_success
                                == false,
                "Authority-backed failure cases disappeared");
        require(
                named(cases, "parallel-nonfinite-java-success")
                        .java_success,
                "Pinned Java's parallel-bearing non-finite success changed");

        const auto ambiguity_results =
                TrackedTargetMotionLeastSquares::localise_ambiguities({
                        named(cases, "straight-starboard").observations,
                        named(cases, "straight-port").observations});
        require(
                ambiguity_results.size() == 2
                        && ambiguity_results[0].succeeded()
                        && ambiguity_results[1].succeeded()
                        && ambiguity_results[0].position_metres[0] > 0.0
                        && ambiguity_results[1].position_metres[0] < 0.0,
                "Both tracked-click ambiguity sides were not retained");

        check_defensive_boundaries();
        std::cout
                << "Tracked-click target-motion Least Squares matched "
                   "pinned PAMGuard for "
                << cases.size()
                << " moving-origin, heading/bearing, ambiguity, point-limit, "
                   "post-filter, error/chi2, and failure cases.\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
