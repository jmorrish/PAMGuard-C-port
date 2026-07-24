#include "pamguard/detectors/SweepClickClassifier.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>

#include "pamguard/dsp/JtFft.h"

namespace pamguard::detectors {

namespace {

using dsp::JtFft;

int next_binary_exp(int source) {
    int power = 0;
    for (int i = 0; i < 31; ++i) {
        power = 1 << i;
        if (power >= source) {
            break;
        }
    }
    return power;
}

std::vector<double> smooth_data(const std::vector<double>& data, int smooth) {
    if (smooth % 2 == 0) {
        ++smooth;
    }
    const int len = static_cast<int>(data.size());
    const int half_n = (smooth - 1) / 2;
    std::vector<double> padded(static_cast<std::size_t>(len + 2 * half_n), 0.0);
    for (int i = 0; i < len; ++i) {
        padded[static_cast<std::size_t>(i + half_n)] = data[static_cast<std::size_t>(i)] / smooth;
    }
    std::vector<double> result(static_cast<std::size_t>(len), 0.0);
    for (int i = 0; i <= half_n; ++i) {
        result[0] += padded[static_cast<std::size_t>(i + half_n)];
    }
    for (int i = 1; i < len; ++i) {
        result[static_cast<std::size_t>(i)] = result[static_cast<std::size_t>(i - 1)] +
            padded[static_cast<std::size_t>(i + 2 * half_n)] -
            padded[static_cast<std::size_t>(i - 1)];
    }
    return result;
}

std::vector<double> hilbert_envelope(const std::vector<double>& signal) {
    const int data_len = static_cast<int>(signal.size());
    const int fft_length = next_binary_exp(data_len);
    auto packed = JtFft::real_forward(signal, static_cast<std::size_t>(fft_length));
    std::vector<double> full(static_cast<std::size_t>(fft_length) * 2, 0.0);
    for (int j = 0; j < fft_length / 2; ++j) {
        const double factor = j == 0 ? 1.0 : 2.0;
        full[static_cast<std::size_t>(2 * j)] = packed[static_cast<std::size_t>(2 * j)] * factor;
        full[static_cast<std::size_t>(2 * j + 1)] = packed[static_cast<std::size_t>(2 * j + 1)] * factor;
    }
    JtFft::complex_inverse(full, static_cast<std::size_t>(fft_length), false);
    std::vector<double> envelope(static_cast<std::size_t>(data_len), 0.0);
    for (int i = 0; i < data_len; ++i) {
        const double re = full[static_cast<std::size_t>(2 * i)];
        const double im = full[static_cast<std::size_t>(2 * i + 1)];
        envelope[static_cast<std::size_t>(i)] = std::sqrt(re * re + im * im) / fft_length;
    }
    return envelope;
}

std::vector<double> fft_filter(const std::vector<double>& input,
                               const SweepFftFilterConfig& config,
                               double sample_rate_hz) {
    if (input.empty()) {
        return {};
    }
    auto packed = JtFft::real_forward(input, input.size());
    const auto fft_bin = [&](double frequency) {
        return static_cast<std::size_t>(std::clamp<long long>(
            std::llround(frequency * input.size() / sample_rate_hz), 0,
            static_cast<long long>(input.size() / 2 - 1)));
    };
    const auto zero_bin = [&](std::size_t bin) {
        packed[2 * bin] = 0.0;
        packed[2 * bin + 1] = 0.0;
    };
    const auto high = fft_bin(config.high_pass_freq_hz);
    const auto low = fft_bin(config.low_pass_freq_hz);
    const auto lower = std::min(high, low);
    const auto upper = std::max(high, low);
    switch (config.band) {
    case SweepFftFilterBand::HighPass:
        for (std::size_t bin = 0; bin < high; ++bin) zero_bin(bin);
        break;
    case SweepFftFilterBand::LowPass:
        for (std::size_t bin = low; bin < input.size() / 2; ++bin) zero_bin(bin);
        break;
    case SweepFftFilterBand::BandPass:
        for (std::size_t bin = 0; bin < lower; ++bin) zero_bin(bin);
        for (std::size_t bin = upper; bin < input.size() / 2; ++bin) zero_bin(bin);
        break;
    case SweepFftFilterBand::BandStop:
        for (std::size_t bin = lower; bin < upper; ++bin) zero_bin(bin);
        break;
    }
    return JtFft::real_inverse(packed);
}

std::vector<std::vector<double>> wave_data(const ClickDetectionResult& click,
                                           const SweepClickTypeConfig& type,
                                           double sample_rate_hz) {
    if (!type.enable_fft_filter) {
        return click.waveform;
    }
    std::vector<std::vector<double>> filtered;
    filtered.reserve(click.waveform.size());
    for (const auto& channel : click.waveform) {
        filtered.push_back(fft_filter(channel, type.fft_filter, sample_rate_hz));
    }
    return filtered;
}

std::vector<std::vector<int>> length_data(const std::vector<std::vector<double>>& wave,
                                          double length_db,
                                          int smoothing) {
    const double ratio = std::pow(10.0, std::abs(length_db) / 20.0);
    std::vector<std::vector<int>> result(wave.size(), std::vector<int>(2, 0));
    for (std::size_t channel = 0; channel < wave.size(); ++channel) {
        auto analytic = smooth_data(hilbert_envelope(wave[channel]), smoothing);
        if (analytic.empty()) {
            return {};
        }
        int max_index = 0;
        double max_value = analytic[0];
        for (int i = 1; i < static_cast<int>(analytic.size()); ++i) {
            if (analytic[static_cast<std::size_t>(i)] > max_value) {
                max_value = analytic[static_cast<std::size_t>(i)];
                max_index = i;
            }
        }
        const double threshold = max_value / ratio;
        result[channel][0] = 0;
        for (int p = max_index - 1; p >= 0; --p) {
            if (analytic[static_cast<std::size_t>(p)] < threshold) {
                result[channel][0] = p + 1;
                break;
            }
        }
        result[channel][1] = static_cast<int>(analytic.size());
        for (int p = max_index + 1; p < static_cast<int>(analytic.size()); ++p) {
            if (analytic[static_cast<std::size_t>(p)] < threshold) {
                result[channel][1] = p - 1;
                break;
            }
        }
    }
    return result;
}

std::vector<double> hann_click_spectrum(const std::vector<double>& source, int fft_length) {
    std::vector<double> input(static_cast<std::size_t>(fft_length), 0.0);
    if (source.size() == 1) {
        input[0] = source[0];
    }
    else if (!source.empty()) {
        const double denominator = static_cast<double>(source.size() - 1);
        for (std::size_t i = 0; i < source.size() && i < input.size(); ++i) {
            input[i] = source[i] *
                0.5 * (1.0 - std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / denominator));
        }
    }
    const auto packed = JtFft::real_forward(input, static_cast<std::size_t>(fft_length));
    std::vector<double> spectrum(static_cast<std::size_t>(fft_length / 2), 0.0);
    if (!spectrum.empty()) {
        spectrum[0] = packed[0] * packed[0] + packed[1] * packed[1];
    }
    for (std::size_t i = 1; i < spectrum.size(); ++i) {
        spectrum[i] = packed[2 * i] * packed[2 * i] + packed[2 * i + 1] * packed[2 * i + 1];
    }
    return spectrum;
}

std::vector<double> restricted_spectrum(const std::vector<double>& source,
                                        const std::vector<int>& length,
                                        const SweepClickTypeConfig& type) {
    int start = type.restricted_bin_type == SweepRestrictedBinType::ClickCenter
        ? (length[0] + length[1] - type.restricted_bins) / 2
        : 0;
    start = std::max(0, start);
    const int end = std::min<int>(start + type.restricted_bins, static_cast<int>(source.size()));
    std::vector<double> input(static_cast<std::size_t>(type.restricted_bins), 0.0);
    for (int i = start; i < end; ++i) {
        input[static_cast<std::size_t>(i - start)] = source[static_cast<std::size_t>(i)];
    }
    for (int i = 0; i < type.restricted_bins; ++i) {
        input[static_cast<std::size_t>(i)] *=
            0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) /
                                 type.restricted_bins);
    }
    const int fft_length = next_binary_exp(type.restricted_bins);
    const auto packed = JtFft::real_forward(input, static_cast<std::size_t>(fft_length));
    std::vector<double> spectrum(static_cast<std::size_t>(type.restricted_bins / 2), 0.0);
    if (!spectrum.empty()) {
        spectrum[0] = packed[0] * packed[0] + packed[1] * packed[1];
    }
    for (std::size_t i = 1; i < spectrum.size(); ++i) {
        spectrum[i] = packed[2 * i] * packed[2 * i] + packed[2 * i + 1] * packed[2 * i + 1];
    }
    return spectrum;
}

