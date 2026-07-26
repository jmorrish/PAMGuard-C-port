#include <algorithm>
#include <any>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <json.hpp>

#include "pamguard/core/BuiltInModules.h"
#include "pamguard/core/DetectorNodes.h"
#include "pamguard/core/MhtClickTrainSettings.h"
#include "pamguard/project/BuiltInControlledUnits.h"
#include "pamguard/project/ControlledUnitJson.h"
#include "pamguard/project/ProjectAuthorityJson.h"
#include "pamguard/project/ProjectProjection.h"

namespace {

using Json = nlohmann::json;
using pamguard::project::ControlledUnitInstance;
using pamguard::project::ControlledUnitRegistry;
using pamguard::project::ProjectDocument;

constexpr auto kAcquisitionId =
    "11111111-1111-4111-8111-111111111111";
constexpr auto kClickDetectorId =
    "22222222-2222-4222-8222-222222222222";
constexpr auto kMhtId =
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
        "Could not open MHT Click Train Java fixture: " + path);
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
    catch (const pamguard::core::MhtClickTrainSettingsError&) {
        rejected = true;
    }
    require(rejected, message);
}

bool has_issue(
    const pamguard::project::ProjectProjectionResult& projection,
    const std::string& code) {
    return std::any_of(
        projection.issues.begin(),
        projection.issues.end(),
        [&](const auto& issue) {
            return issue.code == code &&
                issue.unit_id == kMhtId;
        });
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

void check_java_defaults_and_round_trip(const Json& fixture) {
    require(
        fixture.at("authority").at("version") ==
                "2.02.18e" &&
            fixture.at("authority").at("commit") ==
                "dca55c81ef6f1498a8a3b926c69e7182afb915ee" &&
            fixture.at("authority").at("exporter") ==
                "org.pamguard.port.reference."
                "MhtClickTrainSettingsFixtureExporter",
        "MHT Click Train fixture is not pinned to Java authority");

    const auto default_json =
        pamguard::core::mht_click_train_default_settings_json();
    require(
        Json::parse(default_json) ==
            fixture.at("portableSettingsDefaults"),
        "Portable MHT Click Train defaults differ from Java fixture");
    const auto defaults =
        pamguard::core::mht_click_train_settings_from_json(
            default_json,
            1);
    require(
        defaults.channel_groups ==
                std::vector<std::uint32_t>{1} &&
            !defaults.data_selector.enabled &&
            defaults.data_selector.use_echoes &&
            defaults.data_selector.minimum_amplitude_db == 0.0 &&
            defaults.data_selector.included_click_types.empty() &&
            defaults.kernel.n_hold == 20 &&
            defaults.kernel.n_pruneback == 4 &&
            defaults.kernel.n_pruneback_start == 5 &&
            defaults.kernel.max_coast == 3 &&
            defaults.chi2.maximum_ici_seconds == 0.4 &&
            defaults.chi2.coast_penalty == 10.0 &&
            defaults.chi2.new_track_penalty == 50.0 &&
            defaults.chi2.new_track_clicks == 3 &&
            defaults.chi2.idi.enabled &&
            defaults.chi2.amplitude.enabled &&
            defaults.chi2.bearing.enabled &&
            defaults.chi2.correlation.enabled &&
            defaults.chi2.time_delay.enabled &&
            defaults.chi2.length.enabled &&
            defaults.chi2.peak_frequency.enabled &&
            !defaults.classifier.run_classifier &&
            defaults.classifier.pre.chi2_threshold == 1500.0 &&
            defaults.classifier.pre.minimum_clicks == 5 &&
            !defaults.localisation.enabled &&
            defaults.localisation.minimum_data_units == 20,
        "Decoded MHT Click Train constructor defaults changed");
    require(
        Json::parse(
            pamguard::core::mht_click_train_settings_to_json(
                defaults,
                1)) == Json::parse(default_json),
        "MHT Click Train defaults do not round-trip");

    auto custom = Json::parse(default_json);
    custom["channelGroups"] = Json::array({3, 12});
    custom["dataSelector"]["enabled"] = true;
    custom["dataSelector"]["useEchoes"] = false;
    custom["dataSelector"]["includedClickTypes"] =
        Json::array({0, 7});
    custom["kernel"]["nHold"] = 30;
    custom["chi2"]["variables"]["idi"]["error"] = 0.3;
    custom["chi2"]["variables"]["amplitude"]["maximumJumpDb"] =
        12.0;
    custom["chi2"]["variables"]["bearing"]["jumpDirection"] =
        "both";
    custom["classifier"]["runClassifier"] = true;
    custom["classifier"]["idi"]["enabled"] = true;
    custom["classifier"]["bearing"]["enabled"] = true;
    custom["classifier"]["spectrumTemplate"]["enabled"] = true;
    const auto decoded =
        pamguard::core::mht_click_train_settings_from_json(
            custom.dump(),
            1);
    require(
        Json::parse(
            pamguard::core::mht_click_train_settings_to_json(
                decoded,
                1)) == custom,
        "Custom MHT Click Train settings changed during round-trip");

    const auto runtime = Json::parse(
        pamguard::core::mht_click_train_runtime_settings_json(
            decoded));
    require(
        runtime.at("minClicks") == 3 &&
            runtime.at("channelGroups") == custom.at("channelGroups") &&
            runtime.at("dataSelector") ==
                custom.at("dataSelector") &&
            runtime.at("kernel").at("nHold") == 30 &&
            runtime.at("chi2").at("idi").at("error") == 0.3 &&
            runtime.at("chi2")
                    .at("amplitude")
                    .at("maximumJumpDb") == 12.0 &&
            runtime.at("chi2")
                    .at("bearing")
                    .at("jumpDirection") == "both" &&
            runtime.at("classifier").at("enabled") &&
            runtime.at("classifier")
                    .at("template")
                    .at("templateSpectrum") ==
                custom.at("classifier")
                    .at("spectrumTemplate")
                    .at("spectrum"),
        "MHT Click Train runtime adapter lost scientific settings");

    const auto schema = Json::parse(
        pamguard::core::mht_click_train_settings_schema_json());
    require(
        !schema.at("additionalProperties").get<bool>() &&
            schema.at("x-pamguard-authority").at("commit") ==
                fixture.at("authority").at("commit") &&
            schema.at("x-pamguard-portable-deviations").is_array() &&
            schema.at("x-pamguard-portable-deviations").size() >=
                12 &&
            schema.at("properties")
                    .at("dataSelector")
                    .at("properties")
                    .at("minimumAmplitudeDb")
                    .at("const") == 0,
        "MHT Click Train schema lost strictness or deviation evidence");
}

void check_strict_rejection() {
    const auto defaults = [] {
        return Json::parse(
            pamguard::core::
                mht_click_train_default_settings_json());
    };
    const auto reject = [&](Json settings,
                            const std::string& message) {
        require_rejected(
            [&] {
                (void) pamguard::core::
                    mht_click_train_settings_from_json(
                        settings.dump(),
                        1);
            },
            message);
    };

    auto value = defaults();
    value["unknown"] = true;
    reject(value, "MHT settings accepted an unknown root field");

    value = defaults();
    value.erase("kernel");
    reject(value, "MHT settings accepted a missing root field");

    require_rejected(
        [&] {
            (void) pamguard::core::
                mht_click_train_settings_from_json(
                    defaults().dump(),
                    2);
        },
        "MHT settings accepted an unsupported version");

    value = defaults();
    value["channelGroups"] = Json::array({3, 2});
    reject(value, "MHT settings accepted overlapping channel groups");

    value = defaults();
    value["dataSelector"]["minimumAmplitudeDb"] = 1.0;
    reject(
        value,
        "MHT settings accepted unsupported calibrated-amplitude selection");

    value = defaults();
    value["classifier"]["preClassifier"]
         ["minimumSelectedPercentage"] = 0.5;
    reject(
        value,
        "MHT settings accepted an unimplemented pre-classifier selector");

    value = defaults();
    value["localisation"]["enabled"] = true;
    reject(
        value,
        "MHT settings accepted unimplemented target-motion localisation");

    value = defaults();
    value["chi2"]["variables"]["bearing"]["jumpDirection"] =
        "sideways";
    reject(value, "MHT settings accepted an unknown bearing direction");

    value = defaults();
    value["chi2"]["variables"]["idi"]["error"] = 0;
    reject(value, "MHT settings accepted a zero chi2 error");
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
    const ControlledUnitRegistry& registry,
    bool bind_scientific_inputs) {
    ProjectDocument project;
    project.project_id =
        "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
    project.metadata = {
        "MHT Click Train Detector",
        "Java-authoritative settings projection",
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
    auto click_detector = controlled_unit(
        registry,
        kClickDetectorId,
        "pamguard.click-detector",
        "Click Detector");
    auto mht = controlled_unit(
        registry,
        kMhtId,
        "pamguard.mht-click-train",
        "Click Train Detector");
    click_detector.bindings.push_back({
        "rawAudio",
        {{kAcquisitionId, "rawAudio"}},
    });
    mht.bindings.push_back({
        "clicks",
        {{kClickDetectorId, "clicks"}},
    });
    if (bind_scientific_inputs) {
        mht.bindings.push_back({
            "features",
            {{kClickDetectorId, "features"}},
        });
        mht.bindings.push_back({
            "localisations",
            {{kClickDetectorId, "localisations"}},
        });
        mht.bindings.push_back({
            "bearings",
            {{kClickDetectorId, "bearings"}},
        });
    }
    project.controlled_units = {
        std::move(acquisition),
        std::move(click_detector),
        std::move(mht),
    };
    const auto* click_display =
        registry.find_display_provider("pamguard.click-display");
    require(click_display, "Click display provider is absent");
    const std::string display_id =
        std::string{"display:"} + kClickDetectorId + ":click";
    pamguard::project::DisplayTab tab;
    tab.id = std::string{"tab:"} + kClickDetectorId + ":click";
    tab.name = "Click Detector";
    tab.owner = {kClickDetectorId, "clickDisplay"};
    tab.displays.push_back({
        display_id,
        click_display->id,
        click_display->descriptor_version,
        {kClickDetectorId, "clickDisplay"},
        pamguard::project::SourceReference{
            kClickDetectorId,
            "clicks",
        },
        click_display->settings.version,
        click_display->settings.default_settings_json,
    });
    tab.layout.mode =
        pamguard::project::DisplayLayoutMode::Grid;
    tab.layout.columns = 12;
    tab.layout.selected_display_id = display_id;
    tab.layout.items.push_back({
        display_id,
        0,
        0,
        12,
        8,
    });
    project.display_tabs.push_back(std::move(tab));
    return project;
}

void check_descriptor_and_project_projection() {
    ControlledUnitRegistry controlled;
    pamguard::project::register_builtin_controlled_units(
        controlled);
    pamguard::core::ModuleRegistry runtime_types;
    pamguard::core::register_builtin_module_types(runtime_types);
    const auto compatibility = controlled.validate_against(
        low_level_catalogue(runtime_types));
    std::string compatibility_issues;
    for (const auto& issue : compatibility.issues) {
        if (issue.descriptor_id == "pamguard.mht-click-train") {
            compatibility_issues +=
                " [" + issue.code + "] " + issue.message;
        }
    }
    require(
        compatibility_issues.empty(),
        "MHT controlled-unit recipe is incompatible with runtime:" +
            compatibility_issues);

    const auto* descriptor =
        controlled.find_controlled_unit(
            "pamguard.mht-click-train");
    require(
        descriptor &&
            descriptor->java_authority.registered_name ==
                "Click Train Detector" &&
            descriptor->java_authority.menu_group == "Detectors" &&
            descriptor->java_authority.class_name ==
                "clickTrainDetector.ClickTrainControl" &&
            descriptor->java_authority.tooltip ==
                "Searches for click trains in detected clicks." &&
            descriptor->settings.authority_classes.size() == 19 &&
            descriptor->settings.sections.size() == 1 &&
            descriptor->settings.sections[0].labels ==
                std::vector<std::string>{
                    "Detector",
                    "Pre Classifier",
                    "Species Classifiers",
                } &&
            descriptor->runtime_recipe.children.size() == 1 &&
            descriptor->runtime_recipe.children[0]
                    .settings.adapter_id ==
                "pamguard.mht-click-train-settings.v1" &&
            descriptor->runtime_recipe.public_role_mappings.size() ==
                6 &&
            descriptor->parity_status == "partial" &&
            !pamguard::project::controlled_unit_catalogue_to_json(
                 controlled).empty(),
        "MHT controlled-unit Java authority or recipe changed");

    const auto needs_inputs =
        pamguard::project::project_document_to_runtime_graph(
            project_fixture(controlled, false),
            controlled,
            runtime_types);
    std::string needs_input_issues;
    for (const auto& issue : needs_inputs.issues) {
        needs_input_issues +=
            " [" + issue.code + "] " + issue.message;
    }
    require(
        needs_inputs.editor_valid() &&
            needs_inputs.needs_configuration() &&
            has_issue(
                needs_inputs,
                "mht-click-train-missing-features") &&
            has_issue(
                needs_inputs,
                "mht-click-train-missing-localisations") &&
            has_issue(
                needs_inputs,
                "mht-click-train-missing-bearings"),
        "Exact Java MHT defaults did not request their scientific inputs:" +
            needs_input_issues);

    auto empty_groups = project_fixture(controlled, true);
    auto empty_settings = Json::parse(
        empty_groups.controlled_units[2].settings_json);
    empty_settings["channelGroups"] = Json::array();
    empty_groups.controlled_units[2].settings_json =
        empty_settings.dump();
    const auto needs_groups =
        pamguard::project::project_document_to_runtime_graph(
            empty_groups,
            controlled,
            runtime_types);
    require(
        needs_groups.editor_valid() &&
            needs_groups.needs_configuration() &&
            has_issue(
                needs_groups,
                "mht-click-train-no-channel-groups"),
        "MHT projection accepted zero channel groups as runnable");

    const auto projection =
        pamguard::project::project_document_to_runtime_graph(
            project_fixture(controlled, true),
            controlled,
            runtime_types);
    std::string issues;
    for (const auto& issue : projection.issues) {
        issues += " [" + issue.code + "] " + issue.message;
    }
    require(
        projection.runnable(),
        "Configured MHT controlled unit did not project: " + issues);
    const auto* runtime_node =
        projection.index.find_runtime_node(kMhtId, "detector");
    require(runtime_node, "MHT runtime child ownership is absent");
    const auto module = std::find_if(
        projection.graph.modules.begin(),
        projection.graph.modules.end(),
        [&](const auto& candidate) {
            return candidate.id == runtime_node->runtime_node_id;
        });
    require(
        module != projection.graph.modules.end(),
        "MHT projected runtime module is absent");
    const auto settings = Json::parse(module->settings_json);
    require(
        module->type_id == "pamguard.mht-click-train" &&
            settings.at("channelGroups") == Json::array({1}) &&
            settings.at("chi2").at("maxChi").is_number_integer() &&
            settings.at("chi2").at("maxChi") ==
                std::int64_t{200000000000000000LL} &&
            settings.at("chi2").at("enableBearing") &&
            settings.at("chi2").at("enablePeakFrequency") &&
            settings.at("chi2").at("enableTimeDelay") &&
            settings.at("chi2").at("idi").at("error") == 0.2 &&
            !settings.at("classifier").at("enabled") &&
            projection.index.find_public_output(kMhtId, "trains") &&
            projection.index.find_public_output(
                kMhtId,
                "classifications"),
        "MHT project adapter or public output mapping changed");

    pamguard::project::ActiveProjectSnapshot snapshot{
        project_fixture(controlled, true),
        projection,
        1,
        std::nullopt,
        1,
        "sha256:mht-working",
        std::nullopt,
        true,
        "\"mht-inspection\"",
    };
    const auto inspection = Json::parse(
        pamguard::project::project_inspection_to_json(snapshot));
    require(
        inspection.at("projection").at("status") == "runnable" &&
            inspection.at("projection").at("graph")
                .at("modules")
                .back()
                .at("settings")
                .at("chi2")
                .at("maxChi")
                .is_number_integer(),
        "MHT full project inspection lost the exact maxChi sentinel");
}

std::shared_ptr<pamguard::core::DataBlock> block(
    std::string id,
    std::string type) {
    pamguard::core::DataBlockDescriptor descriptor;
    descriptor.id = std::move(id);
    descriptor.name = descriptor.id;
    descriptor.producer_module_id = "fixture";
    descriptor.producer_port_id = "fixture";
    descriptor.data_type = std::move(type);
    descriptor.sample_rate_hz = 48000.0;
    descriptor.channel_bitmap = 3;
    return std::make_shared<pamguard::core::DataBlock>(
        std::move(descriptor));
}

struct NodeRun {
    std::vector<pamguard::core::GraphMhtClickTrainResult> trains;
    std::size_t classifications = 0;
    bool empty_before_stop = false;
};

NodeRun run_node(int selected_click_type) {
    auto clicks = block("clicks", pamguard::core::kClickDataType);
    auto trains = block(
        "trains",
        pamguard::core::kMhtClickTrainDataType);
    auto classifications = block(
        "classifications",
        pamguard::core::kClickTrainClassificationDataType);

    pamguard::core::MhtClickTrainNodeConfig config;
    config.sample_rate_hz = 48000.0;
    config.min_clicks = 3;
    config.channel_groups = {3};
    config.data_selector_enabled = true;
    config.data_selector_use_echoes = false;
    config.data_selector_included_click_types = {
        selected_click_type,
    };
    config.chi2.enable_idi = true;
    config.chi2.enable_amplitude = false;
    config.chi2.enable_length = false;
    config.chi2.enable_bearing = false;
    config.chi2.enable_peak_frequency = false;
    config.chi2.enable_time_delay = false;
    config.chi2.enable_correlation = false;
    config.classify = false;
    config.pre_classifier.chi2_threshold = 0.0;
    config.pre_classifier.min_clicks = 3;

    pamguard::core::MhtClickTrainNode node(
        "mht-fixture",
        std::move(config),
        {clicks, {}, {}, {}},
        {trains, classifications});
    NodeRun result;
    auto train_subscription = trains->subscribe(
        [&](const auto& unit) {
            const auto* train = std::any_cast<
                pamguard::core::GraphMhtClickTrainResult>(
                    &unit.payload);
            require(train, "MHT train payload type changed");
            result.trains.push_back(*train);
        });
    auto classification_subscription =
        classifications->subscribe(
            [&](const auto& unit) {
                const auto* classification = std::any_cast<
                    pamguard::core::
                        GraphClickTrainClassificationResult>(
                            &unit.payload);
                require(
                    classification,
                    "MHT classification payload type changed");
                ++result.classifications;
            });

    node.prepare();
    node.start();
    for (std::size_t index = 0; index < 8; ++index) {
        pamguard::detectors::ClickDetectionResult click;
        click.channel_bitmap = 1;
        click.trigger_bitmap = 1;
        click.start_sample =
            static_cast<std::int64_t>(index * 4800);
        click.duration_samples = 16;
        click.time_unix_ms =
            1000 + static_cast<std::int64_t>(index * 100);
        click.channels = {0};
        click.waveform = {
            std::vector<double>(16, index % 2 == 0 ? 1.0 : -1.0),
        };
        click.click_type = 7;
        click.echo = index == 3;

        pamguard::core::DataUnitMetadata metadata;
        metadata.start_sample = click.start_sample;
        metadata.time_unix_ms = click.time_unix_ms;
        metadata.channel_bitmap = 1;
        clicks->publish(pamguard::core::make_data_unit(
            std::move(metadata),
            std::move(click)));
    }
    result.empty_before_stop = result.trains.empty();
    node.stop();
    return result;
}

void check_group_selector_preclassifier_and_lifecycle() {
    const auto rejected = run_node(8);
    require(
        rejected.empty_before_stop &&
            rejected.trains.empty() &&
            rejected.classifications == 0,
        "MHT source click-type selector accepted excluded clicks");

    const auto accepted = run_node(7);
    std::size_t largest = 0;
    for (const auto& train : accepted.trains) {
        largest = std::max(largest, train.click_count);
        require(
            train.channel_bitmap == 3 &&
                train.classified &&
                !train.junk_train &&
                train.species_id == 0 &&
                train.classifier_species_ids.empty(),
            "MHT grouping or Java-always-active pre-classifier changed");
    }
    require(
        accepted.empty_before_stop &&
            !accepted.trains.empty() &&
            largest >= 6 &&
            largest <= 7 &&
            accepted.classifications ==
                accepted.trains.size(),
        "MHT processing-end flush, echo selector, or classification "
        "publication changed");
}

} // namespace

int main(int argc, char** argv) {
    try {
        require(
            argc == 2,
            "Usage: mht_click_train_settings_check "
            "<settings-defaults.json>");
        const auto fixture = read_json(argv[1]);
        check_java_defaults_and_round_trip(fixture);
        check_strict_rejection();
        check_descriptor_and_project_projection();
        check_group_selector_preclassifier_and_lifecycle();
        std::cout
            << "MHT Click Train Java defaults, strict settings, "
               "controlled-unit projection, grouping, selector, "
               "pre-classifier, and lifecycle passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
