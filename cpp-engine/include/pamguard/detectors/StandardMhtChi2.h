#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "pamguard/detectors/MhtIdiChi2.h"
#include "pamguard/detectors/MhtKernel.h"
#include "pamguard/detectors/MhtSimpleChi2Vars.h"

namespace pamguard::detectors {

struct StandardMhtChi2Params {
    bool enable_idi = true;
    bool enable_amplitude = true;
    bool enable_length = true;
    /** StandardMHTChi2Params defaults. */
    double coast_penalty = 10.0;
    double new_track_penalty = 50.0;
    std::size_t new_track_n = 3;
    double max_ici = 0.4;
    double low_ici_exponent = 0.1;
    double long_track_exponent = 0.1;
    double junk_track_penalty = 20000000.0;
    /** StandardMHTChi2.maxChi. */
    double max_chi = 200000000000000000.0;
    double sample_rate_hz = 48000.0;
};

/**
 * Provider state shared by every StandardMhtChi2 branch: the master
 * cumulative time series built from IDIManager.calcTime as detections are
 * added, plus the chi2 and kernel parameters.
 */
class StandardMhtChi2Provider;

/**
 * Port of PAMGuard's StandardMHTChi2 over the ported IDI, amplitude, and
 * length chi2 variables: sums the enabled variables' streaming updates,
 * derives coast counts from the track IDI structure, and applies the coast,
 * new-track, junk-ICI, low-ICI-nudge, and long-track adjustments. Tracks
 * with fewer than two detections score maxChi with zero coasts.
 */
class StandardMhtChi2 final : public MhtChi2<MhtChi2Unit> {
public:
    explicit StandardMhtChi2(const StandardMhtChi2Provider* provider);

    [[nodiscard]] double get_chi2() const override;
    [[nodiscard]] int get_n_coasts() const override;
    void update(const MhtChi2Unit& detection, const MhtBitset& track_bits, std::size_t kcount) override;
    [[nodiscard]] std::unique_ptr<MhtChi2<MhtChi2Unit>> clone_chi2() const override;

private:
    const StandardMhtChi2Provider* provider_;
    MhtIdiChi2 idi_chi2_;
    MhtAmplitudeChi2 amplitude_chi2_;
    MhtLengthChi2 length_chi2_;
    double chi2_ = 1.7976931348623157e308;
    int n_coasts_ = 0;
};

class StandardMhtChi2Provider final : public MhtChi2Provider<MhtChi2Unit> {
public:
    StandardMhtChi2Provider(StandardMhtChi2Params params, MhtKernelParams kernel_params);

    void add_detection(const MhtChi2Unit& detection, std::size_t kcount) override;
    [[nodiscard]] std::unique_ptr<MhtChi2<MhtChi2Unit>> new_chi2() override;
    void clear() override;
    void clear_kernel_garbage(std::size_t new_ref_index) override;

    [[nodiscard]] const StandardMhtChi2Params& params() const noexcept { return params_; }
    [[nodiscard]] const MhtKernelParams& kernel_params() const noexcept { return kernel_params_; }
    [[nodiscard]] const std::vector<double>& master_time_series() const noexcept { return master_time_series_; }
    [[nodiscard]] std::size_t ici_count() const noexcept { return ici_count_; }
    [[nodiscard]] std::int64_t first_unit_ms() const noexcept { return first_unit_ms_; }
    [[nodiscard]] double total_time_seconds() const noexcept;

    struct TrackIdiData {
        std::vector<double> time_series;
        std::vector<double> idi_series;
        double median_idi = -1.0;
        double time_diff = 0.0;
    };

    /** IDIManager.getIDIStruct over the master time series. */
    [[nodiscard]] TrackIdiData track_idi_data(const MhtBitset& track_bits) const;

    /** IDIManager.getLastTime(bitSet). */
    [[nodiscard]] double last_time_seconds(const MhtBitset& track_bits) const;

private:
    StandardMhtChi2Params params_;
    MhtKernelParams kernel_params_;
    std::vector<double> master_time_series_;
    std::size_t ici_count_ = 0;
    bool has_units_ = false;
    std::int64_t first_unit_ms_ = 0;
    std::int64_t last_unit_ms_ = 0;
    MhtChi2Unit last_unit_;
};

} // namespace pamguard::detectors