std::vector<std::vector<double>> spectra(const std::vector<std::vector<double>>& wave,
                                         const std::vector<std::vector<int>>& lengths,
                                         const SweepClickTypeConfig& type) {
    std::vector<std::vector<double>> result;
    result.reserve(wave.size());
    for (std::size_t channel = 0; channel < wave.size(); ++channel) {
        if (type.restrict_length) {
            result.push_back(restricted_spectrum(wave[channel], lengths[channel], type));
        }
        else {
            result.push_back(hann_click_spectrum(
                wave[channel], next_binary_exp(static_cast<int>(wave[channel].size()))));
        }
    }
    if (type.channel_choice == SweepChannelChoice::UseMeans && !result.empty()) {
        // SweepClassifierWorker.createSpecData, including its channel-0 double-add
        // and untouched DC-bin quirks.
        std::vector<std::vector<double>> mean(1, result[0]);
        for (std::size_t channel = 0; channel < wave.size(); ++channel) {
            for (std::size_t bin = 1; bin < result[0].size(); ++bin) {
                mean[0][bin] += result[channel][bin];
            }
        }
        return mean;
    }
    return result;
}

template<typename Test>
bool channel_test(SweepChannelChoice choice, std::size_t count, Test test) {
    if (choice == SweepChannelChoice::RequireAll) {
        for (std::size_t i = 0; i < count; ++i) if (!test(i)) return false;
        return true;
    }
    if (choice == SweepChannelChoice::RequireOne) {
        for (std::size_t i = 0; i < count; ++i) if (test(i)) return true;
        return false;
    }
    return test(0);
}

