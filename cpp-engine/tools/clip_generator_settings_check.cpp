#include "pamguard/core/ClipGeneratorSettings.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <json.hpp>

namespace {

using Json = nlohmann::json;
using pamguard::core::ClipGeneratorChannelSelection;
using pamguard::core::ClipGeneratorSettings;
using pamguard::core::ClipGeneratorSettingsError;
using pamguard::core::ClipGeneratorStorageMode;
using pamguard::core::ClipGeneratorTriggerPolicySettings;
using pamguard::core::clip_generator_default_settings_json;
using pamguard::core::clip_generator_settings_from_json;
using pamguard::core::clip_generator_settings_schema_json;
using pamguard::core::clip_generator_settings_to_json;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string read_text(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error(
            "Could not open Clip Generator fixture '" +
            path + "'");
    }
    std::ostringstream output;
    output << input.rdbuf();
    return output.str();
}

Json read_json(const std::string& path) {
    return Json::parse(read_text(path));
}

Json default_policy_fixture(const Json& fixture) {
    auto settings =
        fixture.at("portableSettingsDefaults");
    settings.at("triggerPolicies").push_back(
        fixture.at("triggerPolicyFieldDefaults"));
    return settings;
}

void check_java_defaults(const Json& fixture) {
    const auto& authority = fixture.at("authority");
    require(
        authority.at("version") == "2.02.18e" &&
            authority.at("commit") ==
                "dca55c81ef6f1498a8a3b926c69e7182afb915ee" &&
            authority.at("exporter") ==
                "clipgenerator."
                "ClipGeneratorSettingsFixtureExporter" &&
            authority.at("settingsClass") ==
                "clipgenerator.ClipSettings" &&
            authority.at("triggerPolicyClass") ==
                "clipgenerator.ClipGenSetting",
        "Clip Generator fixture authority changed");

    const auto canonical_default = Json::parse(
        clip_generator_default_settings_json());
    require(
        canonical_default ==
            fixture.at("portableSettingsDefaults"),
        "Clip Generator portable defaults diverged from Java");

    const auto settings =
        clip_generator_settings_from_json(
            canonical_default.dump(), 1);
    require(
        settings.storage_mode ==
                ClipGeneratorStorageMode::Binary &&
            settings.dated_subfolders &&
            settings.trigger_policies.empty(),
        "Clip Generator C++ constructor defaults are incomplete");
    require(
        clip_generator_settings_to_json(settings) ==
            clip_generator_default_settings_json(),
        "Clip Generator default settings did not round-trip canonically");

    const auto& constants =
        fixture.at("javaStoredConstants");
    require(
        constants.at("storage").at("wavFiles") ==
                static_cast<std::uint32_t>(
                    ClipGeneratorStorageMode::WavFiles) &&
            constants.at("storage").at("binary") ==
                static_cast<std::uint32_t>(
                    ClipGeneratorStorageMode::Binary) &&
            constants.at("storage").at("annotation") == 2 &&
            constants.at("storage").at("both") ==
                static_cast<std::uint32_t>(
                    ClipGeneratorStorageMode::Both) &&
            constants.at("channelSelection").at(
                "detectionChannelsOnly") ==
                static_cast<std::uint32_t>(
                    ClipGeneratorChannelSelection::
                        DetectionChannelsOnly) &&
            constants.at("channelSelection").at(
                "firstDetectionChannelOnly") ==
                static_cast<std::uint32_t>(
                    ClipGeneratorChannelSelection::
                        FirstDetectionChannelOnly) &&
            constants.at("channelSelection").at(
                "allChannels") ==
                static_cast<std::uint32_t>(
                    ClipGeneratorChannelSelection::
                        AllChannels),
        "Clip Generator Java stored constants changed");

    const auto policy_json =
        default_policy_fixture(fixture);
    const auto with_policy =
        clip_generator_settings_from_json(
            policy_json.dump(), 1);
    require(
        with_policy.trigger_policies.size() == 1,
        "Clip Generator trigger policy fixture was not imported");
    const auto& policy =
        with_policy.trigger_policies.front();
    require(
        policy.trigger_source.unit_id ==
                "fixture-trigger-unit" &&
            policy.trigger_source.output_role ==
                "detections" &&
            policy.enabled &&
            policy.seconds_before_trigger == 0.0 &&
            policy.seconds_after_trigger == 0.0 &&
            policy.channel_selection ==
                ClipGeneratorChannelSelection::
                    DetectionChannelsOnly &&
            !policy.clip_prefix &&
            policy.use_data_budget &&
            policy.data_budget_kilobytes == 10 * 1024 &&
            policy.budget_period_hours == 24.0,
        "Clip Generator trigger policy defaults diverged from Java");
    require(
        Json::parse(
            clip_generator_settings_to_json(
                with_policy)) == policy_json,
        "Clip Generator Java-default trigger policy did not "
        "round-trip canonically");
}

