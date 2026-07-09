#include "pamguard/localisation/ArrayShape.h"

#include <cmath>
#include <limits>

namespace pamguard::localisation {

namespace {

using Vec3 = std::array<double, 3>;

constexpr Vec3 kCartesianAxes[3] = {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};

bool exactly_equal(const Vec3& a, const Vec3& b) {
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}

Vec3 sub(const Vec3& a, const Vec3& b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

Vec3 times(const Vec3& a, double scale) {
    return {a[0] * scale, a[1] * scale, a[2] * scale};
}

double dot(const Vec3& a, const Vec3& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

double norm(const Vec3& a) {
    return std::sqrt(dot(a, a));
}

Vec3 unit(const Vec3& a) {
    const double size = norm(a);
    if (size == 0.0) {
        return a;
    }
    return times(a, 1.0 / size);
}

Vec3 cross(const Vec3& a, const Vec3& b) {
    return {
        a[1] * b[2] - a[2] * b[1],
        -a[0] * b[2] + a[2] * b[0],
        a[0] * b[1] - a[1] * b[0],
    };
}

double triple_dot(const Vec3& a, const Vec3& b, const Vec3& c) {
    return dot(a, cross(b, c));
}

// PamVector.angle: acos of the unit-vector dot product, without clamping,
// exactly as the Java reference.
double angle(const Vec3& a, const Vec3& b) {
    return std::acos(dot(unit(a), unit(b)));
}

double abs_angle(const Vec3& a, const Vec3& b) {
    double ang = angle(a, b);
    if (ang > std::acos(-1.0) / 2.0) {
        ang = std::acos(-1.0) - ang;
    }
    return ang;
}

bool is_parallel(const Vec3& a, const Vec3& b) {
    const double ang = angle(a, b);
    return ang < 0.001 || std::acos(-1.0) - ang < 0.001;
}

bool is_in_line(const Vec3& a, const Vec3& b) {
    const double size = std::max(norm(a), norm(b));
    if (size == 0.0) {
        return true;
    }
    if (!is_parallel(a, b)) {
        return false;
    }
    const Vec3 difference = sub(b, a);
    if (norm(difference) / size < 0.000001) {
        return true;
    }
    return is_parallel(difference, b);
}

std::size_t principal_axis(const Vec3& a) {
    std::size_t axis = 0;
    double min_angle = abs_angle(a, kCartesianAxes[0]);
    for (std::size_t i = 1; i < 3; ++i) {
        const double ang = abs_angle(a, kCartesianAxes[i]);
        if (ang < min_angle) {
            min_angle = ang;
            axis = i;
        }
    }
    return axis;
}

/** ArrayManager.getSpatiallyUniquePhones: last duplicate wins. */
std::vector<Vec3> spatially_unique(const std::vector<Vec3>& positions) {
    std::vector<Vec3> result;
    result.reserve(positions.size());
    for (std::size_t i = 0; i < positions.size(); ++i) {
        bool unique_position = true;
        for (std::size_t j = i + 1; j < positions.size(); ++j) {
            if (exactly_equal(positions[i], positions[j])) {
                unique_position = false;
            }
        }
        if (unique_position) {
            result.push_back(positions[i]);
        }
    }
    return result;
}

std::vector<Vec3> pair_vectors(const std::vector<Vec3>& positions) {
    std::vector<Vec3> pairs;
    pairs.reserve(positions.size() * (positions.size() - 1) / 2);
    for (std::size_t i = 0; i < positions.size(); ++i) {
        for (std::size_t j = i + 1; j < positions.size(); ++j) {
            pairs.push_back(sub(positions[j], positions[i]));
        }
    }
    return pairs;
}

bool are_in_line(const std::vector<Vec3>& pairs) {
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        for (std::size_t j = i + 1; j < pairs.size(); ++j) {
            if (!is_in_line(pairs[i], pairs[j])) {
                return false;
            }
        }
    }
    return true;
}

/** ArrayManager.getMaxVolume: maximum of SIGNED triple products, as in Java. */
double max_volume(const std::vector<Vec3>& pairs) {
    double max_vol = 0.0;
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        for (std::size_t j = i + 1; j < pairs.size(); ++j) {
            for (std::size_t k = j + 1; k < pairs.size(); ++k) {
                max_vol = std::max(max_vol, triple_dot(pairs[i], pairs[j], pairs[k]));
            }
        }
    }
    return max_vol;
}

std::vector<Vec3> line_array_vector(const std::vector<Vec3>& positions) {
    Vec3 vector = sub(positions[1], positions[0]);
    const auto axis = principal_axis(vector);
    if (dot(vector, kCartesianAxes[axis]) < 0.0) {
        vector = times(vector, -1.0);
    }
    return {unit(vector)};
}

std::vector<Vec3> plane_array_vectors(const std::vector<Vec3>& positions) {
    const auto pairs = pair_vectors(positions);
    const auto pair_count = pairs.size();
    std::vector<std::size_t> closest_axis(pair_count, 0);
    std::vector<double> closest_angle(pair_count, 0.0);
    for (std::size_t i = 0; i < pair_count; ++i) {
        closest_axis[i] = principal_axis(pairs[i]);
        closest_angle[i] = abs_angle(pairs[i], kCartesianAxes[closest_axis[i]]);
    }

    bool have_perpendicular = false;
    Vec3 plane_perpendicular{};
    for (std::size_t i = 0; i < pair_count; ++i) {
        for (std::size_t j = i + 1; j < pair_count; ++j) {
            if (!is_parallel(pairs[i], pairs[j])) {
                plane_perpendicular = cross(pairs[i], pairs[j]);
                have_perpendicular = true;
                break;
            }
        }
        if (!have_perpendicular) {
            break;
        }
    }
    if (!have_perpendicular) {
        plane_perpendicular = kCartesianAxes[2];
    }

    long long closest_pair[3] = {-1, -1, -1};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        double closest = std::numeric_limits<double>::max();
        for (std::size_t i = 0; i < pair_count; ++i) {
            if (closest_axis[i] != axis) {
                continue;
            }
            if (closest_angle[i] < closest) {
                closest = closest_angle[i];
                closest_pair[axis] = static_cast<long long>(i);
            }
        }
    }
    long long start_pair;
    if (closest_pair[1] >= 0) {
        start_pair = closest_pair[1];
    }
    else if (closest_pair[0] >= 0) {
        start_pair = closest_pair[0];
    }
    else {
        start_pair = closest_pair[2];
    }
    if (start_pair < 0) {
        return {};
    }

