#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace pamguard::detectors {

/** One MHT kernel detection as seen by the simple chi2 variables. */
struct MhtChi2Unit {
    std::int64_t time_ns = 0;
    double amplitude_db = 0.0;
    double duration_ms = 0.0;
};

struct MhtLengthChi2Config {
    /** LengthChi2 defaults: fractional error and minimum cut, milliseconds domain. */
    double error = 0.2;
    double min_error = 0.002;
    double sample_rate_hz = 48000.0;
};

/**
 * Port of PAMGuard's LengthChi2 over the SimpleChi2Var base path: per-pair
 * contributions diff^2 / max(minError, idi * error)^2 where diff is the
 * signed duration difference in milliseconds, batch-averaged over
 * unitCount - 1, and the streaming update accumulates from the second
 * usable unit onward.
 */
class MhtLengthChi2 {
public:
    MhtLengthChi2() = default;
    explicit MhtLengthChi2(MhtLengthChi2Config config);

    [[nodiscard]] double calc_chi2(const std::vector<MhtChi2Unit>& units) const;
    double update_chi2(const MhtChi2Unit& unit, bool in_track, std::size_t bitcount, std::size_t kcount);
    void clear();

private:
    MhtLengthChi2Config config_;
    double chi2_ = 0.0;
    bool has_last_unit_ = false;
    MhtChi2Unit last_unit_;

    [[nodiscard]] double pair_chi2(const MhtChi2Unit& unit_0, const MhtChi2Unit& unit_1) const;
};

struct MhtAmplitudeChi2Config {
    /** AmplitudeChi2 defaults: static dB error, minimum cut, and the jump penalty. */
    double error = 30.0;
    double min_error = 1.0;
    bool amp_jump_enable = true;
    double max_amp_jump_db = 10.0;
    double junk_track_penalty = 20000000.0;
    double sample_rate_hz = 48000.0;
};

/**
 * Port of PAMGuard's AmplitudeChi2 over the SimpleChi2VarDelta path: the
 * streaming update scores the change between successive absolute amplitude
 * differences (a linear amplitude ramp scores zero) with the junk track
 * penalty added when the current absolute difference exceeds the maximum
 * amplitude jump. The batch calcChi2 keeps the base SimpleChi2Var per-pair
 * form, exactly as the Java class inherits it.
 */
class MhtAmplitudeChi2 {
public:
    MhtAmplitudeChi2() = default;
    explicit MhtAmplitudeChi2(MhtAmplitudeChi2Config config);

    [[nodiscard]] double calc_chi2(const std::vector<MhtChi2Unit>& units) const;
    double update_chi2(const MhtChi2Unit& unit, bool in_track, std::size_t bitcount, std::size_t kcount);
    void clear();

private:
    MhtAmplitudeChi2Config config_;
    double chi2_ = 0.0;
    double last_delta_ = -1.0;
    bool has_last_unit_ = false;
    MhtChi2Unit last_unit_;
};

/** IDIManager.calcTime replica shared by the simple chi2 variables. */
[[nodiscard]] double mht_calc_time_seconds(const MhtChi2Unit& prev, const MhtChi2Unit& next, double sample_rate_hz);

} // namespace pamguard::detectors
