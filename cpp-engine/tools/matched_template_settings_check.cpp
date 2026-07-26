#include "pamguard/core/MatchedTemplateSettings.h"
#include "pamguard/dsp/WavInterpolator.h"
#include "pamguard/project/MatchedTemplateControlledUnit.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <json.hpp>

namespace {

using Json = nlohmann::json;
using pamguard::core::MatchedTemplateSettingsError;
using pamguard::core::matched_template_default_settings;
using pamguard::core::matched_template_default_settings_json;
using pamguard::core::matched_template_runtime_settings_json;
using pamguard::core::matched_template_settings_from_json;
using pamguard::core::matched_template_settings_to_json;
using pamguard::dsp::wav_interpolator_decimate;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string read_text(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error(
            "Could not open fixture '" + path + "'");
    }
    std::ostringstream output;
    output << input.rdbuf();
    return output.str();
}

std::vector<std::string> split_csv(
    const std::string& line) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= line.size()) {
        const auto comma = line.find(',', start);
        result.push_back(line.substr(
            start,
            comma == std::string::npos
                ? std::string::npos
                : comma - start));
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    return result;
}

void check_java_defaults(
    const std::string& fixture_path) {
    const auto fixture =
        Json::parse(read_text(fixture_path));
    const auto settings =
        matched_template_default_settings();
    const auto canonical =
        Json::parse(
            matched_template_default_settings_json());
    require(
        canonical == fixture,
        "Matched Template portable defaults diverged from the Java fixture");
    require(
        settings.click_type == 101 &&
            settings.normalisation_type == 1 &&
            settings.peak_search &&
            settings.peak_smoothing == 5 &&
            settings.length_db == 6.0 &&
            settings.restricted_bins == 2048 &&
            settings.channel_classification == 0 &&
            settings.classifiers.size() == 1 &&
            settings.classifiers[0].threshold_to_accept == 0.01 &&
            settings.classifiers[0].normalisation_type == 0 &&
            settings.classifiers[0].match_template.name ==
                "Beaked Whale" &&
            settings.classifiers[0].match_template.sample_rate_hz ==
                192000.0 &&
            settings.classifiers[0].match_template.waveform.size() ==
                192 &&
            settings.classifiers[0].reject_template.name ==
                "Dolphin" &&
            settings.classifiers[0].reject_template.waveform.size() ==
                192,
        "Matched Template Java defaults are incomplete");
    require(
        matched_template_settings_from_json(
            matched_template_settings_to_json(settings),
            1) == settings,
        "Matched Template settings did not round-trip canonically");

    auto decimal_rate = canonical;
    decimal_rate["classifiers"][0]
                ["matchTemplate"]["sampleRateHz"] =
        48000.123456789;
    const auto decimal_settings =
        matched_template_settings_from_json(
            decimal_rate.dump(),
            1);
    require(
        decimal_settings.classifiers[0]
                .match_template.sample_rate_hz ==
            static_cast<double>(
                static_cast<float>(
                    48000.123456789)),
        "Matched Template imported rate did not quantise to Java float");

    const auto runtime = Json::parse(
        matched_template_runtime_settings_json(
            settings));
    require(
        runtime.at("clickType") == 101 &&
            runtime.at("classifiers").at(0).at(
                "normalisation") == 0,
        "Matched Template runtime adapter lost Java semantics");

    auto stable_unsigned_type = canonical;
    stable_unsigned_type["clickType"] = 200;
    require(
        matched_template_settings_from_json(
            stable_unsigned_type.dump(),
            1).click_type == 200,
        "Matched Template did not preserve the documented stable unsigned "
        "view of Java click types 128..255");

    pamguard::detectors::MatchedTemplateClassifierConfig
        classifier_config;
    classifier_config.enabled = true;
    classifier_config.normalisation_type =
        settings.normalisation_type;
    classifier_config.peak_search =
        settings.peak_search;
    classifier_config.peak_smoothing =
        settings.peak_smoothing;
    classifier_config.length_db =
        settings.length_db;
    classifier_config.restricted_bins =
        settings.restricted_bins;
    classifier_config.channel_classification =
        settings.channel_classification;
    classifier_config.classifiers =
        settings.classifiers;
    pamguard::detectors::MatchedTemplateClassifier
        classifier_48k(48000.0, classifier_config);
    require(
        classifier_48k.valid(),
        "Java default 192 kHz templates are not valid at a 48 kHz source");
    std::vector<double> click(256);
    for (std::size_t index = 0;
         index < click.size();
         ++index) {
        click[index] =
            std::sin(
                static_cast<double>(index) *
                0.173);
    }
    const auto downsampled_result =
        classifier_48k.classify({click});
    require(
        downsampled_result.best_results.size() == 1,
        "Java default templates did not execute after 192 kHz to 48 kHz "
        "WavInterpolator decimation");
}

