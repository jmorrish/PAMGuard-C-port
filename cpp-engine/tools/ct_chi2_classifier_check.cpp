#include <iostream>
#include <string>
#include <vector>

#include "pamguard/detectors/CtChi2Classifier.h"

namespace {

using pamguard::detectors::CtChi2Classifier;
using pamguard::detectors::CtChi2ClassifierConfig;
using pamguard::detectors::CtClassifierTrain;
using pamguard::detectors::kCtNoSpecies;

constexpr int kSpecies = 7;

struct Case {
    std::string name;
    CtClassifierTrain train;
    CtChi2ClassifierConfig config;
    int expected_species = kCtNoSpecies;
};

CtChi2ClassifierConfig config_of(double chi2_threshold, std::size_t min_clicks, double min_time_seconds) {
    CtChi2ClassifierConfig config;
    config.chi2_threshold = chi2_threshold;
    config.min_clicks = min_clicks;
    config.min_time_seconds = min_time_seconds;
    config.species_flag = kSpecies;
    return config;
}

// Branch catalogue transcribed from Chi2ThresholdClassifier.classifyClickTrain.
std::vector<Case> case_catalogue() {
    return {
        {"pass-defaults", {500.0, 10, 2000.0}, config_of(1500.0, 5, 0.0), kSpecies},
        {"fail-chi2-above-threshold", {2000.0, 10, 2000.0}, config_of(1500.0, 5, 0.0), kCtNoSpecies},
        // The reference rejects only when chi2 is strictly greater.
        {"pass-chi2-equal-threshold", {1500.0, 10, 2000.0}, config_of(1500.0, 5, 0.0), kSpecies},
        {"fail-too-few-clicks", {500.0, 4, 2000.0}, config_of(1500.0, 5, 0.0), kCtNoSpecies},
        {"pass-clicks-equal-minimum", {500.0, 5, 2000.0}, config_of(1500.0, 5, 0.0), kSpecies},
        {"fail-too-short", {500.0, 10, 900.0}, config_of(1500.0, 5, 1.0), kCtNoSpecies},
        {"pass-duration-equal-minimum", {500.0, 10, 1000.0}, config_of(1500.0, 5, 1.0), kSpecies},
        // A zero threshold disables the chi2 test entirely.
        {"zero-threshold-disables-chi2-test", {99999.0, 10, 2000.0}, config_of(0.0, 5, 0.0), kSpecies},
    };
}

} // namespace

int main() {
    try {
        for (const auto& test_case : case_catalogue()) {
            const CtChi2Classifier classifier(test_case.config);
            const auto result = classifier.classify(test_case.train);
            if (result.species_id != test_case.expected_species) {
                std::cerr << "CT chi2 classifier mismatch for case " << test_case.name << "\n";
                std::cerr << "expected speciesId=" << test_case.expected_species
                          << " actual=" << result.species_id << "\n";
                return 1;
            }
        }

        std::cout << "CT chi2 classifier branch coverage passed\n";
        std::cout << "cases=" << case_catalogue().size() << "\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
