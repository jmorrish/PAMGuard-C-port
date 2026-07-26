#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <json.hpp>

#include "pamguard/core/BuiltInModules.h"
#include "pamguard/project/BuiltInControlledUnits.h"
#include "pamguard/project/ProjectAuthorityJson.h"
#include "pamguard/project/ProjectJson.h"

namespace {

using Json = nlohmann::json;
using namespace pamguard::project;

constexpr const char* kProjectId =
    "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
constexpr const char* kAcquisitionId =
    "11111111-1111-4111-8111-111111111111";
constexpr const char* kFftId =
    "22222222-2222-4222-8222-222222222222";
constexpr const char* kUserDisplayId =
    "33333333-3333-4333-8333-333333333333";
constexpr const char* kDisplayId =
    "display:44444444-4444-4444-8444-444444444444";
constexpr const char* kTabId =
    "tab:33333333-3333-4333-8333-333333333333:main";

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::set<std::string> keys(const Json& object) {
    require(object.is_object(), "Expected a JSON object");
    std::set<std::string> result;
    for (auto entry = object.begin();
         entry != object.end();
         ++entry) {
        result.insert(entry.key());
    }
    return result;
}

void require_keys(
    const Json& object,
    std::initializer_list<std::string_view> expected,
    const std::string& context) {
    std::set<std::string> wanted;
    for (const auto value : expected) {
        wanted.emplace(value);
    }
    require(
        keys(object) == wanted,
        context + " has an unexpected JSON shape");
}

const Json& find_by_string(
    const Json& array,
    std::string_view field,
    std::string_view value,
    const std::string& context) {
    require(array.is_array(), context + " must be an array");
    const auto found = std::find_if(
        array.begin(),
        array.end(),
        [&](const Json& entry) {
            return entry.is_object() &&
                entry.value(
                    std::string(field),
                    std::string{}) == value;
        });
    require(
        found != array.end(),
        context + " omits '" + std::string(value) + "'");
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

ControlledUnitInstance unit(
    const ControlledUnitRegistry& registry,
    std::string id,
    const std::string& type_id,
    std::string name) {
    const auto* descriptor =
        registry.find_controlled_unit(type_id);
    require(descriptor != nullptr, "Missing controlled-unit fixture");
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

ProjectDocument fixture_project(
    const ControlledUnitRegistry& registry) {
    ProjectDocument project;
    project.project_id = kProjectId;
    project.metadata = {
        "Authority JSON fixture",
        "First-slice authority response contract",
    };
    project.descriptor_set = {"pamguard-2.02.18e", 1};
    const auto* array_manager =
        registry.find_global_settings("pamguard.array-manager");
    require(array_manager != nullptr, "Missing Array Manager fixture");
    project.global_settings.components.push_back({
        array_manager->id,
        array_manager->settings.version,
        array_manager->settings.default_settings_json,
    });

    auto acquisition = unit(
        registry,
        kAcquisitionId,
        "pamguard.acquisition",
        "Sound Acquisition");
    auto fft = unit(
        registry,
        kFftId,
        "pamguard.fft",
        "FFT (Spectrogram) Engine");
    fft.bindings.push_back({
        "rawAudio",
        {{kAcquisitionId, "rawAudio"}},
    });
    auto user_display = unit(
        registry,
        kUserDisplayId,
        "pamguard.user-display",
        "User Display");
    project.controlled_units = {
        std::move(acquisition),
        std::move(fft),
        std::move(user_display),
    };

    const auto* provider = registry.find_display_provider(
        "pamguard.spectrogram-display");
    require(provider != nullptr, "Missing Spectrogram provider");
    DisplayInstance display;
    display.id = kDisplayId;
    display.provider_type_id = provider->id;
    display.provider_version = provider->descriptor_version;
    display.owner = {kUserDisplayId, "provider"};
    display.source = SourceReference{kFftId, "fft"};
    display.settings_version = provider->settings.version;
    display.settings_json =
        provider->settings.default_settings_json;

    DisplayTab tab;
    tab.id = kTabId;
    tab.name = "User Display";
    tab.owner = {kUserDisplayId, "main"};
    tab.displays.push_back(std::move(display));
    tab.layout.columns = 12;
    tab.layout.selected_display_id = kDisplayId;
    tab.layout.items.push_back({
        kDisplayId,
        0,
        0,
        12,
        6,
    });
    project.display_tabs.push_back(std::move(tab));
    project.data_model_layout.nodes = {
        {kAcquisitionId, 80, 80},
        {kFftId, 360, 80},
        {kUserDisplayId, 640, 80},
    };
    return project_document_from_json(
        project_document_to_json(project));
}

ActiveProjectSnapshot fixture_snapshot(
    const ControlledUnitRegistry& controlled,
    const pamguard::core::ModuleRegistry& runtime) {
    auto project = fixture_project(controlled);
    auto projection = project_document_to_runtime_graph(
        project,
        controlled,
        runtime);
    require(projection.runnable(), "Fixture projection is not runnable");
    projection.graph.revision = 7;
    return {
        std::move(project),
        std::move(projection),
        7,
        6,
        11,
        "sha256:working",
        "sha256:saved",
        true,
        "\"project-authority-fixture\"",
    };
}

void check_snapshot(const ActiveProjectSnapshot& snapshot) {
    const auto compact =
        active_project_snapshot_to_json(snapshot);
    const auto repeated =
        active_project_snapshot_to_json(snapshot);
    const auto pretty =
        active_project_snapshot_to_json(snapshot, true);
    require(
        compact == repeated,
        "Snapshot JSON is not deterministic");
    const auto root = Json::parse(compact);
    require(
        Json::parse(pretty) == root,
        "Pretty snapshot JSON changed values");
    require_keys(
        root,
        {
            "schemaVersion",
            "project",
            "workingRevision",
            "savedRevision",
            "authorityRevision",
            "workingContentHash",
            "savedContentHash",
            "dirty",
            "etag",
            "projection",
        },
        "active snapshot");
    require(
        root.at("schemaVersion") == 1 &&
            root.at("project").at("projectId") == kProjectId &&
            root.at("workingRevision") == 7 &&
            root.at("savedRevision") == 6 &&
            root.at("authorityRevision") == 11 &&
            root.at("workingContentHash") == "sha256:working" &&
            root.at("savedContentHash") == "sha256:saved" &&
            root.at("dirty") == true &&
            root.at("etag") ==
                "\"project-authority-fixture\"" &&
            root.at("projection") ==
                Json({
                    {"status", "runnable"},
                    {"issues", Json::array()},
                }),
        "Active snapshot values/status changed");
    require(
        root.at("project") ==
            Json::parse(
                project_document_to_json(snapshot.project)),
        "Snapshot did not embed the exact project JSON value");

    auto incomplete = snapshot;
    incomplete.projection.issues.push_back({
        ProjectionIssueClass::NeedsConfiguration,
        "missing-required-binding",
        "FFT requires Raw audio source",
        kFftId,
        "rawAudio",
        {},
    });
    const auto issue_response = Json::parse(
        active_project_snapshot_to_json(incomplete));
    require(
        issue_response.at("projection").at("status") ==
                "needs-configuration" &&
            issue_response.at("projection")
                    .at("issues")
                    .at(0) ==
                Json({
                    {"class", "needs-configuration"},
                    {"code", "missing-required-binding"},
                    {"message", "FFT requires Raw audio source"},
                    {"unitId", kFftId},
                    {"roleId", "rawAudio"},
                    {"displayId", nullptr},
                }),
        "Projection issue status/context shape changed");
}

void check_inspection(const ActiveProjectSnapshot& snapshot) {
    const auto root =
        Json::parse(project_inspection_to_json(snapshot));
    require_keys(
        root,
        {
            "schemaVersion",
            "projectId",
            "workingRevision",
            "authorityRevision",
            "projection",
        },
        "inspection");
    require(
        root.at("schemaVersion") == 1 &&
            root.at("projectId") == kProjectId &&
            root.at("workingRevision") == 7 &&
            root.at("authorityRevision") == 11,
        "Inspection authority identity changed");
    const auto& projection = root.at("projection");
    require_keys(
        projection,
        {
            "status",
            "issues",
            "runtimeChildren",
            "dataBlocks",
            "publicOutputs",
            "publicInputs",
            "connections",
            "displayTabs",
            "displays",
            "graph",
        },
        "inspection projection");
    require(
        projection.at("status") == "runnable" &&
            projection.at("issues").empty() &&
            projection.at("runtimeChildren").size() == 3 &&
            projection.at("dataBlocks").size() == 3 &&
            projection.at("publicOutputs").size() == 3 &&
            projection.at("publicInputs").size() == 1 &&
            projection.at("connections").size() == 2 &&
            projection.at("displayTabs").size() == 1 &&
            projection.at("displays").size() == 1 &&
            projection.at("graph").at("revision") == 7 &&
            projection.at("graph").at("modules").size() == 3,
        "Full projection inspection counts changed");

    const auto& fft_child = find_by_string(
        projection.at("runtimeChildren"),
        "childRole",
        "fft-process",
        "runtime children");
    require(
        fft_child ==
            Json({
                {"ownerUnitId", kFftId},
                {"childRole", "fft-process"},
                {
                    "runtimeNodeId",
                    std::string("rt:") + kFftId +
                        ":fft-process",
                },
                {"runtimeTypeId", "pamguard.fft"},
            }),
        "Stable FFT runtime-child inspection changed");

    const auto& raw_audio = find_by_string(
        projection.at("publicOutputs"),
        "outputRole",
        "rawAudio",
        "public outputs");
    require(
        raw_audio.at("unitId") == kAcquisitionId &&
            raw_audio.at("blockId") ==
                std::string("block:rt:") + kAcquisitionId +
                    ":acquisition:audio" &&
            raw_audio.at("dataType") ==
                "pamguard.raw-audio",
        "Stable public-output/block identity changed");

    const auto external_id =
        projected_external_connection_id(
            kFftId,
            "rawAudio",
            kAcquisitionId,
            "rawAudio");
    const auto& external = find_by_string(
        projection.at("connections"),
        "id",
        external_id,
        "connections");
    require(
        external.at("kind") == "external" &&
            external.at("ownerUnitId") == kFftId &&
            external.at("internalEdgeRole").is_null() &&
            external.at("targetInputRole") == "rawAudio" &&
            external.at("publicSource") ==
                Json({
                    {"unitId", kAcquisitionId},
                    {"outputRole", "rawAudio"},
                }),
        "External connection ownership shape changed");

    require(
        projection.at("displayTabs").at(0) ==
                Json({
                    {"tabId", kTabId},
                    {"ownerUnitId", kUserDisplayId},
                    {"ownerRole", "main"},
                }) &&
            projection.at("displays").at(0) ==
                Json({
                    {"tabId", kTabId},
                    {"displayId", kDisplayId},
                    {"ownerUnitId", kUserDisplayId},
                    {"ownerRole", "provider"},
                    {
                        "providerTypeId",
                        "pamguard.spectrogram-display",
                    },
                    {
                        "publicSource",
                        {
                            {"unitId", kFftId},
                            {"outputRole", "fft"},
                        },
                    },
                    {
                        "sourceBlockId",
                        std::string("block:rt:") + kFftId +
                            ":fft-process:fft",
                    },
                }),
        "Display/tab ownership inspection changed");
    require(
        std::none_of(
            projection.at("graph").at("modules").begin(),
            projection.at("graph").at("modules").end(),
            [](const Json& module) {
                return module.at("typeId") ==
                        "pamguard.user-display" ||
                    module.at("typeId") ==
                        "pamguard.spectrogram-display";
            }),
        "Inspection exposed presentation owners as runtime children");
}

void check_other_responses(
    const ActiveProjectSnapshot& snapshot) {
    const auto fft_output = *snapshot.projection.index.find_public_output(
        kFftId,
        "fft");
    const auto compatible = Json::parse(
        project_compatible_sources_to_json(
            kUserDisplayId,
            "fft",
            {fft_output}));
    require_keys(
        compatible,
        {"schemaVersion", "target", "sources"},
        "compatible sources");
    require(
        compatible.at("target") ==
                Json({
                    {"unitId", kUserDisplayId},
                    {"inputRole", "fft"},
                }) &&
            compatible.at("sources").at(0).at("unitId") ==
                kFftId &&
            compatible.at("sources")
                    .at(0)
                    .at("outputRole") == "fft" &&
            compatible.at("sources")
                    .at(0)
                    .at("blockId") ==
                std::string("block:rt:") + kFftId +
                    ":fft-process:fft",
        "Compatible source response changed");

    const std::vector<SavedProjectSummary> summaries{
        {
            kProjectId,
            "Available project",
            "Ready",
            8,
            123456789,
            SavedProjectStatus::Available,
            {},
        },
        {
            "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb",
            {},
            {},
            0,
            0,
            SavedProjectStatus::Corrupt,
            "Invalid envelope",
        },
        {
            "cccccccc-cccc-4ccc-8ccc-cccccccccccc",
            {},
            {},
            0,
            0,
            SavedProjectStatus::Unsupported,
            "Future format",
        },
    };
    const auto saved = Json::parse(
        saved_project_list_to_json(summaries));
    require_keys(
        saved,
        {"schemaVersion", "projects"},
        "saved project list");
    require(
        saved.at("projects").size() == 3 &&
            saved.at("projects").at(0).at("status") ==
                "available" &&
            saved.at("projects").at(0).at("issue").is_null() &&
            saved.at("projects").at(1).at("status") ==
                "corrupt" &&
            saved.at("projects").at(1).at("issue") ==
                "Invalid envelope" &&
            saved.at("projects").at(2).at("status") ==
                "unsupported",
        "Saved project status/issue shape changed");

    ProjectMutationResult result;
    result.changed = true;
    result.validated_only = false;
    result.created_entities = {
        {"newFft", "dddddddd-dddd-4ddd-8ddd-dddddddddddd"},
    };
    result.active = snapshot;
    const auto mutation = Json::parse(
        project_mutation_result_to_json(result));
    require_keys(
        mutation,
        {
            "schemaVersion",
            "changed",
            "validatedOnly",
            "createdEntities",
            "active",
        },
        "mutation result");
    require(
        mutation.at("changed") == true &&
            mutation.at("validatedOnly") == false &&
            mutation.at("createdEntities") ==
                Json::array({
                    {
                        {"clientRef", "newFft"},
                        {
                            "id",
                            "dddddddd-dddd-4ddd-8ddd-dddddddddddd",
                        },
                    },
                }) &&
            mutation.at("active").at("etag") ==
                "\"project-authority-fixture\"",
        "Mutation result/created ID shape changed");
}

Json fixture_display_tab_json() {
    return {
        {"id", kTabId},
        {"name", "User Display"},
        {
            "owner",
            {
                {"unitId", kUserDisplayId},
                {"role", "main"},
            },
        },
        {
            "displays",
            Json::array({
                {
                    {"id", kDisplayId},
                    {
                        "providerTypeId",
                        "pamguard.spectrogram-display",
                    },
                    {"providerVersion", 1},
                    {
                        "owner",
                        {
                            {"unitId", kUserDisplayId},
                            {"role", "provider"},
                        },
                    },
                    {
                        "source",
                        {
                            {"unitId", kFftId},
                            {"outputRole", "fft"},
                        },
                    },
                    {"settingsVersion", 1},
                    {"settings", Json::object()},
                },
            }),
        },
        {
            "layout",
            {
                {"mode", "grid"},
                {"columns", 12},
                {"selectedDisplayId", kDisplayId},
                {
                    "items",
                    Json::array({
                        {
                            {"displayId", kDisplayId},
                            {"column", 0},
                            {"row", 0},
                            {"width", 12},
                            {"height", 6},
                        },
                    }),
                },
            },
        },
    };
}

Json fixture_mutation_json() {
    return {
        {"schemaVersion", 1},
        {"validateOnly", true},
        {
            "operations",
            Json::array({
                {
                    {"op", "addControlledUnit"},
                    {"clientRef", "newFft"},
                    {"typeId", "pamguard.fft"},
                    {"name", nullptr},
                    {"dependencyPolicy", "add-defaults"},
                },
                {
                    {"op", "renameControlledUnit"},
                    {"unit", {{"clientRef", "newFft"}}},
                    {"name", "Second FFT"},
                },
                {
                    {"op", "removeControlledUnit"},
                    {"unit", {{"id", kAcquisitionId}}},
                    {"dependantPolicy", "leave-unbound"},
                },
                {
                    {"op", "reorderControlledUnits"},
                    {
                        "units",
                        Json::array({
                            {{"id", kFftId}},
                            {{"clientRef", "newFft"}},
                        }),
                    },
                },
                {
                    {"op", "replaceSettings"},
                    {"unit", {{"id", kFftId}}},
                    {"settingsVersion", 1},
                    {
                        "settings",
                        {
                            {"fft", {{"fftLength", 2048}}},
                            {"enabled", true},
                        },
                    },
                },
                {
                    {"op", "replaceGlobalSettings"},
                    {"typeId", "pamguard.array-manager"},
                    {"settingsVersion", 1},
                    {
                        "settings",
                        {
                            {"arrayName", "Replacement"},
                        },
                    },
                },
                {
                    {"op", "setBinding"},
                    {"unit", {{"clientRef", "newFft"}}},
                    {"inputRole", "rawAudio"},
                    {
                        "sources",
                        Json::array({
                            {
                                {
                                    "unit",
                                    {{"id", kAcquisitionId}},
                                },
                                {"outputRole", "rawAudio"},
                            },
                        }),
                    },
                },
                {
                    {"op", "replaceDataModelLayout"},
                    {
                        "layout",
                        {
                            {
                                "nodes",
                                Json::array({
                                    {
                                        {"unitId", kFftId},
                                        {"x", 10},
                                        {"y", 20},
                                    },
                                }),
                            },
                            {
                                "viewport",
                                {
                                    {"x", 1},
                                    {"y", 2},
                                    {"zoom", 1.25},
                                },
                            },
                        },
                    },
                },
                {
                    {"op", "replaceDisplayHierarchy"},
                    {
                        "displayTabs",
                        Json::array({
                            fixture_display_tab_json(),
                        }),
                    },
                },
            }),
        },
    };
}

void check_mutation_round_trip() {
    const auto input = fixture_mutation_json();
    const auto parsed =
        project_mutation_batch_from_json(input.dump());
    require(
        parsed.schema_version == 1 &&
            parsed.validate_only &&
            parsed.operations.size() == 9,
        "Mutation batch header/count changed");
    require(
        std::holds_alternative<AddControlledUnitOperation>(
            parsed.operations[0]) &&
            std::get<AddControlledUnitOperation>(
                parsed.operations[0])
                    .client_ref == "newFft" &&
            std::get<AddControlledUnitOperation>(
                parsed.operations[0])
                    .dependency_policy ==
                DependencyPolicy::AddDefaults &&
            !std::get<AddControlledUnitOperation>(
                 parsed.operations[0])
                 .name,
        "addControlledUnit parsing changed");
    require(
        std::get<RenameControlledUnitOperation>(
            parsed.operations[1])
                .unit.client_ref ==
                std::optional<std::string>{"newFft"} &&
            std::get<RemoveControlledUnitOperation>(
                parsed.operations[2])
                    .dependant_policy ==
                DependantRemovalPolicy::LeaveUnbound &&
            std::get<ReorderControlledUnitsOperation>(
                parsed.operations[3])
                    .units.size() == 2,
        "Reference/removal/reorder parsing changed");
    const auto& settings =
        std::get<ReplaceControlledUnitSettingsOperation>(
            parsed.operations[4]);
    require(
        settings.unit.id ==
                std::optional<std::string>{kFftId} &&
            settings.settings_version == 1 &&
            Json::parse(settings.settings_json) ==
                input.at("operations").at(4).at("settings"),
        "replaceSettings parsing/canonicalization changed");
    require(
        std::get<ReplaceGlobalSettingsOperation>(
            parsed.operations[5])
                .type_id == "pamguard.array-manager" &&
            Json::parse(
                std::get<ReplaceGlobalSettingsOperation>(
                    parsed.operations[5])
                    .settings_json) ==
                input.at("operations").at(5).at("settings") &&
            std::get<SetControlledUnitBindingOperation>(
                parsed.operations[6])
                .sources.at(0)
                .output_role == "rawAudio" &&
            std::get<ReplaceDataModelLayoutOperation>(
                parsed.operations[7])
                    .layout.viewport.zoom == 1.25 &&
            std::get<ReplaceDisplayHierarchyOperation>(
                parsed.operations[8])
                    .display_tabs.at(0)
                    .layout.mode == DisplayLayoutMode::Grid,
        "Global settings/binding/layout/display hierarchy parsing changed");

    const auto compact =
        project_mutation_batch_to_json(parsed);
    const auto repeated =
        project_mutation_batch_to_json(parsed);
    require(
        compact == repeated,
        "Mutation serialization is not deterministic");
    const auto reparsed =
        project_mutation_batch_from_json(compact);
    require(
        project_mutation_batch_to_json(reparsed) == compact &&
            Json::parse(
                project_mutation_batch_to_json(parsed, true)) ==
                Json::parse(compact),
        "Mutation JSON did not strictly round-trip");
    const auto output = Json::parse(compact);
    require_keys(
        output,
        {"schemaVersion", "validateOnly", "operations"},
        "mutation batch");
    require(
        output.at("operations").at(0).at("op") ==
                "addControlledUnit" &&
            output.at("operations").at(4).at("op") ==
                "replaceSettings" &&
            output.at("operations").at(5).at("op") ==
                "replaceGlobalSettings" &&
            !output.contains("expectedRevision"),
        "Mutation operation/concurrency contract changed");
}

void require_mutation_rejected(
    const std::string& encoded,
    const std::string& context) {
    bool rejected = false;
    try {
        static_cast<void>(
            project_mutation_batch_from_json(encoded));
    }
    catch (const ProjectAuthorityJsonError&) {
        rejected = true;
    }
    require(rejected, "Accepted invalid mutation: " + context);
}

void check_configuration_template_mutation_contract() {
    const Json input = {
        {"schemaVersion", 1},
        {"validateOnly", true},
        {
            "operations",
            Json::array({
                {
                    {"op", "addConfigurationTemplate"},
                    {"clientRef", "clickMonitoring"},
                    {
                        "templateId",
                        "pamguard.click-monitoring",
                    },
                },
            }),
        },
    };
    const auto parsed =
        project_mutation_batch_from_json(input.dump());
    require(
        parsed.operations.size() == 1 &&
            std::holds_alternative<
                AddConfigurationTemplateOperation>(
                parsed.operations.front()) &&
            std::get<AddConfigurationTemplateOperation>(
                parsed.operations.front())
                    .client_ref == "clickMonitoring" &&
            std::get<AddConfigurationTemplateOperation>(
                parsed.operations.front())
                    .template_id ==
                kClickMonitoringConfigurationTemplateId,
        "addConfigurationTemplate parsing changed");

    const auto encoded =
        project_mutation_batch_to_json(parsed);
    const auto output = Json::parse(encoded);
    require(
        output == input &&
            project_mutation_batch_to_json(
                project_mutation_batch_from_json(encoded)) ==
                encoded,
        "addConfigurationTemplate did not deterministically round-trip");
    require_keys(
        output.at("operations").front(),
        {"op", "clientRef", "templateId"},
        "addConfigurationTemplate operation");

    auto unknown_field = input;
    unknown_field["operations"][0]["dependencyPolicy"] =
        "add-defaults";
    require_mutation_rejected(
        unknown_field.dump(),
        "configuration template unknown field");

    auto unknown_template = input;
    unknown_template["operations"][0]["templateId"] =
        "pamguard.unknown";
    require_mutation_rejected(
        unknown_template.dump(),
        "unknown configuration template");

    auto long_client_ref = input;
    long_client_ref["operations"][0]["clientRef"] =
        std::string(115, 'a');
    require_mutation_rejected(
        long_client_ref.dump(),
        "configuration template child clientRef overflow");
}

void check_strict_rejection() {
    require_mutation_rejected(
        R"({"schemaVersion":1,"schemaVersion":1,"validateOnly":false,"operations":[]})",
        "duplicate key");
    require_mutation_rejected(
        R"({"schemaVersion":1,"validateOnly":false,"operations":[],"expectedRevision":4})",
        "expectedRevision body field");
    require_mutation_rejected(
        R"({"schemaVersion":2,"validateOnly":false,"operations":[]})",
        "unsupported schema");
    require_mutation_rejected(
        R"({"schemaVersion":1,"validateOnly":false,"operations":[{"op":"replace","path":"/name","value":"x"}]})",
        "JSON Patch operation");
    require_mutation_rejected(
        R"({"schemaVersion":1,"validateOnly":false,"operations":[{"op":"addControlledUnit","clientRef":"new","typeId":"pamguard.fft","name":null,"dependencyPolicy":"automatic"}]})",
        "bad dependency policy");
    require_mutation_rejected(
        std::string(
            R"({"schemaVersion":1,"validateOnly":false,"operations":[{"op":"renameControlledUnit","unit":{"id":")") +
            kFftId +
            R"(","clientRef":"fft"},"name":"FFT"}]})",
        "both entity reference fields");
    require_mutation_rejected(
        R"({"schemaVersion":1,"validateOnly":false,"operations":[{"op":"renameControlledUnit","unit":{},"name":"FFT"}]})",
        "empty entity reference");
    require_mutation_rejected(
        R"({"schemaVersion":1,"validateOnly":false,"operations":[{"op":"replaceSettings","unit":{"id":"not-a-uuid"},"settingsVersion":1,"settings":{}}]})",
        "invalid UUID");
    require_mutation_rejected(
        std::string(
            R"({"schemaVersion":1,"validateOnly":false,"operations":[{"op":"replaceSettings","unit":{"id":")") +
            kFftId +
            R"("},"settingsVersion":1,"settings":[]}]})",
        "non-object settings");
    require_mutation_rejected(
        R"({"schemaVersion":1,"validateOnly":false,"operations":[{"op":"replaceGlobalSettings","typeId":"Bad Type","settingsVersion":1,"settings":{}}]})",
        "invalid global-settings type ID");
    require_mutation_rejected(
        R"({"schemaVersion":1,"validateOnly":false,"operations":[{"op":"replaceGlobalSettings","typeId":"pamguard.array-manager","settingsVersion":1,"settings":[]}]})",
        "non-object global settings");

    auto bad_layout = fixture_mutation_json();
    bad_layout["operations"][8]["displayTabs"][0]["layout"]
              ["mode"] = "floating";
    require_mutation_rejected(
        bad_layout.dump(),
        "unknown display layout enum");

    auto bad_source = fixture_mutation_json();
    bad_source["operations"][6]["sources"][0]["outputRole"] =
        "Raw-Audio";
    require_mutation_rejected(
        bad_source.dump(),
        "invalid source output role");

    auto duplicate_source = fixture_mutation_json();
    duplicate_source["operations"][6]["sources"].push_back(
        duplicate_source["operations"][6]["sources"][0]);
    require_mutation_rejected(
        duplicate_source.dump(),
        "duplicate binding source");

    std::string oversized(
        2U * 1024U * 1024U + 1U,
        ' ');
    require_mutation_rejected(
        oversized,
        "mutation body byte limit");
}

