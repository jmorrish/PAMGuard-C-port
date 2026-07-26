#include "pamguard/core/NoiseLtsaSettings.h"

#include <cmath>
#include <limits>
#include <set>
#include <utility>

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
        throw NoiseLtsaSettingsError(
            std::string(unit_name) +
            " settings are not valid JSON: " + error.what());
    }
}

void require_version(
    std::uint32_t version,
    std::string_view unit_name) {
    if (version != 1) {
        throw NoiseLtsaSettingsError(
            "Unsupported " + std::string(unit_name) +
            " settings version");
    }
}

void require_exact_fields(
    const Json& value,
    const std::set<std::string>& expected,
    std::string_view context) {
    if (!value.is_object() || value.size() != expected.size()) {
        throw NoiseLtsaSettingsError(
            std::string(context) +
            " must contain exactly the supported fields");
    }
    for (const auto& [name, _] : value.items()) {
        if (!expected.contains(name)) {
            throw NoiseLtsaSettingsError(
                std::string(context) + " contains unknown field '" +
                name + "'");
        }
    }
}

std::uint32_t channel_bitmap(
    const Json& value,
    std::string_view context) {
    if (!value.is_number_integer() &&
        !value.is_number_unsigned()) {
        throw NoiseLtsaSettingsError(
            std::string(context) +
            " must be an unsigned 32-bit channel bitmap");
    }
    std::uint64_t wide = 0;
    if (value.is_number_unsigned()) {
        wide = value.get<std::uint64_t>();
    }
    else {
        const auto signed_wide = value.get<std::int64_t>();
        if (signed_wide < 0) {
            throw NoiseLtsaSettingsError(
                std::string(context) +
                " must be an unsigned 32-bit channel bitmap");
        }
        wide = static_cast<std::uint64_t>(signed_wide);
    }
    if (wide > std::numeric_limits<std::uint32_t>::max()) {
        throw NoiseLtsaSettingsError(
            std::string(context) +
            " exceeds PAMGuard's 32-channel bitmap");
    }
    return static_cast<std::uint32_t>(wide);
}

int bounded_integer(
    const Json& value,
    int minimum,
    int maximum,
    std::string_view context) {
    if (!value.is_number_integer() &&
        !value.is_number_unsigned()) {
        throw NoiseLtsaSettingsError(
            std::string(context) + " must be an integer");
    }
    if (value.is_number_unsigned()) {
        const auto wide = value.get<std::uint64_t>();
        if (wide < static_cast<std::uint64_t>(minimum) ||
            wide > static_cast<std::uint64_t>(maximum)) {
            throw NoiseLtsaSettingsError(
                std::string(context) + " must be between " +
                std::to_string(minimum) + " and " +
                std::to_string(maximum));
        }
        return static_cast<int>(wide);
    }
    const auto wide = value.get<std::int64_t>();
    if (wide < static_cast<std::int64_t>(minimum) ||
        wide > static_cast<std::int64_t>(maximum)) {
        throw NoiseLtsaSettingsError(
            std::string(context) + " must be between " +
            std::to_string(minimum) + " and " +
            std::to_string(maximum));
    }
    return static_cast<int>(wide);
}

double finite_number(
    const Json& value,
    std::string_view context) {
    if (!value.is_number()) {
        throw NoiseLtsaSettingsError(
            std::string(context) + " must be a number");
    }
    const double result = value.get<double>();
    if (!std::isfinite(result)) {
        throw NoiseLtsaSettingsError(
            std::string(context) + " must be finite");
    }
    return result;
}

void require_boolean(
    const Json& value,
    std::string_view context) {
    if (!value.is_boolean()) {
        throw NoiseLtsaSettingsError(
            std::string(context) + " must be boolean");
    }
}

