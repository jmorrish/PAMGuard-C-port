#include "pamguard/localisation/BearingLocaliserSelector.h"

namespace pamguard::localisation {

BearingLocaliserChoice select_bearing_localiser(ArrayShapeType shape) {
    switch (shape) {
    case ArrayShapeType::None:
    case ArrayShapeType::Point:
        return BearingLocaliserChoice::None;
    case ArrayShapeType::Line:
        return BearingLocaliserChoice::Pair;
    case ArrayShapeType::Plane:
    case ArrayShapeType::Volume:
        return BearingLocaliserChoice::Grid;
    }
    return BearingLocaliserChoice::None;
}

BearingLocaliserChoice select_bearing_localiser(const std::vector<std::array<double, 3>>& hydrophone_positions_m,
                                                const std::vector<int>& streamer_ids) {
    return select_bearing_localiser(array_shape(hydrophone_positions_m, streamer_ids));
}

std::string_view bearing_localiser_name(BearingLocaliserChoice choice) {
    switch (choice) {
    case BearingLocaliserChoice::None:
        return "none";
    case BearingLocaliserChoice::Pair:
        return "pair";
    case BearingLocaliserChoice::Grid:
        return "grid";
    }
    return "none";
}

std::string_view array_shape_name(ArrayShapeType shape) {
    switch (shape) {
    case ArrayShapeType::None:
        return "none";
    case ArrayShapeType::Point:
        return "point";
    case ArrayShapeType::Line:
        return "line";
    case ArrayShapeType::Plane:
        return "plane";
    case ArrayShapeType::Volume:
        return "volume";
    }
    return "none";
}

} // namespace pamguard::localisation
