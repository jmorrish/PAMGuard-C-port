#include "pamguard/detectors/StandardMhtChi2.h"

#include <algorithm>
#include <cmath>

namespace pamguard::detectors {

namespace {

/** PamArrayUtils.median: sorted middle value, averaging the middle pair. */
double pamguard_median(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    auto n = values.size();
    if (n % 2 == 0) {
        n /= 2;
        return (values[n] + values[n - 1]) / 2.0;
    }
    n /= 2;
    return values[n];
}

} // namespace

StandardMhtChi2Provider::StandardMhtChi2Provider(StandardMhtChi2Params params, MhtKernelParams kernel_params)
    : params_(params), kernel_params_(kernel_params) {}

void StandardMhtChi2Provider::add_detection(const MhtChi2Unit& detection, std::size_t kcount) {
    (void)kcount;
    const std::int64_t unit_ms = detection.time_ns / 1'000'000;
    if (!has_units_) {
        has_units_ = true;
        master_time_series_.assign(1, 0.0);
        first_unit_ms_ = unit_ms;
        last_unit_ms_ = unit_ms;
        last_unit_ = detection;
        ici_count_ = 1;
        return;
    }

    const double time = mht_calc_time_seconds(last_unit_, detection, params_.sample_rate_hz);
    master_time_series_.push_back(time + master_time_series_.back());
    ++ici_count_;
    last_unit_ = detection;
    last_unit_ms_ = unit_ms;
}

std::unique_ptr<MhtChi2<MhtChi2Unit>> StandardMhtChi2Provider::new_chi2() {
    return std::make_unique<StandardMhtChi2>(this);
}

void StandardMhtChi2Provider::clear() {
    master_time_series_.clear();
    ici_count_ = 0;
    has_units_ = false;
}

void StandardMhtChi2Provider::clear_kernel_garbage(std::size_t new_ref_index) {
    // IDIManager.trimData: drop leading entries; retained values keep the
    // original time origin, and the first detection reference is unchanged.
    if (new_ref_index == 0 || new_ref_index >= ici_count_) {
        return;
    }
    master_time_series_.erase(master_time_series_.begin(),
                              master_time_series_.begin() + static_cast<std::ptrdiff_t>(new_ref_index));
    ici_count_ -= new_ref_index;
}

double StandardMhtChi2Provider::total_time_seconds() const noexcept {
    return static_cast<double>(last_unit_ms_ - first_unit_ms_) / 1000.0;
}

StandardMhtChi2Provider::TrackIdiData StandardMhtChi2Provider::track_idi_data(const MhtBitset& track_bits) const {
    TrackIdiData data;
    for (std::size_t i = 0; i < ici_count_; ++i) {
        if (track_bits.get(i)) {
            data.time_series.push_back(master_time_series_[i]);
            const auto n = data.time_series.size();
            if (n > 1) {
                data.idi_series.push_back(data.time_series[n - 1] - data.time_series[n - 2]);
            }
        }
    }
    data.median_idi = data.time_series.size() >= 2 ? pamguard_median(data.idi_series) : -1.0;
    data.time_diff = data.time_series.empty()
        ? 0.0
        : master_time_series_[ici_count_ - 1] - data.time_series.back();
    return data;
}

double StandardMhtChi2Provider::last_time_seconds(const MhtBitset& track_bits) const {
    double last_time = 0.0;
    for (std::size_t i = 0; i < ici_count_; ++i) {
        if (track_bits.get(i)) {
            last_time = master_time_series_[i];
        }
    }
    return last_time;
}

StandardMhtChi2::StandardMhtChi2(const StandardMhtChi2Provider* provider)
    : provider_(provider) {
    MhtIdiChi2Config idi_config;
    idi_config.sample_rate_hz = provider_->params().sample_rate_hz;
    idi_chi2_ = MhtIdiChi2(idi_config);
    MhtAmplitudeChi2Config amplitude_config;
    amplitude_config.sample_rate_hz = provider_->params().sample_rate_hz;
    amplitude_chi2_ = MhtAmplitudeChi2(amplitude_config);
    MhtLengthChi2Config length_config;
    length_config.sample_rate_hz = provider_->params().sample_rate_hz;
    length_chi2_ = MhtLengthChi2(length_config);
    MhtBearingChi2Config bearing_config;
    bearing_config.sample_rate_hz = provider_->params().sample_rate_hz;
    bearing_chi2_ = MhtBearingChi2Delta(bearing_config);
    MhtPeakFrequencyChi2Config peak_frequency_config;
    peak_frequency_config.sample_rate_hz = provider_->params().sample_rate_hz;
    peak_frequency_chi2_ = MhtPeakFrequencyChi2(peak_frequency_config);
    // Java field initialiser: Double.MAX_VALUE (not maxChi). The distinction
    // matters for stable-sort tie order in the confirm-all pass.
}

double StandardMhtChi2::get_chi2() const {
    return chi2_;
}

int StandardMhtChi2::get_n_coasts() const {
    return n_coasts_;
}

std::unique_ptr<MhtChi2<MhtChi2Unit>> StandardMhtChi2::clone_chi2() const {
    return std::make_unique<StandardMhtChi2>(*this);
}

void StandardMhtChi2::update(const MhtChi2Unit& detection, const MhtBitset& track_bits, std::size_t kcount) {
    const auto& params = provider_->params();
    const std::size_t bitcount = track_bits.cardinality();
    const bool in_track = track_bits.get(kcount - 1);

    double raw_chi2 = 0.0;
    if (params.enable_idi) {
        raw_chi2 += idi_chi2_.update_chi2(detection.time_ns, in_track, bitcount, kcount);
    }
    if (params.enable_amplitude) {
        raw_chi2 += amplitude_chi2_.update_chi2(detection, in_track, bitcount, kcount);
    }
    if (params.enable_length) {
        raw_chi2 += length_chi2_.update_chi2(detection, in_track, bitcount, kcount);
    }
    if (params.enable_bearing) {
        raw_chi2 += bearing_chi2_.update_chi2(detection, in_track, bitcount, kcount);
    }
    if (params.enable_peak_frequency) {
        raw_chi2 += peak_frequency_chi2_.update_chi2(detection, in_track, bitcount, kcount);
    }

    // StandardMHTChi2.calcNCoasts.
    StandardMhtChi2Provider::TrackIdiData idi_data;
    double n_coasts = 0.0;
    if (bitcount > 1) {
        idi_data = provider_->track_idi_data(track_bits);
        n_coasts = std::floor(idi_data.time_diff / std::abs(idi_data.median_idi));
        n_coasts = static_cast<double>(static_cast<int>(n_coasts));
    }
    else if (bitcount == 1) {
        const std::int64_t unit_ms = detection.time_ns / 1'000'000;
        n_coasts = std::floor((static_cast<double>(unit_ms - provider_->first_unit_ms()) / 1000.0
            - provider_->last_time_seconds(track_bits)) / params.max_ici);
        n_coasts = static_cast<double>(static_cast<int>(n_coasts));
    }

    if (bitcount < 2 || kcount < 2 || std::isnan(raw_chi2)) {
        chi2_ = params.max_chi;
        n_coasts_ = 0;
        return;
    }

    // StandardMHTChi2.addChi2TrackPenalties.
    double chi2 = raw_chi2 + params.coast_penalty * n_coasts;

    const auto n_pruneback = provider_->kernel_params().n_pruneback;
    if (kcount >= 1 + n_pruneback) {
        const auto prefix = track_bits.prefix(kcount - 1 - n_pruneback);
        if (prefix.cardinality() <= params.new_track_n) {
            chi2 += params.new_track_penalty;
        }
    }

    const double max_idi = *std::max_element(idi_data.idi_series.begin(), idi_data.idi_series.end());
    if (idi_data.median_idi > params.max_ici ||
        max_idi > static_cast<double>(provider_->kernel_params().max_coast + 1) * idi_data.median_idi) {
        chi2 += params.junk_track_penalty;
    }
    else if (bitcount > params.new_track_n) {
        chi2 = chi2 * std::pow(idi_data.median_idi / params.max_ici, params.low_ici_exponent);
        double total_track_time = 0.0;
        for (const auto idi : idi_data.idi_series) {
            total_track_time += idi;
        }
        chi2 = chi2 / std::pow(total_track_time / provider_->total_time_seconds(), params.long_track_exponent);
    }

    chi2_ = chi2;
    n_coasts_ = static_cast<int>(n_coasts);
}

} // namespace pamguard::detectors