detectors::NoiseBandType band_type_from_token(
    const Json& value,
    std::string_view context,
    bool fft_noise_dialog_only) {
    if (!value.is_string()) {
        throw NoiseLtsaSettingsError(
            std::string(context) + " must be a string");
    }
    const auto token = value.get<std::string>();
    if (token == "octave") {
        return detectors::NoiseBandType::Octave;
    }
    if (token == "thirdOctave") {
        return detectors::NoiseBandType::ThirdOctave;
    }
    if (token == "decidecade") {
        return detectors::NoiseBandType::Decidecade;
    }
    if (token == "decade") {
        return detectors::NoiseBandType::Decade;
    }
    if (!fft_noise_dialog_only && token == "tenthOctave") {
        return detectors::NoiseBandType::TenthOctave;
    }
    if (!fft_noise_dialog_only && token == "twelfthOctave") {
        return detectors::NoiseBandType::TwelfthOctave;
    }
    throw NoiseLtsaSettingsError(
        std::string(context) +
        " is not a band family exposed by the Java dialog");
}

std::string band_type_token(detectors::NoiseBandType value) {
    switch (value) {
    case detectors::NoiseBandType::Octave:
        return "octave";
    case detectors::NoiseBandType::ThirdOctave:
        return "thirdOctave";
    case detectors::NoiseBandType::Decidecade:
        return "decidecade";
    case detectors::NoiseBandType::Decade:
        return "decade";
    case detectors::NoiseBandType::TenthOctave:
        return "tenthOctave";
    case detectors::NoiseBandType::TwelfthOctave:
        return "twelfthOctave";
    }
    throw NoiseLtsaSettingsError("Unknown noise band family");
}

NoiseBandFilterType filter_type_from_token(const Json& value) {
    if (!value.is_string()) {
        throw NoiseLtsaSettingsError(
            "Noise Band Monitor filterType must be a string");
    }
    const auto token = value.get<std::string>();
    if (token == "butterworth") {
        return NoiseBandFilterType::Butterworth;
    }
    if (token == "firWindow") {
        return NoiseBandFilterType::FirWindow;
    }
    throw NoiseLtsaSettingsError(
        "Noise Band Monitor filterType must be butterworth or "
        "firWindow");
}

std::string filter_type_token(NoiseBandFilterType value) {
    switch (value) {
    case NoiseBandFilterType::Butterworth:
        return "butterworth";
    case NoiseBandFilterType::FirWindow:
        return "firWindow";
    }
    throw NoiseLtsaSettingsError(
        "Unknown Noise Band Monitor filter type");
}

Json channels_from_bitmap(std::uint32_t bitmap) {
    Json channels = Json::array();
    for (std::size_t channel = 0; channel < 32; ++channel) {
        if ((bitmap & (std::uint32_t{1} << channel)) != 0) {
            channels.push_back(channel);
        }
    }
    return channels;
}

Json fft_noise_json(const FftNoiseMonitorSettings& settings) {
    Json bands = Json::array();
    for (const auto& band : settings.bands) {
        bands.push_back({
            {"name", band.name},
            {"lowFrequencyHz", band.low_frequency_hz},
            {"highFrequencyHz", band.high_frequency_hz},
            {"bandType",
             band.band_type
                 ? Json(band_type_token(*band.band_type))
                 : Json(nullptr)},
        });
    }
    return {
        {"channelBitmap", settings.channel_bitmap},
        {"measurementIntervalSeconds",
         settings.measurement_interval_seconds},
        {"nMeasures", settings.n_measures},
        {"useAll", settings.use_all},
        {"bands", std::move(bands)},
    };
}

Json noise_band_json(const NoiseBandMonitorSettings& settings) {
    return {
        {"channelBitmap", settings.channel_bitmap},
        {"bandType", band_type_token(settings.band_type)},
        {"filterType", filter_type_token(settings.filter_type)},
        {"iirOrder", settings.iir_order},
        {"firOrder", settings.fir_order},
        {"firGamma", settings.fir_gamma},
        {"outputIntervalSeconds", settings.output_interval_seconds},
        {"minimumFrequencyHz", settings.minimum_frequency_hz},
        {"maximumFrequencyHz", settings.maximum_frequency_hz},
        {"referenceFrequencyHz", settings.reference_frequency_hz},
    };
}

Json ltsa_json(const LtsaSettings& settings) {
    return {
        {"channelBitmap", settings.channel_bitmap},
        {"intervalSeconds", settings.interval_seconds},
        {"longerFactor", settings.longer_factor},
    };
}

} // namespace

