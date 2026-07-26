#include <algorithm>
#include <any>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <json.hpp>

#include "pamguard/core/BuiltInModules.h"
#include "pamguard/core/FilterDecimatorSettings.h"
#include "pamguard/core/ModuleRuntime.h"
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
constexpr auto kFilterId =
    "22222222-2222-4222-8222-222222222222";
constexpr auto kDecimatorId =
    "33333333-3333-4333-8333-333333333333";

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
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

template <typename Operation>
void require_settings_rejected(
    Operation operation,
    const std::string& message) {
    bool rejected = false;
    try {
        operation();
    }
    catch (const pamguard::core::
               FilterDecimatorSettingsError&) {
        rejected = true;
    }
    require(rejected, message);
}

void check_exact_defaults_and_round_trip() {
    const auto filter_default_json =
        pamguard::core::
            standalone_filter_default_settings_json();
    const auto decimator_default_json =
        pamguard::core::decimator_default_settings_json();
    const auto filter_default =
        pamguard::core::standalone_filter_settings_from_json(
            filter_default_json,
            1);
    const auto decimator_default =
        pamguard::core::decimator_settings_from_json(
            decimator_default_json,
            1);

    require(
        filter_default.channel_bitmap == 0 &&
            filter_default.filter.type ==
                pamguard::dsp::IirFilterType::Butterworth &&
            filter_default.filter.band ==
                pamguard::dsp::IirFilterBand::BandPass &&
            filter_default.filter.order == 4 &&
            filter_default.filter.low_pass_freq_hz ==
                20000.0F &&
            filter_default.filter.high_pass_freq_hz ==
                2000.0F &&
            filter_default.filter.pass_band_ripple_db ==
                2.0 &&
            filter_default.filter.stop_band_ripple_db ==
                2.0 &&
            filter_default.filter.cheby_gamma == 3.0 &&
            filter_default.filter
                .arbitrary_frequencies_hz.empty() &&
            filter_default.filter
                .arbitrary_gains_db.empty(),
        "Standalone Filter defaults differ from FilterParameters_2/FilterParams");
    require(
        decimator_default.output_sample_rate_hz == 2000 &&
            decimator_default.channel_bitmap == 0 &&
            decimator_default.interpolation == 0 &&
            decimator_default.filter.type ==
                pamguard::dsp::IirFilterType::Butterworth &&
            decimator_default.filter.band ==
                pamguard::dsp::IirFilterBand::LowPass &&
            decimator_default.filter.order == 6 &&
            decimator_default.filter.low_pass_freq_hz ==
                1000.0F &&
            decimator_default.filter.high_pass_freq_hz ==
                2000.0F,
        "Decimator defaults differ from DecimatorParams");
    require(
        Json::parse(
            pamguard::core::
                standalone_filter_settings_to_json(
                    filter_default,
                    1)) ==
                Json::parse(filter_default_json) &&
            Json::parse(
                pamguard::core::decimator_settings_to_json(
                    decimator_default,
                    1)) ==
                Json::parse(decimator_default_json),
        "Filter/Decimator default settings do not round-trip");

    auto custom_filter = Json::parse(filter_default_json);
    custom_filter["channelBitmap"] = 5;
    custom_filter["type"] = "chebyshev";
    custom_filter["band"] = "highPass";
    custom_filter["order"] = 6;
    custom_filter["highPassFreqHz"] = 1234.5;
    custom_filter["passBandRippleDb"] = 1.25;
    const auto decoded_filter =
        pamguard::core::standalone_filter_settings_from_json(
            custom_filter.dump(),
            1);
    const auto normalized_filter = Json::parse(
        pamguard::core::standalone_filter_settings_to_json(
            decoded_filter,
            1));
    require(
        normalized_filter.at("channelBitmap") == 5 &&
            normalized_filter.at("type") == "chebyshev" &&
            normalized_filter.at("band") == "highPass" &&
            normalized_filter.at("order") == 6 &&
            normalized_filter.at("highPassFreqHz") ==
                static_cast<double>(
                    static_cast<float>(1234.5)) &&
            normalized_filter.at("passBandRippleDb") == 1.25,
        "Standalone Filter custom settings were not normalized deterministically");

    auto custom_decimator =
        Json::parse(decimator_default_json);
    custom_decimator["outputSampleRateHz"] = 12000;
    custom_decimator["channelBitmap"] = 3;
    custom_decimator["interpolation"] = 2;
    custom_decimator["filter"]["lowPassFreqHz"] = 6000;
    const auto decoded_decimator =
        pamguard::core::decimator_settings_from_json(
            custom_decimator.dump(),
            1);
    require(
        Json::parse(
            pamguard::core::decimator_settings_to_json(
                decoded_decimator,
                1)) == custom_decimator,
        "Decimator custom settings were not normalized deterministically");
}

