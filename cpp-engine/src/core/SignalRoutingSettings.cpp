#include "pamguard/core/SignalRoutingSettings.h"

#include <cmath>
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
        throw SignalRoutingSettingsError(
            std::string(unit_name) +
            " settings are not valid JSON: " + error.what());
    }
}

void require_exact_fields(
    const Json& value,
    const std::set<std::string>& expected,
    std::string_view context) {
    if (!value.is_object() || value.size() != expected.size()) {
        throw SignalRoutingSettingsError(
            std::string(context) +
            " must contain exactly the supported fields");
    }
    for (const auto& [name, _] : value.items()) {
        if (!expected.contains(name)) {
            throw SignalRoutingSettingsError(
                std::string(context) + " contains unknown field '" +
                name + "'");
        }
    }
}

double finite_number(
    const Json& value,
    std::string_view context) {
    if (!value.is_number()) {
        throw SignalRoutingSettingsError(
            std::string(context) + " must be a number");
    }
    const double result = value.get<double>();
    if (!std::isfinite(result)) {
        throw SignalRoutingSettingsError(
            std::string(context) + " must be finite");
    }
    return result;
}

PatchGainMatrix read_gain_matrix(
    const Json& matrix,
    std::string_view context) {
    if (!matrix.is_array() ||
        matrix.size() != kPamguardSignalChannelLimit) {
        throw SignalRoutingSettingsError(
            std::string(context) + " must have exactly 32 input rows");
    }
    PatchGainMatrix result{};
    for (std::size_t input_channel = 0;
         input_channel < kPamguardSignalChannelLimit;
         ++input_channel) {
        const auto& row = matrix.at(input_channel);
        if (!row.is_array() ||
            row.size() != kPamguardSignalChannelLimit) {
            throw SignalRoutingSettingsError(
                std::string(context) +
                " rows must each have exactly 32 outputs");
        }
        for (std::size_t output_channel = 0;
             output_channel < kPamguardSignalChannelLimit;
             ++output_channel) {
            result[input_channel][output_channel] = finite_number(
                row.at(output_channel),
                std::string(context) + "[" +
                    std::to_string(input_channel) + "][" +
                    std::to_string(output_channel) + "]");
        }
    }
    return result;
}

} // namespace

double SignalAmplifierSettings::signed_linear_gain(
    std::size_t channel) const {
    if (channel >= channel_settings.size()) {
        throw SignalRoutingSettingsError(
            "Signal Amplifier channel index is outside PAMGuard's "
            "32-channel limit");
    }
    const auto& setting = channel_settings[channel];
    const double magnitude =
        std::pow(10.0, setting.gain_db / 20.0);
    if (!std::isfinite(magnitude) || magnitude == 0.0) {
        throw SignalRoutingSettingsError(
            "Signal Amplifier gainDb cannot be represented as a "
            "finite, non-zero linear gain");
    }
    return setting.invert ? -magnitude : magnitude;
}

std::vector<double>
SignalAmplifierSettings::signed_linear_gains() const {
    std::vector<double> result;
    result.reserve(channel_settings.size());
    for (std::size_t channel = 0;
         channel < channel_settings.size();
         ++channel) {
        result.push_back(signed_linear_gain(channel));
    }
    return result;
}

double PatchPanelSettings::coefficient(
    std::size_t input_channel,
    std::size_t output_channel) const {
    if (input_channel >= kPamguardSignalChannelLimit ||
        output_channel >= kPamguardSignalChannelLimit) {
        throw SignalRoutingSettingsError(
            "Patch Panel channel index is outside PAMGuard's "
            "32-channel limit");
    }
    if (advanced_gain_matrix) {
        return (*advanced_gain_matrix)[input_channel][output_channel];
    }
    return routing_matrix[input_channel][output_channel] ? 1.0 : 0.0;
}