FftNoiseMonitorSettings fft_noise_monitor_settings_from_json(
    std::string_view settings_json,
    std::uint32_t settings_version) {
    require_version(settings_version, "Noise Monitor");
    const auto settings =
        parse_settings(settings_json, "Noise Monitor");
    require_exact_fields(
        settings,
        {
            "channelBitmap",
            "measurementIntervalSeconds",
            "nMeasures",
            "useAll",
            "bands",
        },
        "Noise Monitor settings");

    FftNoiseMonitorSettings result;
    result.channel_bitmap = channel_bitmap(
        settings.at("channelBitmap"),
        "Noise Monitor channelBitmap");
    result.measurement_interval_seconds = bounded_integer(
        settings.at("measurementIntervalSeconds"),
        1,
        std::numeric_limits<int>::max(),
        "Noise Monitor measurementIntervalSeconds");
    result.n_measures = bounded_integer(
        settings.at("nMeasures"),
        1,
        std::numeric_limits<int>::max(),
        "Noise Monitor nMeasures");
    require_boolean(settings.at("useAll"), "Noise Monitor useAll");
    result.use_all = settings.at("useAll").get<bool>();

    const auto& bands = settings.at("bands");
    if (!bands.is_array()) {
        throw NoiseLtsaSettingsError(
            "Noise Monitor bands must be an array");
    }
    result.bands.reserve(bands.size());
    for (std::size_t index = 0; index < bands.size(); ++index) {
        const auto& band = bands.at(index);
        const auto context =
            "Noise Monitor bands[" + std::to_string(index) + "]";
        require_exact_fields(
            band,
            {
                "name",
                "lowFrequencyHz",
                "highFrequencyHz",
                "bandType",
            },
            context);
        if (!band.at("name").is_string()) {
            throw NoiseLtsaSettingsError(
                context + " name must be a string");
        }
        FftNoiseMeasurementBandSettings decoded;
        decoded.name = band.at("name").get<std::string>();
        decoded.low_frequency_hz = finite_number(
            band.at("lowFrequencyHz"),
            context + " lowFrequencyHz");
        decoded.high_frequency_hz = finite_number(
            band.at("highFrequencyHz"),
            context + " highFrequencyHz");
        if (decoded.low_frequency_hz < 0.0 ||
            !(decoded.high_frequency_hz >
              decoded.low_frequency_hz)) {
            throw NoiseLtsaSettingsError(
                context +
                " must have non-negative, strictly ordered frequencies");
        }
        if (!band.at("bandType").is_null()) {
            decoded.band_type = band_type_from_token(
                band.at("bandType"),
                context + " bandType",
                true);
        }
        result.bands.push_back(std::move(decoded));
    }
    return result;
}

