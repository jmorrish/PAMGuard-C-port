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

pamguard::core::ClickBearingResult bearing(std::int64_t sample, double x, double y, double z, double residual) {
    pamguard::core::ClickBearingResult result;
    result.click_start_sample = sample;
    result.bearing.click_start_sample = sample;
    result.bearing.valid = true;
    result.bearing.unit_x = x;
    result.bearing.unit_y = y;
    result.bearing.unit_z = z;
    result.bearing.residual_rms_seconds = residual;
    return result;
}

} // namespace

int main() {
    try {
        pamguard::detectors::ClickTrainSummary train;
        train.train_id = 7;
        train.channel_bitmap = 0x3;
        train.first_start_sample = 100;
        train.last_start_sample = 300;
        train.click_count = 3;
        train.click_start_samples = {100, 200, 300};

        pamguard::detectors::ClickTrainSummary unmatched_train;
        unmatched_train.train_id = 8;
        unmatched_train.channel_bitmap = 0x3;
        unmatched_train.first_start_sample = 500;
        unmatched_train.last_start_sample = 700;
        unmatched_train.click_count = 3;
        unmatched_train.click_start_samples = {500, 600, 700};

        const auto summaries = pamguard::core::summarize_click_train_bearings(
            {train, unmatched_train},
            {
                bearing(100, 1.0, 0.0, 0.0, 0.1),
                bearing(300, 0.0, 1.0, 0.0, 0.3),
                bearing(900, 0.0, 0.0, 1.0, 0.5),
            });

        if (summaries.size() != 2) {
            std::cerr << "Expected two train bearing summaries\n";
            return 1;
        }

        const auto& summary = summaries[0];
        const double root_half = std::sqrt(0.5);
        if (summary.train_id != 7 || summary.channel_bitmap != 0x3 ||
            summary.first_start_sample != 100 || summary.last_start_sample != 300 ||
            summary.click_count != 3 || summary.bearing_count != 2 || !summary.valid ||
            !close(summary.unit_x, root_half) || !close(summary.unit_y, root_half) || !close(summary.unit_z, 0.0) ||
            !close(summary.azimuth_degrees, 45.0) || !close(summary.elevation_degrees, 0.0) ||
            !close(summary.mean_residual_rms_seconds, 0.2)) {
            std::cerr << "Train bearing summary mismatch\n";
            return 1;
        }

        const auto& unmatched = summaries[1];
        if (unmatched.train_id != 8 || unmatched.bearing_count != 0 || unmatched.valid) {
            std::cerr << "Unmatched train bearing summary mismatch\n";
            return 1;
        }

        std::cout << "Click train bearing summary check passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
