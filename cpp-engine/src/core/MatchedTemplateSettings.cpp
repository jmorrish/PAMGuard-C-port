#include "pamguard/core/MatchedTemplateSettings.h"

#include <cmath>
#include <limits>
#include <set>
#include <string>
#include <utility>

#include <json.hpp>

namespace pamguard::core {

namespace {

using Json = nlohmann::json;

constexpr std::size_t kMaximumClassifiers = 64;
constexpr std::size_t kMaximumTemplateSamples = 1U << 20;

constexpr std::string_view kJavaDefaultSettings = R"json(
{"clickType":101,"normalisationType":1,"peakSearch":true,"peakSmoothing":5,"lengthDb":6.0,"restrictedBins":2048,"channelClassification":0,"classifiers":[{"thresholdToAccept":0.01,"normalisation":0,"matchTemplate":{"name":"Beaked Whale","sampleRateHz":192000.0,"waveform":[2.47E-4,4.17E-4,-2.08E-4,-4.96E-4,2.1E-4,1.77E-4,-1.94E-4,4.12E-4,-3.52E-4,-4.06E-4,5.34E-4,1.78E-4,-1.57E-4,-3.78E-4,6.04E-4,2.92E-4,-0.00111,-9.88E-5,4.69E-4,4.78E-4,-2.56E-4,-6.52E-4,9.52E-4,7.76E-4,-0.00124,-0.00234,-7.72E-4,0.00515,0.0099,-0.00751,-0.0234,0.0101,0.0253,-0.0119,-0.0114,0.00845,4.52E-4,-0.00741,-0.00362,0.0155,0.0074,-0.0144,-2.38E-4,7.15E-4,-0.012,3.95E-4,0.0157,0.00498,0.00192,0.0154,-0.0255,-0.0513,0.0206,0.0493,0.00404,0.0107,-0.00428,-0.0629,-0.0266,0.0346,0.0353,0.0377,0.021,-0.0572,-0.0811,-0.0025,0.0414,0.0701,0.0757,-0.0429,-0.131,-0.0797,0.0448,0.141,0.117,-0.0382,-0.192,-0.116,0.0649,0.17,0.165,-0.103,-0.266,-0.0451,0.166,0.208,0.00935,-0.298,-0.126,0.283,0.167,-0.197,-0.205,0.0955,0.264,-0.0356,-0.288,0.023,0.249,-0.0265,-0.177,0.0171,0.122,0.00603,-0.118,0.0124,0.112,-0.0687,-0.0349,0.0668,-0.0383,-0.0176,0.0404,0.00481,-0.0435,0.00324,0.0549,-0.0368,-0.0193,0.0383,-0.0245,-0.00669,0.0395,-0.0197,-0.0361,0.0418,0.00372,-0.0375,0.0354,-0.00481,-0.033,0.0352,0.0025,-0.0308,0.018,0.014,-0.0237,0.00451,0.0171,-0.0181,-6.25E-4,0.0183,-0.0133,-0.00961,0.0196,-0.00214,-0.0197,0.0143,0.00988,-0.0185,0.00562,0.0113,-0.0141,-6.57E-4,0.0133,-0.00673,-0.00721,0.0107,-0.00259,-0.0103,0.0108,0.00515,-0.0125,7.48E-4,0.00956,-0.00528,-0.00546,0.00789,1.54E-4,-0.00815,0.00509,0.00607,-0.00792,-7.59E-4,0.00878,-0.00557,-0.00539,0.00847,-0.00148,-0.00702,0.0052,0.00333,-0.00612,8.16E-4,0.00654,-0.00425,-0.00356,0.00457,-6.18E-4,-0.00187,0.00152,3.46E-4]},"rejectTemplate":{"name":"Dolphin","sampleRateHz":192000.0,"waveform":[3.56E-5,0.00113,-0.00128,4.39E-5,1.17E-4,4.11E-4,-3.05E-4,1.57E-4,-3.45E-4,5.21E-4,-5.18E-4,0.00193,-8.44E-4,-1.62E-4,-8.8E-4,2.74E-4,-4.06E-4,0.00132,-7.23E-4,5.7E-4,-6.91E-4,6.78E-4,-7.04E-4,-0.00136,-4.1E-4,0.00141,-0.00111,0.00136,-9.02E-4,-2.69E-4,-1.49E-4,2.05E-4,-1.53E-4,6.97E-5,-7.78E-4,9.78E-4,-0.00104,-9.0E-5,-8.66E-4,6.78E-5,-0.00106,0.00139,-6.03E-4,3.49E-4,2.8E-4,9.42E-4,-3.65E-4,-5.98E-4,-1.91E-4,4.0E-4,-0.00122,4.52E-4,-0.00135,8.43E-4,-0.0012,5.83E-4,-3.36E-4,0.00126,-2.39E-4,1.63E-4,-0.00131,0.00115,9.6E-4,0.00158,-5.09E-4,0.00116,-0.00115,-2.38E-4,-3.77E-4,1.37E-4,-0.00157,0.0013,-9.01E-4,0.001,-9.13E-6,8.95E-4,-9.26E-4,3.68E-4,-6.05E-5,5.16E-4,-2.56E-4,0.00245,-0.00146,0.0018,-5.76E-4,0.00427,0.0046,0.0162,0.0277,0.0465,-0.0296,-0.17,-0.327,0.198,0.73,-0.169,-0.494,0.0359,0.108,0.027,-0.00951,-0.0252,-0.0119,0.00512,0.00354,0.00935,0.00657,0.00581,-0.00182,-0.00381,-0.0173,-0.00326,0.0166,0.0147,-1.78E-4,-0.0135,-0.00518,0.00596,0.0012,-0.00203,8.27E-4,0.00308,0.00734,0.00249,-0.00853,-0.00358,-0.00446,0.00239,0.0067,0.00208,-0.00154,3.96E-5,-3.75E-4,4.47E-5,9.08E-4,0.00113,-4.51E-4,-0.00116,-0.00238,0.00133,0.00409,-4.09E-5,-0.00257,-7.47E-5,-4.94E-4,-1.12E-4,-2.1E-4,3.48E-4,-1.33E-4,-8.04E-4,-1.28E-4,2.31E-4,0.00163,0.00406,-0.0019,-0.00481,-0.00343,0.00205,0.00264,0.00105,-0.00125,-0.00152,-2.15E-4,5.58E-4,0.00109,-2.92E-4,-7.16E-5,-1.62E-4,-7.08E-4,0.00137,-3.63E-4,-1.01E-4,-9.1E-4,4.76E-4,-4.89E-4,6.0E-4,-0.00197,-3.72E-4,-0.00123,-0.00213,0.0018,0.00172,-4.45E-4,8.07E-4,0.00273,0.00187,-3.51E-4,-0.00179,-4.25E-5,-3.6E-4,-0.00636,-0.0038]}}]}
)json";

void require_version(std::uint32_t settings_version) {
    if (settings_version != 1) {
        throw MatchedTemplateSettingsError(
            "Unsupported Matched Template settings version");
    }
}

Json parse_json(
    std::string_view value,
    std::string_view context) {
    try {
        return Json::parse(value);
    }
    catch (const std::exception& error) {
        throw MatchedTemplateSettingsError(
            std::string(context) + " are not valid JSON: " +
            error.what());
    }
}

void require_exact_fields(
    const Json& value,
    const std::set<std::string>& expected,
    std::string_view context) {
    if (!value.is_object() ||
        value.size() != expected.size()) {
        throw MatchedTemplateSettingsError(
            std::string(context) +
            " do not contain the complete exact field set");
    }
    for (const auto& [name, _] : value.items()) {
        if (!expected.contains(name)) {
            throw MatchedTemplateSettingsError(
                std::string(context) +
                " contain unknown field '" + name + "'");
        }
    }
}

int bounded_integer(
    const Json& value,
    int minimum,
    int maximum,
    std::string_view context) {
    if (!value.is_number_integer()) {
        throw MatchedTemplateSettingsError(
            std::string(context) + " must be an integer");
    }
    const auto result = value.get<std::int64_t>();
    if (result < minimum || result > maximum) {
        throw MatchedTemplateSettingsError(
            std::string(context) + " is outside the Java range");
    }
    return static_cast<int>(result);
}

double finite_number(
    const Json& value,
    std::string_view context) {
    if (!value.is_number()) {
        throw MatchedTemplateSettingsError(
            std::string(context) + " must be a number");
    }
    const auto result = value.get<double>();
    if (!std::isfinite(result)) {
        throw MatchedTemplateSettingsError(
            std::string(context) + " must be finite");
    }
    return result;
}

bool boolean_value(
    const Json& value,
    std::string_view context) {
    if (!value.is_boolean()) {
        throw MatchedTemplateSettingsError(
            std::string(context) + " must be a boolean");
    }
    return value.get<bool>();
}

std::string bounded_name(
    const Json& value,
    std::string_view context) {
    if (!value.is_string()) {
        throw MatchedTemplateSettingsError(
            std::string(context) + " must be a string");
    }
    auto result = value.get<std::string>();
    if (result.empty() || result.size() > 256) {
        throw MatchedTemplateSettingsError(
            std::string(context) +
            " must contain 1 to 256 bytes");
    }
    return result;
}

detectors::MatchTemplateWaveform template_from_json(
    const Json& value,
    std::string_view context) {
    require_exact_fields(
        value,
        {"name", "sampleRateHz", "waveform"},
        context);
    detectors::MatchTemplateWaveform result;
    result.name = bounded_name(
        value.at("name"),
        std::string(context) + ".name");
    const double parsed_sample_rate = finite_number(
        value.at("sampleRateHz"),
        std::string(context) + ".sampleRateHz");
    // MatchTemplate.sR is a Java float. Quantise at the portable boundary so
    // branch selection, output length, and filter cutoff remain identical for
    // imported non-integer rates.
    result.sample_rate_hz =
        static_cast<double>(
            static_cast<float>(
                parsed_sample_rate));
    if (!(result.sample_rate_hz > 0.0) ||
        !std::isfinite(result.sample_rate_hz)) {
        throw MatchedTemplateSettingsError(
            std::string(context) +
            ".sampleRateHz must fit a finite positive Java float");
    }
    const auto& waveform = value.at("waveform");
    if (!waveform.is_array() || waveform.empty() ||
        waveform.size() > kMaximumTemplateSamples) {
        throw MatchedTemplateSettingsError(
            std::string(context) +
            ".waveform must contain 1 to 1048576 samples");
    }
    result.waveform.reserve(waveform.size());
    for (std::size_t index = 0;
         index < waveform.size();
         ++index) {
        result.waveform.push_back(finite_number(
            waveform.at(index),
            std::string(context) + ".waveform[" +
                std::to_string(index) + "]"));
    }
    return result;
}

Json template_to_json(
    const detectors::MatchTemplateWaveform& value) {
    const Json candidate{
        {"name", value.name},
        {"sampleRateHz", value.sample_rate_hz},
        {"waveform", value.waveform},
    };
    const auto normalized =
        template_from_json(
            candidate,
            "Matched Template template");
    return {
        {"name", normalized.name},
        {"sampleRateHz", normalized.sample_rate_hz},
        {"waveform", normalized.waveform},
    };
}

detectors::MtTemplatePair classifier_from_json(
    const Json& value,
    std::size_t index) {
    const auto context =
        "Matched Template classifiers[" +
        std::to_string(index) + "]";
    require_exact_fields(
        value,
        {
            "thresholdToAccept",
            "normalisation",
            "matchTemplate",
            "rejectTemplate",
        },
        context);
    detectors::MtTemplatePair result;
    result.threshold_to_accept = finite_number(
        value.at("thresholdToAccept"),
        context + ".thresholdToAccept");
    if (result.threshold_to_accept < -5000.0 ||
        result.threshold_to_accept > 5000.0) {
        throw MatchedTemplateSettingsError(
            context +
            ".thresholdToAccept is outside the Java spinner range");
    }
    result.normalisation_type = bounded_integer(
        value.at("normalisation"),
        0,
        2,
        context + ".normalisation");
    result.match_template = template_from_json(
        value.at("matchTemplate"),
        context + ".matchTemplate");
    result.reject_template = template_from_json(
        value.at("rejectTemplate"),
        context + ".rejectTemplate");
    return result;
}

Json classifier_to_json(
    const detectors::MtTemplatePair& value) {
    Json result{
        {"thresholdToAccept", value.threshold_to_accept},
        {"normalisation", value.normalisation_type},
        {"matchTemplate", template_to_json(value.match_template)},
        {"rejectTemplate", template_to_json(value.reject_template)},
    };
    (void) classifier_from_json(result, 0);
    return result;
}

void validate_settings(
    const MatchedTemplateSettings& settings) {
    if (settings.click_type != 0 &&
        (settings.click_type < 100 ||
         settings.click_type > 255)) {
        throw MatchedTemplateSettingsError(
            "Matched Template clickType must be 100 to 255, or 0 for "
            "the Java spinner's byte-wrapped 256 value");
    }
    if (settings.normalisation_type < 0 ||
        settings.normalisation_type > 2) {
        throw MatchedTemplateSettingsError(
            "Matched Template normalisationType must be 0, 1, or 2");
    }
    if (settings.channel_classification < 0 ||
        settings.channel_classification > 1) {
        throw MatchedTemplateSettingsError(
            "Matched Template channelClassification must be 0 or 1");
    }
    if (settings.peak_smoothing < 3 ||
        settings.peak_smoothing > 1025 ||
        (settings.peak_smoothing % 2) == 0) {
        throw MatchedTemplateSettingsError(
            "Matched Template peakSmoothing must be an odd Java list value from 3 to 1025");
    }
    if (!std::isfinite(settings.length_db) ||
        settings.length_db < 0.1 ||
        settings.length_db > 300.0) {
        throw MatchedTemplateSettingsError(
            "Matched Template lengthDb is outside the Java spinner range");
    }
    if (settings.restricted_bins < 4 ||
        settings.restricted_bins > 131072 ||
        (settings.restricted_bins &
         (settings.restricted_bins - 1)) != 0) {
        throw MatchedTemplateSettingsError(
            "Matched Template restrictedBins must be a Java power-of-two list value from 4 to 131072");
    }
    if (settings.classifiers.empty() ||
        settings.classifiers.size() >
            kMaximumClassifiers) {
        throw MatchedTemplateSettingsError(
            "Matched Template requires 1 to 64 classifier tabs");
    }
    for (std::size_t index = 0;
         index < settings.classifiers.size();
         ++index) {
        (void) classifier_to_json(
            settings.classifiers[index]);
    }
}

Json settings_to_value(
    const MatchedTemplateSettings& settings) {
    validate_settings(settings);
    Json classifiers = Json::array();
    for (const auto& classifier : settings.classifiers) {
        classifiers.push_back(
            classifier_to_json(classifier));
    }
    return {
        {"clickType", settings.click_type},
        {"normalisationType",
         settings.normalisation_type},
        {"peakSearch", settings.peak_search},
        {"peakSmoothing", settings.peak_smoothing},
        {"lengthDb", settings.length_db},
        {"restrictedBins", settings.restricted_bins},
        {"channelClassification",
         settings.channel_classification},
        {"classifiers", std::move(classifiers)},
    };
}

MatchedTemplateSettings settings_from_value(
    const Json& value) {
    require_exact_fields(
        value,
        {
            "clickType",
            "normalisationType",
            "peakSearch",
            "peakSmoothing",
            "lengthDb",
            "restrictedBins",
            "channelClassification",
            "classifiers",
        },
        "Matched Template settings");
    MatchedTemplateSettings result;
    result.click_type = bounded_integer(
        value.at("clickType"),
        0,
        255,
        "Matched Template clickType");
    result.normalisation_type = bounded_integer(
        value.at("normalisationType"),
        0,
        2,
        "Matched Template normalisationType");
    result.peak_search = boolean_value(
        value.at("peakSearch"),
        "Matched Template peakSearch");
    result.peak_smoothing = bounded_integer(
        value.at("peakSmoothing"),
        3,
        1025,
        "Matched Template peakSmoothing");
    result.length_db = finite_number(
        value.at("lengthDb"),
        "Matched Template lengthDb");
    result.restricted_bins = bounded_integer(
        value.at("restrictedBins"),
        4,
        131072,
        "Matched Template restrictedBins");
    result.channel_classification = bounded_integer(
        value.at("channelClassification"),
        0,
        1,
        "Matched Template channelClassification");
    const auto& classifiers = value.at("classifiers");
    if (!classifiers.is_array() ||
        classifiers.empty() ||
        classifiers.size() > kMaximumClassifiers) {
        throw MatchedTemplateSettingsError(
            "Matched Template classifiers must contain 1 to 64 tabs");
    }
    result.classifiers.reserve(classifiers.size());
    for (std::size_t index = 0;
         index < classifiers.size();
         ++index) {
        result.classifiers.push_back(
            classifier_from_json(
                classifiers.at(index),
                index));
    }
    validate_settings(result);
    return result;
}

} // namespace