NoiseBandMonitorSettings noise_band_monitor_settings_from_json(
    std::string_view settings_json,
    std::uint32_t settings_version) {
    require_version(settings_version, "Noise Band Monitor");
    const auto settings =
        parse_settings(settings_json, "Noise Band Monitor");
    require_exact_fields(
        settings,
        {
            "channelBitmap",
            "bandType",
            "filterType",
            "iirOrder",
            "firOrder",
            "firGamma",
            "outputIntervalSeconds",
            "minimumFrequencyHz",
            "maximumFrequencyHz",
            "referenceFrequencyHz",
        },
        "Noise Band Monitor settings");

    NoiseBandMonitorSettings result;
    result.channel_bitmap = channel_bitmap(
        settings.at("channelBitmap"),
        "Noise Band Monitor channelBitmap");
    result.band_type = band_type_from_token(
        settings.at("bandType"),
        "Noise Band Monitor bandType",
        false);
    result.filter_type =
        filter_type_from_token(settings.at("filterType"));
    result.iir_order = bounded_integer(
        settings.at("iirOrder"),
        2,
        20,
        "Noise Band Monitor iirOrder");
    if ((result.iir_order & 1) != 0) {
        throw NoiseLtsaSettingsError(
            "Noise Band Monitor iirOrder must be even, matching "
            "NoiseBandDialog");
    }
    result.fir_order = bounded_integer(
        settings.at("firOrder"),
        2,
        20,
        "Noise Band Monitor firOrder");
    result.fir_gamma = finite_number(
        settings.at("firGamma"),
        "Noise Band Monitor firGamma");
    if (!(result.fir_gamma > 0.0)) {
        throw NoiseLtsaSettingsError(
            "Noise Band Monitor firGamma must be positive");
    }
    result.output_interval_seconds = bounded_integer(
        settings.at("outputIntervalSeconds"),
        1,
        std::numeric_limits<int>::max(),
        "Noise Band Monitor outputIntervalSeconds");
    result.minimum_frequency_hz = finite_number(
        settings.at("minimumFrequencyHz"),
        "Noise Band Monitor minimumFrequencyHz");
    result.maximum_frequency_hz = finite_number(
        settings.at("maximumFrequencyHz"),
        "Noise Band Monitor maximumFrequencyHz");
    result.reference_frequency_hz = finite_number(
        settings.at("referenceFrequencyHz"),
        "Noise Band Monitor referenceFrequencyHz");
    if (!(result.minimum_frequency_hz > 0.0) ||
        !(result.maximum_frequency_hz >=
          result.minimum_frequency_hz) ||
        !(result.reference_frequency_hz > 0.0)) {
        throw NoiseLtsaSettingsError(
            "Noise Band Monitor frequencies must be positive and "
            "minimumFrequencyHz must not exceed maximumFrequencyHz");
    }
    return result;
}

LtsaSettings ltsa_settings_from_json(
    std::string_view settings_json,
    std::uint32_t settings_version) {
    require_version(settings_version, "LTSA");
    const auto settings = parse_settings(settings_json, "LTSA");
    require_exact_fields(
        settings,
        {
            "channelBitmap",
            "intervalSeconds",
            "longerFactor",
        },
        "LTSA settings");
    LtsaSettings result;
    result.channel_bitmap = channel_bitmap(
        settings.at("channelBitmap"),
        "LTSA channelBitmap");
    result.interval_seconds = bounded_integer(
        settings.at("intervalSeconds"),
        1,
        std::numeric_limits<int>::max(),
        "LTSA intervalSeconds");
    result.longer_factor = bounded_integer(
        settings.at("longerFactor"),
        1,
        std::numeric_limits<int>::max(),
        "LTSA longerFactor");
    return result;
}

std::string fft_noise_monitor_settings_to_json(
    const FftNoiseMonitorSettings& settings,
    std::uint32_t settings_version) {
    require_version(settings_version, "Noise Monitor");
    const auto encoded = fft_noise_json(settings).dump();
    (void) fft_noise_monitor_settings_from_json(
        encoded,
        settings_version);
    return encoded;
}

std::string noise_band_monitor_settings_to_json(
    const NoiseBandMonitorSettings& settings,
    std::uint32_t settings_version) {
    require_version(settings_version, "Noise Band Monitor");
    const auto encoded = noise_band_json(settings).dump();
    (void) noise_band_monitor_settings_from_json(
        encoded,
        settings_version);
    return encoded;
}

std::string ltsa_settings_to_json(
    const LtsaSettings& settings,
    std::uint32_t settings_version) {
    require_version(settings_version, "LTSA");
    const auto encoded = ltsa_json(settings).dump();
    (void) ltsa_settings_from_json(encoded, settings_version);
    return encoded;
}

std::string fft_noise_monitor_runtime_settings_json(
    const FftNoiseMonitorSettings& settings) {
    const auto controlled =
        fft_noise_monitor_settings_to_json(settings, 1);
    (void) controlled;
    Json bands = Json::array();
    for (const auto& band : settings.bands) {
        bands.push_back({
            {"name", band.name},
            {"lowFrequencyHz", band.low_frequency_hz},
            {"highFrequencyHz", band.high_frequency_hz},
        });
    }
    return Json{
        {"channels", channels_from_bitmap(settings.channel_bitmap)},
        {"measurementIntervalSeconds",
         settings.measurement_interval_seconds},
        {"nMeasures", settings.n_measures},
        {"useAll", settings.use_all},
        {"bands", std::move(bands)},
    }.dump();
}

