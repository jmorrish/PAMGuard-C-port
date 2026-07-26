#include "pamguard/core/FilterDecimatorSettings.h"

#include <cmath>
#include <limits>
#include <set>
#include <utility>
#include <vector>

#include <json.hpp>

namespace pamguard::core {

namespace {

using Json = nlohmann::json;

Json parse_settings(
    std::string_view settings_json,
    std::string_view unit_name) {
    try {
        return Json::parse(settings_json);
    }
    catch (const std::exception& error) {
        throw FilterDecimatorSettingsError(
            std::string(unit_name) +
            " settings are not valid JSON: " + error.what());
    }
}

void require_version(
    std::uint32_t settings_version,
    std::string_view unit_name) {
    if (settings_version != 1) {
        throw FilterDecimatorSettingsError(
            "Unsupported " + std::string(unit_name) +
            " settings version");
    }
}

void require_exact_fields(
    const Json& value,
    const std::set<std::string>& expected,
    std::string_view context) {
    if (!value.is_object() || value.size() != expected.size()) {
        throw FilterDecimatorSettingsError(
            std::string(context) +
            " must contain exactly the supported fields");
    }
    for (const auto& [name, _] : value.items()) {
        if (!expected.contains(name)) {
            throw FilterDecimatorSettingsError(
                std::string(context) + " contains unknown field '" +
                name + "'");
        }
    }
}

double finite_number(
    const Json& value,
    std::string_view context) {
    if (!value.is_number()) {
        throw FilterDecimatorSettingsError(
            std::string(context) + " must be a number");
    }
    const auto result = value.get<double>();
    if (!std::isfinite(result)) {
        throw FilterDecimatorSettingsError(
            std::string(context) + " must be finite");
    }
    return result;
}

std::uint32_t unsigned_32(
    const Json& value,
    std::string_view context) {
    if (!value.is_number_integer() &&
        !value.is_number_unsigned()) {
        throw FilterDecimatorSettingsError(
            std::string(context) + " must be an integer");
    }
    try {
        const auto result = value.get<std::uint64_t>();
        if (result >
            std::numeric_limits<std::uint32_t>::max()) {
            throw FilterDecimatorSettingsError(
                std::string(context) +
                " must fit PAMGuard's 32-channel/rate range");
        }
        return static_cast<std::uint32_t>(result);
    }
    catch (const FilterDecimatorSettingsError&) {
        throw;
    }
    catch (const std::exception&) {
        throw FilterDecimatorSettingsError(
            std::string(context) +
            " must be a non-negative 32-bit integer");
    }
}

int bounded_integer(
    const Json& value,
    int minimum,
    int maximum,
    std::string_view context) {
    if (!value.is_number_integer()) {
        throw FilterDecimatorSettingsError(
            std::string(context) + " must be an integer");
    }
    const auto result = value.get<std::int64_t>();
    if (result < minimum || result > maximum) {
        throw FilterDecimatorSettingsError(
            std::string(context) + " must be between " +
            std::to_string(minimum) + " and " +
            std::to_string(maximum));
    }
    return static_cast<int>(result);
}

float finite_float(
    const Json& value,
    std::string_view context) {
    const auto number = finite_number(value, context);
    if (number < -std::numeric_limits<float>::max() ||
        number > std::numeric_limits<float>::max()) {
        throw FilterDecimatorSettingsError(
            std::string(context) +
            " cannot be represented by Java FilterParams float precision");
    }
    return static_cast<float>(number);
}

dsp::IirFilterType filter_type(
    const Json& value,
    std::string_view context) {
    if (!value.is_string()) {
        throw FilterDecimatorSettingsError(
            std::string(context) + " must be a string");
    }
    const auto name = value.get<std::string>();
    if (name == "none") {
        return dsp::IirFilterType::None;
    }
    if (name == "butterworth") {
        return dsp::IirFilterType::Butterworth;
    }
    if (name == "chebyshev") {
        return dsp::IirFilterType::Chebyshev;
    }
    if (name == "firWindow") {
        return dsp::IirFilterType::FirWindow;
    }
    if (name == "firArbitrary") {
        return dsp::IirFilterType::FirArbitrary;
    }
    if (name == "fft") {
        return dsp::IirFilterType::Fft;
    }
    throw FilterDecimatorSettingsError(
        std::string(context) +
        " must be a PAMGuard FilterType value");
}

std::string_view filter_type_name(
    dsp::IirFilterType value) {
    switch (value) {
    case dsp::IirFilterType::None:
        return "none";
    case dsp::IirFilterType::Butterworth:
        return "butterworth";
    case dsp::IirFilterType::Chebyshev:
        return "chebyshev";
    case dsp::IirFilterType::FirWindow:
        return "firWindow";
    case dsp::IirFilterType::FirArbitrary:
        return "firArbitrary";
    case dsp::IirFilterType::Fft:
        return "fft";
    }
    throw FilterDecimatorSettingsError(
        "Filter type cannot be serialized");
}

dsp::IirFilterBand filter_band(
    const Json& value,
    std::string_view context) {
    if (!value.is_string()) {
        throw FilterDecimatorSettingsError(
            std::string(context) + " must be a string");
    }
    const auto name = value.get<std::string>();
    if (name == "highPass") {
        return dsp::IirFilterBand::HighPass;
    }
    if (name == "lowPass") {
        return dsp::IirFilterBand::LowPass;
    }
    if (name == "bandPass") {
        return dsp::IirFilterBand::BandPass;
    }
    if (name == "bandStop") {
        return dsp::IirFilterBand::BandStop;
    }
    throw FilterDecimatorSettingsError(
        std::string(context) +
        " must be a PAMGuard FilterBand value");
}

std::string_view filter_band_name(
    dsp::IirFilterBand value) {
    switch (value) {
    case dsp::IirFilterBand::HighPass:
        return "highPass";
    case dsp::IirFilterBand::LowPass:
        return "lowPass";
    case dsp::IirFilterBand::BandPass:
        return "bandPass";
    case dsp::IirFilterBand::BandStop:
        return "bandStop";
    }
    throw FilterDecimatorSettingsError(
        "Filter band cannot be serialized");
}

std::vector<double> finite_array(
    const Json& value,
    std::string_view context) {
    if (!value.is_array()) {
        throw FilterDecimatorSettingsError(
            std::string(context) + " must be an array");
    }
    std::vector<double> result;
    result.reserve(value.size());
    for (std::size_t index = 0;
         index < value.size();
         ++index) {
        result.push_back(finite_number(
            value.at(index),
            std::string(context) + "[" +
                std::to_string(index) + "]"));
    }
    return result;
}

void require_portable_filter_arrays(
    const dsp::IirFilterParams& filter) {
    for (const auto frequency :
         filter.arbitrary_frequencies_hz) {
        if (!std::isfinite(frequency) || frequency < 0.0) {
            throw FilterDecimatorSettingsError(
                "Filter arbitraryFrequenciesHz values must be finite and non-negative");
        }
    }
    for (const auto gain : filter.arbitrary_gains_db) {
        if (!std::isfinite(gain)) {
            throw FilterDecimatorSettingsError(
                "Filter arbitraryGainsDb values must be finite");
        }
    }
}

dsp::IirFilterParams filter_params_from_json(
    const Json& settings,
    std::string_view context) {
    require_exact_fields(
        settings,
        {
            "type",
            "band",
            "order",
            "lowPassFreqHz",
            "highPassFreqHz",
            "passBandRippleDb",
            "stopBandRippleDb",
            "chebyGamma",
            "arbitraryFrequenciesHz",
            "arbitraryGainsDb",
        },
        context);

    dsp::IirFilterParams result;
    result.type =
        filter_type(settings.at("type"), "Filter type");
    result.band =
        filter_band(settings.at("band"), "Filter band");
    result.order = bounded_integer(
        settings.at("order"),
        1,
        32,
        "Filter order");
    result.low_pass_freq_hz = finite_float(
        settings.at("lowPassFreqHz"),
        "Filter lowPassFreqHz");
    result.high_pass_freq_hz = finite_float(
        settings.at("highPassFreqHz"),
        "Filter highPassFreqHz");
    result.pass_band_ripple_db = finite_number(
        settings.at("passBandRippleDb"),
        "Filter passBandRippleDb");
    result.stop_band_ripple_db = finite_number(
        settings.at("stopBandRippleDb"),
        "Filter stopBandRippleDb");
    result.cheby_gamma = finite_number(
        settings.at("chebyGamma"),
        "Filter chebyGamma");
    result.arbitrary_frequencies_hz = finite_array(
        settings.at("arbitraryFrequenciesHz"),
        "Filter arbitraryFrequenciesHz");
    result.arbitrary_gains_db = finite_array(
        settings.at("arbitraryGainsDb"),
        "Filter arbitraryGainsDb");

    require_portable_filter_arrays(result);
    if (result.cheby_gamma <= 0.0) {
        throw FilterDecimatorSettingsError(
            "Filter chebyGamma must be positive");
    }
    const bool iir =
        result.type == dsp::IirFilterType::Butterworth ||
        result.type == dsp::IirFilterType::Chebyshev;
    if (iir && result.order > 1 &&
        (result.order % 2) != 0) {
        throw FilterDecimatorSettingsError(
            "PAMGuard's Filter dialog requires IIR order 1 or an even order");
    }
    try {
        dsp::validate_filter_params(result);
    }
    catch (const std::exception& error) {
        throw FilterDecimatorSettingsError(error.what());
    }
    return result;
}

Json filter_params_to_json(
    const dsp::IirFilterParams& filter) {
    require_portable_filter_arrays(filter);
    try {
        dsp::validate_filter_params(filter);
    }
    catch (const std::exception& error) {
        throw FilterDecimatorSettingsError(error.what());
    }
    const bool iir =
        filter.type == dsp::IirFilterType::Butterworth ||
        filter.type == dsp::IirFilterType::Chebyshev;
    if (filter.order < 1 || filter.order > 32 ||
        (iir && filter.order > 1 &&
         (filter.order % 2) != 0) ||
        !(filter.cheby_gamma > 0.0)) {
        throw FilterDecimatorSettingsError(
            "Filter parameters cannot be represented by the portable contract");
    }
    return {
        {"type", filter_type_name(filter.type)},
        {"band", filter_band_name(filter.band)},
        {"order", filter.order},
        {"lowPassFreqHz", filter.low_pass_freq_hz},
        {"highPassFreqHz", filter.high_pass_freq_hz},
        {"passBandRippleDb", filter.pass_band_ripple_db},
        {"stopBandRippleDb", filter.stop_band_ripple_db},
        {"chebyGamma", filter.cheby_gamma},
        {"arbitraryFrequenciesHz",
         filter.arbitrary_frequencies_hz},
        {"arbitraryGainsDb",
         filter.arbitrary_gains_db},
    };
}

} // namespace

dsp::IirFilterParams standalone_filter_default_params() {
    dsp::IirFilterParams result;
    result.type = dsp::IirFilterType::Butterworth;
    result.band = dsp::IirFilterBand::BandPass;
    result.order = 4;
    result.low_pass_freq_hz = 20000.0F;
    result.high_pass_freq_hz = 2000.0F;
    result.pass_band_ripple_db = 2.0;
    result.stop_band_ripple_db = 2.0;
    result.cheby_gamma = 3.0;
    return result;
}

dsp::IirFilterParams decimator_default_filter_params(
    std::uint32_t output_sample_rate_hz) {
    auto result = standalone_filter_default_params();
    result.band = dsp::IirFilterBand::LowPass;
    result.order = 6;
    result.low_pass_freq_hz =
        static_cast<float>(output_sample_rate_hz) / 2.0F;
    return result;
}

StandaloneFilterSettings
standalone_filter_settings_from_json(
    std::string_view settings_json,
    std::uint32_t settings_version) {
    require_version(settings_version, "Filter");
    const auto settings =
        parse_settings(settings_json, "Filter");
    require_exact_fields(
        settings,
        {
            "channelBitmap",
            "type",
            "band",
            "order",
            "lowPassFreqHz",
            "highPassFreqHz",
            "passBandRippleDb",
            "stopBandRippleDb",
            "chebyGamma",
            "arbitraryFrequenciesHz",
            "arbitraryGainsDb",
        },
        "Filter settings");

    auto filter_only = settings;
    filter_only.erase("channelBitmap");
    return {
        unsigned_32(
            settings.at("channelBitmap"),
            "Filter channelBitmap"),
        filter_params_from_json(
            filter_only,
            "Filter FilterParams"),
    };
}

DecimatorSettings decimator_settings_from_json(
    std::string_view settings_json,
    std::uint32_t settings_version) {
    require_version(settings_version, "Decimator");
    const auto settings =
        parse_settings(settings_json, "Decimator");
    require_exact_fields(
        settings,
        {
            "outputSampleRateHz",
            "filter",
            "interpolation",
            "channelBitmap",
        },
        "Decimator settings");

    const auto output_rate = unsigned_32(
        settings.at("outputSampleRateHz"),
        "Decimator outputSampleRateHz");
    if (output_rate == 0) {
        throw FilterDecimatorSettingsError(
            "Decimator outputSampleRateHz must be positive");
    }
    return {
        output_rate,
        unsigned_32(
            settings.at("channelBitmap"),
            "Decimator channelBitmap"),
        filter_params_from_json(
            settings.at("filter"),
            "Decimator FilterParams"),
        bounded_integer(
            settings.at("interpolation"),
            0,
            2,
            "Decimator interpolation"),
    };
}

std::string standalone_filter_settings_to_json(
    const StandaloneFilterSettings& settings,
    std::uint32_t settings_version) {
    require_version(settings_version, "Filter");
    auto result = filter_params_to_json(settings.filter);
    result["channelBitmap"] = settings.channel_bitmap;
    return result.dump();
}

std::string decimator_settings_to_json(
    const DecimatorSettings& settings,
    std::uint32_t settings_version) {
    require_version(settings_version, "Decimator");
    if (settings.output_sample_rate_hz == 0 ||
        settings.interpolation < 0 ||
        settings.interpolation > 2) {
        throw FilterDecimatorSettingsError(
            "Decimator settings cannot be represented by the portable contract");
    }
    return Json{
        {"outputSampleRateHz",
         settings.output_sample_rate_hz},
        {"filter",
         filter_params_to_json(settings.filter)},
        {"interpolation", settings.interpolation},
        {"channelBitmap", settings.channel_bitmap},
    }.dump();
}

std::string standalone_filter_default_settings_json() {
    return standalone_filter_settings_to_json(
        {0, standalone_filter_default_params()},
        1);
}

std::string decimator_default_settings_json() {
    return decimator_settings_to_json(
        {
            2000,
            0,
            decimator_default_filter_params(2000),
            0,
        },
        1);
}

std::string_view
standalone_filter_settings_schema_json() noexcept {
    return R"({
        "$schema":"https://json-schema.org/draft/2020-12/schema",
        "x-pamguard-authority":{
            "version":"2.02.18e",
            "commit":"dca55c81ef6f1498a8a3b926c69e7182afb915ee",
            "settingsClass":"Filters.FilterParameters_2",
            "filterParamsClass":"Filters.FilterParams",
            "dialogClass":"Filters.FilterDialog",
            "processClass":"Filters.FilterProcess"
        },
        "x-pamguard-portable-deviations":[
            "rawDataSource is represented by the public rawAudio binding",
            "FilterParams is flattened into this single runtime settings object",
            "scaleType, lastImportFile, centreFreq, and the static pole/zero plot preference are UI-local or unused by the standalone DSP and are not persisted",
            "Java null arbitrary FIR arrays are normalized to portable empty arrays",
            "finite values, positive active corners, ordered band edges, and implementation-safe order limits are enforced in addition to the Java dialog checks",
            "the Java dialog cancel/self-source/FIR-gamma defects are not reproduced"
        ],
        "type":"object",
        "additionalProperties":false,
        "properties":{
            "channelBitmap":{
                "type":"integer",
                "minimum":0,
                "maximum":4294967295
            },
            "type":{
                "type":"string",
                "enum":["none","butterworth","chebyshev","firWindow","firArbitrary","fft"]
            },
            "band":{
                "type":"string",
                "enum":["highPass","lowPass","bandPass","bandStop"]
            },
            "order":{
                "type":"integer",
                "minimum":1,
                "maximum":32
            },
            "lowPassFreqHz":{"type":"number","minimum":0},
            "highPassFreqHz":{"type":"number","minimum":0},
            "passBandRippleDb":{"type":"number","minimum":0},
            "stopBandRippleDb":{"type":"number","minimum":0},
            "chebyGamma":{"type":"number","exclusiveMinimum":0},
            "arbitraryFrequenciesHz":{
                "type":"array",
                "items":{"type":"number","minimum":0}
            },
            "arbitraryGainsDb":{
                "type":"array",
                "items":{"type":"number"}
            }
        },
        "required":[
            "channelBitmap",
            "type",
            "band",
            "order",
            "lowPassFreqHz",
            "highPassFreqHz",
            "passBandRippleDb",
            "stopBandRippleDb",
            "chebyGamma",
            "arbitraryFrequenciesHz",
            "arbitraryGainsDb"
        ]
    })";
}

