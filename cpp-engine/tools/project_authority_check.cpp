#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "pamguard/core/BuiltInModules.h"
#include "pamguard/core/ModuleGraphJson.h"
#include "pamguard/project/BuiltInControlledUnits.h"
#include "pamguard/project/ControlledUnitJson.h"
#include "pamguard/project/ProjectAuthority.h"
#include "pamguard/project/ProjectIdentity.h"
#include "pamguard/project/ProjectJson.h"

namespace {

using namespace pamguard::project;

constexpr std::string_view kAcquisitionType =
    "pamguard.acquisition";
constexpr std::string_view kFftType = "pamguard.fft";
constexpr std::string_view kClickDetectorType =
    "pamguard.click-detector";
constexpr std::string_view kUserDisplayType =
    "pamguard.user-display";
constexpr std::string_view kSpectrogramProvider =
    "pamguard.spectrogram-display";
constexpr std::string_view kClickDisplayProvider =
    "pamguard.click-display";
constexpr std::string_view kSoundOutputType =
    "pamguard.sound-output";
constexpr std::string_view kAmplifierType =
    "pamguard.amplifier";
constexpr std::string_view kPatchPanelType =
    "pamguard.patch-panel";
constexpr std::string_view kArrayManagerType =
    "pamguard.array-manager";
constexpr std::string_view kMissingProjectId =
    "99999999-9999-4999-8999-999999999999";
constexpr std::string_view kInvalidProjectId =
    "88888888-8888-4888-8888-888888888888";
constexpr std::string_view kUnknownUnitId =
    "77777777-7777-4777-8777-777777777777";
constexpr std::string_view kDisplayId =
    "display:66666666-6666-4666-8666-666666666666";

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Callback>
ProjectAuthorityError require_authority_error(
    Callback&& callback,
    std::string_view code,
    const std::string& message) {
    try {
        callback();
    }
    catch (const ProjectAuthorityError& error) {
        require(
            error.code() == code,
            message + ": expected code '" + std::string(code) +
                "', received '" + error.code() + "'");
        return error;
    }
    throw std::runtime_error(message + ": no error was raised");
}

template <typename Exception, typename Callback>
void require_throws(
    Callback&& callback,
    const std::string& message) {
    try {
        callback();
    }
    catch (const Exception&) {
        return;
    }
    throw std::runtime_error(message);
}

struct Fixture {
    std::filesystem::path root;
    ControlledUnitRegistry controlled;
    pamguard::core::ModuleRegistry runtime;
    std::optional<ProjectStore> store;
    std::optional<ProjectAuthority> authority;

    explicit Fixture(std::string_view label) {
        root =
            std::filesystem::temp_directory_path() /
            (
                "pamguard-project-authority-" +
                std::string(label) + "-" +
                std::to_string(
                    std::chrono::steady_clock::now()
                        .time_since_epoch()
                        .count()));
        std::filesystem::create_directories(root);
        register_builtin_controlled_units(controlled);
        pamguard::core::register_builtin_module_types(runtime);
        store.emplace(root);
        authority.emplace(controlled, runtime, *store);
    }

    ~Fixture() {
        authority.reset();
        store.reset();
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        auto moved = root;
        moved += ".moved";
        std::filesystem::remove_all(moved, ignored);
    }
};

struct SnapshotSignature {
    std::string project_json;
    std::string graph_json;
    ProjectionIndex projection_index;
    std::vector<ProjectionIssue> projection_issues;
    std::uint64_t working_revision = 0;
    std::optional<std::uint64_t> saved_revision;
    std::uint64_t authority_revision = 0;
    std::string working_hash;
    std::optional<std::string> saved_hash;
    bool dirty = false;
    std::string etag;

