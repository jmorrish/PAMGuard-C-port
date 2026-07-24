#include "pamguard/localisation/DelayGroupEstimator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "pamguard/dsp/IirFilter.h"
#include "pamguard/dsp/JtFft.h"

namespace pamguard::localisation {

namespace {

std::size_t rounded_fft_bin(double frequency_hz, std::size_t fft_length, double sample_rate_hz) {
    if (!std::isfinite(frequency_hz) || frequency_hz < 0.0) {
        throw std::invalid_argument("delay filter frequencies must be non-negative and finite");
    }
    const auto bin = static_cast<long long>(
        std::llround(frequency_hz * static_cast<double>(fft_length) / sample_rate_hz));
    return static_cast<std::size_t>(std::clamp<long long>(
        bin, 0, static_cast<long long>(fft_length / 2 - 1)));
}

void zero_packed_bin(std::vector<double>& packed, std::size_t bin) {
    packed[2 * bin] = 0.0;
    packed[2 * bin + 1] = 0.0;
}

void apply_fft_filter(std::vector<double>& packed, std::size_t fft_length,
                      double sample_rate_hz, const DelayMeasurementConfig& config) {
    if (!(sample_rate_hz > 0.0) || !std::isfinite(sample_rate_hz)) {
        throw std::invalid_argument("sample_rate_hz must be positive when delay filtering is enabled");
    }
    const auto high_bin = rounded_fft_bin(config.filter_high_pass_hz, fft_length, sample_rate_hz);
    const auto low_bin = rounded_fft_bin(config.filter_low_pass_hz, fft_length, sample_rate_hz);
    const auto lower = std::min(high_bin, low_bin);
    const auto upper = std::max(high_bin, low_bin);
    const auto half = fft_length / 2;

    switch (config.filter_band) {
    case DelayFilterBand::HighPass:
        for (std::size_t bin = 0; bin < high_bin; ++bin) {
            zero_packed_bin(packed, bin);
        }
        break;
    case DelayFilterBand::LowPass:
        for (std::size_t bin = low_bin; bin < half; ++bin) {
            zero_packed_bin(packed, bin);
        }
        break;
    case DelayFilterBand::BandPass:
        for (std::size_t bin = 0; bin < lower; ++bin) {
            zero_packed_bin(packed, bin);
        }
        for (std::size_t bin = upper; bin < half; ++bin) {
            zero_packed_bin(packed, bin);
        }
        break;
    case DelayFilterBand::BandStop:
        for (std::size_t bin = lower; bin < upper; ++bin) {
            zero_packed_bin(packed, bin);
        }
        break;
    }
}

std::vector<double> analytic_envelope_from_packed(const std::vector<double>& packed,
                                                  std::size_t fft_length,
                                                  std::size_t signal_length) {
    // signal.Hilbert.getHilbert(ComplexArray,...): PAMGuard doubles the
    // positive-frequency bins, leaves bin zero at one, zeroes the negative
    // half, performs an unscaled inverse, then divides magnitude by N.
    std::vector<double> full(fft_length * 2, 0.0);
    for (std::size_t bin = 0; bin < fft_length / 2; ++bin) {
        const double factor = bin == 0 ? 1.0 : 2.0;
        full[2 * bin] = packed[2 * bin] * factor;
        full[2 * bin + 1] = packed[2 * bin + 1] * factor;
    }
    dsp::JtFft::complex_inverse(full, fft_length, false);
    std::vector<double> envelope(signal_length, 0.0);
    for (std::size_t i = 0; i < signal_length; ++i) {
        envelope[i] = std::hypot(full[2 * i], full[2 * i + 1]) /
                      static_cast<double>(fft_length);
    }
    return envelope;
}

void extract_leading_edge(std::vector<double>& envelope, int start, int end) {
    // DelayGroup.extractLeadingEdge intentionally leaves the final element
    // untouched after differentiating elements [0,N-2].
    for (std::size_t i = 1, j = 0; i < envelope.size(); ++i, ++j) {
        envelope[j] = envelope[i] - envelope[j];
    }
    if (start < 0 || end < 0 || envelope.empty()) {
        return;
    }
    start = std::clamp(start, 0, static_cast<int>(envelope.size() - 1));
    end = std::clamp(end, 0, static_cast<int>(envelope.size() - 1));
    if (end < start) {
        std::swap(start, end);
    }
    double max_value = envelope[static_cast<std::size_t>(start)];
    int max_position = start;
    for (int i = start; i <= end; ++i) {
        if (envelope[static_cast<std::size_t>(i)] > max_value) {
            max_value = envelope[static_cast<std::size_t>(i)];
            max_position = i;
        }
    }
    int position = max_position;
    while (position > 0 && envelope[static_cast<std::size_t>(position)] > 0.0) {
        --position;
    }
    for (; position >= 0; --position) {
        envelope[static_cast<std::size_t>(position)] = 0.0;
    }
    position = max_position;
    while (position < static_cast<int>(envelope.size()) &&
           envelope[static_cast<std::size_t>(position)] > 0.0) {
        ++position;
    }
    for (; position < static_cast<int>(envelope.size()); ++position) {
        envelope[static_cast<std::size_t>(position)] = 0.0;
    }
}

std::vector<double> up_sample(const std::vector<double>& waveform, int factor) {
    std::vector<double> output(waveform.size() * static_cast<std::size_t>(factor), 0.0);
    for (std::size_t i = 0; i < waveform.size(); ++i) {
        output[i * static_cast<std::size_t>(factor)] = waveform[i] * factor;
    }
    dsp::IirFilterParams params;
    params.type = dsp::IirFilterType::Butterworth;
    params.band = dsp::IirFilterBand::LowPass;
    params.order = 6;
    params.low_pass_freq_hz = static_cast<float>(1.0 / (2.0 * factor));
    dsp::FastIirFilter filter(1.0, params);
    std::vector<double> filtered;
    filter.run(output, filtered);
    return filtered;
}

TimeDelayData correlate_packed(const std::vector<double>& first,
                               const std::vector<double>& second,
                               std::size_t fft_length,
                               double max_delay_samples,
                               std::size_t first_bin,
                               std::size_t last_bin) {
    // ComplexArray.conjTimes(other, binRange), including PAMGuard's packed
    // bin-zero treatment, followed by FastFFT.realInverse.
    std::vector<double> product(fft_length, 0.0);
    double scale_first = 0.0;
    double scale_second = 0.0;
    for (std::size_t bin = first_bin; bin < last_bin; ++bin) {
        const double ar = first[2 * bin];
        const double ai = first[2 * bin + 1];
        const double br = second[2 * bin];
        const double bi = second[2 * bin + 1];
        product[2 * bin] = ar * br + ai * bi;
        product[2 * bin + 1] = -ar * bi + ai * br;
        scale_first += ar * ar + ai * ai;
        scale_second += br * br + bi * bi;
    }
    const auto inverse = dsp::JtFft::real_inverse(product);
    const double scale =
        std::sqrt(scale_first * scale_second) * 2.0 /
        static_cast<double>(fft_length);
    return CorrelationDelayEstimator::interpolated_peak(
        inverse, scale, max_delay_samples);
}

} // namespace

std::vector<ChannelPairDelay> DelayGroupEstimator::estimate_delays(
    const std::vector<std::vector<double>>& waveforms,
    const std::vector<double>& max_delay_samples,
    double sample_rate_hz,
    const DelayMeasurementConfig& config) {
    const auto channel_count = waveforms.size();
    if (channel_count < 2) {
        return {};
    }

    std::size_t waveform_length = 0;
    for (const auto& waveform : waveforms) {
        waveform_length = std::max(waveform_length, waveform.size());
    }
    if (waveform_length == 0) {
        throw std::invalid_argument("delay group waveforms must contain at least one sample");
    }

    const auto pair_count = (channel_count - 1) * channel_count / 2;
    if (!max_delay_samples.empty() && max_delay_samples.size() != pair_count) {
        throw std::invalid_argument("max_delay_samples must be empty or match the channel-pair count");
    }
    if (config.up_sample < 1) {
        throw std::invalid_argument("delay up_sample must be at least one");
    }
    if (config.use_restricted_bins && config.restricted_bins == 0) {
        throw std::invalid_argument("delay restricted_bins must be positive");
    }

    std::vector<std::vector<double>> processed = waveforms;
    if (config.use_restricted_bins) {
        for (auto& waveform : processed) {
            waveform.resize(config.restricted_bins, 0.0);
        }
    }
    if (config.up_sample > 1) {
        for (auto& waveform : processed) {
            waveform = up_sample(waveform, config.up_sample);
        }
    }

    waveform_length = 0;
    for (const auto& waveform : processed) {
        waveform_length = std::max(waveform_length, waveform.size());
    }
    const auto fft_length = next_power_of_two(waveform_length);
    std::vector<std::vector<double>> spectra;
    spectra.reserve(channel_count);
    for (auto& waveform : processed) {
        auto packed = dsp::JtFft::real_forward(waveform, fft_length);
        if (config.filter_bearings) {
            apply_fft_filter(packed, fft_length,
                             sample_rate_hz * config.up_sample, config);
        }
        if (config.envelope_bearings) {
            waveform = analytic_envelope_from_packed(
                packed, fft_length, waveform.size());
            if (config.use_leading_edge) {
                extract_leading_edge(
                    waveform,
                    config.leading_edge_search_start < 0
                        ? -1
                        : config.leading_edge_search_start * config.up_sample,
                    config.leading_edge_search_end < 0
                        ? -1
                        : config.leading_edge_search_end * config.up_sample);
            }
            packed = dsp::JtFft::real_forward(waveform, fft_length);
        }
        spectra.push_back(std::move(packed));
    }

    std::size_t correlation_first_bin = 0;
    std::size_t correlation_last_bin = fft_length / 2;
    if (config.filter_bearings) {
        const auto high_bin = rounded_fft_bin(
            config.filter_high_pass_hz, fft_length,
            sample_rate_hz * config.up_sample);
        const auto low_bin = rounded_fft_bin(
            config.filter_low_pass_hz, fft_length,
            sample_rate_hz * config.up_sample);
        switch (config.filter_band) {
        case DelayFilterBand::BandPass:
            correlation_first_bin = std::min(high_bin, low_bin);
            correlation_last_bin = std::max(high_bin, low_bin);
            break;
        case DelayFilterBand::BandStop:
            // This inverted range is the pinned Java Correlations behaviour:
            // it consequently correlates no bins for BANDSTOP.
            correlation_first_bin = std::max(high_bin, low_bin);
            correlation_last_bin = std::min(high_bin, low_bin);
            break;
        case DelayFilterBand::HighPass:
            correlation_first_bin = high_bin;
            break;
        case DelayFilterBand::LowPass:
            correlation_last_bin = low_bin;
            break;
        }
    }

    std::vector<ChannelPairDelay> delays;
    delays.reserve(pair_count);
    std::size_t pair_index = 0;
    for (std::size_t i = 0; i < channel_count; ++i) {
        for (std::size_t j = i + 1; j < channel_count; ++j, ++pair_index) {
            const double original_max_delay =
                max_delay_samples.empty() ? static_cast<double>(fft_length)
                                          : max_delay_samples[pair_index];
            const double max_delay = original_max_delay * config.up_sample;
            ChannelPairDelay delay;
            delay.pair_index = pair_index;
            delay.channel_a = i;
            delay.channel_b = j;
            delay.audio_channel_a = i;
            delay.audio_channel_b = j;
            delay.geometry_constrained = !max_delay_samples.empty();
            delay.max_delay_samples = delay.geometry_constrained ? original_max_delay : 0.0;
            delay.delay = correlate_packed(
                spectra[i], spectra[j], fft_length, max_delay,
                correlation_first_bin, correlation_last_bin);
            delay.delay.delay_samples /= config.up_sample;
            delays.push_back(delay);
        }
    }

    return delays;
}

std::size_t DelayGroupEstimator::next_power_of_two(std::size_t value) noexcept {
    std::size_t power = 1;
    while (power < value) {
        power <<= 1;
    }
    return power;
}

} // namespace pamguard::localisation