double pick_spec_energy(const std::vector<double>& spectrum, SweepRange range,
                        double sample_rate_hz) {
    const double bins_per_hz = spectrum.size() * 2.0 / sample_rate_hz;
    double r1 = std::clamp(range.low * bins_per_hz, 0.0, static_cast<double>(spectrum.size()));
    double r2 = std::clamp(range.high * bins_per_hz, 0.0, static_cast<double>(spectrum.size()));
    if (r2 <= r1) return 0.0;
    const int bin1 = static_cast<int>(std::floor(r1));
    int bin2 = static_cast<int>(std::ceil(r2));
    bin2 = std::min(bin2, static_cast<int>(spectrum.size()) - 1);
    if (bin1 < 0 || bin2 >= static_cast<int>(spectrum.size())) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    double energy = 0.0;
    for (int i = bin1; i < bin2; ++i) energy += spectrum[static_cast<std::size_t>(i)];
    energy -= spectrum[static_cast<std::size_t>(bin1)] * (r1 - bin1);
    energy -= spectrum[static_cast<std::size_t>(bin2 - 1)] * (bin2 - r2);
    return energy;
}

int peak_bin(const std::vector<double>& spectrum, SweepRange search, double bins_per_hz) {
    const double r1 = std::max(search.low * bins_per_hz, 0.0);
    const double r2 = std::min(search.high * bins_per_hz, static_cast<double>(spectrum.size()));
    const int bin1 = static_cast<int>(std::floor(r1));
    const int bin2 = static_cast<int>(std::ceil(r2));
    double maximum = spectrum[static_cast<std::size_t>(bin1)];
    int position = bin1;
    for (int i = bin1; i < bin2; ++i) {
        if (spectrum[static_cast<std::size_t>(i)] > maximum) {
            maximum = spectrum[static_cast<std::size_t>(i)];
            position = i;
        }
    }
    return position;
}

int peak_width(const std::vector<double>& spectrum, int peak, double threshold_db) {
    const double threshold = spectrum[static_cast<std::size_t>(peak)] /
        std::pow(10.0, std::abs(threshold_db) / 10.0);
    int bin1 = peak;
    int bin2 = peak;
    for (int i = peak - 1; i >= 0; --i) {
        if (spectrum[static_cast<std::size_t>(i)] >= threshold) bin1 = i;
        else break;
    }
    for (int i = peak + 1; i < static_cast<int>(spectrum.size()); ++i) {
        if (spectrum[static_cast<std::size_t>(i)] >= threshold) bin2 = i;
        else break;
    }
    return bin2 - bin1 + 1;
}

