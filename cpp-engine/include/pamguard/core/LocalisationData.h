#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "pamguard/localisation/BearingLocaliserSelector.h"
#include "pamguard/localisation/DelayGroupEstimator.h"
#include "pamguard/localisation/FarFieldBearingLocaliser.h"
#include "pamguard/localisation/WorldVectors.h"

namespace pamguard::core {

inline constexpr const char* kClickLocalisationDataType =
    "pamguard.click-localisation";
inline constexpr const char* kClickBearingDataType =
    "pamguard.click-bearing";

/** LSQ bearing summary shared by click and whistle localisation outputs. */
struct LsqBearingResult {
    bool valid = false;
    double azimuth_radians = 0.0;
    double elevation_radians = 0.0;
    double azimuth_error_radians = 0.0;
    double elevation_error_radians = 0.0;
    std::size_t used_pairs = 0;
    std::vector<localisation::WorldVector> world_vectors;
    std::vector<localisation::WorldVector> earth_world_vectors;
};

/** PAMGuard MLGridBearingLocaliser2 output in its principal-axis frame. */
struct GridBearingResult {
    bool valid = false;
    double theta_radians = 0.0;
    double phi_radians = 0.0;
    double theta_error_radians = 0.0;
    double phi_error_radians = 0.0;
    bool has_phi = false;
    std::size_t used_pairs = 0;
    std::vector<localisation::WorldVector> world_vectors;
    std::vector<localisation::WorldVector> earth_world_vectors;
};

struct ClickLocalisationResult {
    std::size_t click_index = 0;
    std::int64_t click_start_sample = 0;
    std::vector<localisation::ChannelPairDelay> delays;
    LsqBearingResult lsq_bearing;
    GridBearingResult grid_bearing;
    localisation::ArrayShapeType array_shape =
        localisation::ArrayShapeType::None;
    localisation::BearingLocaliserChoice bearing_localiser =
        localisation::BearingLocaliserChoice::None;
};

struct ClickBearingResult {
    std::size_t click_index = 0;
    std::int64_t click_start_sample = 0;
    localisation::FarFieldBearingResult bearing;
};

} // namespace pamguard::core
