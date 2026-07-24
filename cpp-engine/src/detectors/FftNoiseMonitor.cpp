#include "pamguard/detectors/FftNoiseMonitor.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace pamguard::detectors {

FftNoiseMonitor::FftNoiseMonitor(
    double sample_rate_hz, std::size_t fft_length, std::size_t fft_hop,
    FftNoiseConfig config)
    : sample_rate_hz_(sample_rate_hz),
      fft_length_(fft_length),
      fft_hop_(fft_hop),
      config_(std::move(config)) {
    if (!(sample_rate_hz_ > 0.0) || fft_length_ == 0 || fft_hop_ == 0 ||
        config_.measurement_interval_seconds <= 0 ||
        config_.n_measures <= 0 || config_.channels.empty() ||
        config_.bands.empty()) {
        throw std::invalid_argument("invalid FFT noise monitor configuration");
    }
    std::sort(config_.channels.begin(), config_.channels.end());
    config_.channels.erase(
        std::unique(config_.channels.begin(), config_.channels.end()),
        config_.channels.end());
    set_measurement_times(0);
}

void FftNoiseMonitor::set_measurement_times(std::int64_t current_sample) {
    const auto all_measures = static_cast<std::size_t>(
        std::ceil(sample_rate_hz_ *
                  static_cast<double>(config_.measurement_interval_seconds) /
                  static_cast<double>(fft_hop_)) + 1.0);
    const auto count = config_.use_all
        ? all_measures
        : std::min<std::size_t>(
              static_cast<std::size_t>(config_.n_measures), all_measures);
    measurement_times_.assign(count, 0);
    if (!config_.use_all) {
        const auto interval = static_cast<std::int64_t>(
            config_.measurement_interval_seconds * sample_rate_hz_ /
            static_cast<double>(count + 1));
        if (interval > 0) {
            std::uniform_int_distribution<std::int64_t> distribution(
                0, interval - 1);
            for (std::size_t i = 0; i < count; ++i) {
                measurement_times_[i] =
                    current_sample + static_cast<std::int64_t>(i) * interval +
                    distribution(random_);
            }
        }
    }
    i_measurement_ = 0;
    measurement_data_.assign(
        config_.channels.size(),
        std::vector<std::vector<double>>(
            config_.bands.size(), std::vector<double>(count, 0.0)));
    next_block_start_sample_ =
        current_sample + static_cast<std::int64_t>(
            config_.measurement_interval_seconds * sample_rate_hz_);
}

std::optional<std::size_t> FftNoiseMonitor::channel_index(
    std::size_t channel) const {
    const auto found =
        std::lower_bound(config_.channels.begin(), config_.channels.end(),
                         channel);
    if (found == config_.channels.end() || *found != channel) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(
        std::distance(config_.channels.begin(), found));
}

void FftNoiseMonitor::make_measurements(
    std::size_t channel, std::size_t measurement,
    const std::vector<double>& magnitude_squared) {
    const auto index = channel_index(channel);
    if (!index.has_value() || measurement >= measurement_times_.size()) {
        return;
    }
    const double frequency_to_bin =
        static_cast<double>(fft_length_) / sample_rate_hz_;
    for (std::size_t band_index = 0; band_index < config_.bands.size();
         ++band_index) {
        const auto& band = config_.bands[band_index];
        const double bin1 = band.low_frequency_hz * frequency_to_bin;
        const double bin2 = band.high_frequency_hz * frequency_to_bin;
        int floor_bin1 = static_cast<int>(std::floor(bin1));
        int ceil_bin2 = static_cast<int>(std::ceil(bin2));
        if (floor_bin1 == -1) {
            floor_bin1 = 0;
        }
        if (ceil_bin2 == static_cast<int>(magnitude_squared.size()) + 1) {
            ceil_bin2 = static_cast<int>(magnitude_squared.size());
        }
        double value = std::numeric_limits<double>::quiet_NaN();
        if (floor_bin1 >= 0 && ceil_bin2 <=
                static_cast<int>(magnitude_squared.size()) &&
            ceil_bin2 > floor_bin1) {
            value = std::accumulate(
                magnitude_squared.begin() + floor_bin1,
                magnitude_squared.begin() + ceil_bin2, 0.0);
            value -= magnitude_squared[floor_bin1] *
                     (bin1 - floor_bin1);
            value -= magnitude_squared[ceil_bin2 - 1] *
                     (ceil_bin2 - bin2);
        }
        measurement_data_[*index][band_index][measurement] = value;
    }
}