void check_explicit_boundaries(const Json& fixture) {
    const auto& eligibility =
        fixture.at("sourceEligibilityBoundary");
    require(
        eligibility.at("receiverOwnedPolicies") == true &&
            eligibility.at("requiresCanClipGenerate") == true &&
            eligibility.at("clickDetectorOutputEligible") == false &&
            eligibility.at("spectrogramMarksEligible") == true &&
            eligibility.at("spectrogramMarkUsesDataBudget") == false,
        "Clip Generator source-eligibility boundary changed");

    const auto& omissions =
        fixture.at("portableOmissions");
    require(
        omissions.size() == 6 &&
            omissions.contains("dataSourceName") &&
            omissions.contains("outputFolder") &&
            omissions.contains("compressorIndex") &&
            omissions.contains("mapLineLength") &&
            omissions.contains("hadMapLine") &&
            omissions.contains("dataName"),
        "Clip Generator portable omissions are incomplete");

    const auto& excluded =
        fixture.at("excludedJavaFieldDefaults");
    require(
        excluded.at("dataSourceName").is_null() &&
            excluded.at("outputFolder").is_null() &&
            excluded.at("compressorIndex") == 0 &&
            excluded.at("freshTriggerCount") == 0 &&
            excluded.at("triggerDataName") ==
                "__fixture_trigger__" &&
            excluded.at("mapLineLengthMetres").is_null() &&
            excluded.at("hadMapLine") == false &&
            excluded.at("clonedMapLineLengthMetres") == 1000.0 &&
            excluded.at("clonedHadMapLine") == true,
        "Clip Generator excluded Java defaults or clone migration changed");

    require(
        fixture.at("portableSettingsDefaults")
            .at("triggerPolicies").empty(),
        "A source-specific policy, including a Click Detector policy, "
        "was incorrectly synthesized into fresh defaults");
}

void check_non_default_round_trip() {
    ClipGeneratorSettings settings;
    settings.storage_mode =
        ClipGeneratorStorageMode::Both;
    settings.dated_subfolders = false;

    ClipGeneratorTriggerPolicySettings first;
    first.trigger_source = {
        "ishmael-detector-1",
        "detections",
    };
    first.enabled = false;
    first.seconds_before_trigger = 1.25;
    first.seconds_after_trigger = 2.5;
    first.channel_selection =
        ClipGeneratorChannelSelection::AllChannels;
    first.clip_prefix = "ISH_";
    first.use_data_budget = false;
    first.data_budget_kilobytes = 0;
    first.budget_period_hours = 0.5;
    settings.trigger_policies.push_back(first);

    ClipGeneratorTriggerPolicySettings second;
    second.trigger_source = {
        "whistle-detector:2",
        "whistles",
    };
    second.seconds_before_trigger = 0.125;
    second.seconds_after_trigger = 0.75;
    second.channel_selection =
        ClipGeneratorChannelSelection::
            FirstDetectionChannelOnly;
    second.clip_prefix = "";
    second.data_budget_kilobytes = 4096;
    second.budget_period_hours = 12.0;
    settings.trigger_policies.push_back(second);

    const auto encoded =
        clip_generator_settings_to_json(settings, 1);
    const auto restored =
        clip_generator_settings_from_json(encoded, 1);
    require(
        restored == settings,
        "Non-default Clip Generator settings did not round-trip");
    require(
        clip_generator_settings_to_json(restored, 1) ==
            encoded,
        "Clip Generator serialization is not canonical and stable");

    const auto value = Json::parse(encoded);
    require(
        value.at("triggerPolicies").size() == 2 &&
            value.at("triggerPolicies").at(0).at(
                "channelSelection") == "all-channels" &&
            value.at("triggerPolicies").at(1).at(
                "channelSelection") ==
                "first-detection-channel-only" &&
            value.at("triggerPolicies").at(0).at(
                "dataBudgetKilobytes") == 0,
        "Clip Generator canonical JSON lost non-default values");
}

