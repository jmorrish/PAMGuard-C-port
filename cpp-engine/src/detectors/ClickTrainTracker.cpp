#include "pamguard/detectors/ClickTrainTracker.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace pamguard::detectors {

namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;

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
    if (!std::isfinite(config_.sample_rate_hz) ||
        config_.sample_rate_hz <= 0.0) {
        throw std::invalid_argument("click train tracker sample_rate_hz must be positive");
    }
    if (!std::isfinite(config_.min_ici_seconds) ||
        !std::isfinite(config_.max_ici_seconds) ||
        config_.min_ici_seconds < 0.0 ||
        config_.max_ici_seconds <= 0.0 ||
        config_.min_ici_seconds > config_.max_ici_seconds ||
        config_.min_clicks == 0) {
        throw std::invalid_argument(
            "click train ICI range must be ordered and non-negative, and min_clicks positive");
    }
    if (!std::isfinite(config_.max_ici_change) ||
        config_.max_ici_change < 1.0) {
        throw std::invalid_argument(
            "click train max_ici_change must be finite and at least one");
    }
    if (!std::isfinite(config_.ok_angle_error_degrees) ||
        config_.ok_angle_error_degrees < 0.0 ||
        !std::isfinite(config_.initial_perpendicular_distance_m) ||
        config_.initial_perpendicular_distance_m < 0.0 ||
        !std::isfinite(config_.min_angle_change_degrees) ||
        config_.min_angle_change_degrees < 0.0) {
        throw std::invalid_argument(
            "click train angle and localisation settings are invalid");
    }
    if (!std::isfinite(config_.ici_update_ratio) ||
        config_.ici_update_ratio < 0.0 ||
        config_.ici_update_ratio > 1.0 ||
        !std::isfinite(config_.min_update_gap_seconds) ||
        config_.min_update_gap_seconds < 0.0) {
        throw std::invalid_argument(
            "click train update ratio or minimum update gap is invalid");
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
        const double matching_ici =
            calculate_matching_ici_seconds(previous, click);
        if (!passes_continuation_gates(train, click, matching_ici)) {
            /*
             * PAMGuard leaves a non-matching train open. Its subsequent
             * closeOldTrains call closes it only once the maximum initial ICI
             * has elapsed. This single-active-train port cannot retain the
             * competing startup candidate as well, so at that same boundary
             * it reports/closes the old train and starts the new candidate.
             */
            if (matching_ici > config_.max_ici_seconds) {
                if (train.start_samples.size() >= config_.min_clicks) {
                    summaries.push_back(summarize(train, true));
                }
                train = start_train(click);
            }
            continue;
        }

        append_click(
            train,
            click,
            calculate_ici_seconds(previous, click));
        if (train.start_samples.size() >= config_.min_clicks &&
            train.last_reported_click_count != train.start_samples.size() &&
            active_update_due(train, click)) {
            summaries.push_back(summarize(train, false));
            train.last_reported_click_count = train.start_samples.size();
            train.last_report_time_ms = click.time_unix_ms;
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

double ClickTrainTracker::calculate_ici_seconds(
    const ClickDetectionResult& previous,
    const ClickDetectionResult& current) const {
    return static_cast<double>(
               current.start_sample - previous.start_sample) /
        config_.sample_rate_hz;
}

double ClickTrainTracker::calculate_matching_ici_seconds(
    const ClickDetectionResult& previous,
    const ClickDetectionResult& current) {
    return static_cast<double>(
               current.time_unix_ms - previous.time_unix_ms) /
        1000.0;
}

ClickTrainTracker::ActiveTrain ClickTrainTracker::start_train(const ClickDetectionResult& click) {
    ActiveTrain train;
    train.train_id = next_train_id_++;
    train.channel_bitmap = click.channel_bitmap != 0 ? click.channel_bitmap : click.trigger_bitmap;
    train.start_samples.push_back(click.start_sample);
    train.time_ms.push_back(click.time_unix_ms);
    if (click.bearing_radians.has_value() &&
        std::isfinite(*click.bearing_radians)) {
        train.last_bearing_radians = *click.bearing_radians;
        train.min_bearing_radians = *click.bearing_radians;
        train.max_bearing_radians = *click.bearing_radians;
        train.has_bearing = true;
    }
    return train;
}

void ClickTrainTracker::append_click(ActiveTrain& train, const ClickDetectionResult& click, double ici_seconds) {
    train.start_samples.push_back(click.start_sample);
    train.time_ms.push_back(click.time_unix_ms);
    train.ici_seconds.push_back(ici_seconds);
    /*
     * Java creates a STARTING train from a historical click plus the current
     * click. Its lastClickTime is not initialised by that constructor path, so
     * runningICI remains -1 until the third click is appended.
     */
    if (train.start_samples.size() > 2) {
        if (train.running_ici_seconds < 0.0) {
            train.running_ici_seconds = ici_seconds;
        }
        else {
            train.running_ici_seconds =
                (1.0 - config_.ici_update_ratio) *
                    train.running_ici_seconds +
                config_.ici_update_ratio * ici_seconds;
        }
    }
    if (click.bearing_radians.has_value() &&
        std::isfinite(*click.bearing_radians)) {
        if (!train.has_bearing) {
            train.last_bearing_radians = *click.bearing_radians;
            train.min_bearing_radians = *click.bearing_radians;
            train.max_bearing_radians = *click.bearing_radians;
            train.has_bearing = true;
        }
        else {
            train.last_bearing_radians = *click.bearing_radians;
            // ClickTrainDetection.addSubDetection uses raw min/max bearings;
            // it does not unwrap across the +/-pi boundary.
            train.min_bearing_radians =
                std::min(train.min_bearing_radians, *click.bearing_radians);
            train.max_bearing_radians =
                std::max(train.max_bearing_radians, *click.bearing_radians);
        }
    }
}

bool ClickTrainTracker::passes_continuation_gates(
    const ActiveTrain& train,
    const ClickDetectionResult& click,
    double ici_seconds) const {
    if (!std::isfinite(ici_seconds) || ici_seconds <= 0.0) {
        return false;
    }

    if (train.running_ici_seconds < 0.0) {
        // ClickTrainDetection.testClick applies iciRange to the first ICI.
        if (ici_seconds < config_.min_ici_seconds ||
            ici_seconds > config_.max_ici_seconds) {
            return false;
        }
    }
    else {
        // Thereafter Java compares both possible ICI ratios and uses the
        // larger one, so acceleration and deceleration are symmetric.
        const double ratio = std::max(
            ici_seconds / train.running_ici_seconds,
            train.running_ici_seconds / ici_seconds);
        if (!std::isfinite(ratio) || ratio > config_.max_ici_change) {
            return false;
        }
    }

    if (train.has_bearing &&
        click.bearing_radians.has_value() &&
        std::isfinite(*click.bearing_radians)) {
        const double raw_difference =
            *click.bearing_radians - train.last_bearing_radians;
        const bool startup_pair = train.start_samples.size() == 1;
        const double difference_radians = startup_pair
            ? std::abs(raw_difference)
            : std::abs(std::remainder(raw_difference, 2.0 * kPi));
        /*
         * Preserve two Java runtime quirks rather than normalising the UI
         * units. ClickTrainDetector.matchClickIntoGroup compares startup
         * radian bearings directly with okAngleError's degree-labelled numeric
         * value. Once running, ClickTrainDetection.testClick ignores the
         * setting and rejects beyond 2 * radians(2), i.e. four degrees.
         */
        const double limit_radians = startup_pair
            ? config_.ok_angle_error_degrees
            : 4.0 * kPi / 180.0;
        if (difference_radians > limit_radians) {
            return false;
        }
    }

    return true;
}

bool ClickTrainTracker::active_update_due(
    const ActiveTrain& train,
    const ClickDetectionResult& click) const {
    if (train.last_reported_click_count == 0) {
        // The transition from STARTING to OPEN is always observable.
        return true;
    }
    const double gap_ms = config_.min_update_gap_seconds * 1000.0;
    return static_cast<double>(
               click.time_unix_ms - train.last_report_time_ms) > gap_ms;
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
    summary.running_ici_seconds =
        train.running_ici_seconds < 0.0 ? 0.0 : train.running_ici_seconds;
    if (train.has_bearing) {
        summary.bearing_span_degrees =
            (train.max_bearing_radians -
             train.min_bearing_radians) *
            180.0 / kPi;
        // ClickTrainDetector uses a strict greater-than test here.
        summary.localisation_ready =
            summary.bearing_span_degrees >
            config_.min_angle_change_degrees;
    }
    summary.completed = completed;
    return summary;
}

} // namespace pamguard::detectors
