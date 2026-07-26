#include "pamguard/project/AuralListeningControlledUnit.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <set>
#include <utility>

#include <json.hpp>

namespace pamguard::project {

namespace {

using Json = nlohmann::json;

constexpr std::string_view kSettingsSchema = R"({
    "$schema":"https://json-schema.org/draft/2020-12/schema",
    "type":"object",
    "additionalProperties":false,
    "properties":{
        "maximumVolume":{
            "type":"integer",
            "minimum":0,
            "maximum":2147483647,
            "description":"Highest displayed volume button; Java displays 0 through this value inclusive."
        },
        "hydrophoneBitmap":{
            "type":"integer",
            "minimum":0,
            "maximum":4294967295
        },
        "species":{
            "type":"array",
            "items":{
                "type":"string",
                "minLength":1,
                "description":"Java SpeciesItem.name; the adapter additionally enforces the 50 UTF-16-code-unit limit."
            }
        },
        "effortStatuses":{
            "type":"array",
            "minItems":1,
            "items":{
                "type":"string",
                "minLength":1,
                "description":"Ordered labels used by ListeningEffortData."
            }
        }
    },
    "required":[
        "maximumVolume",
        "hydrophoneBitmap",
        "species",
        "effortStatuses"
    ]
})";

InstanceRulesDescriptor unlimited_in_all_modes() {
    return {
        0,
        std::nullopt,
        {
            RunMode::Normal,
            RunMode::Mixed,
            RunMode::Viewer,
        },
        {},
    };
}

Json parse_json(
    std::string_view encoded,
    std::string_view document_name) {
    try {
        return Json::parse(encoded);
    }
    catch (const Json::exception& error) {
        throw AuralListeningProjectAdapterError(
            "invalid-aural-listening-json",
            "Aural Listening " + std::string(document_name) +
                " is not valid JSON: " + error.what());
    }
}

void require_exact_fields(
    const Json& value,
    std::initializer_list<std::string_view> fields,
    std::string_view error_code,
    std::string_view document_name) {
    if (!value.is_object() || value.size() != fields.size()) {
        throw AuralListeningProjectAdapterError(
            std::string(error_code),
            "Aural Listening " + std::string(document_name) +
                " has an invalid object shape");
    }
    for (const auto field : fields) {
        if (!value.contains(std::string(field))) {
            throw AuralListeningProjectAdapterError(
                std::string(error_code),
                "Aural Listening " + std::string(document_name) +
                    " is missing '" + std::string(field) + "'");
        }
    }
}

std::uint64_t non_negative_integer(
    const Json& value,
    std::uint64_t maximum,
    std::string_view field,
    std::string_view error_code) {
    if (!value.is_number_integer()) {
        throw AuralListeningProjectAdapterError(
            std::string(error_code),
            "Aural Listening '" + std::string(field) +
                "' must be an integer");
    }
    std::uint64_t decoded = 0;
    if (value.is_number_unsigned()) {
        decoded = value.get<std::uint64_t>();
    }
    else {
        const auto signed_value = value.get<std::int64_t>();
        if (signed_value < 0) {
            throw AuralListeningProjectAdapterError(
                std::string(error_code),
                "Aural Listening '" + std::string(field) +
                    "' cannot be negative");
        }
        decoded = static_cast<std::uint64_t>(signed_value);
    }
    if (decoded > maximum) {
        throw AuralListeningProjectAdapterError(
            std::string(error_code),
            "Aural Listening '" + std::string(field) +
                "' exceeds its Java integer range");
    }
    return decoded;
}

