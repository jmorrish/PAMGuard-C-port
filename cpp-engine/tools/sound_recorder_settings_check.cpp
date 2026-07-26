#include "pamguard/core/SoundRecorderSettings.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

#include <json.hpp>

namespace {

using Json = nlohmann::json;
using pamguard::core::SoundRecorderOperationMode;
using pamguard::core::SoundRecorderSettings;
using pamguard::core::SoundRecorderSettingsError;
using pamguard::core::SoundRecorderTriggerPolicySettings;
using pamguard::core::sound_recorder_default_settings_json;
using pamguard::core::sound_recorder_settings_from_json;
using pamguard::core::sound_recorder_settings_schema_json;
using pamguard::core::sound_recorder_settings_to_json;

constexpr std::string_view kExpectedVersion = "2.02.18e";
constexpr std::string_view kExpectedCommit =
    "dca55c81ef6f1498a8a3b926c69e7182afb915ee";

void require(
    bool condition,
    std::string_view message) {
    if (!condition) {
        throw std::runtime_error(
            std::string(message));
    }
}

Json read_json(const char* path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error(
            "Could not open Sound Recorder Java fixture");
    }
    try {
        return Json::parse(input);
    }
    catch (const std::exception& error) {
        throw std::runtime_error(
            std::string(
                "Sound Recorder Java fixture is invalid JSON: ") +
            error.what());
    }
}

void expect_rejected(
    std::string_view settings_json,
    std::uint32_t version = 1) {
    bool rejected = false;
    try {
        (void) sound_recorder_settings_from_json(
            settings_json,
            version);
    }
    catch (const SoundRecorderSettingsError&) {
        rejected = true;
    }
    require(
        rejected,
        "Invalid Sound Recorder settings were accepted");
}

void check_java_defaults_and_omissions(
    const Json& fixture) {
    require(
        fixture.is_object() &&
            fixture.size() == 4 &&
            fixture.at("authority").at("version")
                    .get<std::string>() ==
                kExpectedVersion &&
            fixture.at("authority").at("commit")
                    .get<std::string>() ==
                kExpectedCommit &&
            fixture.at("authority").at("exporter") ==
                "SoundRecorder."
                "SoundRecorderSettingsFixtureExporter" &&
            fixture.at("authority").at("settingsClass") ==
                "SoundRecorder.RecorderSettings" &&
            fixture.at("authority").at(
                "triggerPolicyClass") ==
                "SoundRecorder.trigger.RecorderTriggerData",
        "Sound Recorder fixture authority changed");

    const auto& omissions =
        fixture.at("portableOmissions");
    require(
        omissions.is_object() &&
            omissions.size() == 3 &&
            omissions.contains("rawDataSource") &&
            omissions.contains("outputFolder") &&
            omissions.contains("decisionMaker"),
        "Sound Recorder portable omission boundary changed");

    const auto fixture_defaults =
        fixture.at("portableSettingsDefaults");
    const auto generated_defaults = Json::parse(
        sound_recorder_default_settings_json());
    require(
        fixture_defaults == generated_defaults,
        "Sound Recorder portable defaults diverged from Java");
    require(
        !generated_defaults.contains("rawDataSource") &&
            !generated_defaults.contains("outputFolder") &&
            !generated_defaults.contains("decisionMaker"),
        "Host/binding/transient fields leaked into portable defaults");

    const auto defaults =
        sound_recorder_settings_from_json(
            fixture_defaults.dump(),
            1);
    require(
        defaults.operation_mode ==
                SoundRecorderOperationMode::Idle &&
            defaults.channel_bitmap == 3 &&
            defaults.bit_depth == 16 &&
            !defaults.enable_buffer &&
            defaults.buffer_length_seconds == 30 &&
            defaults.file_initials == "PAM" &&
            defaults.file_type == "WAVE" &&
            defaults.auto_interval_seconds == 300 &&
            defaults.auto_duration_seconds == 10 &&
            defaults.limit_length_seconds &&
            defaults.max_length_seconds == 3600 &&
            defaults.round_file_starts &&
            defaults.limit_length_megabytes &&
            defaults.max_length_megabytes == 640 &&
            defaults.dated_subfolders &&
            defaults.trigger_policies.empty(),
        "Sound Recorder C++ constructor defaults changed");
    require(
        sound_recorder_settings_to_json(defaults) ==
            sound_recorder_default_settings_json(),
        "Sound Recorder defaults did not round-trip canonically");
}

