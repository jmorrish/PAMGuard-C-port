#pragma once

#include <cstdint>
#include <vector>

namespace pamguard::detectors {

/**
 * Port of angleVetoes.AngleVeto. PAMGuard serialises a channel bitmap but its
 * current implementation deliberately does not use it.
 */
struct ClickAngleVeto {
    std::uint32_t channels = 0;
    double start_angle_degrees = 0.0;
    double end_angle_degrees = 0.0;
};

/**
 * Exact non-display semantics of angleVetoes.AngleVetoes.
 */
class ClickAngleVetoes {
public:
    [[nodiscard]] static bool pass_veto(
        const ClickAngleVeto& veto, double angle_degrees) noexcept;

    [[nodiscard]] static bool pass_all(
        const std::vector<ClickAngleVeto>& vetoes,
        double angle_degrees) noexcept;
};

} // namespace pamguard::detectors
