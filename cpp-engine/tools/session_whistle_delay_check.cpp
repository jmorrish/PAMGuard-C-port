#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <numbers>
#include <optional>
#include <unordered_map>
#include <vector>

#include "pamguard/core/AnalysisSession.h"

namespace {

constexpr double sample_rate_hz = 48000.0;
constexpr std::size_t fft_length = 256;
constexpr std::size_t fft_hop = 128;
constexpr double channel_1_delay_samples = 3.0;

pamguard::core::AnalysisConfig base_config(bool with_hydrophones) {
    pamguard::core::AnalysisConfig config;
    config.session_id = "whistle-delay-check";
    config.sample_rate_hz = static_cast<std::uint32_t>(sample_rate_hz);
    config.channel_count = 2;
    config.detector.fft.fft_length = fft_length;
    config.detector.fft.fft_hop = fft_hop;
    // WhistlesAndMoans consumes the noise-reduced FFT directly; the older
    // BetterPeakDetector is an independent output and must not be required.
    config.detector.whistle_peak_detector_enabled = false;
    config.detector.whistle_region_detector_enabled = true;
    config.detector.whistle_region.min_frequency_hz =
        25.0 * sample_rate_hz / static_cast<double>(fft_length);
    config.detector.whistle_region.max_frequency_hz =
        36.0 * sample_rate_hz / static_cast<double>(fft_length);
    config.detector.whistle_noise.run_median_filter = true;
    config.detector.whistle_noise.run_average_subtraction = true;
    config.detector.whistle_noise.run_kernel_smoothing = true;
    config.detector.whistle_noise.run_threshold = true;

    config.array.speed_of_sound_mps = 1500.0;
    config.array.spacing_error_m = 0.02;
    config.array.timing_error_seconds = 1.0e-5;
    if (with_hydrophones) {
        config.array.hydrophones = {
            {0, 0.0, 0.0, 0.0, 0.0},
            {1, 0.0, 3.0, 0.0, 0.0},
        };
    }
    return config;
}

/**
 * Like synthetic_chunk, but channel 1's tone runs far past channel 0's, so the
 * two contours complete at clearly different times. Splitting the audio
 * between those completions puts one region in each chunk while keeping them
 * heavily overlapped in time, which is what the grouper matches on.
 */
pamguard::core::AudioChunk staggered_chunk() {
    constexpr std::size_t total_slices = 220;
    constexpr std::size_t frame_count = total_slices * fft_hop + fft_length;
    constexpr std::size_t tone_start_sample = 100 * fft_hop;
    constexpr std::size_t channel_0_end_sample = 130 * fft_hop;
    constexpr std::size_t channel_1_end_sample = 175 * fft_hop;

    pamguard::core::AudioChunk chunk;
    chunk.start_sample = 0;
    chunk.time_unix_ms = 0;
    chunk.sample_rate_hz = static_cast<std::uint32_t>(sample_rate_hz);
    chunk.channel_count = 2;
    chunk.interleaved_pcm.assign(frame_count * 2, 0.0);
    for (std::size_t i = 0; i < frame_count; ++i) {
        for (std::size_t channel = 0; channel < 2; ++channel) {
            double value = 0.0005 * std::sin(static_cast<double>(i) * 2.4 + static_cast<double>(channel) * 0.9);
            const std::size_t tone_end_sample = channel == 0 ? channel_0_end_sample : channel_1_end_sample;
            if (i >= tone_start_sample && i < tone_end_sample) {
                const double delayed = static_cast<double>(i) - (channel == 1 ? channel_1_delay_samples : 0.0);
                for (std::size_t bin = 29; bin <= 31; ++bin) {
                    const double frequency_hz = static_cast<double>(bin) * sample_rate_hz / static_cast<double>(fft_length);
                    value += 0.3 * std::sin(2.0 * std::numbers::pi * frequency_hz * delayed / sample_rate_hz);
                }
            }
            chunk.interleaved_pcm[i * 2 + channel] = value;
        }
    }
    return chunk;
}

pamguard::core::AudioChunk synthetic_chunk() {
    // 110 warmup slices of background, 30 slices of a three-bin tone burst
    // (bins 29-31), then 20 quiet slices so the region completes in-stream.
    constexpr std::size_t total_slices = 160;
    constexpr std::size_t frame_count = total_slices * fft_hop + fft_length;
    constexpr std::size_t tone_start_sample = 110 * fft_hop;
    constexpr std::size_t tone_end_sample = tone_start_sample + 30 * fft_hop;

    pamguard::core::AudioChunk chunk;
    chunk.start_sample = 0;
    chunk.time_unix_ms = 0;
    chunk.sample_rate_hz = static_cast<std::uint32_t>(sample_rate_hz);
    chunk.channel_count = 2;
    chunk.interleaved_pcm.assign(frame_count * 2, 0.0);
    for (std::size_t i = 0; i < frame_count; ++i) {
        for (std::size_t channel = 0; channel < 2; ++channel) {
            // Keep the background tiny and far from the burst band: a coherent
            // in-band background with an inter-channel phase offset reads as a
            // spurious pseudo-delay region.
            double value = 0.0005 * std::sin(static_cast<double>(i) * 2.4 + static_cast<double>(channel) * 0.9);
            if (i >= tone_start_sample && i < tone_end_sample) {
                const double delayed = static_cast<double>(i) - (channel == 1 ? channel_1_delay_samples : 0.0);
                for (std::size_t bin = 29; bin <= 31; ++bin) {
                    const double frequency_hz = static_cast<double>(bin) * sample_rate_hz / static_cast<double>(fft_length);
                    value += 0.3 * std::sin(2.0 * std::numbers::pi * frequency_hz * delayed / sample_rate_hz);
                }
            }
            chunk.interleaved_pcm[i * 2 + channel] = value;
        }
    }
    return chunk;
}

} // namespace

