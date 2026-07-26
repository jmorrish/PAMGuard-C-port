#include "pamguard/core/SoundRecorderSettings.h"

#include <cmath>
#include <limits>
#include <set>
#include <utility>

#include <json.hpp>

namespace pamguard::core {

namespace {

using Json = nlohmann::json;

constexpr std::size_t kMaximumTriggerPolicies = 1024;
constexpr std::size_t kMaximumNameBytes = 256;
constexpr std::uint64_t kJavaLongMaximum =
    static_cast<std::uint64_t>(
        std::numeric_limits<std::int64_t>::max());

void require_version(std::uint32_t settings_version) {
    if (settings_version != 1) {
        throw SoundRecorderSettingsError(
            "Unsupported Sound Recorder settings version");
    }
}

Json parse_json(std::string_view value) {
    try {
        return Json::parse(value);
    }
    catch (const std::exception& error) {
        throw SoundRecorderSettingsError(
            std::string(
                "Sound Recorder settings are not valid JSON: ") +
            error.what());
    }
}

void require_exact_fields(
    const Json& value,
    const std::set<std::string>& expected,
    std::string_view context) {
    if (!value.is_object() ||
        value.size() != expected.size()) {
        throw SoundRecorderSettingsError(
            std::string(context) +
            " must contain exactly the supported fields");
    }
    for (const auto& [name, _] : value.items()) {
        if (!expected.contains(name)) {
            throw SoundRecorderSettingsError(
                std::string(context) +
                " contains unknown field '" + name + "'");
        }
    }
}

bool boolean_value(
    const Json& value,
    std::string_view context) {
    if (!value.is_boolean()) {
        throw SoundRecorderSettingsError(
            std::string(context) + " must be a boolean");
    }
    return value.get<bool>();
}

int bounded_int(
    const Json& value,
    int minimum,
    int maximum,
    std::string_view context) {
    if (!value.is_number_integer()) {
        throw SoundRecorderSettingsError(
            std::string(context) + " must be an integer");
    }
    try {
        const auto parsed = value.get<std::int64_t>();
        if (parsed < minimum || parsed > maximum) {
            throw SoundRecorderSettingsError(
                std::string(context) +
                " is outside the supported Java integer range");
        }
        return static_cast<int>(parsed);
    }
    catch (const SoundRecorderSettingsError&) {
        throw;
    }
    catch (const std::exception&) {
        throw SoundRecorderSettingsError(
            std::string(context) +
            " is outside the supported Java integer range");
    }
}

std::uint64_t bounded_unsigned(
    const Json& value,
    std::uint64_t minimum,
    std::uint64_t maximum,
    std::string_view context) {
    if (!value.is_number_integer() &&
        !value.is_number_unsigned()) {
        throw SoundRecorderSettingsError(
            std::string(context) + " must be an integer");
    }
    try {
        if (value.is_number_integer()) {
            const auto signed_value =
                value.get<std::int64_t>();
            if (signed_value < 0) {
                throw SoundRecorderSettingsError(
                    std::string(context) +
                    " must be non-negative");
            }
        }
        const auto parsed = value.get<std::uint64_t>();
        if (parsed < minimum || parsed > maximum) {
            throw SoundRecorderSettingsError(
                std::string(context) +
                " is outside the supported range");
        }
        return parsed;
    }
    catch (const SoundRecorderSettingsError&) {
        throw;
    }
    catch (const std::exception&) {
        throw SoundRecorderSettingsError(
            std::string(context) +
            " is outside the supported range");
    }
}

std::int64_t non_negative_java_long(
    const Json& value,
    std::string_view context) {
    const auto parsed = bounded_unsigned(
        value,
        0,
        kJavaLongMaximum,
        context);
    return static_cast<std::int64_t>(parsed);
}

double non_negative_finite(
    const Json& value,
    std::string_view context) {
    if (!value.is_number()) {
        throw SoundRecorderSettingsError(
            std::string(context) + " must be a number");
    }
    const auto parsed = value.get<double>();
    if (!std::isfinite(parsed) || parsed < 0.0) {
        throw SoundRecorderSettingsError(
            std::string(context) +
            " must be finite and non-negative");
    }
    return parsed;
}

std::string bounded_string(
    const Json& value,
    bool allow_empty,
    std::string_view context) {
    if (!value.is_string()) {
        throw SoundRecorderSettingsError(
            std::string(context) + " must be a string");
    }
    auto parsed = value.get<std::string>();
    if ((!allow_empty && parsed.empty()) ||
        parsed.size() > kMaximumNameBytes) {
        throw SoundRecorderSettingsError(
            std::string(context) +
            (allow_empty
                 ? " must contain at most 256 bytes"
                 : " must contain 1 to 256 bytes"));
    }
    return parsed;
}

SoundRecorderOperationMode operation_mode(
    const Json& value) {
    if (!value.is_string()) {
        throw SoundRecorderSettingsError(
            "Sound Recorder operationMode must be a string");
    }
    const auto name = value.get<std::string>();
    if (name == "idle") {
        return SoundRecorderOperationMode::Idle;
    }
    if (name == "continuous") {
        return SoundRecorderOperationMode::Continuous;
    }
    if (name == "cycle") {
        return SoundRecorderOperationMode::Cycle;
    }
    if (name == "restore-last") {
        return SoundRecorderOperationMode::RestoreLast;
    }
    throw SoundRecorderSettingsError(
        "Sound Recorder operationMode must be idle, continuous, "
        "cycle, or restore-last");
}

std::string_view operation_mode_name(
    SoundRecorderOperationMode value) {
    switch (value) {
    case SoundRecorderOperationMode::Idle:
        return "idle";
    case SoundRecorderOperationMode::Continuous:
        return "continuous";
    case SoundRecorderOperationMode::Cycle:
        return "cycle";
    case SoundRecorderOperationMode::RestoreLast:
        return "restore-last";
    }
    throw SoundRecorderSettingsError(
        "Sound Recorder operation mode cannot be serialized");
}

bool supported_bit_depth(int value) noexcept {
    return value == 8 || value == 16 ||
           value == 24 || value == 32;
}

void validate_trigger_policy(
    const SoundRecorderTriggerPolicySettings& policy,
    std::size_t index) {
    const auto context =
        "Sound Recorder triggerPolicies[" +
        std::to_string(index) + "]";
    if (policy.trigger_name.empty() ||
        policy.trigger_name.size() >
            kMaximumNameBytes) {
        throw SoundRecorderSettingsError(
            context +
            ".triggerName must contain 1 to 256 bytes");
    }
    if (!std::isfinite(
            policy.seconds_before_trigger) ||
        policy.seconds_before_trigger < 0.0) {
        throw SoundRecorderSettingsError(
            context +
            ".secondsBeforeTrigger must be finite and non-negative");
    }
    if (!std::isfinite(
            policy.seconds_after_trigger) ||
        policy.seconds_after_trigger < 0.0) {
        throw SoundRecorderSettingsError(
            context +
            ".secondsAfterTrigger must be finite and non-negative");
    }
    if (policy.min_detection_count < 1 ||
        policy.count_seconds < 0 ||
        policy.min_gap_between_triggers_seconds < 0 ||
        policy.max_total_trigger_length_seconds < 0 ||
        policy.day_budget_megabytes < 0 ||
        policy.last_trigger_start_unix_ms < 0 ||
        policy.last_trigger_end_unix_ms < 0 ||
        policy.used_day_budget_bytes >
            kJavaLongMaximum) {
        throw SoundRecorderSettingsError(
            context +
            " contains values outside the portable Java range");
    }
}

void validate_settings(
    const SoundRecorderSettings& settings) {
    (void) operation_mode_name(
        settings.operation_mode);
    if (settings.channel_bitmap == 0) {
        throw SoundRecorderSettingsError(
            "Sound Recorder channelBitmap must select at least one channel");
    }
    if (!supported_bit_depth(settings.bit_depth)) {
        throw SoundRecorderSettingsError(
            "Sound Recorder bitDepth must be 8, 16, 24, or 32");
    }
    if (settings.buffer_length_seconds < 0) {
        throw SoundRecorderSettingsError(
            "Sound Recorder bufferLengthSeconds must be non-negative");
    }
    if (settings.file_initials.size() >
        kMaximumNameBytes) {
        throw SoundRecorderSettingsError(
            "Sound Recorder fileInitials must contain at most 256 bytes");
    }
    if (settings.file_type.empty() ||
        settings.file_type.size() >
            kMaximumNameBytes) {
        throw SoundRecorderSettingsError(
            "Sound Recorder fileType must contain 1 to 256 bytes");
    }
    if (settings.auto_interval_seconds <= 0 ||
        settings.auto_duration_seconds <= 0) {
        throw SoundRecorderSettingsError(
            "Sound Recorder cycle interval and duration must be positive");
    }
    if (settings.max_length_seconds <= 0 ||
        settings.max_length_megabytes == 0 ||
        settings.max_length_megabytes >
            kJavaLongMaximum) {
        throw SoundRecorderSettingsError(
            "Sound Recorder file limits must be positive Java values");
    }
    if (settings.trigger_policies.size() >
        kMaximumTriggerPolicies) {
        throw SoundRecorderSettingsError(
            "Sound Recorder supports at most 1024 trigger policies");
    }
    std::set<std::string> trigger_names;
    for (std::size_t index = 0;
         index < settings.trigger_policies.size();
         ++index) {
        const auto& policy =
            settings.trigger_policies[index];
        validate_trigger_policy(policy, index);
        if (!trigger_names.emplace(
                policy.trigger_name).second) {
            throw SoundRecorderSettingsError(
                "Sound Recorder trigger policy names must be unique");
        }
    }
}

SoundRecorderTriggerPolicySettings trigger_policy_from_value(
    const Json& value,
    std::size_t index) {
    const auto context =
        "Sound Recorder triggerPolicies[" +
        std::to_string(index) + "]";
    require_exact_fields(
        value,
        {
            "triggerName",
            "enabled",
            "secondsBeforeTrigger",
            "secondsAfterTrigger",
            "minDetectionCount",
            "countSeconds",
            "minGapBetweenTriggersSeconds",
            "maxTotalTriggerLengthSeconds",
            "dayBudgetMegaBytes",
            "lastTriggerStartUnixMs",
            "lastTriggerEndUnixMs",
            "usedDayBudgetBytes",
        },
        context);

    SoundRecorderTriggerPolicySettings result;
    result.trigger_name = bounded_string(
        value.at("triggerName"),
        false,
        context + ".triggerName");
    result.enabled = boolean_value(
        value.at("enabled"),
        context + ".enabled");
    result.seconds_before_trigger =
        non_negative_finite(
            value.at("secondsBeforeTrigger"),
            context + ".secondsBeforeTrigger");
    result.seconds_after_trigger =
        non_negative_finite(
            value.at("secondsAfterTrigger"),
            context + ".secondsAfterTrigger");
    result.min_detection_count = bounded_int(
        value.at("minDetectionCount"),
        1,
        std::numeric_limits<int>::max(),
        context + ".minDetectionCount");
    result.count_seconds = bounded_int(
        value.at("countSeconds"),
        0,
        std::numeric_limits<int>::max(),
        context + ".countSeconds");
    result.min_gap_between_triggers_seconds =
        bounded_int(
            value.at(
                "minGapBetweenTriggersSeconds"),
            0,
            std::numeric_limits<int>::max(),
            context +
                ".minGapBetweenTriggersSeconds");
    result.max_total_trigger_length_seconds =
        bounded_int(
            value.at(
                "maxTotalTriggerLengthSeconds"),
            0,
            std::numeric_limits<int>::max(),
            context +
                ".maxTotalTriggerLengthSeconds");
    result.day_budget_megabytes = bounded_int(
        value.at("dayBudgetMegaBytes"),
        0,
        std::numeric_limits<int>::max(),
        context + ".dayBudgetMegaBytes");
    result.last_trigger_start_unix_ms =
        non_negative_java_long(
            value.at("lastTriggerStartUnixMs"),
            context + ".lastTriggerStartUnixMs");
    result.last_trigger_end_unix_ms =
        non_negative_java_long(
            value.at("lastTriggerEndUnixMs"),
            context + ".lastTriggerEndUnixMs");
    result.used_day_budget_bytes =
        bounded_unsigned(
            value.at("usedDayBudgetBytes"),
            0,
            kJavaLongMaximum,
            context + ".usedDayBudgetBytes");
    validate_trigger_policy(result, index);
    return result;
}

Json trigger_policy_to_value(
    const SoundRecorderTriggerPolicySettings& policy,
    std::size_t index) {
    validate_trigger_policy(policy, index);
    return {
        {"triggerName", policy.trigger_name},
        {"enabled", policy.enabled},
        {
            "secondsBeforeTrigger",
            policy.seconds_before_trigger,
        },
        {
            "secondsAfterTrigger",
            policy.seconds_after_trigger,
        },
        {
            "minDetectionCount",
            policy.min_detection_count,
        },
        {"countSeconds", policy.count_seconds},
        {
            "minGapBetweenTriggersSeconds",
            policy.min_gap_between_triggers_seconds,
        },
        {
            "maxTotalTriggerLengthSeconds",
            policy.max_total_trigger_length_seconds,
        },
        {
            "dayBudgetMegaBytes",
            policy.day_budget_megabytes,
        },
        {
            "lastTriggerStartUnixMs",
            policy.last_trigger_start_unix_ms,
        },
        {
            "lastTriggerEndUnixMs",
            policy.last_trigger_end_unix_ms,
        },
        {
            "usedDayBudgetBytes",
            policy.used_day_budget_bytes,
        },
    };
}

SoundRecorderSettings settings_from_value(
    const Json& value) {
    require_exact_fields(
        value,
        {
            "operationMode",
            "channelBitmap",
            "bitDepth",
            "enableBuffer",
            "bufferLengthSeconds",
            "fileInitials",
            "fileType",
            "autoIntervalSeconds",
            "autoDurationSeconds",
            "limitLengthSeconds",
            "maxLengthSeconds",
            "roundFileStarts",
            "limitLengthMegaBytes",
            "maxLengthMegaBytes",
            "datedSubFolders",
            "triggerPolicies",
        },
        "Sound Recorder settings");

    SoundRecorderSettings result;
    result.operation_mode =
        operation_mode(value.at("operationMode"));
    result.channel_bitmap =
        static_cast<std::uint32_t>(
            bounded_unsigned(
                value.at("channelBitmap"),
                1,
                std::numeric_limits<std::uint32_t>::max(),
                "Sound Recorder channelBitmap"));
    result.bit_depth = bounded_int(
        value.at("bitDepth"),
        8,
        32,
        "Sound Recorder bitDepth");
    if (!supported_bit_depth(result.bit_depth)) {
        throw SoundRecorderSettingsError(
            "Sound Recorder bitDepth must be 8, 16, 24, or 32");
    }
    result.enable_buffer = boolean_value(
        value.at("enableBuffer"),
        "Sound Recorder enableBuffer");
    result.buffer_length_seconds = bounded_int(
        value.at("bufferLengthSeconds"),
        0,
        std::numeric_limits<int>::max(),
        "Sound Recorder bufferLengthSeconds");
    result.file_initials = bounded_string(
        value.at("fileInitials"),
        true,
        "Sound Recorder fileInitials");
    result.file_type = bounded_string(
        value.at("fileType"),
        false,
        "Sound Recorder fileType");
    result.auto_interval_seconds = bounded_int(
        value.at("autoIntervalSeconds"),
        1,
        std::numeric_limits<int>::max(),
        "Sound Recorder autoIntervalSeconds");
    result.auto_duration_seconds = bounded_int(
        value.at("autoDurationSeconds"),
        1,
        std::numeric_limits<int>::max(),
        "Sound Recorder autoDurationSeconds");
    result.limit_length_seconds = boolean_value(
        value.at("limitLengthSeconds"),
        "Sound Recorder limitLengthSeconds");
    result.max_length_seconds = bounded_int(
        value.at("maxLengthSeconds"),
        1,
        std::numeric_limits<int>::max(),
        "Sound Recorder maxLengthSeconds");
    result.round_file_starts = boolean_value(
        value.at("roundFileStarts"),
        "Sound Recorder roundFileStarts");
    result.limit_length_megabytes = boolean_value(
        value.at("limitLengthMegaBytes"),
        "Sound Recorder limitLengthMegaBytes");
    result.max_length_megabytes = bounded_unsigned(
        value.at("maxLengthMegaBytes"),
        1,
        kJavaLongMaximum,
        "Sound Recorder maxLengthMegaBytes");
    result.dated_subfolders = boolean_value(
        value.at("datedSubFolders"),
        "Sound Recorder datedSubFolders");

    const auto& policies = value.at("triggerPolicies");
    if (!policies.is_array() ||
        policies.size() > kMaximumTriggerPolicies) {
        throw SoundRecorderSettingsError(
            "Sound Recorder triggerPolicies must be an array "
            "with at most 1024 entries");
    }
    result.trigger_policies.reserve(policies.size());
    for (std::size_t index = 0;
         index < policies.size();
         ++index) {
        result.trigger_policies.push_back(
            trigger_policy_from_value(
                policies.at(index),
                index));
    }
    validate_settings(result);
    return result;
}

Json settings_to_value(
    const SoundRecorderSettings& settings) {
    validate_settings(settings);
    Json policies = Json::array();
    for (std::size_t index = 0;
         index < settings.trigger_policies.size();
         ++index) {
        policies.push_back(
            trigger_policy_to_value(
                settings.trigger_policies[index],
                index));
    }
    return {
        {
            "operationMode",
            operation_mode_name(
                settings.operation_mode),
        },
        {"channelBitmap", settings.channel_bitmap},
        {"bitDepth", settings.bit_depth},
        {"enableBuffer", settings.enable_buffer},
        {
            "bufferLengthSeconds",
            settings.buffer_length_seconds,
        },
        {"fileInitials", settings.file_initials},
        {"fileType", settings.file_type},
        {
            "autoIntervalSeconds",
            settings.auto_interval_seconds,
        },
        {
            "autoDurationSeconds",
            settings.auto_duration_seconds,
        },
        {
            "limitLengthSeconds",
            settings.limit_length_seconds,
        },
        {
            "maxLengthSeconds",
            settings.max_length_seconds,
        },
        {
            "roundFileStarts",
            settings.round_file_starts,
        },
        {
            "limitLengthMegaBytes",
            settings.limit_length_megabytes,
        },
        {
            "maxLengthMegaBytes",
            settings.max_length_megabytes,
        },
        {
            "datedSubFolders",
            settings.dated_subfolders,
        },
        {"triggerPolicies", std::move(policies)},
    };
}

} // namespace

SoundRecorderSettings sound_recorder_settings_from_json(
    std::string_view settings_json,
    std::uint32_t settings_version) {
    require_version(settings_version);
    return settings_from_value(
        parse_json(settings_json));
}

std::string sound_recorder_settings_to_json(
    const SoundRecorderSettings& settings,
    std::uint32_t settings_version) {
    require_version(settings_version);
    return settings_to_value(settings).dump();
}

std::string sound_recorder_default_settings_json() {
    return sound_recorder_settings_to_json(
        SoundRecorderSettings{});
}

std::string_view
sound_recorder_settings_schema_json() noexcept {
    return R"json({
        "$schema":"https://json-schema.org/draft/2020-12/schema",
        "type":"object",
        "additionalProperties":false,
        "properties":{
            "operationMode":{
                "type":"string",
                "enum":[
                    "idle",
                    "continuous",
                    "cycle",
                    "restore-last"
                ]
            },
            "channelBitmap":{
                "type":"integer",
                "minimum":1,
                "maximum":4294967295
            },
            "bitDepth":{
                "type":"integer",
                "enum":[8,16,24,32]
            },
            "enableBuffer":{"type":"boolean"},
            "bufferLengthSeconds":{
                "type":"integer",
                "minimum":0,
                "maximum":2147483647
            },
            "fileInitials":{
                "type":"string",
                "maxLength":256
            },
            "fileType":{
                "type":"string",
                "minLength":1,
                "maxLength":256
            },
            "autoIntervalSeconds":{
                "type":"integer",
                "minimum":1,
                "maximum":2147483647
            },
            "autoDurationSeconds":{
                "type":"integer",
                "minimum":1,
                "maximum":2147483647
            },
            "limitLengthSeconds":{"type":"boolean"},
            "maxLengthSeconds":{
                "type":"integer",
                "minimum":1,
                "maximum":2147483647
            },
            "roundFileStarts":{"type":"boolean"},
            "limitLengthMegaBytes":{"type":"boolean"},
            "maxLengthMegaBytes":{
                "type":"integer",
                "minimum":1,
                "maximum":9223372036854775807
            },
            "datedSubFolders":{"type":"boolean"},
            "triggerPolicies":{
                "type":"array",
                "maxItems":1024,
                "items":{
                    "type":"object",
                    "additionalProperties":false,
                    "properties":{
                        "triggerName":{
                            "type":"string",
                            "minLength":1,
                            "maxLength":256
                        },
                        "enabled":{"type":"boolean"},
                        "secondsBeforeTrigger":{
                            "type":"number",
                            "minimum":0
                        },
                        "secondsAfterTrigger":{
                            "type":"number",
                            "minimum":0
                        },
                        "minDetectionCount":{
                            "type":"integer",
                            "minimum":1,
                            "maximum":2147483647
                        },
                        "countSeconds":{
                            "type":"integer",
                            "minimum":0,
                            "maximum":2147483647
                        },
                        "minGapBetweenTriggersSeconds":{
                            "type":"integer",
                            "minimum":0,
                            "maximum":2147483647
                        },
                        "maxTotalTriggerLengthSeconds":{
                            "type":"integer",
                            "minimum":0,
                            "maximum":2147483647
                        },
                        "dayBudgetMegaBytes":{
                            "type":"integer",
                            "minimum":0,
                            "maximum":2147483647
                        },
                        "lastTriggerStartUnixMs":{
                            "type":"integer",
                            "minimum":0,
                            "maximum":9223372036854775807
                        },
                        "lastTriggerEndUnixMs":{
                            "type":"integer",
                            "minimum":0,
                            "maximum":9223372036854775807
                        },
                        "usedDayBudgetBytes":{
                            "type":"integer",
                            "minimum":0,
                            "maximum":9223372036854775807
                        }
                    },
                    "required":[
                        "triggerName",
                        "enabled",
                        "secondsBeforeTrigger",
                        "secondsAfterTrigger",
                        "minDetectionCount",
                        "countSeconds",
                        "minGapBetweenTriggersSeconds",
                        "maxTotalTriggerLengthSeconds",
                        "dayBudgetMegaBytes",
                        "lastTriggerStartUnixMs",
                        "lastTriggerEndUnixMs",
                        "usedDayBudgetBytes"
                    ]
                }
            }
        },
        "required":[
            "operationMode",
            "channelBitmap",
            "bitDepth",
            "enableBuffer",
            "bufferLengthSeconds",
            "fileInitials",
            "fileType",
            "autoIntervalSeconds",
            "autoDurationSeconds",
            "limitLengthSeconds",
            "maxLengthSeconds",
            "roundFileStarts",
            "limitLengthMegaBytes",
            "maxLengthMegaBytes",
            "datedSubFolders",
            "triggerPolicies"
        ]
    })json";
}

} // namespace pamguard::core
