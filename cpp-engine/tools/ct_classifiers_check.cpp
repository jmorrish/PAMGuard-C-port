#include <cmath>
#include <iostream>
#include <numbers>
#include <string>
#include <vector>

#include "pamguard/detectors/CtClassifiers.h"

namespace {

using pamguard::detectors::CtBearingClassifier;
using pamguard::detectors::CtBearingClassifierConfig;
using pamguard::detectors::CtBearingClick;
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

} // namespace

int main() {
    try {
        if (!check_idi_classifier()) {
            return 1;
        }
        if (!check_bearing_classifier()) {
            return 1;
        }
        std::cout << "CT IDI and bearing classifier branch coverage passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