int main() {
    try {
        {
            // WhistleToneParameters + DataBlock2D.value2bin semantics:
            // truncation (not rounding), max <= 0 / >= Nyquist becomes
            // Nyquist, and ShapeConnector clamps both ends to width - 1.
            const auto defaults = pamguard::detectors::whistle_frequency_bin_range(
                0.0, 0.0, 48000.0, 256, 128);
            const auto limited = pamguard::detectors::whistle_frequency_bin_range(
                2000.0, 20000.0, 96000.0, 512, 256);
            const auto clamped = pamguard::detectors::whistle_frequency_bin_range(
                -100.0, 60000.0, 96000.0, 512, 256);
            if (defaults != std::pair<std::size_t, std::size_t>{0, 127} ||
                limited != std::pair<std::size_t, std::size_t>{10, 106} ||
                clamped != std::pair<std::size_t, std::size_t>{0, 255}) {
                std::cerr << "Whistle frequency-to-bin settings semantics diverged from PAMGuard\n";
                return 1;
            }
        }

        {
            auto config = base_config(true);
            config.detector.whistle_region.background_interval_seconds = 0.1;
            config.detector.whistle_grouping_type =
                pamguard::core::DetectorConfig::ClickGroupingType::Singles;
            pamguard::core::AnalysisSession session(config);
            const auto result = session.process(synthetic_chunk());
            if (result.whistle_backgrounds.size() < 4 ||
                result.whistle_backgrounds.size() % 2 != 0) {
                std::cerr << "Whistle background snapshots were not emitted for both channels\n";
                return 1;
            }
            for (std::size_t i = 0; i < result.whistle_backgrounds.size();
                 i += 2) {
                const auto& channel_0 = result.whistle_backgrounds[i];
                const auto& channel_1 = result.whistle_backgrounds[i + 1];
                if (channel_0.channel != 0 || channel_1.channel != 1 ||
                    channel_0.time_ms != channel_1.time_ms ||
                    channel_0.duration_ms != 100.0 ||
                    channel_0.spectrum.size() != fft_length / 2 ||
                    channel_1.spectrum.size() != fft_length / 2) {
                    std::cerr << "Whistle background snapshot metadata mismatch\n";
                    return 1;
                }
                if (!std::all_of(
                        channel_0.spectrum.begin(), channel_0.spectrum.end(),
                        [](double value) {
                            return std::isfinite(value) && value >= 0.0;
                        })) {
                    std::cerr << "Whistle background spectrum was not finite and non-negative\n";
                    return 1;
                }
            }
        }

        {
            pamguard::core::AnalysisSession session(base_config(true));
            auto result = session.process(synthetic_chunk());
            const auto flushed = session.flush();
            result.whistle_regions.insert(result.whistle_regions.end(),
                                          flushed.whistle_regions.begin(), flushed.whistle_regions.end());
            result.whistle_delays.insert(result.whistle_delays.end(),
                                         flushed.whistle_delays.begin(), flushed.whistle_delays.end());

            if (result.whistle_regions.empty()) {
                std::cerr << "Whistle delay check produced no whistle regions\n";
                return 1;
            }
            if (std::any_of(
                    result.whistle_regions.begin(),
                    result.whistle_regions.end(),
                    [](const auto& region) { return region.channel != 0; })) {
                std::cerr << "One PAMGuard all-channel group should create contours only on its first channel\n";
                return 1;
            }
            if (result.whistle_delays.empty()) {
                std::cerr << "Whistle delay check produced no whistle delays\n";
                return 1;
            }

            // Small leakage-skirt fragments at burst onset/offset can land on
            // a narrowband ambiguity lobe (pinned by the whistle-delay
            // fixture), so the delay value is asserted on each channel's
            // best-scoring region while structure is asserted on all.
            double best_delay[2] = {0.0, 0.0};
            double best_score[2] = {-1.0, -1.0};
            for (const auto& whistle_delay : result.whistle_delays) {
                if (whistle_delay.delays.size() != 1) {
                    std::cerr << "Two-channel whistle region should carry exactly one pair delay\n";
                    return 1;
                }
                const auto& delay = whistle_delay.delays.front();
                if (!delay.geometry_constrained || delay.audio_channel_a != 0 || delay.audio_channel_b != 1) {
                    std::cerr << "Whistle delay pair missing geometry constraint or channel mapping\n";
                    return 1;
                }
                if (!delay.pair_bearing_valid) {
                    std::cerr << "Whistle delay pair missing pair bearing output\n";
                    return 1;
                }
                if (!whistle_delay.bearing_valid || !whistle_delay.bearing_ambiguity ||
                    whistle_delay.bearing_pair_count != whistle_delay.delays.size() ||
                    whistle_delay.bearing_radians != delay.pair_bearing_radians) {
                    std::cerr << "Whistle region bearing summary missing or inconsistent\n";
                    return 1;
                }
                if (whistle_delay.channel < 2 && delay.delay.delay_score > best_score[whistle_delay.channel]) {
                    best_score[whistle_delay.channel] = delay.delay.delay_score;
                    best_delay[whistle_delay.channel] = delay.delay.delay_samples;
                }
            }
            for (std::size_t channel = 0; channel < 1; ++channel) {
                if (best_score[channel] < 0.0) {
                    std::cerr << "Channel " << channel << " produced no whistle delays\n";
                    return 1;
                }
                if (std::abs(best_delay[channel] - channel_1_delay_samples) > 0.5) {
                    std::cerr << "Best whistle delay mismatch on channel " << channel << ": expected about "
                              << channel_1_delay_samples << " samples, got " << best_delay[channel] << "\n";
                    return 1;
                }
            }
        }

        {
            // Four hydrophones in a tetrahedron: PAMGuard would select the
            // LSQ localiser, giving an unambiguous azimuth/elevation over
            // the full six-pair set.
            auto config = base_config(true);
            config.channel_count = 4;
            config.array.hydrophones = {
                {0, 0.0, 0.0, 0.0, 0.0},
                {1, 2.5, 0.0, 0.0, 0.0},
                {2, 0.0, 2.5, 0.0, 0.0},
                {3, 0.0, 0.0, 2.5, 0.0},
            };
            pamguard::core::AnalysisSession session(config);
            auto chunk = synthetic_chunk();
            // Widen the two-channel chunk to four channels by repeating the
            // pair, so every channel carries the same tone burst.
            pamguard::core::AudioChunk wide = chunk;
            wide.channel_count = 4;
            wide.interleaved_pcm.assign(chunk.interleaved_pcm.size() * 2, 0.0);
            const std::size_t frames = chunk.interleaved_pcm.size() / 2;
            for (std::size_t i = 0; i < frames; ++i) {
                for (std::size_t channel = 0; channel < 4; ++channel) {
                    wide.interleaved_pcm[i * 4 + channel] = chunk.interleaved_pcm[i * 2 + (channel % 2)];
                }
            }

            auto result = session.process(wide);
            const auto flushed = session.flush();
            result.whistle_delays.insert(result.whistle_delays.end(),
                                         flushed.whistle_delays.begin(), flushed.whistle_delays.end());
            if (result.whistle_delays.empty()) {
                std::cerr << "Four-channel whistle session produced no delays\n";
                return 1;
            }
            bool saw_full_pair_set = false;
            for (const auto& whistle_delay : result.whistle_delays) {
                if (whistle_delay.delays.size() == 6) {
                    saw_full_pair_set = true;
                    if (whistle_delay.lsq_bearing.valid) {
                        if (whistle_delay.bearing_ambiguity || whistle_delay.lsq_bearing.used_pairs != 6) {
                            std::cerr << "LSQ whistle bearing should be unambiguous over six pairs\n";
                            return 1;
                        }
                    }
                }
            }
            if (!saw_full_pair_set) {
                std::cerr << "Four-channel whistle regions should carry all six channel pairs\n";
                return 1;
            }
        }

        {
            // Cross-chunk grouping: the same two-channel burst fed as two
            // successive chunks must still associate contours across
            // channels, which needs the retained region history.
            auto grouping_config = base_config(true);
            grouping_config.detector.whistle_grouping_type =
                pamguard::core::DetectorConfig::ClickGroupingType::Singles;
            pamguard::core::AnalysisSession session(grouping_config);
            auto first = session.process(synthetic_chunk());
            auto second_chunk = synthetic_chunk();
            const auto frames = second_chunk.interleaved_pcm.size() / second_chunk.channel_count;
            second_chunk.start_sample = static_cast<std::int64_t>(frames);
            second_chunk.time_unix_ms = static_cast<std::int64_t>(frames) / 48;
            auto second = session.process(second_chunk);
            const auto flushed = session.flush();

            std::size_t total_groups = first.whistle_groups.size() + second.whistle_groups.size()
                + flushed.whistle_groups.size();
            if (total_groups == 0) {
                std::cerr << "Two-channel whistle session should associate contours across channels\n";
                return 1;
            }
            std::vector<pamguard::core::WhistleRegionGroup> all_groups;
            all_groups.insert(all_groups.end(), first.whistle_groups.begin(), first.whistle_groups.end());
            all_groups.insert(all_groups.end(), second.whistle_groups.begin(), second.whistle_groups.end());
            all_groups.insert(all_groups.end(), flushed.whistle_groups.begin(), flushed.whistle_groups.end());
            for (const auto& group : all_groups) {
                if (group.channels.size() < 2) {
                    std::cerr << "A whistle group should span at least two channels\n";
                    return 1;
                }
                // Channels are a set: a group must not list the same channel
                // twice however many of its contours came from that channel.
                auto sorted_channels = group.channels;
                std::sort(sorted_channels.begin(), sorted_channels.end());
                if (std::adjacent_find(sorted_channels.begin(), sorted_channels.end()) != sorted_channels.end()) {
                    std::cerr << "A whistle group listed a channel more than once\n";
                    return 1;
                }
                // Every index must address this chunk's region list, and the
                // members that do not appear there must be accounted for.
                if (group.region_indices.empty() && group.earlier_region_count == 0) {
                    std::cerr << "A whistle group reported no members at all\n";
                    return 1;
                }
                if (group.first_start_sample > group.last_start_sample) {
                    std::cerr << "A whistle group's first sample should not follow its last\n";
                    return 1;
                }
            }

            // Group ids must not be reused across chunks for unrelated groups.
            std::unordered_map<std::size_t, std::int64_t> first_seen;
            for (const auto& result : {first, second, flushed}) {
                for (const auto& group : result.whistle_groups) {
                    const auto seen = first_seen.find(group.group_id);
                    if (seen == first_seen.end()) {
                        first_seen.emplace(group.group_id, group.first_start_sample);
                        continue;
                    }
                    // A group re-reported in a later chunk keeps the first
                    // sample it had when formed rather than adopting its
                    // newest member's.
                    if (group.first_start_sample > seen->second) {
                        std::cerr << "Group " << group.group_id << " moved its first sample forward from "
                                  << seen->second << " to " << group.first_start_sample
                                  << " when re-reported in a later chunk\n";
                        return 1;
                    }
                }
            }
        }

        {
            // The real cross-chunk case. Grouping needs contours that overlap
            // in **time**, so two successive bursts never group with each
            // other; what spans a chunk boundary is one call whose contours
            // finish in different chunks. Channel 1's tone runs well past
            // channel 0's, and the split falls between the two completions, so
            // channel 0's region is retained history by the time channel 1's
            // region appears and forms the group.
            auto staggered = staggered_chunk();
            const auto split_frame = static_cast<std::size_t>(150 * fft_hop);
            pamguard::core::AudioChunk head = staggered;
            pamguard::core::AudioChunk tail = staggered;
            head.interleaved_pcm.assign(staggered.interleaved_pcm.begin(),
                                        staggered.interleaved_pcm.begin() + static_cast<std::ptrdiff_t>(split_frame * 2));
            tail.interleaved_pcm.assign(staggered.interleaved_pcm.begin() + static_cast<std::ptrdiff_t>(split_frame * 2),
                                        staggered.interleaved_pcm.end());
            tail.start_sample = static_cast<std::int64_t>(split_frame);
            tail.time_unix_ms = static_cast<std::int64_t>(split_frame) / 48;

            auto grouping_config = base_config(true);
            grouping_config.detector.whistle_grouping_type =
                pamguard::core::DetectorConfig::ClickGroupingType::Singles;
            pamguard::core::AnalysisSession session(grouping_config);
            const auto head_result = session.process(head);
            const auto tail_result = session.process(tail);
            const auto tail_flush = session.flush();

            std::optional<pamguard::core::WhistleRegionGroup> carried;
            for (const auto* result : {&tail_result, &tail_flush}) {
                for (const auto& group : result->whistle_groups) {
                    if (group.earlier_region_count > 0) {
                        carried = group;
                    }
                }
            }
            if (!carried.has_value()) {
                std::cerr << "A contour completing after a chunk boundary should group with one "
                             "retained from before it, reporting earlierRegionCount > 0\n";
                return 1;
            }
            if (carried->channels.size() < 2) {
                std::cerr << "A cross-chunk group should still span two channels\n";
                return 1;
            }
            // Its first sample belongs to the earlier-chunk member, so it must
            // precede the chunk boundary even though the group was formed
            // after it.
            if (carried->first_start_sample >= static_cast<std::int64_t>(split_frame)) {
                std::cerr << "A cross-chunk group's first sample should come from its earlier-chunk "
                             "member, got " << carried->first_start_sample << " with the split at "
                          << split_frame << "\n";
                return 1;
            }
            (void)head_result;
        }

        {
            // One contour starts in the head chunk and ends in the tail. The
            // FFT and region state must remain continuous, so nothing closes
            // in the head and the eventual contour keeps its true pre-split
            // start sample.
            auto whole = synthetic_chunk();
            constexpr std::size_t split_frame = 125 * fft_hop;
            pamguard::core::AudioChunk head = whole;
            pamguard::core::AudioChunk tail = whole;
            head.interleaved_pcm.assign(
                whole.interleaved_pcm.begin(),
                whole.interleaved_pcm.begin() + static_cast<std::ptrdiff_t>(split_frame * 2));
            tail.interleaved_pcm.assign(
                whole.interleaved_pcm.begin() + static_cast<std::ptrdiff_t>(split_frame * 2),
                whole.interleaved_pcm.end());
            tail.start_sample = static_cast<std::int64_t>(split_frame);
            tail.time_unix_ms = static_cast<std::int64_t>(split_frame) / 48;

            pamguard::core::AnalysisSession session(base_config(false));
            const auto head_result = session.process(head);
            auto tail_result = session.process(tail);
            auto flushed = session.flush();
            if (!head_result.whistle_regions.empty()) {
                std::cerr << "An in-progress contour must not close at an audio chunk boundary\n";
                return 1;
            }
            tail_result.whistle_regions.insert(
                tail_result.whistle_regions.end(),
                flushed.whistle_regions.begin(), flushed.whistle_regions.end());
            if (tail_result.whistle_regions.empty()) {
                std::cerr << "A contour crossing an audio chunk boundary was lost\n";
                return 1;
            }
            if (std::none_of(
                    tail_result.whistle_regions.begin(), tail_result.whistle_regions.end(),
                    [](const auto& region) {
                        return region.start_sample < static_cast<std::int64_t>(split_frame);
                    })) {
                std::cerr << "A cross-chunk contour did not retain its pre-split start sample\n";
                return 1;
            }
        }

        {
            pamguard::core::AnalysisSession session(base_config(false));
            auto result = session.process(synthetic_chunk());
            const auto flushed = session.flush();
            result.whistle_regions.insert(result.whistle_regions.end(),
                                          flushed.whistle_regions.begin(), flushed.whistle_regions.end());
            result.whistle_delays.insert(result.whistle_delays.end(),
                                         flushed.whistle_delays.begin(), flushed.whistle_delays.end());
            if (result.whistle_regions.empty()) {
                std::cerr << "Geometry-free whistle session should still detect regions\n";
                return 1;
            }
            if (!result.whistle_delays.empty()) {
                std::cerr << "Whistle delays should require hydrophone geometry\n";
                return 1;
            }
        }

        {
            // Noise reduction sits in the whistle path: an absurd threshold
            // zeroes every bin before the peak detector, so the same signal
            // that produced regions above now produces none. This pins the
            // reducer's placement, not its maths (the parity fixture does
            // that).
            auto muted = base_config(true);
            muted.detector.whistle_noise.run_threshold = true;
            muted.detector.whistle_noise.threshold_db = 200.0;
            muted.detector.whistle_noise.threshold_final_output =
                pamguard::detectors::SpectrogramNoiseConfig::kOutputRaw;
            pamguard::core::AnalysisSession muted_session(muted);
            auto muted_result = muted_session.process(synthetic_chunk());
            const auto muted_flush = muted_session.flush();
            muted_result.whistle_regions.insert(muted_result.whistle_regions.end(),
                                                muted_flush.whistle_regions.begin(),
                                                muted_flush.whistle_regions.end());
            if (!muted_result.whistle_regions.empty()) {
                std::cerr << "A 200 dB noise threshold should mute every whistle region, got "
                          << muted_result.whistle_regions.size() << "\n";
                return 1;
            }

            // And PAMGuard's default threshold leaves the signal detectable, so the
            // muting above is the threshold at work rather than a broken path.
            auto open_config = base_config(true);
            open_config.detector.whistle_noise.run_threshold = true;
            open_config.detector.whistle_noise.threshold_db = 8.0;
            open_config.detector.whistle_noise.threshold_final_output =
                pamguard::detectors::SpectrogramNoiseConfig::kOutputRaw;
            pamguard::core::AnalysisSession open_session(open_config);
            auto open_result = open_session.process(synthetic_chunk());
            const auto open_flush = open_session.flush();
            open_result.whistle_regions.insert(open_result.whistle_regions.end(),
                                               open_flush.whistle_regions.begin(),
                                               open_flush.whistle_regions.end());
            if (open_result.whistle_regions.empty()) {
                std::cerr << "PAMGuard's default 8 dB noise threshold should leave whistle regions detectable\n";
                return 1;
            }
        }

        std::cout << "Session whistle delay wiring passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
