#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "pamguard/detectors/CtChi2Classifier.h"

namespace pamguard::detectors {

/** PamArrayUtils.median(double[]): sorted middle, averaging the middle pair. */
[[nodiscard]] inline double ct_median(std::vector<double> values) {
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

/** PamArrayUtils.mean(double[]). */
[[nodiscard]] inline double ct_mean(const std::vector<double>& values) {
    double sum = 0.0;
    for (const auto value : values) {
        sum += value;
    }
    return sum / static_cast<double>(values.size());
}

/** PamArrayUtils.std(double[]): population standard deviation. */
[[nodiscard]] inline double ct_std(const std::vector<double>& values) {
    const double mean = ct_mean(values);
    double total = 0.0;
    for (const auto value : values) {
        total += (mean - value) * (mean - value);
    }
    return std::sqrt(total / static_cast<double>(values.size()));
}

struct CtIdiClassifierConfig {
    /** IDIClassifierParams defaults; limits in seconds. */
    bool use_median_idi = true;
    double min_median_idi = 0.0;
    double max_median_idi = 2.0;
    bool use_mean_idi = false;
    double min_mean_idi = 0.0;
    double max_mean_idi = 2.0;
    bool use_std_idi = false;
    double min_std_idi = 0.0;
    double max_std_idi = 100.0;
    int species_flag = 1;
};

struct CtIdiSummary {
    double median_idi = 0.0;
    double mean_idi = 0.0;
    double std_idi = 0.0;
};

/**
 * Port of PAMGuard's IDIClassifier: each enabled criterion must place the
 * train's IDI statistic inside its inclusive limits. The statistics
 * themselves come from the IDI summary layer, which has bitwise Java
 * fixture parity (`docs/156`).
 */
class CtIdiClassifier {
public:
    CtIdiClassifier() = default;
    explicit CtIdiClassifier(CtIdiClassifierConfig config) : config_(config) {}

    [[nodiscard]] CtClassificationResult classify(const CtIdiSummary& idi) const {
        if (config_.use_median_idi &&
            (idi.median_idi < config_.min_median_idi || idi.median_idi > config_.max_median_idi)) {
            return {};
        }
        if (config_.use_mean_idi &&
            (idi.mean_idi < config_.min_mean_idi || idi.mean_idi > config_.max_mean_idi)) {
            return {};
        }
        if (config_.use_std_idi &&
            (idi.std_idi < config_.min_std_idi || idi.std_idi > config_.max_std_idi)) {
            return {};
        }
        return CtClassificationResult{config_.species_flag};
    }

private:
    CtIdiClassifierConfig config_;
};

struct CtBearingClassifierConfig {
    /** BearingClassifierParams defaults; angles in radians. */
    double bearing_lim_min = 85.0 * 3.141592653589793238462643383279502884 / 180.0;
    double bearing_lim_max = 95.0 * 3.141592653589793238462643383279502884 / 180.0;
    bool use_mean = false;
    double min_mean_bearing_derivative = -0.005 * 3.141592653589793238462643383279502884 / 180.0;
    double max_mean_bearing_derivative = 0.005 * 3.141592653589793238462643383279502884 / 180.0;
    bool use_median = true;
    double min_median_bearing_derivative = -0.005 * 3.141592653589793238462643383279502884 / 180.0;
    double max_median_bearing_derivative = 0.005 * 3.141592653589793238462643383279502884 / 180.0;
    bool use_std = true;
    double min_std_bearing_derivative = 0.0;
    double max_std_bearing_derivative = 1.5 * 3.141592653589793238462643383279502884 / 180.0;
    int species_flag = -1;
};

/** One click in a train as the bearing classifier sees it. */
struct CtBearingClick {
    std::int64_t time_ms = 0;
    /** Absent when the click has no localisation, as PAMGuard allows. */
    std::optional<double> bearing_radians;
};

struct CtBearingClassificationResult {
    int species_id = kCtNoSpecies;
    double mean_bearing_derivative = 0.0;
    double median_bearing_derivative = 0.0;
    double std_bearing_derivative = 0.0;
    bool valid = false;
};

/**
 * Port of PAMGuard's BearingClassifier. Bearing derivatives (radians per
 * second) are taken between successive clicks, then mean/median/population
 * standard deviation are tested against the configured limits, along with a
 * range test that passes when EITHER the minimum or the maximum bearing
 * falls inside the bearing limits.
 *
 * Two reference behaviours are preserved deliberately: clicks without a
 * localisation are skipped but leave a zero in the derivative array (rather
 * than shortening it), and the final bearing is filled in after the loop, so
 * the bearing array's last entry is always taken even though the loop stops
 * one short. Trains with fewer than three clicks, or with too many missing
 * localisations, return no species.
 */
class CtBearingClassifier {
public:
    CtBearingClassifier() = default;
    explicit CtBearingClassifier(CtBearingClassifierConfig config) : config_(config) {}

    [[nodiscard]] CtBearingClassificationResult classify(const std::vector<CtBearingClick>& clicks) const {
        CtBearingClassificationResult result;
        const auto count = clicks.size();
        if (count < 3) {
            return result;
        }

        std::vector<double> bearing_diff(count - 1, 0.0);
        std::vector<double> bearing(count, 0.0);
        std::size_t null_count = 0;
        for (std::size_t i = 0; i + 1 < count; ++i) {
            if (!clicks[i].bearing_radians.has_value()) {
                ++null_count;
                continue;
            }
            bearing[i] = *clicks[i].bearing_radians;
            const double next = clicks[i + 1].bearing_radians.value_or(0.0);
            const double time_diff = static_cast<double>(clicks[i + 1].time_ms - clicks[i].time_ms) / 1000.0;
            bearing_diff[i] = (*clicks[i].bearing_radians - next) / time_diff;
        }

        if (null_count + 4 > count) {
            return result;
        }

        bearing[bearing.size() - 1] = clicks[bearing.size() - 1].bearing_radians.value_or(0.0);

        const double min = *std::min_element(bearing.begin(), bearing.end());
        const double max = *std::max_element(bearing.begin(), bearing.end());
        result.mean_bearing_derivative = ct_mean(bearing_diff);
        result.median_bearing_derivative = ct_median(bearing_diff);
        result.std_bearing_derivative = ct_std(bearing_diff);
        result.valid = true;

        // Range test passes when either extreme falls inside the limits.
        bool passed = (min >= config_.bearing_lim_min && min <= config_.bearing_lim_max) ||
            (max >= config_.bearing_lim_min && max <= config_.bearing_lim_max);
        if (config_.use_mean && !(result.mean_bearing_derivative >= config_.min_mean_bearing_derivative &&
                                  result.mean_bearing_derivative <= config_.max_mean_bearing_derivative)) {
            passed = false;
        }
        if (config_.use_median && !(result.median_bearing_derivative >= config_.min_median_bearing_derivative &&
                                    result.median_bearing_derivative <= config_.max_median_bearing_derivative)) {
            passed = false;
        }
        if (config_.use_std && !(result.std_bearing_derivative >= config_.min_std_bearing_derivative &&
                                 result.std_bearing_derivative <= config_.max_std_bearing_derivative)) {
            passed = false;
        }

        if (passed) {
            result.species_id = config_.species_flag;
        }
        return result;
    }

private:
    CtBearingClassifierConfig config_;
};

/** Everything the ported click train classifiers read from a train. */
struct CtTrainSummary {
    double chi2 = 0.0;
    std::size_t click_count = 0;
    double duration_ms = 0.0;
    CtIdiSummary idi;
    std::vector<CtBearingClick> bearing_clicks;
    /** Average spectrum of the train's clicks, evenly spaced from 0 Hz. */
    std::vector<double> average_spectrum;
    /** Sample rate the average spectrum spans, as PAMGuard passes it. */
    double average_spectrum_sample_rate_hz = 0.0;
};

/** PamArrayUtils.normalise: scale so the L2 norm is one. */
[[nodiscard]] inline std::vector<double> ct_normalise(const std::vector<double>& values) {
    double sum = 0.0;
    for (const auto value : values) {
        sum += value * value;
    }
    sum = std::sqrt(sum);
    std::vector<double> result(values.size(), 0.0);
    for (std::size_t i = 0; i < values.size(); ++i) {
        result[i] = values[i] / sum;
    }
    return result;
}

/** PamInterp.interp1: linear interpolation with a constant extrapolation value. */
[[nodiscard]] inline std::vector<double> ct_interp1(const std::vector<double>& x, const std::vector<double>& y,
                                                    const std::vector<double>& xi, double extrapolation) {
    std::vector<double> result(xi.size(), extrapolation);
    if (x.size() < 2 || x.size() != y.size()) {
        return result;
    }
    for (std::size_t i = 0; i < xi.size(); ++i) {
        const double target = xi[i];
        if (target < x.front() || target > x.back()) {
            continue;
        }
        const auto upper = std::lower_bound(x.begin(), x.end(), target);
        if (upper == x.begin()) {
            result[i] = y.front();
            continue;
        }
        const auto index = static_cast<std::size_t>(upper - x.begin());
        const double x0 = x[index - 1];
        const double x1 = x[index];
        const double span = x1 - x0;
        result[i] = span == 0.0 ? y[index] : y[index - 1] + (y[index] - y[index - 1]) * (target - x0) / span;
    }
    return result;
}

/** Evenly spaced values from `from` to `to`, as PamUtils.linspace. */
[[nodiscard]] inline std::vector<double> ct_linspace(double from, double to, std::size_t count) {
    std::vector<double> result(count, from);
    if (count < 2) {
        return result;
    }
    const double step = (to - from) / static_cast<double>(count - 1);
    for (std::size_t i = 0; i < count; ++i) {
        result[i] = from + step * static_cast<double>(i);
    }
    return result;
}

struct CtTemplateClassifierConfig {
    /** Template spectrum and the sample rate it spans. */
    std::vector<double> template_spectrum;
    double template_sample_rate_hz = 0.0;
    /** TemplateClassifierParams default. */
    double correlation_threshold = 0.5;
    int species_flag = 1;
};

struct CtTemplateClassificationResult {
    int species_id = kCtPreClassifierFlag;
    double correlation = 0.0;
};

/**
 * Port of PAMGuard's CTTemplateClassifier. The template spectrum is linearly
 * interpolated onto the average spectrum's frequency grid (extrapolating
 * zero), both are L2-normalised, and the correlation is their dot product —
 * a cosine similarity that reaches one for identical shapes.
 *
 * Faithful detail: a failing correlation (or a missing average spectrum, or
 * NaN) returns PRECLASSIFIERFLAG (-1) rather than NOSPECIES (0). Both are at
 * or below NOSPECIES, so either still vetoes the standard composite
 * classifier, but the distinct value is preserved for reporting.
 */
class CtTemplateClassifier {
public:
    explicit CtTemplateClassifier(CtTemplateClassifierConfig config) : config_(std::move(config)) {}

    [[nodiscard]] int classify_species(const CtTrainSummary& train) const {
        return classify_detailed(train).species_id;
    }

    [[nodiscard]] CtTemplateClassificationResult classify_detailed(const CtTrainSummary& train) const {
        CtTemplateClassificationResult result;
        if (train.average_spectrum.empty() || train.average_spectrum_sample_rate_hz <= 0.0 ||
            config_.template_spectrum.size() < 2 || config_.template_sample_rate_hz <= 0.0) {
            return result;
        }

        const auto template_bins = ct_linspace(0.0, config_.template_sample_rate_hz, config_.template_spectrum.size());
        const auto target_bins = ct_linspace(0.0, train.average_spectrum_sample_rate_hz, train.average_spectrum.size());
        const auto interpolated = ct_normalise(
            ct_interp1(template_bins, config_.template_spectrum, target_bins, 0.0));
        const auto normalised_spectrum = ct_normalise(train.average_spectrum);

        double correlation = 0.0;
        for (std::size_t i = 0; i < interpolated.size() && i < normalised_spectrum.size(); ++i) {
            correlation += normalised_spectrum[i] * interpolated[i];
        }
        result.correlation = correlation;

        if (std::isnan(correlation) || correlation < config_.correlation_threshold) {
            return result;
        }
        result.species_id = config_.species_flag;
        return result;
    }

private:
    CtTemplateClassifierConfig config_;
};

/** Common interface so classifiers can be composed and chained. */
class CtClassifier {
public:
    virtual ~CtClassifier() = default;
    [[nodiscard]] virtual int classify(const CtTrainSummary& train) const = 0;
};

class CtChi2ClassifierAdapter final : public CtClassifier {
public:
    explicit CtChi2ClassifierAdapter(CtChi2ClassifierConfig config) : classifier_(config) {}
    [[nodiscard]] int classify(const CtTrainSummary& train) const override {
        return classifier_.classify(CtClassifierTrain{train.chi2, train.click_count, train.duration_ms}).species_id;
    }

private:
    CtChi2Classifier classifier_;
};

class CtIdiClassifierAdapter final : public CtClassifier {
public:
    explicit CtIdiClassifierAdapter(CtIdiClassifierConfig config) : classifier_(config) {}
    [[nodiscard]] int classify(const CtTrainSummary& train) const override {
        return classifier_.classify(train.idi).species_id;
    }

private:
    CtIdiClassifier classifier_;
};

class CtTemplateClassifierAdapter final : public CtClassifier {
public:
    explicit CtTemplateClassifierAdapter(CtTemplateClassifierConfig config) : classifier_(std::move(config)) {}
    [[nodiscard]] int classify(const CtTrainSummary& train) const override {
        return classifier_.classify_species(train);
    }

private:
    CtTemplateClassifier classifier_;
};

class CtBearingClassifierAdapter final : public CtClassifier {
public:
    explicit CtBearingClassifierAdapter(CtBearingClassifierConfig config) : classifier_(config) {}
    [[nodiscard]] int classify(const CtTrainSummary& train) const override {
        return classifier_.classify(train.bearing_clicks).species_id;
    }

private:
    CtBearingClassifier classifier_;
};

/**
 * Port of PAMGuard's StandardClassifier: a composite AND gate. Every
 * sub-classifier runs, and if any **enabled** one returns NOSPECIES or below
 * the composite returns NOSPECIES; otherwise it returns its own species flag.
 * Sub-results are retained for reporting, as the reference keeps them in its
 * StandardClassification.
 */
class CtStandardClassifier final : public CtClassifier {
public:
    struct Entry {
        std::shared_ptr<const CtClassifier> classifier;
        bool enabled = true;
    };

    CtStandardClassifier(std::vector<Entry> entries, int species_flag)
        : entries_(std::move(entries)), species_flag_(species_flag) {}

    [[nodiscard]] int classify(const CtTrainSummary& train) const override {
        return classify_detailed(train).species_id;
    }

    struct Result {
        int species_id = kCtNoSpecies;
        std::vector<int> sub_species_ids;
    };

    [[nodiscard]] Result classify_detailed(const CtTrainSummary& train) const {
        Result result;
        result.species_id = species_flag_;
        result.sub_species_ids.reserve(entries_.size());
        for (const auto& entry : entries_) {
            const int sub = entry.classifier ? entry.classifier->classify(train) : kCtNoSpecies;
            result.sub_species_ids.push_back(sub);
            if (entry.enabled && sub <= kCtNoSpecies) {
                result.species_id = kCtNoSpecies;
            }
        }
        return result;
    }

private:
    std::vector<Entry> entries_;
    int species_flag_ = kCtNoSpecies;
};

struct CtClassifierChainResult {
    /** True when the chi2 pre-classifier rejected the train outright. */
    bool junk_train = false;
    /** Index of the first classifier returning a species, or npos. */
    std::size_t classification_index = static_cast<std::size_t>(-1);
    int species_id = kCtNoSpecies;
    /** Every classifier's verdict, retained as the reference does. */
    std::vector<int> classifications;
};

/**
 * Port of PAMGuard's CTClassifierManager.classify: the chi2 pre-classifier
 * gates first — a rejection flags the train as junk, clears classifications,
 * and short-circuits. Otherwise every classifier runs and all verdicts are
 * kept, with the **first** classifier returning a species setting the
 * classification index.
 */
class CtClassifierChain {
public:
    CtClassifierChain(CtChi2ClassifierConfig pre_classifier,
                      std::vector<std::shared_ptr<const CtClassifier>> classifiers,
                      bool run_classifiers = true)
        : pre_classifier_(pre_classifier), classifiers_(std::move(classifiers)),
          run_classifiers_(run_classifiers) {}

    [[nodiscard]] CtClassifierChainResult classify(const CtTrainSummary& train) const {
        CtClassifierChainResult result;
        const CtChi2Classifier pre(pre_classifier_);
        if (pre.classify(CtClassifierTrain{train.chi2, train.click_count, train.duration_ms}).species_id
            == kCtNoSpecies) {
            result.junk_train = true;
            return result;
        }
        if (!run_classifiers_) {
            return result;
        }
        for (std::size_t i = 0; i < classifiers_.size(); ++i) {
            const int species = classifiers_[i] ? classifiers_[i]->classify(train) : kCtNoSpecies;
            result.classifications.push_back(species);
            if (species > kCtNoSpecies && result.classification_index == static_cast<std::size_t>(-1)) {
                result.classification_index = i;
                result.species_id = species;
            }
        }
        return result;
    }

private:
    CtChi2ClassifierConfig pre_classifier_;
    std::vector<std::shared_ptr<const CtClassifier>> classifiers_;
    bool run_classifiers_ = true;
};

} // namespace pamguard::detectors