std::vector<FftNoisePeriod> FftNoiseMonitor::create_stats(
    std::size_t n_measurements) const {
    std::vector<FftNoisePeriod> output;
    if (n_measurements == 0 || measurement_times_.empty()) {
        return output;
    }
    const auto unused = measurement_times_.size() - n_measurements;
    const auto median_index =
        static_cast<std::size_t>(
            static_cast<long long>(n_measurements / 2) - 1 +
            static_cast<long long>(unused));
    auto low_index = static_cast<long long>(n_measurements / 20) - 1 +
                     static_cast<long long>(unused);
    low_index = std::max<long long>(0, low_index);
    auto high_index =
        static_cast<long long>(n_measurements) - 1 - low_index +
        static_cast<long long>(unused);
    high_index = std::min<long long>(
        static_cast<long long>(n_measurements) - 1, high_index);

    for (std::size_t channel_index = 0;
         channel_index < config_.channels.size(); ++channel_index) {
        FftNoisePeriod period;
        period.channel = config_.channels[channel_index];
        period.end_sample = next_block_start_sample_;
        period.time_unix_ms = epoch_offset_ms_.has_value()
            ? static_cast<std::int64_t>(
                  *epoch_offset_ms_ +
                  next_block_start_sample_ * 1000.0 / sample_rate_hz_)
            : 0;
        period.n_measurements = n_measurements;
        for (const auto& source :
             measurement_data_[channel_index]) {
            FftNoiseBandStatistics stats;
            stats.mean = std::accumulate(
                source.begin(), source.begin() + n_measurements, 0.0) /
                static_cast<double>(n_measurements);
            auto sorted = source;
            std::sort(sorted.begin(), sorted.end());
            stats.median = sorted.at(median_index);
            stats.low_95 = sorted.at(static_cast<std::size_t>(low_index));
            stats.high_95 =
                sorted.at(static_cast<std::size_t>(high_index));
            stats.minimum = sorted.at(unused);
            stats.maximum = sorted.at(n_measurements - 1);
            period.bands.push_back(stats);
        }
        output.push_back(std::move(period));
    }
    return output;
}

std::vector<FftNoisePeriod> FftNoiseMonitor::process_frame(
    std::size_t channel, std::int64_t time_unix_ms,
    std::int64_t start_sample,
    const std::vector<double>& magnitude_squared) {
    if (!channel_index(channel).has_value()) {
        return {};
    }
    if (!epoch_offset_ms_.has_value()) {
        epoch_offset_ms_ =
            static_cast<double>(time_unix_ms) -
            start_sample * 1000.0 / sample_rate_hz_;
    }
    if (measurement_start_ms_ == 0) {
        measurement_start_ms_ = time_unix_ms;
        measurement_end_ms_ =
            measurement_start_ms_ +
            config_.measurement_interval_seconds * 1000LL;
    }
    std::vector<FftNoisePeriod> output;
    const bool highest = channel == config_.channels.back();
    if (highest && time_unix_ms >= measurement_end_ms_) {
        output = create_stats(i_measurement_);
        set_measurement_times(start_sample);
        measurement_start_ms_ = time_unix_ms;
        measurement_end_ms_ =
            measurement_start_ms_ +
            config_.measurement_interval_seconds * 1000LL;
    }
    if (config_.use_all ||
        (i_measurement_ < measurement_times_.size() &&
         start_sample > measurement_times_[i_measurement_])) {
        make_measurements(channel, i_measurement_, magnitude_squared);
        if (highest) {
            ++i_measurement_;
        }
    }
    return output;
}

} // namespace pamguard::detectors