void check_strict_rejection() {
    auto filter = Json::parse(
        pamguard::core::
            standalone_filter_default_settings_json());
    filter["unknown"] = true;
    require_settings_rejected(
        [&] {
            (void) pamguard::core::
                standalone_filter_settings_from_json(
                    filter.dump(),
                    1);
        },
        "Filter accepted an unknown setting");

    filter = Json::parse(
        pamguard::core::
            standalone_filter_default_settings_json());
    filter.erase("order");
    require_settings_rejected(
        [&] {
            (void) pamguard::core::
                standalone_filter_settings_from_json(
                    filter.dump(),
                    1);
        },
        "Filter accepted an incomplete setting object");

    filter = Json::parse(
        pamguard::core::
            standalone_filter_default_settings_json());
    filter["order"] = 3;
    require_settings_rejected(
        [&] {
            (void) pamguard::core::
                standalone_filter_settings_from_json(
                    filter.dump(),
                    1);
        },
        "Filter accepted Java-dialog-invalid odd IIR order");

    filter = Json::parse(
        pamguard::core::
            standalone_filter_default_settings_json());
    filter["type"] = "firArbitrary";
    filter["order"] = 5;
    filter["arbitraryFrequenciesHz"] =
        Json::array({0, 1000, 2000});
    filter["arbitraryGainsDb"] =
        Json::array({0, -20});
    require_settings_rejected(
        [&] {
            (void) pamguard::core::
                standalone_filter_settings_from_json(
                    filter.dump(),
                    1);
        },
        "Filter accepted mismatched arbitrary FIR arrays");

    filter = Json::parse(
        pamguard::core::
            standalone_filter_default_settings_json());
    filter["arbitraryFrequenciesHz"] =
        Json::array({-1});
    require_settings_rejected(
        [&] {
            (void) pamguard::core::
                standalone_filter_settings_from_json(
                    filter.dump(),
                    1);
        },
        "Filter accepted schema-invalid negative arbitrary frequency");

    auto decimator = Json::parse(
        pamguard::core::decimator_default_settings_json());
    decimator["outputSampleRateHz"] = 0;
    require_settings_rejected(
        [&] {
            (void) pamguard::core::
                decimator_settings_from_json(
                    decimator.dump(),
                    1);
        },
        "Decimator accepted zero output sample rate");

    decimator = Json::parse(
        pamguard::core::decimator_default_settings_json());
    decimator["outputSampleRateHz"] = 2000.5;
    require_settings_rejected(
        [&] {
            (void) pamguard::core::
                decimator_settings_from_json(
                    decimator.dump(),
                    1);
        },
        "Decimator accepted an unrepresentable fractional output rate");

    decimator = Json::parse(
        pamguard::core::decimator_default_settings_json());
    decimator["interpolation"] = 3;
    require_settings_rejected(
        [&] {
            (void) pamguard::core::
                decimator_settings_from_json(
                    decimator.dump(),
                    1);
        },
        "Decimator accepted interpolation outside None/Linear/Quadratic");
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
        "Controlled-unit descriptor is absent");
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
        "Filter and Decimator",
        "Java-authoritative controlled-unit projection",
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
    auto filter = controlled_unit(
        registry,
        kFilterId,
        "pamguard.filter",
        "Filters (IIR and FIR)");
    auto decimator = controlled_unit(
        registry,
        kDecimatorId,
        "pamguard.decimator",
        "Decimator");
    filter.bindings.push_back({
        "rawAudio",
        {{kAcquisitionId, "rawAudio"}},
    });
    decimator.bindings.push_back({
        "rawAudio",
        {{kFilterId, "filteredAudio"}},
    });
    project.controlled_units = {
        std::move(acquisition),
        std::move(filter),
        std::move(decimator),
    };
    return project;
}