void check_trigger_policy_field_defaults(
    const Json& fixture) {
    auto settings =
        fixture.at("portableSettingsDefaults");
    const auto trigger =
        fixture.at("triggerPolicyFieldDefaults");
    settings.at("triggerPolicies").push_back(trigger);

    const auto decoded =
        sound_recorder_settings_from_json(
            settings.dump(),
            1);
    require(
        decoded.trigger_policies.size() == 1,
        "Sound Recorder trigger policy was not decoded");
    const auto& policy = decoded.trigger_policies.front();
    require(
        policy.trigger_name == "__fixture_trigger__" &&
            !policy.enabled &&
            policy.seconds_before_trigger == 0.0 &&
            policy.seconds_after_trigger == 10.0 &&
            policy.min_detection_count == 1 &&
            policy.count_seconds == 0 &&
            policy.min_gap_between_triggers_seconds == 0 &&
            policy.max_total_trigger_length_seconds == 0 &&
            policy.day_budget_megabytes == 0 &&
            policy.last_trigger_start_unix_ms == 0 &&
            policy.last_trigger_end_unix_ms == 0 &&
            policy.used_day_budget_bytes == 0,
        "RecorderTriggerData field defaults diverged from Java");

    const auto encoded = Json::parse(
        sound_recorder_settings_to_json(decoded));
    require(
        encoded.at("triggerPolicies").at(0) == trigger,
        "RecorderTriggerData fields did not round-trip exactly");
}

void check_non_default_round_trip() {
    SoundRecorderSettings settings;
    settings.operation_mode =
        SoundRecorderOperationMode::RestoreLast;
    settings.channel_bitmap = 0x80000001U;
    settings.bit_depth = 24;
    settings.enable_buffer = true;
    settings.buffer_length_seconds = 45;
    settings.file_initials.clear();
    settings.file_type = "AIFF";
    settings.auto_interval_seconds = 601;
    settings.auto_duration_seconds = 17;
    settings.limit_length_seconds = false;
    settings.max_length_seconds = 7200;
    settings.round_file_starts = false;
    settings.limit_length_megabytes = false;
    settings.max_length_megabytes = 2048;
    settings.dated_subfolders = false;

    SoundRecorderTriggerPolicySettings policy;
    policy.trigger_name = "Whistle trigger";
    policy.enabled = true;
    policy.seconds_before_trigger = 2.5;
    policy.seconds_after_trigger = 13.25;
    policy.min_detection_count = 4;
    policy.count_seconds = 9;
    policy.min_gap_between_triggers_seconds = 60;
    policy.max_total_trigger_length_seconds = 900;
    policy.day_budget_megabytes = 512;
    policy.last_trigger_start_unix_ms =
        1721856000123LL;
    policy.last_trigger_end_unix_ms =
        1721856001123LL;
    policy.used_day_budget_bytes = 123456789;
    settings.trigger_policies.push_back(policy);

    const auto encoded =
        sound_recorder_settings_to_json(settings);
    const auto decoded =
        sound_recorder_settings_from_json(
            encoded,
            1);
    require(
        decoded == settings &&
            sound_recorder_settings_to_json(decoded) ==
                encoded,
        "Non-default Sound Recorder settings did not round-trip");

    for (const auto mode : {
             SoundRecorderOperationMode::Idle,
             SoundRecorderOperationMode::Continuous,
             SoundRecorderOperationMode::Cycle,
             SoundRecorderOperationMode::RestoreLast,
         }) {
        settings.operation_mode = mode;
        const auto mode_json =
            sound_recorder_settings_to_json(settings);
        require(
            sound_recorder_settings_from_json(
                mode_json,
                1) == settings,
            "Sound Recorder operation mode did not round-trip");
    }
}

