#include "pamguard/localisation/DelayGroupEstimator.h"

#include <algorithm>
#include <stdexcept>

namespace pamguard::localisation {

std::vector<ChannelPairDelay> DelayGroupEstimator::estimate_delays(
    const std::vector<std::vector<double>>& waveforms,
    const std::vector<double>& max_delay_samples) {
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

    const auto fft_length = next_power_of_two(waveform_length);
    const auto pair_count = (channel_count - 1) * channel_count / 2;
    if (!max_delay_samples.empty() && max_delay_samples.size() != pair_count) {
        throw std::invalid_argument("max_delay_samples must be empty or match the channel-pair count");
    }

    std::vector<ChannelPairDelay> delays;
    delays.reserve(pair_count);
    std::size_t pair_index = 0;
    for (std::size_t i = 0; i < channel_count; ++i) {
        for (std::size_t j = i + 1; j < channel_count; ++j, ++pair_index) {
            const double max_delay = max_delay_samples.empty() ? static_cast<double>(fft_length) : max_delay_samples[pair_index];
            ChannelPairDelay delay;
            delay.pair_index = pair_index;
            delay.channel_a = i;
            delay.channel_b = j;
            delay.audio_channel_a = i;
            delay.audio_channel_b = j;
            delay.geometry_constrained = !max_delay_samples.empty();
            delay.max_delay_samples = delay.geometry_constrained ? max_delay : 0.0;
            delay.delay = correlation_.estimate_delay(waveforms[i], waveforms[j], fft_length, max_delay);
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
