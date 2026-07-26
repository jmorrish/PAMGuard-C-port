#include <algorithm>
#include <any>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>

#include <json.hpp>

#include "pamguard/core/BuiltInModules.h"
#include "pamguard/core/FftDetectorNodes.h"
#include "pamguard/core/ModuleRuntime.h"
#include "pamguard/core/NoiseLtsaSettings.h"
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
constexpr auto kNoiseId =
    "33333333-3333-4333-8333-333333333333";
constexpr auto kNoiseBandId =
    "44444444-4444-4444-8444-444444444444";
constexpr auto kLtsaId =
    "55555555-5555-4555-8555-555555555555";

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

Json read_json(const std::string& path) {
    std::ifstream input(path);
    require(
        static_cast<bool>(input),
        "Could not open Java defaults fixture: " + path);
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
    catch (const pamguard::core::NoiseLtsaSettingsError&) {
        rejected = true;
    }
    require(rejected, message);
}

void check_defaults_and_strict_settings(const Json& fixture) {
    const auto& expected =
        fixture.at("portableSettingsDefaults");
    const auto noise_default = Json::parse(
        pamguard::core::fft_noise_monitor_default_settings_json());
    const auto noise_band_default = Json::parse(
        pamguard::core::noise_band_monitor_default_settings_json());
    const auto ltsa_default = Json::parse(
        pamguard::core::ltsa_default_settings_json());
    require(
        noise_default == expected.at("noiseMonitor") &&
            noise_band_default ==
                expected.at("noiseBandMonitor") &&
            ltsa_default == expected.at("ltsa"),
        "Portable defaults differ from the pinned Java exporter");

    const auto noise =
        pamguard::core::fft_noise_monitor_settings_from_json(
            noise_default.dump(),
            1);
    const auto noise_band =
        pamguard::core::noise_band_monitor_settings_from_json(
            noise_band_default.dump(),
            1);
    const auto ltsa = pamguard::core::ltsa_settings_from_json(
        ltsa_default.dump(),
        1);
    require(
        Json::parse(
            pamguard::core::fft_noise_monitor_settings_to_json(
                noise,
                1)) == noise_default &&
            Json::parse(
                pamguard::core::noise_band_monitor_settings_to_json(
                    noise_band,
                    1)) == noise_band_default &&
            Json::parse(
                pamguard::core::ltsa_settings_to_json(
                    ltsa,
                    1)) == ltsa_default,
        "Noise/LTSA defaults do not round-trip canonically");

    const auto& display_defaults =
        fixture.at("excludedDisplayDefaults")
            .at("noiseBandMonitor.NoiseBandSettings");
    for (const auto& [name, _] : display_defaults.items()) {
        require(
            !noise_band_default.contains(name),
            "Noise Band display preference leaked into portable science: " +
                name);
    }

    auto malformed = noise_default;
    malformed["unknown"] = true;
    require_settings_rejected(
        [&] {
            (void) pamguard::core::
                fft_noise_monitor_settings_from_json(
                    malformed.dump(),
                    1);
        },
        "Noise Monitor accepted an unknown field");
    malformed = noise_default;
    malformed["bands"].push_back({
        {"name", "bad"},
        {"lowFrequencyHz", 1000},
        {"highFrequencyHz", 100},
        {"bandType", nullptr},
    });
    require_settings_rejected(
        [&] {
            (void) pamguard::core::
                fft_noise_monitor_settings_from_json(
                    malformed.dump(),
                    1);
        },
        "Noise Monitor accepted a reversed measurement band");
    malformed = noise_default;
    malformed["channelBitmap"] =
        std::numeric_limits<std::uint64_t>::max();
    require_settings_rejected(
        [&] {
            (void) pamguard::core::
                fft_noise_monitor_settings_from_json(
                    malformed.dump(),
                    1);
        },
        "Noise Monitor accepted a channel bitmap wider than Java's int");
    malformed = noise_default;
    malformed["measurementIntervalSeconds"] =
        std::numeric_limits<std::uint64_t>::max();
    require_settings_rejected(
        [&] {
            (void) pamguard::core::
                fft_noise_monitor_settings_from_json(
                    malformed.dump(),
                    1);
        },
        "Noise Monitor did not reject an out-of-range unsigned interval through its typed settings error");

    auto malformed_band = noise_band_default;
    malformed_band["iirOrder"] = 5;
    require_settings_rejected(
        [&] {
            (void) pamguard::core::
                noise_band_monitor_settings_from_json(
                    malformed_band.dump(),
                    1);
        },
        "Noise Band Monitor accepted Java-dialog-invalid odd IIR order");
    malformed_band = noise_band_default;
    malformed_band["filterType"] = "chebyshev";
    require_settings_rejected(
        [&] {
            (void) pamguard::core::
                noise_band_monitor_settings_from_json(
                    malformed_band.dump(),
                    1);
        },
        "Noise Band Monitor accepted a filter type its Java control never constructs");

    auto malformed_ltsa = ltsa_default;
    malformed_ltsa["intervalSeconds"] = 0;
    require_settings_rejected(
        [&] {
            (void) pamguard::core::ltsa_settings_from_json(
                malformed_ltsa.dump(),
                1);
        },
        "LTSA accepted a non-positive interval");

    auto fir = noise_band_default;
    fir["filterType"] = "firWindow";
    fir["minimumFrequencyHz"] = 1000;
    fir["maximumFrequencyHz"] = 2000;
    fir["outputIntervalSeconds"] = 1;
    const auto fir_settings =
        pamguard::core::noise_band_monitor_settings_from_json(
            fir.dump(),
            1);
    pamguard::detectors::NoiseBandConfig fir_config;
    fir_config.enabled = true;
    fir_config.band_type = fir_settings.band_type;
    fir_config.filter_type =
        pamguard::dsp::IirFilterType::FirWindow;
    fir_config.fir_order = fir_settings.fir_order;
    fir_config.fir_gamma = fir_settings.fir_gamma;
    fir_config.min_frequency_hz =
        fir_settings.minimum_frequency_hz;
    fir_config.max_frequency_hz =
        fir_settings.maximum_frequency_hz;
    fir_config.reference_frequency_hz =
        fir_settings.reference_frequency_hz;
    fir_config.output_interval_seconds =
        fir_settings.output_interval_seconds;
    require(
        pamguard::detectors::NoiseBandMonitor(
            48000.0,
            fir_config).valid(),
        "Java FIR Window Noise Band filter bank is not implemented");
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
        "Noise and LTSA",
        "Java-authoritative controlled-unit projection",
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
        "FFT Engine");
    auto noise = controlled_unit(
        registry,
        kNoiseId,
        "pamguard.fft-noise-monitor",
        "Noise Monitor");
    auto noise_band = controlled_unit(
        registry,
        kNoiseBandId,
        "pamguard.noise-band-monitor",
        "Noise Band Monitor");
    auto ltsa = controlled_unit(
        registry,
        kLtsaId,
        "pamguard.ltsa",
        "LTSA");

    fft.bindings.push_back({
        "rawAudio",
        {{kAcquisitionId, "rawAudio"}},
    });
    noise.bindings.push_back({
        "fft",
        {{kFftId, "fft"}},
    });
    noise_band.bindings.push_back({
        "rawAudio",
        {{kAcquisitionId, "rawAudio"}},
    });
    ltsa.bindings.push_back({
        "fft",
        {{kFftId, "fft"}},
    });
    project.controlled_units = {
        std::move(acquisition),
        std::move(fft),
        std::move(noise),
        std::move(noise_band),
        std::move(ltsa),
    };
    return project;
}

