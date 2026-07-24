#include "pamguard/core/GroupedSource.h"

#include <algorithm>
#include <map>

namespace pamguard::core {

std::vector<std::vector<std::size_t>> grouped_source_channels(
    std::uint32_t channel_bitmap,
    const std::vector<int>& group_by_channel,
    std::size_t channel_count) {
    std::map<int, std::vector<std::size_t>> grouped;
    const auto limit = std::min<std::size_t>(
        {channel_count, group_by_channel.size(), 32});
    for (std::size_t channel = 0; channel < limit; ++channel) {
        if ((channel_bitmap & (std::uint32_t{1} << channel)) == 0) {
            continue;
        }
        grouped[group_by_channel[channel]].push_back(channel);
    }
    std::vector<std::vector<std::size_t>> result;
    result.reserve(grouped.size());
    for (auto& [_, channels] : grouped) {
        result.push_back(std::move(channels));
    }
    return result;
}

} // namespace pamguard::core
