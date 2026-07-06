#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "pamguard/core/AnalysisSession.h"

namespace {

bool close(double a, double b) {
    return std::abs(a - b) < 1e-12;
}

pamguard::localisation::ChannelPairDelay delay(
    std::size_t pair_index,
    std::size_t channel_a,
    std::size_t channel_b,
    double delay_samples,
    double score,
    double max_delay_samples = 0.0,
    double hydrophone_distance_m = 0.0,
    std::size_t audio_channel_a = 0,
    std::size_t audio_channel_b = 0,
    double pair_bearing_radians = -1.0) {
    pamguard::localisation::ChannelPairDelay result;
    result.pair_index = pair_index;
    result.channel_a = channel_a;
    result.channel_b = channel_b;
    result.audio_channel_a = audio_channel_a;
    result.audio_channel_b = audio_channel_b;
    result.geometry_constrained = max_delay_samples > 0.0;
    result.max_delay_samples = max_delay_samples;
    result.hydrophone_distance_m = hydrophone_distance_m;
    result.delay.delay_samples = delay_samples;
    result.delay.delay_score = score;
    if (pair_bearing_radians >= 0.0) {
        result.pair_bearing_valid = true;
        result.pair_bearing_radians = pair_bearing_radians;
    }
    return result;
}

pamguard::core::ClickLocalisationResult localisation(std::int64_t sample, std::vector<pamguard::localisation::ChannelPairDelay> delays) {
    pamguard::core::ClickLocalisationResult result;
    result.click_start_sample = sample;
    result.delays = std::move(delays);
    return result;
}

} // namespace

int main() {
    try {
        pamguard::detectors::ClickTrainSummary train;
        train.train_id = 3;
        train.channel_bitmap = 0x7;
        train.first_start_sample = 100;
        train.last_start_sample = 300;
        train.click_count = 3;
        train.click_start_samples = {100, 200, 300};

        pamguard::detectors::ClickTrainSummary unmatched_train;
        unmatched_train.train_id = 4;
        unmatched_train.channel_bitmap = 0x7;
        unmatched_train.first_start_sample = 500;
        unmatched_train.last_start_sample = 700;
        unmatched_train.click_count = 3;
        unmatched_train.click_start_samples = {500, 600, 700};

        const auto summaries = pamguard::core::summarize_click_train_localisations(
            {train, unmatched_train},
            {
                localisation(100, {delay(0, 0, 1, 2.0, 0.8, 33.0, 1.0, 10, 11, 1.2), delay(1, 0, 2, -1.0, 0.7, 49.0, 1.5, 10, 12)}),
                localisation(300, {delay(0, 0, 1, 4.0, 0.6, 33.0, 1.0, 10, 11, 1.6), delay(1, 0, 2, -3.0, 0.5, 49.0, 1.5, 10, 12)}),
                localisation(900, {delay(0, 0, 1, 100.0, 0.1, 0.0, 0.0, 10, 11)}),
            });

        if (summaries.size() != 2) {
            std::cerr << "Expected two train localisation summaries\n";
            return 1;
        }

        const auto& summary = summaries[0];
        if (summary.train_id != 3 || summary.channel_bitmap != 0x7 ||
            summary.first_start_sample != 100 || summary.last_start_sample != 300 ||
            summary.click_count != 3 || summary.localisation_count != 2 ||
            !summary.valid || summary.pair_delays.size() != 2) {
            std::cerr << "Train localisation summary metadata mismatch\n";
            return 1;
        }
        if (summary.pair_delays[0].pair_index != 0 || summary.pair_delays[0].delay_count != 2 ||
            summary.pair_delays[0].audio_channel_a != 10 || summary.pair_delays[0].audio_channel_b != 11 ||
            !summary.pair_delays[0].geometry_constrained ||
            !close(summary.pair_delays[0].max_delay_samples, 33.0) ||
            !close(summary.pair_delays[0].hydrophone_distance_m, 1.0) ||
            !close(summary.pair_delays[0].mean_delay_samples, 3.0) ||
            !close(summary.pair_delays[0].mean_delay_score, 0.7)) {
            std::cerr << "First pair delay summary mismatch\n";
            return 1;
        }
        if (summary.pair_delays[0].pair_bearing_count != 2 ||
            !close(summary.pair_delays[0].mean_pair_bearing_radians, 1.4)) {
            std::cerr << "First pair delay bearing aggregation mismatch\n";
            return 1;
        }
        if (summary.pair_delays[1].pair_bearing_count != 0 ||
            !close(summary.pair_delays[1].mean_pair_bearing_radians, 0.0)) {
            std::cerr << "Second pair delay should have no bearing aggregation\n";
            return 1;
        }
        if (summary.pair_delays[1].pair_index != 1 || summary.pair_delays[1].delay_count != 2 ||
            summary.pair_delays[1].audio_channel_a != 10 || summary.pair_delays[1].audio_channel_b != 12 ||
            !summary.pair_delays[1].geometry_constrained ||
            !close(summary.pair_delays[1].max_delay_samples, 49.0) ||
            !close(summary.pair_delays[1].hydrophone_distance_m, 1.5) ||
            !close(summary.pair_delays[1].mean_delay_samples, -2.0) ||
            !close(summary.pair_delays[1].mean_delay_score, 0.6)) {
            std::cerr << "Second pair delay summary mismatch\n";
            return 1;
        }

        const auto& unmatched = summaries[1];
        if (unmatched.train_id != 4 || unmatched.localisation_count != 0 || unmatched.valid || !unmatched.pair_delays.empty()) {
            std::cerr << "Unmatched train localisation summary mismatch\n";
            return 1;
        }

        std::cout << "Click train localisation summary check passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