struct ZeroCrossingStats {
    int count = 0;
    double sweep_rate = 0.0;
    double start_frequency = 0.0;
    double end_frequency = 0.0;
};

ZeroCrossingStats zero_crossing_stats(const std::vector<double>& wave,
                                      const std::vector<int>& length,
                                      double sample_rate_hz) {
    std::vector<double> crossings;
    double last = -1.0;
    for (int i = length[0]; i < length[1] - 1; ++i) {
        if (wave[static_cast<std::size_t>(i)] * wave[static_cast<std::size_t>(i + 1)] > 0.0) continue;
        const double exact = i + wave[static_cast<std::size_t>(i)] /
            (wave[static_cast<std::size_t>(i)] - wave[static_cast<std::size_t>(i + 1)]);
        if (exact > last) {
            last = exact;
            crossings.push_back(exact);
        }
    }
    ZeroCrossingStats stats;
    stats.count = static_cast<int>(crossings.size());
    if (crossings.size() < 2) return stats;
    const std::size_t n = crossings.size() - 1;
    std::vector<double> frequencies(n);
    std::vector<double> times(n);
    for (std::size_t i = 0; i < n; ++i) {
        frequencies[i] = sample_rate_hz / 2.0 / (crossings[i + 1] - crossings[i]);
        times[i] = ((crossings[i + 1] + crossings[i]) / 2.0 - crossings[0]) / sample_rate_hz;
    }
    if (n == 1) {
        stats.start_frequency = stats.end_frequency = frequencies[0];
        return stats;
    }
    double mean_t = 0.0;
    double mean_f = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        mean_t += times[i];
        mean_f += frequencies[i];
    }
    mean_t /= n;
    mean_f /= n;
    double numerator = 0.0;
    double denominator = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        numerator += (times[i] - mean_t) * (frequencies[i] - mean_f);
        denominator += (times[i] - mean_t) * (times[i] - mean_t);
    }
    stats.sweep_rate = numerator / denominator;
    stats.start_frequency = mean_f - stats.sweep_rate * mean_t;
    stats.end_frequency = stats.start_frequency + stats.sweep_rate * times.back();
    return stats;
}

std::vector<double> java_correlation(const std::vector<double>& a, const std::vector<double>& b) {
    const int maximum = static_cast<int>(std::max(a.size(), b.size()));
    const int fft_length = next_binary_exp(maximum);
    const auto pa = JtFft::real_forward(a, static_cast<std::size_t>(fft_length));
    const auto pb = JtFft::real_forward(b, static_cast<std::size_t>(fft_length));
    std::vector<double> corr(static_cast<std::size_t>(fft_length) * 2, 0.0);
    for (int i = 0; i < fft_length / 2; ++i) {
        const std::complex<double> ca(pa[static_cast<std::size_t>(2 * i)],
                                      pa[static_cast<std::size_t>(2 * i + 1)]);
        const std::complex<double> cb(pb[static_cast<std::size_t>(2 * i)],
                                      pb[static_cast<std::size_t>(2 * i + 1)]);
        const auto value = ca * std::conj(cb);
        corr[static_cast<std::size_t>(2 * i)] = value.real();
        corr[static_cast<std::size_t>(2 * i + 1)] = value.imag();
        const int mirror = fft_length - i - 1;
        corr[static_cast<std::size_t>(2 * mirror)] = value.real();
        corr[static_cast<std::size_t>(2 * mirror + 1)] = -value.imag();
    }
    JtFft::complex_inverse(corr, static_cast<std::size_t>(fft_length), false);
    int length = maximum;
    if (length % 2 != 0) ++length;
    std::vector<double> result(static_cast<std::size_t>(length), 0.0);
    for (int i = 0; i < length / 2; ++i) {
        result[static_cast<std::size_t>(i + length / 2)] = corr[static_cast<std::size_t>(2 * i)];
        result[static_cast<std::size_t>(length / 2 - i - 1)] =
            corr[static_cast<std::size_t>(2 * (fft_length - 1 - i))];
    }
    return result;
}

