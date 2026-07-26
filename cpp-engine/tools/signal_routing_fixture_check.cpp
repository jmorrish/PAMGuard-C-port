#include <algorithm>
#include <any>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <json.hpp>

#include "pamguard/core/BuiltInModules.h"
#include "pamguard/core/ModuleRuntime.h"
#include "pamguard/core/SignalRoutingSettings.h"
#include "pamguard/project/BuiltInControlledUnits.h"
#include "pamguard/project/ControlledUnitJson.h"
#include "pamguard/project/ProjectProjection.h"

namespace {

using Json = nlohmann::json;

constexpr std::size_t kChannels = 4;
constexpr std::size_t kFrames = 9;
constexpr std::uint32_t kInputBitmap = 15;

struct FixtureCase {
    std::string module;
    std::uint32_t input_bitmap = 0;
    std::uint32_t output_bitmap = 0;
    std::map<std::size_t, std::vector<double>> channel_samples;
};

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> result;
    std::string field;
    for (const char character : line) {
        if (character == ',') {
            result.push_back(std::move(field));
            field.clear();
        }
        else {
            field.push_back(character);
        }
    }
    result.push_back(std::move(field));
    return result;
}

std::map<std::string, FixtureCase> read_fixture(
    const std::string& path) {
    std::ifstream input(path);
    require(
        static_cast<bool>(input),
        "Could not open signal-routing fixture: " + path);
    std::map<std::string, FixtureCase> result;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line.rfind("case,", 0) == 0) {
            continue;
        }
        const auto fields = split_csv(line);
        require(fields.size() == 7, "Invalid signal-routing fixture row");
        auto& fixture_case = result[fields[0]];
        const auto input_bitmap =
            static_cast<std::uint32_t>(std::stoul(fields[2]));
        const auto output_bitmap =
            static_cast<std::uint32_t>(std::stoul(fields[3]));
        if (fixture_case.module.empty()) {
            fixture_case.module = fields[1];
            fixture_case.input_bitmap = input_bitmap;
            fixture_case.output_bitmap = output_bitmap;
        }
        require(
            fixture_case.module == fields[1] &&
                fixture_case.input_bitmap == input_bitmap &&
                fixture_case.output_bitmap == output_bitmap,
            "Signal-routing fixture case metadata is inconsistent");
        const auto channel =
            static_cast<std::size_t>(std::stoul(fields[4]));
        const auto frame =
            static_cast<std::size_t>(std::stoul(fields[5]));
        auto& samples = fixture_case.channel_samples[channel];
        require(
            frame == samples.size(),
            "Signal-routing fixture frames are not ordered");
        samples.push_back(std::stod(fields[6]));
    }
    require(result.size() == 5, "Signal-routing fixture case count changed");
    return result;
}

double input_sample(std::size_t channel, std::size_t frame) {
    const auto stepped =
        static_cast<double>(
            static_cast<int>((frame + channel) % 3) - 1) *
        0.05;
    return static_cast<double>(frame + 1) * 0.125 +
        static_cast<double>(channel) * 0.3 + stepped;
}

Json amplifier_settings(bool configured) {
    auto settings = Json::parse(
        pamguard::core::signal_amplifier_default_settings_json());
    if (configured) {
        settings["channelSettings"][0] = {
            {"gainDb", 6.0},
            {"invert", false},
        };
        settings["channelSettings"][1] = {
            {"gainDb", -3.0},
            {"invert", true},
        };
        settings["channelSettings"][2] = {
            {"gainDb", 12.5},
            {"invert", false},
        };
        settings["channelSettings"][3] = {
            {"gainDb", 0.0},
            {"invert", true},
        };
    }
    return settings;
}

void clear_routes(Json& settings) {
    for (auto& row : settings.at("routingMatrix")) {
        for (auto& route : row) {
            route = false;
        }
    }
}

