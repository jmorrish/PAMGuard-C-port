#include "pamguard/detectors/SgramCorrDetector.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pamguard::detectors {

namespace {

// The derivative-of-Gaussian 'Mexican hat', verbatim (the reference notes
// "should divide by sqrt(2*pi)?" and doesn't).
double hat(double x) {
    return (1.0 - x * x) * std::exp(-x * x / 2.0);
}

// PamUtils.linterp.
double linterp(double x0, double x1, double y0, double y1, double x) {
    return (x - x0) / (x1 - x0) * (y1 - y0) + y0;
}

} // namespace

SgramCorrDetector::SgramCorrDetector(double sample_rate_hz, std::size_t fft_length,
                                     std::size_t fft_hop, const SgramCorrConfig& config)
    : config_(config) {
    if (config_.segments.empty() || fft_length < 2 || fft_hop == 0 || sample_rate_hz <= 0.0) {
        return;
    }
    const double frame_rate = sample_rate_hz / static_cast<double>(fft_hop);
    make_kernel(sample_rate_hz, frame_rate, static_cast<int>(fft_length / 2));
}

void SgramCorrDetector::make_kernel(double sample_rate_hz, double frame_rate_hz, int gram_height) {
    // Segment span.
    double min_t = std::numeric_limits<double>::infinity();
    double max_t = -std::numeric_limits<double>::infinity();
    min_f_ = min_t;
    max_f_ = max_t;
    for (const auto& seg : config_.segments) {
        min_t = std::min(min_t, std::min(seg[0], seg[2]));
        max_t = std::max(max_t, std::max(seg[0], seg[2]));
        min_f_ = std::min(min_f_, std::min(seg[1], seg[3]));
        max_f_ = std::max(max_f_, std::max(seg[1], seg[3]));
    }
    // The 4 means the kernel extends 4 standard deviations out; the top
    // clamp is gramHeight/2 — HALF the spectrum — exactly as written.
    const double bin_bw = (sample_rate_hz / 2.0) / gram_height;
    int min_bin = static_cast<int>(std::floor((min_f_ - config_.spread * 4) / bin_bw));
    int max_bin = static_cast<int>(std::ceil((max_f_ + config_.spread * 4) / bin_bw));
    min_bin = std::max(min_bin, 0);
    max_bin = std::min(max_bin, gram_height / 2);
    const int n_bin = max_bin - min_bin + 1;
    const int dur_n = std::max(1, static_cast<int>(std::ceil((max_t - min_t) * frame_rate_hz)));

    kernel_.assign(static_cast<std::size_t>(dur_n),
                   std::vector<double>(static_cast<std::size_t>(n_bin), 0.0));
    bin_offset_ = min_bin;
    for (int i = 0; i < dur_n; ++i) {
        const double t = i / frame_rate_hz + min_t;
        for (int j = 0; j < n_bin; ++j) {
            const double f = (j + bin_offset_) * bin_bw;
            for (const auto& seg : config_.segments) {
                if (t >= seg[0] && t <= seg[2]) {
                    const double axis_f = linterp(seg[0], seg[2], seg[1], seg[3], t);
                    kernel_[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] +=
                        hat((f - axis_f) / config_.spread);
                }
            }
        }
    }
}

SgramCorrDetector::PerChannelInfo& SgramCorrDetector::channel_info(std::size_t channel) {
    if (channel >= channels_.size()) {
        channels_.resize(channel + 1);
    }
    auto& info = channels_[channel];
    if (info.stored_gram.empty() && !kernel_.empty()) {
        info.stored_gram.assign(kernel_.size(), std::vector<double>(kernel_[0].size(), 0.0));
    }
    return info;
}

std::optional<double> SgramCorrDetector::process_frame(std::size_t channel,
                                                       const std::vector<double>& magnitude_squared) {
    if (kernel_.empty()) {
        return std::nullopt;
    }
    PerChannelInfo& chan = channel_info(channel);

    auto& slice = chan.stored_gram[chan.slice_ix];
    for (std::size_t bin = 0, j = static_cast<std::size_t>(bin_offset_); bin < slice.size(); ++bin, ++j) {
        const double mag = j < magnitude_squared.size() ? magnitude_squared[j] : 0.0;
        slice[bin] = config_.use_log ? std::log10(std::max(1.0, mag)) : mag;
    }
    if (++chan.slice_ix >= chan.stored_gram.size()) { // circular buffer
        chan.slice_ix = 0;
    }

    if (++chan.n_frames_in >= static_cast<std::int64_t>(kernel_.size())) {
        // gramDotProd: kernel row 0 aligns with the OLDEST stored slice.
        double sum = 0.0;
        std::size_t ker_i = 0;
        for (std::size_t gram_i = chan.slice_ix; gram_i < chan.stored_gram.size(); ++gram_i, ++ker_i) {
            for (std::size_t j = 0; j < kernel_[0].size(); ++j) {
                sum += kernel_[ker_i][j] * chan.stored_gram[gram_i][j];
            }
        }
        for (std::size_t gram_i = 0; gram_i < chan.slice_ix; ++gram_i, ++ker_i) {
            for (std::size_t j = 0; j < kernel_[0].size(); ++j) {
                sum += kernel_[ker_i][j] * chan.stored_gram[gram_i][j];
            }
        }
        return sum;
    }
    return std::nullopt;
}

} // namespace pamguard::detectors