std::string noise_band_monitor_runtime_settings_json(
    const NoiseBandMonitorSettings& settings) {
    const auto controlled =
        noise_band_monitor_settings_to_json(settings, 1);
    (void) controlled;
    auto runtime = noise_band_json(settings);
    return runtime.dump();
}

std::string ltsa_runtime_settings_json(
    const LtsaSettings& settings) {
    const auto controlled = ltsa_settings_to_json(settings, 1);
    (void) controlled;
    return Json{
        {"channelBitmap", settings.channel_bitmap},
        {"intervalSeconds", settings.interval_seconds},
    }.dump();
}

std::string fft_noise_monitor_default_settings_json() {
    return fft_noise_json(FftNoiseMonitorSettings{}).dump();
}

std::string noise_band_monitor_default_settings_json() {
    return noise_band_json(NoiseBandMonitorSettings{}).dump();
}

std::string ltsa_default_settings_json() {
    return ltsa_json(LtsaSettings{}).dump();
}

std::string_view
fft_noise_monitor_settings_schema_json() noexcept {
    return R"({
        "$schema":"https://json-schema.org/draft/2020-12/schema",
        "x-pamguard-authority":{
            "version":"2.02.18e",
            "commit":"dca55c81ef6f1498a8a3b926c69e7182afb915ee",
            "settingsClasses":[
                "noiseMonitor.NoiseSettings",
                "noiseMonitor.NoiseMeasurementBand",
                "noiseBandMonitor.BandType"
            ],
            "dialogClass":"noiseMonitor.NoiseDialog",
            "processClass":"noiseMonitor.NoiseProcess"
        },
        "x-pamguard-portable-deviations":[
            "dataSource is represented by the public fft binding",
            "lowestFrequency/highestFrequency are derived caches and are not persisted",
            "FFT length and hop are derived from the bound FFT DataBlock rather than duplicated in settings",
            "positive intervals/counts and non-negative strictly ordered bands are enforced at the portable boundary",
            "Java random subsampling uses an unseeded Random; the C++ scientific core intentionally uses a stable seed for reproducible runs"
        ],
        "type":"object",
        "additionalProperties":false,
        "properties":{
            "channelBitmap":{
                "type":"integer",
                "minimum":0,
                "maximum":4294967295
            },
            "measurementIntervalSeconds":{
                "type":"integer",
                "minimum":1
            },
            "nMeasures":{
                "type":"integer",
                "minimum":1
            },
            "useAll":{"type":"boolean"},
            "bands":{
                "type":"array",
                "items":{
                    "type":"object",
                    "additionalProperties":false,
                    "properties":{
                        "name":{"type":"string"},
                        "lowFrequencyHz":{
                            "type":"number",
                            "minimum":0
                        },
                        "highFrequencyHz":{
                            "type":"number",
                            "exclusiveMinimum":0
                        },
                        "bandType":{
                            "type":["string","null"],
                            "enum":[
                                null,
                                "thirdOctave",
                                "decidecade",
                                "octave",
                                "decade"
                            ]
                        }
                    },
                    "required":[
                        "name",
                        "lowFrequencyHz",
                        "highFrequencyHz",
                        "bandType"
                    ]
                }
            }
        },
        "required":[
            "channelBitmap",
            "measurementIntervalSeconds",
            "nMeasures",
            "useAll",
            "bands"
        ],
        "x-pamguardConstraints":[
            {
                "id":"noise-band-frequency-order",
                "kind":"array-object-less-than",
                "arrayPointer":"/bands",
                "leftField":"lowFrequencyHz",
                "rightField":"highFrequencyHz"
            }
        ]
    })";
}