double rotation_corrected_peak(const std::vector<double>& wave) {
    if (wave.empty()) return 0.0;
    if (wave.size() == 1) return 0.0;
    const double slope = (wave.back() - wave.front()) / static_cast<double>(wave.size() - 1);
    double peak = 0.0;
    for (std::size_t i = 0; i < wave.size(); ++i) {
        peak = std::max(peak, std::abs(wave[i] - (wave.front() + slope * i)));
    }
    return peak;
}

bool within(double value, SweepRange range) {
    return value >= range.low && value <= range.high;
}

} // namespace

SweepClickTypeConfig standard_sweep_click_type(int species_code, BasicClickStandardType standard_type) {
    SweepClickTypeConfig type;
    type.species_code = species_code;
    if (standard_type == BasicClickStandardType::BeakedWhale) {
        type.name = "Beaked Whale";
        type.enable_length = false;
        type.length_ms = {0.1, 0.5};
        type.enable_energy_bands = true;
        type.test_energy_band_hz = {24000.0, 48000.0};
        type.control_energy_band_0_hz = {12000.0, 24000.0};
        type.control_energy_band_1_hz = {12000.0, 24000.0};
        type.energy_threshold_0_db = 3.0;
        type.energy_threshold_1_db = 3.0;
        type.enable_peak = true;
        type.peak_search_range_hz = {10000.0, 96000.0};
        type.peak_range_hz = {25000.0, 48000.0};
        type.enable_mean = true;
        type.mean_range_hz = {25000.0, 48000.0};
        type.enable_zero_crossings = true;
        type.zero_crossing_count = {7.0, 50.0};
        // Java writes zcSweep but leaves enableSweep false.
        type.zero_crossing_sweep_khz_per_ms = {1.0, 500.0};
    }
    else {
        type.name = "Porpoise";
        type.enable_length = true;
        type.length_ms = {0.03, 0.22};
        type.length_db = 6.0;
        type.enable_energy_bands = true;
        type.test_energy_band_hz = {100000.0, 150000.0};
        type.control_energy_band_0_hz = {40000.0, 90000.0};
        type.control_energy_band_1_hz = {160000.0, 190000.0};
        type.energy_threshold_0_db = 6.0;
        type.energy_threshold_1_db = 6.0;
        type.enable_peak = true;
        type.peak_search_range_hz = {40000.0, 240000.0};
        type.peak_range_hz = {100000.0, 150000.0};
        type.enable_zero_crossings = true;
        type.zero_crossing_count = {10.0, 50.0};
        type.zero_crossing_sweep_khz_per_ms = {-200.0, 200.0};
    }
    return type;
}

SweepClickClassifier::SweepClickClassifier(SweepClickClassifierConfig config)
    : config_(std::move(config)) {
    if (config_.sample_rate_hz <= 0.0) {
        throw std::invalid_argument("sweep click classifier sample_rate_hz must be positive");
    }
    for (const auto& type : config_.click_types) {
        if (type.restricted_bins <= 0) {
            throw std::invalid_argument("sweep click classifier restricted_bins must be positive");
        }
        if (type.length_smoothing <= 0 || type.peak_smoothing <= 0) {
            throw std::invalid_argument("sweep click classifier smoothing must be positive");
        }
    }
}

const SweepClickClassifierConfig& SweepClickClassifier::config() const noexcept {
    return config_;
}

ClickClassificationResult SweepClickClassifier::identify(const ClickDetectionResult& click) const {
    ClickClassificationResult result;
    result.click_start_sample = click.start_sample;
    for (const auto& type : config_.click_types) {
        if (!type.enabled || !classify(click, type)) continue;
        if (!config_.check_all_classifiers) {
            result.click_type = type.species_code;
            result.discard = type.discard;
            return result;
        }
        if (result.classifiers_passed.empty()) {
            result.click_type = type.species_code;
            result.discard = type.discard;
        }
        result.classifiers_passed.push_back(type.species_code);
    }
    return result;
}

