#include "pamguard/detectors/WhistlePeakDetector.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace pamguard::detectors {

WhistlePeakDetector::WhistlePeakDetector(WhistlePeakConfig config)
    : config_(std::move(config)) {
    if (config_.fft_length < 8 || config_.fft_length % 2 != 0) {
        throw std::invalid_argument("whistle peak fft_length must be an even value >= 8");
    }
    if (config_.fft_hop == 0 || config_.sample_rate_hz <= 0.0) {
        throw std::invalid_argument("whistle peak fft_hop and sample_rate_hz must be positive");
    }
    if (config_.search_bin1 == 0) {
        config_.search_bin1 = half_spectrum_bins() - 2;
    }
    reset();
}

const WhistlePeakConfig& WhistlePeakDetector::config() const noexcept {
    return config_;
}

void WhistlePeakDetector::reset() {
    const auto half = half_spectrum_bins();
    spectrum_average_.assign(half, 0.0);
    over_threshold_.assign(half, false);
    mag_square_data_.assign(half, 0.0);
    local_average_.assign(half, 0.0);
    slices_analysed_ = 0;
    prepare_constants();
}

std::vector<WhistlePeak> WhistlePeakDetector::process_magnitude_slice(
    const std::vector<double>& magnitude_squared,
    std::int64_t start_sample,
    std::int64_t time_ms,
    std::size_t slice_number) {
    const auto half = half_spectrum_bins();
    if (magnitude_squared.size() != half) {
        throw std::invalid_argument("whistle peak magnitude slice must be fft_length / 2 bins");
    }

    ++slices_analysed_;
    if (slices_analysed_ <= config_.warmup_slices) {
        for (std::size_t i = 0; i < half; ++i) {
            spectrum_average_[i] += magnitude_squared[i] / static_cast<double>(config_.warmup_slices);
        }
        return {};
    }

    const auto search_bin0 = std::max<std::size_t>(config_.search_bin0, 1);
    const auto search_bin1 = std::min(config_.search_bin1, half - 2);
    int n_over = 0;
    for (std::size_t i = search_bin0; i <= search_bin1; ++i) {
        mag_square_data_[i] = magnitude_squared[i];
        const double new_value = mag_square_data_[i] / spectrum_average_[i];
        if ((over_threshold_[i] = new_value > detection_threshold_)) {
            spectrum_average_[i] *= bgnd_update1_1_;
            spectrum_average_[i] += mag_square_data_[i] * bgnd_update1_;
            ++n_over;
        }
        else {
            spectrum_average_[i] *= bgnd_update0_1_;
            spectrum_average_[i] += mag_square_data_[i] * bgnd_update0_;
        }
        local_average_[i] = 0.0;
    }

    if (static_cast<double>(n_over) * 100.0 / static_cast<double>(over_threshold_.size()) > config_.max_percent_over_threshold) {
        return {};
    }

    constexpr std::size_t local_average_len = 5;
    constexpr std::size_t local_average_gap = 5;
    constexpr std::size_t lao = local_average_len / 2;
    std::size_t k = half;
    for (std::size_t i = 0; i < local_average_len - lao; ++i) {
        --k;
        std::size_t l = half;
        for (std::size_t j = 0; j < local_average_len; ++j) {
            --l;
            local_average_[i] += mag_square_data_[j];
            local_average_[k] += mag_square_data_[l];
        }
        local_average_[i] /= static_cast<double>(local_average_len);
        local_average_[k] /= static_cast<double>(local_average_len);
    }
    for (std::size_t i = local_average_len - lao; i < half - lao; ++i) {
        local_average_[i] += (mag_square_data_[i + lao] - mag_square_data_[i - lao]) / static_cast<double>(local_average_len);
    }

    const std::size_t sao = (local_average_len / 2) + (local_average_gap / 2);
    for (std::size_t i = 0; i < sao; ++i) {
        mag_square_data_[i] = 0.0;
        mag_square_data_[half - 1 - i] = 0.0;
    }

    std::vector<WhistlePeak> peaks;
    PeakStatus peak_on = PeakStatus::PeakOff;
    WhistlePeak new_peak;
    new_peak.start_sample = start_sample;
    new_peak.time_ms = time_ms;
    new_peak.slice_number = slice_number;

    for (std::size_t i = sao + search_bin0; i < search_bin1 - sao; ++i) {
        mag_square_data_[i] -= (local_average_[i - sao] + local_average_[i + sao]) / 2.0;
        over_threshold_[i] = ((mag_square_data_[i] + spectrum_average_[i]) / spectrum_average_[i] > detection_threshold_);
        if (peak_on == PeakStatus::PeakOff) {
            if (over_threshold_[i]) {
                new_peak.min_freq = i;
                new_peak.max_freq = i;
                new_peak.peak_freq = i;
                new_peak.signal = spectrum_average_[i];
                new_peak.max_amp = spectrum_average_[i];
                new_peak.noise = spectrum_average_[i];
                new_peak.ok = over_threshold_[i];
                peak_on = PeakStatus::PeakOn;
            }
        }
        else if (peak_on == PeakStatus::PeakOn) {
            if (!over_threshold_[i]) {
                peak_on = PeakStatus::PeakOff;
                const auto peak_width = new_peak.max_freq - new_peak.min_freq + 1;
                if (peak_width >= config_.min_peak_width && peak_width <= config_.max_peak_width) {
                    peaks.push_back(new_peak);
                    new_peak = WhistlePeak{};
                    new_peak.start_sample = start_sample;
                    new_peak.time_ms = time_ms;
                    new_peak.slice_number = slice_number;
                }
            }
            else {
                new_peak.max_freq = i;
                mag_square_data_[i] = magnitude_squared[i];
                if (mag_square_data_[i] > new_peak.max_amp) {
                    new_peak.max_amp = mag_square_data_[i];
                    new_peak.peak_freq = i;
                }
                new_peak.signal += mag_square_data_[i];
                new_peak.noise += spectrum_average_[i];
                new_peak.ok = new_peak.ok || over_threshold_[i];
            }
        }
    }

    return peaks;
}

std::size_t WhistlePeakDetector::half_spectrum_bins() const noexcept {
    return config_.fft_length / 2;
}

void WhistlePeakDetector::prepare_constants() {
    detection_threshold_ = std::pow(10.0, config_.detection_threshold_db / 10.0);
    bgnd_update0_ = static_cast<double>(config_.fft_hop) / config_.sample_rate_hz / config_.peak_time_constant_0;
    bgnd_update0_1_ = 1.0 - bgnd_update0_;
    bgnd_update1_ = static_cast<double>(config_.fft_hop) / config_.sample_rate_hz / config_.peak_time_constant_1;
    bgnd_update1_1_ = 1.0 - bgnd_update1_;
}

} // namespace pamguard::detectors
