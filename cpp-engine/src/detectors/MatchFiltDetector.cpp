#include "pamguard/detectors/MatchFiltDetector.h"

#include <algorithm>
#include <cmath>

#include "pamguard/dsp/JtFft.h"

namespace pamguard::detectors {

namespace {

// FastFFT.nextBinaryExp.
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

} // namespace

MatchFiltDetector::MatchFiltDetector(double sample_rate_hz, const MatchFiltConfig& config)
    : config_(config) {
    if (config_.kernel.empty() || sample_rate_hz <= 0.0) {
        return;
    }
    // prepareKernel: buffer sized to the longer of 0.1 s and twice the
    // kernel, rounded up to a power of two; Java Math.round is
    // floor(x + 0.5).
    const int buf_len = std::max(
        static_cast<int>(std::floor(sample_rate_hz * 0.1 + 0.5)),
        static_cast<int>(config_.kernel.size()) * 2);
    fft_length_ = static_cast<std::size_t>(next_binary_exp(buf_len));
    complex_kernel_ = dsp::JtFft::real_forward(config_.kernel, fft_length_);
    useful_samples_ = fft_length_ - config_.kernel.size();
    norm_const_ = 0.0;
    for (const double value : config_.kernel) {
        norm_const_ += std::pow(value, 2);
    }
}

MatchFiltDetector::ChannelDetector& MatchFiltDetector::channel_detector(std::size_t channel) {
    if (channel >= channels_.size()) {
        channels_.resize(channel + 1);
    }
    auto& chan = channels_[channel];
    if (chan.data_buffer.empty()) {
        chan.data_buffer.assign(fft_length_, 0.0);
        chan.buffer_index = 0;
    }
    return chan;
}

MatchFiltDetector::FnBlock MatchFiltDetector::process_buffer(ChannelDetector& chan) {
    // processBuffer: conj(data spectrum) times kernel spectrum over the
    // PACKED pairs, scaled real inverse, then the sliding-energy
    // normalisation (subtract first, add next — verbatim).
    const auto fft_data = dsp::JtFft::real_forward(chan.data_buffer, fft_length_);
    std::vector<double> xcorr_packed(fft_length_, 0.0);
    for (std::size_t re = 0; re + 1 < fft_length_; re += 2) {
        const std::size_t im = re + 1;
        xcorr_packed[re] = fft_data[re] * complex_kernel_[re] + fft_data[im] * complex_kernel_[im];
        xcorr_packed[im] = -fft_data[re] * complex_kernel_[im] + fft_data[im] * complex_kernel_[re];
    }
    const auto xcorr = dsp::JtFft::real_inverse(xcorr_packed);

    double norm2 = 0.0;
    for (std::size_t i = 0; i < config_.kernel.size(); ++i) {
        norm2 += std::pow(chan.data_buffer[i], 2);
    }

    FnBlock block;
    block.start_sample = chan.total_samples - static_cast<std::int64_t>(fft_length_);
    block.values.resize(useful_samples_, 0.0);
    for (std::size_t i = 0, j = config_.kernel.size(); i < useful_samples_; ++i, ++j) {
        block.values[i] = xcorr[i] / std::sqrt(norm_const_ * norm2);
        norm2 -= std::pow(chan.data_buffer[i], 2); // remove first
        norm2 += std::pow(chan.data_buffer[j], 2); // add next
    }
    return block;
}

std::vector<MatchFiltDetector::FnBlock> MatchFiltDetector::process(std::size_t channel,
                                                                   const std::vector<double>& samples) {
    std::vector<FnBlock> blocks;
    if (!valid()) {
        return blocks;
    }
    ChannelDetector& chan = channel_detector(channel);
    for (const double sample : samples) {
        chan.data_buffer[chan.buffer_index++] = sample;
        ++chan.total_samples;
        if (chan.buffer_index == fft_length_) {
            blocks.push_back(process_buffer(chan));
            // shuffleBuffer: keep a kernel's length of overlap.
            for (std::size_t i = 0, j = useful_samples_; j < fft_length_; ++i, ++j) {
                chan.data_buffer[i] = chan.data_buffer[j];
            }
            chan.buffer_index -= useful_samples_;
        }
    }
    return blocks;
}

} // namespace pamguard::detectors
