#pragma once

#include <cstdint>
#include <optional>

namespace pamguard::detectors {

/**
 * Port of PAMGuard clickDetector.echoDetection.SimpleEchoDetector — the
 * default (and only headlessly-reachable) echo detection system
 * ClickControl constructs.
 *
 * A click is an echo when its delay from the last **non-echo** click is
 * within `max_interval_seconds` (default 0.1 s, SimpleEchoParams). Three
 * details the reference makes precise, all pinned by the fixture:
 *
 * - The anchor only advances on a non-echo: a burst of echoes all measure
 *   their delay from the original click, not from each other, so a long
 *   reverberation train is entirely echoes rather than a chain where every
 *   second click escapes.
 * - The first click is never an echo, and becomes the anchor.
 * - The delay must be **non-negative**: an out-of-order click is not an echo
 *   and becomes the new anchor. The boundary is inclusive (`delay <= max`).
 *
 * PAMGuard runs one detector per channel group; the engine's click detector
 * is one group per session, so one state per session matches.
 */
class SimpleEchoDetector {
public:
    SimpleEchoDetector(double sample_rate_hz, double max_interval_seconds)
        : sample_rate_hz_(sample_rate_hz), max_interval_seconds_(max_interval_seconds) {}

    [[nodiscard]] bool is_echo(std::int64_t start_sample) {
        if (!previous_start_sample_.has_value()) {
            previous_start_sample_ = start_sample;
            return false;
        }
        const double delay_seconds =
            static_cast<double>(start_sample - *previous_start_sample_) / sample_rate_hz_;
        const bool echo = delay_seconds >= 0.0 && delay_seconds <= max_interval_seconds_;
        if (!echo) {
            previous_start_sample_ = start_sample;
        }
        return echo;
    }

private:
    double sample_rate_hz_;
    double max_interval_seconds_;
    std::optional<std::int64_t> previous_start_sample_;
};

} // namespace pamguard::detectors