Json patch_settings(const std::string& fixture_name) {
    auto settings = Json::parse(
        pamguard::core::patch_panel_default_settings_json());
    if (fixture_name == "patch-identity") {
        return settings;
    }
    clear_routes(settings);
    if (fixture_name == "patch-route-mix-duplicate") {
        settings["routingMatrix"][0][2] = true;
        settings["routingMatrix"][1][0] = true;
        settings["routingMatrix"][1][2] = true;
        settings["routingMatrix"][2][2] = true;
        settings["routingMatrix"][3][1] = true;
        settings["routingMatrix"][3][7] = true;
        return settings;
    }
    require(
        fixture_name == "patch-advanced-gains",
        "Unknown Patch Panel fixture case");
    Json matrix = Json::array();
    for (std::size_t input = 0; input < 32; ++input) {
        matrix.push_back(Json::array());
        for (std::size_t output = 0; output < 32; ++output) {
            matrix.back().push_back(0.0);
        }
    }
    matrix[0][0] = 0.5;
    matrix[1][0] = -0.25;
    matrix[2][3] = 2.0;
    matrix[3][3] = 0.125;
    settings["advancedGainMatrix"] = std::move(matrix);
    return settings;
}

Json settings_for_case(const std::string& fixture_name) {
    if (fixture_name == "amplifier-default") {
        return amplifier_settings(false);
    }
    if (fixture_name == "amplifier-db-invert") {
        return amplifier_settings(true);
    }
    return patch_settings(fixture_name);
}

pamguard::core::AudioChunk input_audio() {
    pamguard::core::AudioChunk chunk;
    chunk.start_sample = 480;
    chunk.time_unix_ms = 123456;
    chunk.sample_rate_hz = 48000;
    chunk.channel_count = kChannels;
    chunk.interleaved_pcm.reserve(kFrames * kChannels);
    for (std::size_t frame = 0; frame < kFrames; ++frame) {
        for (std::size_t channel = 0; channel < kChannels; ++channel) {
            chunk.interleaved_pcm.push_back(
                input_sample(channel, frame));
        }
    }
    return chunk;
}

pamguard::core::ModuleGraphDocument runtime_document(
    const FixtureCase& fixture_case,
    const Json& settings) {
    const auto runtime_type =
        fixture_case.module == "amplifier"
        ? "pamguard.amplifier"
        : "pamguard.patch-panel";
    return {
        1,
        1,
        {
            {
                "source",
                "pamguard.acquisition",
                "Java fixture input",
                true,
                R"({"sourceId":"signal-routing-fixture","sampleRateHz":48000,"channelCount":4,"subtractDC":false,"dcTimeConstantSeconds":1,"calibrationDbOffsetByChannel":[100,101,102,103]})",
            },
            {
                "transform",
                runtime_type,
                "Java fixture transform",
                true,
                settings.dump(),
            },
        },
        {
            {
                "source-to-transform",
                {"source", "audio"},
                {"transform", "input"},
            },
        },
    };
}

std::vector<double> expected_calibration(
    const std::string& fixture_name) {
    constexpr double offsets[]{100.0, 101.0, 102.0, 103.0};
    if (fixture_name == "amplifier-default") {
        return {100.0, 101.0, 102.0, 103.0};
    }
    if (fixture_name == "amplifier-db-invert") {
        return {
            offsets[0] - 6.0,
            offsets[1] + 3.0,
            offsets[2] - 12.5,
            offsets[3],
        };
    }
    if (fixture_name == "patch-identity") {
        return {100.0, 101.0, 102.0, 103.0};
    }
    if (fixture_name == "patch-route-mix-duplicate") {
        return {
            offsets[1],
            offsets[3],
            offsets[0],
            0.0,
            0.0,
            0.0,
            0.0,
            offsets[3],
        };
    }
    return {
        offsets[0] - 20.0 * std::log10(0.5),
        0.0,
        0.0,
        offsets[2] - 20.0 * std::log10(2.0),
    };
}