std::uint32_t utf16_code_units(
    std::string_view value,
    std::uint32_t maximum,
    std::string_view field,
    std::string_view error_code) {
    std::uint64_t units = 0;
    for (std::size_t offset = 0; offset < value.size();) {
        const auto first =
            static_cast<unsigned char>(value[offset]);
        std::uint32_t code_point = 0;
        std::size_t length = 0;
        std::uint32_t minimum = 0;
        if (first <= 0x7Fu) {
            code_point = first;
            length = 1;
        }
        else if ((first & 0xE0u) == 0xC0u) {
            code_point = first & 0x1Fu;
            length = 2;
            minimum = 0x80u;
        }
        else if ((first & 0xF0u) == 0xE0u) {
            code_point = first & 0x0Fu;
            length = 3;
            minimum = 0x800u;
        }
        else if ((first & 0xF8u) == 0xF0u) {
            code_point = first & 0x07u;
            length = 4;
            minimum = 0x10000u;
        }
        else {
            throw AuralListeningProjectAdapterError(
                std::string(error_code),
                "Aural Listening '" + std::string(field) +
                    "' is not valid UTF-8");
        }
        if (offset + length > value.size()) {
            throw AuralListeningProjectAdapterError(
                std::string(error_code),
                "Aural Listening '" + std::string(field) +
                    "' ends inside a UTF-8 sequence");
        }
        for (std::size_t index = 1; index < length; ++index) {
            const auto next = static_cast<unsigned char>(
                value[offset + index]);
            if ((next & 0xC0u) != 0x80u) {
                throw AuralListeningProjectAdapterError(
                    std::string(error_code),
                    "Aural Listening '" + std::string(field) +
                        "' is not valid UTF-8");
            }
            code_point =
                (code_point << 6u) | (next & 0x3Fu);
        }
        if (code_point < minimum ||
            code_point > 0x10FFFFu ||
            (code_point >= 0xD800u &&
             code_point <= 0xDFFFu)) {
            throw AuralListeningProjectAdapterError(
                std::string(error_code),
                "Aural Listening '" + std::string(field) +
                    "' contains an invalid Unicode scalar");
        }
        units += code_point > 0xFFFFu ? 2u : 1u;
        if (units > maximum) {
            throw AuralListeningProjectAdapterError(
                std::string(error_code),
                "Aural Listening '" + std::string(field) +
                    "' exceeds " + std::to_string(maximum) +
                    " Java UTF-16 code units");
        }
        offset += length;
    }
    return static_cast<std::uint32_t>(units);
}

void validate_settings(const AuralListeningSettings& settings) {
    if (settings.maximum_volume >
        static_cast<std::uint32_t>(
            std::numeric_limits<std::int32_t>::max())) {
        throw AuralListeningProjectAdapterError(
            "invalid-aural-listening-settings",
            "Aural Listening maximumVolume exceeds Java int range");
    }
    for (const auto& species : settings.species) {
        if (species.empty()) {
            throw AuralListeningProjectAdapterError(
                "invalid-aural-listening-species",
                "Aural Listening species names cannot be empty");
        }
        static_cast<void>(utf16_code_units(
            species,
            kAuralListeningSpeciesMaximumUtf16CodeUnits,
            "species name",
            "invalid-aural-listening-species"));
    }
    if (settings.effort_statuses.empty()) {
        throw AuralListeningProjectAdapterError(
            "invalid-aural-listening-effort-statuses",
            "Aural Listening requires at least one effort status");
    }
    for (const auto& status : settings.effort_statuses) {
        if (status.empty()) {
            throw AuralListeningProjectAdapterError(
                "invalid-aural-listening-effort-statuses",
                "Aural Listening effort status labels cannot be empty");
        }
        static_cast<void>(utf16_code_units(
            status,
            kAuralListeningSpeciesMaximumUtf16CodeUnits,
            "effort status",
            "invalid-aural-listening-effort-statuses"));
    }
}

std::uint32_t action_bitmap(
    const Json& action,
    std::string_view error_code) {
    return static_cast<std::uint32_t>(
        non_negative_integer(
            action.at("hydrophoneBitmap"),
            std::numeric_limits<std::uint32_t>::max(),
            "hydrophoneBitmap",
            error_code));
}

std::vector<SettingDefaultDescriptor> default_evidence() {
    return {
        {
            "/maximumVolume",
            "nVolumes",
            "5",
            {},
            "Java renders volume buttons from 0 through nVolumes inclusive",
            "listening.ListeningParameters#nVolumes",
        },
        {
            "/hydrophoneBitmap",
            "hydrophones",
            "3",
            {},
            "bits 0 and 1 are selected initially",
            "listening.ListeningParameters#hydrophones",
        },
        {
            "/species",
            "speciesList",
            R"(["Sperm Whale","Dolphin Clicks","Dolphin Whistles","Ship Noise","Airguns","Other Noise"])",
            {},
            "ordered SpeciesItem.name projection; optional PamSymbol is an "
            "excluded Java display preference",
            "listening.ListeningParameters#speciesList",
        },
        {
            "/effortStatuses",
            "effortStati",
            R"(["On Effort","Off Effort"])",
            {},
            "ordered radio-button and persisted status labels",
            "listening.ListeningParameters#effortStati",
        },
    };
}

