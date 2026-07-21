#include "pamguard/localisation/StreamerOrientation.h"

#include <cmath>
#include <numbers>

namespace pamguard::localisation {

std::array<double, 3> rotate_by_streamer_orientation(const std::array<double, 3>& vector_m,
                                                     double heading_radians,
                                                     double pitch_radians,
                                                     double roll_radians) {
    // PamQuaternion.fromEulerAngles: the heading is flipped first, so a
    // positive heading turns clockwise as a compass bearing does.
    const double heading = 2.0 * std::numbers::pi - heading_radians;
    const double c1 = std::cos(pitch_radians / 2.0);
    const double s1 = std::sin(pitch_radians / 2.0);
    const double c2 = std::cos(roll_radians / 2.0);
    const double s2 = std::sin(roll_radians / 2.0);
    const double c3 = std::cos(heading / 2.0);
    const double s3 = std::sin(heading / 2.0);

    const double w = c1 * c2 * c3 + s1 * s2 * s3;
    const double x = s1 * c2 * c3 - c1 * s2 * s3;
    const double y = c1 * s2 * c3 + s1 * c2 * s3;
    const double z = c1 * c2 * s3 - s1 * s2 * c3;

    // PamQuaternion.toRotation.
    const double xx = x * x;
    const double xy = x * y;
    const double xz = x * z;
    const double xw = x * w;
    const double yy = y * y;
    const double yz = y * z;
    const double yw = y * w;
    const double zz = z * z;
    const double zw = z * w;

    const double m00 = 1.0 - 2.0 * (yy + zz);
    const double m01 = 2.0 * (xy - zw);
    const double m02 = 2.0 * (xz + yw);
    const double m10 = 2.0 * (xy + zw);
    const double m11 = 1.0 - 2.0 * (xx + zz);
    const double m12 = 2.0 * (yz - xw);
    const double m20 = 2.0 * (xz - yw);
    const double m21 = 2.0 * (yz + xw);
    const double m22 = 1.0 - 2.0 * (xx + yy);

    return {
        m00 * vector_m[0] + m01 * vector_m[1] + m02 * vector_m[2],
        m10 * vector_m[0] + m11 * vector_m[1] + m12 * vector_m[2],
        m20 * vector_m[0] + m21 * vector_m[1] + m22 * vector_m[2],
    };
}

} // namespace pamguard::localisation
