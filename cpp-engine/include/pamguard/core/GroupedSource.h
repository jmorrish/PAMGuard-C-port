#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace pamguard::core {

/**
 * PamView.dialog.GroupedSourcePanel getGroupMap/countChannelGroups/
 * getGroupChannels semantics. Groups are returned in ascending used group
 * number, and each group's channels are ascending.
 */
[[nodiscard]] std::vector<std::vector<std::size_t>> grouped_source_channels(
    std::uint32_t channel_bitmap,
    const std::vector<int>& group_by_channel,
    std::size_t channel_count);

} // namespace pamguard::core
