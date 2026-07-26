#include <algorithm>
#include <any>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <json.hpp>

#include "pamguard/core/BuiltInModules.h"
#include "pamguard/core/FftDetectorNodes.h"
#include "pamguard/core/IshmaelSettings.h"
#include "pamguard/core/ModuleRuntime.h"
#include "pamguard/core/SignalNodes.h"
#include "pamguard/project/BuiltInControlledUnits.h"
#include "pamguard/project/ControlledUnitJson.h"
#include "pamguard/project/ProjectProjection.h"

namespace {

using Json = nlohmann::json;
using pamguard::core::DataBlock;
using pamguard::core::DataBlockDescriptor;
using pamguard::core::DataUnitMetadata;
using pamguard::project::ControlledUnitInstance;
using pamguard::project::ControlledUnitRegistry;
using pamguard::project::ProjectDocument;

constexpr auto kAcquisitionId =
    "11111111-1111-4111-8111-111111111111";
constexpr auto kFftId =
    "22222222-2222-4222-8222-222222222222";
constexpr auto kEnergyId =
    "33333333-3333-4333-8333-333333333333";
constexpr auto kSgramId =
    "44444444-4444-4444-8444-444444444444";
constexpr auto kMatchId =
    "55555555-5555-4555-8555-555555555555";

void require(
    bool condition,
    const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

Json read_json(const std::string& path) {
    std::ifstream input(path);
    require(
        static_cast<bool>(input),
        "Could not open Java Ishmael settings fixture: " +
            path);
    return Json::parse(input);
}

template <typename Operation>
void require_settings_rejected(
    Operation operation,
    const std::string& message) {
    bool rejected = false;
    try {
        operation();
    }
    catch (const pamguard::core::IshmaelSettingsError&) {
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

void check_defaults_and_strict_settings(
    const Json& fixture) {
    const auto& expected =
        fixture.at("portableSettingsDefaults");
    const auto energy_default = Json::parse(
        pamguard::core::
            ishmael_energy_sum_default_settings_json());
    const auto sgram_default = Json::parse(
        pamguard::core::
            ishmael_sgram_corr_default_settings_json());
    const auto match_default = Json::parse(
        pamguard::core::
            ishmael_match_filter_default_settings_json());
    require(
        energy_default == expected.at("energySum") &&
            sgram_default ==
                expected.at("spectrogramCorrelation") &&
            match_default ==
                expected.at("matchedFilter"),
        "Portable Ishmael defaults differ from the pinned Java exporter");

    const auto energy =
        pamguard::core::
            ishmael_energy_sum_settings_from_json(
                energy_default.dump(),
                1);
    const auto sgram =
        pamguard::core::
            ishmael_sgram_corr_settings_from_json(
                sgram_default.dump(),
                1);
    const auto match =
        pamguard::core::
            ishmael_match_filter_settings_from_json(
                match_default.dump(),
                1);
    require(
        Json::parse(
            pamguard::core::
                ishmael_energy_sum_settings_to_json(
                    energy,
                    1)) == energy_default &&
            Json::parse(
                pamguard::core::
                    ishmael_sgram_corr_settings_to_json(
                        sgram,
                        1)) == sgram_default &&
            Json::parse(
                pamguard::core::
                    ishmael_match_filter_settings_to_json(
                        match,
                        1)) == match_default,
        "Ishmael defaults do not round-trip canonically");
    require(
        !pamguard::core::ishmael_energy_sum_ready(
            energy) &&
            !pamguard::core::ishmael_sgram_corr_ready(
                sgram) &&
            !pamguard::core::
                ishmael_match_filter_ready(match),
        "Bare Java Ishmael defaults unexpectedly became runnable");

    auto malformed = energy_default;
    malformed["unknown"] = true;
    require_settings_rejected(
        [&] {
            (void) pamguard::core::
                ishmael_energy_sum_settings_from_json(
                    malformed.dump(),
                    1);
        },
        "Energy Sum accepted an unknown field");
    malformed = energy_default;
    malformed["useRatio"] = true;
    malformed["adaptiveThreshold"] = true;
    require_settings_rejected(
        [&] {
            (void) pamguard::core::
                ishmael_energy_sum_settings_from_json(
                    malformed.dump(),
                    1);
        },
        "Energy Sum accepted ratio and adaptive modes together");
    malformed = energy_default;
    malformed["f0Hz"] = 2000;
    malformed["f1Hz"] = 1000;
    require_settings_rejected(
        [&] {
            (void) pamguard::core::
                ishmael_energy_sum_settings_from_json(
                    malformed.dump(),
                    1);
        },
        "Energy Sum accepted a reversed frequency band");

    auto malformed_sgram = sgram_default;
    malformed_sgram["segments"] =
        Json::array({Json::array({1, 100, 0, 200})});
    require_settings_rejected(
        [&] {
            (void) pamguard::core::
                ishmael_sgram_corr_settings_from_json(
                    malformed_sgram.dump(),
                    1);
        },
        "Spectrogram Correlation accepted a reversed segment");
    malformed_sgram = sgram_default;
    malformed_sgram["spreadHz"] = 0;
    require_settings_rejected(
        [&] {
            (void) pamguard::core::
                ishmael_sgram_corr_settings_from_json(
                    malformed_sgram.dump(),
                    1);
        },
        "Spectrogram Correlation accepted zero spread");

    auto malformed_match = match_default;
    malformed_match["kernelFilenameList"] =
        Json::array({"C:\\host\\kernel.wav"});
    require_settings_rejected(
        [&] {
            (void) pamguard::core::
                ishmael_match_filter_settings_from_json(
                    malformed_match.dump(),
                    1);
        },
        "Matched Filter accepted a host-specific absolute path");
    malformed_match = match_default;
    malformed_match["groupingType"] = "user";
    malformed_match["channelBitmap"] = 3;
    malformed_match["channelGroups"] =
        Json::array({7});
    require_settings_rejected(
        [&] {
            (void) pamguard::core::
                ishmael_match_filter_settings_from_json(
                    malformed_match.dump(),
                    1);
        },
        "Matched Filter accepted an incomplete user grouping");

    require(
        Json::parse(
            pamguard::core::
                ishmael_energy_sum_settings_schema_json())
                .at("x-pamguard-authority")
                .at("commit") ==
            fixture.at("authority").at("commit") &&
            Json::parse(
                pamguard::core::
                    ishmael_sgram_corr_settings_schema_json())
                .at("x-pamguard-authority")
                .at("commit") ==
            fixture.at("authority").at("commit") &&
            Json::parse(
                pamguard::core::
                    ishmael_match_filter_settings_schema_json())
                .at("x-pamguard-authority")
                .at("commit") ==
            fixture.at("authority").at("commit"),
        "Ishmael schemas lost their pinned Java authority");
}

void check_group_semantics() {
    pamguard::core::IshmaelGroupedSourceSettings source;
    source.channel_bitmap = 0b10110;
    source.grouping_type =
        pamguard::core::IshmaelSourceGrouping::Singles;
    require(
        pamguard::core::ishmael_active_channel_bitmap(
            source) == 0b10110,
        "GROUP_SINGLES did not retain every selected channel");

    source.grouping_type =
        pamguard::core::IshmaelSourceGrouping::All;
    require(
        pamguard::core::ishmael_active_channel_bitmap(
            source) == 0b00010,
        "GROUP_ALL did not choose the lowest selected channel");

    source.channel_bitmap = 0b11110;
    source.grouping_type =
        pamguard::core::IshmaelSourceGrouping::User;
    source.channel_groups = {0, 7, 4, 7, 1};
    require(
        pamguard::core::ishmael_active_channel_bitmap(
            source) == 0b10110,
        "Sparse Java group IDs were not normalized to first channels");

    source = {};
    require(
        pamguard::core::ishmael_match_filter_channels(
            source) ==
            std::vector<std::size_t>{0},
        "Matched Filter lost Java's no-group channel-zero fallback");
}

ControlledUnitInstance controlled_unit(
    const ControlledUnitRegistry& registry,
    std::string id,
    const std::string& type_id,
    std::string name) {
    const auto* descriptor =
        registry.find_controlled_unit(type_id);
    require(
        descriptor != nullptr,
        "Controlled-unit descriptor is absent: " + type_id);
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
        "Ishmael controlled units",
        "Java-authoritative projection fixture",
    };
    project.descriptor_set = {
        "pamguard-2.02.18e",
        1,
    };
    const auto* array_manager =
        registry.find_global_settings(
            "pamguard.array-manager");
    require(
        array_manager != nullptr,
        "Array Manager descriptor is absent");
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
        "FFT Engine");
    auto energy = controlled_unit(
        registry,
        kEnergyId,
        "pamguard.ishmael-energy-sum",
        "Energy Sum");
    auto sgram = controlled_unit(
        registry,
        kSgramId,
        "pamguard.ishmael-sgram-corr",
        "Spectrogram Correlation");
    auto match = controlled_unit(
        registry,
        kMatchId,
        "pamguard.ishmael-match-filter",
        "Matched Filter");
    fft.bindings.push_back({
        "rawAudio",
        {{kAcquisitionId, "rawAudio"}},
    });
    energy.bindings.push_back({
        "fft",
        {{kFftId, "noiseReducedFft"}},
    });
    sgram.bindings.push_back({
        "fft",
        {{kFftId, "fft"}},
    });
    match.bindings.push_back({
        "rawAudio",
        {{kAcquisitionId, "rawAudio"}},
    });
    project.controlled_units = {
        std::move(acquisition),
        std::move(fft),
        std::move(energy),
        std::move(sgram),
        std::move(match),
    };
    return project;
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

const pamguard::core::ModuleInstance& find_module(
    const pamguard::core::ModuleGraphDocument& graph,
    const std::string& id) {
    const auto found = std::find_if(
        graph.modules.begin(),
        graph.modules.end(),
        [&](const auto& module) {
            return module.id == id;
        });
    require(
        found != graph.modules.end(),
        "Projected runtime module is absent: " + id);
    return *found;
}

void check_descriptors_projection_and_output_maps() {
    ControlledUnitRegistry controlled;
    pamguard::project::register_builtin_controlled_units(
        controlled);
    pamguard::core::ModuleRegistry runtime_types;
    pamguard::core::register_builtin_module_types(
        runtime_types);

    const auto* energy = controlled.find_controlled_unit(
        "pamguard.ishmael-energy-sum");
    const auto* sgram = controlled.find_controlled_unit(
        "pamguard.ishmael-sgram-corr");
    const auto* match = controlled.find_controlled_unit(
        "pamguard.ishmael-match-filter");
    require(
        energy && sgram && match,
        "One or more Ishmael controlled-unit descriptors are absent");
    require(
        energy->java_authority.registered_name ==
                "Ishmael energy sum" &&
            sgram->java_authority.registered_name ==
                "Ishmael spectrogram correlation" &&
            match->java_authority.registered_name ==
                "Ishmael matched filtering" &&
            energy->runtime_recipe.children.at(0)
                    .settings.adapter_id ==
                "pamguard.ishmael-energy-sum-settings.v1" &&
            sgram->runtime_recipe.children.at(0)
                    .settings.adapter_id ==
                "pamguard.ishmael-sgram-corr-settings.v1" &&
            match->runtime_recipe.children.at(0)
                    .settings.adapter_id ==
                "pamguard.ishmael-match-filter-settings.v1",
        "Ishmael registrations or pure adapter IDs changed");
    require(
        energy->public_roles.at(0).id == "fft" &&
            sgram->public_roles.at(0).id == "fft" &&
            match->public_roles.at(0).id == "rawAudio" &&
            energy->public_roles.at(1).id ==
                "detectionFunction" &&
            energy->public_roles.at(2).id ==
                "detections",
        "Ishmael source/function/detection roles changed");
    const auto compatibility =
        controlled.validate_against(
            low_level_catalogue(runtime_types));
    std::string registry_issues;
    for (const auto& issue : compatibility.issues) {
        registry_issues +=
            " [" + issue.code + "] " + issue.message;
    }
    require(
        compatibility.valid(),
        "Ishmael recipes fail low-level compatibility:" +
            registry_issues);
    require(
        !pamguard::project::
            controlled_unit_catalogue_to_json(
                controlled).empty(),
        "Controlled-unit catalogue omitted Ishmael");

    auto project = project_fixture(controlled);
    const auto defaults =
        pamguard::project::
            project_document_to_runtime_graph(
                project,
                controlled,
                runtime_types);
    require(
        defaults.editor_valid() &&
            defaults.needs_configuration() &&
            has_issue(
                defaults,
                "ishmael-energy-no-channels",
                kEnergyId) &&
            has_issue(
                defaults,
                "ishmael-sgram-no-segments",
                kSgramId) &&
            has_issue(
                defaults,
                "ishmael-match-no-kernel",
                kMatchId),
        "Bare Java Ishmael defaults were not projected as needs-configuration");

    auto energy_settings = Json::parse(
        project.controlled_units[2].settings_json);
    energy_settings["channelBitmap"] = 3;
    energy_settings["groupingType"] = "all";
    project.controlled_units[2].settings_json =
        energy_settings.dump();

    auto sgram_settings = Json::parse(
        project.controlled_units[3].settings_json);
    sgram_settings["channelBitmap"] = 3;
    sgram_settings["groupingType"] = "singles";
    sgram_settings["segments"] =
        Json::array({
            Json::array({0.0, 100.0, 0.03, 200.0}),
        });
    project.controlled_units[3].settings_json =
        sgram_settings.dump();

    auto match_settings = Json::parse(
        project.controlled_units[4].settings_json);
    match_settings["channelBitmap"] = 3;
    match_settings["groupingType"] = "user";
    match_settings["channelGroups"] =
        Json::array({7, 7});
    match_settings["kernelFilenameList"] =
        Json::array({"kernel.wav"});
    match_settings["kernelSamples"] =
        Json::array({1.0, 0.5, -0.25, 0.0});
    project.controlled_units[4].settings_json =
        match_settings.dump();

    const auto projection =
        pamguard::project::
            project_document_to_runtime_graph(
                project,
                controlled,
                runtime_types);
    std::string issues;
    for (const auto& issue : projection.issues) {
        issues +=
            " [" + issue.code + "] " + issue.message;
    }
    require(
        projection.runnable(),
        "Configured Ishmael project is not runnable:" + issues);

    const auto* energy_node =
        projection.index.find_runtime_node(
            kEnergyId,
            "detector");
    const auto* sgram_node =
        projection.index.find_runtime_node(
            kSgramId,
            "detector");
    const auto* match_node =
        projection.index.find_runtime_node(
            kMatchId,
            "detector");
    require(
        energy_node && sgram_node && match_node,
        "Ishmael runtime child ownership is absent");
    const auto energy_runtime = Json::parse(
        find_module(
            projection.graph,
            energy_node->runtime_node_id)
            .settings_json);
    const auto sgram_runtime = Json::parse(
        find_module(
            projection.graph,
            sgram_node->runtime_node_id)
            .settings_json);
    const auto match_runtime = Json::parse(
        find_module(
            projection.graph,
            match_node->runtime_node_id)
            .settings_json);
    require(
        energy_runtime.at("fftLength") == 1024 &&
            energy_runtime.at("fftHop") == 512 &&
            sgram_runtime.at("fftLength") == 1024 &&
            sgram_runtime.at("fftHop") == 512,
        "Ishmael FFT adapters did not derive geometry from the selected source");
    require(
        energy_runtime.at("channelBitmap") == 3 &&
            energy_runtime.at("activeChannelBitmap") == 1 &&
            sgram_runtime.at("channelBitmap") == 3 &&
            sgram_runtime.at("activeChannelBitmap") == 3 &&
            match_runtime.at("channels") ==
                Json::array({0}),
        "Ishmael adapters changed Java group/channel semantics");

    pamguard::core::ModuleRuntime runtime;
    runtime.configure(projection.graph);
    const auto energy_function = runtime.find_block(
        pamguard::core::ModuleRuntime::block_id(
            energy_node->runtime_node_id,
            "function"));
    const auto energy_detections = runtime.find_block(
        pamguard::core::ModuleRuntime::block_id(
            energy_node->runtime_node_id,
            "detections"));
    const auto sgram_function = runtime.find_block(
        pamguard::core::ModuleRuntime::block_id(
            sgram_node->runtime_node_id,
            "function"));
    const auto sgram_detections = runtime.find_block(
        pamguard::core::ModuleRuntime::block_id(
            sgram_node->runtime_node_id,
            "detections"));
    const auto match_function = runtime.find_block(
        pamguard::core::ModuleRuntime::block_id(
            match_node->runtime_node_id,
            "function"));
    const auto match_detections = runtime.find_block(
        pamguard::core::ModuleRuntime::block_id(
            match_node->runtime_node_id,
            "detections"));
    require(
        energy_function && energy_detections &&
            sgram_function && sgram_detections &&
            match_function && match_detections,
        "Ishmael runtime outputs are absent");
    require(
        energy_function->descriptor().channel_bitmap == 3 &&
            energy_detections->descriptor().channel_bitmap == 1 &&
            sgram_function->descriptor().channel_bitmap == 3 &&
            sgram_detections->descriptor().channel_bitmap == 3 &&
            match_function->descriptor().channel_bitmap == 1 &&
            match_detections->descriptor().channel_bitmap == 1,
        "Ishmael output channel maps do not distinguish function and group-leading detections");
}

std::shared_ptr<DataBlock> block(
    std::string id,
    std::string type,
    double sample_rate_hz,
    std::uint32_t channel_bitmap,
    std::optional<std::size_t> fft_length = std::nullopt,
    std::optional<std::size_t> fft_hop = std::nullopt) {
    DataBlockDescriptor descriptor;
    descriptor.id = std::move(id);
    descriptor.name = descriptor.id;
    descriptor.data_type = std::move(type);
    descriptor.sample_rate_hz = sample_rate_hz;
    descriptor.channel_bitmap = channel_bitmap;
    descriptor.fft_length = fft_length;
    descriptor.fft_hop = fft_hop;
    descriptor.clock_domain_id = "ishmael-lifecycle";
    return std::make_shared<DataBlock>(
        std::move(descriptor));
}

void publish_fft(
    const std::shared_ptr<DataBlock>& input,
    std::size_t channel,
    std::int64_t start_sample,
    double magnitude_squared,
    std::size_t fft_length = 8) {
    pamguard::dsp::SpectrogramFrame frame;
    frame.channel = channel;
    frame.start_sample = start_sample;
    frame.time_unix_ms =
        100000 + static_cast<std::int64_t>(std::llround(
            static_cast<double>(start_sample) * 1000.0 /
            input->descriptor().sample_rate_hz));
    frame.bins.assign(
        fft_length / 2 + 1,
        {0.0, 0.0});
    frame.bins[1] = {
        std::sqrt(magnitude_squared),
        0.0,
    };
    DataUnitMetadata metadata;
    metadata.start_sample = frame.start_sample;
    metadata.time_unix_ms = frame.time_unix_ms;
    metadata.channel_bitmap =
        std::uint32_t{1} << channel;
    metadata.sequence_bitmap =
        metadata.channel_bitmap;
    metadata.clock_domain_id = "ishmael-lifecycle";
    input->publish(pamguard::core::make_data_unit(
        std::move(metadata),
        std::move(frame)));
}

void check_energy_group_and_lifecycle() {
    const auto input = block(
        "energy-input",
        pamguard::core::kFftDataType,
        8000.0,
        3,
        8,
        4);
    const auto function = block(
        "energy-function",
        pamguard::core::kIshmaelFunctionDataType,
        8000.0,
        3);
    const auto detections = block(
        "energy-detections",
        pamguard::core::kIshmaelDetectionDataType,
        8000.0,
        1);
    pamguard::detectors::IshmaelEnergySumConfig config;
    config.enabled = true;
    config.f0 = 0.0;
    config.f1 = 1000.0;
    config.thresh = 0.5;
    pamguard::core::IshmaelNode node(
        "energy-node",
        8000.0,
        4,
        config,
        input,
        {function, detections});
    node.prepare();
    std::size_t function_count = 0;
    std::vector<pamguard::detectors::IshmaelDetection>
        picked;
    auto fn_subscription = function->subscribe(
        [&](const auto&) {
            ++function_count;
        });
    auto det_subscription = detections->subscribe(
        [&](const auto& unit) {
            picked.push_back(
                std::any_cast<
                    pamguard::detectors::IshmaelDetection>(
                        unit.payload));
        });
    node.start();

    publish_fft(input, 1, 0, 4.0);
    publish_fft(input, 1, 4, 0.0);
    require(
        function_count == 2 && picked.empty(),
        "Energy function did not process a selected non-leading group channel independently of peak picking");

    publish_fft(input, 0, 8, 4.0);
    node.stop();
    node.start();
    publish_fft(input, 0, 12, 0.0);
    require(
        picked.empty(),
        "Energy Sum carried an open Java peak across stop/start");

    publish_fft(input, 0, 16, 4.0);
    publish_fft(input, 0, 20, 0.0);
    require(
        picked.size() == 1 &&
            picked.front().channel == 0 &&
            picked.front().start_time_ms == 100002,
        "Energy Sum did not restart with a fresh peak picker or preserve absolute time");
    node.stop();
}

void check_sgram_lifecycle() {
    const auto input = block(
        "sgram-input",
        pamguard::core::kFftDataType,
        1000.0,
        3,
        8,
        100);
    const auto function = block(
        "sgram-function",
        pamguard::core::kIshmaelFunctionDataType,
        1000.0,
        3);
    const auto detections = block(
        "sgram-detections",
        pamguard::core::kIshmaelDetectionDataType,
        1000.0,
        1);
    pamguard::detectors::SgramCorrConfig config;
    config.enabled = true;
    config.segments.push_back(
        {0.0, 100.0, 0.2, 100.0});
    config.spread = 20.0;
    config.thresh =
        std::numeric_limits<double>::max();
    pamguard::core::SgramCorrNode node(
        "sgram-node",
        1000.0,
        8,
        100,
        config,
        input,
        {function, detections});
    node.prepare();
    std::vector<std::uint32_t> function_channels;
    auto subscription = function->subscribe(
        [&](const auto& unit) {
            function_channels.push_back(
                unit.metadata.channel_bitmap);
        });
    node.start();
    publish_fft(input, 0, 0, 4.0);
    node.stop();
    node.start();
    publish_fft(input, 0, 100, 4.0);
    require(
        function_channels.empty(),
        "Spectrogram Correlation retained a partial circular buffer across stop/start");
    publish_fft(input, 0, 200, 4.0);
    publish_fft(input, 1, 300, 4.0);
    publish_fft(input, 1, 400, 4.0);
    require(
        function_channels ==
            std::vector<std::uint32_t>({1, 2}),
        "Spectrogram Correlation did not keep independent selected-channel buffers");
    node.stop();
}

void publish_audio(
    const std::shared_ptr<DataBlock>& input,
    std::size_t frame_count,
    std::uint64_t start_sample) {
    pamguard::core::AudioChunk chunk;
    chunk.channel_count = 2;
    chunk.sample_rate_hz = 1000.0;
    chunk.start_sample = start_sample;
    chunk.time_unix_ms =
        200000 + static_cast<std::int64_t>(start_sample);
    chunk.interleaved_pcm.assign(
        frame_count * chunk.channel_count,
        0.25);
    DataUnitMetadata metadata;
    metadata.start_sample =
        static_cast<std::int64_t>(start_sample);
    metadata.time_unix_ms = chunk.time_unix_ms;
    metadata.duration_samples = frame_count;
    metadata.channel_bitmap = 3;
    metadata.sequence_bitmap = 3;
    metadata.clock_domain_id = "ishmael-lifecycle";
    input->publish(pamguard::core::make_data_unit(
        std::move(metadata),
        std::move(chunk)));
}

void check_match_filter_lifecycle() {
    const auto input = block(
        "match-input",
        pamguard::core::kRawAudioDataType,
        1000.0,
        3);
    const auto function = block(
        "match-function",
        pamguard::core::kIshmaelFunctionDataType,
        1000.0,
        1);
    const auto detections = block(
        "match-detections",
        pamguard::core::kIshmaelDetectionDataType,
        1000.0,
        1);
    pamguard::detectors::MatchFiltConfig config;
    config.enabled = true;
    config.kernel.assign(10, 1.0);
    config.channels = {0};
    config.thresh =
        std::numeric_limits<double>::max();
    pamguard::core::MatchFiltNode node(
        "match-node",
        1000.0,
        config,
        input,
        {function, detections});
    node.prepare();
    std::size_t function_count = 0;
    std::vector<std::uint32_t> channel_maps;
    auto subscription = function->subscribe(
        [&](const auto& unit) {
            ++function_count;
            channel_maps.push_back(
                unit.metadata.channel_bitmap);
        });
    node.start();
    // max(round(0.1*1000), 2*10) -> next power of two 128.
    publish_audio(input, 127, 0);
    node.stop();
    node.start();
    publish_audio(input, 1, 127);
    require(
        function_count == 0,
        "Matched Filter retained a partial overlap-save buffer across stop/start");
    publish_audio(input, 127, 128);
    require(
        function_count == 118 &&
            std::all_of(
                channel_maps.begin(),
                channel_maps.end(),
                [](auto bitmap) {
                    return bitmap == 1;
                }),
        "Matched Filter did not restart cleanly on the first channel of its group");
    node.stop();
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error(
                "Usage: ishmael_settings_check "
                "<java-defaults-fixture.json>");
        }
        const auto fixture = read_json(argv[1]);
        check_defaults_and_strict_settings(fixture);
        check_group_semantics();
        check_descriptors_projection_and_output_maps();
        check_energy_group_and_lifecycle();
        check_sgram_lifecycle();
        check_match_filter_lifecycle();
        std::cout
            << "Ishmael Java defaults, strict settings, controlled-unit "
               "projection, grouping, and lifecycle validated\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
