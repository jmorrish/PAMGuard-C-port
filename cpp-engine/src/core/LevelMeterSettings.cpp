#include "pamguard/core/LevelMeterSettings.h"

#include <set>

#include <json.hpp>

namespace pamguard::core {

namespace {

using Json = nlohmann::json;

std::uint32_t required_stored_enum(
    const Json& settings,
    const char* name,
    std::uint32_t maximum) {
    const auto& value = settings.at(name);
    if (!value.is_number_integer() ||
        value.get<std::int64_t>() < 0 ||
        static_cast<std::uint64_t>(value.get<std::int64_t>()) >
            maximum) {
        throw LevelMeterSettingsError(
            std::string("Level Meter ") + name +
            " is not a supported Java stored value");
    }
    return value.get<std::uint32_t>();
}

void validate(const LevelMeterSettings& settings) {
    // LevelMeterDialog stores -abs((int) enteredValue) and rejects zero.
    if (settings.min_level_db >= 0) {
        throw LevelMeterSettingsError(
            "Level Meter minLevel must be a negative integer");
    }
}

} // namespace

LevelMeterSettings level_meter_settings_from_json(
    std::string_view settings_json,
    std::uint32_t settings_version) {
    if (settings_version != 1) {
        throw LevelMeterSettingsError(
            "Unsupported Level Meter settings version");
    }

    Json settings;
    try {
        settings = Json::parse(settings_json);
    }
    catch (const std::exception& error) {
        throw LevelMeterSettingsError(
            std::string("Level Meter settings are not valid JSON: ") +
            error.what());
    }

    const std::set<std::string> expected{
        "minLevel",
        "scaleReference",
        "scaleType",
    };
    if (!settings.is_object() || settings.size() != expected.size()) {
        throw LevelMeterSettingsError(
            "Level Meter settings must contain exactly minLevel, "
            "scaleReference, and scaleType");
    }
    for (const auto& [name, _] : settings.items()) {
        if (!expected.contains(name)) {
            throw LevelMeterSettingsError(
                "Level Meter settings contain unknown field '" +
                name + "'");
        }
    }
    if (!settings.at("minLevel").is_number_integer()) {
        throw LevelMeterSettingsError(
            "Level Meter minLevel must be an integer");
    }

    LevelMeterSettings result;
    result.min_level_db = settings.at("minLevel").get<int>();
    result.scale_reference =
        static_cast<LevelMeterScaleReference>(
            required_stored_enum(settings, "scaleReference", 2));
    result.scale_type =
        static_cast<LevelMeterScaleType>(
            required_stored_enum(settings, "scaleType", 1));
    validate(result);
    return result;
}

std::string level_meter_settings_to_json(
    const LevelMeterSettings& settings,
    std::uint32_t settings_version) {
    if (settings_version != 1) {
        throw LevelMeterSettingsError(
            "Unsupported Level Meter settings version");
    }
    validate(settings);
    return Json{
        {"minLevel", settings.min_level_db},
        {
            "scaleReference",
            static_cast<std::uint32_t>(settings.scale_reference),
        },
        {
            "scaleType",
            static_cast<std::uint32_t>(settings.scale_type),
        },
    }.dump();
}

std::string level_meter_default_settings_json() {
    return level_meter_settings_to_json(LevelMeterSettings{});
}

std::string_view level_meter_settings_schema_json() noexcept {
    return R"({
        "$schema":"https://json-schema.org/draft/2020-12/schema",
        "type":"object",
        "additionalProperties":false,
        "properties":{
            "minLevel":{
                "type":"integer",
                "maximum":-1
            },
            "scaleReference":{
                "type":"integer",
                "enum":[0,1,2]
            },
            "scaleType":{
                "type":"integer",
                "enum":[0,1]
            }
        },
        "required":[
            "minLevel",
            "scaleReference",
            "scaleType"
        ]
    })";
}

} // namespace pamguard::core