void require_rejected(
    const Json& candidate,
    const char* message) {
    bool rejected = false;
    try {
        (void) clip_generator_settings_from_json(
            candidate.dump(), 1);
    }
    catch (const ClipGeneratorSettingsError&) {
        rejected = true;
    }
    require(rejected, message);
}

void check_strict_rejection(const Json& fixture) {
    const auto defaults =
        fixture.at("portableSettingsDefaults");
    const auto with_policy =
        default_policy_fixture(fixture);

    bool rejected = false;
    try {
        (void) clip_generator_settings_from_json(
            defaults.dump(), 2);
    }
    catch (const ClipGeneratorSettingsError&) {
        rejected = true;
    }
    require(
        rejected,
        "Unsupported Clip Generator settings version was accepted");

    rejected = false;
    try {
        (void) clip_generator_settings_from_json(
            "{not-json", 1);
    }
    catch (const ClipGeneratorSettingsError&) {
        rejected = true;
    }
    require(
        rejected,
        "Malformed Clip Generator JSON was accepted");

    std::vector<Json> invalid;
    auto value = defaults;
    value["extra"] = 1;
    invalid.push_back(value);
    value = defaults;
    value.erase("storageMode");
    invalid.push_back(value);
    value = defaults;
    value["storageMode"] = "annotation";
    invalid.push_back(value);
    value = defaults;
    value["storageMode"] = 1;
    invalid.push_back(value);
    value = defaults;
    value["datedSubFolders"] = 1;
    invalid.push_back(value);
    value = defaults;
    value["triggerPolicies"] = Json::object();
    invalid.push_back(value);

    value = with_policy;
    value["triggerPolicies"][0]["extra"] = 1;
    invalid.push_back(value);
    value = with_policy;
    value["triggerPolicies"][0]["triggerSource"]
         ["unitId"] = "-bad";
    invalid.push_back(value);
    value = with_policy;
    value["triggerPolicies"][0]["triggerSource"]
         ["outputRole"] = "Detections";
    invalid.push_back(value);
    value = with_policy;
    value["triggerPolicies"][0]["triggerSource"]
         ["extra"] = 1;
    invalid.push_back(value);
    value = with_policy;
    value["triggerPolicies"][0]["enabled"] = 1;
    invalid.push_back(value);
    value = with_policy;
    value["triggerPolicies"][0]["secondsBeforeTrigger"] =
        -0.01;
    invalid.push_back(value);
    value = with_policy;
    value["triggerPolicies"][0]["secondsAfterTrigger"] =
        "0";
    invalid.push_back(value);
    value = with_policy;
    value["triggerPolicies"][0]["channelSelection"] =
        "first-channel";
    invalid.push_back(value);
    value = with_policy;
    value["triggerPolicies"][0]["clipPrefix"] = 7;
    invalid.push_back(value);
    value = with_policy;
    value["triggerPolicies"][0]["clipPrefix"] =
        std::string(257, 'x');
    invalid.push_back(value);
    value = with_policy;
    value["triggerPolicies"][0]["useDataBudget"] = "true";
    invalid.push_back(value);
    value = with_policy;
    value["triggerPolicies"][0]["dataBudgetKilobytes"] =
        -1;
    invalid.push_back(value);
    value = with_policy;
    value["triggerPolicies"][0]["dataBudgetKilobytes"] =
        std::uint64_t{2147483648ULL};
    invalid.push_back(value);
    value = with_policy;
    value["triggerPolicies"][0]["budgetPeriodHours"] =
        0.0;
    invalid.push_back(value);
    value = with_policy;
    value["triggerPolicies"].push_back(
        value["triggerPolicies"][0]);
    invalid.push_back(value);
    value = defaults;
    value["triggerPolicies"] = Json::array();
    for (std::size_t index = 0; index < 1025; ++index) {
        value["triggerPolicies"].push_back(
            fixture.at("triggerPolicyFieldDefaults"));
    }
    invalid.push_back(std::move(value));

    for (const auto& candidate : invalid) {
        require_rejected(
            candidate,
            "Invalid Clip Generator settings were accepted");
    }

    ClipGeneratorSettings invalid_direct;
    invalid_direct.storage_mode =
        static_cast<ClipGeneratorStorageMode>(2);
    rejected = false;
    try {
        (void) clip_generator_settings_to_json(
            invalid_direct, 1);
    }
    catch (const ClipGeneratorSettingsError&) {
        rejected = true;
    }
    require(
        rejected,
        "Direct Java annotation storage mode was serialized");

    invalid_direct = ClipGeneratorSettings{};
    invalid_direct.trigger_policies.push_back(
        ClipGeneratorTriggerPolicySettings{});
    rejected = false;
    try {
        (void) clip_generator_settings_to_json(
            invalid_direct, 1);
    }
    catch (const ClipGeneratorSettingsError&) {
        rejected = true;
    }
    require(
        rejected,
        "Direct trigger policy without a stable source was serialized");
}