    const auto start = static_cast<std::size_t>(start_pair);
    Vec3 first = pairs[start];
    if (angle(first, kCartesianAxes[closest_axis[start]]) > std::acos(-1.0) / 2.0) {
        first = times(first, -1.0);
    }
    Vec3 second = cross(first, plane_perpendicular);
    const auto second_axis = principal_axis(second);
    if (angle(second, kCartesianAxes[second_axis]) > std::acos(-1.0) / 2.0) {
        second = times(second, -1.0);
    }
    return {unit(first), unit(second)};
}

} // namespace

ArrayShapeType array_shape(const std::vector<Vec3>& hydrophone_positions_m) {
    if (hydrophone_positions_m.empty()) {
        return ArrayShapeType::None;
    }
    const auto unique_positions = spatially_unique(hydrophone_positions_m);
    if (unique_positions.size() <= 1) {
        return ArrayShapeType::Point;
    }

    const auto pairs = pair_vectors(unique_positions);
    if (unique_positions.size() == 2) {
        return ArrayShapeType::Line;
    }
    if (are_in_line(pairs)) {
        return ArrayShapeType::Line;
    }
    if (unique_positions.size() == 3) {
        return ArrayShapeType::Plane;
    }
    if (max_volume(pairs) == 0.0) {
        return ArrayShapeType::Plane;
    }
    return ArrayShapeType::Volume;
}

std::vector<Vec3> array_directions(const std::vector<Vec3>& hydrophone_positions_m) {
    const auto unique_positions = spatially_unique(hydrophone_positions_m);
    if (unique_positions.empty()) {
        return {};
    }
    switch (array_shape(hydrophone_positions_m)) {
    case ArrayShapeType::Line:
        return line_array_vector(unique_positions);
    case ArrayShapeType::Plane:
        return plane_array_vectors(unique_positions);
    case ArrayShapeType::Volume:
        return {kCartesianAxes[0], kCartesianAxes[1], kCartesianAxes[2]};
    case ArrayShapeType::None:
    case ArrayShapeType::Point:
    default:
        return {};
    }
}

} // namespace pamguard::localisation
