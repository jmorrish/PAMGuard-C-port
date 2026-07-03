#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace pamguard::detectors {

struct WhistlePeakConfig {
    std::size_t fft_length = 1024;
    std::size_t fft_hop = 512;
    double sample_rate_hz = 48000.0;
    double detection_threshold_db = 6.0;
    double peak_time_constant_0 = 10.0;
    double peak_time_constant_1 = 100.0;
    double max_percent_over_threshold = 50.0;
    std::size_t min_peak_width = 3;
    std::size_t max_peak_width = 20;
    std::size_t search_bin0 = 1;
    std::size_t search_bin1 = 0;
    std::size_t warmup_slices = 100;
};

struct WhistlePeak {
    std::size_t channel = 0;
    std::int64_t start_sample = 0;
    std::int64_t time_ms = 0;
    std::size_t slice_number = 0;
    std::size_t min_freq = 0;
    std::size_t peak_freq = 0;
    std::size_t max_freq = 0;
    double max_amp = 0.0;
    double signal = 0.0;
    double noise = 0.0;
    bool ok = false;
};

class WhistlePeakDetector {
public:
    explicit WhistlePeakDetector(WhistlePeakConfig config);

    [[nodiscard]] const WhistlePeakConfig& config() const noexcept;
    std::vector<WhistlePeak> process_magnitude_slice(
        const std::vector<double>& magnitude_squared,
        std::int64_t start_sample,
        std::int64_t time_ms,
        std::size_t slice_number);
    void reset();

private:
    enum class PeakStatus {
        PeakOn,
        PeakOff
    };

    WhistlePeakConfig config_;
    std::vector<double> spectrum_average_;
    std::vector<bool> over_threshold_;
    std::vector<double> mag_square_data_;
    std::vector<double> local_average_;
    std::size_t slices_analysed_ = 0;
    double detection_threshold_ = 0.0;
    double bgnd_update0_ = 0.0;
    double bgnd_update0_1_ = 1.0;
    double bgnd_update1_ = 0.0;
    double bgnd_update1_1_ = 1.0;

    [[nodiscard]] std::size_t half_spectrum_bins() const noexcept;
    void prepare_constants();
};

} // namespace pamguard::detectors
