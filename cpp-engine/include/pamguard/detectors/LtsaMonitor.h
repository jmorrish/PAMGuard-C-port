#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace pamguard::detectors {

struct LtsaConfig {
    bool enabled = false;
    // LtsaParameters.intervalSeconds default.
    int interval_seconds = 60;
};

// One completed averaging period: the PAMGuard LtsaDataUnit equivalent.
// magnitude[i] = sqrt(mean magnitude-squared) of FFT bin i over the period,
// exactly the value LtsaProcess.ChannelProcess.closePeriod stores (the
// reference keeps it uncalibrated; display gain is applied downstream).
struct LtsaInterval {
    std::int64_t start_time_ms = 0;
    std::int64_t end_time_ms = 0;
    int n_fft = 0;
    std::int64_t start_sample = 0;
    std::int64_t duration_samples = 0;
    std::vector<double> magnitude;
};

// Port of ltsa.LtsaProcess.ChannelProcess: accumulate per-bin magnitude
// squared across FFT slices, close an averaging period when a slice's
// timestamp reaches the period end. Periods are aligned to absolute
// wall-clock interval boundaries (timeMillis / interval * interval), so the
// first period is normally partial — the reference does exactly this.
//
// Reference quirks preserved: closePeriod advances by ONE interval however
// far past the boundary the closing slice is, so after a time gap slices
// land in stale windows until the window catches up; the slice that
// triggers a close is accumulated into the NEXT period; a close with no
// accumulated slices emits nothing and does not advance the window.
class LtsaMonitor {
public:
    explicit LtsaMonitor(const LtsaConfig& config);

    // Feed one FFT slice (magnitude_squared per bin, ascending). Returns the
    // completed period when this slice's timestamp closes one, as the
    // reference's newData -> closePeriod path does (at most one per slice).
    std::optional<LtsaInterval> process_frame(std::int64_t time_ms,
                                              std::int64_t start_sample,
                                              std::int64_t duration_samples,
                                              const std::vector<double>& magnitude_squared);

    // Close the in-progress period (flushDataBlockBuffers -> closePeriod).
    std::optional<LtsaInterval> flush();

private:
    std::int64_t interval_ms_ = 60000;
    bool prepared_ = false;
    std::int64_t current_start_ = 0;
    std::int64_t current_end_ = 0;
    std::vector<double> mean_fft_data_;
    int n_fft_ = 0;
    std::int64_t start_sample_ = 0;
    std::int64_t last_sample_ = 0;

    void prepare(std::int64_t time_ms, std::size_t half_fft_length);
    std::optional<LtsaInterval> close_period();
};

} // namespace pamguard::detectors