template <typename Parser>
void require_request_rejected(
    const std::string& encoded,
    Parser parser,
    const std::string& context) {
    bool rejected = false;
    try {
        static_cast<void>(parser(encoded));
    }
    catch (const ProjectAuthorityJsonError&) {
        rejected = true;
    }
    require(rejected, "Accepted invalid request: " + context);
}

void check_command_requests() {
    require(
        new_project_request_from_json(
            R"({"schemaVersion":1,"name":"  Survey  ","description":"North Sea","discardDirty":true})") ==
            NewProjectRequest{"Survey", "North Sea", true},
        "New-project request parsing changed");
    require(
        open_project_request_from_json(
            std::string(
                R"({"schemaVersion":1,"projectId":")") +
            kProjectId +
            R"(","discardDirty":false})") ==
            OpenProjectRequest{kProjectId, false},
        "Open-project request parsing changed");
    require(
        save_as_project_request_from_json(
            R"({"schemaVersion":1,"name":"Copy"})") ==
            SaveAsProjectRequest{"Copy"},
        "Save-As request parsing changed");

    require_request_rejected(
        R"({"schemaVersion":1,"name":"X","description":"","discardDirty":false,"expectedRevision":1})",
        new_project_request_from_json,
        "New expectedRevision");
    require_request_rejected(
        R"({"schemaVersion":1,"projectId":"not-a-uuid","discardDirty":false})",
        open_project_request_from_json,
        "Open invalid project ID");
    require_request_rejected(
        R"({"schemaVersion":1,"name":"A","name":"B"})",
        save_as_project_request_from_json,
        "Save-As duplicate key");
    require_request_rejected(
        std::string(64U * 1024U + 1U, ' '),
        save_as_project_request_from_json,
        "command body byte limit");
}

} // namespace

int main() {
    try {
        const auto controlled = controlled_registry();
        const auto runtime = runtime_registry();
        const auto snapshot =
            fixture_snapshot(controlled, runtime);

        check_snapshot(snapshot);
        check_inspection(snapshot);
        check_other_responses(snapshot);
        check_mutation_round_trip();
        check_configuration_template_mutation_contract();
        check_strict_rejection();
        check_command_requests();

        std::cout
            << "Project authority JSON contract covers strict mutations, "
               "requests, snapshots, inspection, source choices, saved "
               "projects, and created IDs\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