PublicDataRoleDescriptor effort_output() {
    return {
        "effort",
        "Listening Effort",
        DataRoleDirection::Output,
        std::string(kAuralListeningEffortDataType),
        RoleCardinality::ExactlyOne,
        {
            "events",
            "effort",
            "hydrophone-selection",
        },
        {},
        std::nullopt,
    };
}

PublicDataRoleDescriptor things_heard_output() {
    return {
        "thingsHeard",
        "Things Heard",
        DataRoleDirection::Output,
        std::string(kAuralListeningThingHeardDataType),
        RoleCardinality::ExactlyOne,
        {
            "events",
            "detections",
            "annotations",
            "hydrophone-selection",
        },
        {},
        std::nullopt,
    };
}

} // namespace

AuralListeningProjectAdapterError::
    AuralListeningProjectAdapterError(
        std::string code,
        std::string message)
    : std::invalid_argument(std::move(message)),
      code_(std::move(code)) {}

const std::string&
AuralListeningProjectAdapterError::code() const noexcept {
    return code_;
}

std::string
aural_listening_default_settings_json() {
    return aural_listening_settings_to_json(
        AuralListeningSettings{},
        1);
}

std::string_view
aural_listening_settings_schema_json() noexcept {
    return kSettingsSchema;
}

AuralListeningSettings
aural_listening_settings_from_json(
    std::string_view encoded,
    std::uint32_t settings_version) {
    if (settings_version != 1) {
        throw AuralListeningProjectAdapterError(
            "unsupported-aural-listening-settings-version",
            "Unsupported Aural Listening settings version");
    }
    const auto value = parse_json(encoded, "settings");
    require_exact_fields(
        value,
        {
            "maximumVolume",
            "hydrophoneBitmap",
            "species",
            "effortStatuses",
        },
        "invalid-aural-listening-settings",
        "settings");
    if (!value.at("species").is_array() ||
        !value.at("effortStatuses").is_array()) {
        throw AuralListeningProjectAdapterError(
            "invalid-aural-listening-settings",
            "Aural Listening species and effortStatuses must be arrays");
    }

    AuralListeningSettings settings;
    settings.maximum_volume =
        static_cast<std::uint32_t>(
            non_negative_integer(
                value.at("maximumVolume"),
                static_cast<std::uint64_t>(
                    std::numeric_limits<std::int32_t>::max()),
                "maximumVolume",
                "invalid-aural-listening-settings"));
    settings.hydrophone_bitmap =
        static_cast<std::uint32_t>(
            non_negative_integer(
                value.at("hydrophoneBitmap"),
                std::numeric_limits<std::uint32_t>::max(),
                "hydrophoneBitmap",
                "invalid-aural-listening-settings"));
    settings.species.clear();
    for (const auto& species : value.at("species")) {
        if (!species.is_string()) {
            throw AuralListeningProjectAdapterError(
                "invalid-aural-listening-species",
                "Aural Listening species entries must be strings");
        }
        settings.species.push_back(species.get<std::string>());
    }
    settings.effort_statuses.clear();
    for (const auto& status : value.at("effortStatuses")) {
        if (!status.is_string()) {
            throw AuralListeningProjectAdapterError(
                "invalid-aural-listening-effort-statuses",
                "Aural Listening effort status entries must be strings");
        }
        settings.effort_statuses.push_back(
            status.get<std::string>());
    }
    validate_settings(settings);
    return settings;
}

std::string
aural_listening_settings_to_json(
    const AuralListeningSettings& settings,
    std::uint32_t settings_version) {
    if (settings_version != 1) {
        throw AuralListeningProjectAdapterError(
            "unsupported-aural-listening-settings-version",
            "Unsupported Aural Listening settings version");
    }
    validate_settings(settings);
    return Json{
        {"maximumVolume", settings.maximum_volume},
        {"hydrophoneBitmap", settings.hydrophone_bitmap},
        {"species", settings.species},
        {"effortStatuses", settings.effort_statuses},
    }.dump();
}

std::string
aural_listening_runtime_settings_json(
    std::string_view portable_settings_json,
    std::uint32_t settings_version) {
    const auto canonical = Json::parse(
        aural_listening_settings_to_json(
            aural_listening_settings_from_json(
                portable_settings_json,
                settings_version),
            settings_version));
    return Json{
        {"settings", canonical},
        {
            "naturalLifetimeSeconds",
            kAuralListeningNaturalLifetimeSeconds,
        },
        {
            "speciesMaximumUtf16CodeUnits",
            kAuralListeningSpeciesMaximumUtf16CodeUnits,
        },
        {
            "databaseCommentUtf16CodeUnits",
            kAuralListeningDatabaseCommentUtf16CodeUnits,
        },
    }.dump();
}

