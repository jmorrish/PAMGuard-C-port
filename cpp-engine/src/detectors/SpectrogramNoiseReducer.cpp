#include "pamguard/detectors/SpectrogramNoiseReducer.h"

#include <algorithm>
#include <cmath>

namespace pamguard::detectors {

namespace {

double magsq(const std::complex<double>& value) {
    return value.real() * value.real() + value.imag() * value.imag();
}

/** The normalised Gaussian kernel KernelSmoothing hard-codes. */
constexpr double kKernel[3][3] = {
    {1.0 / 16.0, 2.0 / 16.0, 1.0 / 16.0},
    {2.0 / 16.0, 4.0 / 16.0, 2.0 / 16.0},
    {1.0 / 16.0, 2.0 / 16.0, 1.0 / 16.0},
};

} // namespace

void pamguard_median_filter(const std::vector<double>& input, std::vector<double>& output, int filter_length) {
    const int data_length = static_cast<int>(input.size());
    output.assign(input.size(), 0.0);
    if (data_length == 0) {
        return;
    }
    int N = filter_length;
    if (N % 2 == 0) {
        ++N;
    }
    const int half = (N - 1) / 2;

    std::vector<int> indices(static_cast<std::size_t>(N) + 2, 0);
    std::vector<double> sorted(static_cast<std::size_t>(N) + 2, 0.0);
    std::vector<double> padded(static_cast<std::size_t>(2 * half + data_length + 1), 0.0);

    // Pad with the beginning and end of the real data, not zeros.
    for (int k = 0; k < data_length; ++k) {
        padded[static_cast<std::size_t>(k + half)] = input[static_cast<std::size_t>(k)];
    }
    for (int k = 0; k < half; ++k) {
        padded[static_cast<std::size_t>(k)] = input[static_cast<std::size_t>(k)];
        padded[padded.size() - 1 - static_cast<std::size_t>(k)] = input[static_cast<std::size_t>(data_length - k - 1)];
    }

    for (int k = 0; k < N; ++k) {
        indices[static_cast<std::size_t>(k)] = k;
        sorted[static_cast<std::size_t>(k)] = padded[static_cast<std::size_t>(k)];
    }
    // Bubble sort ascending, carrying indices (the reference's bubble()).
    bool moved = true;
    while (moved) {
        moved = false;
        for (int n = 1; n < N; ++n) {
            if (sorted[static_cast<std::size_t>(n)] < sorted[static_cast<std::size_t>(n - 1)]) {
                std::swap(sorted[static_cast<std::size_t>(n)], sorted[static_cast<std::size_t>(n - 1)]);
                std::swap(indices[static_cast<std::size_t>(n)], indices[static_cast<std::size_t>(n - 1)]);
                moved = true;
            }
        }
    }
    output[0] = sorted[static_cast<std::size_t>(half)];

    for (int n = 1; n < data_length; ++n) {
        const int arg = n + N - 1;
        const double fresh = padded[static_cast<std::size_t>(arg)];
        int index = 0;
        for (index = 0; index < N; ++index) {
            if (sorted[static_cast<std::size_t>(index)] > fresh) {
                break;
            }
        }
        for (int k = N; k >= index; --k) {
            sorted[static_cast<std::size_t>(k + 1)] = sorted[static_cast<std::size_t>(k)];
            indices[static_cast<std::size_t>(k + 1)] = indices[static_cast<std::size_t>(k)];
        }
        sorted[static_cast<std::size_t>(index)] = fresh;
        indices[static_cast<std::size_t>(index)] = arg;

        int kmin = 0;
        int imin = indices[0];
        for (int k = 1; k <= N; ++k) {
            if (imin > indices[static_cast<std::size_t>(k)]) {
                kmin = k;
                imin = indices[static_cast<std::size_t>(k)];
            }
        }
        for (int k = kmin + 1; k <= N; ++k) {
            sorted[static_cast<std::size_t>(k - 1)] = sorted[static_cast<std::size_t>(k)];
            indices[static_cast<std::size_t>(k - 1)] = indices[static_cast<std::size_t>(k)];
        }
        output[static_cast<std::size_t>(n)] = sorted[static_cast<std::size_t>(half)];
    }
}

SpectrogramNoiseReducer::SpectrogramNoiseReducer(SpectrogramNoiseConfig config)
    : config_(config), power_threshold_(std::pow(10.0, config.threshold_db / 10.0)) {}

bool SpectrogramNoiseReducer::active() const noexcept {
    return config_.run_median_filter || config_.run_average_subtraction || config_.run_kernel_smoothing ||
           config_.run_threshold;
}

void SpectrogramNoiseReducer::median_filter(std::vector<std::complex<double>>& slice) const {
    if (config_.median_filter_length <= 0) {
        return;
    }
    std::vector<double> magnitudes(slice.size(), 0.0);
    for (std::size_t bin = 0; bin < slice.size(); ++bin) {
        magnitudes[bin] = std::sqrt(magsq(slice[bin]));
    }
    std::vector<double> medians;
    pamguard_median_filter(magnitudes, medians, config_.median_filter_length);
    for (std::size_t bin = 0; bin < slice.size(); ++bin) {
        if (medians[bin] > 0.0) {
            slice[bin] *= 1.0 / medians[bin];
        }
    }
}

void SpectrogramNoiseReducer::average_subtraction(ChannelState& state,
                                                  std::vector<std::complex<double>>& slice) const {
    // AverageSubtraction's fixed run-in: ten slices, doubled accumulation.
    constexpr std::size_t kRunInSlices = 10;
    constexpr double kRunInScale = 2.0;
    const double new_constant = config_.average_update_constant;
    const double old_constant = 1.0 - new_constant;

    if (state.average_log.size() != slice.size()) {
        state.average_log.assign(slice.size(), 0.0);
        state.average_total_slices = 0;
    }
    if (state.average_total_slices++ < kRunInSlices) {
        for (std::size_t bin = 0; bin < slice.size(); ++bin) {
            const double power = magsq(slice[bin]);
            if (std::isnan(power) || power == 0.0) {
                continue;
            }
            state.average_log[bin] +=
                std::log10(power) / 2.0 / static_cast<double>(kRunInSlices) * kRunInScale;
            // Faithful oddity: during run-in the reference divides the data by
            // the run-in slice count rather than by the average.
            slice[bin] *= 1.0 / static_cast<double>(kRunInSlices);
        }
    }
    else {
        for (std::size_t bin = 0; bin < slice.size(); ++bin) {
            const double power = magsq(slice[bin]);
            if (std::isnan(power) || power == 0.0) {
                continue;
            }
            // Faithful ordering: the scale divides off the average from
            // *before* this slice updated it.
            const double scale = std::pow(10.0, state.average_log[bin]);
            state.average_log[bin] = state.average_log[bin] * old_constant + new_constant * std::log10(power) / 2.0;
            slice[bin] *= 1.0 / scale;
        }
    }
}

bool SpectrogramNoiseReducer::kernel_smoothing(ChannelState& state,
                                               std::vector<std::complex<double>>& slice) const {
    constexpr std::size_t kColumns = 3;
    constexpr std::size_t kSpace = 1;
    if (state.kernel_store.size() != kColumns) {
        state.kernel_store.assign(kColumns, {});
        state.kernel_complex.assign(kColumns, {});
        state.kernel_store_valid.assign(kColumns, false);
    }
    // Shuffle columns along, newest last.
    std::rotate(state.kernel_store.begin(), state.kernel_store.begin() + 1, state.kernel_store.end());
    std::rotate(state.kernel_complex.begin(), state.kernel_complex.begin() + 1, state.kernel_complex.end());
    {
        // std::vector<bool> iterators don't satisfy std::rotate cleanly
        // everywhere; do it by value.
        const bool first = state.kernel_store_valid[0];
        state.kernel_store_valid[0] = state.kernel_store_valid[1];
        state.kernel_store_valid[1] = state.kernel_store_valid[2];
        state.kernel_store_valid[2] = first;
    }

    const std::size_t padded_length = slice.size() + 2 * kSpace;
    auto& newest = state.kernel_store.back();
    newest.assign(padded_length, 0.0);
    for (std::size_t bin = 0; bin < slice.size(); ++bin) {
        newest[bin + kSpace] = magsq(slice[bin]);
    }
    state.kernel_complex.back() = slice;
    state.kernel_store_valid.back() = true;

    // Until three columns exist, the reference returns without touching the
    // data — the slice passes through unchanged.
    if (!state.kernel_store_valid[0] || state.kernel_store[0].size() != padded_length ||
        state.kernel_complex[1].size() != slice.size()) {
        return false;
    }

    for (std::size_t bin = 0; bin < slice.size(); ++bin) {
        double total = 0.0;
        const double centre = state.kernel_store[1][bin + kSpace];
        for (std::size_t column = 0; column < kColumns; ++column) {
            for (std::size_t row = 0; row < 3; ++row) {
                total += state.kernel_store[column][bin + row] * kKernel[column][row];
            }
        }
        // Faithful: a zero centre value divides by zero, exactly as the
        // reference's Math.sqrt(dumTot / cenVal) does.
        const double factor = std::sqrt(total / centre);
        slice[bin] = state.kernel_complex[1][bin] * factor;
    }
    return true;
}

void SpectrogramNoiseReducer::threshold(std::vector<std::complex<double>>& slice) const {
    for (auto& bin : slice) {
        if (magsq(bin) < power_threshold_) {
            bin = {0.0, 0.0};
        }
        else if (config_.threshold_final_output != SpectrogramNoiseConfig::kOutputInput) {
            bin = {1.0, 0.0};
        }
    }
}

std::vector<std::complex<double>> SpectrogramNoiseReducer::process(
    std::size_t channel, const std::vector<std::complex<double>>& slice) {
    if (channels_.size() <= channel) {
        channels_.resize(channel + 1);
    }
    auto& state = channels_[channel];

    std::vector<std::complex<double>> working = slice;
    if (config_.run_median_filter) {
        median_filter(working);
    }
    if (config_.run_average_subtraction) {
        average_subtraction(state, working);
    }
    if (config_.run_kernel_smoothing) {
        kernel_smoothing(state, working);
    }
    if (config_.run_threshold) {
        threshold(working);
        if (config_.threshold_final_output == SpectrogramNoiseConfig::kOutputRaw) {
            // pickEarlierData: surviving bins carry the raw input values.
            for (std::size_t bin = 0; bin < working.size(); ++bin) {
                if (working[bin].real() > 0.0) {
                    working[bin] = slice[bin];
                }
            }
        }
    }
    return working;
}

} // namespace pamguard::detectors
