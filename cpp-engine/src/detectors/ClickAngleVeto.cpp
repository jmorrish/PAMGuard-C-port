#include "pamguard/detectors/ClickAngleVeto.h"

#include <cmath>

namespace pamguard::detectors {

bool ClickAngleVetoes::pass_veto(
    const ClickAngleVeto& veto, double angle_degrees) noexcept {
    const double angle = std::abs(angle_degrees);
    return angle < veto.start_angle_degrees ||
           angle > veto.end_angle_degrees;
}

bool ClickAngleVetoes::pass_all(
    const std::vector<ClickAngleVeto>& vetoes,
    double angle_degrees) noexcept {
    for (const auto& veto : vetoes) {
        if (!pass_veto(veto, angle_degrees)) {
            return false;
        }
    }
    return true;
}

} // namespace pamguard::detectors