AuralListeningEffortEntry
aural_listening_effort_entry_from_action_json(
    const AuralListeningSettings& settings,
    std::string_view action_json,
    std::int64_t time_milliseconds) {
    validate_settings(settings);
    const auto action = parse_json(action_json, "effort action");
    require_exact_fields(
        action,
        {
            "statusIndex",
            "hydrophoneBitmap",
        },
        "invalid-aural-listening-effort-action",
        "effort action");
    const auto status_index = non_negative_integer(
        action.at("statusIndex"),
        std::numeric_limits<std::uint32_t>::max(),
        "statusIndex",
        "invalid-aural-listening-effort-action");
    if (status_index >= settings.effort_statuses.size()) {
        throw AuralListeningProjectAdapterError(
            "aural-listening-effort-status-out-of-range",
            "Aural Listening effort action statusIndex is out of range");
    }
    return {
        time_milliseconds,
        settings.effort_statuses[
            static_cast<std::size_t>(status_index)],
        action_bitmap(
            action,
            "invalid-aural-listening-effort-action"),
    };
}

AuralListeningThingHeardEntry
aural_listening_thing_heard_from_species_action_json(
    const AuralListeningSettings& settings,
    std::string_view action_json,
    std::int64_t time_milliseconds) {
    validate_settings(settings);
    const auto action =
        parse_json(action_json, "species/volume action");
    require_exact_fields(
        action,
        {
            "speciesIndex",
            "volume",
            "hydrophoneBitmap",
            "comment",
        },
        "invalid-aural-listening-species-action",
        "species/volume action");
    if (!action.at("comment").is_string()) {
        throw AuralListeningProjectAdapterError(
            "invalid-aural-listening-species-action",
            "Aural Listening species/volume action comment must be a "
            "string");
    }
    const auto species_index = non_negative_integer(
        action.at("speciesIndex"),
        std::numeric_limits<std::uint32_t>::max(),
        "speciesIndex",
        "invalid-aural-listening-species-action");
    if (species_index >= settings.species.size()) {
        throw AuralListeningProjectAdapterError(
            "aural-listening-species-out-of-range",
            "Aural Listening species action speciesIndex is out of range");
    }
    const auto volume = non_negative_integer(
        action.at("volume"),
        settings.maximum_volume,
        "volume",
        "aural-listening-volume-out-of-range");
    return {
        time_milliseconds,
        static_cast<std::int32_t>(species_index),
        settings.species[
            static_cast<std::size_t>(species_index)],
        static_cast<std::int32_t>(volume),
        action_bitmap(
            action,
            "invalid-aural-listening-species-action"),
        action.at("comment").get<std::string>(),
    };
}

AuralListeningThingHeardEntry
aural_listening_thing_heard_from_comment_action_json(
    std::string_view action_json,
    std::int64_t time_milliseconds) {
    const auto action =
        parse_json(action_json, "comment action");
    require_exact_fields(
        action,
        {
            "hydrophoneBitmap",
            "comment",
        },
        "invalid-aural-listening-comment-action",
        "comment action");
    if (!action.at("comment").is_string()) {
        throw AuralListeningProjectAdapterError(
            "invalid-aural-listening-comment-action",
            "Aural Listening comment action comment must be a string");
    }
    return {
        time_milliseconds,
        -1,
        std::nullopt,
        -1,
        action_bitmap(
            action,
            "invalid-aural-listening-comment-action"),
        action.at("comment").get<std::string>(),
    };
}

std::string
aural_listening_effort_entry_to_json(
    const AuralListeningEffortEntry& entry) {
    if (entry.status.empty()) {
        throw AuralListeningProjectAdapterError(
            "invalid-aural-listening-effort-entry",
            "Aural Listening effort entry status cannot be empty");
    }
    return Json{
        {"timeMilliseconds", entry.time_milliseconds},
        {"status", entry.status},
        {"channelBitmap", entry.channel_bitmap},
    }.dump();
}

