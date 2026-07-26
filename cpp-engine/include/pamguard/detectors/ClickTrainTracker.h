#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "pamguard/detectors/ClickDetectorEngine.h"

namespace pamguard::detectors {

struct ClickTrainConfig {
    double sample_rate_hz = 0.0;
    /**
     * ClickTrainIdParams.iciRange, in seconds. PAMGuard requires the first
     * usable ICI to lie inside this inclusive range. Once a train has a
     * running ICI, max_ici_change is the tighter continuation gate.
     */
    double min_ici_seconds = 0.1;
    double max_ici_seconds = 2.0;
    /** Maximum of newICI/runningICI and runningICI/newICI. */
    double max_ici_change = 1.2;
    /**
     * ClickTrainIdParams.okAngleError is presented and persisted in degrees.
     * Java's startup comparison omits its degrees-to-radians conversion and
     * its continuation path instead hard-codes a four-degree rejection. The
     * tracker intentionally preserves both runtime quirks for output parity.
     */
    double ok_angle_error_degrees = 1.0;
    /**
     * Initial perpendicular distance for train localisation, in metres.
     * PAMGuard 2.02.18e persists the value but its current ClickTrainDetector
     * does not read it while matching clicks.
     */
    double initial_perpendicular_distance_m = 100.0;
    std::size_t min_clicks = 6;
    /** Minimum bearing span before target-motion localisation, in degrees. */
    double min_angle_change_degrees = 5.0;
    /** Exponential running-ICI update weight: 0 keeps, 1 replaces. */
    double ici_update_ratio = 0.5;
    /**
     * Minimum interval between active localisation/data updates, in seconds.
     * This is not the train-closing gap; max_ici_seconds closes a train.
     */
    double min_update_gap_seconds = 5.0;
};

struct ClickTrainSummary {
    std::size_t train_id = 0;
    std::uint32_t channel_bitmap = 0;
    std::int64_t first_start_sample = 0;
    std::int64_t last_start_sample = 0;
    std::int64_t first_time_ms = 0;
    std::int64_t last_time_ms = 0;
    std::vector<std::int64_t> click_start_samples;
    std::vector<std::int64_t> click_time_ms;
    std::size_t click_count = 0;
    std::int64_t duration_samples = 0;
    double duration_seconds = 0.0;
    double time_span_seconds = 0.0;
    double last_ici_seconds = 0.0;
    double min_ici_seconds = 0.0;
    double max_ici_seconds = 0.0;
    double mean_ici_seconds = 0.0;
    double median_ici_seconds = 0.0;
    double std_ici_seconds = 0.0;
    double ici_cv = 0.0;
    double click_rate_hz = 0.0;
    /** Exponentially updated ICI used by the Java continuation-ratio gate. */
    double running_ici_seconds = 0.0;
    /** Java's raw max-minus-min bearing span across clicks carrying bearings. */
    double bearing_span_degrees = 0.0;
    /** Whether the Java min-angle gate permits target-motion localisation. */
    bool localisation_ready = false;
    bool completed = false;
};

class ClickTrainTracker {
public:
    explicit ClickTrainTracker(ClickTrainConfig config);

    [[nodiscard]] const ClickTrainConfig& config() const noexcept;
    std::vector<ClickTrainSummary> process(const std::vector<ClickDetectionResult>& clicks);
    std::vector<ClickTrainSummary> flush();
    void reset();

private:
    struct ActiveTrain {
        std::size_t train_id = 0;
        std::uint32_t channel_bitmap = 0;
        std::vector<std::int64_t> start_samples;
        std::vector<std::int64_t> time_ms;
        std::vector<double> ici_seconds;
        double running_ici_seconds = -1.0;
        double last_bearing_radians = 0.0;
        double min_bearing_radians = 0.0;
        double max_bearing_radians = 0.0;
        bool has_bearing = false;
        std::int64_t last_report_time_ms = 0;
        std::size_t last_reported_click_count = 0;
    };

    ClickTrainConfig config_;
    std::unordered_map<std::uint32_t, ActiveTrain> active_trains_;
    std::size_t next_train_id_ = 1;

    /** ClickTrainDetection.addSubDetection running-ICI/sample-domain value. */
    [[nodiscard]] double calculate_ici_seconds(
        const ClickDetectionResult& previous,
        const ClickDetectionResult& current) const;
    /** ClickTrainDetection.getICI matching/time-millisecond-domain value. */
    [[nodiscard]] static double calculate_matching_ici_seconds(
        const ClickDetectionResult& previous,
        const ClickDetectionResult& current);
    [[nodiscard]] ClickTrainSummary summarize(const ActiveTrain& train, bool completed) const;
    [[nodiscard]] ActiveTrain start_train(const ClickDetectionResult& click);
    void append_click(ActiveTrain& train, const ClickDetectionResult& click, double ici_seconds);
    [[nodiscard]] bool passes_continuation_gates(
        const ActiveTrain& train,
        const ClickDetectionResult& click,
        double ici_seconds) const;
    [[nodiscard]] bool active_update_due(
        const ActiveTrain& train,
        const ClickDetectionResult& click) const;
};

} // namespace pamguard::detectors