std::vector<std::vector<double>>
PatchPanelSettings::coefficient_matrix() const {
    std::vector<std::vector<double>> result(
        kPamguardSignalChannelLimit,
        std::vector<double>(kPamguardSignalChannelLimit));
    for (std::size_t input_channel = 0;
         input_channel < kPamguardSignalChannelLimit;
         ++input_channel) {
        for (std::size_t output_channel = 0;
             output_channel < kPamguardSignalChannelLimit;
             ++output_channel) {
            result[input_channel][output_channel] =
                coefficient(input_channel, output_channel);
        }
    }
    return result;
}

SignalAmplifierSettings signal_amplifier_settings_from_json(
    std::string_view settings_json,
    std::uint32_t settings_version) {
    if (settings_version != 1) {
        throw SignalRoutingSettingsError(
            "Unsupported Signal Amplifier settings version");
    }
    const auto settings =
        parse_settings(settings_json, "Signal Amplifier");
    require_exact_fields(
        settings,
        {"channelSettings"},
        "Signal Amplifier settings");
    const auto& channels = settings.at("channelSettings");
    if (!channels.is_array() ||
        channels.size() != kPamguardSignalChannelLimit) {
        throw SignalRoutingSettingsError(
            "Signal Amplifier channelSettings must contain exactly "
            "32 absolute-channel rows");
    }

    SignalAmplifierSettings result;
    for (std::size_t channel = 0;
         channel < kPamguardSignalChannelLimit;
         ++channel) {
        const auto& row = channels.at(channel);
        require_exact_fields(
            row,
            {"gainDb", "invert"},
            "Signal Amplifier channelSettings[" +
                std::to_string(channel) + "]");
        if (!row.at("invert").is_boolean()) {
            throw SignalRoutingSettingsError(
                "Signal Amplifier invert must be boolean");
        }
        result.channel_settings[channel] = {
            finite_number(
                row.at("gainDb"),
                "Signal Amplifier gainDb"),
            row.at("invert").get<bool>(),
        };
        // Validate the dB-to-linear projection at the settings boundary.
        (void) result.signed_linear_gain(channel);
    }
    return result;
}

PatchPanelSettings patch_panel_settings_from_json(
    std::string_view settings_json,
    std::uint32_t settings_version) {
    if (settings_version != 1) {
        throw SignalRoutingSettingsError(
            "Unsupported Patch Panel settings version");
    }
    const auto settings =
        parse_settings(settings_json, "Patch Panel");
    require_exact_fields(
        settings,
        {"routingMatrix", "advancedGainMatrix"},
        "Patch Panel settings");
    const auto& routing = settings.at("routingMatrix");
    if (!routing.is_array() ||
        routing.size() != kPamguardSignalChannelLimit) {
        throw SignalRoutingSettingsError(
            "Patch Panel routingMatrix must have exactly 32 input rows");
    }

    PatchPanelSettings result;
    for (std::size_t input_channel = 0;
         input_channel < kPamguardSignalChannelLimit;
         ++input_channel) {
        const auto& row = routing.at(input_channel);
        if (!row.is_array() ||
            row.size() != kPamguardSignalChannelLimit) {
            throw SignalRoutingSettingsError(
                "Patch Panel routingMatrix rows must each have exactly "
                "32 outputs");
        }
        for (std::size_t output_channel = 0;
             output_channel < kPamguardSignalChannelLimit;
             ++output_channel) {
            if (!row.at(output_channel).is_boolean()) {
                throw SignalRoutingSettingsError(
                    "Patch Panel routingMatrix entries must be boolean");
            }
            result.routing_matrix[input_channel][output_channel] =
                row.at(output_channel).get<bool>();
        }
    }

    const auto& advanced = settings.at("advancedGainMatrix");
    if (!advanced.is_null()) {
        result.advanced_gain_matrix =
            read_gain_matrix(advanced, "Patch Panel advancedGainMatrix");
    }
    return result;
}

