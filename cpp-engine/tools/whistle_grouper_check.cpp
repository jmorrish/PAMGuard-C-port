#include <cmath>
#include <iostream>
#include <vector>

#include "pamguard/detectors/WhistleDetectionGrouper.h"

namespace {

using pamguard::detectors::find_whistle_groups;
using pamguard::detectors::whistle_detections_match;
using pamguard::detectors::WhistleGroupCandidate;
using pamguard::detectors::whistle_frequency_overlap;
using pamguard::detectors::whistle_time_overlap;

WhistleGroupCandidate candidate(std::uint32_t bitmap, std::int64_t start, std::int64_t last,
                                double min_hz, double max_hz, std::int64_t time_ms) {
    WhistleGroupCandidate c;
    c.sequence_bitmap = bitmap;
    c.start_sample = start;
    c.last_sample = last;
    c.time_ms = time_ms;
    c.min_frequency_hz = min_hz;
    c.max_frequency_hz = max_hz;
    return c;
}

} // namespace

int main() {
    try {
        const auto base = candidate(0x1, 1000, 2000, 5000.0, 9000.0, 1000);

        // Same sequence bitmap never matches: grouping is across groups.
        if (whistle_detections_match(base, candidate(0x1, 1000, 2000, 5000.0, 9000.0, 1000))) {
            std::cerr << "Detections on the same sequence bitmap should not group\n";
            return 1;
        }

        // A near-identical contour on another group matches.
        if (!whistle_detections_match(base, candidate(0x2, 1010, 1990, 5100.0, 8900.0, 1000))) {
            std::cerr << "Overlapping contours on different groups should group\n";
            return 1;
        }

        // Disjoint in time does not match, however similar in frequency.
        if (whistle_detections_match(base, candidate(0x2, 5000, 6000, 5000.0, 9000.0, 1100))) {
            std::cerr << "Time-disjoint contours should not group\n";
            return 1;
        }

        // Disjoint in frequency does not match, however aligned in time.
        if (whistle_detections_match(base, candidate(0x2, 1000, 2000, 20000.0, 24000.0, 1000))) {
            std::cerr << "Frequency-disjoint contours should not group\n";
            return 1;
        }

        // Non-overlapping ranges give zero overlap in both dimensions.
        if (whistle_time_overlap(base, candidate(0x2, 5000, 6000, 5000.0, 9000.0, 1100)) != 0.0 ||
            whistle_frequency_overlap(base, candidate(0x2, 1000, 2000, 20000.0, 24000.0, 1000)) != 0.0) {
            std::cerr << "Disjoint ranges should report zero overlap\n";
            return 1;
        }

        // The reference frequency overlap uses max for the upper bound, so a
        // wider partner yields a value above one rather than a true
        // intersection fraction. Pinned so the quirk cannot drift silently.
        const double wide = whistle_frequency_overlap(base, candidate(0x2, 1000, 2000, 6000.0, 20000.0, 1000));
        if (!(wide > 1.0)) {
            std::cerr << "Reference frequency overlap should exceed one for a wider partner, got " << wide << "\n";
            return 1;
        }

        // Fewer than two groups disables grouping entirely.
        const std::vector<WhistleGroupCandidate> recent{
            candidate(0x2, 1010, 1990, 5100.0, 8900.0, 1000),
        };
        if (!find_whistle_groups(base, recent, 1).empty()) {
            std::cerr << "Grouping should be disabled below two channel groups\n";
            return 1;
        }
        if (find_whistle_groups(base, recent, 2).size() != 1) {
            std::cerr << "A single matching recent detection should be grouped\n";
            return 1;
        }

        // Scan is newest-first, and the two-second cutoff only breaks after a
        // match: an old match stops the scan before an even older one.
        const std::vector<WhistleGroupCandidate> history{
            candidate(0x2, 1010, 1990, 5100.0, 8900.0, -50000),
            candidate(0x4, 1010, 1990, 5100.0, 8900.0, -40000),
            candidate(0x2, 1010, 1990, 5100.0, 8900.0, 1000),
        };
        const auto matches = find_whistle_groups(base, history, 3);
        if (matches.size() != 2 || matches[0] != 2 || matches[1] != 1) {
            std::cerr << "Expected newest-first matching to stop after the first old match, got "
                      << matches.size() << " matches\n";
            return 1;
        }

        std::cout << "Whistle detection grouper coverage passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
