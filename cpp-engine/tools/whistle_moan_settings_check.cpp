#include <algorithm>
#include <any>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <json.hpp>

#include "pamguard/core/BuiltInModules.h"
#include "pamguard/core/ModuleRuntime.h"
#include "pamguard/core/WhistleMoanSettings.h"
#include "pamguard/dsp/SpectrogramEngine.h"
#include "pamguard/project/BuiltInControlledUnits.h"
#include "pamguard/project/ControlledUnitJson.h"
#include "pamguard/project/ProjectProjection.h"

namespace {

using Json = nlohmann::json;
using pamguard::project::ControlledUnitInstance;
using pamguard::project::ControlledUnitRegistry;
using pamguard::project::ProjectDocument;

constexpr auto kAcquisitionId =
    "11111111-1111-4111-8111-111111111111";
constexpr auto kFftId =
    "22222222-2222-4222-8222-222222222222";
constexpr auto kWhistleId =
    "33333333-3333-4333-8333-333333333333";

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

Json read_json(const std::string& path) {
    std::ifstream input(path);
    require(
        static_cast<bool>(input),
        "Could not open Whistle/Moan Java fixture: " + path);
    Json result;
    input >> result;
    return result;
}

template <typename Operation>
void require_rejected(
    Operation operation,
    const std::string& message) {
    bool rejected = false;
    try {
        operation();
    }
    catch (const pamguard::core::WhistleMoanSettingsError&) {
        rejected = true;
    }
    require(rejected, message);
}

bool has_issue(
    const pamguard::project::ProjectProjectionResult& projection,
    const std::string& code,
    const std::string& unit_id) {
    return std::any_of(
        projection.issues.begin(),
        projection.issues.end(),
        [&](const auto& issue) {
            return issue.code == code &&
                issue.unit_id == unit_id;
        });
}

Json configured_settings() {
    auto settings = Json::parse(
        pamguard::core::whistle_moan_default_settings_json());
    settings["channelBitmap"] = 3;
    settings["groupingType"] = "all";
    settings["channelGroups"] = Json::array();
    settings["minFrequencyHz"] = 0.0;
    settings["maxFrequencyHz"] = 0.0;
    settings["connectType"] = 8;
    settings["minLength"] = 2;
    settings["minPixels"] = 2;
    settings["keepShapeStubs"] = true;
    settings["fragmentationMethod"] = 0;
    settings["maxCrossLength"] = 5;
    settings["noiseReduction"]["medianFilter"] = true;
    settings["noiseReduction"]["averageSubtraction"] = true;
    settings["noiseReduction"]["kernelSmoothing"] = false;
    settings["noiseReduction"]["threshold"] = true;
    return settings;
}

void check_java_defaults_and_round_trip(
    const Json& fixture) {
    require(
        fixture.at("authority").at("version") ==
                "2.02.18e" &&
            fixture.at("authority").at("commit") ==
                "dca55c81ef6f1498a8a3b926c69e7182afb915ee" &&
            fixture.at("authority").at("exporter") ==
                "org.pamguard.port.reference."
                "WhistleMoanSettingsFixtureExporter",
        "Whistle/Moan fixture is not pinned to the Java authority");

    const auto default_json =
        pamguard::core::whistle_moan_default_settings_json();
    require(
        Json::parse(default_json) ==
            fixture.at("portableSettingsDefaults"),
        "Portable Whistle/Moan defaults differ from the Java fixture");
    const auto defaults =
        pamguard::core::whistle_moan_settings_from_json(
            default_json,
            1);
    require(
        defaults.channel_bitmap == 0 &&
            defaults.grouping_type ==
                pamguard::core::WhistleSourceGrouping::All &&
            defaults.channel_groups.empty() &&
            defaults.min_frequency_hz == 0.0 &&
            defaults.max_frequency_hz == 0.0 &&
            defaults.connect_type == 8 &&
            defaults.min_length == 10 &&
            defaults.min_pixels == 20 &&
            !defaults.keep_shape_stubs &&
            defaults.fragmentation_method == 3 &&
            defaults.max_cross_length == 5 &&
            !defaults.noise_reduction.run_median_filter &&
            defaults.noise_reduction.median_filter_length == 61 &&
            !defaults.noise_reduction.run_average_subtraction &&
            defaults.noise_reduction.average_update_constant == 0.02 &&
            !defaults.noise_reduction.run_kernel_smoothing &&
            !defaults.noise_reduction.run_threshold &&
            defaults.noise_reduction.threshold_db == 8.0 &&
            defaults.noise_reduction.threshold_final_output == 2 &&
            !pamguard::core::whistle_moan_local_noise_ready(
                defaults),
        "Decoded Whistle/Moan constructor defaults changed");
    require(
        Json::parse(
            pamguard::core::whistle_moan_settings_to_json(
                defaults,
                1)) == Json::parse(default_json),
        "Whistle/Moan defaults do not round-trip");

    auto custom = Json::parse(default_json);
    custom["channelBitmap"] = 7;
    custom["groupingType"] = "user";
    custom["channelGroups"] = Json::array({0, 0, 1});
    custom["minFrequencyHz"] = 2000.0;
    custom["maxFrequencyHz"] = 20000.0;
    custom["connectType"] = 4;
    custom["minLength"] = 12;
    custom["minPixels"] = 25;
    custom["keepShapeStubs"] = true;
    custom["fragmentationMethod"] = 2;
    custom["maxCrossLength"] = 6;
    custom["noiseReduction"]["medianFilter"] = true;
    custom["noiseReduction"]["medianFilterLength"] = 63;
    custom["noiseReduction"]["averageSubtraction"] = true;
    custom["noiseReduction"]["updateConstant"] = 0.1;
    custom["noiseReduction"]["kernelSmoothing"] = true;
    custom["noiseReduction"]["threshold"] = true;
    custom["noiseReduction"]["thresholdDb"] = 9.5;
    custom["noiseReduction"]["finalOutput"] = 1;
    const auto decoded =
        pamguard::core::whistle_moan_settings_from_json(
            custom.dump(),
            1);
    require(
        Json::parse(
            pamguard::core::whistle_moan_settings_to_json(
                decoded,
                1)) == custom &&
            pamguard::core::whistle_moan_local_noise_ready(
                decoded),
        "Custom Whistle/Moan settings changed during round-trip");

    auto expected_contour = custom;
    expected_contour.erase("noiseReduction");
    require(
        Json::parse(
            pamguard::core::
                whistle_moan_noise_runtime_settings_json(
                    decoded)) ==
                custom.at("noiseReduction") &&
            Json::parse(
                pamguard::core::
                    whistle_moan_contour_runtime_settings_json(
                        decoded)) == expected_contour,
        "Whistle/Moan child adapters changed scientific settings");

    const auto schema = Json::parse(
        pamguard::core::whistle_moan_settings_schema_json());
    require(
        !schema.at("additionalProperties").get<bool>() &&
            schema.at("x-pamguard-authority").at("commit") ==
                fixture.at("authority").at("commit") &&
            schema.at("x-pamguard-portable-deviations").is_array() &&
            schema.at("x-pamguard-portable-deviations").size() >= 8,
        "Whistle/Moan schema lost strictness or authority evidence");
}

void check_strict_rejection() {
    const auto defaults = [] {
        return Json::parse(
            pamguard::core::whistle_moan_default_settings_json());
    };
    const auto reject = [&](Json settings,
                            const std::string& message) {
        require_rejected(
            [&] {
                (void) pamguard::core::
                    whistle_moan_settings_from_json(
                        settings.dump(),
                        1);
            },
            message);
    };

    auto settings = defaults();
    settings["unknown"] = true;
    reject(settings, "Whistle/Moan accepted an unknown field");

    settings = defaults();
    settings.erase("connectType");
    reject(settings, "Whistle/Moan accepted a missing field");

    require_rejected(
        [&] {
            (void) pamguard::core::
                whistle_moan_settings_from_json(
                    defaults().dump(),
                    2);
        },
        "Whistle/Moan accepted an unsupported settings version");

    settings = defaults();
    settings["channelBitmap"] = -1;
    reject(settings, "Whistle/Moan accepted a negative bitmap");

    settings = defaults();
    settings["channelBitmap"] = 3;
    settings["groupingType"] = "user";
    settings["channelGroups"] = Json::array({0});
    reject(
        settings,
        "Whistle/Moan accepted incomplete user grouping");

    settings = defaults();
    settings["minFrequencyHz"] = 1000;
    settings["maxFrequencyHz"] = 500;
    reject(settings, "Whistle/Moan accepted reversed frequencies");

    settings = defaults();
    settings["connectType"] = 6;
    reject(settings, "Whistle/Moan accepted invalid connectivity");

    settings = defaults();
    settings["minPixels"] = 0;
    reject(settings, "Whistle/Moan accepted zero minimum pixels");

    settings = defaults();
    settings["fragmentationMethod"] = 4;
    reject(settings, "Whistle/Moan accepted an unknown fragment method");

    settings = defaults();
    settings["noiseReduction"]["medianFilterLength"] = 60;
    reject(settings, "Whistle/Moan accepted an even median length");

    settings = defaults();
    settings["noiseReduction"]["updateConstant"] = 0.6;
    reject(settings, "Whistle/Moan accepted updateConstant above 0.5");

    settings = defaults();
    settings["noiseReduction"]["thresholdDb"] = 0;
    reject(settings, "Whistle/Moan accepted a non-positive threshold");

    settings = defaults();
    settings["noiseReduction"]["finalOutput"] = 3;
    reject(settings, "Whistle/Moan accepted an unknown threshold output");
}

std::vector<pamguard::project::LowLevelTypeContract>
low_level_catalogue(
    const pamguard::core::ModuleRegistry& runtime_types) {
    std::vector<pamguard::project::LowLevelTypeContract> result;
    for (const auto& type : runtime_types.list()) {
        pamguard::project::LowLevelTypeContract converted;
        converted.id = type.id;
        for (const auto& port : type.ports) {
            converted.ports.push_back({
                port.id,
                port.direction ==
                        pamguard::core::PortDirection::Input
                    ? pamguard::project::DataRoleDirection::Input
                    : pamguard::project::DataRoleDirection::Output,
                port.data_type,
                port.capabilities,
            });
        }
        result.push_back(std::move(converted));
    }
    return result;
}

ControlledUnitInstance controlled_unit(
    const ControlledUnitRegistry& registry,
    std::string id,
    const std::string& type_id,
    std::string name) {
    const auto* descriptor =
        registry.find_controlled_unit(type_id);
    require(descriptor, "Controlled-unit descriptor is absent");
    return {
        std::move(id),
        type_id,
        descriptor->descriptor_version,
        {
            descriptor->runtime_recipe.id,
            descriptor->runtime_recipe.version,
        },
        std::move(name),
        descriptor->settings.version,
        descriptor->settings.default_settings_json,
        {},
    };
}

ProjectDocument project_fixture(
    const ControlledUnitRegistry& registry) {
    ProjectDocument project;
    project.project_id =
        "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
    project.metadata = {
        "Whistle and Moan Detector",
        "Java-authoritative process-ownership projection",
    };
    project.descriptor_set = {
        "pamguard-2.02.18e",
        1,
    };
    const auto* array_manager =
        registry.find_global_settings("pamguard.array-manager");
    require(array_manager, "Array Manager descriptor is absent");
    project.global_settings.components.push_back({
        array_manager->id,
        array_manager->settings.version,
        array_manager->settings.default_settings_json,
    });

    auto acquisition = controlled_unit(
        registry,
        kAcquisitionId,
        "pamguard.acquisition",
        "Sound Acquisition");
    auto fft = controlled_unit(
        registry,
        kFftId,
        "pamguard.fft",
        "FFT (Spectrogram) Engine");
    auto whistle = controlled_unit(
        registry,
        kWhistleId,
        "pamguard.whistles-moans",
        "Whistle and Moan Detector");
    fft.bindings.push_back({
        "rawAudio",
        {{kAcquisitionId, "rawAudio"}},
    });
    whistle.bindings.push_back({
        "fft",
        {{kFftId, "fft"}},
    });
    project.controlled_units = {
        std::move(acquisition),
        std::move(fft),
        std::move(whistle),
    };
    return project;
}

void publish_frame(
    const std::shared_ptr<pamguard::core::DataBlock>& block,
    std::size_t channel,
    std::size_t slice,
    bool active) {
    pamguard::dsp::SpectrogramFrame frame;
    frame.channel = channel;
    frame.fft_slice = slice;
    frame.start_sample =
        13000 + static_cast<std::int64_t>(slice * 512);
    frame.time_unix_ms =
        2000 + static_cast<std::int64_t>(slice * 11);
    frame.bins.assign(9, {0.0, 0.0});
    if (active) {
        frame.bins[2] = {10.0, 0.0};
    }
    pamguard::core::DataUnitMetadata metadata;
    metadata.start_sample = frame.start_sample;
    metadata.time_unix_ms = frame.time_unix_ms;
    metadata.channel_bitmap =
        std::uint32_t{1} << channel;
    block->publish(pamguard::core::make_data_unit(
        std::move(metadata),
        std::move(frame)));
}

void check_descriptor_projection_and_runtime() {
    ControlledUnitRegistry controlled;
    pamguard::project::register_builtin_controlled_units(
        controlled);
    pamguard::core::ModuleRegistry runtime_types;
    pamguard::core::register_builtin_module_types(runtime_types);
    const auto compatibility = controlled.validate_against(
        low_level_catalogue(runtime_types));
    std::string compatibility_issues;
    for (const auto& issue : compatibility.issues) {
        if (issue.descriptor_id !=
            "pamguard.whistles-moans") {
            continue;
        }
        compatibility_issues +=
            " [" + issue.code + "/" + issue.descriptor_id +
            "] " + issue.message;
    }
    require(
        compatibility_issues.empty(),
        "Whistle/Moan recipe fails low-level compatibility:" +
            compatibility_issues);

    const auto* descriptor =
        controlled.find_controlled_unit(
            "pamguard.whistles-moans");
    const auto* contour_runtime =
        runtime_types.find("pamguard.whistles-moans");
    require(
        descriptor && contour_runtime,
        "Whistle/Moan controlled or contour runtime type is absent");
    require(
        descriptor->java_authority.registered_name ==
                "Whistle and Moan Detector" &&
            descriptor->java_authority.menu_group ==
                "Detectors" &&
            descriptor->java_authority.class_name ==
                "whistlesAndMoans.WhistleMoanControl" &&
            descriptor->java_authority.help_point ==
                "detectors/whistleMoanHelp/docs/"
                "whistleMoan_Overview.html" &&
            descriptor->availability ==
                pamguard::project::AvailabilityStatus::Available &&
            descriptor->parity_status == "partial",
        "Whistle/Moan Java registration authority changed");
    require(
        descriptor->public_roles.size() == 3 &&
            descriptor->public_roles[0].id == "fft" &&
            descriptor->public_roles[1].id ==
                "noiseReducedFft" &&
            descriptor->public_roles[1].name ==
                "Noise free FFT data" &&
            descriptor->public_roles[2].id == "contours" &&
            descriptor->public_roles[2].name ==
                "Contours" &&
            descriptor->runtime_recipe.children.size() == 2 &&
            descriptor->runtime_recipe.children[0].role_id ==
                "noise-reduction" &&
            descriptor->runtime_recipe.children[0].runtime_type_id ==
                "pamguard.spectrogram-noise" &&
            descriptor->runtime_recipe.children[0]
                    .settings.adapter_id ==
                "pamguard.whistle-noise-settings.v1" &&
            descriptor->runtime_recipe.children[1].role_id ==
                "contour-connect" &&
            descriptor->runtime_recipe.children[1].runtime_type_id ==
                "pamguard.whistles-moans" &&
            descriptor->runtime_recipe.children[1]
                    .settings.adapter_id ==
                "pamguard.whistle-contour-settings.v1" &&
            descriptor->runtime_recipe.internal_edges.size() == 1 &&
            descriptor->runtime_recipe.internal_edges[0].id ==
                "noise-to-contours",
        "Whistle/Moan Java process ownership recipe changed");
    require(
        contour_runtime->ports.size() == 2 &&
            contour_runtime->ports[0].id == "input" &&
            contour_runtime->ports[1].id == "contours" &&
            Json::parse(contour_runtime->default_settings_json) ==
                Json::parse(
                    pamguard::core::
                        whistle_moan_contour_runtime_default_settings_json()) &&
            !pamguard::project::controlled_unit_catalogue_to_json(
                 controlled).empty(),
        "Whistle contour child contract or catalogue entry changed");

    auto project = project_fixture(controlled);
    const auto unconfigured =
        pamguard::project::project_document_to_runtime_graph(
            project,
            controlled,
            runtime_types);
    require(
        unconfigured.editor_valid() &&
            unconfigured.needs_configuration() &&
            has_issue(
                unconfigured,
                "whistle-moan-no-channels",
                kWhistleId) &&
            has_issue(
                unconfigured,
                "whistle-moan-noise-chain",
                kWhistleId),
        "Exact Whistle/Moan defaults are not exposed as needs-configuration");

    const auto settings = configured_settings();
    project.controlled_units[2].settings_json =
        settings.dump();
    const auto projection =
        pamguard::project::project_document_to_runtime_graph(
            project,
            controlled,
            runtime_types);
    std::string issues;
    for (const auto& issue : projection.issues) {
        issues += " [" + issue.code + "] " + issue.message;
    }
    require(
        projection.runnable() &&
            projection.graph.modules.size() == 5 &&
            projection.graph.connections.size() == 4 &&
            projection.index.find_public_output(
                kWhistleId,
                "noiseReducedFft") &&
            projection.index.find_public_output(
                kWhistleId,
                "contours"),
        "Configured Whistle/Moan did not project its two Java processes: " +
            issues);

    const auto* noise_node =
        projection.index.find_runtime_node(
            kWhistleId,
            "noise-reduction");
    const auto* contour_node =
        projection.index.find_runtime_node(
            kWhistleId,
            "contour-connect");
    require(
        noise_node && contour_node,
        "Whistle/Moan runtime child ownership is absent");
    const auto find_module =
        [&](const std::string& id) {
            const auto found = std::find_if(
                projection.graph.modules.begin(),
                projection.graph.modules.end(),
                [&](const auto& module) {
                    return module.id == id;
                });
            require(
                found != projection.graph.modules.end(),
                "Projected Whistle/Moan child is absent");
            return found;
        };
    auto expected_contour = settings;
    expected_contour.erase("noiseReduction");
    require(
        Json::parse(
            find_module(noise_node->runtime_node_id)
                ->settings_json) ==
                settings.at("noiseReduction") &&
            Json::parse(
                find_module(contour_node->runtime_node_id)
                    ->settings_json) == expected_contour,
        "Whistle/Moan projection adapters changed child settings");

    pamguard::core::ModuleRuntime runtime;
    runtime.configure(projection.graph);
    const auto noise_output = runtime.find_block(
        pamguard::core::ModuleRuntime::block_id(
            noise_node->runtime_node_id,
            "output"));
    const auto contour_output = runtime.find_block(
        pamguard::core::ModuleRuntime::block_id(
            contour_node->runtime_node_id,
            "contours"));
    require(
        noise_output && contour_output &&
            noise_output->descriptor().channel_bitmap == 3 &&
            contour_output->descriptor().channel_bitmap == 3 &&
            noise_output->descriptor().fft_length == 1024 &&
            noise_output->descriptor().fft_hop == 512,
        "Whistle/Moan outputs lost FFT geometry or channel selection");

    std::vector<pamguard::detectors::ConnectedRegionResult>
        contours;
    std::vector<std::uint32_t> contour_bitmaps;
    auto subscription = contour_output->subscribe(
        [&](const pamguard::core::DataUnit& unit) {
            contours.push_back(
                std::any_cast<
                    pamguard::detectors::ConnectedRegionResult>(
                    unit.payload));
            contour_bitmaps.push_back(
                unit.metadata.channel_bitmap);
        });
    runtime.start();

    // Java creates one ShapeConnector per group and drives it only from the
    // group's lowest channel. Channel 1 must not create another contour.
    publish_frame(noise_output, 1, 0, true);
    publish_frame(noise_output, 1, 1, true);
    publish_frame(noise_output, 1, 2, false);
    require(
        contours.empty(),
        "Non-leading group channel drove a Whistle contour");

    publish_frame(noise_output, 0, 0, true);
    publish_frame(noise_output, 0, 1, true);
    publish_frame(noise_output, 0, 2, false);
    require(
        contours.size() == 1 &&
            contours.front().channel == 0 &&
            contour_bitmaps.front() == 3,
        "Grouped Whistle contour did not retain the full group bitmap");

    // The portable graph finalizes partial scientific results on stop. This
    // intentionally differs from Java pamStop and is recorded in the schema.
    publish_frame(noise_output, 0, 3, true);
    publish_frame(noise_output, 0, 4, true);
    runtime.stop();
    require(
        contours.size() == 2 &&
            contour_bitmaps.back() == 3,
        "Portable stop finalization lost a pending Whistle contour");
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error(
                "Usage: whistle_moan_settings_check "
                "<java-defaults-fixture.json>");
        }
        const auto fixture = read_json(argv[1]);
        check_java_defaults_and_round_trip(fixture);
        check_strict_rejection();
        check_descriptor_projection_and_runtime();
        std::cout
            << "Whistle/Moan Java defaults, strict settings, grouped "
               "process ownership, and runtime wiring validated\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