void check_fixture_case(
    const std::string& name,
    const FixtureCase& fixture_case,
    double& maximum_error,
    std::size_t& compared_samples) {
    require(
        fixture_case.input_bitmap == kInputBitmap,
        "Java fixture input bitmap changed");
    pamguard::core::ModuleRuntime runtime;
    runtime.configure(runtime_document(
        fixture_case,
        settings_for_case(name)));
    const auto output = runtime.find_block(
        pamguard::core::ModuleRuntime::block_id(
            "transform",
            "output"));
    require(
        output != nullptr,
        "Signal-routing runtime omitted its output block");
    require(
        output->descriptor().channel_bitmap ==
            fixture_case.output_bitmap,
        name + " output channel bitmap differs from Java");

    const auto calibration = expected_calibration(name);
    require(
        output->descriptor().calibration_db_offset_by_channel.size() ==
            calibration.size(),
        name + " calibration channel count differs");
    for (std::size_t channel = 0;
         channel < calibration.size();
         ++channel) {
        require(
            std::abs(
                output->descriptor()
                        .calibration_db_offset_by_channel[channel] -
                calibration[channel]) < 1e-11,
            name + " first-source calibration differs");
    }

    std::size_t publications = 0;
    auto subscription = output->subscribe(
        [&](const pamguard::core::DataUnit& unit) {
            ++publications;
            const auto* audio =
                std::any_cast<pamguard::core::AudioChunk>(&unit.payload);
            require(
                audio != nullptr,
                name + " output payload is not raw audio");
            require(
                audio->start_sample == 480,
                name + " output start sample differs");
            require(
                audio->time_unix_ms == 123456,
                name + " output time differs");
            require(
                audio->sample_rate_hz == 48000,
                name + " output sample rate differs");
            require(
                audio->frame_count() == kFrames,
                name + " output duration differs");
            require(
                unit.metadata.start_sample == 480,
                name + " metadata start sample differs");
            require(
                unit.metadata.duration_samples == kFrames,
                name + " metadata duration differs");
            require(
                unit.metadata.channel_bitmap ==
                    fixture_case.output_bitmap,
                name + " metadata channel bitmap differs");
            for (std::size_t channel = 0;
                 channel < audio->channel_count;
                 ++channel) {
                const auto expected =
                    fixture_case.channel_samples.find(channel);
                const bool active =
                    (fixture_case.output_bitmap &
                     (std::uint32_t{1} << channel)) != 0;
                require(
                    active == (expected !=
                               fixture_case.channel_samples.end()),
                    name + " fixture active-channel rows differ");
                for (std::size_t frame = 0;
                     frame < kFrames;
                     ++frame) {
                    const double expected_value =
                        active ? expected->second.at(frame) : 0.0;
                    const double error = std::abs(
                        audio->sample(frame, channel) -
                        expected_value);
                    maximum_error =
                        std::max(maximum_error, error);
                    require(
                        error <= 1e-12,
                        name + " sample differs from pinned Java");
                    ++compared_samples;
                }
            }
        });
    runtime.start();
    runtime.ingest("source", input_audio());
    runtime.stop();
    require(publications == 1, name + " did not publish exactly once");
}

template <typename Operation>
void require_settings_rejected(
    Operation operation,
    const std::string& message) {
    bool rejected = false;
    try {
        operation();
    }
    catch (const pamguard::core::SignalRoutingSettingsError&) {
        rejected = true;
    }
    require(rejected, message);
}