void check_schema_contract() {
    const auto schema =
        Json::parse(
            clip_generator_settings_schema_json());
    const auto& authority =
        schema.at("x-pamguard-authority");
    require(
        authority.at("version") == "2.02.18e" &&
            authority.at("commit") ==
                "dca55c81ef6f1498a8a3b926c69e7182afb915ee" &&
            authority.at("settingsClasses").size() == 2 &&
            authority.at("dialogClasses").size() == 2 &&
            authority.at("processClasses").size() == 3,
        "Clip Generator schema authority is incomplete");

    require(
        schema.at("additionalProperties") == false &&
            schema.at("properties").size() == 3 &&
            schema.at("required").size() == 3 &&
            schema.at("x-pamguard-portable-omissions").size() ==
                5 &&
            schema.at("x-pamguard-portable-boundaries").size() ==
                9 &&
            schema.at("x-pamguardConstraints").size() == 3,
        "Clip Generator top-level schema is incomplete");

    const auto& policies =
        schema.at("properties").at("triggerPolicies");
    const auto& policy = policies.at("items");
    const auto& source =
        policy.at("properties").at("triggerSource");
    require(
        policies.at("maxItems") == 1024 &&
            policy.at("additionalProperties") == false &&
            policy.at("properties").size() == 9 &&
            policy.at("required").size() == 9 &&
            source.at("additionalProperties") == false &&
            source.at("properties").size() == 2 &&
            source.at("required").size() == 2,
        "Clip Generator trigger policy schema is incomplete");

    const auto& storage =
        schema.at("properties").at("storageMode").at("enum");
    require(
        storage.size() == 3 &&
            storage.at(0) == "wav-files" &&
            storage.at(1) == "binary" &&
            storage.at(2) == "both",
        "Clip Generator executable storage schema changed");
}

} // namespace

int main(int argc, char** argv) {
    try {
        require(
            argc == 2,
            "Usage: clip_generator_settings_check "
            "<settings-defaults.json>");
        const auto fixture = read_json(argv[1]);
        check_java_defaults(fixture);
        check_explicit_boundaries(fixture);
        check_non_default_round_trip();
        check_strict_rejection(fixture);
        check_schema_contract();
        std::cout
            << "Clip Generator Java defaults, receiver-owned "
               "per-source policies, source eligibility, explicit "
               "portable boundaries, strict v1 settings, schema, "
               "and canonical round-trip passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
