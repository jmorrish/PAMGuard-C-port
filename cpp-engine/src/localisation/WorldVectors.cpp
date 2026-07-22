#include "pamguard/localisation/WorldVectors.h"

#include <cmath>
#include <numbers>

#include "pamguard/localisation/JamaMatrix.h"
#include "pamguard/localisation/StreamerOrientation.h"

namespace pamguard::localisation {

namespace {

constexpr double kPi = std::numbers::pi;

using Vec3 = std::array<double, 3>;

/** PamVector.fromHeadAndSlantR. */
Vec3 from_head_and_slant(double heading, double slant_angle) {
    const double z = std::sin(slant_angle);
    const double r = std::cos(slant_angle);
    return {r * std::sin(heading), r * std::cos(heading), z};
}

/**
 * AbstractLocalisation.getPlanarVector: a unit vector towards the detection in
 * the array's coordinate frame, plus whether it describes a cone rather than a
 * direction.
 */
Vec3 planar_vector(const std::vector<double>& angles_radians) {
    if (angles_radians.size() >= 2) {
        return from_head_and_slant(kPi / 2.0 - angles_radians[0], angles_radians[1]);
    }
    return from_head_and_slant(kPi / 2.0 - angles_radians[0], 0.0);
}

/** PamVector.getZAxis().vecProd(v), and the x axis. */
constexpr Vec3 kXAxis = {1.0, 0.0, 0.0};
constexpr Vec3 kZAxis = {0.0, 0.0, 1.0};

/**
 * AbstractLocalisation.linearCoordinateMatrix: a full frame built from a
 * single array axis. If the axis has any horizontal component the second row
 * is perpendicular to it in the horizontal plane; a perfectly vertical array
 * falls back to the x axis.
 */
Matrix3 linear_coordinate_matrix(const Vec3& axis, bool flip_z) {
    Matrix3 matrix{};
    matrix[0] = axis;
    const Vec3 second = (axis[0] == 0.0 && axis[1] == 0.0) ? kXAxis : cross_product(kZAxis, axis);
    matrix[1] = second;
    matrix[2] = flip_z ? cross_product(second, axis) : cross_product(axis, second);
    return matrix;
}

/**
 * AbstractLocalisation.getCoordinateMatrix. With no axes at all the reference
 * falls back to a fixed frame with y as the principal axis, which is the usual
 * orientation for a forward-towed array.
 */
Matrix3 coordinate_matrix(const std::vector<Vec3>& array_axes, bool flip_z) {
    if (array_axes.empty()) {
        return Matrix3{Vec3{0.0, 1.0, 0.0}, Vec3{1.0, 0.0, 0.0}, Vec3{0.0, 0.0, 1.0}};
    }
    if (array_axes.size() == 1) {
        return linear_coordinate_matrix(array_axes[0], flip_z);
    }
    Matrix3 matrix{};
    matrix[0] = array_axes[0];
    matrix[1] = array_axes[1];
    matrix[2] = flip_z ? cross_product(array_axes[1], array_axes[0]) : cross_product(array_axes[0], array_axes[1]);
    return matrix;
}

} // namespace

std::vector<WorldVector> real_world_vectors(ArrayShapeType shape,
                                            const std::vector<WorldVector>& array_frame_vectors,
                                            bool orientation_declared,
                                            double heading_radians,
                                            double pitch_radians,
                                            double roll_radians) {
    std::vector<WorldVector> out = array_frame_vectors;
    for (auto& vector : out) {
        if (orientation_declared) {
            vector.direction = rotate_by_streamer_orientation(vector.direction, heading_radians, pitch_radians,
                                                              roll_radians);
        }
        if (shape == ArrayShapeType::Line) {
            vector.cone = true;
        }
    }
    return out;
}

std::array<double, 3> planar_unit_vector(double azimuth_radians, double elevation_radians) {
    return from_head_and_slant(kPi / 2.0 - azimuth_radians, elevation_radians);
}

std::vector<WorldVector> world_vectors(ArrayShapeType shape, const std::vector<Vec3>& array_axes,
                                       const std::vector<double>& angles_radians) {
    if (shape == ArrayShapeType::None || shape == ArrayShapeType::Point || angles_radians.empty()) {
        return {};
    }

    const auto pointer = planar_vector(angles_radians);

    Matrix3 inverse{};
    if (!jama_inverse_3x3(coordinate_matrix(array_axes, false), inverse)) {
        return {};
    }

    // Only the line branch marks its vectors as cones. A plane or volume
    // vector is never marked, even when the localiser supplied a single angle
    // and the planar vector itself was a cone — the reference builds fresh
    // vectors there and leaves the flag at its default.
    if (shape == ArrayShapeType::Volume) {
        return {WorldVector{matrix_times_column(inverse, pointer), false}};
    }

    if (shape == ArrayShapeType::Plane) {
        // The mirror pair: the same pointer read through frames whose third
        // axis points either side of the array plane.
        Matrix3 flipped_inverse{};
        if (!jama_inverse_3x3(coordinate_matrix(array_axes, true), flipped_inverse)) {
            return {};
        }
        return {
            WorldVector{matrix_times_column(inverse, pointer), false},
            WorldVector{matrix_times_column(flipped_inverse, pointer), false},
        };
    }

    // Line: the left/right pair, from negating the pointer's y component
    // through the same frame. Both are cones about the array axis whatever the
    // localiser reported, because a line array cannot resolve more than that.
    const Vec3 mirrored_pointer = {pointer[0], -pointer[1], pointer[2]};
    return {
        WorldVector{matrix_times_column(inverse, pointer), true},
        WorldVector{matrix_times_column(inverse, mirrored_pointer), true},
    };
}

} // namespace pamguard::localisation