void check_validation() {
    const auto defaults =
        Json::parse(
            matched_template_default_settings_json());
    std::vector<Json> invalid;
    auto value = defaults;
    value["extra"] = 1;
    invalid.push_back(value);
    value = defaults;
    value["clickType"] = 99;
    invalid.push_back(value);
    value = defaults;
    value["clickType"] = 256;
    invalid.push_back(value);
    value = defaults;
    value["channelClassification"] = 2;
    invalid.push_back(value);
    value = defaults;
    value["peakSmoothing"] = 4;
    invalid.push_back(value);
    value = defaults;
    value["restrictedBins"] = 2000;
    invalid.push_back(value);
    value = defaults;
    value["classifiers"] = Json::array();
    invalid.push_back(value);
    value = defaults;
    value["classifiers"][0]["thresholdToAccept"] = 5001;
    invalid.push_back(value);
    value = defaults;
    value["classifiers"][0]["matchTemplate"]["waveform"] =
        Json::array();
    invalid.push_back(value);
    value = defaults;
    value["classifiers"][0]["normalisation"] = 3;
    invalid.push_back(value);

    for (const auto& candidate : invalid) {
        bool rejected = false;
        try {
            (void) matched_template_settings_from_json(
                candidate.dump(),
                1);
        }
        catch (const MatchedTemplateSettingsError&) {
            rejected = true;
        }
        require(
            rejected,
            "Invalid Matched Template settings were accepted");
    }
}

void check_direct_classifier_preflight() {
    using pamguard::detectors::MatchTemplateWaveform;
    using pamguard::detectors::MatchedTemplateClassifier;
    using pamguard::detectors::MatchedTemplateClassifierConfig;
    using pamguard::detectors::MtTemplatePair;

    MatchedTemplateClassifierConfig empty;
    empty.enabled = true;
    require(
        !MatchedTemplateClassifier(48000.0, empty).valid(),
        "Direct Matched Template config accepted zero classifiers");

    MtTemplatePair pair;
    pair.match_template = MatchTemplateWaveform{
        "float-rate match",
        48000.0001,
        {1.0, -1.0},
    };
    pair.reject_template = MatchTemplateWaveform{
        "float-rate reject",
        48000.0001,
        {0.5, -0.5},
    };
    MatchedTemplateClassifierConfig float_rate;
    float_rate.enabled = true;
    float_rate.classifiers.push_back(pair);
    require(
        MatchedTemplateClassifier(48000.0, float_rate).valid(),
        "Direct template rates did not quantise to Java float before "
        "branch selection");

    auto too_short = float_rate;
    too_short.classifiers[0].match_template.sample_rate_hz =
        96000.0;
    require(
        !MatchedTemplateClassifier(48000.0, too_short).valid(),
        "Direct downsampling input shorter than the Java cubic-spline "
        "minimum was not rejected during prepare");

    const auto empty_decimation =
        wav_interpolator_decimate(
            std::vector<double>{1.0, -1.0, 0.5, -0.5, 0.25},
            1000000.0,
            48000.0);
    require(
        empty_decimation.empty(),
        "Zero-length WavInterpolator decimation diverged from Java");
}

