#pragma once

#include <cstddef>

namespace pamguard::detectors {

struct CtChi2ClassifierConfig {
    /** Chi2ThresholdParams defaults. */
    double chi2_threshold = 1500.0;
    std::size_t min_clicks = 5;
    double min_time_seconds = 0.0;
    int species_flag = 1;
};

/** CTClassifier.NOSPECIES. */
inline constexpr int kCtNoSpecies = 0;

struct CtClassificationResult {
    int species_id = kCtNoSpecies;
};

struct CtClassifierTrain {
    double chi2 = 0.0;
    std::size_t click_count = 0;
    double duration_ms = 0.0;
};

/**
 * Port of PAMGuard's Chi2ThresholdClassifier decision logic
 * (clickTrainDetector/classification/simplechi2classifier). A train is
 * classified as the configured species only if it passes, in order:
 * duration at or above minTime, click count at or above minClicks, and chi2
 * at or below chi2Threshold — with a zero threshold disabling the chi2 test
 * entirely, exactly as the reference's `chi2Threshold != 0` guard does.
 * Otherwise NOSPECIES is returned.
 *
 * PAMGuard's `minPercentage` data-selector criterion is not ported; the
 * reference short-circuits it when the percentage is zero (its default).
 */
class CtChi2Classifier {
public:
    CtChi2Classifier() = default;
    explicit CtChi2Classifier(CtChi2ClassifierConfig config) : config_(config) {}

    [[nodiscard]] const CtChi2ClassifierConfig& config() const noexcept { return config_; }

    [[nodiscard]] CtClassificationResult classify(const CtClassifierTrain& train) const {
        if (train.duration_ms < config_.min_time_seconds * 1000.0) {
            return {};
        }
        if (train.click_count < config_.min_clicks) {
            return {};
        }
        if (train.chi2 > config_.chi2_threshold && config_.chi2_threshold != 0.0) {
            return {};
        }
        return CtClassificationResult{config_.species_flag};
    }

private:
    CtChi2ClassifierConfig config_;
};

} // namespace pamguard::detectors