void check_descriptors_projection_and_runtime() {
    ControlledUnitRegistry controlled;
    pamguard::project::register_builtin_controlled_units(controlled);
    pamguard::core::ModuleRegistry runtime_types;
    pamguard::core::register_builtin_module_types(runtime_types);

    const auto* noise = controlled.find_controlled_unit(
        "pamguard.fft-noise-monitor");
    const auto* noise_band = controlled.find_controlled_unit(
        "pamguard.noise-band-monitor");
    const auto* ltsa = controlled.find_controlled_unit(
        "pamguard.ltsa");
    require(
        noise && noise_band && ltsa,
        "Noise/LTSA controlled descriptors are absent");
    require(
        noise->java_authority.registered_name ==
                "Noise Monitor" &&
            noise_band->java_authority.registered_name ==
                "Noise Band Monitor" &&
            ltsa->java_authority.registered_name ==
                "Long Term Spectral Average" &&
            noise->runtime_recipe.children.at(0)
                    .settings.adapter_id ==
                "pamguard.fft-noise-settings.v1" &&
            noise_band->runtime_recipe.children.at(0)
                    .settings.adapter_id ==
                "pamguard.noise-band-settings.v1" &&
            ltsa->runtime_recipe.children.at(0)
                    .settings.adapter_id ==
                "pamguard.ltsa-settings.v1",
        "Noise/LTSA descriptor identities or adapters changed");
    require(
        ltsa->public_roles.at(0).data_type == "pamguard.fft" &&
            ltsa->public_roles.at(0).java_data_class ==
                "PamDetection.RawDataUnit",
        "LTSA did not preserve the declared Java dependency quirk while following its real FFT process");

    std::vector<pamguard::project::LowLevelTypeContract> contracts;
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
        contracts.push_back(std::move(converted));
    }
    const auto compatibility =
        controlled.validate_against(contracts);
    std::string registry_issues;
    for (const auto& issue : compatibility.issues) {
        registry_issues +=
            " [" + issue.code + "] " + issue.message;
    }
    require(
        compatibility.valid(),
        "Noise/LTSA recipes fail runtime compatibility:" +
            registry_issues);
    require(
        !pamguard::project::controlled_unit_catalogue_to_json(
             controlled).empty(),
        "Catalogue serialization omitted Noise/LTSA");

    auto project = project_fixture(controlled);
    const auto defaults =
        pamguard::project::project_document_to_runtime_graph(
            project,
            controlled,
            runtime_types);
    require(
        defaults.editor_valid() &&
            !defaults.runnable() &&
            defaults.needs_configuration() &&
            has_issue(
                defaults,
                "noise-monitor-no-measurement-bands",
                kNoiseId) &&
            has_issue(
                defaults,
                "ltsa-no-channels",
                kLtsaId),
        "Exact Java incomplete defaults are not needs-configuration");

    auto noise_settings = Json::parse(
        project.controlled_units[2].settings_json);
    noise_settings["bands"].push_back({
        {"name", "100-500 Hz"},
        {"lowFrequencyHz", 100.0},
        {"highFrequencyHz", 500.0},
        {"bandType", nullptr},
    });
    noise_settings["measurementIntervalSeconds"] = 1;
    project.controlled_units[2].settings_json =
        noise_settings.dump();

    auto noise_band_settings = Json::parse(
        project.controlled_units[3].settings_json);
    noise_band_settings["outputIntervalSeconds"] = 1;
    noise_band_settings["minimumFrequencyHz"] = 100.0;
    noise_band_settings["maximumFrequencyHz"] = 2000.0;
    project.controlled_units[3].settings_json =
        noise_band_settings.dump();

    auto ltsa_settings = Json::parse(
        project.controlled_units[4].settings_json);
    ltsa_settings["channelBitmap"] = 1;
    ltsa_settings["intervalSeconds"] = 1;
    project.controlled_units[4].settings_json =
        ltsa_settings.dump();

    const auto projection =
        pamguard::project::project_document_to_runtime_graph(
            project,
            controlled,
            runtime_types);
    std::string projection_issues;
    for (const auto& issue : projection.issues) {
        projection_issues +=
            " [" + issue.code + "] " + issue.message;
    }
    require(
        projection.runnable(),
        "Configured Noise/LTSA project did not project:" +
            projection_issues);

    const auto* acquisition_node =
        projection.index.find_runtime_node(
            kAcquisitionId,
            "acquisition");
    const auto* fft_node =
        projection.index.find_runtime_node(
            kFftId,
            "fft-process");
    const auto* noise_node =
        projection.index.find_runtime_node(
            kNoiseId,
            "noise-process");
    const auto* noise_band_node =
        projection.index.find_runtime_node(
            kNoiseBandId,
            "noise-band-process");
    const auto* ltsa_node =
        projection.index.find_runtime_node(
            kLtsaId,
            "ltsa-process");
    require(
        acquisition_node && fft_node && noise_node &&
            noise_band_node && ltsa_node,
        "Projected Noise/LTSA child ownership is incomplete");

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
    const auto projected_noise = Json::parse(
        find_module(noise_node->runtime_node_id)->settings_json);
    require(
        !projected_noise.contains("fftLength") &&
            !projected_noise.contains("fftHop") &&
            projected_noise.at("channels") ==
                Json::array({0}),
        "Noise Monitor adapter persisted duplicate FFT geometry or lost channel selection");
    const auto projected_ltsa = Json::parse(
        find_module(ltsa_node->runtime_node_id)->settings_json);
    require(
        !projected_ltsa.contains("longerFactor") &&
            projected_ltsa.at("channelBitmap") == 1,
        "LTSA adapter did not separate dormant persisted state from runtime science");

    pamguard::core::ModuleRuntime runtime;
    runtime.configure(projection.graph);
    const auto fft_block = runtime.find_block(
        pamguard::core::ModuleRuntime::block_id(
            fft_node->runtime_node_id,
            "fft"));
    const auto noise_output = runtime.find_block(
        pamguard::core::ModuleRuntime::block_id(
            noise_node->runtime_node_id,
            "measurements"));
    const auto noise_band_output = runtime.find_block(
        pamguard::core::ModuleRuntime::block_id(
            noise_band_node->runtime_node_id,
            "measurements"));
    const auto ltsa_output = runtime.find_block(
        pamguard::core::ModuleRuntime::block_id(
            ltsa_node->runtime_node_id,
            "ltsa"));
    require(
        fft_block && noise_output && noise_band_output &&
            ltsa_output &&
            fft_block->descriptor().fft_length == 1024 &&
            fft_block->descriptor().fft_hop == 512 &&
            noise_output->descriptor().channel_bitmap == 1 &&
            noise_band_output->descriptor().channel_bitmap == 1 &&
            ltsa_output->descriptor().channel_bitmap == 1,
        "Runtime blocks lost authoritative FFT/channel metadata");

    std::size_t noise_publications = 0;
    std::size_t noise_band_publications = 0;
    std::size_t ltsa_publications = 0;
    bool saw_calibrated_noise = false;
    auto noise_subscription = noise_output->subscribe(
        [&](const pamguard::core::DataUnit& unit) {
            const auto* period =
                std::any_cast<
                    pamguard::detectors::FftNoisePeriod>(
                    &unit.payload);
            require(
                period && !period->bands.empty(),
                "Noise Monitor published the wrong payload");
            saw_calibrated_noise =
                std::isfinite(period->bands[0].mean) &&
                period->bands[0].mean != 0.0;
            ++noise_publications;
        });
    auto noise_band_subscription =
        noise_band_output->subscribe(
            [&](const pamguard::core::DataUnit& unit) {
                require(
                    std::any_cast<
                        pamguard::core::NoiseBandMeasurement>(
                        &unit.payload) != nullptr,
                    "Noise Band Monitor published the wrong payload");
                ++noise_band_publications;
            });
    auto ltsa_subscription = ltsa_output->subscribe(
        [&](const pamguard::core::DataUnit& unit) {
            require(
                std::any_cast<
                    pamguard::core::LtsaChannelInterval>(
                    &unit.payload) != nullptr,
                "LTSA published the wrong payload");
            ++ltsa_publications;
        });

    pamguard::core::AudioChunk input;
    input.start_sample = 0;
    input.time_unix_ms = 1000;
    input.sample_rate_hz = 48000;
    input.channel_count = 2;
    constexpr std::size_t frames = 50176;
    input.interleaved_pcm.resize(frames * 2);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto time = static_cast<double>(frame) / 48000.0;
        input.interleaved_pcm[frame * 2] =
            0.25 * std::sin(
                2.0 * std::numbers::pi * 250.0 * time);
        input.interleaved_pcm[frame * 2 + 1] =
            0.1 * std::sin(
                2.0 * std::numbers::pi * 600.0 * time);
    }
    runtime.start();
    runtime.ingest(
        acquisition_node->runtime_node_id,
        std::move(input));
    runtime.stop();
    require(
        noise_publications > 0 &&
            noise_band_publications > 0 &&
            ltsa_publications > 0 &&
            saw_calibrated_noise,
        "Projected Noise/LTSA runtime did not publish calibrated interval data");
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr
            << "Usage: noise_ltsa_controlled_units_check "
               "<settings-defaults.json>\n";
        return 2;
    }
    try {
        const auto fixture = read_json(argv[1]);
        require(
            fixture.at("authority").at("commit") ==
                "dca55c81ef6f1498a8a3b926c69e7182afb915ee",
            "Noise/LTSA fixture authority commit changed");
        check_defaults_and_strict_settings(fixture);
        check_descriptors_projection_and_runtime();
        std::cout
            << "Noise Monitor, Noise Band Monitor, and LTSA "
               "Java-authoritative controlled units validated\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
