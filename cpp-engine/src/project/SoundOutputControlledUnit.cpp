#include "pamguard/project/SoundOutputControlledUnit.h"

#include <cmath>
#include <limits>
#include <set>

#include <json.hpp>

namespace pamguard::project {

namespace {

using Json = nlohmann::json;

void require_finite_range(
    double value,
    double minimum,
    double maximum,
    const char* name) {
    if (!std::isfinite(value) || value < minimum || value > maximum) {
        throw SoundOutputSettingsError(
            std::string("Sound Output ") + name +
            " is outside its supported range");
    }
}

} // namespace

SoundOutputPortableSettings sound_output_settings_from_json(
    std::string_view settings_json,
    std::uint32_t settings_version) {
    if (settings_version != 1) {
        throw SoundOutputSettingsError(
            "Unsupported Sound Output settings version");
    }
    Json settings;
    try {
        settings = Json::parse(settings_json);
    }
    catch (const std::exception& error) {
        throw SoundOutputSettingsError(
            std::string("Sound Output settings are not valid JSON: ") +
            error.what());
    }
    const std::set<std::string> expected{
        "channelBitmap",
        "defaultSampleRate",
        "playbackRateHz",
        "playbackSpeed",
        "playbackGainDb",
        "hpFilter",
    };
    if (!settings.is_object() || settings.size() != expected.size()) {
        throw SoundOutputSettingsError(
            "Sound Output settings must contain exactly the supported fields");
    }
    for (const auto& [name, _] : settings.items()) {
        if (!expected.contains(name)) {
            throw SoundOutputSettingsError(
                "Sound Output settings contain unknown field '" +
                name + "'");
        }
    }
    const auto& channel_bitmap = settings.at("channelBitmap");
    const bool valid_channel_bitmap =
        (channel_bitmap.is_number_unsigned() &&
         channel_bitmap.get<std::uint64_t>() <=
             std::numeric_limits<std::uint32_t>::max()) ||
        (channel_bitmap.is_number_integer() &&
         channel_bitmap.get<std::int64_t>() >= 0 &&
         static_cast<std::uint64_t>(
             channel_bitmap.get<std::int64_t>()) <=
             std::numeric_limits<std::uint32_t>::max());
    if (!valid_channel_bitmap) {
        throw SoundOutputSettingsError(
            "Sound Output channelBitmap must be an unsigned 32-bit bitmap");
    }
    if (!settings.at("defaultSampleRate").is_boolean()) {
        throw SoundOutputSettingsError(
            "Sound Output defaultSampleRate must be boolean");
    }
    SoundOutputPortableSettings result;
    result.channel_bitmap =
        settings.at("channelBitmap").get<std::uint32_t>();
    result.default_sample_rate =
        settings.at("defaultSampleRate").get<bool>();
    try {
        result.playback_rate_hz =
            settings.at("playbackRateHz").get<double>();
        result.playback_speed =
            settings.at("playbackSpeed").get<double>();
        result.playback_gain_db =
            settings.at("playbackGainDb").get<double>();
        result.hp_filter = settings.at("hpFilter").get<double>();
    }
    catch (const std::exception& error) {
        throw SoundOutputSettingsError(
            std::string("Sound Output numeric setting has the wrong type: ") +
            error.what());
    }
    require_finite_range(
        result.playback_rate_hz,
        std::numeric_limits<double>::min(),
        std::numeric_limits<double>::max(),
        "playbackRateHz");
    require_finite_range(
        result.playback_speed,
        0.03125,
        32.0,
        "playbackSpeed");
    require_finite_range(
        result.playback_gain_db,
        -80.0,
        80.0,
        "playbackGainDb");
    require_finite_range(
        result.hp_filter,
        0.0,
        0.5,
        "hpFilter");
    return result;
}

ControlledUnitDescriptor
make_sound_output_controlled_unit_descriptor() {
    return {
        "pamguard.sound-output",
        1,
        {
            "Sound Output",
            "Sound Processing",
            "soundPlayback.PlaybackControl",
            "direct",
            "Controls output of sound data for listening to on headphones",
            "sound_processing/soundPlaybackHelp/docs/soundPlayback_soundPlayback.html",
            {
                "src/PamModel/PamModel.java",
                "src/soundPlayback/PlaybackControl.java",
                "src/soundPlayback/PlaybackParameters.java",
                "src/soundPlayback/PlaybackProcess.java",
                "src/soundPlayback/swing/PlaybackDialog.java",
                "src/soundPlayback/swing/FilePlaybackDialogComponent.java",
                "src/soundPlayback/preprocess/PlaybackGain.java",
                "src/soundPlayback/preprocess/PlaybackFilter.java",
                "src/soundPlayback/preprocess/PlaybackDecimator.java",
            },
        },
        {
            0,
            std::nullopt,
            {
                RunMode::Normal,
                RunMode::Mixed,
                RunMode::Viewer,
            },
            {
                {
                    RunMode::Viewer,
                    1,
                    std::optional<std::size_t>{1},
                },
            },
        },
        {
            {
                "audio",
                "Playable raw audio",
                DataRoleDirection::Input,
                "pamguard.raw-audio",
                RoleCardinality::ExactlyOne,
                {
                    "sampled",
                },
                "PamDetection.RawDataUnit",
                std::nullopt,
            },
        },
        {
            1,
            {
                "soundPlayback.PlaybackParameters",
            },
            {
                "src/soundPlayback/PlaybackParameters.java",
                "src/soundPlayback/PlaybackControl.java",
                "src/soundPlayback/swing/PlaybackDialog.java",
                "src/soundPlayback/swing/FilePlaybackDialogComponent.java",
                "src/soundPlayback/preprocess/PlaybackGain.java",
                "src/soundPlayback/preprocess/PlaybackFilter.java",
                "src/soundPlayback/preprocess/PlaybackDecimator.java",
            },
            R"({"channelBitmap":0,"defaultSampleRate":true,"playbackRateHz":48000,"playbackSpeed":1,"playbackGainDb":0,"hpFilter":0})",
            {
                {
                    "settings.tabs",
                    {
                        "Playback",
                        "Side Bar",
                    },
                },
                {
                    "playback.sections",
                    {
                        "Data source",
                        "Source-specific playback options",
                    },
                },
                {
                    "runtime-controls",
                    {
                        "High-pass filter",
                        "Envelope mix",
                        "Playback speed",
                        "Gain",
                    },
                },
            },
            {
                {
                    "/channelBitmap",
                    "channelBitmap",
                    "0",
                    {},
                    {},
                    "soundPlayback.PlaybackParameters#channelBitmap",
                },
                {
                    "/defaultSampleRate",
                    "defaultSampleRate",
                    "true",
                    {},
                    {},
                    "soundPlayback.PlaybackParameters#defaultSampleRate",
                },
                {
                    "/playbackRateHz",
                    "playbackRate",
                    "48000",
                    {},
                    {},
                    "soundPlayback.PlaybackControl#DEFAULT_OUTPUT_RATE",
                },
                {
                    "/playbackSpeed",
                    "playbackSpeed",
                    "1",
                    {},
                    {},
                    "soundPlayback.PlaybackParameters#getPlaybackSpeed",
                },
                {
                    "/playbackGainDb",
                    "playbackGain",
                    "0",
                    {},
                    {},
                    "soundPlayback.PlaybackParameters#playbackGain",
                },
                {
                    "/hpFilter",
                    "hpFilter",
                    "0",
                    {},
                    {},
                    "soundPlayback.PlaybackParameters#hpFilter",
                },
            },
            SettingsChangePolicy::LiveSafe,
            "not-claimed",
            R"({
                "$schema":"https://json-schema.org/draft/2020-12/schema",
                "type":"object",
                "additionalProperties":false,
                "properties":{
                    "channelBitmap":{
                        "type":"integer",
                        "minimum":0,
                        "maximum":4294967295
                    },
                    "defaultSampleRate":{"type":"boolean"},
                    "playbackRateHz":{
                        "type":"number",
                        "exclusiveMinimum":0
                    },
                    "playbackSpeed":{
                        "type":"number",
                        "minimum":0.03125,
                        "maximum":32
                    },
                    "playbackGainDb":{
                        "type":"number",
                        "minimum":-80,
                        "maximum":80
                    },
                    "hpFilter":{
                        "type":"number",
                        "minimum":0,
                        "maximum":0.5
                    }
                },
                "required":[
                    "channelBitmap",
                    "defaultSampleRate",
                    "playbackRateHz",
                    "playbackSpeed",
                    "playbackGainDb",
                    "hpFilter"
                ]
            })",
        },
        {
            1,
            {
                {
                    "playback-process",
                    "pamguard.sound-output",
                    {
                        "",
                        "identity.v1",
                    },
                    true,
                    AvailabilityStatus::Available,
                    "browser-validated",
                },
            },
            {
                {
                    "audio",
                    {
                        "playback-process",
                        "audio",
                    },
                },
            },
            {},
            {},
            "pamguard.sound-output.runtime",
        },
        AvailabilityStatus::Available,
        "partial",
    };
}

} // namespace pamguard::project
