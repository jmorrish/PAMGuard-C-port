#include "pamguard/core/ClipGeneratorSettings.h"

#include <cmath>
#include <limits>
#include <set>
#include <utility>

#include <json.hpp>

namespace pamguard::core {

namespace {

using Json = nlohmann::json;

constexpr std::size_t kMaximumTriggerPolicies = 1024;
constexpr std::size_t kMaximumClipPrefixBytes = 256;
constexpr std::size_t kMaximumUnitIdBytes = 128;
constexpr std::size_t kMaximumOutputRoleBytes = 64;

void require_version(std::uint32_t settings_version) {
    if (settings_version != 1) {
        throw ClipGeneratorSettingsError(
            "Unsupported Clip Generator settings version");
    }
}

Json parse_json(std::string_view value) {
    try {
        return Json::parse(value);
    }
    catch (const std::exception& error) {
        throw ClipGeneratorSettingsError(
            std::string(
                "Clip Generator settings are not valid JSON: ") +
            error.what());
    }
}

void require_exact_fields(
    const Json& value,
    const std::set<std::string>& expected,
    std::string_view context) {
    if (!value.is_object() ||
        value.size() != expected.size()) {
        throw ClipGeneratorSettingsError(
            std::string(context) +
            " must contain exactly the supported fields");
    }
    for (const auto& [name, _] : value.items()) {
        if (!expected.contains(name)) {
            throw ClipGeneratorSettingsError(
                std::string(context) +
                " contains unknown field '" + name + "'");
        }
    }
}

bool boolean_value(
    const Json& value,
    std::string_view context) {
    if (!value.is_boolean()) {
        throw ClipGeneratorSettingsError(
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
        throw ClipGeneratorSettingsError(
            std::string(context) + " must be an integer");
    }
    try {
        const auto parsed = value.get<std::int64_t>();
        if (parsed < minimum || parsed > maximum) {
            throw ClipGeneratorSettingsError(
                std::string(context) +
                " is outside the supported Java integer range");
        }
        return static_cast<int>(parsed);
    }
    catch (const ClipGeneratorSettingsError&) {
        throw;
    }
    catch (const std::exception&) {
        throw ClipGeneratorSettingsError(
            std::string(context) +
            " is outside the supported Java integer range");
    }
}

double finite_non_negative(
    const Json& value,
    std::string_view context) {
    if (!value.is_number()) {
        throw ClipGeneratorSettingsError(
            std::string(context) + " must be a number");
    }
    const auto parsed = value.get<double>();
    if (!std::isfinite(parsed) || parsed < 0.0) {
        throw ClipGeneratorSettingsError(
            std::string(context) +
            " must be finite and non-negative");
    }
    return parsed;
}

double finite_positive(
    const Json& value,
    std::string_view context) {
    const auto parsed =
        finite_non_negative(value, context);
    if (parsed == 0.0) {
        throw ClipGeneratorSettingsError(
            std::string(context) +
            " must be finite and positive");
    }
    return parsed;
}

bool valid_unit_id(std::string_view value) noexcept {
    if (value.empty() ||
        value.size() > kMaximumUnitIdBytes) {
        return false;
    }
    const auto initial = value.front();
    if (!((initial >= 'A' && initial <= 'Z') ||
          (initial >= 'a' && initial <= 'z') ||
          (initial >= '0' && initial <= '9'))) {
        return false;
    }
    for (const auto character : value) {
        if (!((character >= 'A' && character <= 'Z') ||
              (character >= 'a' && character <= 'z') ||
              (character >= '0' && character <= '9') ||
              character == '.' || character == '_' ||
              character == ':' || character == '-')) {
            return false;
        }
    }
    return true;
}

bool valid_output_role(std::string_view value) noexcept {
    if (value.empty() ||
        value.size() > kMaximumOutputRoleBytes ||
        value.front() < 'a' || value.front() > 'z') {
        return false;
    }
    for (std::size_t index = 1;
         index < value.size();
         ++index) {
        const auto character = value[index];
        if (!((character >= 'a' && character <= 'z') ||
              (character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9'))) {
            return false;
        }
    }
    return true;
}

ClipGeneratorStorageMode storage_mode(
    const Json& value) {
    if (!value.is_string()) {
        throw ClipGeneratorSettingsError(
            "Clip Generator storageMode must be a string");
    }
    const auto name = value.get<std::string>();
    if (name == "wav-files") {
        return ClipGeneratorStorageMode::WavFiles;
    }
    if (name == "binary") {
        return ClipGeneratorStorageMode::Binary;
    }
    if (name == "both") {
        return ClipGeneratorStorageMode::Both;
    }
    throw ClipGeneratorSettingsError(
        "Clip Generator storageMode must be wav-files, binary, or both");
}

std::string_view storage_mode_name(
    ClipGeneratorStorageMode value) {
    switch (value) {
    case ClipGeneratorStorageMode::WavFiles:
        return "wav-files";
    case ClipGeneratorStorageMode::Binary:
        return "binary";
    case ClipGeneratorStorageMode::Both:
        return "both";
    }
    throw ClipGeneratorSettingsError(
        "Clip Generator storage mode cannot be serialized");
}

ClipGeneratorChannelSelection channel_selection(
    const Json& value,
    std::string_view context) {
    if (!value.is_string()) {
        throw ClipGeneratorSettingsError(
            std::string(context) + " must be a string");
    }
    const auto name = value.get<std::string>();
    if (name == "detection-channels-only") {
        return ClipGeneratorChannelSelection::
            DetectionChannelsOnly;
    }
    if (name == "first-detection-channel-only") {
        return ClipGeneratorChannelSelection::
            FirstDetectionChannelOnly;
    }
    if (name == "all-channels") {
        return ClipGeneratorChannelSelection::AllChannels;
    }
    throw ClipGeneratorSettingsError(
        std::string(context) +
        " must be detection-channels-only, "
        "first-detection-channel-only, or all-channels");
}

std::string_view channel_selection_name(
    ClipGeneratorChannelSelection value) {
    switch (value) {
    case ClipGeneratorChannelSelection::
            DetectionChannelsOnly:
        return "detection-channels-only";
    case ClipGeneratorChannelSelection::
            FirstDetectionChannelOnly:
        return "first-detection-channel-only";
    case ClipGeneratorChannelSelection::AllChannels:
        return "all-channels";
    }
    throw ClipGeneratorSettingsError(
        "Clip Generator channel selection cannot be serialized");
}

void validate_source(
    const ClipGeneratorTriggerSourceReference& source,
    std::string_view context) {
    if (!valid_unit_id(source.unit_id)) {
        throw ClipGeneratorSettingsError(
            std::string(context) +
            ".unitId is not a stable project entity id");
    }
    if (!valid_output_role(source.output_role)) {
        throw ClipGeneratorSettingsError(
            std::string(context) +
            ".outputRole is not a lower-camel project role id");
    }
}

void validate_policy(
    const ClipGeneratorTriggerPolicySettings& policy,
    std::size_t index) {
    const auto context =
        "Clip Generator triggerPolicies[" +
        std::to_string(index) + "]";
    validate_source(
        policy.trigger_source,
        context + ".triggerSource");
    if (!std::isfinite(policy.seconds_before_trigger) ||
        policy.seconds_before_trigger < 0.0) {
        throw ClipGeneratorSettingsError(
            context +
            ".secondsBeforeTrigger must be finite and non-negative");
    }
    if (!std::isfinite(policy.seconds_after_trigger) ||
        policy.seconds_after_trigger < 0.0) {
        throw ClipGeneratorSettingsError(
            context +
            ".secondsAfterTrigger must be finite and non-negative");
    }
    (void) channel_selection_name(
        policy.channel_selection);
    if (policy.clip_prefix &&
        policy.clip_prefix->size() >
            kMaximumClipPrefixBytes) {
        throw ClipGeneratorSettingsError(
            context +
            ".clipPrefix must contain at most 256 bytes");
    }
    if (policy.data_budget_kilobytes < 0) {
        throw ClipGeneratorSettingsError(
            context +
            ".dataBudgetKilobytes must be non-negative");
    }
    if (!std::isfinite(policy.budget_period_hours) ||
        policy.budget_period_hours <= 0.0) {
        throw ClipGeneratorSettingsError(
            context +
            ".budgetPeriodHours must be finite and positive");
    }
}

void validate_settings(
    const ClipGeneratorSettings& settings) {
    (void) storage_mode_name(settings.storage_mode);
    if (settings.trigger_policies.size() >
        kMaximumTriggerPolicies) {
        throw ClipGeneratorSettingsError(
            "Clip Generator supports at most 1024 trigger policies");
    }

    std::set<std::pair<std::string, std::string>>
        trigger_sources;
    for (std::size_t index = 0;
         index < settings.trigger_policies.size();
         ++index) {
        const auto& policy =
            settings.trigger_policies[index];
        validate_policy(policy, index);
        if (!trigger_sources.emplace(
                policy.trigger_source.unit_id,
                policy.trigger_source.output_role).second) {
            throw ClipGeneratorSettingsError(
                "Clip Generator trigger policy sources must be unique");
        }
    }
}

ClipGeneratorTriggerSourceReference source_from_value(
    const Json& value,
    std::string_view context) {
    require_exact_fields(
        value,
        {"unitId", "outputRole"},
        context);
    if (!value.at("unitId").is_string() ||
        !value.at("outputRole").is_string()) {
        throw ClipGeneratorSettingsError(
            std::string(context) +
            " unitId and outputRole must be strings");
    }

    ClipGeneratorTriggerSourceReference result{
        value.at("unitId").get<std::string>(),
        value.at("outputRole").get<std::string>(),
    };
    validate_source(result, context);
    return result;
}

Json source_to_value(
    const ClipGeneratorTriggerSourceReference& source,
    std::string_view context) {
    validate_source(source, context);
    return {
        {"unitId", source.unit_id},
        {"outputRole", source.output_role},
    };
}

std::optional<std::string> optional_prefix(
    const Json& value,
    std::string_view context) {
    if (value.is_null()) {
        return std::nullopt;
    }
    if (!value.is_string()) {
        throw ClipGeneratorSettingsError(
            std::string(context) +
            " must be a string or null");
    }
    auto result = value.get<std::string>();
    if (result.size() > kMaximumClipPrefixBytes) {
        throw ClipGeneratorSettingsError(
            std::string(context) +
            " must contain at most 256 bytes");
    }
    return result;
}

ClipGeneratorTriggerPolicySettings policy_from_value(
    const Json& value,
    std::size_t index) {
    const auto context =
        "Clip Generator triggerPolicies[" +
        std::to_string(index) + "]";
    require_exact_fields(
        value,
        {
            "triggerSource",
            "enabled",
            "secondsBeforeTrigger",
            "secondsAfterTrigger",
            "channelSelection",
            "clipPrefix",
            "useDataBudget",
            "dataBudgetKilobytes",
            "budgetPeriodHours",
        },
        context);

    ClipGeneratorTriggerPolicySettings result;
    result.trigger_source = source_from_value(
        value.at("triggerSource"),
        context + ".triggerSource");
    result.enabled = boolean_value(
        value.at("enabled"),
        context + ".enabled");
    result.seconds_before_trigger =
        finite_non_negative(
            value.at("secondsBeforeTrigger"),
            context + ".secondsBeforeTrigger");
    result.seconds_after_trigger =
        finite_non_negative(
            value.at("secondsAfterTrigger"),
            context + ".secondsAfterTrigger");
    result.channel_selection = channel_selection(
        value.at("channelSelection"),
        context + ".channelSelection");
    result.clip_prefix = optional_prefix(
        value.at("clipPrefix"),
        context + ".clipPrefix");
    result.use_data_budget = boolean_value(
        value.at("useDataBudget"),
        context + ".useDataBudget");
    result.data_budget_kilobytes = bounded_int(
        value.at("dataBudgetKilobytes"),
        0,
        std::numeric_limits<int>::max(),
        context + ".dataBudgetKilobytes");
    result.budget_period_hours = finite_positive(
        value.at("budgetPeriodHours"),
        context + ".budgetPeriodHours");
    validate_policy(result, index);
    return result;
}

Json policy_to_value(
    const ClipGeneratorTriggerPolicySettings& policy,
    std::size_t index) {
    const auto source_context =
        "Clip Generator triggerPolicies[" +
        std::to_string(index) + "].triggerSource";
    validate_policy(policy, index);
    return {
        {
            "triggerSource",
            source_to_value(
                policy.trigger_source,
                source_context),
        },
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
            "channelSelection",
            channel_selection_name(
                policy.channel_selection),
        },
        {
            "clipPrefix",
            policy.clip_prefix
                ? Json(*policy.clip_prefix)
                : Json(nullptr),
        },
        {"useDataBudget", policy.use_data_budget},
        {
            "dataBudgetKilobytes",
            policy.data_budget_kilobytes,
        },
        {
            "budgetPeriodHours",
            policy.budget_period_hours,
        },
    };
}

ClipGeneratorSettings settings_from_value(
    const Json& value) {
    require_exact_fields(
        value,
        {
            "storageMode",
            "datedSubFolders",
            "triggerPolicies",
        },
        "Clip Generator settings");

    ClipGeneratorSettings result;
    result.storage_mode =
        storage_mode(value.at("storageMode"));
    result.dated_subfolders = boolean_value(
        value.at("datedSubFolders"),
        "Clip Generator datedSubFolders");
    const auto& policies =
        value.at("triggerPolicies");
    if (!policies.is_array()) {
        throw ClipGeneratorSettingsError(
            "Clip Generator triggerPolicies must be an array");
    }
    if (policies.size() >
        kMaximumTriggerPolicies) {
        throw ClipGeneratorSettingsError(
            "Clip Generator supports at most 1024 trigger policies");
    }
    result.trigger_policies.reserve(policies.size());
    for (std::size_t index = 0;
         index < policies.size();
         ++index) {
        result.trigger_policies.push_back(
            policy_from_value(policies.at(index), index));
    }
    validate_settings(result);
    return result;
}

Json settings_to_value(
    const ClipGeneratorSettings& settings) {
    validate_settings(settings);
    Json policies = Json::array();
    for (std::size_t index = 0;
         index < settings.trigger_policies.size();
         ++index) {
        policies.push_back(
            policy_to_value(
                settings.trigger_policies[index],
                index));
    }
    return {
        {
            "storageMode",
            storage_mode_name(settings.storage_mode),
        },
        {"datedSubFolders", settings.dated_subfolders},
        {"triggerPolicies", std::move(policies)},
    };
}

} // namespace

ClipGeneratorSettings clip_generator_settings_from_json(
    std::string_view settings_json,
    std::uint32_t settings_version) {
    require_version(settings_version);
    try {
        return settings_from_value(
            parse_json(settings_json));
    }
    catch (const ClipGeneratorSettingsError&) {
        throw;
    }
    catch (const std::exception& error) {
        throw ClipGeneratorSettingsError(
            std::string(
                "Clip Generator settings are invalid: ") +
            error.what());
    }
}

std::string clip_generator_settings_to_json(
    const ClipGeneratorSettings& settings,
    std::uint32_t settings_version) {
    require_version(settings_version);
    try {
        const auto encoded =
            settings_to_value(settings).dump();
        (void) settings_from_value(
            Json::parse(encoded));
        return encoded;
    }
    catch (const ClipGeneratorSettingsError&) {
        throw;
    }
    catch (const std::exception& error) {
        throw ClipGeneratorSettingsError(
            std::string(
                "Clip Generator settings cannot be serialized: ") +
            error.what());
    }
}

std::string clip_generator_default_settings_json() {
    return clip_generator_settings_to_json(
        ClipGeneratorSettings{});
}

std::string_view
clip_generator_settings_schema_json() noexcept {
    return R"({
        "$schema":"https://json-schema.org/draft/2020-12/schema",
        "x-pamguard-authority":{
            "version":"2.02.18e",
            "commit":"dca55c81ef6f1498a8a3b926c69e7182afb915ee",
            "settingsClasses":[
                "clipgenerator.ClipSettings",
                "clipgenerator.ClipGenSetting"
            ],
            "dialogClasses":[
                "clipgenerator.ClipDialog",
                "clipgenerator.ClipGenSettingDialog"
            ],
            "processClasses":[
                "clipgenerator.ClipControl",
                "clipgenerator.ClipProcess",
                "clipgenerator.StandardClipBudgetMaker"
            ]
        },
        "x-pamguard-portable-omissions":[
            {
                "javaField":"dataSourceName",
                "reason":"the raw audio source is exclusively the controlled-unit rawAudio graph binding"
            },
            {
                "javaField":"outputFolder",
                "reason":"filesystem destinations are host/deployment owned"
            },
            {
                "javaField":"compressorIndex",
                "reason":"it belongs to the unfinished annotation storage path, which the Java dialog does not expose"
            },
            {
                "javaField":"mapLineLength",
                "reason":"it is a Clip display/map preference, not clip generation science"
            },
            {
                "javaField":"hadMapLine",
                "reason":"it is private legacy display-setting migration state"
            }
        ],
        "x-pamguard-portable-boundaries":[
            "ClipGenSetting.dataName is replaced by triggerSource {unitId, outputRole}, matching stable project binding identity",
            "trigger policies are receiver-owned by Clip Generator and must correspond one-to-one with sources on its multi-source trigger binding",
            "only outputs advertising Java-equivalent canClipGenerate capability may appear in triggerPolicies",
            "ClickDetector output explicitly sets canClipGenerate(false), so no Click Detector trigger policy is synthesized",
            "ClipSpectrogramMarkDataBlock is eligible and ClipControl changes its new policy to useDataBudget=false",
            "fresh ClipSettings.clipGenSettings null is canonically materialized as an empty triggerPolicies array",
            "STORE_ANNOTATION is excluded because its Java dialog path is commented out and ClipProcess cannot execute it",
            "non-negative clip windows and a positive finite budget period are enforced at the portable boundary",
            "raw-audio history capacity is derived from the largest enabled pre/post policy window and is not an independent Java setting"
        ],
        "type":"object",
        "additionalProperties":false,
        "properties":{
            "storageMode":{
                "type":"string",
                "enum":["wav-files","binary","both"]
            },
            "datedSubFolders":{"type":"boolean"},
            "triggerPolicies":{
                "type":"array",
                "maxItems":1024,
                "items":{
                    "type":"object",
                    "additionalProperties":false,
                    "properties":{
                        "triggerSource":{
                            "type":"object",
                            "additionalProperties":false,
                            "properties":{
                                "unitId":{
                                    "type":"string",
                                    "pattern":"^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$"
                                },
                                "outputRole":{
                                    "type":"string",
                                    "pattern":"^[a-z][A-Za-z0-9]{0,63}$"
                                }
                            },
                            "required":["unitId","outputRole"]
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
                        "channelSelection":{
                            "type":"string",
                            "enum":[
                                "detection-channels-only",
                                "first-detection-channel-only",
                                "all-channels"
                            ]
                        },
                        "clipPrefix":{
                            "type":["string","null"],
                            "maxLength":256
                        },
                        "useDataBudget":{"type":"boolean"},
                        "dataBudgetKilobytes":{
                            "type":"integer",
                            "minimum":0,
                            "maximum":2147483647
                        },
                        "budgetPeriodHours":{
                            "type":"number",
                            "exclusiveMinimum":0
                        }
                    },
                    "required":[
                        "triggerSource",
                        "enabled",
                        "secondsBeforeTrigger",
                        "secondsAfterTrigger",
                        "channelSelection",
                        "clipPrefix",
                        "useDataBudget",
                        "dataBudgetKilobytes",
                        "budgetPeriodHours"
                    ]
                }
            }
        },
        "required":[
            "storageMode",
            "datedSubFolders",
            "triggerPolicies"
        ],
        "x-pamguardConstraints":[
            {
                "id":"unique-trigger-source",
                "kind":"unique-composite",
                "arrayPointer":"/triggerPolicies",
                "fieldPointers":[
                    "/triggerSource/unitId",
                    "/triggerSource/outputRole"
                ]
            },
            {
                "id":"trigger-policy-binding-correspondence",
                "kind":"external-binding-set-equality",
                "bindingRole":"triggers",
                "settingsArrayPointer":"/triggerPolicies",
                "sourcePointer":"/triggerSource"
            },
            {
                "id":"clip-trigger-capability",
                "kind":"source-capability",
                "bindingRole":"triggers",
                "requiredCapability":"clip-trigger"
            }
        ]
    })";
}

} // namespace pamguard::core