void check_settings_contracts() {
    const auto amplifier_schema = Json::parse(
        pamguard::core::signal_amplifier_settings_schema_json());
    const auto patch_schema = Json::parse(
        pamguard::core::patch_panel_settings_schema_json());
    require(
        amplifier_schema.at("x-pamguard-authority").at("commit") ==
                "dca55c81ef6f1498a8a3b926c69e7182afb915ee" &&
            patch_schema.at("x-pamguard-authority").at("commit") ==
                "dca55c81ef6f1498a8a3b926c69e7182afb915ee" &&
            amplifier_schema.at("x-pamguard-portable-deviations")
                    .size() == 3 &&
            patch_schema.at("x-pamguard-portable-deviations")
                    .size() == 6 &&
            patch_schema.at("properties")
                    .at("advancedGainMatrix")
                    .at("x-pamguard-advanced") == true,
        "Signal-routing authority/deviation metadata changed");
    const auto amplifier =
        pamguard::core::signal_amplifier_settings_from_json(
            pamguard::core::signal_amplifier_default_settings_json(),
            1);
    for (std::size_t channel = 0; channel < 32; ++channel) {
        require(
            amplifier.signed_linear_gain(channel) == 1.0,
            "Signal Amplifier default is not Java unity gain");
    }
    auto malformed_amplifier = amplifier_settings(false);
    malformed_amplifier["channelSettings"].erase(
        malformed_amplifier["channelSettings"].end() - 1);
    require_settings_rejected(
        [&] {
            (void) pamguard::core::signal_amplifier_settings_from_json(
                malformed_amplifier.dump(),
                1);
        },
        "Signal Amplifier accepted 31 channel settings");
    malformed_amplifier = amplifier_settings(false);
    malformed_amplifier["channelGains"] = Json::array({1.0});
    require_settings_rejected(
        [&] {
            (void) pamguard::core::signal_amplifier_settings_from_json(
                malformed_amplifier.dump(),
                1);
        },
        "Signal Amplifier accepted the superseded C++ field");
    malformed_amplifier = amplifier_settings(false);
    malformed_amplifier["channelSettings"][0]["gainDb"] = 1e308;
    require_settings_rejected(
        [&] {
            (void) pamguard::core::signal_amplifier_settings_from_json(
                malformed_amplifier.dump(),
                1);
        },
        "Signal Amplifier accepted an unrepresentable linear gain");

    const auto patch =
        pamguard::core::patch_panel_settings_from_json(
            pamguard::core::patch_panel_default_settings_json(),
            1);
    for (std::size_t input = 0; input < 32; ++input) {
        for (std::size_t output = 0; output < 32; ++output) {
            require(
                patch.coefficient(input, output) ==
                    (input == output ? 1.0 : 0.0),
                "Patch Panel default is not Java's identity matrix");
        }
    }
    auto malformed_patch = patch_settings("patch-identity");
    malformed_patch["routingMatrix"][0].erase(
        malformed_patch["routingMatrix"][0].end() - 1);
    require_settings_rejected(
        [&] {
            (void) pamguard::core::patch_panel_settings_from_json(
                malformed_patch.dump(),
                1);
        },
        "Patch Panel accepted a 31-column route row");
    malformed_patch = patch_settings("patch-identity");
    malformed_patch["patches"] = Json::array();
    require_settings_rejected(
        [&] {
            (void) pamguard::core::patch_panel_settings_from_json(
                malformed_patch.dump(),
                1);
        },
        "Patch Panel accepted the superseded C++ field");

    const auto advanced =
        pamguard::core::patch_panel_settings_from_json(
            patch_settings("patch-advanced-gains").dump(),
            1);
    require(
        advanced.advanced_gain_matrix.has_value() &&
            advanced.coefficient(0, 0) == 0.5 &&
            advanced.coefficient(1, 0) == -0.25 &&
            advanced.coefficient(1, 1) == 0.0,
        "Patch Panel Advanced gain matrix did not explicitly override "
        "canonical routes");
}

