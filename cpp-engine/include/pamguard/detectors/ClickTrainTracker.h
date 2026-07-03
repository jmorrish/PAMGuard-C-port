#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "pamguard/detectors/ClickDetectorEngine.h"

namespace pamguard::detectors {

struct ClickTrainConfig {
    double sample_rate_hz = 0.0;
    double max_ici_seconds = 0.5;
    std::size_t min_clicks = 3;
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
        std::size_t last_reported_click_count = 0;
    };

    ClickTrainConfig config_;
    std::unordered_map<std::uint32_t, ActiveTrain> active_trains_;
    std::size_t next_train_id_ = 1;

    [[nodiscard]] double calculate_ici_seconds(const ClickDetectionResult& previous, const ClickDetectionResult& current) const;
    [[nodiscard]] ClickTrainSummary summarize(const ActiveTrain& train, bool completed) const;
    [[nodiscard]] ActiveTrain start_train(const ClickDetectionResult& click);
    void append_click(ActiveTrain& train, const ClickDetectionResult& click, double ici_seconds);
};

} // namespace pamguard::detectors