void check_catalogue_projection_and_runtime() {
    ControlledUnitRegistry controlled;
    pamguard::project::register_builtin_controlled_units(
        controlled);
    pamguard::core::ModuleRegistry runtime_types;
    pamguard::core::register_builtin_module_types(
        runtime_types);

    const auto* filter =
        controlled.find_controlled_unit(
            "pamguard.filter");
    const auto* decimator =
        controlled.find_controlled_unit(
            "pamguard.decimator");
    const auto* runtime_filter =
        runtime_types.find("pamguard.filter");
    const auto* runtime_decimator =
        runtime_types.find("pamguard.decimator");
    require(
        filter && decimator &&
            runtime_filter && runtime_decimator,
        "Filter/Decimator descriptors are absent");
    require(
        Json::parse(filter->settings.default_settings_json) ==
                Json::parse(runtime_filter->default_settings_json) &&
            Json::parse(
                decimator->settings.default_settings_json) ==
                Json::parse(
                    runtime_decimator->default_settings_json) &&
            Json::parse(filter->settings.settings_schema_json) ==
                Json::parse(
                    runtime_filter->settings_schema_json) &&
            Json::parse(
                decimator->settings.settings_schema_json) ==
                Json::parse(
                    runtime_decimator->settings_schema_json),
        "Controlled-unit and low-level settings contracts diverge");
    std::vector<pamguard::project::LowLevelTypeContract>
        contracts;
    for (const auto& type : runtime_types.list()) {
        pamguard::project::LowLevelTypeContract converted;
        converted.id = type.id;
        for (const auto& port : type.ports) {
            converted.ports.push_back({
                port.id,
                port.direction ==
                        pamguard::core::PortDirection::Input
                    ? pamguard::project::
                          DataRoleDirection::Input
                    : pamguard::project::
                          DataRoleDirection::Output,
                port.data_type,
                port.capabilities,
            });
        }
        contracts.push_back(std::move(converted));
    }
    require(
        controlled.validate_against(contracts).valid(),
        "Filter/Decimator recipes fail low-level compatibility");
    require(
        !pamguard::project::
            controlled_unit_catalogue_to_json(
                controlled).empty(),
        "Controlled-unit catalogue omitted Filter/Decimator");

    auto project = project_fixture(controlled);
    const auto needs_channels =
        pamguard::project::
            project_document_to_runtime_graph(
                project,
                controlled,
                runtime_types);
    require(
        needs_channels.editor_valid() &&
            !needs_channels.runnable() &&
            needs_channels.needs_configuration() &&
            has_issue(
                needs_channels,
                "filter-no-channels",
                kFilterId) &&
            has_issue(
                needs_channels,
                "decimator-no-channels",
                kDecimatorId),
        "Exact zero-channel defaults are not exposed as needs-configuration");

    auto filter_settings = Json::parse(
        project.controlled_units[1].settings_json);
    auto decimator_settings = Json::parse(
        project.controlled_units[2].settings_json);
    filter_settings["channelBitmap"] = 3;
    decimator_settings["channelBitmap"] = 3;
    project.controlled_units[1].settings_json =
        filter_settings.dump();
    project.controlled_units[2].settings_json =
        decimator_settings.dump();

    const auto projection =
        pamguard::project::
            project_document_to_runtime_graph(
                project,
                controlled,
                runtime_types);
    std::string issues;
    for (const auto& issue : projection.issues) {
        issues += " [" + issue.code + "] " +
            issue.message;
    }
    require(
        projection.runnable() &&
            projection.graph.modules.size() == 3 &&
            projection.graph.connections.size() == 2 &&
            projection.index.find_public_output(
                kFilterId,
                "filteredAudio") &&
            projection.index.find_public_output(
                kDecimatorId,
                "decimatedAudio"),
        "Configured Filter/Decimator did not project: " +
            issues);

    const auto* filter_node =
        projection.index.find_runtime_node(
            kFilterId,
            "filter-process");
    const auto* decimator_node =
        projection.index.find_runtime_node(
            kDecimatorId,
            "decimator-process");
    require(
        filter_node && decimator_node,
        "Filter/Decimator runtime child ownership is absent");
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
                "Projected runtime module is absent");
            return found;
        };
    require(
        Json::parse(
            find_module(filter_node->runtime_node_id)
                ->settings_json) == filter_settings &&
            Json::parse(
                find_module(decimator_node->runtime_node_id)
                    ->settings_json) == decimator_settings,
        "Explicit Filter/Decimator adapters changed canonical settings");

    pamguard::core::ModuleRuntime runtime;
    runtime.configure(projection.graph);
    const auto* acquisition_node =
        projection.index.find_runtime_node(
            kAcquisitionId,
            "acquisition");
    require(
        acquisition_node != nullptr,
        "Projected Acquisition runtime child is absent");
    const auto output = runtime.find_block(
        pamguard::core::ModuleRuntime::block_id(
            decimator_node->runtime_node_id,
            "output"));
    require(
        output &&
            output->descriptor().sample_rate_hz == 2000 &&
            output->descriptor().channel_bitmap == 3,
        "Decimator runtime output contract is wrong");

    std::size_t publications = 0;
    auto subscription = output->subscribe(
        [&](const pamguard::core::DataUnit& unit) {
            const auto* audio =
                std::any_cast<
                    pamguard::core::AudioChunk>(
                    &unit.payload);
            require(
                audio &&
                    audio->sample_rate_hz == 2000 &&
                    unit.metadata.channel_bitmap == 3,
                "Decimator publication lost sample-rate/channel metadata");
            ++publications;
        });
    pamguard::core::AudioChunk input;
    input.start_sample = 0;
    input.time_unix_ms = 1000;
    input.sample_rate_hz = 48000;
    input.channel_count = 2;
    input.interleaved_pcm.resize(2048 * 2);
    for (std::size_t frame = 0;
         frame < 2048;
         ++frame) {
        input.interleaved_pcm[frame * 2] =
            (frame % 23) / 23.0;
        input.interleaved_pcm[frame * 2 + 1] =
            (frame % 31) / 31.0;
    }
    runtime.start();
    runtime.ingest(
        acquisition_node->runtime_node_id,
        std::move(input));
    runtime.stop();
    require(
        publications > 0,
        "Projected Filter/Decimator runtime produced no audio");
}

} // namespace

int main() {
    try {
        check_exact_defaults_and_round_trip();
        check_strict_rejection();
        check_catalogue_projection_and_runtime();
        std::cout
            << "Filter/Decimator Java settings, project adapters, "
               "and runtime wiring validated\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