bool SweepClickClassifier::classify(const ClickDetectionResult& click,
                                    const SweepClickTypeConfig& type) const {
    if (click.waveform.empty()) return false;
    const auto wave = wave_data(click, type, config_.sample_rate_hz);
    const auto lengths = length_data(wave, type.length_db, type.length_smoothing);
    if (lengths.empty()) return false;
    const std::size_t channels = wave.size();

    if (type.enable_length) {
        const bool pass = channel_test(type.channel_choice, channels, [&](std::size_t channel) {
            const double milliseconds =
                (lengths[channel][1] - lengths[channel][0]) / config_.sample_rate_hz * 1000.0;
            return within(milliseconds, type.length_ms);
        });
        if (!pass) return false;
    }

    if (type.test_amplitude) {
        std::vector<double> amplitudes(channels, 0.0);
        for (std::size_t channel = 0; channel < channels; ++channel) {
            // PamDataUnit.linAmplitudeToDB asks acquisition calibration for
            // the bitmap's lowest physical channel, even while Sweep loops
            // over per-channel peak amplitudes.
            const std::size_t lowest_physical = click.channels.empty()
                ? 0 : *std::min_element(click.channels.begin(), click.channels.end());
            const double offset = lowest_physical < config_.amplitude_db_offset_by_channel.size()
                ? config_.amplitude_db_offset_by_channel[lowest_physical] : 0.0;
            // ClickDetection.calculateAmplitude runs when the click is created,
            // before a Sweep set's optional FFT filter is considered.
            amplitudes[channel] =
                20.0 * std::log10(rotation_corrected_peak(click.waveform[channel])) + offset;
        }
        if (type.channel_choice == SweepChannelChoice::UseMeans) {
            double mean = 0.0;
            for (double value : amplitudes) mean += value;
            mean /= amplitudes.size();
            if (!within(mean, type.amplitude_range_db)) return false;
        }
        else if (!channel_test(type.channel_choice, channels, [&](std::size_t channel) {
            return within(amplitudes[channel], type.amplitude_range_db);
        })) {
            return false;
        }
    }

    const bool needs_spectrum = type.enable_energy_bands || type.enable_peak ||
        type.enable_width || type.enable_mean;
    std::vector<std::vector<double>> spec;
    if (needs_spectrum) {
        // The ordinary ClickDetection power spectrum is unfiltered; the set's
        // optional FFT filter only feeds Sweep's restricted-length spectrum.
        spec = spectra(type.restrict_length ? wave : click.waveform, lengths, type);
    }

    if (type.enable_energy_bands) {
        std::vector<double> test(spec.size());
        std::vector<double> control0(spec.size());
        std::vector<double> control1(spec.size());
        for (std::size_t channel = 0; channel < spec.size(); ++channel) {
            test[channel] = 10.0 * std::log10(
                pick_spec_energy(spec[channel], type.test_energy_band_hz, config_.sample_rate_hz));
            control0[channel] = 10.0 * std::log10(
                pick_spec_energy(spec[channel], type.control_energy_band_0_hz, config_.sample_rate_hz));
            control1[channel] = 10.0 * std::log10(
                pick_spec_energy(spec[channel], type.control_energy_band_1_hz, config_.sample_rate_hz));
        }
        const bool pass = channel_test(type.channel_choice, spec.size(), [&](std::size_t channel) {
            return test[channel] - control0[channel] >= type.energy_threshold_0_db &&
                   test[channel] - control1[channel] >= type.energy_threshold_1_db;
        });
        if (!pass) return false;
    }

    std::vector<int> peaks;
    std::vector<std::vector<double>> smoothed;
    if (type.enable_peak || type.enable_width) {
        smoothed.reserve(spec.size());
        peaks.reserve(spec.size());
        const double bins_per_hz = spec[0].size() * 2.0 / config_.sample_rate_hz;
        for (const auto& channel : spec) {
            smoothed.push_back(smooth_data(channel, type.peak_smoothing));
            peaks.push_back(peak_bin(smoothed.back(), type.peak_search_range_hz, bins_per_hz));
        }
        if (type.enable_peak) {
            const bool pass = channel_test(type.channel_choice, spec.size(), [&](std::size_t channel) {
                return within(peaks[channel] / bins_per_hz, type.peak_range_hz);
            });
            if (!pass) return false;
        }
        if (type.enable_width) {
            const int f1 = static_cast<int>(type.peak_width_range_hz.low * bins_per_hz);
            const int f2 = static_cast<int>(type.peak_width_range_hz.high * bins_per_hz);
            const bool pass = channel_test(type.channel_choice, spec.size(), [&](std::size_t channel) {
                const int width = peak_width(
                    smoothed[channel], peaks[channel], type.peak_width_threshold_db);
                return width >= f1 && width <= f2;
            });
            if (!pass) return false;
        }
    }

    if (type.enable_mean) {
        const double bins_per_hz = spec[0].size() * 2.0 / config_.sample_rate_hz;
        const int bin1 = static_cast<int>(std::floor(std::max(
            type.peak_search_range_hz.low * bins_per_hz, 0.0)));
        const int bin2 = static_cast<int>(std::ceil(std::min(
            type.peak_search_range_hz.high * bins_per_hz, static_cast<double>(spec[0].size()))));
        const bool pass = channel_test(type.channel_choice, spec.size(), [&](std::size_t channel) {
            double weighted = 0.0;
            double total = 0.0;
            for (int bin = bin1; bin < bin2; ++bin) {
                weighted += bin * spec[channel][static_cast<std::size_t>(bin)];
                total += spec[channel][static_cast<std::size_t>(bin)];
            }
            return within(weighted / total / bins_per_hz, type.mean_range_hz);
        });
        if (!pass) return false;
    }

    if (type.enable_zero_crossings || type.enable_sweep) {
        std::vector<ZeroCrossingStats> stats;
        stats.reserve(channels);
        for (std::size_t channel = 0; channel < channels; ++channel) {
            stats.push_back(zero_crossing_stats(
                wave[channel], lengths[channel], config_.sample_rate_hz));
        }
        const auto test_stats = [&](const ZeroCrossingStats& value) {
            if (type.enable_zero_crossings &&
                !within(value.count, type.zero_crossing_count)) return false;
            if (type.enable_sweep &&
                !within(value.sweep_rate / 1e6, type.zero_crossing_sweep_khz_per_ms)) return false;
            return true;
        };
        if (type.channel_choice == SweepChannelChoice::UseMeans) {
            ZeroCrossingStats mean;
            for (const auto& value : stats) {
                mean.count += value.count;
                mean.sweep_rate += value.sweep_rate;
                mean.start_frequency += value.start_frequency;
                mean.end_frequency += value.end_frequency;
            }
            mean.count /= static_cast<int>(channels);
            mean.sweep_rate /= channels;
            mean.start_frequency /= channels;
            mean.end_frequency /= channels;
            if (!test_stats(mean)) return false;
        }
        else if (!channel_test(type.channel_choice, channels, [&](std::size_t channel) {
            return test_stats(stats[channel]);
        })) {
            return false;
        }
    }

    if ((type.enable_min_cross_correlation || type.enable_peak_cross_correlation) &&
        channels > 1) {
        const std::size_t pairs = channels * (channels - 1) / 2;
        std::vector<double> minimums(pairs, 0.0);
        std::vector<double> maximums(pairs, 0.0);
        std::vector<bool> peak_above_trough(pairs, false);
        for (std::size_t i = 0; i < channels; ++i) {
            for (std::size_t j = i + 1; j < channels; ++j) {
                const auto corr = java_correlation(wave[i], wave[j]);
                minimums[i] = *std::min_element(corr.begin(), corr.end());
                maximums[i] = *std::max_element(corr.begin(), corr.end());
                peak_above_trough[i] =
                    maximums[i] > type.correlation_factor * std::abs(minimums[i]);
            }
        }
        if (type.enable_min_cross_correlation &&
            *std::max_element(maximums.begin(), maximums.end()) < type.min_correlation) return false;
        if (type.enable_peak_cross_correlation &&
            std::find(peak_above_trough.begin(), peak_above_trough.end(), true) ==
                peak_above_trough.end()) return false;
    }

    if (type.enable_bearing_limits && click.bearing_radians.has_value()) {
        const bool inside = *click.bearing_radians > type.bearing_limits_radians.low &&
                            *click.bearing_radians < type.bearing_limits_radians.high;
        if (inside == type.exclude_bearing_limits) return false;
    }

    return true;
}

} // namespace pamguard::detectors
