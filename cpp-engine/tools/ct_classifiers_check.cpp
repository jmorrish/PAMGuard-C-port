#include <cmath>
#include <iostream>
#include <numbers>
#include <string>
#include <vector>

#include "pamguard/detectors/CtClassifiers.h"
#include "pamguard/detectors/MhtSimpleChi2Vars.h"

namespace {

using pamguard::detectors::CtBearingClassifier;
using pamguard::detectors::CtBearingClassifierConfig;
using pamguard::detectors::CtBearingClick;
using pamguard::detectors::CtChi2ClassifierConfig;
using pamguard::detectors::CtIdiClassifier;
using pamguard::detectors::CtIdiClassifierConfig;
using pamguard::detectors::CtIdiSummary;
using pamguard::detectors::kCtNoSpecies;

constexpr int kSpecies = 5;
constexpr double kDeg = std::numbers::pi / 180.0;

bool check_idi_classifier() {
    struct Case {
        std::string name;
        CtIdiSummary idi;
        CtIdiClassifierConfig config;
        int expected = kCtNoSpecies;
    };

    CtIdiClassifierConfig defaults;
    defaults.species_flag = kSpecies;

    auto all_enabled = defaults;
    all_enabled.use_mean_idi = true;
    all_enabled.use_std_idi = true;
    all_enabled.max_std_idi = 0.5;

    auto none_enabled = defaults;
    none_enabled.use_median_idi = false;

    const std::vector<Case> cases{
        {"median-in-range", {0.5, 0.5, 0.01}, defaults, kSpecies},
        {"median-above-max", {2.5, 0.5, 0.01}, defaults, kCtNoSpecies},
        {"median-at-max-passes", {2.0, 0.5, 0.01}, defaults, kSpecies},
        // Mean and std are ignored unless their use flags are set.
        {"mean-out-of-range-but-disabled", {0.5, 9.0, 0.01}, defaults, kSpecies},
        {"mean-out-of-range-enabled", {0.5, 9.0, 0.01}, all_enabled, kCtNoSpecies},
        {"std-above-max-enabled", {0.5, 0.5, 0.9}, all_enabled, kCtNoSpecies},
        {"all-enabled-pass", {0.5, 0.5, 0.2}, all_enabled, kSpecies},
        {"no-criteria-always-passes", {99.0, 99.0, 99.0}, none_enabled, kSpecies},
    };

    for (const auto& test_case : cases) {
        const CtIdiClassifier classifier(test_case.config);
        const auto result = classifier.classify(test_case.idi);
        if (result.species_id != test_case.expected) {
            std::cerr << "IDI classifier mismatch for case " << test_case.name
                      << ": expected " << test_case.expected << " got " << result.species_id << "\n";
            return false;
        }
    }
    return true;
}

std::vector<CtBearingClick> steady_train(double start_deg, double step_deg, std::size_t count) {
    std::vector<CtBearingClick> clicks;
    for (std::size_t i = 0; i < count; ++i) {
        CtBearingClick click;
        click.time_ms = static_cast<std::int64_t>(1000 + i * 100);
        click.bearing_radians = (start_deg + step_deg * static_cast<double>(i)) * kDeg;
        clicks.push_back(click);
    }
    return clicks;
}

bool check_bearing_classifier() {
    CtBearingClassifierConfig config;
    config.species_flag = kSpecies;

    // Fewer than three clicks is rejected outright.
    {
        const CtBearingClassifier classifier(config);
        const auto result = classifier.classify(steady_train(90.0, 0.0, 2));
        if (result.species_id != kCtNoSpecies || result.valid) {
            std::cerr << "Bearing classifier should reject trains shorter than three clicks\n";
            return false;
        }
    }

    // A steady beam-aspect train with no bearing change passes.
    {
        const CtBearingClassifier classifier(config);
        const auto result = classifier.classify(steady_train(90.0, 0.0, 6));
        if (result.species_id != kSpecies || !result.valid ||
            std::abs(result.median_bearing_derivative) > 1e-12) {
            std::cerr << "Steady beam-aspect train should classify with zero derivative\n";
            return false;
        }
    }

    // Bearings far from the configured limits fail the range test.
    {
        const CtBearingClassifier classifier(config);
        const auto result = classifier.classify(steady_train(10.0, 0.0, 6));
        if (result.species_id != kCtNoSpecies || !result.valid) {
            std::cerr << "Out-of-range bearings should fail the range test but still report values\n";
            return false;
        }
    }

    // A fast bearing sweep fails the median-derivative test.
    {
        const CtBearingClassifier classifier(config);
        const auto result = classifier.classify(steady_train(88.0, 1.0, 6));
        if (result.species_id != kCtNoSpecies) {
            std::cerr << "Fast bearing sweep should fail the median derivative test\n";
            return false;
        }
    }

    // Too many clicks without localisation is rejected.
    {
        auto clicks = steady_train(90.0, 0.0, 6);
        for (std::size_t i = 0; i < 4; ++i) {
            clicks[i].bearing_radians.reset();
        }
        const CtBearingClassifier classifier(config);
        const auto result = classifier.classify(clicks);
        if (result.species_id != kCtNoSpecies || result.valid) {
            std::cerr << "Trains with too few localised clicks should be rejected\n";
            return false;
        }
    }

    // Disabling every derivative criterion leaves only the range test.
    {
        auto permissive = config;
        permissive.use_median = false;
        permissive.use_std = false;
        const CtBearingClassifier classifier(permissive);
        const auto result = classifier.classify(steady_train(88.0, 1.0, 6));
        if (result.species_id != kSpecies) {
            std::cerr << "With derivative criteria disabled the range test alone should decide\n";
            return false;
        }
    }

    return true;
}

bool check_correlation_chi2() {
    // CorrelationChi2: log(1/corr)^2 over the squared time-scaled error.
    // With 100 ms spacing and default error 1, the divisor is 0.1^2, so a
    // correlation of 1 scores zero and lower correlations grow as log^2.
    pamguard::detectors::MhtCorrelationChi2 chi2_var{pamguard::detectors::MhtCorrelationChi2Config{}};
    pamguard::detectors::MhtChi2Unit unit;
    unit.time_ns = 1'000'000'000;
    (void)chi2_var.update_chi2(unit, 1.0, true, 1, 1);

    unit.time_ns = 1'100'000'000;
    const double after_perfect = chi2_var.update_chi2(unit, 1.0, true, 2, 2);
    if (std::abs(after_perfect) > 1e-12) {
        std::cerr << "Perfect correlation should contribute zero chi2, got " << after_perfect << "\n";
        return false;
    }

    unit.time_ns = 1'200'000'000;
    const double after_half = chi2_var.update_chi2(unit, 0.5, true, 3, 3);
    const double expected_raw = std::pow(std::log(1.0 / 0.5), 2.0) / std::pow(0.1, 2.0);
    if (std::abs(after_half - expected_raw / 3.0) > 1e-9) {
        std::cerr << "Correlation chi2 mismatch: expected " << expected_raw / 3.0
                  << " got " << after_half << "\n";
        return false;
    }

    // Lower correlation must score strictly worse.
    pamguard::detectors::MhtCorrelationChi2 poor{pamguard::detectors::MhtCorrelationChi2Config{}};
    pamguard::detectors::MhtChi2Unit poor_unit;
    poor_unit.time_ns = 1'000'000'000;
    (void)poor.update_chi2(poor_unit, 1.0, true, 1, 1);
    poor_unit.time_ns = 1'100'000'000;
    (void)poor.update_chi2(poor_unit, 1.0, true, 2, 2);
    poor_unit.time_ns = 1'200'000'000;
    if (!(poor.update_chi2(poor_unit, 0.1, true, 3, 3) > after_half)) {
        std::cerr << "Weaker correlation should score worse than stronger correlation\n";
        return false;
    }
    return true;
}

CtIdiClassifierConfig passing_idi_config() {
    CtIdiClassifierConfig config;
    config.species_flag = kSpecies;
    return config;
}

pamguard::detectors::CtTrainSummary good_train() {
    pamguard::detectors::CtTrainSummary train;
    train.chi2 = 500.0;
    train.click_count = 10;
    train.duration_ms = 2000.0;
    train.idi = {0.5, 0.5, 0.01};
    train.bearing_clicks = steady_train(90.0, 0.0, 6);
    return train;
}

bool check_standard_classifier() {
    using pamguard::detectors::CtBearingClassifierAdapter;
    using pamguard::detectors::CtIdiClassifierAdapter;
    using pamguard::detectors::CtStandardClassifier;

    CtBearingClassifierConfig bearing_config;
    bearing_config.species_flag = kSpecies;

    auto passing_idi = std::make_shared<const CtIdiClassifierAdapter>(passing_idi_config());
    auto passing_bearing = std::make_shared<const CtBearingClassifierAdapter>(bearing_config);

    auto failing_idi_config = passing_idi_config();
    failing_idi_config.max_median_idi = 0.1; // the train's median is 0.5
    auto failing_idi = std::make_shared<const CtIdiClassifierAdapter>(failing_idi_config);

    // All enabled and passing: the composite returns its own flag.
    {
        const CtStandardClassifier classifier({{passing_idi, true}, {passing_bearing, true}}, kSpecies);
        if (classifier.classify(good_train()) != kSpecies) {
            std::cerr << "Standard classifier should pass when every enabled sub-classifier passes\n";
            return false;
        }
    }

    // One enabled sub-classifier failing rejects the whole composite.
    {
        const CtStandardClassifier classifier({{failing_idi, true}, {passing_bearing, true}}, kSpecies);
        if (classifier.classify(good_train()) != kCtNoSpecies) {
            std::cerr << "Standard classifier should reject when an enabled sub-classifier fails\n";
            return false;
        }
    }

    // The same failure is ignored when that sub-classifier is disabled, and
    // its verdict is still reported.
    {
        const CtStandardClassifier classifier({{failing_idi, false}, {passing_bearing, true}}, kSpecies);
        const auto detailed = classifier.classify_detailed(good_train());
        if (detailed.species_id != kSpecies || detailed.sub_species_ids.size() != 2 ||
            detailed.sub_species_ids[0] != kCtNoSpecies) {
            std::cerr << "Disabled sub-classifier should not veto but should still be reported\n";
            return false;
        }
    }
    return true;
}

bool check_template_classifier() {
    using pamguard::detectors::CtTemplateClassifier;
    using pamguard::detectors::CtTemplateClassifierConfig;
    using pamguard::detectors::kCtPreClassifierFlag;

    // A simple peaked template; the average spectrum spans the same band.
    CtTemplateClassifierConfig config;
    config.template_spectrum = {0.0, 0.2, 1.0, 0.6, 0.1, 0.0};
    config.template_sample_rate_hz = 96000.0;
    config.correlation_threshold = 0.9;
    config.species_flag = kSpecies;

    pamguard::detectors::CtTrainSummary train;
    train.average_spectrum_sample_rate_hz = 96000.0;

    // Identical shape: cosine similarity is 1, comfortably over threshold.
    {
        train.average_spectrum = config.template_spectrum;
        const CtTemplateClassifier classifier(config);
        const auto result = classifier.classify_detailed(train);
        if (result.species_id != kSpecies || std::abs(result.correlation - 1.0) > 1e-9) {
            std::cerr << "Identical spectrum should correlate to 1 and classify, got "
                      << result.correlation << "\n";
            return false;
        }
    }

    // Scaling the spectrum does not change the normalised correlation.
    {
        train.average_spectrum = {0.0, 0.4, 2.0, 1.2, 0.2, 0.0};
        const CtTemplateClassifier classifier(config);
        const auto result = classifier.classify_detailed(train);
        if (result.species_id != kSpecies || std::abs(result.correlation - 1.0) > 1e-9) {
            std::cerr << "Correlation should be scale invariant\n";
            return false;
        }
    }

    // A mismatched shape falls below threshold and returns PRECLASSIFIERFLAG,
    // which is distinct from NOSPECIES.
    {
        train.average_spectrum = {1.0, 0.0, 0.0, 0.0, 0.0, 1.0};
        const CtTemplateClassifier classifier(config);
        const auto result = classifier.classify_detailed(train);
        if (result.species_id != kCtPreClassifierFlag || result.correlation >= config.correlation_threshold) {
            std::cerr << "Mismatched spectrum should return PRECLASSIFIERFLAG\n";
            return false;
        }
    }

    // A missing average spectrum also returns PRECLASSIFIERFLAG.
    {
        pamguard::detectors::CtTrainSummary empty;
        const CtTemplateClassifier classifier(config);
        if (classifier.classify_detailed(empty).species_id != kCtPreClassifierFlag) {
            std::cerr << "Missing average spectrum should return PRECLASSIFIERFLAG\n";
            return false;
        }
    }
    return true;
}

bool check_classifier_chain() {
    using pamguard::detectors::CtClassifierChain;
    using pamguard::detectors::CtIdiClassifierAdapter;

    CtChi2ClassifierConfig pre;
    pre.chi2_threshold = 1500.0;
    pre.min_clicks = 5;
    pre.species_flag = kSpecies;

    auto failing_idi_config = passing_idi_config();
    failing_idi_config.max_median_idi = 0.1;
    std::vector<std::shared_ptr<const pamguard::detectors::CtClassifier>> classifiers{
        std::make_shared<const CtIdiClassifierAdapter>(failing_idi_config),
        std::make_shared<const CtIdiClassifierAdapter>(passing_idi_config()),
    };

    // Pre-classifier rejection flags junk and short-circuits.
    {
        const CtClassifierChain chain(pre, classifiers);
        auto train = good_train();
        train.chi2 = 9999.0;
        const auto result = chain.classify(train);
        if (!result.junk_train || !result.classifications.empty() ||
            result.species_id != kCtNoSpecies) {
            std::cerr << "Pre-classifier rejection should flag junk and skip classification\n";
            return false;
        }
    }

    // Otherwise every classifier runs and the FIRST match sets the index.
    {
        const CtClassifierChain chain(pre, classifiers);
        const auto result = chain.classify(good_train());
        if (result.junk_train || result.classifications.size() != 2 ||
            result.classification_index != 1 || result.species_id != kSpecies) {
            std::cerr << "Chain should retain all verdicts with the first match setting the index\n";
            return false;
        }
    }

    // Classifiers disabled: pre-classifier passes but nothing else runs.
    {
        const CtClassifierChain chain(pre, classifiers, false);
        const auto result = chain.classify(good_train());
        if (result.junk_train || !result.classifications.empty()) {
            std::cerr << "Disabled classifier chain should run only the pre-classifier\n";
            return false;
        }
    }
    return true;
}

} // namespace

int main() {
    try {
        if (!check_idi_classifier()) {
            return 1;
        }
        if (!check_standard_classifier()) {
            return 1;
        }
        if (!check_template_classifier()) {
            return 1;
        }
        if (!check_classifier_chain()) {
            return 1;
        }
        if (!check_bearing_classifier()) {
            return 1;
        }
        if (!check_correlation_chi2()) {
            return 1;
        }
        std::cout << "CT IDI and bearing classifier branch coverage passed\n";
        std::cout << "CT standard classifier and chain coverage passed\n";
        std::cout << "MHT correlation chi2 behaviour passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