std::string_view
decimator_settings_schema_json() noexcept {
    return R"({
        "$schema":"https://json-schema.org/draft/2020-12/schema",
        "x-pamguard-authority":{
            "version":"2.02.18e",
            "commit":"dca55c81ef6f1498a8a3b926c69e7182afb915ee",
            "settingsClass":"decimator.DecimatorParams",
            "dialogClass":"decimator.DecimatorParamsDialog",
            "processClass":"decimator.DecimatorProcessW",
            "workerClass":"decimator.DecimatorWorker"
        },
        "x-pamguard-portable-deviations":[
            "rawDataSource is represented by the public rawAudio binding",
            "the obsolete clone-time channelMap zero to 0xFFFF migration is not reproduced; zero remains an explicit needs-configuration state",
            "viewer-only offline WAV storage belongs to acquisition/storage infrastructure and is not part of runtime settings",
            "outputSampleRateHz is restricted to a positive uint32 integer because AudioChunk sample-rate metadata is integral",
            "Java invalid-rate and swallowed interpolation exceptions are rejected at the settings boundary",
            "Java null arbitrary FIR arrays are normalized to portable empty arrays",
            "FilterParams plot/import/centre-frequency preferences are not runtime settings",
            "partial buffered output is not flushed on stop, matching DecimatorWorker"
        ],
        "type":"object",
        "additionalProperties":false,
        "properties":{
            "outputSampleRateHz":{
                "type":"integer",
                "minimum":1,
                "maximum":4294967295
            },
            "filter":{"$ref":"#/$defs/filterParams"},
            "interpolation":{
                "type":"integer",
                "minimum":0,
                "maximum":2
            },
            "channelBitmap":{
                "type":"integer",
                "minimum":0,
                "maximum":4294967295
            }
        },
        "required":[
            "outputSampleRateHz",
            "filter",
            "interpolation",
            "channelBitmap"
        ],
        "$defs":{
            "filterParams":{
                "type":"object",
                "additionalProperties":false,
                "properties":{
                    "type":{
                        "type":"string",
                        "enum":["none","butterworth","chebyshev","firWindow","firArbitrary","fft"]
                    },
                    "band":{
                        "type":"string",
                        "enum":["highPass","lowPass","bandPass","bandStop"]
                    },
                    "order":{
                        "type":"integer",
                        "minimum":1,
                        "maximum":32
                    },
                    "lowPassFreqHz":{"type":"number","minimum":0},
                    "highPassFreqHz":{"type":"number","minimum":0},
                    "passBandRippleDb":{"type":"number","minimum":0},
                    "stopBandRippleDb":{"type":"number","minimum":0},
                    "chebyGamma":{"type":"number","exclusiveMinimum":0},
                    "arbitraryFrequenciesHz":{
                        "type":"array",
                        "items":{"type":"number","minimum":0}
                    },
                    "arbitraryGainsDb":{
                        "type":"array",
                        "items":{"type":"number"}
                    }
                },
                "required":[
                    "type",
                    "band",
                    "order",
                    "lowPassFreqHz",
                    "highPassFreqHz",
                    "passBandRippleDb",
                    "stopBandRippleDb",
                    "chebyGamma",
                    "arbitraryFrequenciesHz",
                    "arbitraryGainsDb"
                ]
            }
        }
    })";
}

} // namespace pamguard::core
