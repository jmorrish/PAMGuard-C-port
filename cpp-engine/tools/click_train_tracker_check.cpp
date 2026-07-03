#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "pamguard/detectors/ClickTrainTracker.h"

namespace {

pamguard::detectors::ClickDetectionResult click(std::int64_t start_sample, std::int64_t time_ms, std::uint32_t bitmap = 0x3) {
    pamguard::detectors::ClickDetectionResult result;
    result.channel_bitmap = bitmap;
    result.trigger_bitmap = bitmap;
    result.start_sample = start_sample;
    result.time_unix_ms = time_ms;
    return result;
}

bool close(double a, double b) {
    return std::abs(a - b) < 1e-12;
}

} // namespace

int main() {
    try {
        pamguard::detectors::ClickTrainConfig config;
        config.sample_rate_hz = 48000.0;
        config.max_ici_seconds = 0.2;
        config.min_clicks = 3;
        pamguard::detectors::ClickTrainTracker tracker(config);

        const auto summaries = tracker.process({
            click(0, 0),
            click(4800, 100),
            click(9600, 200),
            click(30000, 625),
        });

        if (summaries.size() != 2) {
            std::cerr << "Expected active and completed train summaries, got " << summaries.size() << "\n";
            return 1;
        }
        const auto& active = summaries[0];
        const auto& completed = summaries[1];
        if (active.train_id != 1 || active.completed || active.click_count != 3 ||
            active.first_start_sample != 0 || active.last_start_sample != 9600 ||
            active.click_start_samples != std::vector<std::int64_t>{0, 4800, 9600} ||
            active.click_time_ms != std::vector<std::int64_t>{0, 100, 200} ||
            active.duration_samples != 9600 ||
            !close(active.duration_seconds, 0.2) ||
            !close(active.time_span_seconds, 0.2) ||
            !close(active.min_ici_seconds, 0.1) ||
            !close(active.max_ici_seconds, 0.1) ||
            !close(active.mean_ici_seconds, 0.1) || !close(active.median_ici_seconds, 0.1) ||
            !close(active.std_ici_seconds, 0.0) ||
            !close(active.ici_cv, 0.0) ||
            !close(active.click_rate_hz, 10.0)) {
            std::cerr << "Active click train summary mismatch\n";
            return 1;
        }
        if (completed.train_id != 1 || !completed.completed || completed.click_count != 3 ||
            completed.click_start_samples != std::vector<std::int64_t>{0, 4800, 9600} ||
            completed.click_time_ms != std::vector<std::int64_t>{0, 100, 200} ||
            !close(completed.last_ici_seconds, 0.1)) {
            std::cerr << "Completed click train summary mismatch\n";
            return 1;
        }

        pamguard::detectors::ClickTrainTracker flush_tracker(config);
        const auto active_only = flush_tracker.process({
            click(0, 0),
            click(4800, 100),
            click(9600, 200),
        });
        const auto flushed = flush_tracker.flush();
        if (active_only.size() != 1 || active_only[0].completed ||
            flushed.size() != 1 || !flushed[0].completed || flushed[0].click_count != 3 ||
            flushed[0].click_start_samples != std::vector<std::int64_t>{0, 4800, 9600} ||
            flushed[0].click_time_ms != std::vector<std::int64_t>{0, 100, 200} ||
            !close(flushed[0].mean_ici_seconds, 0.1)) {
            std::cerr << "Flushed click train summary mismatch\n";
            return 1;
        }

        pamguard::detectors::ClickTrainTracker short_tracker(config);
        const auto short_summaries = short_tracker.process({
            click(0, 0),
            click(4800, 100),
        });
        const auto short_flushed = short_tracker.flush();
        if (!short_summaries.empty() || !short_flushed.empty()) {
            std::cerr << "Sub-minimum click train should not be reported\n";
            return 1;
        }

        pamguard::detectors::ClickTrainTracker reset_tracker(config);
        const auto reset_summaries = reset_tracker.process({
            click(0, 0),
            click(4800, 100),
            click(30000, 625),
            click(34800, 725),
            click(39600, 825),
        });
        if (reset_summaries.size() != 1 || reset_summaries[0].train_id != 2 ||
            reset_summaries[0].completed || reset_summaries[0].click_count != 3 ||
            reset_summaries[0].click_start_samples != std::vector<std::int64_t>{30000, 34800, 39600}) {
            std::cerr << "Gap reset train summary mismatch\n";
            return 1;
        }

        pamguard::detectors::ClickTrainTracker channel_tracker(config);
        const auto channel_summaries = channel_tracker.process({
            click(0, 0, 0x1),
            click(1000, 21, 0x2),
            click(4800, 100, 0x1),
            click(5800, 121, 0x2),
            click(9600, 200, 0x1),
            click(10600, 221, 0x2),
        });
        if (channel_summaries.size() != 2 ||
            channel_summaries[0].channel_bitmap != 0x1 ||
            channel_summaries[0].click_start_samples != std::vector<std::int64_t>{0, 4800, 9600} ||
            channel_summaries[1].channel_bitmap != 0x2 ||
            channel_summaries[1].click_start_samples != std::vector<std::int64_t>{1000, 5800, 10600}) {
            std::cerr << "Channel-isolated click train summary mismatch\n";
            return 1;
        }

        pamguard::detectors::ClickTrainTracker variable_tracker(config);
        const auto variable_summaries = variable_tracker.process({
            click(0, 0),
            click(2400, 50),
            click(9600, 200),
        });
        if (variable_summaries.size() != 1 ||
            !close(variable_summaries[0].min_ici_seconds, 0.05) ||
            !close(variable_summaries[0].max_ici_seconds, 0.15) ||
            !close(variable_summaries[0].mean_ici_seconds, 0.1) ||
            !close(variable_summaries[0].median_ici_seconds, 0.1) ||
            !close(variable_summaries[0].std_ici_seconds, 0.05) ||
            !close(variable_summaries[0].ici_cv, 0.5) ||
            !close(variable_summaries[0].click_rate_hz, 10.0)) {
            std::cerr << "Variable ICI click train metrics mismatch\n";
            return 1;
        }

        std::cout << "Click train tracker check passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
