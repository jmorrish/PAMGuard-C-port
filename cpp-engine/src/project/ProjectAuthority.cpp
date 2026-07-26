#include "pamguard/project/ProjectAuthority.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <functional>
#include <limits>
#include <set>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "pamguard/project/ControlledUnitJson.h"
#include "pamguard/project/ProjectIdentity.h"
#include "pamguard/project/ProjectJson.h"

namespace pamguard::project {

namespace {

constexpr std::uint32_t kMutationSchemaVersion = 1;

[[noreturn]] void reject(
    std::string code,
    std::string message) {
    throw ProjectAuthorityError(
        std::move(code),
        std::move(message));
}

std::string ascii_fold(std::string_view value) {
    std::string folded(value);
    std::transform(
        folded.begin(),
        folded.end(),
        folded.begin(),
        [](unsigned char character) {
            return static_cast<char>(
                character >= 'A' && character <= 'Z'
                    ? character - 'A' + 'a'
                    : character);
        });
    return folded;
}

RunMode registry_mode(ProjectMode mode) {
    switch (mode) {
    case ProjectMode::Normal:
        return RunMode::Normal;
    case ProjectMode::Mixed:
        return RunMode::Mixed;
    case ProjectMode::Viewer:
        return RunMode::Viewer;
    }
    return RunMode::Normal;
}

bool contains_mode(
    const InstanceRulesDescriptor& rules,
    ProjectMode mode) {
    const auto wanted = registry_mode(mode);
    return std::find(
               rules.allowed_modes.begin(),
               rules.allowed_modes.end(),
               wanted) != rules.allowed_modes.end();
}

bool contains_all_capabilities(
    const std::vector<std::string>& provided,
    const std::vector<std::string>& required) {
    return std::all_of(
        required.begin(),
        required.end(),
        [&](const auto& capability) {
            return std::find(
                       provided.begin(),
                       provided.end(),
                       capability) != provided.end();
        });
}

std::string recipe_id_for(
    const ControlledUnitDescriptor& descriptor) {
    if (descriptor.runtime_recipe.id.empty()) {
        reject(
            "invalid_controlled_unit_recipe",
            "Controlled-unit type '" + descriptor.id +
                "' has no stable expansion recipe ID");
    }
    return descriptor.runtime_recipe.id;
}

const PublicDataRoleDescriptor* find_role(
    const ControlledUnitDescriptor& descriptor,
    std::string_view role_id,
    DataRoleDirection direction) {
    const auto found = std::find_if(
        descriptor.public_roles.begin(),
        descriptor.public_roles.end(),
        [&](const auto& role) {
            return role.id == role_id &&
                role.direction == direction;
        });
    return found == descriptor.public_roles.end()
        ? nullptr
        : &*found;
}

std::size_t count_type(
    const ProjectDocument& project,
    std::string_view type_id) {
    return static_cast<std::size_t>(
        std::count_if(
            project.controlled_units.begin(),
            project.controlled_units.end(),
            [&](const auto& unit) {
                return unit.type_id == type_id;
            }));
}

std::string unique_unit_name(
    const ProjectDocument& project,
    const ControlledUnitRegistry& registry,
    const ControlledUnitDescriptor& descriptor,
    std::optional<std::string> requested) {
    auto conflicts = [&](std::string_view candidate) {
        const auto folded = ascii_fold(candidate);
        return std::any_of(
            project.controlled_units.begin(),
            project.controlled_units.end(),
            [&](const auto& unit) {
                const auto* existing =
                    registry.find_controlled_unit(unit.type_id);
                return existing &&
                    existing->java_authority.class_name ==
                        descriptor.java_authority.class_name &&
                    ascii_fold(unit.name) == folded;
            });
    };

    if (requested) {
        auto name = trim_java_string(*requested);
        if (!is_valid_java_item_name(name)) {
            reject(
                "invalid_name",
                "Controlled-unit names must contain 1 to 50 "
                "Java UTF-16 code units");
        }
        if (conflicts(name)) {
            reject(
                "duplicate_name",
                "A controlled unit of this Java class already uses "
                "that name");
        }
        return name;
    }

    const auto base =
        trim_java_string(
            descriptor.java_authority.registered_name);
    if (!conflicts(base) &&
        is_valid_java_item_name(base)) {
        return base;
    }
    for (std::size_t suffix = 2; suffix < 100000; ++suffix) {
        const auto suffix_text =
            " " + std::to_string(suffix);
        auto candidate = base;
        while (!candidate.empty() &&
               java_utf16_code_unit_length(candidate) +
                       suffix_text.size() >
                   kMaximumJavaItemNameUtf16Units) {
            auto code_point = candidate.size() - 1;
            while (code_point > 0 &&
                   (static_cast<unsigned char>(
                        candidate[code_point]) &
                    0xc0U) == 0x80U) {
                --code_point;
            }
            candidate.erase(code_point);
        }
        candidate += suffix_text;
        if (is_valid_java_item_name(candidate) &&
            !conflicts(candidate)) {
            return candidate;
        }
    }
    reject(
        "name_generation_failed",
        "Could not generate a unique PAMGuard-style unit name");
}

std::size_t find_unit_index(
    const ProjectDocument& project,
    const std::string& id) {
    const auto found = std::find_if(
        project.controlled_units.begin(),
        project.controlled_units.end(),
        [&](const auto& unit) { return unit.id == id; });
    if (found == project.controlled_units.end()) {
        reject(
            "controlled_unit_not_found",
            "Controlled unit '" + id + "' does not exist");
    }
    return static_cast<std::size_t>(
        std::distance(
            project.controlled_units.begin(),
            found));
}

std::string resolve_reference(
    const ProjectEntityReference& reference,
    const std::unordered_map<std::string, std::string>& client_ids) {
    if (reference.id.has_value() ==
        reference.client_ref.has_value()) {
        reject(
            "invalid_entity_reference",
            "An entity reference requires exactly one id or clientRef");
    }
    if (reference.id) {
        return *reference.id;
    }
    const auto found =
        client_ids.find(*reference.client_ref);
    if (found == client_ids.end()) {
        reject(
            "unknown_client_reference",
            "Unknown mutation clientRef '" +
                *reference.client_ref + "'");
    }
    return found->second;
}

std::vector<SourceReference> matching_outputs(
    const ProjectDocument& project,
    const ControlledUnitRegistry& registry,
    const PublicDataRoleDescriptor& input) {
    std::vector<SourceReference> result;
    for (const auto& unit : project.controlled_units) {
        const auto* descriptor =
            registry.find_controlled_unit(unit.type_id);
        if (!descriptor) {
            continue;
        }
        for (const auto& output : descriptor->public_roles) {
            if (output.direction ==
                    DataRoleDirection::Output &&
                output.data_type == input.data_type &&
                contains_all_capabilities(
                    output.capabilities,
                    input.capabilities)) {
                result.push_back({unit.id, output.id});
            }
        }
    }
    return result;
}

bool role_requires_source(RoleCardinality cardinality) {
    return cardinality == RoleCardinality::ExactlyOne ||
        cardinality == RoleCardinality::OneOrMany;
}

bool type_is_available(
    const ControlledUnitDescriptor& descriptor) {
    return descriptor.availability ==
        AvailabilityStatus::Available;
}

void add_required_global_settings(
    ProjectDocument& project,
    const ControlledUnitRegistry& registry) {
    for (const auto& descriptor : registry.global_settings()) {
        if (!descriptor.required) {
            continue;
        }
        project.global_settings.components.push_back({
            descriptor.id,
            descriptor.settings.version,
            descriptor.settings.default_settings_json,
        });
    }
}

std::int64_t now_unix_ms() {
    return std::chrono::duration_cast<
               std::chrono::milliseconds>(
               std::chrono::system_clock::now()
                   .time_since_epoch())
        .count();
}

std::uint64_t next_revision(
    std::uint64_t current,
    std::string_view counter_name) {
    if (current == std::numeric_limits<std::uint64_t>::max()) {
        reject(
            "revision_exhausted",
            std::string(counter_name) +
                " revision counter is exhausted");
    }
    return current + 1;
}

std::string first_projection_error(
    const ProjectProjectionResult& projection) {
    const auto found = std::find_if(
        projection.issues.begin(),
        projection.issues.end(),
        [](const auto& issue) {
            return issue.issue_class ==
                ProjectionIssueClass::EditorInvalid;
        });
    return found == projection.issues.end()
        ? "Project projection is editor-invalid"
        : found->message;
}

} // namespace

ProjectAuthorityError::ProjectAuthorityError(
    std::string code,
    std::string message,
    std::string current_etag)
    : std::runtime_error(std::move(message)),
      code_(std::move(code)),
      current_etag_(std::move(current_etag)) {}

const std::string& ProjectAuthorityError::code() const noexcept {
    return code_;
}

const std::string&
ProjectAuthorityError::current_etag() const noexcept {
    return current_etag_;
}

const ProjectMutationResult&
PreparedProjectMutation::preview() const noexcept {
    return preview_;
}

const ActiveProjectSnapshot&
PreparedProjectSwitch::preview() const noexcept {
    return preview_;
}

const ActiveProjectSnapshot&
PreparedProjectSaveAs::preview() const noexcept {
    return preview_;
}

ProjectAuthority::ProjectAuthority(
    const ControlledUnitRegistry& controlled_unit_registry,
    const core::ModuleRegistry& runtime_registry,
    ProjectStore& store)
    : controlled_unit_registry_(controlled_unit_registry),
      runtime_registry_(runtime_registry),
      store_(store) {
    state_.project.project_id = generate_uuid_v4();
    state_.project.metadata = {
        "Untitled Project",
        {},
    };
    state_.project.descriptor_set = {
        std::string(kControlledUnitDescriptorSetId),
        kControlledUnitDescriptorSetVersion,
    };
    add_required_global_settings(
        state_.project,
        controlled_unit_registry_);
    state_.project =
        project_document_from_json(
            project_document_to_canonical_json(
                state_.project));
    state_.projection =
        project_document_to_runtime_graph(
            state_.project,
            controlled_unit_registry_,
            runtime_registry_);
    if (!state_.projection.editor_valid()) {
        throw ProjectAuthorityError(
            "invalid_blank_project",
            first_projection_error(state_.projection));
    }
    state_.working_content_hash =
        project_content_hash(state_.project);
}

ActiveProjectSnapshot
ProjectAuthority::snapshot_unlocked() const {
    auto projection = state_.projection;
    projection.graph.revision =
        state_.working_revision;
    return {
        state_.project,
        std::move(projection),
        state_.working_revision,
        state_.saved_revision,
        state_.authority_revision,
        state_.working_content_hash,
        state_.saved_content_hash,
        !state_.saved_content_hash ||
            state_.working_content_hash !=
                *state_.saved_content_hash,
        project_authority_etag(
            state_.project.project_id,
            state_.authority_revision,
            state_.working_content_hash,
            state_.saved_content_hash),
    };
}

ActiveProjectSnapshot ProjectAuthority::snapshot() const {
    std::lock_guard lock(mutex_);
    return snapshot_unlocked();
}

void ProjectAuthority::require_etag_unlocked(
    const std::string& expected_etag) const {
    const auto current = snapshot_unlocked().etag;
    if (expected_etag.empty()) {
        throw ProjectAuthorityError(
            "precondition_required",
            "A strong If-Match project ETag is required",
            current);
    }
    if (expected_etag != current) {
        throw ProjectAuthorityError(
            "precondition_failed",
            "The active project changed since it was read",
            current);
    }
}

PreparedProjectMutation ProjectAuthority::prepare_mutation(
    const std::string& expected_etag,
    const ProjectMutationBatch& batch) const {
    std::lock_guard lock(mutex_);
    require_etag_unlocked(expected_etag);
    const auto base_etag = snapshot_unlocked().etag;
    if (batch.schema_version != kMutationSchemaVersion) {
        reject(
            "unsupported_mutation_schema",
            "Only project mutation schemaVersion 1 is supported");
    }
    if (batch.operations.empty()) {
        PreparedProjectMutation prepared;
        prepared.base_etag_ = base_etag;
        prepared.committed_working_revision_ =
            state_.working_revision;
        prepared.committed_authority_revision_ =
            state_.authority_revision;
        prepared.preview_ = {
            false,
            batch.validate_only,
            {},
            snapshot_unlocked(),
        };
        return prepared;
    }

    auto candidate = state_.project;
    std::unordered_map<std::string, std::string> client_ids;
    std::unordered_set<std::string> reserved_client_refs;
    std::vector<CreatedProjectEntity> created;
    std::unordered_set<std::string> dependency_stack;

    using ForcedInputBindings = std::unordered_map<
        std::string,
        std::vector<SourceReference>>;
    std::function<std::string(
        const AddControlledUnitOperation&,
        bool,
        const ForcedInputBindings&)> add_unit;
    add_unit =
        [&](const AddControlledUnitOperation& operation,
            bool automatic,
            const ForcedInputBindings&
                forced_input_bindings) -> std::string {
        const auto* descriptor =
            controlled_unit_registry_.find_controlled_unit(
                operation.type_id);
        if (!descriptor || !type_is_available(*descriptor)) {
            reject(
                "controlled_unit_type_unavailable",
                "Controlled-unit type '" +
                    operation.type_id +
                    "' is not available");
        }
        if (!contains_mode(
                descriptor->instance_rules,
                candidate.mode)) {
            reject(
                "controlled_unit_mode_unsupported",
                "Controlled-unit type '" +
                    operation.type_id +
                    "' is unavailable in this project mode");
        }
        const auto existing_count =
            count_type(candidate, descriptor->id);
        const auto instance_limits =
            effective_instance_limits(
                descriptor->instance_rules,
                registry_mode(candidate.mode));
        if (instance_limits.maximum_instances &&
            existing_count >=
                *instance_limits.maximum_instances) {
            reject(
                "maximum_instances",
                "Controlled-unit type '" +
                    operation.type_id +
                    "' is already at its instance limit");
        }
        if (!automatic &&
            operation.client_ref.empty()) {
            reject(
                "invalid_client_reference",
                "addControlledUnit requires a non-empty clientRef");
        }
        if (!operation.client_ref.empty() &&
            (client_ids.contains(operation.client_ref) ||
             reserved_client_refs.contains(
                 operation.client_ref))) {
            reject(
                "duplicate_client_reference",
                "Mutation clientRef '" +
                    operation.client_ref +
                    "' is duplicated");
        }

        ControlledUnitInstance unit;
        unit.id = generate_uuid_v4();
        unit.type_id = descriptor->id;
        unit.descriptor_version =
            descriptor->descriptor_version;
        unit.recipe = {
            recipe_id_for(*descriptor),
            descriptor->runtime_recipe.version,
        };
        unit.name = unique_unit_name(
            candidate,
            controlled_unit_registry_,
            *descriptor,
            operation.name);
        unit.settings_version =
            descriptor->settings.version;
        unit.settings_json =
            descriptor->settings.default_settings_json;
        candidate.controlled_units.push_back(unit);
        candidate.data_model_layout.nodes.push_back({
            unit.id,
            80.0 +
                static_cast<double>(
                    (candidate.controlled_units.size() - 1) % 4) *
                    340.0,
            80.0 +
                static_cast<double>(
                    (candidate.controlled_units.size() - 1) / 4) *
                    250.0,
        });

        if (descriptor->id == "pamguard.user-display") {
            DisplayTab tab;
            tab.id = "tab:" + unit.id + ":main";
            tab.name = unit.name;
            tab.owner = {unit.id, "main"};
            candidate.display_tabs.push_back(std::move(tab));
        }
        else if (descriptor->id == "pamguard.click-detector") {
            const auto* provider =
                controlled_unit_registry_.find_display_provider(
                    "pamguard.click-display");
            if (!provider) {
                reject(
                    "missing_static_display_provider",
                    "Click Detector's static Click display provider is "
                    "not registered");
            }
            DisplayInstance display;
            display.id = "display:" + unit.id + ":click";
            display.provider_type_id = provider->id;
            display.provider_version = provider->descriptor_version;
            display.owner = {unit.id, "clickDisplay"};
            display.source = SourceReference{unit.id, "clicks"};
            display.settings_version = provider->settings.version;
            display.settings_json =
                provider->settings.default_settings_json;

            DisplayTab tab;
            tab.id = "tab:" + unit.id + ":click";
            tab.name = unit.name;
            tab.owner = {unit.id, "clickDisplay"};
            tab.displays.push_back(display);
            tab.layout.mode = DisplayLayoutMode::Grid;
            tab.layout.columns = 12;
            tab.layout.selected_display_id = display.id;
            tab.layout.items.push_back({
                display.id,
                0,
                0,
                12,
                8,
            });
            candidate.display_tabs.push_back(std::move(tab));
        }
        else if (descriptor->id == "pamguard.level-meter") {
            const auto* provider =
                controlled_unit_registry_.find_display_provider(
                    "pamguard.level-meter-display");
            if (!provider) {
                reject(
                    "missing_static_display_provider",
                    "Level Meter's static display provider is not "
                    "registered");
            }
            DisplayInstance display;
            display.id = "display:" + unit.id + ":level";
            display.provider_type_id = provider->id;
            display.provider_version = provider->descriptor_version;
            display.owner = {unit.id, "levelMeter"};
            display.source = SourceReference{unit.id, "levels"};
            display.settings_version = provider->settings.version;
            display.settings_json =
                provider->settings.default_settings_json;

            DisplayTab tab;
            tab.id = "tab:" + unit.id + ":level";
            tab.name = unit.name;
            tab.owner = {unit.id, "levelMeter"};
            tab.displays.push_back(display);
            tab.layout.mode = DisplayLayoutMode::Grid;
            tab.layout.columns = 12;
            tab.layout.selected_display_id = display.id;
            tab.layout.items.push_back({
                display.id,
                0,
                0,
                12,
                4,
            });
            candidate.display_tabs.push_back(std::move(tab));
        }

        if (!operation.client_ref.empty()) {
            client_ids.emplace(operation.client_ref, unit.id);
            created.push_back({
                operation.client_ref,
                unit.id,
            });
        }

        if (!dependency_stack.emplace(descriptor->id).second) {
            reject(
                "dependency_cycle",
                "Default-provider dependencies contain a cycle");
        }
        std::unordered_set<std::string> applied_forced_bindings;
        for (const auto& input : descriptor->public_roles) {
            if (input.direction !=
                    DataRoleDirection::Input ||
                !role_requires_source(input.cardinality)) {
                continue;
            }
            auto sources = matching_outputs(
                candidate,
                controlled_unit_registry_,
                input);
            sources.erase(
                std::remove_if(
                    sources.begin(),
                    sources.end(),
                    [&](const auto& source) {
                        return source.unit_id == unit.id;
                    }),
                sources.end());
            const auto forced =
                forced_input_bindings.find(input.id);
            if (forced != forced_input_bindings.end()) {
                for (const auto& requested : forced->second) {
                    if (std::find(
                            sources.begin(),
                            sources.end(),
                            requested) == sources.end()) {
                        reject(
                            "configuration_template_incompatible",
                            "Configuration template source '" +
                                requested.unit_id + ":" +
                                requested.output_role +
                                "' cannot satisfy '" + input.id +
                                "' for '" + unit.name + "'");
                    }
                }
                sources = forced->second;
                applied_forced_bindings.emplace(input.id);
            }
            else {
                if (sources.size() > 1) {
                    // PAMGuard adds the controlled unit first and lets the
                    // operator choose its source in the unit dialog. An
                    // arbitrary first-match choice would make module order
                    // scientific configuration, while rejecting the add
                    // prevents precisely that PAMGuard workflow. Preserve an
                    // empty required binding so projection reports
                    // NeedsConfiguration until the source is selected.
                    sources.clear();
                }
                else if (sources.empty()) {
                    if (operation.dependency_policy ==
                            DependencyPolicy::Reject ||
                        !input
                             .default_provider_controlled_unit_type_id) {
                        reject(
                            "missing_dependencies",
                            "Adding '" + unit.name +
                                "' requires a provider for '" +
                                input.id + "'");
                    }
                    AddControlledUnitOperation provider;
                    provider.type_id =
                        *input
                             .default_provider_controlled_unit_type_id;
                    provider.dependency_policy =
                        DependencyPolicy::AddDefaults;
                    const auto provider_id =
                        add_unit(provider, true, {});
                    const auto refreshed = matching_outputs(
                        candidate,
                        controlled_unit_registry_,
                        input);
                    const auto selected = std::find_if(
                        refreshed.begin(),
                        refreshed.end(),
                        [&](const auto& source) {
                            return source.unit_id == provider_id;
                        });
                    if (selected == refreshed.end()) {
                        reject(
                            "default_provider_incompatible",
                            "The authoritative default provider did not "
                            "publish a compatible output");
                    }
                    sources = {*selected};
                }
            }
            candidate.controlled_units[
                find_unit_index(candidate, unit.id)]
                .bindings.push_back({
                    input.id,
                    std::move(sources),
                });
        }
        if (applied_forced_bindings.size() !=
            forced_input_bindings.size()) {
            reject(
                "configuration_template_incompatible",
                "Configuration template forced an unknown or optional "
                "controlled-unit input role");
        }
        dependency_stack.erase(descriptor->id);
        return unit.id;
    };

    const auto add_configuration_template =
        [&](const AddConfigurationTemplateOperation& operation) {
        if (!is_entity_id(operation.client_ref)) {
            reject(
                "invalid_client_reference",
                "addConfigurationTemplate requires a stable clientRef");
        }
        if (operation.template_id !=
            kClickMonitoringConfigurationTemplateId) {
            reject(
                "configuration_template_unavailable",
                "Configuration template '" +
                    operation.template_id + "' is not available");
        }
        if (client_ids.contains(operation.client_ref) ||
            !reserved_client_refs.emplace(
                 operation.client_ref)
                 .second) {
            reject(
                "duplicate_client_reference",
                "Mutation clientRef '" +
                    operation.client_ref + "' is duplicated");
        }

        const auto child_ref =
            [&](std::string_view role) {
            auto result =
                operation.client_ref + ":" +
                std::string(role);
            if (!is_entity_id(result)) {
                reject(
                    "invalid_client_reference",
                    "addConfigurationTemplate clientRef is too long "
                    "to create stable child references");
            }
            return result;
        };
        const auto acquisition_ref =
            child_ref("acquisition");
        const auto fft_ref = child_ref("fft");
        const auto user_display_ref =
            child_ref("userDisplay");
        const auto click_detector_ref =
            child_ref("clickDetector");
        const auto sound_output_ref =
            child_ref("soundOutput");

        const auto* spectrogram_provider =
            controlled_unit_registry_.find_display_provider(
                "pamguard.spectrogram-display");
        if (!spectrogram_provider ||
            spectrogram_provider->availability !=
                AvailabilityStatus::Available ||
            spectrogram_provider
                    ->owner_controlled_unit_type_id !=
                "pamguard.user-display") {
            reject(
                "configuration_template_unavailable",
                "Click monitoring requires the available User Display "
                "Spectrogram provider");
        }

        std::vector<std::string> acquisition_ids;
        for (const auto& unit :
             candidate.controlled_units) {
            if (unit.type_id == "pamguard.acquisition") {
                acquisition_ids.push_back(unit.id);
            }
        }
        if (acquisition_ids.size() > 1) {
            reject(
                "ambiguous_template_acquisition",
                "Click monitoring cannot choose between multiple "
                "Acquisition controlled units");
        }

        std::string acquisition_id;
        if (acquisition_ids.empty()) {
            acquisition_id = add_unit(
                {
                    acquisition_ref,
                    "pamguard.acquisition",
                    std::nullopt,
                    DependencyPolicy::Reject,
                },
                false,
                {});
        }
        else {
            if (client_ids.contains(acquisition_ref) ||
                reserved_client_refs.contains(
                    acquisition_ref)) {
                reject(
                    "duplicate_client_reference",
                    "Mutation clientRef '" +
                        acquisition_ref +
                        "' is duplicated");
            }
            acquisition_id = acquisition_ids.front();
            client_ids.emplace(
                acquisition_ref,
                acquisition_id);
        }

        const ForcedInputBindings raw_audio_binding{
            {
                "rawAudio",
                {{acquisition_id, "rawAudio"}},
            },
        };
        const auto fft_id = add_unit(
            {
                fft_ref,
                "pamguard.fft",
                std::nullopt,
                DependencyPolicy::Reject,
            },
            false,
            raw_audio_binding);
        const auto user_display_id = add_unit(
            {
                user_display_ref,
                "pamguard.user-display",
                std::nullopt,
                DependencyPolicy::Reject,
            },
            false,
            {});

        const auto tab = std::find_if(
            candidate.display_tabs.begin(),
            candidate.display_tabs.end(),
            [&](const auto& value) {
                return value.owner.unit_id ==
                        user_display_id &&
                    value.owner.role == "main";
            });
        if (tab == candidate.display_tabs.end()) {
            reject(
                "configuration_template_incompatible",
                "User Display did not create its owned display tab");
        }
        DisplayInstance spectrogram;
        spectrogram.id =
            "display:" + user_display_id +
            ":spectrogram";
        spectrogram.provider_type_id =
            spectrogram_provider->id;
        spectrogram.provider_version =
            spectrogram_provider->descriptor_version;
        spectrogram.owner = {
            user_display_id,
            "provider",
        };
        spectrogram.source =
            SourceReference{fft_id, "fft"};
        spectrogram.settings_version =
            spectrogram_provider->settings.version;
        spectrogram.settings_json =
            spectrogram_provider
                ->settings.default_settings_json;
        tab->displays.push_back(spectrogram);
        tab->layout.mode = DisplayLayoutMode::Grid;
        tab->layout.columns = 12;
        tab->layout.selected_display_id =
            spectrogram.id;
        tab->layout.items.push_back({
            spectrogram.id,
            0,
            0,
            12,
            8,
        });

        (void)add_unit(
            {
                click_detector_ref,
                "pamguard.click-detector",
                std::nullopt,
                DependencyPolicy::Reject,
            },
            false,
            raw_audio_binding);
        const ForcedInputBindings playback_binding{
            {
                "audio",
                {{acquisition_id, "rawAudio"}},
            },
        };
        (void)add_unit(
            {
                sound_output_ref,
                "pamguard.sound-output",
                std::nullopt,
                DependencyPolicy::Reject,
            },
            false,
            playback_binding);
    };

    for (const auto& operation : batch.operations) {
        std::visit(
            [&](const auto& value) {
                using Operation = std::decay_t<decltype(value)>;
                if constexpr (
                    std::is_same_v<
                        Operation,
                        AddControlledUnitOperation>) {
                    (void)add_unit(value, false, {});
                }
                else if constexpr (
                    std::is_same_v<
                        Operation,
                        AddConfigurationTemplateOperation>) {
                    add_configuration_template(value);
                }
                else if constexpr (
                    std::is_same_v<
                        Operation,
                        RenameControlledUnitOperation>) {
                    const auto id =
                        resolve_reference(
                            value.unit,
                            client_ids);
                    const auto index =
                        find_unit_index(candidate, id);
                    const auto* descriptor =
                        controlled_unit_registry_
                            .find_controlled_unit(
                                candidate
                                    .controlled_units[index]
                                    .type_id);
                    if (!descriptor) {
                        reject(
                            "controlled_unit_type_unavailable",
                            "Cannot rename a unit with an unknown type");
                    }
                    const auto old_name =
                        candidate.controlled_units[index].name;
                    auto without_unit = candidate;
                    without_unit.controlled_units.erase(
                        without_unit.controlled_units.begin() +
                        static_cast<std::ptrdiff_t>(index));
                    candidate.controlled_units[index].name =
                        unique_unit_name(
                            without_unit,
                            controlled_unit_registry_,
                            *descriptor,
                            value.name);
                    for (auto& tab : candidate.display_tabs) {
                        if (tab.owner.unit_id == id &&
                            tab.name == old_name) {
                            tab.name =
                                candidate
                                    .controlled_units[index]
                                    .name;
                        }
                    }
                }
                else if constexpr (
                    std::is_same_v<
                        Operation,
                        RemoveControlledUnitOperation>) {
                    const auto id =
                        resolve_reference(
                            value.unit,
                            client_ids);
                    const auto index =
                        find_unit_index(candidate, id);
                    const auto removed_type =
                        candidate.controlled_units[index].type_id;
                    const auto* descriptor =
                        controlled_unit_registry_
                            .find_controlled_unit(removed_type);
                    const auto instance_limits =
                        descriptor
                        ? effective_instance_limits(
                              descriptor->instance_rules,
                              registry_mode(candidate.mode))
                        : InstanceLimitDescriptor{};
                    if (descriptor &&
                        count_type(candidate, removed_type) <=
                            instance_limits.minimum_instances) {
                        reject(
                            "minimum_instances",
                            "Removing this unit would violate the "
                            "controlled-unit minimum");
                    }
                    bool has_dependants = false;
                    for (const auto& unit :
                         candidate.controlled_units) {
                        if (unit.id == id) {
                            continue;
                        }
                        for (const auto& binding : unit.bindings) {
                            has_dependants =
                                has_dependants ||
                                std::any_of(
                                    binding.sources.begin(),
                                    binding.sources.end(),
                                    [&](const auto& source) {
                                        return source.unit_id == id;
                                    });
                        }
                    }
                    for (const auto& tab :
                         candidate.display_tabs) {
                        if (tab.owner.unit_id == id) {
                            continue;
                        }
                        for (const auto& display : tab.displays) {
                            has_dependants =
                                has_dependants ||
                                (display.source &&
                                 display.source->unit_id == id);
                        }
                    }
                    if (has_dependants &&
                        value.dependant_policy ==
                            DependantRemovalPolicy::Reject) {
                        reject(
                            "controlled_unit_has_dependants",
                            "Disconnect dependent inputs/displays or "
                            "request leave-unbound removal");
                    }
                    for (auto& unit :
                         candidate.controlled_units) {
                        for (auto& binding : unit.bindings) {
                            std::erase_if(
                                binding.sources,
                                [&](const auto& source) {
                                    return source.unit_id == id;
                                });
                        }
                    }
                    for (auto& tab : candidate.display_tabs) {
                        for (auto& display : tab.displays) {
                            if (display.source &&
                                display.source->unit_id == id) {
                                display.source.reset();
                            }
                        }
                    }
                    std::erase_if(
                        candidate.display_tabs,
                        [&](const auto& tab) {
                            return tab.owner.unit_id == id;
                        });
                    std::erase_if(
                        candidate.data_model_layout.nodes,
                        [&](const auto& node) {
                            return node.unit_id == id;
                        });
                    candidate.controlled_units.erase(
                        candidate.controlled_units.begin() +
                        static_cast<std::ptrdiff_t>(index));
                }
                else if constexpr (
                    std::is_same_v<
                        Operation,
                        ReorderControlledUnitsOperation>) {
                    if (value.units.size() !=
                        candidate.controlled_units.size()) {
                        reject(
                            "invalid_controlled_unit_order",
                            "Reorder must name every controlled unit "
                            "exactly once");
                    }
                    std::vector<ControlledUnitInstance> reordered;
                    reordered.reserve(value.units.size());
                    std::unordered_set<std::string> seen;
                    for (const auto& reference : value.units) {
                        const auto id =
                            resolve_reference(
                                reference,
                                client_ids);
                        if (!seen.emplace(id).second) {
                            reject(
                                "invalid_controlled_unit_order",
                                "Reorder contains a duplicate unit");
                        }
                        reordered.push_back(
                            candidate.controlled_units[
                                find_unit_index(candidate, id)]);
                    }
                    candidate.controlled_units =
                        std::move(reordered);
                }
                else if constexpr (
                    std::is_same_v<
                        Operation,
                        ReplaceControlledUnitSettingsOperation>) {
                    const auto id =
                        resolve_reference(
                            value.unit,
                            client_ids);
                    auto& unit =
                        candidate.controlled_units[
                            find_unit_index(candidate, id)];
                    unit.settings_version =
                        value.settings_version;
                    unit.settings_json =
                        value.settings_json;
                }
                else if constexpr (
                    std::is_same_v<
                        Operation,
                        ReplaceGlobalSettingsOperation>) {
                    const auto* descriptor =
                        controlled_unit_registry_
                            .find_global_settings(value.type_id);
                    if (!descriptor ||
                        descriptor->availability !=
                            AvailabilityStatus::Available) {
                        reject(
                            "global_settings_type_unavailable",
                            "Global settings type '" +
                                value.type_id +
                                "' is unavailable");
                    }
                    const auto component = std::find_if(
                        candidate.global_settings.components.begin(),
                        candidate.global_settings.components.end(),
                        [&](const auto& existing) {
                            return existing.type_id ==
                                value.type_id;
                        });
                    if (component ==
                        candidate.global_settings.components.end()) {
                        candidate.global_settings.components.push_back({
                            value.type_id,
                            value.settings_version,
                            value.settings_json,
                        });
                    }
                    else {
                        component->settings_version =
                            value.settings_version;
                        component->settings_json =
                            value.settings_json;
                    }
                }
                else if constexpr (
                    std::is_same_v<
                        Operation,
                        SetControlledUnitBindingOperation>) {
                    const auto id =
                        resolve_reference(
                            value.unit,
                            client_ids);
                    auto& unit =
                        candidate.controlled_units[
                            find_unit_index(candidate, id)];
                    const auto* descriptor =
                        controlled_unit_registry_
                            .find_controlled_unit(unit.type_id);
                    if (!descriptor ||
                        !find_role(
                            *descriptor,
                            value.input_role,
                            DataRoleDirection::Input)) {
                        reject(
                            "unknown_input_role",
                            "Controlled unit has no input role '" +
                                value.input_role + "'");
                    }
                    std::vector<SourceReference> sources;
                    sources.reserve(value.sources.size());
                    for (const auto& source : value.sources) {
                        sources.push_back({
                            resolve_reference(
                                source.unit,
                                client_ids),
                            source.output_role,
                        });
                    }
                    const auto binding = std::find_if(
                        unit.bindings.begin(),
                        unit.bindings.end(),
                        [&](const auto& existing) {
                            return existing.input_role ==
                                value.input_role;
                        });
                    if (binding == unit.bindings.end()) {
                        unit.bindings.push_back({
                            value.input_role,
                            std::move(sources),
                        });
                    }
                    else {
                        binding->sources = std::move(sources);
                    }
                }
                else if constexpr (
                    std::is_same_v<
                        Operation,
                        ReplaceDataModelLayoutOperation>) {
                    candidate.data_model_layout =
                        value.layout;
                }
                else if constexpr (
                    std::is_same_v<
                        Operation,
                        ReplaceDisplayHierarchyOperation>) {
                    candidate.display_tabs =
                        value.display_tabs;
                }
            },
            operation);
    }

    try {
        candidate =
            project_document_from_json(
                project_document_to_canonical_json(
                    candidate));
    }
    catch (const ProjectJsonError& error) {
        reject("invalid_project", error.what());
    }
    auto projection =
        project_document_to_runtime_graph(
            candidate,
            controlled_unit_registry_,
            runtime_registry_);
    if (!projection.editor_valid()) {
        reject(
            "invalid_project_projection",
            first_projection_error(projection));
    }
    const auto content_hash =
        project_content_hash(candidate);
    const bool changed =
        content_hash != state_.working_content_hash;

    std::erase_if(
        created,
        [&](const auto& entity) {
            return std::none_of(
                candidate.controlled_units.begin(),
                candidate.controlled_units.end(),
                [&](const auto& unit) {
                    return unit.id == entity.id;
                });
        });

    PreparedProjectMutation prepared;
    prepared.base_etag_ = base_etag;
    prepared.committed_working_revision_ =
        changed
        ? next_revision(state_.working_revision, "Working")
        : state_.working_revision;
    prepared.committed_authority_revision_ =
        changed
        ? next_revision(state_.authority_revision, "Authority")
        : state_.authority_revision;

    auto preview = snapshot_unlocked();
    preview.project = std::move(candidate);
    preview.projection = std::move(projection);
    // Runtime preflight must see the same graph revision that commit will
    // publish, while the surrounding preview retains the currently accepted
    // authority/working revisions and ETag until commit succeeds.
    preview.projection.graph.revision =
        prepared.committed_working_revision_;
    preview.working_content_hash = content_hash;
    preview.dirty =
        !state_.saved_content_hash ||
        content_hash != *state_.saved_content_hash;
    prepared.preview_ = {
        changed,
        batch.validate_only,
        std::move(created),
        std::move(preview),
    };
    return prepared;
}

ProjectMutationResult ProjectAuthority::commit_mutation(
    PreparedProjectMutation prepared) {
    std::lock_guard lock(mutex_);
    require_etag_unlocked(prepared.base_etag_);
    if (prepared.preview_.validated_only) {
        throw ProjectAuthorityError(
            "validated_only_candidate",
            "A validateOnly project mutation cannot be committed",
            snapshot_unlocked().etag);
    }
    if (prepared.preview_.changed) {
        state_.project =
            std::move(prepared.preview_.active.project);
        state_.projection =
            std::move(prepared.preview_.active.projection);
        state_.working_revision =
            prepared.committed_working_revision_;
        state_.authority_revision =
            prepared.committed_authority_revision_;
        state_.working_content_hash =
            std::move(
                prepared.preview_.active
                    .working_content_hash);
        state_.projection.graph.revision =
            state_.working_revision;
    }
    return {
        prepared.preview_.changed,
        false,
        std::move(prepared.preview_.created_entities),
        snapshot_unlocked(),
    };
}

ProjectMutationResult ProjectAuthority::mutate(
    const std::string& expected_etag,
    const ProjectMutationBatch& batch) {
    auto prepared =
        prepare_mutation(expected_etag, batch);
    if (batch.validate_only) {
        return prepared.preview();
    }
    return commit_mutation(std::move(prepared));
}

PreparedProjectSwitch ProjectAuthority::prepare_new_project(
    const std::string& expected_etag,
    std::string name,
    std::string description,
    bool discard_dirty) const {
    std::lock_guard lock(mutex_);
    require_etag_unlocked(expected_etag);
    const auto current = snapshot_unlocked();
    if (current.dirty && !discard_dirty) {
        throw ProjectAuthorityError(
            "dirty_project",
            "The active project has unsaved changes",
            current.etag);
    }

    State candidate;
    candidate.project.project_id = generate_uuid_v4();
    candidate.project.metadata = {
        std::move(name),
        std::move(description),
    };
    candidate.project.descriptor_set = {
        std::string(kControlledUnitDescriptorSetId),
        kControlledUnitDescriptorSetVersion,
    };
    add_required_global_settings(
        candidate.project,
        controlled_unit_registry_);
    try {
        candidate.project =
            project_document_from_json(
                project_document_to_canonical_json(
                    candidate.project));
    }
    catch (const ProjectJsonError& error) {
        reject("invalid_project", error.what());
    }
    candidate.projection =
        project_document_to_runtime_graph(
            candidate.project,
            controlled_unit_registry_,
            runtime_registry_);
    if (!candidate.projection.editor_valid()) {
        reject(
            "invalid_project_projection",
            first_projection_error(candidate.projection));
    }
    candidate.working_content_hash =
        project_content_hash(candidate.project);
    candidate.projection.graph.revision =
        candidate.working_revision;

    PreparedProjectSwitch prepared;
    prepared.base_etag_ = current.etag;
    prepared.preview_ = {
        std::move(candidate.project),
        std::move(candidate.projection),
        candidate.working_revision,
        candidate.saved_revision,
        candidate.authority_revision,
        std::move(candidate.working_content_hash),
        std::move(candidate.saved_content_hash),
        true,
        {},
    };
    prepared.preview_.etag =
        project_authority_etag(
            prepared.preview_.project.project_id,
            prepared.preview_.authority_revision,
            prepared.preview_.working_content_hash,
            prepared.preview_.saved_content_hash);
    return prepared;
}

PreparedProjectSwitch ProjectAuthority::prepare_open(
    const std::string& expected_etag,
    const std::string& project_id,
    bool discard_dirty) const {
    std::lock_guard lock(mutex_);
    require_etag_unlocked(expected_etag);
    const auto current = snapshot_unlocked();
    if (current.dirty && !discard_dirty) {
        throw ProjectAuthorityError(
            "dirty_project",
            "The active project has unsaved changes",
            current.etag);
    }

    const auto loaded = store_.load(project_id);
    auto projection =
        project_document_to_runtime_graph(
            loaded.envelope.project,
            controlled_unit_registry_,
            runtime_registry_);
    if (!projection.editor_valid()) {
        reject(
            "invalid_project_projection",
            first_projection_error(projection));
    }

    State candidate;
    candidate.project = loaded.envelope.project;
    candidate.projection = std::move(projection);
    candidate.working_revision =
        loaded.envelope.saved_revision;
    candidate.saved_revision =
        loaded.envelope.saved_revision;
    candidate.authority_revision =
        loaded.envelope.authority_revision;
    candidate.working_content_hash =
        loaded.envelope.content_hash;
    candidate.saved_content_hash =
        loaded.envelope.content_hash;
    candidate.durable_fingerprint =
        loaded.fingerprint;
    candidate.projection.graph.revision =
        candidate.working_revision;

    PreparedProjectSwitch prepared;
    prepared.base_etag_ = current.etag;
    prepared.durable_fingerprint_ =
        candidate.durable_fingerprint;
    prepared.preview_ = {
        std::move(candidate.project),
        std::move(candidate.projection),
        candidate.working_revision,
        candidate.saved_revision,
        candidate.authority_revision,
        std::move(candidate.working_content_hash),
        std::move(candidate.saved_content_hash),
        false,
        {},
    };
    prepared.preview_.etag =
        project_authority_etag(
            prepared.preview_.project.project_id,
            prepared.preview_.authority_revision,
            prepared.preview_.working_content_hash,
            prepared.preview_.saved_content_hash);
    return prepared;
}

ActiveProjectSnapshot ProjectAuthority::commit_project_switch(
    PreparedProjectSwitch prepared) {
    std::lock_guard lock(mutex_);
    require_etag_unlocked(prepared.base_etag_);

    State candidate;
    candidate.project =
        std::move(prepared.preview_.project);
    candidate.projection =
        std::move(prepared.preview_.projection);
    candidate.working_revision =
        prepared.preview_.working_revision;
    candidate.saved_revision =
        prepared.preview_.saved_revision;
    candidate.authority_revision =
        prepared.preview_.authority_revision;
    candidate.working_content_hash =
        std::move(
            prepared.preview_.working_content_hash);
    candidate.saved_content_hash =
        std::move(
            prepared.preview_.saved_content_hash);
    candidate.durable_fingerprint =
        std::move(prepared.durable_fingerprint_);
    candidate.projection.graph.revision =
        candidate.working_revision;
    state_ = std::move(candidate);
    return snapshot_unlocked();
}

ActiveProjectSnapshot ProjectAuthority::new_project(
    const std::string& expected_etag,
    std::string name,
    std::string description,
    bool discard_dirty) {
    auto prepared =
        prepare_new_project(
            expected_etag,
            std::move(name),
            std::move(description),
            discard_dirty);
    return commit_project_switch(std::move(prepared));
}

ActiveProjectSnapshot ProjectAuthority::open(
    const std::string& expected_etag,
    const std::string& project_id,
    bool discard_dirty) {
    auto prepared =
        prepare_open(
            expected_etag,
            project_id,
            discard_dirty);
    return commit_project_switch(std::move(prepared));
}

ActiveProjectSnapshot ProjectAuthority::save(
    const std::string& expected_etag) {
    std::lock_guard lock(mutex_);
    require_etag_unlocked(expected_etag);
    const auto current_etag = snapshot_unlocked().etag;

    ProjectFileEnvelope envelope;
    envelope.authority_revision =
        next_revision(
            state_.authority_revision,
            "Authority");
    envelope.saved_revision =
        state_.working_revision;
    envelope.content_hash =
        state_.working_content_hash;
    envelope.saved_at_unix_ms = now_unix_ms();
    envelope.project = state_.project;

    ProjectFileFingerprint fingerprint;
    try {
        if (state_.durable_fingerprint) {
            fingerprint = store_.replace(
                envelope,
                *state_.durable_fingerprint);
        }
        else {
            fingerprint = store_.create(envelope);
        }
    }
    catch (const ProjectFileConflict& error) {
        throw ProjectAuthorityError(
            "durable_conflict",
            error.what(),
            current_etag);
    }
    catch (const ProjectDurabilityError& error) {
        throw ProjectAuthorityError(
            "project_save_uncertain",
            error.what(),
            current_etag);
    }
    catch (const ProjectStoreError& error) {
        throw ProjectAuthorityError(
            "project_save_failed",
            error.what(),
            current_etag);
    }
    state_.authority_revision =
        envelope.authority_revision;
    state_.saved_revision =
        state_.working_revision;
    state_.saved_content_hash =
        state_.working_content_hash;
    state_.durable_fingerprint =
        std::move(fingerprint);
    return snapshot_unlocked();
}

PreparedProjectSaveAs ProjectAuthority::prepare_save_as(
    const std::string& expected_etag,
    std::string name) const {
    std::lock_guard lock(mutex_);
    require_etag_unlocked(expected_etag);
    const auto current = snapshot_unlocked();

    auto project = state_.project;
    project.project_id = generate_uuid_v4();
    project.metadata.name = std::move(name);
    try {
        project =
            project_document_from_json(
                project_document_to_canonical_json(project));
    }
    catch (const ProjectJsonError& error) {
        reject("invalid_project", error.what());
    }
    auto projection =
        project_document_to_runtime_graph(
            project,
            controlled_unit_registry_,
            runtime_registry_);
    if (!projection.editor_valid()) {
        reject(
            "invalid_project_projection",
            first_projection_error(projection));
    }
    const auto hash = project_content_hash(project);
    const auto working_revision =
        next_revision(
            state_.working_revision,
            "Working");
    const auto authority_revision =
        next_revision(
            state_.authority_revision,
            "Authority");
    projection.graph.revision = working_revision;

    PreparedProjectSaveAs prepared;
    prepared.base_etag_ = current.etag;
    prepared.preview_ = {
        std::move(project),
        std::move(projection),
        working_revision,
        working_revision,
        authority_revision,
        hash,
        hash,
        false,
        {},
    };
    prepared.preview_.etag =
        project_authority_etag(
            prepared.preview_.project.project_id,
            prepared.preview_.authority_revision,
            prepared.preview_.working_content_hash,
            prepared.preview_.saved_content_hash);
    return prepared;
}

ActiveProjectSnapshot ProjectAuthority::commit_save_as(
    PreparedProjectSaveAs prepared) {
    std::lock_guard lock(mutex_);
    require_etag_unlocked(prepared.base_etag_);
    const auto current_etag = snapshot_unlocked().etag;

    ProjectFileEnvelope envelope;
    envelope.authority_revision =
        prepared.preview_.authority_revision;
    envelope.saved_revision =
        *prepared.preview_.saved_revision;
    envelope.content_hash =
        prepared.preview_.working_content_hash;
    envelope.saved_at_unix_ms = now_unix_ms();
    envelope.project = prepared.preview_.project;
    ProjectFileFingerprint fingerprint;
    try {
        fingerprint = store_.create(envelope);
    }
    catch (const ProjectFileConflict& error) {
        throw ProjectAuthorityError(
            "durable_conflict",
            error.what(),
            current_etag);
    }
    catch (const ProjectDurabilityError& error) {
        throw ProjectAuthorityError(
            "project_save_uncertain",
            error.what(),
            current_etag);
    }
    catch (const ProjectStoreError& error) {
        throw ProjectAuthorityError(
            "project_save_failed",
            error.what(),
            current_etag);
    }

    State candidate;
    candidate.project =
        std::move(prepared.preview_.project);
    candidate.projection =
        std::move(prepared.preview_.projection);
    candidate.working_revision =
        prepared.preview_.working_revision;
    candidate.saved_revision =
        prepared.preview_.saved_revision;
    candidate.authority_revision =
        prepared.preview_.authority_revision;
    candidate.working_content_hash =
        std::move(
            prepared.preview_.working_content_hash);
    candidate.saved_content_hash =
        std::move(
            prepared.preview_.saved_content_hash);
    candidate.durable_fingerprint =
        std::move(fingerprint);
    state_ = std::move(candidate);
    state_.projection.graph.revision =
        state_.working_revision;
    return snapshot_unlocked();
}

ActiveProjectSnapshot ProjectAuthority::save_as(
    const std::string& expected_etag,
    std::string name) {
    auto prepared =
        prepare_save_as(
            expected_etag,
            std::move(name));
    return commit_save_as(std::move(prepared));
}

std::vector<ProjectedPublicOutput>
ProjectAuthority::compatible_sources(
    const std::string& unit_id,
    const std::string& input_role) const {
    std::lock_guard lock(mutex_);
    const auto* input =
        state_.projection.index.find_public_input(
            unit_id,
            input_role);
    if (!input) {
        throw ProjectAuthorityError(
            "public_input_not_found",
            "The controlled-unit public input does not exist");
    }
    const auto unit = std::find_if(
        state_.project.controlled_units.begin(),
        state_.project.controlled_units.end(),
        [&](const auto& candidate) {
            return candidate.id == unit_id;
        });
    const auto* descriptor =
        unit == state_.project.controlled_units.end()
        ? nullptr
        : controlled_unit_registry_.find_controlled_unit(
              unit->type_id);
    const auto* input_descriptor = descriptor
        ? find_role(
              *descriptor,
              input_role,
              DataRoleDirection::Input)
        : nullptr;
    if (!input_descriptor) {
        throw ProjectAuthorityError(
            "public_input_not_found",
            "The controlled-unit input descriptor is unavailable");
    }
    std::vector<ProjectedPublicOutput> result;
    for (const auto& output :
         state_.projection.index.public_outputs) {
        if (output.unit_id != unit_id &&
            output.data_type == input->data_type &&
            contains_all_capabilities(
                output.capabilities,
                input_descriptor->capabilities)) {
            result.push_back(output);
        }
    }
    return result;
}

} // namespace pamguard::project
