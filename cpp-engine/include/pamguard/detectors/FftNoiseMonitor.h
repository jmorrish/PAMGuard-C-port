#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace pamguard::detectors {

struct FftNoiseBand {
    std::string name;
    double low_frequency_hz = 0.0;
    double high_frequency_hz = 0.0;
};

struct FftNoiseConfig {
    bool enabled = false;
    std::vector<std::size_t> channels;
    int measurement_interval_seconds = 60;
    int n_measures = 100;
    bool use_all = true;
    std::vector<FftNoiseBand> bands;
};

struct FftNoiseBandStatistics {
    double mean = 0.0;
    double median = 0.0;
    double low_95 = 0.0;
    double high_95 = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;
};

struct FftNoisePeriod {
    std::size_t channel = 0;
    std::int64_t end_sample = 0;
    std::int64_t time_unix_ms = 0;
    std::size_t n_measurements = 0;
    std::vector<FftNoiseBandStatistics> bands;
};

/**
 * Port of noiseMonitor.NoiseProcess up to its acquisition calibration step.
 * The six statistics deliberately retain the Java indexing quirks.
 */
class FftNoiseMonitor {
public:
    FftNoiseMonitor(double sample_rate_hz, std::size_t fft_length,
                    std::size_t fft_hop, FftNoiseConfig config);

    std::vector<FftNoisePeriod> process_frame(
        std::size_t channel, std::int64_t time_unix_ms,
        std::int64_t start_sample,
        const std::vector<double>& magnitude_squared);

private:
    double sample_rate_hz_ = 0.0;
    std::size_t fft_length_ = 0;
    std::size_t fft_hop_ = 0;
    FftNoiseConfig config_;
    std::vector<std::int64_t> measurement_times_;
    std::vector<std::vector<std::vector<double>>> measurement_data_;
    std::size_t i_measurement_ = 0;
    std::int64_t next_block_start_sample_ = 0;
    std::int64_t measurement_start_ms_ = 0;
    std::int64_t measurement_end_ms_ = 0;
    std::optional<double> epoch_offset_ms_;
    std::mt19937 random_{0x50414d47u};

    void set_measurement_times(std::int64_t current_sample);
    void make_measurements(std::size_t channel, std::size_t measurement,
                           const std::vector<double>& magnitude_squared);
    std::vector<FftNoisePeriod> create_stats(std::size_t n_measurements) const;
    [[nodiscard]] std::optional<std::size_t> channel_index(
        std::size_t channel) const;
};

} // namespace pamguard::detectors
