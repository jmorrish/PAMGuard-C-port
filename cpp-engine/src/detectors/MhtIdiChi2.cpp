#include "pamguard/detectors/MhtIdiChi2.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace pamguard::detectors {

MhtIdiChi2::MhtIdiChi2(MhtIdiChi2Config config)
    : config_(std::move(config)) {
    if (config_.sample_rate_hz <= 0.0) {
        throw std::invalid_argument("mht idi chi2 sample_rate_hz must be positive");
    }
    if (config_.error <= 0.0 || config_.min_error <= 0.0) {
        throw std::invalid_argument("mht idi chi2 error and min_error must be positive");
    }
}

const MhtIdiChi2Config& MhtIdiChi2::config() const noexcept {
    return config_;
}

void MhtIdiChi2::clear() {
    chi2_ = 0.0;
    last_idi_ = -1.0;
    has_last_unit_ = false;
    last_unit_time_ns_ = 0;
}

double MhtIdiChi2::calc_time_seconds(std::int64_t prev_ns, std::int64_t next_ns) const {
    // IDIManager.calcTimeSR: nanosecond path, millisecond fallback when the
    // next unit's nanosecond time runs backwards; the sample-rate multiply
    // and divide are kept for floating-point fidelity with the reference.
    double sample_diff;
    if (next_ns < prev_ns) {
        const auto prev_ms = prev_ns / 1'000'000;
        const auto next_ms = next_ns / 1'000'000;
        sample_diff = (static_cast<double>(next_ms - prev_ms) / 1000.0) * config_.sample_rate_hz;
    }
    else {
        sample_diff = (static_cast<double>(next_ns - prev_ns) / 1E9) * config_.sample_rate_hz;
    }
    return sample_diff / config_.sample_rate_hz;
}

double MhtIdiChi2::calc_idi_chi2(double idi_1, double idi_2) const {
    if (idi_2 < config_.min_idi) {
        return config_.junk_track_penalty;
    }
    return std::pow(idi_2 - idi_1, 2.0) /
        std::pow(std::max(idi_1 * config_.error, config_.min_error), 2.0);
}

double MhtIdiChi2::calc_chi2(const std::vector<std::int64_t>& unit_times_ns) const {
    // IDIChi2.calcChi2: chi2 over successive IDI differences, divided by the
    // number of IDI pairs (unit count minus two).
    double last_idi = calc_time_seconds(unit_times_ns[0], unit_times_ns[1]);
    double chi2 = 0.0;
    for (std::size_t i = 2; i < unit_times_ns.size(); ++i) {
        const double new_idi = calc_time_seconds(unit_times_ns[i - 1], unit_times_ns[i]);
        chi2 += calc_idi_chi2(last_idi, new_idi);
        last_idi = new_idi;
    }
    return chi2 / static_cast<double>(unit_times_ns.size() - 2);
}

double MhtIdiChi2::update_chi2(std::int64_t unit_time_ns, bool in_track, std::size_t bitcount, std::size_t kcount) {
    if (std::isnan(chi2_)) {
        chi2_ = 0.0;
    }

    if (!in_track) {
        return chi2_ / static_cast<double>(bitcount);
    }

    if (!has_last_unit_ || kcount <= 1 || bitcount < 2) {
        has_last_unit_ = true;
        last_unit_time_ns_ = unit_time_ns;
        chi2_ = 0.0;
        return chi2_;
    }

    const double new_idi = calc_time_seconds(last_unit_time_ns_, unit_time_ns);
    if (last_idi_ == -1.0) {
        last_unit_time_ns_ = unit_time_ns;
        last_idi_ = new_idi;
        return chi2_ / static_cast<double>(bitcount);
    }

    chi2_ += calc_idi_chi2(last_idi_, new_idi);
    last_unit_time_ns_ = unit_time_ns;
    last_idi_ = new_idi;
    return chi2_ / static_cast<double>(bitcount);
}

} // namespace pamguard::detectors