MatchedTemplateSettings
matched_template_settings_from_json(
    std::string_view settings_json,
    std::uint32_t settings_version) {
    require_version(settings_version);
    return settings_from_value(parse_json(
        settings_json,
        "Matched Template settings"));
}

std::string matched_template_settings_to_json(
    const MatchedTemplateSettings& settings,
    std::uint32_t settings_version) {
    require_version(settings_version);
    return settings_to_value(settings).dump();
}

MatchedTemplateSettings
matched_template_default_settings() {
    return matched_template_settings_from_json(
        kJavaDefaultSettings,
        1);
}

std::string matched_template_default_settings_json() {
    return matched_template_settings_to_json(
        matched_template_default_settings());
}

std::string matched_template_runtime_settings_json(
    const MatchedTemplateSettings& settings) {
    return settings_to_value(settings).dump();
}

std::string_view
matched_template_settings_schema_json() noexcept {
    return R"json({
        "$schema":"https://json-schema.org/draft/2020-12/schema",
        "x-pamguard-authority":{
            "version":"2.02.18e",
            "commit":"dca55c81ef6f1498a8a3b926c69e7182afb915ee",
            "settingsClass":"matchedTemplateClassifer.MatchedTemplateParams",
            "classifierClass":"matchedTemplateClassifer.MTClassifier",
            "templateClass":"matchedTemplateClassifer.MatchTemplate",
            "dialogClass":"matchedTemplateClassifer.layoutFX.MTSettingsPane",
            "processClass":"matchedTemplateClassifer.MTProcess"
        },
        "x-pamguard-portable-deviations":[
            "dataSourceName and dataSourceIndex are represented by the public clicks binding",
            "pamSymbol is a Java display preference and is excluded",
            "enableFFTFilter and fftFilterParams are dormant fields not read by MTProcess and are excluded",
            "clickType stores a stable unsigned 128..255 instead of reproducing Java's signed-byte reopen and downstream-classification bugs; the Java spinner's 256 alias is normalized to portable 0",
            "the live port currently accepts ClickDetection waveform inputs only; Java CTDataUnit average-waveform classification and MATCHEDCLICK train flags are not implemented",
            "the Java click code/name provider is not yet exposed; portable annotations retain the configured numeric clickType and classifier instance identity",
            "CSV template import is available, but MATLAB MAT import is currently unavailable; host file paths are never persisted",
            "viewer-only offline reclassification is not part of the live runtime",
            "finite values and implementation-safe list limits are enforced at the portable boundary"
        ],
        "type":"object",
        "additionalProperties":false,
        "properties":{
            "clickType":{
                "type":"integer",
                "anyOf":[
                    {"const":0},
                    {"minimum":100,"maximum":255}
                ]
            },
            "normalisationType":{
                "type":"integer",
                "enum":[0,1,2]
            },
            "peakSearch":{"type":"boolean"},
            "peakSmoothing":{
                "type":"integer",
                "minimum":3,
                "maximum":1025
            },
            "lengthDb":{
                "type":"number",
                "minimum":0.1,
                "maximum":300
            },
            "restrictedBins":{
                "type":"integer",
                "minimum":4,
                "maximum":131072
            },
            "channelClassification":{
                "type":"integer",
                "enum":[0,1]
            },
            "classifiers":{
                "type":"array",
                "minItems":1,
                "maxItems":64,
                "items":{"$ref":"#/$defs/classifier"}
            }
        },
        "required":[
            "clickType",
            "normalisationType",
            "peakSearch",
            "peakSmoothing",
            "lengthDb",
            "restrictedBins",
            "channelClassification",
            "classifiers"
        ],
        "$defs":{
            "template":{
                "type":"object",
                "additionalProperties":false,
                "properties":{
                    "name":{
                        "type":"string",
                        "minLength":1,
                        "maxLength":256
                    },
                    "sampleRateHz":{
                        "type":"number",
                        "exclusiveMinimum":0
                    },
                    "waveform":{
                        "type":"array",
                        "minItems":1,
                        "maxItems":1048576,
                        "items":{"type":"number"}
                    }
                },
                "required":["name","sampleRateHz","waveform"]
            },
            "classifier":{
                "type":"object",
                "additionalProperties":false,
                "properties":{
                    "thresholdToAccept":{
                        "type":"number",
                        "minimum":-5000,
                        "maximum":5000
                    },
                    "normalisation":{
                        "type":"integer",
                        "enum":[0,1,2]
                    },
                    "matchTemplate":{"$ref":"#/$defs/template"},
                    "rejectTemplate":{"$ref":"#/$defs/template"}
                },
                "required":[
                    "thresholdToAccept",
                    "normalisation",
                    "matchTemplate",
                    "rejectTemplate"
                ]
            }
        },
        "x-pamguardConstraints":[
            {
                "id":"peak-smoothing-java-list",
                "kind":"odd-integer",
                "pointer":"/peakSmoothing"
            },
            {
                "id":"restricted-bins-java-list",
                "kind":"power-of-two",
                "pointer":"/restrictedBins"
            },
            {
                "id":"dialog-synchronises-classifier-normalisation",
                "kind":"dialog-write-through",
                "sourcePointer":"/normalisationType",
                "targetPattern":"/classifiers/*/normalisation"
            }
        ]
    })json";
}

} // namespace pamguard::core