std::string
aural_listening_thing_heard_entry_to_json(
    const AuralListeningThingHeardEntry& entry) {
    const auto comment_only =
        entry.species_index == -1 &&
        entry.volume == -1 &&
        !entry.species_name.has_value();
    const auto species_observation =
        entry.species_index >= 0 &&
        entry.volume >= 0 &&
        entry.species_name.has_value() &&
        !entry.species_name->empty();
    if (!comment_only && !species_observation) {
        throw AuralListeningProjectAdapterError(
            "invalid-aural-listening-thing-heard-entry",
            "Aural Listening Things Heard entry must be either Java's "
            "comment-only sentinel or a species/volume observation");
    }
    Json encoded{
        {"timeMilliseconds", entry.time_milliseconds},
        {"speciesIndex", entry.species_index},
        {"volume", entry.volume},
        {"channelBitmap", entry.channel_bitmap},
        {"comment", entry.comment},
    };
    encoded["speciesName"] = entry.species_name.has_value()
        ? Json(*entry.species_name)
        : Json(nullptr);
    return encoded.dump();
}

ControlledUnitDescriptor
make_aural_listening_controlled_unit_descriptor() {
    return {
        std::string(kAuralListeningControlledUnitTypeId),
        1,
        {
            "Aural Listening Form",
            "Utilities",
            "listening.ListeningControl",
            "direct",
            "Creates a form for the user to manually log things they hear",
            "utilities/listening/docs/Listening_Overview.html",
            {
                "src/PamModel/PamModel.java",
                "src/listening/ListeningControl.java",
                "src/listening/ListeningParameters.java",
                "src/listening/SpeciesItem.java",
                "src/listening/ListeningDialog.java",
                "src/listening/ThingHeardTabPanelControl.java",
                "src/listening/ListeningProcess.java",
                "src/listening/ListeningEffortData.java",
                "src/listening/ThingHeard.java",
                "src/listening/ListeningEffortLogging.java",
                "src/listening/ThingHeardLogging.java",
                "src/help/utilities/listening/docs/"
                "Listening_Overview.html",
                "src/help/utilities/listening/docs/"
                "Listening_Configuration.html",
            },
        },
        unlimited_in_all_modes(),
        {
            effort_output(),
            things_heard_output(),
        },
        {
            1,
            {
                "listening.ListeningParameters",
                "listening.SpeciesItem",
            },
            {
                "src/listening/ListeningParameters.java",
                "src/listening/SpeciesItem.java",
                "src/listening/ListeningDialog.java",
                "src/listening/ListeningControl.java",
                "src/listening/ThingHeardTabPanelControl.java",
                "src/listening/ListeningProcess.java",
                "src/listening/ListeningEffortData.java",
                "src/listening/ThingHeard.java",
            },
            aural_listening_default_settings_json(),
            {
                {
                    "menu.detection",
                    {
                        "<unit name> Settings...",
                    },
                },
                {
                    "settings.species-sound-types",
                    {
                        "Aural Monitoring",
                        "Species / Sound Types",
                        "#",
                        "Type",
                        "Sym",
                    },
                },
                {
                    "settings.species-sound-types.actions",
                    {
                        "Add ...",
                        "Remove",
                        "Edit ...",
                        "Move up",
                        "Move Down",
                        "Edit symbol (double-click Sym; Java display "
                        "preference deferred)",
                    },
                },
                {
                    "operator.sections",
                    {
                        "Effort",
                        "Things Heard",
                        "History",
                    },
                },
                {
                    "operator.effort.actions",
                    {
                        "On Effort",
                        "Off Effort",
                        "Hydrophones Monitored :",
                    },
                },
                {
                    "operator.things-heard",
                    {
                        "Species / type",
                        "Volume",
                        "0",
                        "1",
                        "2",
                        "3",
                        "4",
                        "5",
                        "Hit Enter to store comment",
                        "Comment:",
                    },
                },
                {
                    "operator.history.columns",
                    {
                        "Time",
                        "Species",
                        "Volume",
                        "Comment",
                    },
                },
            },
            default_evidence(),
            SettingsChangePolicy::LiveSafe,
            "java-source-validated-operational-settings",
            std::string(aural_listening_settings_schema_json()),
        },
        {
            1,
            {
                {
                    "listening-process",
                    std::string(
                        kAuralListeningControlledUnitTypeId),
                    {
                        "",
                        std::string(
                            kAuralListeningRuntimeSettingsAdapterId),
                    },
                    true,
                    AvailabilityStatus::Unavailable,
                    "dedicated-effort-and-things-heard-runtime-required",
                },
            },
            {
                {
                    "effort",
                    {
                        "listening-process",
                        "effort",
                    },
                },
                {
                    "thingsHeard",
                    {
                        "listening-process",
                        "things-heard",
                    },
                },
            },
            {},
            {},
            "pamguard.aural-listening.runtime",
        },
        AvailabilityStatus::Unavailable,
        "experimental",
    };
}

} // namespace pamguard::project
