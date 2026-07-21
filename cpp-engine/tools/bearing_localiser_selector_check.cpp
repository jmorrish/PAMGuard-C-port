#include <array>
#include <iostream>
#include <string>
#include <vector>

#include "pamguard/localisation/BearingLocaliserSelector.h"

namespace {

using pamguard::localisation::ArrayShapeType;
using pamguard::localisation::BearingLocaliserChoice;
using pamguard::localisation::select_bearing_localiser;

int failures = 0;

void expect_choice(const std::string& label, BearingLocaliserChoice actual, BearingLocaliserChoice expected) {
    if (actual != expected) {
        std::cerr << label << ": expected " << pamguard::localisation::bearing_localiser_name(expected)
                  << ", got " << pamguard::localisation::bearing_localiser_name(actual) << "\n";
        ++failures;
    }
}

} // namespace

int main() {
    // The switch in BearingLocaliserSelector.createBearingLocaliser.
    expect_choice("none shape", select_bearing_localiser(ArrayShapeType::None), BearingLocaliserChoice::None);
    expect_choice("point shape", select_bearing_localiser(ArrayShapeType::Point), BearingLocaliserChoice::None);
    expect_choice("line shape", select_bearing_localiser(ArrayShapeType::Line), BearingLocaliserChoice::Pair);
    expect_choice("plane shape", select_bearing_localiser(ArrayShapeType::Plane), BearingLocaliserChoice::Grid);
    expect_choice("volume shape", select_bearing_localiser(ArrayShapeType::Volume), BearingLocaliserChoice::Grid);

    // The same decisions reached from hydrophone positions, which is how the
    // selector is actually called — on the sub-array taking part in one
    // localisation.
    expect_choice("empty sub-array", select_bearing_localiser(std::vector<std::array<double, 3>>{}),
                  BearingLocaliserChoice::None);
    expect_choice("single hydrophone", select_bearing_localiser({{0.0, 0.0, 0.0}}), BearingLocaliserChoice::None);
    expect_choice("two hydrophones", select_bearing_localiser({{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}}),
                  BearingLocaliserChoice::Pair);

    // Co-located hydrophones on one streamer collapse to a point, so a pair of
    // them selects nothing at all rather than a degenerate pair bearing.
    expect_choice("co-located pair", select_bearing_localiser({{2.0, 3.0, 0.0}, {2.0, 3.0, 0.0}}),
                  BearingLocaliserChoice::None);

    // Four hydrophones in a line stay a line: channel count alone would have
    // chosen a grid-class localiser here, and the delays cannot support one.
    expect_choice("four in a line",
                  select_bearing_localiser({{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, {3.0, 0.0, 0.0}}),
                  BearingLocaliserChoice::Pair);

    // Three non-collinear hydrophones are a plane, which selects a grid-class
    // localiser even though the engine's LSQ substitute needs four.
    expect_choice("three-hydrophone plane",
                  select_bearing_localiser({{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}}),
                  BearingLocaliserChoice::Grid);

    expect_choice("non-coplanar tetrahedron",
                  select_bearing_localiser({{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}}),
                  BearingLocaliserChoice::Grid);

    // Streamer-scoped uniqueness carries through: two hydrophones at the same
    // position on different streamers are distinct points, so a co-located
    // pair on separate streamers is a line, not a point.
    expect_choice("co-located pair on different streamers",
                  select_bearing_localiser({{2.0, 3.0, 0.0}, {2.0, 3.0, 0.0}}, {1, 2}),
                  BearingLocaliserChoice::Pair);

    if (failures > 0) {
        std::cerr << "Bearing localiser selection failed " << failures << " case(s)\n";
        return 1;
    }
    std::cout << "Bearing localiser selection passed\n";
    return 0;
}
