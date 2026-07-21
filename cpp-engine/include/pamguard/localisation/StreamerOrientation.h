#pragma once

#include <array>

namespace pamguard::localisation {

/**
 * Port of PAMGuard's PamQuaternion euler-angle construction combined with
 * PamVector.rotateVector — the rotation `HydrophoneLocator.getPhoneLatLong`
 * applies to a hydrophone's coordinates for its streamer's heading, pitch,
 * and roll before translating by the streamer position.
 *
 * Angles are in **radians**. Faithful detail: PAMGuard converts the heading
 * as `2*pi - heading` inside `fromEulerAngles`, so positive heading rotates
 * clockwise (compass convention) rather than counter-clockwise.
 */
[[nodiscard]] std::array<double, 3> rotate_by_streamer_orientation(const std::array<double, 3>& vector_m,
                                                                   double heading_radians,
                                                                   double pitch_radians,
                                                                   double roll_radians);

} // namespace pamguard::localisation