pamguard::project::ControlledUnitInstance project_unit(
    const pamguard::project::ControlledUnitRegistry& registry,
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

void check_controlled_unit_projection() {
    pamguard::project::ControlledUnitRegistry controlled;
    pamguard::project::register_builtin_controlled_units(controlled);
    const auto* amplifier =
        controlled.find_controlled_unit("pamguard.amplifier");
    const auto* patch =
        controlled.find_controlled_unit("pamguard.patch-panel");
    require(
        amplifier && patch &&
            amplifier->public_roles.size() == 2 &&
            patch->public_roles.size() == 2 &&
            amplifier->public_roles[0].id == "rawAudio" &&
            amplifier->public_roles[1].id == "amplifiedAudio" &&
            patch->public_roles[0].id == "rawAudio" &&
            patch->public_roles[1].id == "patchedAudio" &&
            amplifier->runtime_recipe.children.size() == 1 &&
            patch->runtime_recipe.children.size() == 1 &&
            amplifier->runtime_recipe.children[0]
                    .settings.adapter_id == "identity.v1" &&
            patch->runtime_recipe.children[0]
                    .settings.adapter_id == "identity.v1",
        "Signal-routing controlled-unit public roles/recipes differ");
    require(
        controlled.validate().valid(),
        "Signal-routing descriptors fail structural validation");
    const auto catalogue =
        pamguard::project::controlled_unit_catalogue_to_json(controlled);
    require(
        !catalogue.empty(),
        "Signal-routing controlled settings schemas/defaults are invalid");

    pamguard::core::ModuleRegistry runtime_registry;
    pamguard::core::register_builtin_module_types(runtime_registry);
    pamguard::project::ProjectDocument project;
    project.project_id =
        "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
    project.metadata = {
        "Signal routing projection",
        "Pinned Java amplifier and Patch Panel",
    };
    project.descriptor_set = {
        "pamguard-2.02.18e",
        1,
    };
    const auto* array_manager =
        controlled.find_global_settings("pamguard.array-manager");
    require(
        array_manager != nullptr,
        "Array Manager descriptor is absent");
    project.global_settings.components.push_back({
        array_manager->id,
        array_manager->settings.version,
        array_manager->settings.default_settings_json,
    });
    auto acquisition = project_unit(
        controlled,
        "11111111-1111-4111-8111-111111111111",
        "pamguard.acquisition",
        "Sound Acquisition");
    auto amp = project_unit(
        controlled,
        "22222222-2222-4222-8222-222222222222",
        "pamguard.amplifier",
        "Signal Amplifier");
    auto panel = project_unit(
        controlled,
        "33333333-3333-4333-8333-333333333333",
        "pamguard.patch-panel",
        "Patch Panel");
    amp.bindings.push_back({
        "rawAudio",
        {{acquisition.id, "rawAudio"}},
    });
    panel.bindings.push_back({
        "rawAudio",
        {{amp.id, "amplifiedAudio"}},
    });
    project.controlled_units = {
        std::move(acquisition),
        std::move(amp),
        std::move(panel),
    };
    const auto projection =
        pamguard::project::project_document_to_runtime_graph(
            project,
            controlled,
            runtime_registry);
    std::string projection_issue_text;
    for (const auto& issue : projection.issues) {
        projection_issue_text +=
            " [" + issue.code + "] " + issue.message;
    }
    require(
        projection.editor_valid() &&
            projection.graph.modules.size() == 3 &&
            projection.graph.connections.size() == 2 &&
            projection.index.find_public_output(
                project.controlled_units[1].id,
                "amplifiedAudio") &&
            projection.index.find_public_output(
                project.controlled_units[2].id,
                "patchedAudio"),
        "Signal-routing source bindings did not project to the runtime graph" +
            projection_issue_text);

    auto invalid_project = project;
    invalid_project.controlled_units[1].settings_json =
        R"({"channelGains":[1]})";
    const auto invalid_projection =
        pamguard::project::project_document_to_runtime_graph(
            invalid_project,
            controlled,
            runtime_registry);
    require(
        !invalid_projection.editor_valid() &&
            std::any_of(
                invalid_projection.issues.begin(),
                invalid_projection.issues.end(),
                [](const auto& issue) {
                    return issue.code == "invalid-settings";
                }),
        "Controlled-unit projection accepted superseded amplifier settings");
}

void check_no_route_suppresses_output() {
    auto settings = Json::parse(
        pamguard::core::patch_panel_default_settings_json());
    clear_routes(settings);
    FixtureCase no_routes;
    no_routes.module = "patch-panel";
    no_routes.input_bitmap = kInputBitmap;
    no_routes.output_bitmap = 0;
    pamguard::core::ModuleRuntime runtime;
    runtime.configure(runtime_document(no_routes, settings));
    const auto output = runtime.find_block(
        pamguard::core::ModuleRuntime::block_id(
            "transform",
            "output"));
    require(
        output && output->descriptor().channel_bitmap == 0,
        "Empty Patch Panel routes did not produce an empty channel map");
    std::size_t publications = 0;
    auto subscription = output->subscribe(
        [&](const auto&) { ++publications; });
    runtime.start();
    runtime.ingest("source", input_audio());
    runtime.stop();
    require(
        publications == 0,
        "Patch Panel published audio despite having no output routes");
}

void check_sparse_absolute_channel_semantics() {
    auto patch = Json::parse(
        pamguard::core::patch_panel_default_settings_json());
    clear_routes(patch);
    patch["routingMatrix"][0][2] = true;
    patch["routingMatrix"][3][7] = true;
    auto amplifier = amplifier_settings(false);
    amplifier["channelSettings"][2]["gainDb"] = 6.0;
    amplifier["channelSettings"][7]["gainDb"] = -3.0;
    amplifier["channelSettings"][7]["invert"] = true;

    pamguard::core::ModuleGraphDocument document{
        1,
        1,
        {
            {
                "source",
                "pamguard.acquisition",
                "Sparse channel input",
                true,
                R"({"sourceId":"sparse-signal-routing","sampleRateHz":48000,"channelCount":4,"subtractDC":false,"dcTimeConstantSeconds":1,"calibrationDbOffsetByChannel":[100,101,102,103]})",
            },
            {
                "panel",
                "pamguard.patch-panel",
                "Sparse Patch Panel",
                true,
                patch.dump(),
            },
            {
                "amplifier",
                "pamguard.amplifier",
                "Absolute-channel amplifier",
                true,
                amplifier.dump(),
            },
        },
        {
            {
                "source-to-panel",
                {"source", "audio"},
                {"panel", "input"},
            },
            {
                "panel-to-amplifier",
                {"panel", "output"},
                {"amplifier", "input"},
            },
        },
    };
    pamguard::core::ModuleRuntime runtime;
    runtime.configure(std::move(document));
    const auto output = runtime.find_block(
        pamguard::core::ModuleRuntime::block_id(
            "amplifier",
            "output"));
    require(
        output != nullptr &&
            output->descriptor().channel_bitmap ==
                ((std::uint32_t{1} << 2) |
                 (std::uint32_t{1} << 7)),
        "Sparse Patch Panel channel map was not preserved by Amplifier");
    const auto& calibration =
        output->descriptor().calibration_db_offset_by_channel;
    require(
        calibration.size() == 8 &&
            std::abs(calibration[2] - 94.0) < 1e-11 &&
            std::abs(calibration[7] - 106.0) < 1e-11,
        "Sparse absolute-channel calibration used positional channel gains");

    std::size_t publications = 0;
    auto subscription = output->subscribe(
        [&](const pamguard::core::DataUnit& unit) {
            ++publications;
            const auto* audio =
                std::any_cast<pamguard::core::AudioChunk>(&unit.payload);
            require(
                audio != nullptr && audio->channel_count == 8,
                "Sparse routed AudioChunk does not retain absolute indexes");
            const double gain2 = std::pow(10.0, 6.0 / 20.0);
            const double gain7 = -std::pow(10.0, -3.0 / 20.0);
            for (std::size_t frame = 0; frame < kFrames; ++frame) {
                for (std::size_t channel = 0; channel < 8; ++channel) {
                    double expected = 0.0;
                    if (channel == 2) {
                        expected = input_sample(0, frame) * gain2;
                    }
                    else if (channel == 7) {
                        expected = input_sample(3, frame) * gain7;
                    }
                    require(
                        std::abs(audio->sample(frame, channel) -
                                 expected) <= 1e-12,
                        "Sparse routing used packed instead of absolute "
                        "channel indexes");
                }
            }
        });
    runtime.start();
    runtime.ingest("source", input_audio());
    runtime.stop();
    require(
        publications == 1,
        "Sparse Patch Panel/Amplifier chain did not publish once");
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr
            << "Usage: signal_routing_fixture_check <signal-routing.csv>\n";
        return 2;
    }
    try {
        check_settings_contracts();
        check_controlled_unit_projection();
        const auto fixtures = read_fixture(argv[1]);
        double maximum_error = 0.0;
        std::size_t compared_samples = 0;
        for (const auto& [name, fixture_case] : fixtures) {
            check_fixture_case(
                name,
                fixture_case,
                maximum_error,
                compared_samples);
        }
        check_no_route_suppresses_output();
        check_sparse_absolute_channel_semantics();
        std::cout
            << "Signal Amplifier/Patch Panel matched pinned PAMGuard "
               "2.02.18e across "
            << fixtures.size() << " cases and "
            << compared_samples << " interleaved samples; max error "
            << maximum_error
            << ". Canonical settings, source bindings, calibration, "
               "empty routing, and Advanced gains validated.\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