void check_strict_rejection() {
    const auto defaults = Json::parse(
        sound_recorder_default_settings_json());

    expect_rejected("{");
    expect_rejected(defaults.dump(), 0);
    expect_rejected(defaults.dump(), 2);

    auto candidate = defaults;
    candidate.erase("fileType");
    expect_rejected(candidate.dump());

    candidate = defaults;
    candidate["extra"] = true;
    expect_rejected(candidate.dump());

    candidate = defaults;
    candidate["operationMode"] = "always-record";
    expect_rejected(candidate.dump());

    candidate = defaults;
    candidate["channelBitmap"] = 0;
    expect_rejected(candidate.dump());

    candidate = defaults;
    candidate["channelBitmap"] = 4294967296ULL;
    expect_rejected(candidate.dump());

    candidate = defaults;
    candidate["bitDepth"] = 12;
    expect_rejected(candidate.dump());

    candidate = defaults;
    candidate["enableBuffer"] = 1;
    expect_rejected(candidate.dump());

    candidate = defaults;
    candidate["bufferLengthSeconds"] = -1;
    expect_rejected(candidate.dump());

    candidate = defaults;
    candidate["fileInitials"] =
        std::string(257, 'p');
    expect_rejected(candidate.dump());

    candidate = defaults;
    candidate["fileType"] = "";
    expect_rejected(candidate.dump());

    candidate = defaults;
    candidate["autoIntervalSeconds"] = 0;
    expect_rejected(candidate.dump());

    candidate = defaults;
    candidate["autoDurationSeconds"] = 0;
    expect_rejected(candidate.dump());

    candidate = defaults;
    candidate["maxLengthSeconds"] = 0;
    expect_rejected(candidate.dump());

    candidate = defaults;
    candidate["maxLengthMegaBytes"] = -1;
    expect_rejected(candidate.dump());

    candidate = defaults;
    candidate["triggerPolicies"] = Json::object();
    expect_rejected(candidate.dump());

    SoundRecorderTriggerPolicySettings policy;
    policy.trigger_name = "Detector";
    auto with_policy = defaults;
    with_policy["triggerPolicies"] = Json::array(
        {Json::parse(sound_recorder_settings_to_json(
             [&]() {
                 SoundRecorderSettings value;
                 value.trigger_policies.push_back(policy);
                 return value;
             }()))
             .at("triggerPolicies")
             .at(0)});

    candidate = with_policy;
    candidate.at("triggerPolicies").at(0).erase(
        "usedDayBudgetBytes");
    expect_rejected(candidate.dump());

    candidate = with_policy;
    candidate.at("triggerPolicies").at(0)["extra"] = 1;
    expect_rejected(candidate.dump());

    candidate = with_policy;
    candidate.at("triggerPolicies").at(0)["triggerName"] = "";
    expect_rejected(candidate.dump());

    candidate = with_policy;
    candidate.at("triggerPolicies").at(0)[
        "secondsBeforeTrigger"] = -0.1;
    expect_rejected(candidate.dump());

    candidate = with_policy;
    candidate.at("triggerPolicies").at(0)[
        "minDetectionCount"] = 0;
    expect_rejected(candidate.dump());

    candidate = with_policy;
    candidate.at("triggerPolicies").at(0)[
        "dayBudgetMegaBytes"] = -1;
    expect_rejected(candidate.dump());

    candidate = with_policy;
    candidate.at("triggerPolicies").at(0)[
        "lastTriggerStartUnixMs"] = -1;
    expect_rejected(candidate.dump());

    candidate = with_policy;
    candidate.at("triggerPolicies").at(0)[
        "usedDayBudgetBytes"] =
        9223372036854775808ULL;
    expect_rejected(candidate.dump());

    candidate = with_policy;
    candidate.at("triggerPolicies").push_back(
        candidate.at("triggerPolicies").at(0));
    expect_rejected(candidate.dump());
}

void check_schema_contract() {
    const auto schema = Json::parse(
        sound_recorder_settings_schema_json());
    const auto& properties = schema.at("properties");
    require(
        schema.at("additionalProperties") == false &&
            properties.size() == 16 &&
            !properties.contains("rawDataSource") &&
            !properties.contains("outputFolder") &&
            properties.at("operationMode").at("enum").size() == 4 &&
            properties.at("bitDepth").at("enum") ==
                Json::array({8, 16, 24, 32}),
        "Sound Recorder root schema contract changed");

    const auto& trigger =
        properties.at("triggerPolicies").at("items");
    require(
        trigger.at("additionalProperties") == false &&
            trigger.at("properties").size() == 12 &&
            trigger.at("required").size() == 12 &&
            !trigger.at("properties").contains("decisionMaker"),
        "Sound Recorder trigger policy schema is incomplete");
}

} // namespace

int main(int argc, char** argv) {
    try {
        require(
            argc == 2,
            "Usage: sound_recorder_settings_check "
            "<settings-defaults.json>");
        const auto fixture = read_json(argv[1]);
        check_java_defaults_and_omissions(fixture);
        check_trigger_policy_field_defaults(fixture);
        check_non_default_round_trip();
        check_strict_rejection();
        check_schema_contract();
        std::cout
            << "Sound Recorder Java defaults, explicit portable "
               "omissions, trigger policy fields, strict v1 settings, "
               "schema, and canonical round-trip passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
