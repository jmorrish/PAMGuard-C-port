#include "pamguard/detectors/ClickFeatureExtractor.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>

#include "pamguard/dsp/RealFft.h"

namespace pamguard::detectors {

namespace {

bool is_power_of_two(std::size_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

std::size_t pamguard_min_fft_length(std::size_t length) {
    std::size_t fft_length = 4;
    while (fft_length < length) {
        fft_length *= 2;
    }
    return fft_length;
}

std::vector<double> apply_click_hann_window(const std::vector<double>& waveform) {
    std::vector<double> windowed(waveform.size(), 0.0);
    if (waveform.empty()) {
        return windowed;
    }
    if (waveform.size() == 1) {
        windowed[0] = waveform[0];
        return windowed;
    }

    const double denom = static_cast<double>(waveform.size() - 1);
    for (std::size_t i = 0; i < waveform.size(); ++i) {
        windowed[i] = waveform[i] * 0.5 * (1.0 - std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / denom));
    }
    return windowed;
}

std::vector<double> pamguard_packed_magnitude_squared(const dsp::ComplexSpectrum& bins) {
    if (bins.size() < 2) {
        return {};
    }
    const auto fft_length = (bins.size() - 1) * 2;
    std::vector<double> magsq(fft_length / 2, 0.0);
    magsq[0] = bins[0].real() * bins[0].real() + bins[fft_length / 2].real() * bins[fft_length / 2].real();
    for (std::size_t i = 1; i < magsq.size(); ++i) {
        magsq[i] = std::norm(bins[i]);
    }
    return magsq;
}

std::vector<double> power_spectrum(const std::vector<double>& waveform, std::size_t fft_length) {
    auto windowed = apply_click_hann_window(waveform);
    std::vector<double> padded(fft_length, 0.0);
    const auto copy_count = std::min(windowed.size(), fft_length);
    std::copy_n(windowed.begin(), copy_count, padded.begin());

    dsp::RealFft fft;
    return pamguard_packed_magnitude_squared(fft.forward(padded, fft_length));
}

std::pair<std::size_t, std::size_t> bins_for_range(FrequencyRange range, std::size_t fft_length, double sample_rate_hz) {
    const auto half = fft_length / 2;
    if (half == 0) {
        return {0, 0};
    }

    const auto low_bin = static_cast<long long>(std::floor(range.low_hz * static_cast<double>(fft_length) / sample_rate_hz));
    const auto high_bin = static_cast<long long>(std::ceil(range.high_hz * static_cast<double>(fft_length) / sample_rate_hz));
    const auto f1 = static_cast<std::size_t>(std::max<long long>(0, low_bin));
    const auto f2 = static_cast<std::size_t>(std::min<long long>(static_cast<long long>(half - 1), high_bin));
    return {f1, f2};
}

FrequencyRange full_range_if_empty(FrequencyRange range, double sample_rate_hz) {
    if (range.high_hz <= range.low_hz) {
        return FrequencyRange{0.0, sample_rate_hz / 2.0};
    }
    return range;
}

int spike_width_fraction(const std::vector<double>& data, std::size_t peak_pos, double percent) {
    int width = 1;
    const auto len = static_cast<int>(data.size());
    if (len == 0) {
        return 0;
    }
    if (percent >= 100.0) {
        return len;
    }

    double target_energy = 0.0;
    for (double value : data) {
        target_energy += value;
    }
    target_energy *= percent / 100.0;

    double found_energy = data[peak_pos];
    int inext = static_cast<int>(peak_pos) + 1;
    int iprev = static_cast<int>(peak_pos) - 1;
    while (found_energy < target_energy) {
        double next = 0.0;
        double prev = 0.0;
        if (inext < len) {
            next = data[static_cast<std::size_t>(inext)];
        }
        if (iprev >= 0) {
            prev = data[static_cast<std::size_t>(iprev)];
        }

        if (next > prev) {
            found_energy += next;
            ++inext;
            ++width;
        }
        else if (next < prev) {
            found_energy += prev;
            --iprev;
            ++width;
        }
        else {
            found_energy += next + prev;
            ++inext;
            --iprev;
            width += 2;
        }

        if (iprev < 0 && inext >= len) {
            break;
        }
    }

    return width;
}

double click_length_channel(const std::vector<double>& waveform, double percent, double sample_rate_hz) {
    if (waveform.empty()) {
        return 0.0;
    }

    constexpr int n_average = 3;
    std::vector<double> smooth(waveform.size(), 0.0);
    for (std::size_t i = 0; i < smooth.size(); ++i) {
        smooth[i] = waveform[i] * waveform[i];
    }

    double data_maximum = 0.0;
    std::size_t max_position = 0;
    if (smooth.size() > n_average) {
        for (std::size_t i = 0; i < smooth.size() - n_average; ++i) {
            for (int j = 1; j < n_average; ++j) {
                smooth[i] += smooth[i + static_cast<std::size_t>(j)];
            }
            if (smooth[i] > data_maximum) {
                data_maximum = smooth[i];
                max_position = i;
            }
        }
    }

    const int length = spike_width_fraction(smooth, max_position, percent);
    return static_cast<double>(length) / sample_rate_hz;
}

double in_band_energy_db(const std::vector<std::vector<double>>& spectra, FrequencyRange range, std::size_t fft_length, double sample_rate_hz) {
    const auto [f1, f2] = bins_for_range(range, fft_length, sample_rate_hz);
    if (f1 > f2) {
        return -100.0;
    }

    double energy = 0.0;
    for (const auto& spectrum : spectra) {
        const auto hi = std::min(f2, spectrum.empty() ? 0 : spectrum.size() - 1);
        if (spectrum.empty() || f1 > hi) {
            continue;
        }
        for (std::size_t bin = f1; bin <= hi; ++bin) {
            energy += spectrum[bin];
        }
    }

    if (energy > 0.0) {
        return 10.0 * std::log10(energy) + 172.0;
    }
    return -100.0;
}

double peak_frequency_hz(const std::vector<double>& total_power, FrequencyRange range, std::size_t fft_length, double sample_rate_hz) {
    const auto [bin1, bin2] = bins_for_range(full_range_if_empty(range, sample_rate_hz), fft_length, sample_rate_hz);
    double peak_energy = 0.0;
    std::size_t peak_pos = 0;
    const auto hi = std::min(bin2, total_power.empty() ? 0 : total_power.size() - 1);
    if (!total_power.empty() && bin1 <= hi) {
        for (std::size_t bin = bin1; bin <= hi; ++bin) {
            if (total_power[bin] > peak_energy) {
                peak_energy = total_power[bin];
                peak_pos = bin;
            }
        }
    }
    return static_cast<double>(peak_pos) * sample_rate_hz / static_cast<double>(fft_length);
}

double peak_frequency_width_hz(const std::vector<double>& total_power, double peak_frequency, double percent, std::size_t fft_length, double sample_rate_hz) {
    if (total_power.empty()) {
        return 0.0;
    }
    auto peak_pos = static_cast<std::size_t>(peak_frequency * static_cast<double>(fft_length) / sample_rate_hz);
    peak_pos = std::min(peak_pos, total_power.size() - 1);
    const int width = spike_width_fraction(total_power, peak_pos, percent);
    return static_cast<double>(width) * sample_rate_hz / static_cast<double>(fft_length);
}

double mean_frequency_hz(const std::vector<double>& total_power, FrequencyRange range, std::size_t fft_length, double sample_rate_hz) {
    const auto [bin1, bin2] = bins_for_range(full_range_if_empty(range, sample_rate_hz), fft_length, sample_rate_hz);
    const auto hi = std::min(bin2, total_power.empty() ? 0 : total_power.size() - 1);
    double top = 0.0;
    double bottom = 0.0;
    if (!total_power.empty() && bin1 <= hi) {
        for (std::size_t bin = bin1; bin <= hi; ++bin) {
            top += static_cast<double>(bin) * total_power[bin];
            bottom += total_power[bin];
        }
    }
    if (bottom == 0.0) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return (top / bottom) * sample_rate_hz / static_cast<double>(fft_length);
}

} // namespace

ClickFeatureExtractor::ClickFeatureExtractor(ClickFeatureConfig config)
    : config_(std::move(config)) {
    if (config_.sample_rate_hz <= 0.0) {
        throw std::invalid_argument("click feature sample_rate_hz must be positive");
    }
    if (config_.fft_length != 0 && !is_power_of_two(config_.fft_length)) {
        throw std::invalid_argument("click feature fft_length must be zero or a power of two");
    }
}

const ClickFeatureConfig& ClickFeatureExtractor::config() const noexcept {
    return config_;
}

ClickFeatureResult ClickFeatureExtractor::extract(const ClickDetectionResult& click) const {
    if (click.waveform.empty()) {
        throw std::invalid_argument("click waveform is required for feature extraction");
    }

    std::size_t waveform_length = click.duration_samples;
    for (const auto& channel_waveform : click.waveform) {
        waveform_length = std::max(waveform_length, channel_waveform.size());
    }
    const auto fft_length = config_.fft_length == 0 ? pamguard_min_fft_length(waveform_length) : config_.fft_length;

    ClickFeatureResult result;
    result.click_start_sample = click.start_sample;
    result.fft_length = fft_length;
    result.total_power_spectrum.assign(fft_length / 2, 0.0);
    result.channels.reserve(click.waveform.size());

    std::vector<std::vector<double>> spectra;
    spectra.reserve(click.waveform.size());

    double length_sum = 0.0;
    for (std::size_t i = 0; i < click.waveform.size(); ++i) {
        auto spectrum = power_spectrum(click.waveform[i], fft_length);
        for (std::size_t bin = 0; bin < result.total_power_spectrum.size(); ++bin) {
            result.total_power_spectrum[bin] += spectrum[bin];
        }

        ClickChannelFeatures channel_features;
        channel_features.channel = i < click.channels.size() ? click.channels[i] : i;
        channel_features.length_seconds = click_length_channel(click.waveform[i], config_.length_energy_fraction, config_.sample_rate_hz);
        channel_features.power_spectrum = spectrum;
        length_sum += channel_features.length_seconds;

        spectra.push_back(std::move(spectrum));
        result.channels.push_back(std::move(channel_features));
    }

    result.click_length_seconds = length_sum / static_cast<double>(click.waveform.size());
    result.band_energy_db.reserve(config_.energy_bands_hz.size());
    for (const auto& band : config_.energy_bands_hz) {
        result.band_energy_db.push_back(in_band_energy_db(spectra, band, fft_length, config_.sample_rate_hz));
    }

    result.peak_frequency_hz = peak_frequency_hz(result.total_power_spectrum, config_.peak_frequency_search_hz, fft_length, config_.sample_rate_hz);
    result.peak_width_hz = peak_frequency_width_hz(result.total_power_spectrum, result.peak_frequency_hz, config_.width_energy_fraction, fft_length, config_.sample_rate_hz);
    result.mean_frequency_hz = mean_frequency_hz(result.total_power_spectrum, config_.mean_frequency_range_hz, fft_length, config_.sample_rate_hz);
    return result;
}

} // namespace pamguard::detectors