std::string signal_amplifier_default_settings_json() {
    Json channels = Json::array();
    for (std::size_t channel = 0;
         channel < kPamguardSignalChannelLimit;
         ++channel) {
        channels.push_back({
            {"gainDb", 0.0},
            {"invert", false},
        });
    }
    return Json{{"channelSettings", std::move(channels)}}.dump();
}

std::string patch_panel_default_settings_json() {
    Json routing = Json::array();
    for (std::size_t input_channel = 0;
         input_channel < kPamguardSignalChannelLimit;
         ++input_channel) {
        Json row = Json::array();
        for (std::size_t output_channel = 0;
             output_channel < kPamguardSignalChannelLimit;
             ++output_channel) {
            row.push_back(input_channel == output_channel);
        }
        routing.push_back(std::move(row));
    }
    return Json{
        {"routingMatrix", std::move(routing)},
        {"advancedGainMatrix", nullptr},
    }.dump();
}

std::string_view
signal_amplifier_settings_schema_json() noexcept {
    return R"({
        "$schema":"https://json-schema.org/draft/2020-12/schema",
        "x-pamguard-authority":{
            "version":"2.02.18e",
            "commit":"dca55c81ef6f1498a8a3b926c69e7182afb915ee",
            "settingsClass":"amplifier.AmpParameters",
            "dialogClass":"amplifier.AmpDialog",
            "processClass":"amplifier.AmpProcess"
        },
        "x-pamguard-portable-deviations":[
            "rawDataSource is represented by the public rawAudio binding",
            "gainDb must be finite and project to a finite non-zero linear gain",
            "Raw AudioChunk currently has no Java measuredAmplitude field"
        ],
        "type":"object",
        "additionalProperties":false,
        "properties":{
            "channelSettings":{
                "type":"array",
                "minItems":32,
                "maxItems":32,
                "items":{
                    "type":"object",
                    "additionalProperties":false,
                    "properties":{
                        "gainDb":{"type":"number"},
                        "invert":{"type":"boolean"}
                    },
                    "required":["gainDb","invert"]
                }
            }
        },
        "required":["channelSettings"]
    })";
}

std::string_view patch_panel_settings_schema_json() noexcept {
    return R"({
        "$schema":"https://json-schema.org/draft/2020-12/schema",
        "x-pamguard-authority":{
            "version":"2.02.18e",
            "commit":"dca55c81ef6f1498a8a3b926c69e7182afb915ee",
            "settingsClass":"patchPanel.PatchPanelParameters",
            "dialogClass":"patchPanel.PatchPanelDialog",
            "processClass":"patchPanel.PatchPanelProcess"
        },
        "x-pamguard-portable-deviations":[
            "dataSource is represented by the public rawAudio binding",
            "the Swing-only immediate apply preference is not persisted",
            "advancedGainMatrix is a nullable C++ extension and is disabled by default",
            "output channels are recomputed on every settings change instead of retaining Java configureSummary stale bits",
            "Advanced negative coefficients use non-zero route topology while Java configureSummary only marks positive coefficients",
            "the current data model carries first-route calibration but not Java ChannelListManager hydrophone mappings"
        ],
        "type":"object",
        "additionalProperties":false,
        "properties":{
            "routingMatrix":{
                "type":"array",
                "minItems":32,
                "maxItems":32,
                "items":{
                    "type":"array",
                    "minItems":32,
                    "maxItems":32,
                    "items":{"type":"boolean"}
                }
            },
            "advancedGainMatrix":{
                "type":["array","null"],
                "x-pamguard-advanced":true,
                "description":"When non-null, this Advanced C++ matrix replaces the unit coefficients implied by routingMatrix.",
                "minItems":32,
                "maxItems":32,
                "items":{
                    "type":"array",
                    "minItems":32,
                    "maxItems":32,
                    "items":{"type":"number"}
                }
            }
        },
        "required":["routingMatrix","advancedGainMatrix"]
    })";
}

} // namespace pamguard::core
