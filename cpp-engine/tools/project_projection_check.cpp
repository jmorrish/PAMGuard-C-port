#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <json.hpp>

#include "pamguard/core/BuiltInModules.h"
#include "pamguard/core/ModuleGraphJson.h"
#include "pamguard/project/BuiltInControlledUnits.h"
#include "pamguard/project/ProjectJson.h"
#include "pamguard/project/ProjectProjection.h"

namespace {

using json = nlohmann::json;
using namespace pamguard::project;

constexpr const char* kProjectId =
    "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
constexpr const char* kAcquisitionId =
    "11111111-1111-4111-8111-111111111111";
constexpr const char* kFftId =
    "22222222-2222-4222-8222-222222222222";
constexpr const char* kUserDisplayId =
    "33333333-3333-4333-8333-333333333333";
constexpr const char* kSpectrogramId =
    "display:44444444-4444-4444-8444-444444444444";
constexpr const char* kSecondFftId =
    "55555555-5555-4555-8555-555555555555";
constexpr const char* kSecondSpectrogramId =
    "display:66666666-6666-4666-8666-666666666666";

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool has_issue(
    const ProjectProjectionResult& result,
    const std::string& code,
    ProjectionIssueClass issue_class) {
    return std::any_of(
        result.issues.begin(),
        result.issues.end(),
        [&](const auto& issue) {
            return issue.code == code &&
                issue.issue_class == issue_class;
        });
}

void require_runnable(
    const ProjectProjectionResult& result,
    const std::string& context) {
    if (result.runnable()) {
        return;
    }
    std::string message = context + ":";
    for (const auto& issue : result.issues) {
        message += " [" + issue.code + "] " + issue.message;
    }
    throw std::runtime_error(message);
}

const pamguard::core::ModuleInstance& module(
    const ProjectProjectionResult& result,
    const std::string& id) {
    const auto found = std::find_if(
        result.graph.modules.begin(),
        result.graph.modules.end(),
        [&](const auto& value) { return value.id == id; });
    require(
        found != result.graph.modules.end(),
        "Projected graph omits module '" + id + "'");
    return *found;
}

const pamguard::core::ModuleConnection& connection(
    const ProjectProjectionResult& result,
    const std::string& id) {
    const auto found = std::find_if(
        result.graph.connections.begin(),
        result.graph.connections.end(),
        [&](const auto& value) { return value.id == id; });
    require(
        found != result.graph.connections.end(),
        "Projected graph omits connection '" + id + "'");
    return *found;
}

ControlledUnitRegistry controlled_registry() {
    ControlledUnitRegistry registry;
    register_builtin_controlled_units(registry);
    return registry;
}

pamguard::core::ModuleRegistry runtime_registry() {
    pamguard::core::ModuleRegistry registry;
    pamguard::core::register_builtin_module_types(registry);
    return registry;
}

ControlledUnitInstance controlled_unit(
    const ControlledUnitRegistry& registry,
    const std::string& id,
    const std::string& type_id,
    const std::string& name) {
    const auto* descriptor =
        registry.find_controlled_unit(type_id);
    require(descriptor, "Fixture controlled-unit type is absent");
    return {
        id,
        type_id,
        descriptor->descriptor_version,
        {
            descriptor->runtime_recipe.id,
            descriptor->runtime_recipe.version,
        },
        name,
        descriptor->settings.version,
        descriptor->settings.default_settings_json,
        {},
    };
}

ProjectDocument example_project(
    const ControlledUnitRegistry& registry) {
    ProjectDocument document;
    document.project_id = kProjectId;
    document.metadata = {
        "FFT projection",
        "First controlled-unit projection fixture",
    };
    document.mode = ProjectMode::Normal;
    document.descriptor_set = {"pamguard-2.02.18e", 1};
    const auto* array_manager =
        registry.find_global_settings("pamguard.array-manager");
    require(array_manager, "Array Manager global settings are absent");
    document.global_settings.components.push_back({
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
    fft.bindings.push_back({
        "rawAudio",
        {{kAcquisitionId, "rawAudio"}},
    });
    auto user_display = controlled_unit(
        registry,
        kUserDisplayId,
        "pamguard.user-display",
        "User Display");
    document.controlled_units = {
        std::move(acquisition),
        std::move(fft),
        std::move(user_display),
    };

    const auto* provider = registry.find_display_provider(
        "pamguard.spectrogram-display");
    require(provider, "Spectrogram provider is absent");
    DisplayInstance spectrogram;
    spectrogram.id = kSpectrogramId;
    spectrogram.provider_type_id = provider->id;
    spectrogram.provider_version =
        provider->descriptor_version;
    spectrogram.owner = {kUserDisplayId, "provider"};
    spectrogram.source =
        SourceReference{kFftId, "fft"};
    spectrogram.settings_version =
        provider->settings.version;
    spectrogram.settings_json =
        provider->settings.default_settings_json;

    DisplayTab tab;
    tab.id =
        "tab:33333333-3333-4333-8333-333333333333:main";
    tab.name = "User Display";
    tab.owner = {kUserDisplayId, "main"};
    tab.displays.push_back(std::move(spectrogram));
    tab.layout.columns = 12;
    tab.layout.selected_display_id = kSpectrogramId;
    tab.layout.items.push_back({
        kSpectrogramId,
        0,
        0,
        12,
        6,
    });
    document.display_tabs.push_back(std::move(tab));
    return document;
}

void check_exact_graph(
    const ProjectProjectionResult& projection,
    const pamguard::core::ModuleRegistry& runtime) {
    require_runnable(projection, "Exact first-slice projection failed");
    require(
        projection.graph.schema_version == 1 &&
            projection.graph.revision == 0 &&
            projection.graph.modules.size() == 3 &&
            projection.graph.connections.size() == 2,
        "Projection emitted an unexpected graph shape");
    require(
        projection.array_geometry.has_value() &&
            projection.array_geometry->id ==
                "Basic Linear Array" &&
            projection.array_geometry->hydrophones.size() == 2,
        "Projection omitted authoritative Array Manager geometry");

    const auto acquisition_node =
        projected_runtime_node_id(
            kAcquisitionId,
            "acquisition");
    const auto fft_node =
        projected_runtime_node_id(kFftId, "fft-process");
    const auto noise_node =
        projected_runtime_node_id(kFftId, "spectral-noise");
    require(
        acquisition_node ==
            std::string("rt:") + kAcquisitionId +
                ":acquisition" &&
            fft_node ==
                std::string("rt:") + kFftId +
                    ":fft-process" &&
            noise_node ==
                std::string("rt:") + kFftId +
                    ":spectral-noise",
        "Runtime-node ID format changed");

    const auto& acquisition =
        module(projection, acquisition_node);
    const auto& fft = module(projection, fft_node);
    const auto& noise = module(projection, noise_node);
    const double default_calibration_db =
        20.0 * std::log10(5.0 / 2.0) -
        (-170.0 + 0.0 + 0.0);
    require(
        acquisition.type_id == "pamguard.acquisition" &&
            fft.type_id == "pamguard.fft" &&
            noise.type_id == "pamguard.spectrogram-noise",
        "Runtime child types changed");
    require(
        json::parse(acquisition.settings_json) == json{
            {"daqSystemType", "Sound Card"},
            {"sampleRateHz", 48000},
            {"channelCount", 2},
            {"hardwareChannelList", {0, 1}},
            {"hydrophoneList", {0, 1}},
            {"voltsPeak2Peak", 5},
            {"preamplifier", {
                {"gainDb", 0},
                {"bandwidthHz", {0, 20000}},
            }},
            {"subtractDC", true},
            {"dcTimeConstantSeconds", 1},
            {"calibrationDbOffsetByChannel", {
                default_calibration_db,
                default_calibration_db,
            }},
        },
        "Acquisition Java-to-runtime settings adapter changed");
    require(
        json::parse(fft.settings_json) == json{
            {"fftLength", 1024},
            {"fftHop", 512},
            {"windowType", "Hann"},
            {"channels", {0, 1}},
            {"clickRemoval", false},
            {"clickThreshold", 5},
            {"clickPower", 6},
        },
        "FFT Java-to-runtime settings adapter changed");
    require(
        json::parse(noise.settings_json) == json{
            {"medianFilter", false},
            {"medianFilterLength", 61},
            {"averageSubtraction", false},
            {"updateConstant", 0.02},
            {"kernelSmoothing", false},
            {"threshold", false},
            {"thresholdDb", 8.0},
            {"finalOutput", 2},
        },
        "identity.v1 spectral-noise settings projection changed");

    const auto internal_id =
        projected_internal_connection_id(
            kFftId,
            "fft-to-spectral-noise");
    const auto external_id =
        projected_external_connection_id(
            kFftId,
            "rawAudio",
            kAcquisitionId,
            "rawAudio");
    const auto& internal =
        connection(projection, internal_id);
    const auto& external =
        connection(projection, external_id);
    require(
        internal.source.module_id == fft_node &&
            internal.source.port_id == "fft" &&
            internal.target.module_id == noise_node &&
            internal.target.port_id == "input" &&
            external.source.module_id == acquisition_node &&
            external.source.port_id == "audio" &&
            external.target.module_id == fft_node &&
            external.target.port_id == "input",
        "Projected internal/external endpoints changed");

    const auto* raw_audio =
        projection.index.find_public_output(
            kAcquisitionId,
            "rawAudio");
    const auto* raw_fft =
        projection.index.find_public_output(kFftId, "fft");
    const auto* reduced_fft =
        projection.index.find_public_output(
            kFftId,
            "noiseReducedFft");
    require(
        raw_audio && raw_fft && reduced_fft &&
            raw_audio->block_id ==
                projected_data_block_id(
                    acquisition_node,
                    "audio") &&
            raw_fft->block_id ==
                projected_data_block_id(fft_node, "fft") &&
            reduced_fft->block_id ==
                projected_data_block_id(
                    noise_node,
                    "output") &&
            raw_fft->block_id != reduced_fft->block_id,
        "Stable public output/data-block projection changed");
    require(
        projection.index.data_blocks.size() == 3 &&
            projection.index.find_data_block(
                "block:" + raw_fft->runtime_node_id +
                ":" + raw_fft->runtime_port_id),
        "Projected data-block index is incomplete");

    const auto* display =
        projection.index.find_display(kSpectrogramId);
    const auto* projected_input =
        projection.index.find_public_input(kFftId, "rawAudio");
    const auto external_ownership = std::find_if(
        projection.index.connections.begin(),
        projection.index.connections.end(),
        [&](const auto& value) {
            return value.connection_id == external_id;
        });
    require(
        display && display->public_source ==
                       std::optional<SourceReference>{
                           SourceReference{kFftId, "fft"}} &&
            display->source_block_id ==
                std::optional<std::string>{raw_fft->block_id} &&
            projected_input &&
            projected_input->sources ==
                std::vector<SourceReference>{
                    {kAcquisitionId, "rawAudio"}} &&
            projected_input->connection_ids ==
                std::vector<std::string>{external_id} &&
            external_ownership !=
                projection.index.connections.end() &&
            external_ownership->public_source ==
                std::optional<SourceReference>{
                    SourceReference{
                        kAcquisitionId,
                        "rawAudio"}},
        "Projection index did not retain public source-role identity");
    require(
        std::none_of(
            projection.graph.modules.begin(),
            projection.graph.modules.end(),
            [](const auto& projected) {
                return projected.type_id ==
                        "pamguard.user-display" ||
                    projected.type_id ==
                        "pamguard.spectrogram-display";
            }),
        "Presentation ownership generated runtime nodes");

    pamguard::core::ModuleGraph validator(runtime);
    const auto validation =
        validator.validate(projection.graph);
    require(
        validation.valid(),
        "Runnable projection failed low-level ModuleGraph validation");
}

std::set<std::string> runtime_ids(
    const ProjectProjectionResult& projection) {
    std::set<std::string> result;
    for (const auto& node : projection.index.runtime_nodes) {
        result.insert(node.runtime_node_id);
    }
    return result;
}

std::set<std::string> block_ids(
    const ProjectProjectionResult& projection) {
    std::set<std::string> result;
    for (const auto& block : projection.index.data_blocks) {
        result.insert(block.block_id);
    }
    return result;
}

std::set<std::string> connection_ids(
    const ProjectProjectionResult& projection) {
    std::set<std::string> result;
    for (const auto& value : projection.index.connections) {
        result.insert(value.connection_id);
    }
    return result;
}

void check_round_trip_and_stability(
    const ProjectDocument& document,
    const ControlledUnitRegistry& controlled,
    const pamguard::core::ModuleRegistry& runtime) {
    const auto first = project_document_to_runtime_graph(
        document,
        controlled,
        runtime);
    const auto round_trip = project_document_from_json(
        project_document_to_json(document));
    const auto second = project_document_to_runtime_graph(
        round_trip,
        controlled,
        runtime);
    require_runnable(first, "Initial round-trip projection failed");
    require_runnable(second, "Restored round-trip projection failed");
    require(
        pamguard::core::module_graph_to_json(first.graph) ==
                pamguard::core::module_graph_to_json(second.graph) &&
            first.index == second.index &&
            first.issues == second.issues,
        "Project JSON round-trip changed projection bytes or ownership");

    auto edited = document;
    edited.controlled_units[0].name = "Renamed Acquisition";
    edited.controlled_units[1].name = "Renamed FFT";
    auto acquisition_settings = json::parse(
        edited.controlled_units[0].settings_json);
    acquisition_settings["voltsPeak2Peak"] = 2.5;
    edited.controlled_units[0].settings_json =
        acquisition_settings.dump();
    auto fft_settings = json::parse(
        edited.controlled_units[1].settings_json);
    fft_settings["fft"]["clickThreshold"] = 7.5;
    edited.controlled_units[1].settings_json =
        fft_settings.dump();
    const auto changed = project_document_to_runtime_graph(
        edited,
        controlled,
        runtime);
    require_runnable(changed, "Edited project projection failed");
    require(
        runtime_ids(first) == runtime_ids(changed) &&
            block_ids(first) == block_ids(changed) &&
            connection_ids(first) == connection_ids(changed),
        "Rename/settings edits changed stable child, block, or connection IDs");
}

void check_window_and_channel_mapping(
    const ProjectDocument& base,
    const ControlledUnitRegistry& controlled,
    const pamguard::core::ModuleRegistry& runtime) {
    const std::vector<std::string> windows{
        "Rectangular",
        "Hamming",
        "Hann",
        "Bartlett",
        "Blackman",
        "Blackman-Harris",
    };
    for (std::size_t index = 0; index < windows.size(); ++index) {
        auto document = base;
        auto settings =
            json::parse(document.controlled_units[1].settings_json);
        settings["fft"]["windowFunction"] = index;
        settings["fft"]["channelMap"] = 9;
        document.controlled_units[1].settings_json =
            settings.dump();
        const auto projection =
            project_document_to_runtime_graph(
                document,
                controlled,
                runtime);
        require_runnable(
            projection,
            "Window/channel adapter case failed");
        const auto fft_node =
            projected_runtime_node_id(kFftId, "fft-process");
        const auto projected_settings =
            json::parse(
                module(projection, fft_node).settings_json);
        require(
            projected_settings.at("windowType") ==
                    windows[index] &&
                projected_settings.at("channels") ==
                    json::array({0, 3}),
            "Java window/channelMap adapter changed");
    }
}

void check_acquisition_portable_settings(
    const ProjectDocument& base,
    const ControlledUnitRegistry& controlled,
    const pamguard::core::ModuleRegistry& runtime) {
    const auto defaults =
        json::parse(base.controlled_units[0].settings_json);
    require(
        defaults == json{
            {"daqSystemType", "Sound Card"},
            {"sampleRate", 48000},
            {"nChannels", 2},
            {"hardwareChannelList", {0, 1}},
            {"hydrophoneList", {0, 1}},
            {"voltsPeak2Peak", 5},
            {"preamplifier", {
                {"gainDb", 0},
                {"bandwidthHz", {0, 20000}},
            }},
            {"subtractDC", true},
            {"dcTimeConstantSeconds", 1},
            {"calibrationDbOffsetByChannel", json::array()},
        } &&
            !defaults.contains("sourceId"),
        "Acquisition portable defaults diverged from Java authority or "
        "persisted a host binding");

    auto changed = base;
    auto settings =
        json::parse(changed.controlled_units[0].settings_json);
    settings["daqSystemType"] = "Sound Card";
    settings["sampleRate"] = 96000;
    settings["hardwareChannelList"] = {3, 7};
    settings["hydrophoneList"] = {1, 0};
    settings["voltsPeak2Peak"] = 2.5;
    settings["preamplifier"]["gainDb"] = 24.0;
    settings["preamplifier"]["bandwidthHz"] =
        {100.0, 40000.0};
    settings["subtractDC"] = false;
    settings["dcTimeConstantSeconds"] = 2.5;
    settings["calibrationDbOffsetByChannel"] =
        {100.0, 101.0};
    changed.controlled_units[0].settings_json =
        settings.dump();

    const auto projection =
        project_document_to_runtime_graph(
            changed,
            controlled,
            runtime);
    require_runnable(
        projection,
        "Valid Java Acquisition settings failed projection");
    const auto projected = json::parse(module(
        projection,
        projected_runtime_node_id(
            kAcquisitionId,
            "acquisition")).settings_json);
    require(
        !projected.contains("sourceId") &&
            projected.at("daqSystemType") ==
                "Sound Card" &&
            projected.at("sampleRateHz") == 96000 &&
            projected.at("channelCount") == 2 &&
            projected.at("hardwareChannelList") ==
                json::array({3, 7}) &&
            projected.at("hydrophoneList") ==
                json::array({1, 0}) &&
            projected.at("voltsPeak2Peak") == 2.5 &&
            projected.at("preamplifier") ==
                settings.at("preamplifier") &&
            projected.at("subtractDC") == false &&
            projected.at("dcTimeConstantSeconds") == 2.5 &&
            projected.at("calibrationDbOffsetByChannel") ==
                json::array({100.0, 101.0}),
        "Acquisition portable settings adapter omitted scientific fields "
        "or leaked a host binding");

    auto derived = base;
    auto array_settings = json::parse(
        derived.global_settings.components.at(0)
            .settings_json);
    array_settings["hydrophones"][0]["sensitivityDb"] =
        -180.0;
    array_settings["hydrophones"][0]["preampGainDb"] =
        12.0;
    array_settings["hydrophones"][1]["sensitivityDb"] =
        -160.0;
    array_settings["hydrophones"][1]["preampGainDb"] =
        3.0;
    derived.global_settings.components.at(0).settings_json =
        array_settings.dump();

    auto derived_settings = json::parse(
        derived.controlled_units[0].settings_json);
    derived_settings["hydrophoneList"] = {1, 0};
    derived_settings["voltsPeak2Peak"] = 4.0;
    derived_settings["preamplifier"]["gainDb"] = 20.0;
    derived_settings["calibrationDbOffsetByChannel"] =
        json::array();
    derived.controlled_units[0].settings_json =
        derived_settings.dump();
    const auto derived_projection =
        project_document_to_runtime_graph(
            derived,
            controlled,
            runtime);
    require_runnable(
        derived_projection,
        "Portable Acquisition calibration derivation failed");
    const auto derived_runtime_settings = json::parse(
        module(
            derived_projection,
            projected_runtime_node_id(
                kAcquisitionId,
                "acquisition"))
            .settings_json);
    const auto derived_offsets =
        derived_runtime_settings
            .at("calibrationDbOffsetByChannel")
            .get<std::vector<double>>();
    const std::vector<double> expected_offsets{
        20.0 * std::log10(4.0 / 2.0) -
            (-160.0 + 3.0 + 20.0),
        20.0 * std::log10(4.0 / 2.0) -
            (-180.0 + 12.0 + 20.0),
    };
    require(
        derived_offsets.size() == expected_offsets.size() &&
            std::abs(
                derived_offsets[0] -
                expected_offsets[0]) < 1e-12 &&
            std::abs(
                derived_offsets[1] -
                expected_offsets[1]) < 1e-12,
        "Acquisition calibration did not match Java "
        "rawAmplitude2dB or hydrophoneList routing");
}

void check_needs_configuration(
    const ProjectDocument& base,
    const ControlledUnitRegistry& controlled,
    const pamguard::core::ModuleRegistry& runtime) {
    auto missing = base;
    missing.controlled_units[1].bindings.clear();
    const auto projection =
        project_document_to_runtime_graph(
            missing,
            controlled,
            runtime);
    require(
        projection.editor_valid() &&
            projection.needs_configuration() &&
            !projection.runnable() &&
            has_issue(
                projection,
                "missing-required-binding",
                ProjectionIssueClass::NeedsConfiguration) &&
            projection.graph.modules.size() == 3 &&
            projection.graph.connections.size() == 1,
        "Missing required FFT source was not saveable needs-configuration state");

    pamguard::core::ModuleGraph validator(runtime);
    const auto validation =
        validator.validate(projection.graph);
    require(
        !validation.valid() &&
            std::any_of(
                validation.issues.begin(),
                validation.issues.end(),
                [](const auto& issue) {
                    return issue.code == "missing_required_input";
                }),
        "Incomplete projected graph did not retain low-level start blocker");
}

void check_unbound_display_is_allowed(
    const ProjectDocument& base,
    const ControlledUnitRegistry& controlled,
    const pamguard::core::ModuleRegistry& runtime) {
    auto unbound = base;
    unbound.display_tabs[0].displays[0].source.reset();
    const auto projection =
        project_document_to_runtime_graph(
            unbound,
            controlled,
            runtime);
    require_runnable(
        projection,
        "Java-permitted unbound Spectrogram was rejected");
    const auto* display =
        projection.index.find_display(kSpectrogramId);
    require(
        display && !display->public_source &&
            !display->source_block_id,
        "Unbound Spectrogram acquired phantom source ownership");
}

void check_independent_spectrogram_displays(
    const ProjectDocument& base,
    const ControlledUnitRegistry& controlled,
    const pamguard::core::ModuleRegistry& runtime) {
    auto document = base;
    auto second_fft = controlled_unit(
        controlled,
        kSecondFftId,
        "pamguard.fft",
        "High-frequency FFT");
    second_fft.bindings.push_back({
        "rawAudio",
        {{kAcquisitionId, "rawAudio"}},
    });
    document.controlled_units.push_back(std::move(second_fft));

    auto first_settings = json::parse(
        document.display_tabs[0].displays[0].settings_json);
    first_settings["nPanels"] = 2;
    first_settings["channelList"] = {0, 1};
    first_settings["frequencyLimits"] = {0, 12000};
    first_settings["amplitudeLimits"] = {55, 105};
    first_settings["colourMap"] = "GREY";
    first_settings["timeScaleFixed"] = true;
    first_settings["displayLength"] = 12.5;
    first_settings["wrapDisplay"] = false;
    document.display_tabs[0].displays[0].settings_json =
        first_settings.dump();

    const auto* provider = controlled.find_display_provider(
        "pamguard.spectrogram-display");
    require(provider, "Spectrogram provider is absent");
    DisplayInstance second;
    second.id = kSecondSpectrogramId;
    second.provider_type_id = provider->id;
    second.provider_version = provider->descriptor_version;
    second.owner = {kUserDisplayId, "provider"};
    second.source = SourceReference{kSecondFftId, "fft"};
    second.settings_version = provider->settings.version;
    auto second_settings =
        json::parse(provider->settings.default_settings_json);
    second_settings["channelList"] = {1};
    second_settings["frequencyLimits"] = {2000, 8000};
    second_settings["amplitudeLimits"] = {65, 95};
    second_settings["colourMap"] = "FIRE";
    second_settings["pixelsPerSlics"] = 3;
    second.settings_json = second_settings.dump();
    document.display_tabs[0].displays.push_back(std::move(second));
    document.display_tabs[0].layout.items.push_back({
        kSecondSpectrogramId,
        0,
        6,
        12,
        6,
    });

    const auto projection = project_document_to_runtime_graph(
        document,
        controlled,
        runtime);
    require_runnable(
        projection,
        "Two independently configured Spectrograms were rejected");
    const auto* first_projected =
        projection.index.find_display(kSpectrogramId);
    const auto* second_projected =
        projection.index.find_display(kSecondSpectrogramId);
    const auto* first_fft =
        projection.index.find_public_output(kFftId, "fft");
    const auto* second_fft_output =
        projection.index.find_public_output(kSecondFftId, "fft");
    require(
        first_projected && second_projected &&
            first_fft && second_fft_output &&
            first_projected->source_block_id ==
                std::optional<std::string>{first_fft->block_id} &&
            second_projected->source_block_id ==
                std::optional<std::string>{second_fft_output->block_id} &&
            first_projected->source_block_id !=
                second_projected->source_block_id,
        "Spectrogram displays did not retain independent FFT sources");

    const auto restored = project_document_from_json(
        project_document_to_json(document));
    const auto restored_projection =
        project_document_to_runtime_graph(
            restored,
            controlled,
            runtime);
    require_runnable(
        restored_projection,
        "Restored independent Spectrograms were rejected");
    require(
        restored.display_tabs[0].displays[0].id ==
                kSpectrogramId &&
            restored.display_tabs[0].displays[1].id ==
                kSecondSpectrogramId &&
            restored_projection.index.find_display(kSpectrogramId)
                    ->source_block_id ==
                first_projected->source_block_id &&
            restored_projection.index.find_display(
                    kSecondSpectrogramId)
                    ->source_block_id ==
                second_projected->source_block_id,
        "Spectrogram identities or bindings changed across save/restart");

    auto partially_unbound = restored;
    partially_unbound.display_tabs[0].displays[0].source.reset();
    const auto unbound_projection =
        project_document_to_runtime_graph(
            partially_unbound,
            controlled,
            runtime);
    require_runnable(
        unbound_projection,
        "One unbound Spectrogram invalidated an independent display");
    require(
        !unbound_projection.index.find_display(kSpectrogramId)
             ->source_block_id &&
            unbound_projection.index.find_display(
                    kSecondSpectrogramId)
                    ->source_block_id ==
                second_projected->source_block_id,
        "Unbinding one Spectrogram disturbed another display");
}

template <typename Mutation>
void require_invalid_projection(
    const ProjectDocument& base,
    const ControlledUnitRegistry& controlled,
    const pamguard::core::ModuleRegistry& runtime,
    Mutation mutation,
    const std::string& expected_code) {
    auto invalid = base;
    mutation(invalid);
    const auto projection =
        project_document_to_runtime_graph(
            invalid,
            controlled,
            runtime);
    require(
        !projection.editor_valid() &&
            has_issue(
                projection,
                expected_code,
                ProjectionIssueClass::EditorInvalid),
        "Invalid projection did not report '" + expected_code + "'");
}

void check_invalid_projects(
    const ProjectDocument& base,
    const ControlledUnitRegistry& controlled,
    const pamguard::core::ModuleRegistry& runtime) {
    require_invalid_projection(
        base,
        controlled,
        runtime,
        [](auto& document) {
            auto settings = json::parse(
                document.controlled_units[0].settings_json);
            settings["sourceId"] = "host-device-must-not-persist";
            document.controlled_units[0].settings_json =
                settings.dump();
        },
        "invalid-settings");
    require_invalid_projection(
        base,
        controlled,
        runtime,
        [](auto& document) {
            auto settings = json::parse(
                document.controlled_units[0].settings_json);
            settings["hardwareChannelList"] = {0};
            document.controlled_units[0].settings_json =
                settings.dump();
        },
        "invalid-settings");
    require_invalid_projection(
        base,
        controlled,
        runtime,
        [](auto& document) {
            auto settings = json::parse(
                document.controlled_units[0].settings_json);
            settings["hydrophoneList"] = {0, 2};
            document.controlled_units[0].settings_json =
                settings.dump();
        },
        "invalid-settings");
    require_invalid_projection(
        base,
        controlled,
        runtime,
        [](auto& document) {
            auto settings = json::parse(
                document.controlled_units[0].settings_json);
            settings["preamplifier"]["bandwidthHz"] =
                {20000, 100};
            document.controlled_units[0].settings_json =
                settings.dump();
        },
        "invalid-settings");
    require_invalid_projection(
        base,
        controlled,
        runtime,
        [](auto& document) {
            auto settings = json::parse(
                document.controlled_units[0].settings_json);
            settings["dcTimeConstantSeconds"] = 0;
            document.controlled_units[0].settings_json =
                settings.dump();
        },
        "invalid-settings");
    require_invalid_projection(
        base,
        controlled,
        runtime,
        [](auto& document) {
            document.display_tabs.clear();
        },
        "user-display-tab-ownership");
    require_invalid_projection(
        base,
        controlled,
        runtime,
        [](auto& document) {
            document.descriptor_set.version = 2;
        },
        "descriptor-set-version-mismatch");
    require_invalid_projection(
        base,
        controlled,
        runtime,
        [](auto& document) {
            document.controlled_units[1].descriptor_version = 2;
        },
        "descriptor-version-mismatch");
    require_invalid_projection(
        base,
        controlled,
        runtime,
        [](auto& document) {
            document.controlled_units[1].settings_version = 2;
        },
        "settings-version-mismatch");
    require_invalid_projection(
        base,
        controlled,
        runtime,
        [](auto& document) {
            document.controlled_units[1].recipe.version = 2;
        },
        "recipe-version-mismatch");
    require_invalid_projection(
        base,
        controlled,
        runtime,
        [](auto& document) {
            document.controlled_units[1].recipe.id =
                "pamguard.wrong.runtime";
        },
        "recipe-version-mismatch");
    require_invalid_projection(
        base,
        controlled,
        runtime,
        [](auto& document) {
            auto settings =
                json::parse(
                    document.controlled_units[1].settings_json);
            settings["fft"]["windowFunction"] = 6;
            document.controlled_units[1].settings_json =
                settings.dump();
        },
        "invalid-settings");
    require_invalid_projection(
        base,
        controlled,
        runtime,
        [](auto& document) {
            document.controlled_units[1]
                .bindings[0]
                .sources[0]
                .output_role = "fft";
        },
        "unknown-output-role");
    require_invalid_projection(
        base,
        controlled,
        runtime,
        [](auto& document) {
            document.display_tabs[0]
                .displays[0]
                .provider_type_id = "pamguard.unknown-display";
        },
        "unknown-display-provider");
    require_invalid_projection(
        base,
        controlled,
        runtime,
        [](auto& document) {
            document.display_tabs[0]
                .displays[0]
                .provider_version = 2;
        },
        "display-provider-version-mismatch");
    require_invalid_projection(
        base,
        controlled,
        runtime,
        [](auto& document) {
            document.display_tabs[0]
                .displays[0]
                .settings_version = 3;
        },
        "display-settings-version-mismatch");
    require_invalid_projection(
        base,
        controlled,
        runtime,
        [](auto& document) {
            document.display_tabs[0]
                .displays[0]
                .source =
                SourceReference{kAcquisitionId, "rawAudio"};
        },
        "incompatible-display-source");
    require_invalid_projection(
        base,
        controlled,
        runtime,
        [](auto& document) {
            document.display_tabs[0]
                .displays[0]
                .owner.unit_id = kAcquisitionId;
        },
        "display-owner-tab-mismatch");
    require_invalid_projection(
        base,
        controlled,
        runtime,
        [](auto& document) {
            auto settings = json::parse(
                document.display_tabs[0]
                    .displays[0]
                    .settings_json);
            settings["displayLength"] = 0;
            document.display_tabs[0]
                .displays[0]
                .settings_json = settings.dump();
        },
        "invalid-display-settings");
    require_invalid_projection(
        base,
        controlled,
        runtime,
        [](auto& document) {
            auto settings = json::parse(
                document.display_tabs[0]
                    .displays[0]
                    .settings_json);
            settings["sourceName"] = "FFT source";
            document.display_tabs[0]
                .displays[0]
                .settings_json = settings.dump();
        },
        "invalid-settings");
    require_invalid_projection(
        base,
        controlled,
        runtime,
        [](auto& document) {
            auto settings = json::parse(
                document.display_tabs[0]
                    .displays[0]
                    .settings_json);
            settings["nPanels"] = 2;
            settings["channelList"] = {0};
            document.display_tabs[0]
                .displays[0]
                .settings_json = settings.dump();
        },
        "invalid-display-settings");
    require_invalid_projection(
        base,
        controlled,
        runtime,
        [](auto& document) {
            auto settings = json::parse(
                document.display_tabs[0]
                    .displays[0]
                    .settings_json);
            settings["colourMap"] = "not-a-java-colour";
            document.display_tabs[0]
                .displays[0]
                .settings_json = settings.dump();
        },
        "invalid-display-settings");
}

void check_cardinality_and_names(
    const ProjectDocument& base,
    const ControlledUnitRegistry& controlled,
    const pamguard::core::ModuleRegistry& runtime) {
    require_invalid_projection(
        base,
        controlled,
        runtime,
        [&](auto& document) {
            auto second = controlled_unit(
                controlled,
                "55555555-5555-4555-8555-555555555555",
                "pamguard.acquisition",
                "Second Acquisition");
            document.controlled_units.push_back(std::move(second));
            document.controlled_units[1]
                .bindings[0]
                .sources.push_back({
                    "55555555-5555-4555-8555-555555555555",
                    "rawAudio",
                });
        },
        "too-many-sources");

    require_invalid_projection(
        base,
        controlled,
        runtime,
        [&](auto& document) {
            auto duplicate = controlled_unit(
                controlled,
                "55555555-5555-4555-8555-555555555555",
                "pamguard.acquisition",
                "sound acquisition");
            document.controlled_units.push_back(
                std::move(duplicate));
        },
        "duplicate-java-class-instance-name");
}

ControlledUnitRegistry restricted_registry(
    const ControlledUnitRegistry& source,
    bool restrict_fft_to_normal,
    bool limit_acquisition_to_one) {
    ControlledUnitRegistry result;
    for (auto descriptor : source.controlled_units()) {
        if (restrict_fft_to_normal &&
            descriptor.id == "pamguard.fft") {
            descriptor.instance_rules.allowed_modes = {
                RunMode::Normal,
            };
        }
        if (limit_acquisition_to_one &&
            descriptor.id == "pamguard.acquisition") {
            descriptor.instance_rules.maximum_instances = 1;
        }
        result.register_controlled_unit(std::move(descriptor));
    }
    for (const auto& provider : source.display_providers()) {
        result.register_display_provider(provider);
    }
    for (const auto& global : source.global_settings()) {
        result.register_global_settings(global);
    }
    return result;
}

void check_run_mode_and_multiplicity(
    const ProjectDocument& base,
    const ControlledUnitRegistry& controlled,
    const pamguard::core::ModuleRegistry& runtime) {
    auto viewer = base;
    viewer.mode = ProjectMode::Viewer;
    const auto mode_registry =
        restricted_registry(controlled, true, false);
    const auto mode_projection =
        project_document_to_runtime_graph(
            viewer,
            mode_registry,
            runtime);
    require(
        !mode_projection.editor_valid() &&
            has_issue(
                mode_projection,
                "unsupported-controlled-unit-run-mode",
                ProjectionIssueClass::EditorInvalid),
        "Projection ignored controlled-unit run-mode restrictions");

    auto too_many = base;
    too_many.controlled_units.push_back(controlled_unit(
        controlled,
        "55555555-5555-4555-8555-555555555555",
        "pamguard.acquisition",
        "Second Acquisition"));
    const auto limit_registry =
        restricted_registry(controlled, false, true);
    const auto limit_projection =
        project_document_to_runtime_graph(
            too_many,
            limit_registry,
            runtime);
    require(
        !limit_projection.editor_valid() &&
            has_issue(
                limit_projection,
                "controlled-unit-multiplicity",
                ProjectionIssueClass::EditorInvalid),
        "Projection ignored controlled-unit instance limits");
}

void check_sound_output_run_mode_instance_rules(
    const ProjectDocument& base,
    const ControlledUnitRegistry& controlled,
    const pamguard::core::ModuleRegistry& runtime) {
    const auto sound_output =
        [&](std::string id, std::string name) {
            auto unit = controlled_unit(
                controlled,
                id,
                "pamguard.sound-output",
                name);
            unit.bindings.push_back({
                "audio",
                {{kAcquisitionId, "rawAudio"}},
            });
            auto settings = json::parse(unit.settings_json);
            settings["channelBitmap"] = 3;
            unit.settings_json = settings.dump();
            return unit;
        };

    auto viewer_without_output = base;
    viewer_without_output.mode = ProjectMode::Viewer;
    const auto missing_viewer_output =
        project_document_to_runtime_graph(
            viewer_without_output,
            controlled,
            runtime);
    require(
        !missing_viewer_output.editor_valid() &&
            has_issue(
                missing_viewer_output,
                "controlled-unit-multiplicity",
                ProjectionIssueClass::EditorInvalid),
        "Viewer projection accepted zero Sound Outputs");

    auto viewer_with_one = viewer_without_output;
    viewer_with_one.controlled_units.push_back(
        sound_output(
            "77777777-7777-4777-8777-777777777777",
            "Sound Output"));
    const auto exact_viewer_output =
        project_document_to_runtime_graph(
            viewer_with_one,
            controlled,
            runtime);
    require(
        exact_viewer_output.editor_valid(),
        "Viewer projection rejected exactly one Sound Output");

    auto normal_with_two = base;
    normal_with_two.controlled_units.push_back(
        sound_output(
            "77777777-7777-4777-8777-777777777777",
            "Sound Output"));
    normal_with_two.controlled_units.push_back(
        sound_output(
            "88888888-8888-4888-8888-888888888888",
            "Sound Output 2"));
    const auto normal_projection =
        project_document_to_runtime_graph(
            normal_with_two,
            controlled,
            runtime);
    require(
        normal_projection.editor_valid(),
        "Normal projection did not retain unlimited Sound Outputs");

    auto mixed_with_two = normal_with_two;
    mixed_with_two.mode = ProjectMode::Mixed;
    const auto mixed_projection =
        project_document_to_runtime_graph(
            mixed_with_two,
            controlled,
            runtime);
    require(
        mixed_projection.editor_valid(),
        "Mixed projection did not retain unlimited Sound Outputs");

    auto viewer_with_two = normal_with_two;
    viewer_with_two.mode = ProjectMode::Viewer;
    const auto excess_viewer_output =
        project_document_to_runtime_graph(
            viewer_with_two,
            controlled,
            runtime);
    require(
        !excess_viewer_output.editor_valid() &&
            has_issue(
                excess_viewer_output,
                "controlled-unit-multiplicity",
                ProjectionIssueClass::EditorInvalid),
        "Viewer projection accepted more than one Sound Output");
}

} // namespace

int main() {
    try {
        const auto controlled = controlled_registry();
        const auto runtime = runtime_registry();
        const auto document = example_project(controlled);

        const auto projection =
            project_document_to_runtime_graph(
                document,
                controlled,
                runtime);
        check_exact_graph(projection, runtime);
        check_round_trip_and_stability(
            document,
            controlled,
            runtime);
        check_window_and_channel_mapping(
            document,
            controlled,
            runtime);
        check_acquisition_portable_settings(
            document,
            controlled,
            runtime);
        check_needs_configuration(
            document,
            controlled,
            runtime);
        check_unbound_display_is_allowed(
            document,
            controlled,
            runtime);
        check_independent_spectrogram_displays(
            document,
            controlled,
            runtime);
        check_invalid_projects(
            document,
            controlled,
            runtime);
        check_cardinality_and_names(
            document,
            controlled,
            runtime);
        check_run_mode_and_multiplicity(
            document,
            controlled,
            runtime);
        check_sound_output_run_mode_instance_rules(
            document,
            controlled,
            runtime);

        std::cout
            << "Project projection produced deterministic first-slice runtime graph and ownership\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