void check_resampling(
    const std::string& fixture_path,
    const std::string& preset_catalogue_path) {
    std::ifstream input(fixture_path);
    if (!input) {
        throw std::runtime_error(
            "Could not open Matched Template resample fixture");
    }
    const auto presets =
        Json::parse(read_text(preset_catalogue_path));
    std::string header;
    std::getline(input, header);
    require(
        header ==
            "name,sourceRateHz,targetRateHz,inputSamples,"
            "outputSamples,values",
        "Matched Template resample fixture header changed");

    std::string line;
    std::size_t case_count = 0;
    double maximum_error = 0.0;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        const auto cells = split_csv(line);
        require(
            cells.size() >= 6,
            "Matched Template resample fixture row is incomplete");
        const double source_rate = std::stod(cells[1]);
        const double target_rate = std::stod(cells[2]);
        const auto input_samples =
            static_cast<std::size_t>(
                std::stoull(cells[3]));
        const auto output_samples =
            static_cast<std::size_t>(
                std::stoull(cells[4]));

        std::size_t preset_index = 0;
        if (cells[0].starts_with("dolphin")) {
            preset_index = 1;
        }
        else if (cells[0].starts_with("porpoise")) {
            preset_index = 2;
        }
        else if (cells[0].starts_with("sperm")) {
            preset_index = 3;
        }
        const auto waveform =
            presets.at("templates")
                .at(preset_index)
                .at("waveform")
                .get<std::vector<double>>();
        require(
            waveform.size() == input_samples,
            "Matched Template preset and resample fixture disagree");
        const auto actual =
            wav_interpolator_decimate(
                waveform,
                source_rate,
                target_rate);
        require(
            actual.size() == output_samples &&
                cells.size() == 5 + output_samples,
            "Matched Template resample length diverged from Java");
        for (std::size_t index = 0;
             index < output_samples;
             ++index) {
            const double expected =
                std::stod(cells[5 + index]);
            const double scale = std::max(
                {1.0, std::abs(expected),
                 std::abs(actual[index])});
            const double error =
                std::abs(actual[index] - expected) /
                scale;
            maximum_error =
                std::max(maximum_error, error);
            if (error > 5e-13) {
                throw std::runtime_error(
                    cells[0] +
                    " resample value diverged at sample " +
                    std::to_string(index) +
                    " by " + std::to_string(error));
            }
        }
        ++case_count;
    }
    require(
        case_count == 4,
        "Matched Template resample fixture case count changed");
    std::cout
        << "WavInterpolator parity: cases=" << case_count
        << " maxRelError=" << maximum_error << '\n';
}

void check_descriptor() {
    const auto descriptor =
        pamguard::project::
            make_matched_template_controlled_unit_descriptor();
    require(
        descriptor.id ==
                "pamguard.matched-template-classifier" &&
            descriptor.java_authority.registered_name ==
                "Matched Template Click Classifer" &&
            descriptor.java_authority.menu_group ==
                "Classifiers" &&
            descriptor.public_roles.size() == 3 &&
            descriptor.public_roles[0].id == "clicks" &&
            descriptor.public_roles[0].
                    default_provider_controlled_unit_type_id ==
                "pamguard.click-detector" &&
            descriptor.settings.default_settings_json ==
                matched_template_default_settings_json() &&
            descriptor.runtime_recipe.children.size() == 1 &&
            descriptor.runtime_recipe.children[0].settings.adapter_id ==
                "pamguard.matched-template-settings.v1" &&
            descriptor.runtime_recipe.public_role_mappings.size() ==
                3,
        "Matched Template controlled-unit descriptor is incomplete");
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 4) {
            std::cerr
                << "Usage: matched_template_settings_check "
                   "<settings-defaults.json> <template-resample.csv> "
                   "<preset-catalogue.json>\n";
            return 2;
        }
        check_java_defaults(argv[1]);
        check_validation();
        check_direct_classifier_preflight();
        check_resampling(argv[2], argv[3]);
        check_descriptor();
        std::cout
            << "Matched Template Java defaults, strict settings, "
               "runtime adapter, resampler, and descriptor passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
