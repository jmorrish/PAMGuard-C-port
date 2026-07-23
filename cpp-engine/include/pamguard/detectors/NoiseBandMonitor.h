#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "pamguard/dsp/IirFilter.h"

namespace pamguard::detectors {

/**
 * Port of PAMGuard's noiseBandMonitor: octave/third-octave/decidecade band
 * noise levels via a chain of decimators (Butterworth lowpass + take every
 * Nth sample) feeding per-band Butterworth bandpass filters, with RMS and
 * peak accumulated per output interval.
 *
 * Structure follows the reference exactly: decimation groups chain (each
 * group's decimated output is the next group's input; the first group has no
 * decimator), band filters attach to the group whose decimator lowpass sits
 * above the band's high edge by the sqrt(2) band gap, bands are built in
 * descending frequency order and reported ascending, and the decimator's
 * pick-every-Nth offset carries across chunks so odd-length chunks stay
 * sample-exact.
 */
enum class NoiseBandType { Octave, ThirdOctave, Decidecade, Decade, TenthOctave, TwelfthOctave };

struct NoiseBandConfig {
    bool enabled = false;
    NoiseBandType band_type = NoiseBandType::ThirdOctave;
    double min_frequency_hz = 10.0;
    /** 0 means up to Nyquist. */
    double max_frequency_hz = 0.0;
    /** ANSI band reference; 1 kHz for octave-family bands. */
    double reference_frequency_hz = 1000.0;
    /** NoiseBandSettings.iirOrder; decimators use iirOrder + 2, as PAMGuard does. */
    int iir_order = 6;
    double output_interval_seconds = 10.0;
};

struct NoiseBand {
    double centre_hz = 0.0;
    double lo_edge_hz = 0.0;
    double hi_edge_hz = 0.0;
};

struct NoiseBandLevels {
    /** Raw (uncalibrated) linear RMS and peak per band, ascending frequency. */
    std::vector<double> rms;
    std::vector<double> peak;
    std::int64_t end_sample = 0;
    std::int64_t time_unix_ms = 0;
};

/** BandData.getBandRatio / getDecimateFactor. */
double noise_band_ratio(NoiseBandType type);
int noise_band_decimate_factor(NoiseBandType type);

/**
 * Port of BandData.calculateBands: band centres and edges between min and max
 * frequency, anchored at the reference frequency, ascending.
 */
std::vector<NoiseBand> calculate_noise_bands(NoiseBandType type, double min_frequency_hz,
                                             double max_frequency_hz, double reference_frequency_hz);

class NoiseBandMonitor {
public:
    /** One monitor per channel, mirroring the reference's ChannelProcess. */
    NoiseBandMonitor(double sample_rate_hz, const NoiseBandConfig& config);

    [[nodiscard]] bool valid() const noexcept { return valid_; }
    [[nodiscard]] const std::vector<NoiseBand>& bands() const noexcept { return bands_; }

    /**
     * Feed samples; returns a measurement whenever the output interval
     * completes inside this call (at most one per call for chunk lengths up
     * to the interval).
     */
    std::optional<NoiseBandLevels> process(const std::vector<double>& samples, std::int64_t start_sample,
                                           std::int64_t time_unix_ms);

private:
    struct BandOutput {
        std::size_t samples = 0;
        double max_value = 0.0;
        double sum_squared = 0.0;
    };
    struct DecimationGroup {
        std::optional<dsp::FastIirFilter> decimation_filter;
        int decimate_factor = 1;
        int decimator_offset = 0;
        /** Indices into bands_ (ascending order) served by this group. */
        std::vector<std::size_t> band_indices;
        std::vector<dsp::FastIirFilter> band_filters;
        std::vector<double> decimated;
    };

    NoiseBandConfig config_;
    double sample_rate_hz_ = 0.0;
    bool valid_ = false;
    std::vector<NoiseBand> bands_;
    std::vector<DecimationGroup> groups_;
    std::vector<BandOutput> outputs_;
    std::uint64_t interval_samples_ = 0;
    std::uint64_t samples_into_interval_ = 0;
};

} // namespace pamguard::detectors
