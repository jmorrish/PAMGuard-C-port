#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "pamguard/detectors/ClickTrainTracker.h"

namespace {

pamguard::detectors::ClickDetectionResult click(
    std::int64_t start_sample,
    std::int64_t time_ms,
    std::uint32_t bitmap = 0x3,
    std::optional<double> bearing_degrees = std::nullopt) {
    pamguard::detectors::ClickDetectionResult result;
    result.channel_bitmap = bitmap;
    result.trigger_bitmap = bitmap;
    result.start_sample = start_sample;
    result.time_unix_ms = time_ms;
    if (bearing_degrees.has_value()) {
        result.bearing_radians =
            *bearing_degrees * 3.141592653589793238462643383279502884 / 180.0;
    }
    return result;
}

bool close(double a, double b) {
    return std::abs(a - b) < 1e-12;
}

} // namespace

int main() {
    try {
        /*
         * Exact ClickTrainIdParams defaults in PAMGuard 2.02.18e. The
         * runClickTrainId=false owner gate is intentionally outside this
         * low-level tracker, just as module presence/enabled state owns it in
         * the C++ runtime.
         */
        {
            const pamguard::detectors::ClickTrainConfig defaults;
            if (!close(defaults.min_ici_seconds, 0.1) ||
                !close(defaults.max_ici_seconds, 2.0) ||
                !close(defaults.max_ici_change, 1.2) ||
                !close(defaults.ok_angle_error_degrees, 1.0) ||
                !close(defaults.initial_perpendicular_distance_m, 100.0) ||
                defaults.min_clicks != 6 ||
                !close(defaults.min_angle_change_degrees, 5.0) ||
                !close(defaults.ici_update_ratio, 0.5) ||
                !close(defaults.min_update_gap_seconds, 5.0)) {
                std::cerr << "Java-authoritative click train defaults mismatch\n";
                return 1;
            }
        }

        pamguard::detectors::ClickTrainConfig config;
        config.sample_rate_hz = 48000.0;
        // Preserve the original foundation scenarios while the new focused
        // cases below pin each Java gate independently.
        config.min_ici_seconds = 0.0;
        config.max_ici_seconds = 0.2;
        config.max_ici_change = 4.0;
        config.min_clicks = 3;
        config.min_update_gap_seconds = 0.0;
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

        {
            auto java_config = config;
            java_config.min_ici_seconds = 0.1;
            java_config.max_ici_seconds = 2.0;
            java_config.max_ici_change = 1.2;
            java_config.ici_update_ratio = 0.5;
            java_config.min_clicks = 3;

            // A 50 ms click is below iciRange[0] and remains outside the
            // train. The inclusive 100 ms boundary starts it.
            pamguard::detectors::ClickTrainTracker min_ici_tracker(java_config);
            const auto min_ici_summaries = min_ici_tracker.process({
                click(0, 0),
                click(2400, 50),
                click(4800, 100),
                click(9600, 200),
            });
            if (min_ici_summaries.size() != 1 ||
                min_ici_summaries[0].click_start_samples !=
                    std::vector<std::int64_t>{0, 4800, 9600}) {
                std::cerr << "Minimum ICI gate mismatch\n";
                return 1;
            }

            /*
             * Java leaves runningICI unset for the two-click startup pair.
             * The third click establishes it at 110 ms; the fourth blends a
             * 120 ms ICI to 115 ms with iciUpdateRatio=0.5. A subsequent
             * 150 ms ICI exceeds the symmetric 1.2 ratio.
             */
            pamguard::detectors::ClickTrainTracker ratio_tracker(java_config);
            const auto ratio_open = ratio_tracker.process({
                click(0, 0),
                click(4800, 100),
                click(10080, 210),
            });
            if (ratio_open.size() != 1 ||
                !close(ratio_open[0].running_ici_seconds, 0.11)) {
                std::cerr << "Running ICI update mismatch\n";
                return 1;
            }
            const auto ratio_updated = ratio_tracker.process({
                click(15840, 330),
            });
            if (ratio_updated.size() != 1 ||
                !close(ratio_updated[0].running_ici_seconds, 0.115)) {
                std::cerr << "Running ICI blend mismatch\n";
                return 1;
            }
            const auto ratio_rejected = ratio_tracker.process({
                click(23040, 480),
            });
            if (!ratio_rejected.empty()) {
                std::cerr << "Maximum ICI-change gate accepted a rejected click\n";
                return 1;
            }
            const auto ratio_flushed = ratio_tracker.flush();
            if (ratio_flushed.size() != 1 ||
                ratio_flushed[0].click_count != 4) {
                std::cerr << "Rejected ICI-ratio click leaked into the train\n";
                return 1;
            }

            /*
             * Java gates with millisecond timestamps but updates runningICI
             * from start-sample differences. Keep the domains distinct: the
             * third click is 100 ms in samples but 115 ms in event time, so
             * it fails a 1.1 matching ratio.
             */
            auto domain_config = java_config;
            domain_config.max_ici_change = 1.1;
            pamguard::detectors::ClickTrainTracker domain_tracker(
                domain_config);
            const auto domain_open = domain_tracker.process({
                    click(0, 0),
                    click(4800, 100),
                    click(9600, 200),
                });
            if (domain_open.size() != 1 ||
                !domain_tracker.process({
                    click(14400, 315),
                }).empty()) {
                std::cerr << "Java matching/running ICI domains were conflated\n";
                return 1;
            }
            const auto domain_flushed = domain_tracker.flush();
            if (domain_flushed.size() != 1 ||
                domain_flushed[0].click_count != 3) {
                std::cerr << "Time-domain rejected click leaked into the train\n";
                return 1;
            }
        }

        {
            auto angle_config = config;
            angle_config.min_ici_seconds = 0.1;
            angle_config.max_ici_seconds = 2.0;
            // Keep ICI permissive here so this case isolates the bearing gate.
            angle_config.max_ici_change = 3.0;
            angle_config.ok_angle_error_degrees = 1.0;
            angle_config.min_angle_change_degrees = 5.0;
            angle_config.min_clicks = 3;

            /*
             * Pin Java's units defect: the startup code compares radian
             * bearings directly with the numeric, degree-labelled setting.
             * With the default 1.0, a 50 degree change (0.873 rad) passes and
             * a 60 degree change (1.047 rad) fails.
             */
            pamguard::detectors::ClickTrainTracker startup_reject_tracker(
                angle_config);
            if (!startup_reject_tracker.process({
                    click(0, 0, 0x3, 0.0),
                    click(4800, 100, 0x3, 60.0),
                }).empty() ||
                !startup_reject_tracker.flush().empty()) {
                std::cerr << "Java startup angle-units quirk mismatch\n";
                return 1;
            }

            pamguard::detectors::ClickTrainTracker angle_tracker(angle_config);
            const auto opened = angle_tracker.process({
                click(0, 0, 0x3, 0.0),
                click(4800, 100, 0x3, 50.0),
                click(9600, 200, 0x3, 52.0),
            });
            if (opened.size() != 1 ||
                !close(opened[0].bearing_span_degrees, 52.0) ||
                !opened[0].localisation_ready) {
                std::cerr << "Bearing span/minimum-angle gate mismatch\n";
                return 1;
            }

            /*
             * Once running, Java ignores okAngleError and hard-codes a
             * 2 * radians(2) rejection: five degrees fails, irrespective of
             * the degree-labelled 1.0 setting.
             */
            if (!angle_tracker.process({
                    click(14400, 300, 0x3, 57.0),
                }).empty()) {
                std::cerr << "Angle-error gate accepted a rejected click\n";
                return 1;
            }
            const auto accepted = angle_tracker.process({
                click(19200, 400, 0x3, 55.5),
            });
            if (accepted.size() != 1 ||
                accepted[0].click_count != 4 ||
                !close(accepted[0].bearing_span_degrees, 55.5) ||
                !accepted[0].localisation_ready) {
                std::cerr << "Hard-coded continuation angle gate mismatch\n";
                return 1;
            }

            /*
             * Java also stores raw min/max bearings for its TMA span (no
             * wrap). Make the degree-labelled startup numeric permissive
             * enough to admit the +/-180 crossing, then pin the resulting
             * 358 degree span.
             */
            auto wrap_config = angle_config;
            wrap_config.ok_angle_error_degrees = 7.0;
            pamguard::detectors::ClickTrainTracker wrap_tracker(wrap_config);
            const auto wrap_summary = wrap_tracker.process({
                click(0, 0, 0x3, 179.0),
                click(4800, 100, 0x3, -179.0),
                click(9600, 200, 0x3, -178.0),
            });
            if (wrap_summary.size() != 1 ||
                !close(wrap_summary[0].bearing_span_degrees, 358.0)) {
                std::cerr << "Java raw bearing-span quirk mismatch\n";
                return 1;
            }
        }

        {
            auto update_config = config;
            update_config.min_ici_seconds = 0.1;
            update_config.max_ici_seconds = 2.0;
            update_config.max_ici_change = 1.2;
            update_config.min_clicks = 2;
            update_config.min_update_gap_seconds = 5.0;

            pamguard::detectors::ClickTrainTracker update_tracker(update_config);
            const auto opened = update_tracker.process({
                click(0, 0),
                click(4800, 100),
            });
            if (opened.size() != 1 || opened[0].click_count != 2) {
                std::cerr << "Train-open update should be immediate\n";
                return 1;
            }

            std::vector<pamguard::detectors::ClickDetectionResult> continuation;
            for (std::int64_t time_ms = 200; time_ms <= 5200; time_ms += 100) {
                continuation.push_back(click(time_ms * 48, time_ms));
            }
            const auto throttled = update_tracker.process(continuation);
            if (throttled.size() != 1 ||
                throttled[0].last_time_ms != 5200 ||
                throttled[0].click_count != 53) {
                std::cerr << "Five-second active update throttle mismatch\n";
                return 1;
            }
        }

        {
            auto invalid = config;
            invalid.ici_update_ratio = 1.01;
            bool rejected = false;
            try {
                pamguard::detectors::ClickTrainTracker invalid_tracker(invalid);
                (void)invalid_tracker;
            }
            catch (const std::invalid_argument&) {
                rejected = true;
            }
            if (!rejected) {
                std::cerr << "Invalid Java click-train update ratio was accepted\n";
                return 1;
            }
        }

        std::cout << "Click train tracker check passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