    bool operator==(const SnapshotSignature&) const = default;
};

SnapshotSignature signature(
    const ActiveProjectSnapshot& snapshot) {
    return {
        project_document_to_canonical_json(snapshot.project),
        pamguard::core::module_graph_to_json(
            snapshot.projection.graph),
        snapshot.projection.index,
        snapshot.projection.issues,
        snapshot.working_revision,
        snapshot.saved_revision,
        snapshot.authority_revision,
        snapshot.working_content_hash,
        snapshot.saved_content_hash,
        snapshot.dirty,
        snapshot.etag,
    };
}

void require_unchanged(
    ProjectAuthority& authority,
    const SnapshotSignature& before,
    const std::string& message) {
    require(
        signature(authority.snapshot()) == before,
        message);
}

ProjectEntityReference by_id(std::string id) {
    ProjectEntityReference reference;
    reference.id = std::move(id);
    return reference;
}

ProjectEntityReference by_client_ref(std::string client_ref) {
    ProjectEntityReference reference;
    reference.client_ref = std::move(client_ref);
    return reference;
}

AddControlledUnitOperation add(
    std::string client_ref,
    std::string type_id,
    DependencyPolicy dependency_policy =
        DependencyPolicy::Reject,
    std::optional<std::string> name = std::nullopt) {
    AddControlledUnitOperation operation;
    operation.client_ref = std::move(client_ref);
    operation.type_id = std::move(type_id);
    operation.name = std::move(name);
    operation.dependency_policy = dependency_policy;
    return operation;
}

const ControlledUnitInstance& unit_by_id(
    const ActiveProjectSnapshot& snapshot,
    std::string_view id) {
    const auto found = std::find_if(
        snapshot.project.controlled_units.begin(),
        snapshot.project.controlled_units.end(),
        [&](const auto& unit) { return unit.id == id; });
    require(
        found != snapshot.project.controlled_units.end(),
        "Controlled-unit ID was not present");
    return *found;
}

std::vector<const ControlledUnitInstance*> units_by_type(
    const ActiveProjectSnapshot& snapshot,
    std::string_view type_id) {
    std::vector<const ControlledUnitInstance*> result;
    for (const auto& unit :
         snapshot.project.controlled_units) {
        if (unit.type_id == type_id) {
            result.push_back(&unit);
        }
    }
    return result;
}

std::string created_id(
    const ProjectMutationResult& result,
    std::string_view client_ref) {
    const auto found = std::find_if(
        result.created_entities.begin(),
        result.created_entities.end(),
        [&](const auto& entity) {
            return entity.client_ref == client_ref;
        });
    require(
        found != result.created_entities.end(),
        "Mutation result omitted clientRef '" +
            std::string(client_ref) + "'");
    return found->id;
}

bool has_created_id(
    const ProjectMutationResult& result,
    std::string_view client_ref) {
    return std::any_of(
        result.created_entities.begin(),
        result.created_entities.end(),
        [&](const auto& entity) {
            return entity.client_ref == client_ref;
        });
}

const InputBinding& binding_by_role(
    const ControlledUnitInstance& unit,
    std::string_view input_role) {
    const auto found = std::find_if(
        unit.bindings.begin(),
        unit.bindings.end(),
        [&](const auto& binding) {
            return binding.input_role == input_role;
        });
    require(
        found != unit.bindings.end(),
        "Controlled unit omitted binding '" +
            std::string(input_role) + "'");
    return *found;
}

const DisplayTab& tab_by_owner(
    const ActiveProjectSnapshot& snapshot,
    std::string_view unit_id,
    std::string_view role) {
    const auto found = std::find_if(
        snapshot.project.display_tabs.begin(),
        snapshot.project.display_tabs.end(),
        [&](const auto& tab) {
            return tab.owner.unit_id == unit_id &&
                tab.owner.role == role;
        });
    require(
        found != snapshot.project.display_tabs.end(),
        "Controlled unit omitted owned display tab");
    return *found;
}

ControlledUnitDescriptor& mutable_descriptor(
    ControlledUnitRegistry& registry,
    std::string_view type_id) {
    const auto* descriptor =
        registry.find_controlled_unit(std::string(type_id));
    require(descriptor, "Controlled-unit descriptor is absent");
    // The registry is mutable and owns this descriptor. This narrowly scoped
    // test hook exercises authority-side min/max gates without adding a
    // production mutation API to the catalogue.
    return const_cast<ControlledUnitDescriptor&>(*descriptor);
}

ProjectMutationResult add_fft_with_default_source(
    ProjectAuthority& authority) {
    ProjectMutationBatch batch;
    batch.operations.push_back(
        add(
            "fft",
            std::string(kFftType),
            DependencyPolicy::AddDefaults));
    return authority.mutate(
        authority.snapshot().etag,
        batch);
}

const GlobalSettingsComponent& array_manager_component(
    const ActiveProjectSnapshot& snapshot) {
    const auto found = std::find_if(
        snapshot.project.global_settings.components.begin(),
        snapshot.project.global_settings.components.end(),
        [](const auto& component) {
            return component.type_id == kArrayManagerType;
        });
    require(
        found !=
            snapshot.project.global_settings.components.end(),
        "Active project omits Array Manager global settings");
    return *found;
}

std::string replace_once(
    std::string value,
    std::string_view needle,
    std::string_view replacement) {
    const auto position = value.find(needle);
    require(
        position != std::string::npos,
        "Could not construct global-settings fixture");
    value.replace(position, needle.size(), replacement);
    return value;
}

void check_global_array_settings_authority() {
    Fixture fixture("global-array");
    auto& authority = *fixture.authority;
    const auto initial = authority.snapshot();
    const auto* descriptor =
        fixture.controlled.find_global_settings(
            std::string(kArrayManagerType));
    require(
        descriptor &&
            descriptor->required &&
            initial.project.global_settings.components.size() == 1 &&
            array_manager_component(initial).settings_version == 1 &&
            initial.projection.array_geometry.has_value() &&
            initial.projection.array_geometry->hydrophones.size() == 2,
        "Blank authority did not default one typed Array Manager geometry");

    const auto changed_json = replace_once(
        descriptor->settings.default_settings_json,
        R"("speedOfSoundMps":1500)",
        R"("speedOfSoundMps":1482.5)");
    ProjectMutationBatch replace;
    replace.operations.push_back(
        ReplaceGlobalSettingsOperation{
            std::string(kArrayManagerType),
            1,
            changed_json,
        });
    const auto changed =
        authority.mutate(initial.etag, replace);
    require(
        changed.changed &&
            changed.active.working_revision == 1 &&
            changed.active.authority_revision == 1 &&
            changed.active.projection.array_geometry.has_value() &&
            changed.active.projection.array_geometry
                    ->speed_of_sound_mps == 1482.5 &&
            array_manager_component(changed.active)
                    .settings_json.find("1482.5") !=
                std::string::npos,
        "Typed Array Manager mutation did not update project/projection authority");

    const auto no_op =
        authority.mutate(changed.active.etag, replace);
    require(
        !no_op.changed &&
            no_op.active.etag == changed.active.etag &&
            no_op.active.working_revision ==
                changed.active.working_revision,
        "Identical Array Manager replacement advanced authority");

    ProjectMutationBatch invalid_version;
    invalid_version.operations.push_back(
        ReplaceGlobalSettingsOperation{
            std::string(kArrayManagerType),
            2,
            changed_json,
        });
    const auto before_invalid = signature(authority.snapshot());
    require_authority_error(
        [&] {
            (void)authority.mutate(
                authority.snapshot().etag,
                invalid_version);
        },
        "invalid_project_projection",
        "Array Manager mutation accepted an unknown settings version");
    require_unchanged(
        authority,
        before_invalid,
        "Rejected Array Manager version changed authority");

    ProjectMutationBatch invalid_geometry;
    invalid_geometry.operations.push_back(
        ReplaceGlobalSettingsOperation{
            std::string(kArrayManagerType),
            1,
            R"({"arrayName":"Incomplete"})",
        });
    require_authority_error(
        [&] {
            (void)authority.mutate(
                authority.snapshot().etag,
                invalid_geometry);
        },
        "invalid_project_projection",
        "Array Manager mutation accepted incomplete geometry");
    require_unchanged(
        authority,
        before_invalid,
        "Rejected Array Manager geometry changed authority");

    ProjectMutationBatch unknown;
    unknown.operations.push_back(
        ReplaceGlobalSettingsOperation{
            "pamguard.unknown-global",
            1,
            "{}",
        });
    require_authority_error(
        [&] {
            (void)authority.mutate(
                authority.snapshot().etag,
                unknown);
        },
        "global_settings_type_unavailable",
        "Typed global mutation accepted an unregistered component");
    require_unchanged(
        authority,
        before_invalid,
        "Unknown global-settings mutation changed authority");
}

void check_blank_preconditions_staging_and_rules() {
    Fixture fixture("mutation");
    auto& authority = *fixture.authority;

    const auto blank = authority.snapshot();
    require(
        is_uuid_v4(blank.project.project_id) &&
            blank.project.metadata.name == "Untitled Project" &&
            blank.project.controlled_units.empty() &&
            blank.project.global_settings.components.size() == 1 &&
            blank.project.display_tabs.empty() &&
            blank.working_revision == 0 &&
            blank.authority_revision == 0 &&
            !blank.saved_revision &&
            !blank.saved_content_hash &&
            blank.dirty &&
            blank.projection.runnable() &&
            !blank.etag.empty(),
        "Blank active-project authority contract changed");

    ProjectMutationBatch empty;
    const auto required = require_authority_error(
        [&] { (void)authority.mutate({}, empty); },
        "precondition_required",
        "Mutation accepted a missing ETag");
    require(
        required.current_etag() == blank.etag,
        "Missing-precondition error omitted the current ETag");
    const auto stale = require_authority_error(
        [&] { (void)authority.mutate("\"stale\"", empty); },
        "precondition_failed",
        "Mutation accepted a stale ETag");
    require(
        stale.current_etag() == blank.etag,
        "Stale-precondition error omitted the current ETag");

    ProjectMutationBatch reject_dependencies;
    reject_dependencies.operations.push_back(
        add("fft", std::string(kFftType)));
    const auto blank_signature = signature(blank);
    require_authority_error(
        [&] {
            (void)authority.mutate(
                blank.etag,
                reject_dependencies);
        },
        "missing_dependencies",
        "FFT add bypassed required-source policy");
    require_unchanged(
        authority,
        blank_signature,
        "Rejected dependency mutation changed the active project");

    ProjectMutationBatch atomic_failure;
    atomic_failure.operations.push_back(
        add("source", std::string(kAcquisitionType)));
    atomic_failure.operations.push_back(
        RenameControlledUnitOperation{
            by_client_ref("missing"),
            "Must not commit",
        });
    require_authority_error(
        [&] {
            (void)authority.mutate(
                blank.etag,
                atomic_failure);
        },
        "unknown_client_reference",
        "A partially invalid batch was accepted");
    require_unchanged(
        authority,
        blank_signature,
        "A partially invalid batch was not atomic");

    ProjectMutationBatch add_fft;
    add_fft.operations.push_back(
        add(
            "fft",
            std::string(kFftType),
            DependencyPolicy::AddDefaults));
    auto prepared =
        authority.prepare_mutation(blank.etag, add_fft);
    const auto prepared_fft_id =
        created_id(prepared.preview(), "fft");
    require(
        is_uuid_v4(prepared_fft_id) &&
            prepared.preview().active.project
                    .controlled_units.size() == 2 &&
            prepared.preview().active.projection.graph
                    .modules.size() == 3 &&
            prepared.preview().active.projection.graph
                    .revision == 1,
        "Prepared default dependency graph is incomplete");
    require_unchanged(
        authority,
        blank_signature,
        "Preparing a mutation changed active authority");

    const auto committed =
        authority.commit_mutation(std::move(prepared));
    require(
        committed.changed &&
            !committed.validated_only &&
            created_id(committed, "fft") ==
                prepared_fft_id &&
            committed.active.working_revision == 1 &&
            committed.active.authority_revision == 1 &&
            committed.active.etag != blank.etag,
        "Prepared mutation did not commit its exact identities");
    const auto& fft =
        unit_by_id(committed.active, prepared_fft_id);
    require(
        fft.bindings.size() == 1 &&
            fft.bindings[0].input_role == "rawAudio" &&
            fft.bindings[0].sources.size() == 1,
        "Default-provider creation did not bind FFT input");
    const auto acquisition_id =
        fft.bindings[0].sources[0].unit_id;
    const auto& default_layout =
        committed.active.project.data_model_layout.nodes;
    require(
        unit_by_id(committed.active, acquisition_id).type_id ==
                kAcquisitionType &&
            committed.created_entities.size() == 1 &&
            default_layout.size() == 2 &&
            (
                std::abs(
                    default_layout[0].x -
                    default_layout[1].x) >= 292.0 ||
                std::abs(
                    default_layout[0].y -
                    default_layout[1].y) >= 220.0
            ),
        "Default dependency identity/layout bookkeeping changed");

    ProjectMutationBatch add_then_remove;
    add_then_remove.operations.push_back(
        add("temporary", std::string(kUserDisplayType)));
    add_then_remove.operations.push_back(
        RemoveControlledUnitOperation{
            by_client_ref("temporary"),
            DependantRemovalPolicy::Reject,
        });
    const auto before_add_remove = authority.snapshot();
    const auto add_remove_result =
        authority.mutate(
            before_add_remove.etag,
            add_then_remove);
    require(
        !add_remove_result.changed &&
            add_remove_result.created_entities.empty() &&
            add_remove_result.active.etag ==
                before_add_remove.etag,
        "A removed same-batch clientRef leaked as a created entity");

    ProjectMutationBatch duplicate_client_ref;
    duplicate_client_ref.operations.push_back(
        add("duplicate-ref", std::string(kUserDisplayType)));
    duplicate_client_ref.operations.push_back(
        add("duplicate-ref", std::string(kUserDisplayType)));
    const auto before_duplicate_ref =
        signature(authority.snapshot());
    require_authority_error(
        [&] {
            (void)authority.mutate(
                authority.snapshot().etag,
                duplicate_client_ref);
        },
        "duplicate_client_reference",
        "Duplicate mutation clientRef was accepted");
    require_unchanged(
        authority,
        before_duplicate_ref,
        "Duplicate clientRef partially committed");

    ProjectMutationBatch client_ref_batch;
    client_ref_batch.operations.push_back(
        add("display1", std::string(kUserDisplayType)));
    client_ref_batch.operations.push_back(
        RenameControlledUnitOperation{
            by_client_ref("display1"),
            "Operations One",
        });
    client_ref_batch.operations.push_back(
        add("display2", std::string(kUserDisplayType)));
    client_ref_batch.operations.push_back(
        ReorderControlledUnitsOperation{
            {
                by_client_ref("display2"),
                by_id(prepared_fft_id),
                by_id(acquisition_id),
                by_client_ref("display1"),
            },
        });
    const auto client_ref_result =
        authority.mutate(
            authority.snapshot().etag,
            client_ref_batch);
    const auto display1_id =
        created_id(client_ref_result, "display1");
    const auto display2_id =
        created_id(client_ref_result, "display2");
    require(
        client_ref_result.active.project.controlled_units
                .size() == 4 &&
            client_ref_result.active.project.controlled_units[0]
                    .id == display2_id &&
            client_ref_result.active.project.controlled_units[1]
                    .id == prepared_fft_id &&
            client_ref_result.active.project.controlled_units[2]
                    .id == acquisition_id &&
            client_ref_result.active.project.controlled_units[3]
                    .id == display1_id &&
            unit_by_id(
                client_ref_result.active,
                display1_id).name == "Operations One" &&
            std::any_of(
                client_ref_result.active.project.display_tabs
                    .begin(),
                client_ref_result.active.project.display_tabs
                    .end(),
                [&](const auto& tab) {
                    return tab.owner.unit_id == display1_id &&
                        tab.name == "Operations One";
                }),
        "Sequential clientRef resolution/reorder did not commit exactly");

    ProjectMutationBatch invalid_reorder;
    invalid_reorder.operations.push_back(
        ReorderControlledUnitsOperation{
            {
                by_id(display2_id),
                by_id(prepared_fft_id),
                by_id(acquisition_id),
                by_id(acquisition_id),
            },
        });
    const auto before_invalid_reorder =
        signature(authority.snapshot());
    require_authority_error(
        [&] {
            (void)authority.mutate(
                authority.snapshot().etag,
                invalid_reorder);
        },
        "invalid_controlled_unit_order",
        "Duplicate controlled-unit reorder was accepted");
    require_unchanged(
        authority,
        before_invalid_reorder,
        "Invalid reorder changed authority");

    ProjectMutationBatch first_rename;
    first_rename.operations.push_back(
        RenameControlledUnitOperation{
            by_id(prepared_fft_id),
            "FFT Primary",
        });
    ProjectMutationBatch second_rename;
    second_rename.operations.push_back(
        RenameControlledUnitOperation{
            by_id(prepared_fft_id),
            "FFT Stale",
        });
    const auto shared_etag = authority.snapshot().etag;
    auto first_prepared =
        authority.prepare_mutation(shared_etag, first_rename);
    auto second_prepared =
        authority.prepare_mutation(shared_etag, second_rename);
    const auto first_committed =
        authority.commit_mutation(std::move(first_prepared));
    const auto after_first = signature(first_committed.active);
    require_authority_error(
        [&] {
            (void)authority.commit_mutation(
                std::move(second_prepared));
        },
        "precondition_failed",
        "A stale prepared mutation overwrote a newer writer");
    require_unchanged(
        authority,
        after_first,
        "Stale prepared commit changed authority");

    const auto before_noop = authority.snapshot();
    const auto noop =
        authority.mutate(
            before_noop.etag,
            first_rename);
    require(
        !noop.changed &&
            noop.active.working_revision ==
                before_noop.working_revision &&
            noop.active.authority_revision ==
                before_noop.authority_revision &&
            noop.active.etag == before_noop.etag,
        "No-op mutation advanced project authority");

    ProjectMutationBatch validate_only;
    validate_only.validate_only = true;
    validate_only.operations.push_back(
        add("preview", std::string(kAcquisitionType)));
    const auto before_validation =
        signature(authority.snapshot());
    const auto validation =
        authority.mutate(
            authority.snapshot().etag,
            validate_only);
    require(
        validation.changed &&
            validation.validated_only &&
            validation.active.project.controlled_units.size() ==
                authority.snapshot().project
                    .controlled_units.size() + 1,
        "validateOnly did not return a projected candidate");
    require_unchanged(
        authority,
        before_validation,
        "validateOnly changed active authority");
    auto validation_candidate =
        authority.prepare_mutation(
            authority.snapshot().etag,
            validate_only);
    require_authority_error(
        [&] {
            (void)authority.commit_mutation(
                std::move(validation_candidate));
        },
        "validated_only_candidate",
        "A validateOnly candidate was committed");
    require_unchanged(
        authority,
        before_validation,
        "Rejected validateOnly commit changed authority");

    ProjectMutationBatch second_source;
    second_source.operations.push_back(
        add("source2", std::string(kAcquisitionType)));
    const auto second_source_result =
        authority.mutate(
            authority.snapshot().etag,
            second_source);
    const auto second_source_id =
        created_id(second_source_result, "source2");
    require(
        unit_by_id(
            second_source_result.active,
            second_source_id).name ==
                "Sound Acquisition 2",
        "PAMGuard-style generated suffix changed");

    ProjectMutationBatch third_source;
    third_source.operations.push_back(
        add("source3", std::string(kAcquisitionType)));
    const auto third_source_result =
        authority.mutate(
            authority.snapshot().etag,
            third_source);
    require(
        unit_by_id(
            third_source_result.active,
            created_id(third_source_result, "source3")).name ==
                "Sound Acquisition 3",
        "Repeated generated suffix was not unique");

    ProjectMutationBatch duplicate_name;
    duplicate_name.operations.push_back(
        add(
            "duplicate",
            std::string(kAcquisitionType),
            DependencyPolicy::Reject,
            " sound acquisition "));
    const auto before_bad_name =
        signature(authority.snapshot());
    require_authority_error(
        [&] {
            (void)authority.mutate(
                authority.snapshot().etag,
                duplicate_name);
        },
        "duplicate_name",
        "Trimmed ASCII-case duplicate name was accepted");
    require_unchanged(
        authority,
        before_bad_name,
        "Duplicate-name rejection changed authority");

    ProjectMutationBatch long_name;
    long_name.operations.push_back(
        add(
            "long",
            std::string(kAcquisitionType),
            DependencyPolicy::Reject,
            std::string(51, 'x')));
    require_authority_error(
        [&] {
            (void)authority.mutate(
                authority.snapshot().etag,
                long_name);
        },
        "invalid_name",
        "Over-length Java item name was accepted");

    auto& acquisition_descriptor =
        mutable_descriptor(
            fixture.controlled,
            kAcquisitionType);
    const auto acquisition_count =
        units_by_type(
            authority.snapshot(),
            kAcquisitionType).size();
    acquisition_descriptor.instance_rules.maximum_instances =
        acquisition_count;
    ProjectMutationBatch beyond_maximum;
    beyond_maximum.operations.push_back(
        add("too-many", std::string(kAcquisitionType)));
    require_authority_error(
        [&] {
            (void)authority.mutate(
                authority.snapshot().etag,
                beyond_maximum);
        },
        "maximum_instances",
        "Controlled-unit maximum was not enforced");

    acquisition_descriptor.instance_rules.minimum_instances =
        acquisition_count;
    ProjectMutationBatch below_minimum;
    below_minimum.operations.push_back(
        RemoveControlledUnitOperation{
            by_id(second_source_id),
            DependantRemovalPolicy::LeaveUnbound,
        });
    require_authority_error(
        [&] {
            (void)authority.mutate(
                authority.snapshot().etag,
                below_minimum);
        },
        "minimum_instances",
        "Controlled-unit minimum was not enforced");
    acquisition_descriptor.instance_rules.minimum_instances = 0;
    acquisition_descriptor.instance_rules.maximum_instances.reset();
}

void check_settings_bindings_layout_and_displays() {
    Fixture fixture("model");
    auto& authority = *fixture.authority;
    const auto initial = add_fft_with_default_source(authority);
    const auto fft_id = created_id(initial, "fft");
    auto acquisition_ids =
        units_by_type(initial.active, kAcquisitionType);
    require(
        acquisition_ids.size() == 1,
        "Default FFT fixture has the wrong source count");
    const auto first_acquisition_id =
        acquisition_ids.front()->id;

    ProjectMutationBatch add_source;
    add_source.operations.push_back(
        add("source2", std::string(kAcquisitionType)));
    const auto source_result =
        authority.mutate(
            authority.snapshot().etag,
            add_source);
    const auto second_acquisition_id =
        created_id(source_result, "source2");

    const auto compatible =
        authority.compatible_sources(fft_id, "rawAudio");
    std::set<std::string> compatible_ids;
    for (const auto& source : compatible) {
        compatible_ids.insert(source.unit_id);
        require(
            source.output_role == "rawAudio" &&
                source.data_type == "pamguard.raw-audio",
            "Compatible source exposed an incompatible role");
    }
    require(
        compatible_ids ==
            std::set<std::string>{
                first_acquisition_id,
                second_acquisition_id,
            },
        "Compatible-source filtering omitted or added a source");
    require_authority_error(
        [&] {
            (void)authority.compatible_sources(
                fft_id,
                "missing");
        },
        "public_input_not_found",
        "Unknown public input was accepted");

    ProjectMutationBatch rebind;
    rebind.operations.push_back(
        SetControlledUnitBindingOperation{
            by_id(fft_id),
            "rawAudio",
            {
                {
                    by_id(second_acquisition_id),
                    "rawAudio",
                },
            },
        });
    const auto rebound =
        authority.mutate(
            authority.snapshot().etag,
            rebind);
    const auto& rebound_fft =
        unit_by_id(rebound.active, fft_id);
    require(
        rebound_fft.bindings.size() == 1 &&
            rebound_fft.bindings[0].sources ==
                std::vector<SourceReference>{
                    {second_acquisition_id, "rawAudio"}} &&
            rebound.active.projection.index
                .find_public_input(fft_id, "rawAudio")
                ->sources ==
                rebound_fft.bindings[0].sources,
        "Graph-line and dialog binding state diverged");

    ProjectMutationBatch invalid_source;
    invalid_source.operations.push_back(
        SetControlledUnitBindingOperation{
            by_id(fft_id),
            "rawAudio",
            {
                {
                    by_id(fft_id),
                    "fft",
                },
            },
        });
    const auto before_invalid_source =
        signature(authority.snapshot());
    require_authority_error(
        [&] {
            (void)authority.mutate(
                authority.snapshot().etag,
                invalid_source);
        },
        "invalid_project_projection",
        "Incompatible binding was accepted");
    require_unchanged(
        authority,
        before_invalid_source,
        "Invalid binding changed authority");

    ProjectMutationBatch duplicate_source;
    duplicate_source.operations.push_back(
        SetControlledUnitBindingOperation{
            by_id(fft_id),
            "rawAudio",
            {
                {
                    by_id(first_acquisition_id),
                    "rawAudio",
                },
                {
                    by_id(second_acquisition_id),
                    "rawAudio",
                },
            },
        });
    require_authority_error(
        [&] {
            (void)authority.mutate(
                authority.snapshot().etag,
                duplicate_source);
        },
        "invalid_project_projection",
        "Exactly-one binding accepted multiple sources");

    ProjectMutationBatch unbind;
    unbind.operations.push_back(
        SetControlledUnitBindingOperation{
            by_id(fft_id),
            "rawAudio",
            {},
        });
    const auto unbound =
        authority.mutate(
            authority.snapshot().etag,
            unbind);
    require(
        unbound.active.projection.needs_configuration() &&
            unbound.active.project.controlled_units[
                std::distance(
                    unbound.active.project.controlled_units.begin(),
                    std::find_if(
                        unbound.active.project.controlled_units.begin(),
                        unbound.active.project.controlled_units.end(),
                        [&](const auto& unit) {
                            return unit.id == fft_id;
                        }))]
                .bindings[0]
                .sources.empty(),
        "Required input could not remain explicitly unbound");
    (void)authority.mutate(
        authority.snapshot().etag,
        rebind);

    ProjectMutationBatch remove_bound_reject;
    remove_bound_reject.operations.push_back(
        RemoveControlledUnitOperation{
            by_id(second_acquisition_id),
            DependantRemovalPolicy::Reject,
        });
    require_authority_error(
        [&] {
            (void)authority.mutate(
                authority.snapshot().etag,
                remove_bound_reject);
        },
        "controlled_unit_has_dependants",
        "Bound source removal ignored dependant policy");

    ProjectMutationBatch remove_bound;
    remove_bound.operations.push_back(
        RemoveControlledUnitOperation{
            by_id(second_acquisition_id),
            DependantRemovalPolicy::LeaveUnbound,
        });
    const auto removed_source =
        authority.mutate(
            authority.snapshot().etag,
            remove_bound);
    require(
        removed_source.active.projection.needs_configuration() &&
            !removed_source.active.projection.index
                 .find_public_input(fft_id, "rawAudio")
                 ->sources.size(),
        "Leave-unbound removal retained a phantom source");

    ProjectMutationBatch bind_first;
    bind_first.operations.push_back(
        SetControlledUnitBindingOperation{
            by_id(fft_id),
            "rawAudio",
            {
                {
                    by_id(first_acquisition_id),
                    "rawAudio",
                },
            },
        });
    (void)authority.mutate(
        authority.snapshot().etag,
        bind_first);

    const auto* fft_descriptor =
        fixture.controlled.find_controlled_unit(
            std::string(kFftType));
    require(fft_descriptor, "FFT descriptor is absent");
    std::string modified_fft_settings =
        fft_descriptor->settings.default_settings_json;
    const auto threshold =
        modified_fft_settings.find("\"clickThreshold\":5");
    require(
        threshold != std::string::npos,
        "Could not construct modified FFT settings");
    modified_fft_settings.replace(
        threshold,
        std::string("\"clickThreshold\":5").size(),
        "\"clickThreshold\":7.25");

    ProjectMutationBatch settings;
    settings.operations.push_back(
        ReplaceControlledUnitSettingsOperation{
            by_id(fft_id),
            1,
            modified_fft_settings,
        });
    const auto settings_result =
        authority.mutate(
            authority.snapshot().etag,
            settings);
    require(
        unit_by_id(settings_result.active, fft_id)
                .settings_json.find("7.25") !=
            std::string::npos,
        "Valid controlled-unit settings were not committed");

    ProjectMutationBatch invalid_settings;
    invalid_settings.operations.push_back(
        RenameControlledUnitOperation{
            by_id(fft_id),
            "Atomic rename must roll back",
        });
    invalid_settings.operations.push_back(
        ReplaceControlledUnitSettingsOperation{
            by_id(fft_id),
            1,
            "{}",
        });
    const auto before_invalid_settings =
        signature(authority.snapshot());
    require_authority_error(
        [&] {
            (void)authority.mutate(
                authority.snapshot().etag,
                invalid_settings);
        },
        "invalid_project_projection",
        "Invalid settings were accepted");
    require_unchanged(
        authority,
        before_invalid_settings,
        "Invalid settings did not roll back the whole batch");

    auto current = authority.snapshot();
    DataModelLayout layout;
    for (std::size_t index = 0;
         index < current.project.controlled_units.size();
         ++index) {
        layout.nodes.push_back({
            current.project.controlled_units[index].id,
            100.0 + 50.0 * static_cast<double>(index),
            200.0 + 25.0 * static_cast<double>(index),
        });
    }
    layout.viewport = {42.0, -17.0, 1.75};
    ProjectMutationBatch replace_layout;
    replace_layout.operations.push_back(
        ReplaceDataModelLayoutOperation{layout});
    const auto layout_result =
        authority.mutate(
            current.etag,
            replace_layout);
    require(
        layout_result.active.project.data_model_layout ==
            layout,
        "Data Model layout/viewport did not round-trip");

    auto invalid_layout = layout;
    invalid_layout.nodes.push_back({
        std::string(kUnknownUnitId),
        0.0,
        0.0,
    });
    ProjectMutationBatch replace_invalid_layout;
    replace_invalid_layout.operations.push_back(
        ReplaceDataModelLayoutOperation{
            std::move(invalid_layout)});
    const auto before_invalid_layout =
        signature(authority.snapshot());
    require_authority_error(
        [&] {
            (void)authority.mutate(
                authority.snapshot().etag,
                replace_invalid_layout);
        },
        "invalid_project",
        "Layout accepted an unknown controlled unit");
    require_unchanged(
        authority,
        before_invalid_layout,
        "Invalid layout changed authority");

    ProjectMutationBatch add_display_owner;
    add_display_owner.operations.push_back(
        add("display-owner", std::string(kUserDisplayType)));
    const auto owner_result =
        authority.mutate(
            authority.snapshot().etag,
            add_display_owner);
    const auto owner_id =
        created_id(owner_result, "display-owner");
    require(
        owner_result.active.project.display_tabs.size() == 1 &&
            owner_result.active.project.display_tabs[0]
                    .owner.unit_id == owner_id &&
            owner_result.active.project.display_tabs[0]
                    .displays.empty(),
        "User Display did not create exactly one empty owned tab");

    const auto* provider =
        fixture.controlled.find_display_provider(
            std::string(kSpectrogramProvider));
    require(provider, "Spectrogram provider is absent");
    auto tab = owner_result.active.project.display_tabs[0];
    DisplayInstance display;
    display.id = std::string(kDisplayId);
    display.provider_type_id = provider->id;
    display.provider_version = provider->descriptor_version;
    display.owner = {owner_id, "provider"};
    display.source = SourceReference{fft_id, "fft"};
    display.settings_version = provider->settings.version;
    display.settings_json =
        provider->settings.default_settings_json;
    tab.displays.push_back(display);
    tab.layout.selected_display_id = display.id;
    tab.layout.items.push_back({
        display.id,
        0,
        0,
        12,
        1,
    });
    ProjectMutationBatch replace_displays;
    replace_displays.operations.push_back(
        ReplaceDisplayHierarchyOperation{{tab}});
    const auto display_result =
        authority.mutate(
            authority.snapshot().etag,
            replace_displays);
    const auto* projected_display =
        display_result.active.projection.index.find_display(
            kDisplayId);
    require(
        projected_display &&
            projected_display->owner_unit_id == owner_id &&
            projected_display->public_source ==
                std::optional<SourceReference>{
                    SourceReference{fft_id, "fft"}} &&
            projected_display->source_block_id,
        "Display ownership/source did not project from project state");

    auto wrong_owner_tab = tab;
    wrong_owner_tab.displays[0].owner.unit_id =
        first_acquisition_id;
    ProjectMutationBatch wrong_owner;
    wrong_owner.operations.push_back(
        ReplaceDisplayHierarchyOperation{
            {wrong_owner_tab}});
    const auto before_wrong_owner =
        signature(authority.snapshot());
    require_authority_error(
        [&] {
            (void)authority.mutate(
                authority.snapshot().etag,
                wrong_owner);
        },
        "invalid_project_projection",
        "Cross-owned display was accepted");
    require_unchanged(
        authority,
        before_wrong_owner,
        "Invalid display ownership changed authority");

    ProjectMutationBatch remove_fft_reject;
    remove_fft_reject.operations.push_back(
        RemoveControlledUnitOperation{
            by_id(fft_id),
            DependantRemovalPolicy::Reject,
        });
    require_authority_error(
        [&] {
            (void)authority.mutate(
                authority.snapshot().etag,
                remove_fft_reject);
        },
        "controlled_unit_has_dependants",
        "Display-backed FFT removal ignored dependant policy");

    ProjectMutationBatch remove_fft;
    remove_fft.operations.push_back(
        RemoveControlledUnitOperation{
            by_id(fft_id),
            DependantRemovalPolicy::LeaveUnbound,
        });
    const auto removed_fft =
        authority.mutate(
            authority.snapshot().etag,
            remove_fft);
    require(
        removed_fft.active.project.display_tabs.size() == 1 &&
            removed_fft.active.project.display_tabs[0]
                    .displays.size() == 1 &&
            !removed_fft.active.project.display_tabs[0]
                 .displays[0]
                 .source &&
            removed_fft.active.projection.index
                .find_display(kDisplayId) &&
            !removed_fft.active.projection.index
                 .find_display(kDisplayId)
                 ->public_source,
        "Removing a display source did not preserve it unbound");

    ProjectMutationBatch remove_owner;
    remove_owner.operations.push_back(
        RemoveControlledUnitOperation{
            by_id(owner_id),
            DependantRemovalPolicy::Reject,
        });
    const auto removed_owner =
        authority.mutate(
            authority.snapshot().etag,
            remove_owner);
    require(
        removed_owner.active.project.display_tabs.empty() &&
            removed_owner.active.projection.index.displays.empty(),
        "Removing User Display retained orphan presentation state");
}

void check_ambiguous_source_addition_needs_configuration() {
    Fixture fixture("ambiguous-source-add");
    auto& authority = *fixture.authority;

    ProjectMutationBatch add_acquisition;
    add_acquisition.operations.push_back(
        add("source", std::string(kAcquisitionType)));
    const auto source_result = authority.mutate(
        authority.snapshot().etag,
        add_acquisition);
    const auto acquisition_id =
        created_id(source_result, "source");

    ProjectMutationBatch add_amplifier;
    add_amplifier.operations.push_back(
        add(
            "amplifier",
            std::string(kAmplifierType),
            DependencyPolicy::AddDefaults));
    const auto amplifier_result = authority.mutate(
        authority.snapshot().etag,
        add_amplifier);
    const auto amplifier_id =
        created_id(amplifier_result, "amplifier");
    require(
        binding_by_role(
            unit_by_id(amplifier_result.active, amplifier_id),
            "rawAudio").sources ==
            std::vector<SourceReference>{
                {acquisition_id, "rawAudio"}},
        "Single compatible source was not selected during add");

    ProjectMutationBatch add_patch;
    add_patch.operations.push_back(
        add(
            "patch",
            std::string(kPatchPanelType),
            DependencyPolicy::AddDefaults));
    const auto patch_result = authority.mutate(
        authority.snapshot().etag,
        add_patch);
    const auto patch_id = created_id(patch_result, "patch");
    const auto& patch_binding = binding_by_role(
        unit_by_id(patch_result.active, patch_id),
        "rawAudio");
    require(
        patch_result.active.projection.editor_valid(),
        "Ambiguous-source addition made the project editor-invalid");
    require(
        patch_result.active.projection.needs_configuration(),
        "Ambiguous-source addition did not report needs-configuration");
    require(
        patch_binding.sources.empty(),
        "Ambiguous-source addition selected an arbitrary binding");

    ProjectMutationBatch choose_source;
    choose_source.operations.push_back(
        SetControlledUnitBindingOperation{
            by_id(patch_id),
            "rawAudio",
            {
                {
                    by_id(amplifier_id),
                    "amplifiedAudio",
                },
            },
        });
    const auto configured = authority.mutate(
        authority.snapshot().etag,
        choose_source);
    require(
        configured.active.projection.runnable() &&
            binding_by_role(
                unit_by_id(configured.active, patch_id),
                "rawAudio").sources ==
                std::vector<SourceReference>{
                    {amplifier_id, "amplifiedAudio"}},
        "Choosing an ambiguous unit's source did not restore a runnable "
        "project");
}

std::set<std::string> runtime_node_ids(
    const ActiveProjectSnapshot& snapshot);

void check_click_detector_bundle_authority() {
    Fixture fixture("click-bundle");
    auto& authority = *fixture.authority;

    ProjectMutationBatch add_click;
    add_click.operations.push_back(
        add(
            "click",
            std::string(kClickDetectorType),
            DependencyPolicy::AddDefaults,
            "Harbour Clicks"));
    const auto added = authority.mutate(
        authority.snapshot().etag,
        add_click);
    const auto click_id = created_id(added, "click");
    const auto acquisitions =
        units_by_type(added.active, kAcquisitionType);
    const auto clicks =
        units_by_type(added.active, kClickDetectorType);
    require(
        acquisitions.size() == 1 && clicks.size() == 1,
        "Adding Click Detector did not add exactly one default Acquisition");

    const auto& click_unit = *clicks.front();
    require(
        click_unit.name == "Harbour Clicks" &&
            click_unit.bindings.size() == 1 &&
            click_unit.bindings.front().input_role == "rawAudio" &&
            click_unit.bindings.front().sources ==
                std::vector<SourceReference>{
                    {acquisitions.front()->id, "rawAudio"}},
        "Click Detector default source binding changed");
    const auto* descriptor =
        fixture.controlled.find_controlled_unit(
            std::string(kClickDetectorType));
    require(
        descriptor &&
            click_unit.settings_json.find(
                R"("groupingType":"all")") !=
                std::string::npos &&
            click_unit.settings_json.find(
                R"("thresholdDb":10)") !=
                std::string::npos &&
            click_unit.settings_json.find(
                R"("runOnline":false)") !=
                std::string::npos &&
            click_unit.settings_json.find(
                R"("enabled":false)") !=
                std::string::npos &&
            click_unit.settings_json.find(
                R"("minClicks":6)") !=
                std::string::npos,
        "Click Detector did not start with canonical Java defaults");

    require(
        added.active.project.display_tabs.size() == 1,
        "Click Detector did not create its one static display tab");
    const auto& tab = added.active.project.display_tabs.front();
    require(
        tab.id == "tab:" + click_id + ":click" &&
            tab.name == click_unit.name &&
            tab.owner ==
                DisplayOwner{click_id, "clickDisplay"} &&
            tab.displays.size() == 1 &&
            tab.layout.mode == DisplayLayoutMode::Grid &&
            tab.layout.columns == 12 &&
            tab.layout.items.size() == 1 &&
            tab.layout.items.front().width == 12 &&
            tab.layout.items.front().height == 8,
        "Click Detector static tab ownership/layout changed");
    const auto& display = tab.displays.front();
    require(
        display.id == "display:" + click_id + ":click" &&
            display.provider_type_id == "pamguard.click-display" &&
            display.owner ==
                DisplayOwner{click_id, "clickDisplay"} &&
            display.source ==
                std::optional<SourceReference>{
                    SourceReference{click_id, "clicks"}} &&
            tab.layout.selected_display_id ==
                std::optional<std::string>{display.id},
        "Click Detector static display identity/source changed");

    const auto runtime_count = std::count_if(
        added.active.projection.index.runtime_nodes.begin(),
        added.active.projection.index.runtime_nodes.end(),
        [&](const auto& node) {
            return node.owner_unit_id == click_id;
        });
    const auto internal_count = std::count_if(
        added.active.projection.index.connections.begin(),
        added.active.projection.index.connections.end(),
        [&](const auto& connection) {
            return connection.owner_unit_id == click_id &&
                connection.kind ==
                    ProjectedConnectionKind::Internal;
        });
    const auto output_count = std::count_if(
        added.active.projection.index.public_outputs.begin(),
        added.active.projection.index.public_outputs.end(),
        [&](const auto& output) {
            return output.unit_id == click_id;
        });
    const auto* projected_display =
        added.active.projection.index.find_display(display.id);
    require(
        runtime_count == 5 &&
            internal_count == 4 &&
            output_count == 9 &&
            projected_display &&
            projected_display->owner_unit_id == click_id &&
            projected_display->public_source == display.source &&
            projected_display->source_block_id &&
            added.active.projection.array_geometry &&
            added.active.projection.runnable(),
        "Click Detector hidden graph/display/global-Array projection changed");

    ProjectMutationBatch invalid_settings;
    invalid_settings.operations.push_back(
        ReplaceControlledUnitSettingsOperation{
            by_id(click_id),
            1,
            "{}",
        });
    const auto before_invalid =
        signature(authority.snapshot());
    require_authority_error(
        [&] {
            (void)authority.mutate(
                authority.snapshot().etag,
                invalid_settings);
        },
        "invalid_project_projection",
        "Click Detector accepted incomplete settings");
    require_unchanged(
        authority,
        before_invalid,
        "Rejected Click Detector settings changed authority");

    ProjectMutationBatch rename;
    rename.operations.push_back(
        RenameControlledUnitOperation{
            by_id(click_id),
            "Shelf Edge Clicks",
        });
    const auto renamed = authority.mutate(
        authority.snapshot().etag,
        rename);
    require(
        renamed.active.project.display_tabs.front().name ==
                "Shelf Edge Clicks" &&
            renamed.active.project.display_tabs.front().id ==
                tab.id &&
            renamed.active.project.display_tabs.front()
                    .displays.front().id == display.id,
        "Renaming Click Detector changed static identities or stale tab name");

    const auto saved = authority.save(
        authority.snapshot().etag);
    const auto saved_project_id = saved.project.project_id;
    const auto saved_project_json =
        project_document_to_canonical_json(saved.project);
    const auto saved_runtime_ids =
        runtime_node_ids(saved);
    const auto fresh = authority.new_project(
        saved.etag,
        "Temporary",
        {},
        false);
    const auto reopened = authority.open(
        fresh.etag,
        saved_project_id,
        true);
    require(
        project_document_to_canonical_json(reopened.project) ==
                saved_project_json &&
            runtime_node_ids(reopened) == saved_runtime_ids &&
            reopened.projection.index.find_display(display.id),
        "Save/restart changed Click Detector or static display identities");

    ProjectMutationBatch remove_click;
    remove_click.operations.push_back(
        RemoveControlledUnitOperation{
            by_id(click_id),
            DependantRemovalPolicy::Reject,
        });
    const auto removed = authority.mutate(
        authority.snapshot().etag,
        remove_click);
    require(
        units_by_type(removed.active, kClickDetectorType).empty() &&
            removed.active.project.display_tabs.empty() &&
            std::none_of(
                removed.active.projection.index.runtime_nodes.begin(),
                removed.active.projection.index.runtime_nodes.end(),
                [&](const auto& node) {
                    return node.owner_unit_id == click_id;
                }),
        "Removing Click Detector retained owned runtime/display state");
}

ProjectFileEnvelope envelope_for(
    ProjectDocument project,
    std::uint64_t authority_revision,
    std::uint64_t saved_revision) {
    ProjectFileEnvelope envelope;
    envelope.authority_revision = authority_revision;
    envelope.saved_revision = saved_revision;
    envelope.saved_at_unix_ms = 1785000000000;
    envelope.project = std::move(project);
    envelope.content_hash =
        project_content_hash(envelope.project);
    return envelope;
}

void check_sound_output_run_mode_instance_rules() {
    Fixture fixture("sound-output-run-modes");
    auto& authority = *fixture.authority;
    auto& store = *fixture.store;

    ProjectMutationBatch normal_additions;
    normal_additions.operations.push_back(
        add("normal-source", std::string(kAcquisitionType)));
    normal_additions.operations.push_back(
        add("normal-output-1", std::string(kSoundOutputType)));
    normal_additions.operations.push_back(
        add("normal-output-2", std::string(kSoundOutputType)));
    const auto normal_added = authority.mutate(
        authority.snapshot().etag,
        normal_additions);
    require(
        normal_added.active.project.mode == ProjectMode::Normal &&
            units_by_type(
                normal_added.active,
                kSoundOutputType).size() == 2,
        "Normal mode did not apply Sound Output's unlimited base maximum");

    const auto normal_outputs =
        units_by_type(normal_added.active, kSoundOutputType);
    const auto normal_output_1 = normal_outputs[0]->id;
    const auto normal_output_2 = normal_outputs[1]->id;
    ProjectMutationBatch normal_removals;
    normal_removals.operations.push_back(
        RemoveControlledUnitOperation{
            by_id(normal_output_1),
            DependantRemovalPolicy::LeaveUnbound,
        });
    normal_removals.operations.push_back(
        RemoveControlledUnitOperation{
            by_id(normal_output_2),
            DependantRemovalPolicy::LeaveUnbound,
        });
    const auto normal_removed = authority.mutate(
        authority.snapshot().etag,
        normal_removals);
    require(
        units_by_type(
            normal_removed.active,
            kSoundOutputType).empty(),
        "Normal mode did not apply Sound Output's zero base minimum");

    auto mixed_project = normal_removed.active.project;
    mixed_project.project_id = generate_uuid_v4();
    mixed_project.metadata.name = "Mixed Sound Output Rules";
    mixed_project.mode = ProjectMode::Mixed;
    const auto mixed_project_id = mixed_project.project_id;
    (void)store.create(
        envelope_for(
            std::move(mixed_project),
            normal_removed.active.authority_revision,
            normal_removed.active.working_revision));
    const auto mixed_opened = authority.open(
        authority.snapshot().etag,
        mixed_project_id,
        true);
    require(
        mixed_opened.project.mode == ProjectMode::Mixed &&
            units_by_type(
                mixed_opened,
                kSoundOutputType).empty(),
        "Mixed project with zero Sound Outputs was rejected");

    ProjectMutationBatch mixed_additions;
    mixed_additions.operations.push_back(
        add("mixed-output-1", std::string(kSoundOutputType)));
    mixed_additions.operations.push_back(
        add("mixed-output-2", std::string(kSoundOutputType)));
    const auto mixed_added = authority.mutate(
        authority.snapshot().etag,
        mixed_additions);
    require(
        units_by_type(
            mixed_added.active,
            kSoundOutputType).size() == 2,
        "Mixed mode did not apply Sound Output's unlimited base maximum");

    const auto mixed_outputs =
        units_by_type(mixed_added.active, kSoundOutputType);
    ProjectMutationBatch mixed_removals;
    mixed_removals.operations.push_back(
        RemoveControlledUnitOperation{
            by_id(mixed_outputs[0]->id),
            DependantRemovalPolicy::LeaveUnbound,
        });
    mixed_removals.operations.push_back(
        RemoveControlledUnitOperation{
            by_id(mixed_outputs[1]->id),
            DependantRemovalPolicy::LeaveUnbound,
        });
    const auto mixed_removed = authority.mutate(
        authority.snapshot().etag,
        mixed_removals);
    require(
        units_by_type(
            mixed_removed.active,
            kSoundOutputType).empty(),
        "Mixed mode did not apply Sound Output's zero base minimum");

    ProjectMutationBatch viewer_seed;
    viewer_seed.operations.push_back(
        add("viewer-output", std::string(kSoundOutputType)));
    const auto seeded = authority.mutate(
        authority.snapshot().etag,
        viewer_seed);
    auto viewer_project = seeded.active.project;
    viewer_project.project_id = generate_uuid_v4();
    viewer_project.metadata.name = "Viewer Sound Output Rules";
    viewer_project.mode = ProjectMode::Viewer;
    const auto viewer_project_id = viewer_project.project_id;
    (void)store.create(
        envelope_for(
            std::move(viewer_project),
            seeded.active.authority_revision,
            seeded.active.working_revision));
    const auto viewer_opened = authority.open(
        authority.snapshot().etag,
        viewer_project_id,
        true);
    const auto viewer_outputs =
        units_by_type(viewer_opened, kSoundOutputType);
    require(
        viewer_opened.project.mode == ProjectMode::Viewer &&
            viewer_outputs.size() == 1,
        "Viewer project did not accept exactly one Sound Output");

    ProjectMutationBatch second_viewer_output;
    second_viewer_output.operations.push_back(
        add("viewer-output-2", std::string(kSoundOutputType)));
    require_authority_error(
        [&] {
            (void)authority.mutate(
                authority.snapshot().etag,
                second_viewer_output);
        },
        "maximum_instances",
        "Viewer mode accepted a second Sound Output");

    ProjectMutationBatch remove_viewer_output;
    remove_viewer_output.operations.push_back(
        RemoveControlledUnitOperation{
            by_id(viewer_outputs.front()->id),
            DependantRemovalPolicy::LeaveUnbound,
        });
    require_authority_error(
        [&] {
            (void)authority.mutate(
                authority.snapshot().etag,
                remove_viewer_output);
        },
        "minimum_instances",
        "Viewer mode removed its required Sound Output");
}

std::set<std::string> runtime_node_ids(
    const ActiveProjectSnapshot& snapshot) {
    std::set<std::string> result;
    for (const auto& node :
         snapshot.projection.index.runtime_nodes) {
        result.insert(node.runtime_node_id);
    }
    return result;
}

struct ClickMonitoringBranchIds {
    std::string acquisition;
    std::string fft;
    std::string user_display;
    std::string click_detector;
    std::string sound_output;
};

ClickMonitoringBranchIds require_click_monitoring_branch(
    const ProjectMutationResult& result,
    const std::string& client_ref,
    bool acquisition_created,
    std::optional<std::string> reused_acquisition =
        std::nullopt) {
    const auto child_ref =
        [&](std::string_view role) {
            return client_ref + ":" + std::string(role);
        };
    ClickMonitoringBranchIds ids;
    if (acquisition_created) {
        ids.acquisition =
            created_id(result, child_ref("acquisition"));
    }
    else {
        require(
            reused_acquisition.has_value(),
            "Reused template Acquisition ID was not supplied");
        require(
            !has_created_id(
                result,
                child_ref("acquisition")),
            "Reused Acquisition was reported as a created entity");
        ids.acquisition = *reused_acquisition;
    }
    ids.fft = created_id(result, child_ref("fft"));
    ids.user_display =
        created_id(result, child_ref("userDisplay"));
    ids.click_detector =
        created_id(result, child_ref("clickDetector"));
    ids.sound_output =
        created_id(result, child_ref("soundOutput"));
    require(
        result.created_entities.size() ==
            (acquisition_created ? 5 : 4),
        "Configuration template returned an unexpected created-ID set");

    const std::set<std::string> unique_ids{
        ids.acquisition,
        ids.fft,
        ids.user_display,
        ids.click_detector,
        ids.sound_output,
    };
    require(
        unique_ids.size() == 5 &&
            std::all_of(
                unique_ids.begin(),
                unique_ids.end(),
                [](const auto& id) {
                    return is_uuid_v4(id);
                }),
        "Configuration template did not allocate unique UUIDv4 unit IDs");

    const auto& acquisition =
        unit_by_id(result.active, ids.acquisition);
    const auto& fft = unit_by_id(result.active, ids.fft);
    const auto& user_display =
        unit_by_id(result.active, ids.user_display);
    const auto& click =
        unit_by_id(result.active, ids.click_detector);
    const auto& sound =
        unit_by_id(result.active, ids.sound_output);
    require(
        acquisition.type_id == kAcquisitionType &&
            fft.type_id == kFftType &&
            user_display.type_id == kUserDisplayType &&
            click.type_id == kClickDetectorType &&
            sound.type_id == kSoundOutputType,
        "Configuration template created the wrong controlled-unit types");
    require(
        acquisition.settings_json.find("\"sourceId\"") ==
            std::string::npos,
        "Configuration template persisted a host Acquisition source");

    const auto require_single_source =
        [&](const ControlledUnitInstance& unit,
            std::string_view input_role) {
        const auto& binding =
            binding_by_role(unit, input_role);
        require(
            binding.sources ==
                std::vector<SourceReference>{
                    {ids.acquisition, "rawAudio"},
                },
            "Configuration template branch did not bind directly "
            "to its selected Acquisition");
    };
    require_single_source(fft, "rawAudio");
    require_single_source(click, "rawAudio");
    require_single_source(sound, "audio");

    const auto& spectrogram_tab =
        tab_by_owner(
            result.active,
            ids.user_display,
            "main");
    require(
        spectrogram_tab.displays.size() == 1 &&
            spectrogram_tab.layout.mode ==
                DisplayLayoutMode::Grid &&
            spectrogram_tab.layout.columns == 12 &&
            spectrogram_tab.layout.items.size() == 1,
        "Template User Display did not own exactly one grid Spectrogram");
    const auto& spectrogram =
        spectrogram_tab.displays.front();
    require(
        spectrogram.provider_type_id ==
                kSpectrogramProvider &&
            spectrogram.owner ==
                DisplayOwner{ids.user_display, "provider"} &&
            spectrogram.source ==
                std::optional<SourceReference>{
                    {ids.fft, "fft"}} &&
            spectrogram_tab.layout.selected_display_id ==
                std::optional<std::string>{spectrogram.id} &&
            spectrogram_tab.layout.items.front().display_id ==
                spectrogram.id,
        "Template Spectrogram ownership or FFT source changed");

    const auto& click_tab =
        tab_by_owner(
            result.active,
            ids.click_detector,
            "clickDisplay");
    require(
        click_tab.displays.size() == 1 &&
            click_tab.layout.items.size() == 1,
        "Template Click Detector did not retain its static Click display");
    const auto& click_display =
        click_tab.displays.front();
    require(
        click_display.provider_type_id ==
                kClickDisplayProvider &&
            click_display.owner ==
                DisplayOwner{
                    ids.click_detector,
                    "clickDisplay"} &&
            click_display.source ==
                std::optional<SourceReference>{
                    {ids.click_detector, "clicks"}} &&
            click_tab.layout.selected_display_id ==
                std::optional<std::string>{click_display.id},
        "Static Click display ownership or source changed");
    require(
        spectrogram.id != click_display.id &&
            spectrogram_tab.id != click_tab.id &&
            result.active.projection.index.find_display(
                spectrogram.id) &&
            result.active.projection.index.find_display(
                click_display.id),
        "Configuration template display IDs are not unique/projected");

    require(
        result.active.projection.needs_configuration() &&
            std::any_of(
                result.active.projection.issues.begin(),
                result.active.projection.issues.end(),
                [&](const auto& issue) {
                    return issue.issue_class ==
                            ProjectionIssueClass::
                                NeedsConfiguration &&
                        issue.code ==
                            "sound-output-no-channels" &&
                        issue.unit_id == ids.sound_output;
                }) &&
            sound.settings_json.find(
                "\"channelBitmap\":0") !=
                std::string::npos,
        "Java-default Sound Output did not remain NeedsConfiguration");
    return ids;
}

void check_click_monitoring_configuration_template() {
    Fixture fixture("click-monitoring-template");
    auto& authority = *fixture.authority;

    ProjectMutationBatch validate_only;
    validate_only.validate_only = true;
    validate_only.operations.push_back(
        AddConfigurationTemplateOperation{
            "monitor",
            std::string(
                kClickMonitoringConfigurationTemplateId),
        });
    const auto blank = signature(authority.snapshot());
    const auto validation = authority.mutate(
        authority.snapshot().etag,
        validate_only);
    (void)require_click_monitoring_branch(
        validation,
        "monitor",
        true);
    require(
        validation.changed &&
            validation.validated_only &&
            validation.active.project.controlled_units.size() ==
                5 &&
            validation.active.project.display_tabs.size() ==
                2,
        "Validate-only template did not return the complete candidate");
    require_unchanged(
        authority,
        blank,
        "Validate-only configuration template changed authority");

    ProjectMutationBatch commit_batch = validate_only;
    commit_batch.validate_only = false;
    auto prepared = authority.prepare_mutation(
        authority.snapshot().etag,
        commit_batch);
    const auto preview_project =
        prepared.preview().active.project;
    const auto preview_index =
        prepared.preview().active.projection.index;
    const auto preview_issues =
        prepared.preview().active.projection.issues;
    const auto preview_graph =
        pamguard::core::module_graph_to_json(
            prepared.preview().active.projection.graph);
    const auto preview_created =
        prepared.preview().created_entities;
    const auto prepared_ids =
        require_click_monitoring_branch(
            prepared.preview(),
            "monitor",
            true);
    const auto committed =
        authority.commit_mutation(std::move(prepared));
    require(
        committed.active.project == preview_project &&
            committed.active.projection.index ==
                preview_index &&
            committed.active.projection.issues ==
                preview_issues &&
            pamguard::core::module_graph_to_json(
                committed.active.projection.graph) ==
                preview_graph &&
            committed.created_entities ==
                preview_created,
        "Committed template differed from its exact prepared preview");
    const auto first_ids =
        require_click_monitoring_branch(
            committed,
            "monitor",
            true);
    require(
        first_ids.acquisition ==
                prepared_ids.acquisition &&
            first_ids.fft == prepared_ids.fft &&
            first_ids.user_display ==
                prepared_ids.user_display &&
            first_ids.click_detector ==
                prepared_ids.click_detector &&
            first_ids.sound_output ==
                prepared_ids.sound_output,
        "Prepared template identities changed during commit");
    require(
        committed.active.working_revision == 1 &&
            committed.active.authority_revision == 1,
        "Template commit did not advance one atomic project revision");

    const auto first_project = committed.active.project;
    ProjectMutationBatch repeat;
    repeat.operations.push_back(
        AddConfigurationTemplateOperation{
            "secondMonitor",
            std::string(
                kClickMonitoringConfigurationTemplateId),
        });
    const auto repeated = authority.mutate(
        authority.snapshot().etag,
        repeat);
    const auto repeated_ids =
        require_click_monitoring_branch(
            repeated,
            "secondMonitor",
            false,
            first_ids.acquisition);
    require(
        units_by_type(repeated.active, kAcquisitionType)
                    .size() == 1 &&
            units_by_type(repeated.active, kFftType)
                    .size() == 2 &&
            units_by_type(repeated.active, kUserDisplayType)
                    .size() == 2 &&
            units_by_type(repeated.active, kClickDetectorType)
                    .size() == 2 &&
            units_by_type(repeated.active, kSoundOutputType)
                    .size() == 2 &&
            unit_by_id(repeated.active, repeated_ids.fft).name ==
                "FFT (Spectrogram) Engine 2" &&
            unit_by_id(
                repeated.active,
                repeated_ids.user_display).name ==
                "User Display 2" &&
            unit_by_id(
                repeated.active,
                repeated_ids.click_detector).name ==
                "Click Detector 2" &&
            unit_by_id(
                repeated.active,
                repeated_ids.sound_output).name ==
                "Sound Output 2",
        "Repeated Normal-mode template did not create independent "
        "PAMGuard-style branches");
    for (const auto& original :
         first_project.controlled_units) {
        require(
            unit_by_id(repeated.active, original.id) ==
                original,
            "Repeated template merged or changed an existing unit");
    }
    for (const auto& original :
         first_project.display_tabs) {
        const auto found = std::find_if(
            repeated.active.project.display_tabs.begin(),
            repeated.active.project.display_tabs.end(),
            [&](const auto& tab) {
                return tab.id == original.id;
            });
        require(
            found !=
                    repeated.active.project.display_tabs.end() &&
                *found == original,
            "Repeated template merged or changed an existing display tab");
    }

    const auto saved = authority.save(
        authority.snapshot().etag);
    const auto saved_project_id =
        saved.project.project_id;
    const auto saved_project_json =
        project_document_to_canonical_json(saved.project);
    const auto saved_index = saved.projection.index;
    const auto saved_runtime_ids =
        runtime_node_ids(saved);
    fixture.authority.reset();
    fixture.authority.emplace(
        fixture.controlled,
        fixture.runtime,
        *fixture.store);
    auto& restarted = *fixture.authority;
    const auto reopened = restarted.open(
        restarted.snapshot().etag,
        saved_project_id,
        true);
    require(
        project_document_to_canonical_json(
            reopened.project) ==
                saved_project_json &&
            reopened.projection.index == saved_index &&
            runtime_node_ids(reopened) ==
                saved_runtime_ids &&
            reopened.projection.needs_configuration(),
        "Save/restart changed configuration template identities, "
        "ownership, bindings, or readiness");

    {
        Fixture reuse_fixture("template-reuse");
        auto& reuse_authority = *reuse_fixture.authority;
        ProjectMutationBatch existing_branch;
        existing_branch.operations = {
            add(
                "existingAcquisition",
                std::string(kAcquisitionType)),
            add(
                "existingFft",
                std::string(kFftType)),
            add(
                "existingDisplay",
                std::string(kUserDisplayType)),
            add(
                "existingClick",
                std::string(kClickDetectorType)),
        };
        const auto existing = reuse_authority.mutate(
            reuse_authority.snapshot().etag,
            existing_branch);
        const auto acquisition_id =
            created_id(existing, "existingAcquisition");
        const auto existing_project =
            existing.active.project;

        ProjectMutationBatch reuse_template;
        reuse_template.operations.push_back(
            AddConfigurationTemplateOperation{
                "secondMonitor",
                std::string(
                    kClickMonitoringConfigurationTemplateId),
            });
        const auto reused = reuse_authority.mutate(
            reuse_authority.snapshot().etag,
            reuse_template);
        const auto second_ids =
            require_click_monitoring_branch(
                reused,
                "secondMonitor",
                false,
                acquisition_id);
        require(
            units_by_type(reused.active, kAcquisitionType)
                        .size() == 1 &&
                units_by_type(reused.active, kFftType)
                        .size() == 2 &&
                units_by_type(reused.active, kUserDisplayType)
                        .size() == 2 &&
                units_by_type(reused.active, kClickDetectorType)
                        .size() == 2 &&
                units_by_type(reused.active, kSoundOutputType)
                        .size() == 1 &&
                unit_by_id(reused.active, second_ids.fft).name ==
                    "FFT (Spectrogram) Engine 2" &&
                unit_by_id(
                    reused.active,
                    second_ids.user_display).name ==
                    "User Display 2" &&
                unit_by_id(
                    reused.active,
                    second_ids.click_detector).name ==
                    "Click Detector 2" &&
                unit_by_id(
                    reused.active,
                    second_ids.sound_output).name ==
                    "Sound Output",
            "Template did not reuse one Acquisition and generate "
            "PAMGuard-style unique branch names");
        for (const auto& original :
             existing_project.controlled_units) {
            require(
                unit_by_id(reused.active, original.id) ==
                    original,
                "Template merged or changed an existing unit");
        }
        for (const auto& original :
             existing_project.display_tabs) {
            const auto found = std::find_if(
                reused.active.project.display_tabs.begin(),
                reused.active.project.display_tabs.end(),
                [&](const auto& tab) {
                    return tab.id == original.id;
                });
            require(
                found !=
                        reused.active.project.display_tabs.end() &&
                    *found == original,
                "Template merged or changed an existing display tab");
        }
    }

    {
        Fixture rollback_fixture("template-rollback");
        auto& rollback_authority =
            *rollback_fixture.authority;
        const auto before =
            signature(rollback_authority.snapshot());
        ProjectMutationBatch failing;
        failing.operations.push_back(
            AddConfigurationTemplateOperation{
                "rollback",
                std::string(
                    kClickMonitoringConfigurationTemplateId),
            });
        failing.operations.push_back(
            RenameControlledUnitOperation{
                by_id(std::string(kUnknownUnitId)),
                "Must Not Commit",
            });
        require_authority_error(
            [&] {
                (void)rollback_authority.mutate(
                    rollback_authority.snapshot().etag,
                    failing);
            },
            "controlled_unit_not_found",
            "Trailing failure committed part of a configuration template");
        require_unchanged(
            rollback_authority,
            before,
            "Failed configuration template batch changed authority");
    }

    {
        Fixture ambiguous_fixture("template-ambiguous");
        auto& ambiguous_authority =
            *ambiguous_fixture.authority;
        ProjectMutationBatch two_acquisitions;
        two_acquisitions.operations = {
            add(
                "inputA",
                std::string(kAcquisitionType)),
            add(
                "inputB",
                std::string(kAcquisitionType)),
        };
        (void)ambiguous_authority.mutate(
            ambiguous_authority.snapshot().etag,
            two_acquisitions);
        const auto before =
            signature(ambiguous_authority.snapshot());
        ProjectMutationBatch ambiguous_template;
        ambiguous_template.operations.push_back(
            AddConfigurationTemplateOperation{
                "ambiguous",
                std::string(
                    kClickMonitoringConfigurationTemplateId),
            });
        require_authority_error(
            [&] {
                (void)ambiguous_authority.mutate(
                    ambiguous_authority.snapshot().etag,
                    ambiguous_template);
            },
            "ambiguous_template_acquisition",
            "Template guessed between multiple Acquisitions");
        require_unchanged(
            ambiguous_authority,
            before,
            "Ambiguous template selection changed authority");
    }

    {
        Fixture cas_fixture("template-cas");
        auto& cas_authority = *cas_fixture.authority;
        const auto base_etag =
            cas_authority.snapshot().etag;
        ProjectMutationBatch template_batch;
        template_batch.operations.push_back(
            AddConfigurationTemplateOperation{
                "staleTemplate",
                std::string(
                    kClickMonitoringConfigurationTemplateId),
            });
        auto stale_prepared =
            cas_authority.prepare_mutation(
                base_etag,
                template_batch);
        ProjectMutationBatch intervening;
        intervening.operations.push_back(
            add(
                "winner",
                std::string(kAcquisitionType)));
        const auto winner = cas_authority.mutate(
            base_etag,
            intervening);
        const auto after_winner =
            signature(winner.active);
        require_authority_error(
            [&] {
                (void)cas_authority.commit_mutation(
                    std::move(stale_prepared));
            },
            "precondition_failed",
            "Stale prepared template overwrote an intervening writer");
        require_unchanged(
            cas_authority,
            after_winner,
            "Rejected stale template changed authority");
        require_authority_error(
            [&] {
                (void)cas_authority.prepare_mutation(
                    base_etag,
                    template_batch);
            },
            "precondition_failed",
            "Template accepted a stale HTTP-style ETag");
        require_unchanged(
            cas_authority,
            after_winner,
            "Rejected stale template preparation changed authority");
    }
}

void check_persistence_switches_and_failure_preservation() {
    Fixture fixture("persistence");
    auto& authority = *fixture.authority;
    auto& store = *fixture.store;

    const auto added = add_fft_with_default_source(authority);
    const auto fft_id = created_id(added, "fft");
    const auto pre_save_ids =
        runtime_node_ids(authority.snapshot());
    const auto first_save =
        authority.save(authority.snapshot().etag);
    require(
        !first_save.dirty &&
            first_save.saved_revision ==
                std::optional<std::uint64_t>{
                    first_save.working_revision} &&
            first_save.saved_content_hash ==
                std::optional<std::string>{
                    first_save.working_content_hash} &&
            first_save.authority_revision ==
                added.active.authority_revision + 1 &&
            store.exists(first_save.project.project_id),
        "Save did not advance the durable baseline atomically");
    const auto loaded_first =
        store.load(first_save.project.project_id);
    require(
        loaded_first.envelope.project ==
                first_save.project &&
            loaded_first.envelope.content_hash ==
                first_save.working_content_hash,
        "Saved project did not round-trip exactly");

    ProjectMutationBatch local_change;
    local_change.operations.push_back(
        RenameControlledUnitOperation{
            by_id(fft_id),
            "Locally Edited FFT",
        });
    const auto local =
        authority.mutate(
            first_save.etag,
            local_change);
    require(local.active.dirty, "Working edit did not become dirty");

    auto external = loaded_first.envelope;
    external.project.metadata.description =
        "External cooperating writer";
    external.authority_revision += 1;
    external.saved_revision += 1;
    external.saved_at_unix_ms += 1;
    external.content_hash =
        project_content_hash(external.project);
    const auto external_fingerprint =
        store.replace(
            external,
            loaded_first.fingerprint);
    const auto before_conflict =
        signature(authority.snapshot());
    const auto conflict = require_authority_error(
        [&] {
            (void)authority.save(
                authority.snapshot().etag);
        },
        "durable_conflict",
        "Stale durable fingerprint overwrote an external writer");
    require(
        conflict.current_etag() ==
            authority.snapshot().etag,
        "Durable conflict omitted the current authority ETag");
    require_unchanged(
        authority,
        before_conflict,
        "Failed durable save advanced the saved baseline");
    require(
        store.load(first_save.project.project_id)
                .fingerprint ==
            external_fingerprint,
        "Failed durable save changed the external file");

    require_authority_error(
        [&] {
            (void)authority.prepare_open(
                authority.snapshot().etag,
                first_save.project.project_id,
                false);
        },
        "dirty_project",
        "Open silently discarded a dirty working project");

    auto prepared_open =
        authority.prepare_open(
            authority.snapshot().etag,
            first_save.project.project_id,
            true);
    require(
        !prepared_open.preview().dirty &&
            prepared_open.preview().project.metadata.description ==
                "External cooperating writer",
        "Prepared Open did not expose the durable target");
    require_unchanged(
        authority,
        before_conflict,
        "Preparing Open replaced active authority");
    const auto reopened =
        authority.commit_project_switch(
            std::move(prepared_open));
    require(
        !reopened.dirty &&
            reopened.project ==
                external.project &&
            reopened.saved_revision ==
                std::optional<std::uint64_t>{
                    external.saved_revision},
        "Committed Open did not adopt the prepared durable target");

    const auto before_failed_open =
        signature(authority.snapshot());
    require_throws<ProjectStoreError>(
        [&] {
            (void)authority.prepare_open(
                authority.snapshot().etag,
                std::string(kMissingProjectId),
                true);
        },
        "Open accepted a missing durable project");
    require_unchanged(
        authority,
        before_failed_open,
        "Missing Open target changed authority");

    ProjectDocument invalid_project;
    invalid_project.project_id =
        std::string(kInvalidProjectId);
    invalid_project.metadata = {
        "Unsupported unit fixture",
        "Valid project JSON with no current descriptor",
    };
    invalid_project.descriptor_set = {
        std::string(kControlledUnitDescriptorSetId),
        kControlledUnitDescriptorSetVersion,
    };
    invalid_project.controlled_units.push_back({
        std::string(kUnknownUnitId),
        "pamguard.unknown",
        1,
        {"pamguard.unknown.runtime", 1},
        "Unknown Unit",
        1,
        "{}",
        {},
    });
    (void)store.create(
        envelope_for(
            std::move(invalid_project),
            1,
            0));
    require_authority_error(
        [&] {
            (void)authority.prepare_open(
                authority.snapshot().etag,
                std::string(kInvalidProjectId),
                true);
        },
        "invalid_project_projection",
        "Open accepted an editor-invalid project");
    require_unchanged(
        authority,
        before_failed_open,
        "Invalid projected Open target changed authority");

    const auto old_project_id =
        authority.snapshot().project.project_id;

    auto stale_save_as =
        authority.prepare_save_as(
            authority.snapshot().etag,
            "Stale Save As");
    const auto stale_save_as_id =
        stale_save_as.preview().project.project_id;
    require(
        !store.exists(stale_save_as_id),
        "Preparing Save As wrote a durable file");
    ProjectMutationBatch save_as_race;
    save_as_race.operations.push_back(
        RenameControlledUnitOperation{
            by_id(fft_id),
            "Save As Intervening Writer",
        });
    (void)authority.mutate(
        authority.snapshot().etag,
        save_as_race);
    const auto after_save_as_race =
        signature(authority.snapshot());
    require_authority_error(
        [&] {
            (void)authority.commit_save_as(
                std::move(stale_save_as));
        },
        "precondition_failed",
        "A stale prepared Save As overwrote an edit");
    require(
        !store.exists(stale_save_as_id),
        "Stale Save As commit created a durable file");
    require_unchanged(
        authority,
        after_save_as_race,
        "Stale Save As commit changed authority");

    auto colliding_save_as =
        authority.prepare_save_as(
            authority.snapshot().etag,
            "Colliding Save As");
    const auto collision_preview =
        colliding_save_as.preview();
    (void)store.create(
        envelope_for(
            collision_preview.project,
            collision_preview.authority_revision,
            *collision_preview.saved_revision));
    const auto before_collision =
        signature(authority.snapshot());
    require_authority_error(
        [&] {
            (void)authority.commit_save_as(
                std::move(colliding_save_as));
        },
        "durable_conflict",
        "Prepared Save As overwrote an existing project ID");
    require_unchanged(
        authority,
        before_collision,
        "Failed prepared Save As changed authority");

    const auto before_save_as =
        authority.snapshot();
    auto prepared_save_as =
        authority.prepare_save_as(
            before_save_as.etag,
            "Cloned Project");
    const auto prepared_save_as_id =
        prepared_save_as.preview().project.project_id;
    require(
        !store.exists(prepared_save_as_id) &&
            prepared_save_as.preview().project
                    .controlled_units ==
                before_save_as.project.controlled_units &&
            runtime_node_ids(prepared_save_as.preview()) ==
                pre_save_ids,
        "Prepared Save As changed identities or wrote early");
    require_unchanged(
        authority,
        signature(before_save_as),
        "Preparing Save As changed authority");
    const auto saved_as =
        authority.commit_save_as(
            std::move(prepared_save_as));
    require(
        saved_as.project.project_id ==
                prepared_save_as_id &&
            saved_as.project.project_id != old_project_id &&
            saved_as.project.metadata.name ==
                "Cloned Project" &&
            saved_as.project.controlled_units ==
                before_save_as.project.controlled_units &&
            runtime_node_ids(saved_as) == pre_save_ids &&
            !saved_as.dirty &&
            store.exists(old_project_id) &&
            store.exists(saved_as.project.project_id),
        "Save As did not preserve stable unit/runtime identities");

    const auto before_prepared_new =
        signature(authority.snapshot());
    auto prepared_new =
        authority.prepare_new_project(
            authority.snapshot().etag,
            "Prepared New",
            "Candidate only",
            false);
    require(
        prepared_new.preview().project.project_id !=
                authority.snapshot().project.project_id &&
            prepared_new.preview().project.controlled_units
                .empty() &&
            prepared_new.preview().dirty,
        "Prepared New candidate is not a clean blank model");
    require_unchanged(
        authority,
        before_prepared_new,
        "Preparing New replaced active authority");

    ProjectMutationBatch intervening;
    intervening.operations.push_back(
        RenameControlledUnitOperation{
            by_id(fft_id),
            "Intervening Writer",
        });
    const auto intervened =
        authority.mutate(
            authority.snapshot().etag,
            intervening);
    const auto after_intervening =
        signature(intervened.active);
    require_authority_error(
        [&] {
            (void)authority.commit_project_switch(
                std::move(prepared_new));
        },
        "precondition_failed",
        "A stale prepared project switch overwrote an edit");
    require_unchanged(
        authority,
        after_intervening,
        "Rejected prepared switch changed authority");

    require_authority_error(
        [&] {
            (void)authority.new_project(
                authority.snapshot().etag,
                "New Project",
                {},
                false);
        },
        "dirty_project",
        "New silently discarded dirty changes");
    const auto before_invalid_new =
        signature(authority.snapshot());
    require_authority_error(
        [&] {
            (void)authority.prepare_new_project(
                authority.snapshot().etag,
                {},
                {},
                true);
        },
        "invalid_project",
        "New accepted an empty project name");
    require_unchanged(
        authority,
        before_invalid_new,
        "Invalid New candidate changed authority");

    const auto fresh =
        authority.new_project(
            authority.snapshot().etag,
            "Fresh Project",
            "Explicitly discarded",
            true);
    require(
        fresh.project.project_id !=
                saved_as.project.project_id &&
            fresh.project.controlled_units.empty() &&
            fresh.working_revision == 0 &&
            fresh.authority_revision == 0 &&
            fresh.dirty,
        "Committed New did not reset project-local authority");

    const auto opened_clone =
        authority.open(
            fresh.etag,
            saved_as.project.project_id,
            true);
    require(
        opened_clone.project.project_id ==
                saved_as.project.project_id &&
            opened_clone.project.controlled_units ==
                saved_as.project.controlled_units &&
            runtime_node_ids(opened_clone) == pre_save_ids &&
            !opened_clone.dirty,
        "Open did not restore stable saved identities");

    const auto before_io_failure =
        signature(authority.snapshot());
    auto moved_root = fixture.root;
    moved_root += ".moved";
    std::filesystem::rename(fixture.root, moved_root);
    std::filesystem::create_directories(fixture.root);
    const auto io_error = require_authority_error(
        [&] {
            (void)authority.save_as(
                authority.snapshot().etag,
                "Must Fail");
        },
        "project_save_failed",
        "Save As ignored replaced project-store root");
    require(
        io_error.current_etag() ==
            authority.snapshot().etag,
        "Save failure omitted the current authority ETag");
    require_unchanged(
        authority,
        before_io_failure,
        "Failed Save As changed active authority");
    std::filesystem::remove_all(fixture.root);
    std::filesystem::rename(moved_root, fixture.root);
}

} // namespace

int main() {
    try {
        check_global_array_settings_authority();
        check_blank_preconditions_staging_and_rules();
        check_sound_output_run_mode_instance_rules();
        check_ambiguous_source_addition_needs_configuration();
        check_settings_bindings_layout_and_displays();
        check_click_detector_bundle_authority();
        check_click_monitoring_configuration_template();
        check_persistence_switches_and_failure_preservation();
        std::cout
            << "Project authority check passed: exact staged mutations/"
               "switches, ETag concurrency, typed atomic edits, source and "
               "display ownership, configuration templates, and durable "
               "rollback\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr
            << "Project authority check failed: "
            << error.what() << "\n";
        return 1;
    }
}
