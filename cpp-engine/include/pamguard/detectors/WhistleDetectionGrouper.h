#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace pamguard::detectors {

/** A whistle contour as the detection grouper sees it. */
struct WhistleGroupCandidate {
    /** PAMGuard matches on the sequence bitmap (beamformer-aware). */
    std::uint32_t sequence_bitmap = 0;
    std::int64_t start_sample = 0;
    std::int64_t last_sample = 0;
    std::int64_t time_ms = 0;
    double min_frequency_hz = 0.0;
    double max_frequency_hz = 0.0;
};

/**
 * PamDataUnit.getTimeOverlap: the intersection duration as a fraction of
 * *this* unit's duration, zero when the units do not overlap.
 */
[[nodiscard]] inline double whistle_time_overlap(const WhistleGroupCandidate& self,
                                                 const WhistleGroupCandidate& other) {
    if (other.last_sample < self.start_sample || other.start_sample > self.last_sample) {
        return 0.0;
    }
    const auto duration = self.last_sample - self.start_sample;
    if (duration == 0) {
        return 0.0;
    }
    const auto overlap_start = std::max(other.start_sample, self.start_sample);
    const auto overlap_end = std::min(other.last_sample, self.last_sample);
    return static_cast<double>(overlap_end - overlap_start) / static_cast<double>(duration);
}

/**
 * PamDataUnit.getFrequencyOverlap, transcribed including its asymmetry: the
 * upper bound uses `max` where an intersection would use `min` (compare
 * getTimeOverlap, which uses `min`). The result is therefore not a true
 * intersection fraction and can exceed one, which makes the grouper's 0.5
 * frequency test far easier to pass than it looks. Preserved deliberately —
 * changing it would silently alter which contours group together.
 */
[[nodiscard]] inline double whistle_frequency_overlap(const WhistleGroupCandidate& self,
                                                      const WhistleGroupCandidate& other) {
    if (other.max_frequency_hz < self.min_frequency_hz || other.min_frequency_hz > self.max_frequency_hz) {
        return 0.0;
    }
    const double span = self.max_frequency_hz - self.min_frequency_hz;
    if (span == 0.0) {
        return 0.0;
    }
    const double overlap_start = std::max(other.min_frequency_hz, self.min_frequency_hz);
    const double overlap_end = std::max(other.max_frequency_hz, self.max_frequency_hz);
    return (overlap_end - overlap_start) / span;
}

/**
 * WhistleDetectionGrouper.match: contours on the **same** sequence bitmap
 * never match (grouping is across channel groups), otherwise both the time
 * and frequency overlap — each taken as the larger of the two directional
 * overlaps — must exceed 0.5.
 */
[[nodiscard]] inline bool whistle_detections_match(const WhistleGroupCandidate& current,
                                                   const WhistleGroupCandidate& older) {
    if (current.sequence_bitmap == older.sequence_bitmap) {
        return false;
    }
    const double time_overlap = std::max(whistle_time_overlap(current, older),
                                         whistle_time_overlap(older, current));
    const double frequency_overlap = std::max(whistle_frequency_overlap(current, older),
                                              whistle_frequency_overlap(older, current));
    return frequency_overlap > 0.5 && time_overlap > 0.5;
}

/**
 * Port of DetectionGrouper.findGroups: scans recent detections newest-first
 * and collects matches for the new detection.
 *
 * Faithful quirk: the two-second cutoff `break` sits **inside** the match
 * branch in the reference, so a long run of non-matching detections does not
 * stop the scan — only a match older than two seconds does. Returns indices
 * into `recent_detections` (which must be ordered oldest first).
 */
[[nodiscard]] inline std::vector<std::size_t> find_whistle_groups(
    const WhistleGroupCandidate& detection,
    const std::vector<WhistleGroupCandidate>& recent_detections,
    std::size_t group_count) {
    std::vector<std::size_t> matches;
    if (group_count < 2) {
        return matches;
    }
    for (std::size_t i = recent_detections.size(); i-- > 0;) {
        const auto& older = recent_detections[i];
        if (whistle_detections_match(detection, older)) {
            matches.push_back(i);
            if (older.time_ms < detection.time_ms - 2000) {
                break;
            }
        }
    }
    return matches;
}

} // namespace pamguard::detectors
