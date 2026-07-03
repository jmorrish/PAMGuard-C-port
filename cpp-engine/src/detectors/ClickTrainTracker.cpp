#include "pamguard/detectors/ClickTrainTracker.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace pamguard::detectors {

namespace {

double mean(const std::vector<double>& values) {
    if (values.empty()) {
        return 0.0;
    }
    return std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
}

double median(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    auto n = values.size();
    if (n % 2 == 0) {
        n /= 2;
        return (values[n] + values[n - 1]) / 2.0;
    }
    n /= 2;
    return values[n];
}

double stddev(const std::vector<double>& values, double value_mean) {
    if (values.empty()) {
        return 0.0;
    }
    double total = 0.0;
    for (double value : values) {
        total += (value_mean - value) * (value_mean - value);
    }
    return std::sqrt(total / static_cast<double>(values.size()));
}

double min_value(const std::vector<double>& values) {
    if (values.empty()) {
        return 0.0;
    }
    return *std::min_element(values.begin(), values.end());
}

double max_value(const std::vector<double>& values) {
    if (values.empty()) {
        return 0.0;
    }
    return *std::max_element(values.begin(), values.end());
}

} // namespace

ClickTrainTracker::ClickTrainTracker(ClickTrainConfig config)
    : config_(std::move(config)) {
    if (config_.sample_rate_hz <= 0.0) {
        throw std::invalid_argument("click train tracker sample_rate_hz must be positive");
    }
    if (config_.max_ici_seconds <= 0.0 || config_.min_clicks == 0) {
        throw std::invalid_argument("click train max_ici_seconds and min_clicks must be positive");
    }
}

const ClickTrainConfig& ClickTrainTracker::config() const noexcept {
    return config_;
}

void ClickTrainTracker::reset() {
    active_trains_.clear();
    next_train_id_ = 1;
}

std::vector<ClickTrainSummary> ClickTrainTracker::process(const std::vector<ClickDetectionResult>& clicks) {
    std::vector<ClickTrainSummary> summaries;
    for (const auto& click : clicks) {
        const auto key = click.channel_bitmap != 0 ? click.channel_bitmap : click.trigger_bitmap;
        auto found = active_trains_.find(key);
        if (found == active_trains_.end()) {
            active_trains_.emplace(key, start_train(click));
            continue;
        }

        auto& train = found->second;
        ClickDetectionResult previous;
        previous.start_sample = train.start_samples.back();
        previous.time_unix_ms = train.time_ms.back();
        const double ici = calculate_ici_seconds(previous, click);
        if (ici > config_.max_ici_seconds) {
            if (train.start_samples.size() >= config_.min_clicks) {
                summaries.push_back(summarize(train, true));
            }
            train = start_train(click);
            continue;
        }

        append_click(train, click, ici);
        if (train.start_samples.size() >= config_.min_clicks && train.last_reported_click_count != train.start_samples.size()) {
            summaries.push_back(summarize(train, false));
            train.last_reported_click_count = train.start_samples.size();
        }
    }
    return summaries;
}

std::vector<ClickTrainSummary> ClickTrainTracker::flush() {
    std::vector<ClickTrainSummary> summaries;
    for (const auto& [_, train] : active_trains_) {
        if (train.start_samples.size() >= config_.min_clicks) {
            summaries.push_back(summarize(train, true));
        }
    }
    active_trains_.clear();
    return summaries;
}

double ClickTrainTracker::calculate_ici_seconds(const ClickDetectionResult& previous, const ClickDetectionResult& current) const {
    const double ici_ms = static_cast<double>(current.time_unix_ms - previous.time_unix_ms) / 1000.0;
    const double ici_samples = static_cast<double>(current.start_sample - previous.start_sample) / config_.sample_rate_hz;
    return std::abs(ici_samples - ici_ms) < 1.0 ? ici_samples : ici_ms;
}

ClickTrainTracker::ActiveTrain ClickTrainTracker::start_train(const ClickDetectionResult& click) {
    ActiveTrain train;
    train.train_id = next_train_id_++;
    train.channel_bitmap = click.channel_bitmap != 0 ? click.channel_bitmap : click.trigger_bitmap;
    train.start_samples.push_back(click.start_sample);
    train.time_ms.push_back(click.time_unix_ms);
    return train;
}

void ClickTrainTracker::append_click(ActiveTrain& train, const ClickDetectionResult& click, double ici_seconds) {
    train.start_samples.push_back(click.start_sample);
    train.time_ms.push_back(click.time_unix_ms);
    train.ici_seconds.push_back(ici_seconds);
}

ClickTrainSummary ClickTrainTracker::summarize(const ActiveTrain& train, bool completed) const {
    ClickTrainSummary summary;
    summary.train_id = train.train_id;
    summary.channel_bitmap = train.channel_bitmap;
    summary.first_start_sample = train.start_samples.front();
    summary.last_start_sample = train.start_samples.back();
    summary.first_time_ms = train.time_ms.front();
    summary.last_time_ms = train.time_ms.back();
    summary.click_start_samples = train.start_samples;
    summary.click_time_ms = train.time_ms;
    summary.click_count = train.start_samples.size();
    summary.duration_samples = summary.last_start_sample - summary.first_start_sample;
    summary.duration_seconds = static_cast<double>(summary.duration_samples) / config_.sample_rate_hz;
    summary.time_span_seconds = static_cast<double>(summary.last_time_ms - summary.first_time_ms) / 1000.0;
    summary.last_ici_seconds = train.ici_seconds.empty() ? 0.0 : train.ici_seconds.back();
    summary.min_ici_seconds = min_value(train.ici_seconds);
    summary.max_ici_seconds = max_value(train.ici_seconds);
    summary.mean_ici_seconds = mean(train.ici_seconds);
    summary.median_ici_seconds = median(train.ici_seconds);
    summary.std_ici_seconds = stddev(train.ici_seconds, summary.mean_ici_seconds);
    summary.ici_cv = summary.mean_ici_seconds == 0.0 ? 0.0 : summary.std_ici_seconds / summary.mean_ici_seconds;
    summary.click_rate_hz = summary.duration_seconds <= 0.0 || summary.click_count < 2
        ? 0.0
        : static_cast<double>(summary.click_count - 1) / summary.duration_seconds;
    summary.completed = completed;
    return summary;
}

} // namespace pamguard::detectors