std::string_view
noise_band_monitor_settings_schema_json() noexcept {
    return R"({
        "$schema":"https://json-schema.org/draft/2020-12/schema",
        "x-pamguard-authority":{
            "version":"2.02.18e",
            "commit":"dca55c81ef6f1498a8a3b926c69e7182afb915ee",
            "settingsClasses":[
                "noiseBandMonitor.NoiseBandSettings",
                "noiseBandMonitor.BandType",
                "Filters.FilterType"
            ],
            "dialogClass":"noiseBandMonitor.NoiseBandDialog",
            "processClass":"noiseBandMonitor.NoiseBandProcess"
        },
        "x-pamguard-excluded-display-preferences":[
            "logFreqScale",
            "showGrid",
            "showDecimators",
            "showStandard",
            "scaleToggleState"
        ],
        "x-pamguard-portable-deviations":[
            "rawDataSource is represented by the public rawAudio binding",
            "deprecated startDecimation/endDecimation/lowBandNumber/highBandNumber migration state is excluded",
            "lazy maxFrequency/minFrequency defaults are materialized as their Java getter results",
            "only Butterworth and FIR Window are included because those are the two filter choices constructed by NoiseBandDialog and NoiseBandControl",
            "positive intervals/firGamma and positive ordered frequencies are enforced at the portable boundary",
            "plot colours, layout, grid/scale toggles and ANSI response overlays are display preferences rather than portable science"
        ],
        "type":"object",
        "additionalProperties":false,
        "properties":{
            "channelBitmap":{
                "type":"integer",
                "minimum":0,
                "maximum":4294967295
            },
            "bandType":{
                "type":"string",
                "enum":[
                    "octave",
                    "thirdOctave",
                    "decidecade",
                    "decade",
                    "tenthOctave",
                    "twelfthOctave"
                ]
            },
            "filterType":{
                "type":"string",
                "enum":["butterworth","firWindow"]
            },
            "iirOrder":{
                "type":"integer",
                "minimum":2,
                "maximum":20,
                "multipleOf":2
            },
            "firOrder":{
                "type":"integer",
                "minimum":2,
                "maximum":20
            },
            "firGamma":{
                "type":"number",
                "exclusiveMinimum":0
            },
            "outputIntervalSeconds":{
                "type":"integer",
                "minimum":1
            },
            "minimumFrequencyHz":{
                "type":"number",
                "exclusiveMinimum":0
            },
            "maximumFrequencyHz":{
                "type":"number",
                "exclusiveMinimum":0
            },
            "referenceFrequencyHz":{
                "type":"number",
                "exclusiveMinimum":0
            }
        },
        "required":[
            "channelBitmap",
            "bandType",
            "filterType",
            "iirOrder",
            "firOrder",
            "firGamma",
            "outputIntervalSeconds",
            "minimumFrequencyHz",
            "maximumFrequencyHz",
            "referenceFrequencyHz"
        ],
        "x-pamguardConstraints":[
            {
                "id":"noise-band-frequency-order",
                "kind":"less-than-or-equal",
                "leftPointer":"/minimumFrequencyHz",
                "rightPointer":"/maximumFrequencyHz"
            }
        ]
    })";
}

std::string_view ltsa_settings_schema_json() noexcept {
    return R"({
        "$schema":"https://json-schema.org/draft/2020-12/schema",
        "x-pamguard-authority":{
            "version":"2.02.18e",
            "commit":"dca55c81ef6f1498a8a3b926c69e7182afb915ee",
            "settingsClass":"ltsa.LtsaParameters",
            "dialogClass":"ltsa.LtsaDialog",
            "processClass":"ltsa.LtsaProcess"
        },
        "x-pamguard-portable-deviations":[
            "dataSource is represented by the public fft binding",
            "PamModel registers a RawDataUnit dependency but LtsaDialog and LtsaProcess actually select FFTDataUnit/FFTDataBlock; the public role follows the executable process",
            "longerFactor is retained for settings round-trip but the pinned Java longer-average process/output is commented out and it has no runtime effect",
            "legacy Java binary-file writing is outside portable scientific settings"
        ],
        "type":"object",
        "additionalProperties":false,
        "properties":{
            "channelBitmap":{
                "type":"integer",
                "minimum":0,
                "maximum":4294967295
            },
            "intervalSeconds":{
                "type":"integer",
                "minimum":1
            },
            "longerFactor":{
                "type":"integer",
                "minimum":1,
                "x-pamguard-dormant":true
            }
        },
        "required":[
            "channelBitmap",
            "intervalSeconds",
            "longerFactor"
        ]
    })";
}

} // namespace pamguard::core
