#include "pamguard/detectors/MhtSimpleChi2Vars.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace pamguard::detectors {

double mht_calc_time_seconds(const MhtChi2Unit& prev, const MhtChi2Unit& next, double sample_rate_hz) {
    double sample_diff;
    if (next.time_ns < prev.time_ns) {
        const auto prev_ms = prev.time_ns / 1'000'000;
        const auto next_ms = next.time_ns / 1'000'000;
        sample_diff = (static_cast<double>(next_ms - prev_ms) / 1000.0) * sample_rate_hz;
    }
    else {
        sample_diff = (static_cast<double>(next.time_ns - prev.time_ns) / 1E9) * sample_rate_hz;
    }
    return sample_diff / sample_rate_hz;
}

MhtLengthChi2::MhtLengthChi2(MhtLengthChi2Config config)
    : config_(std::move(config)) {
    if (config_.sample_rate_hz <= 0.0 || config_.error <= 0.0 || config_.min_error <= 0.0) {
        throw std::invalid_argument("mht length chi2 config values must be positive");
    }
}

void MhtLengthChi2::clear() {
    chi2_ = 0.0;
    has_last_unit_ = false;
}

double MhtLengthChi2::pair_chi2(const MhtChi2Unit& unit_0, const MhtChi2Unit& unit_1) const {
    // SimpleChi2Var.calcChi2(unit0, unit1): diff^2 over the squared max of
    // the minimum cut and the inter-unit time scaled by the error.
    const double diff = unit_0.duration_ms - unit_1.duration_ms;
    const double idi = mht_calc_time_seconds(unit_0, unit_1, config_.sample_rate_hz);
    return std::pow(diff, 2.0) / std::pow(std::max(config_.min_error, idi * config_.error), 2.0);
}

double MhtLengthChi2::calc_chi2(const std::vector<MhtChi2Unit>& units) const {
    double chi2 = 0.0;
    for (std::size_t i = 1; i < units.size(); ++i) {
        chi2 += pair_chi2(units[i - 1], units[i]);
    }
    return chi2 / static_cast<double>(units.size() - 1);
}

double MhtLengthChi2::update_chi2(const MhtChi2Unit& unit, bool in_track, std::size_t bitcount, std::size_t kcount) {
    if (std::isnan(chi2_)) {
        chi2_ = 0.0;
    }
    if (!in_track) {
        return chi2_ / static_cast<double>(bitcount);
    }
    if (!has_last_unit_ || kcount <= 1 || bitcount < 2) {
        has_last_unit_ = true;
        last_unit_ = unit;
        chi2_ = 0.0;
        return chi2_;
    }
    chi2_ += pair_chi2(last_unit_, unit);
    last_unit_ = unit;
    return chi2_ / static_cast<double>(bitcount);
}

MhtAmplitudeChi2::MhtAmplitudeChi2(MhtAmplitudeChi2Config config)
    : config_(std::move(config)) {
    if (config_.sample_rate_hz <= 0.0 || config_.error <= 0.0 || config_.min_error <= 0.0) {
        throw std::invalid_argument("mht amplitude chi2 config values must be positive");
    }
}

void MhtAmplitudeChi2::clear() {
    chi2_ = 0.0;
    last_delta_ = -1.0;
    has_last_unit_ = false;
}

double MhtAmplitudeChi2::calc_chi2(const std::vector<MhtChi2Unit>& units) const {
    // AmplitudeChi2 inherits SimpleChi2Var.calcChi2 for the batch path:
    // absolute amplitude differences over the time-scaled static error.
    double chi2 = 0.0;
    for (std::size_t i = 1; i < units.size(); ++i) {
        const double diff = std::abs(units[i - 1].amplitude_db - units[i].amplitude_db);
        const double idi = mht_calc_time_seconds(units[i - 1], units[i], config_.sample_rate_hz);
        chi2 += std::pow(diff, 2.0) / std::pow(std::max(config_.min_error, idi * config_.error), 2.0);
    }
    return chi2 / static_cast<double>(units.size() - 1);
}

double MhtAmplitudeChi2::update_chi2(const MhtChi2Unit& unit, bool in_track, std::size_t bitcount, std::size_t kcount) {
    if (std::isnan(chi2_)) {
        chi2_ = 0.0;
    }
    if (!in_track) {
        return chi2_ / static_cast<double>(bitcount);
    }
    if (!has_last_unit_ || kcount <= 1 || bitcount < 2) {
        has_last_unit_ = true;
        last_unit_ = unit;
        chi2_ = 0.0;
        return chi2_;
    }

    const double new_delta = std::abs(last_unit_.amplitude_db - unit.amplitude_db);
    if (last_delta_ == -1.0) {
        last_unit_ = unit;
        last_delta_ = new_delta;
        return chi2_ / static_cast<double>(bitcount);
    }

    const double time_diff = mht_calc_time_seconds(last_unit_, unit, config_.sample_rate_hz);
    double delta_chi2 = std::pow(last_delta_ - new_delta, 2.0) /
        std::pow(std::max(time_diff * config_.error, config_.min_error), 2.0);
    // AmplitudeChi2.calcDeltaChi2: the junk penalty keys off the current
    // absolute amplitude difference, not the delta of deltas.
    if (new_delta > config_.max_amp_jump_db && config_.amp_jump_enable) {
        delta_chi2 += config_.junk_track_penalty;
    }
    chi2_ += delta_chi2;
    last_unit_ = unit;
    last_delta_ = new_delta;
    return chi2_ / static_cast<double>(bitcount);
}

} // namespace pamguard::detectors
