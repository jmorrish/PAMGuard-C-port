#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace pamguard::detectors {

struct MhtIdiChi2Config {
    /** Fractional IDI error, PAMGuard IDIChi2Params default. */
    double error = 0.2;
    /** Minimum error cut, seconds. */
    double min_error = 0.0005;
    /** Minimum plausible IDI, seconds; below this the junk penalty applies. */
    double min_idi = 0.0005;
    /** StandardMHTChi2Params.JUNK_TRACK_PENALTY. */
    double junk_track_penalty = 20000000.0;
    /** Sample rate used by PAMGuard's IDIManager time conversion round trip. */
    double sample_rate_hz = 48000.0;
};

/**
 * Port of PAMGuard's MHT IDI chi-squared variable
 * (clickTrainDetector...mhtvar.IDIChi2 over SimpleChi2Var), the core track
 * quality measure of the MHT click train detector. Reproduces the batch
 * calcChi2 over a unit list and the streaming updateChi2 state machine:
 * out-of-track units return the held chi2 over bitcount, the first usable
 * unit resets, the first IDI is only recorded, and sub-minimum IDIs add the
 * junk track penalty. Times follow IDIManager.calcTime's nanosecond path
 * (millisecond fallback for out-of-order units), including the sample-rate
 * multiply/divide round trip for floating-point fidelity.
 */
class MhtIdiChi2 {
public:
    MhtIdiChi2() = default;
    explicit MhtIdiChi2(MhtIdiChi2Config config);

    [[nodiscard]] const MhtIdiChi2Config& config() const noexcept;

    /** PAMGuard SimpleChi2Var/IDIChi2 calcChi2 over a full unit list. */
    [[nodiscard]] double calc_chi2(const std::vector<std::int64_t>& unit_times_ns) const;

    /** PAMGuard IDIChi2.updateChi2 for one kernel step. */
    double update_chi2(std::int64_t unit_time_ns, bool in_track, std::size_t bitcount, std::size_t kcount);

    void clear();

    /** Raw accumulated chi2 (not divided by bitcount), as MHTChi2Var.getChi2. */
    [[nodiscard]] double raw_chi2() const noexcept { return chi2_; }
    [[nodiscard]] double error() const noexcept { return config_.error; }

private:
    MhtIdiChi2Config config_;
    double chi2_ = 0.0;
    double last_idi_ = -1.0;
    bool has_last_unit_ = false;
    std::int64_t last_unit_time_ns_ = 0;

    [[nodiscard]] double calc_time_seconds(std::int64_t prev_ns, std::int64_t next_ns) const;
    [[nodiscard]] double calc_idi_chi2(double idi_1, double idi_2) const;
};

} // namespace pamguard::detectors
