#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pamguard/core/AnalysisConfig.h"
#include "pamguard/core/ModuleGraph.h"
#include "pamguard/project/ControlledUnitRegistry.h"
#include "pamguard/project/ProjectDocument.h"

namespace pamguard::project {

enum class ProjectionIssueClass {
    /** The document cannot be accepted by the editor/project model. */
    EditorInvalid,
    /** The document is saveable but cannot start until configured. */
    NeedsConfiguration,
};

enum class ProjectionStatus {
    Invalid,
    NeedsConfiguration,
    Runnable,
};

struct ProjectionIssue {
    ProjectionIssueClass issue_class =
        ProjectionIssueClass::EditorInvalid;
    std::string code;
    std::string message;
    std::string unit_id;
    std::string role_id;
    std::string display_id;

    bool operator==(const ProjectionIssue&) const = default;
};

struct ProjectedRuntimeNode {
    std::string owner_unit_id;
    RuntimeChildRoleId child_role_id;
    std::string runtime_node_id;
    std::string runtime_type_id;

    bool operator==(const ProjectedRuntimeNode&) const = default;
};

struct ProjectedDataBlock {
    std::string owner_unit_id;
    RuntimeChildRoleId child_role_id;
    std::string runtime_node_id;
    std::string port_id;
    std::string block_id;
    std::string data_type;
    std::vector<std::string> capabilities;

    bool operator==(const ProjectedDataBlock&) const = default;
};

struct ProjectedPublicOutput {
    std::string unit_id;
    PublicRoleId output_role;
    std::string runtime_node_id;
    std::string runtime_port_id;
    std::string block_id;
    std::string data_type;
    std::vector<std::string> capabilities;

    bool operator==(const ProjectedPublicOutput&) const = default;
};

struct ProjectedPublicInput {
    std::string unit_id;
    PublicRoleId input_role;
    std::string runtime_node_id;
    std::string runtime_port_id;
    std::string data_type;
    RoleCardinality cardinality = RoleCardinality::ZeroOrOne;
    std::vector<SourceReference> sources;
    std::vector<std::string> connection_ids;

    bool operator==(const ProjectedPublicInput&) const = default;
};

enum class ProjectedConnectionKind {
    Internal,
    External,
};

struct ProjectedConnectionOwnership {
    std::string connection_id;
    ProjectedConnectionKind kind =
        ProjectedConnectionKind::Internal;
    /** Internal-edge owner or external target controlled unit. */
    std::string owner_unit_id;
    std::string internal_edge_role;
    std::string target_input_role;
    std::optional<SourceReference> public_source;

    bool operator==(const ProjectedConnectionOwnership&) const = default;
};

struct ProjectedDisplayTabOwnership {
    std::string tab_id;
    std::string owner_unit_id;
    std::string owner_role;

    bool operator==(const ProjectedDisplayTabOwnership&) const = default;
};

struct ProjectedDisplayOwnership {
    std::string tab_id;
    std::string display_id;
    std::string owner_unit_id;
    std::string owner_role;
    DisplayProviderTypeId provider_type_id;
    std::optional<SourceReference> public_source;
    std::optional<std::string> source_block_id;

    bool operator==(const ProjectedDisplayOwnership&) const = default;
};

/**
 * Stable ownership/inspection view for the generated runtime graph.
 *
 * Public source references intentionally remain controlled-unit ID/output-role
 * pairs. Runtime child and block IDs are projections, never persistence truth.
 */
struct ProjectionIndex {
    std::vector<ProjectedRuntimeNode> runtime_nodes;
    std::vector<ProjectedDataBlock> data_blocks;
    std::vector<ProjectedPublicOutput> public_outputs;
    std::vector<ProjectedPublicInput> public_inputs;
    std::vector<ProjectedConnectionOwnership> connections;
    std::vector<ProjectedDisplayTabOwnership> display_tabs;
    std::vector<ProjectedDisplayOwnership> displays;

    [[nodiscard]] const ProjectedRuntimeNode* find_runtime_node(
        std::string_view unit_id,
        std::string_view child_role_id) const noexcept;
    [[nodiscard]] const ProjectedDataBlock* find_data_block(
        std::string_view block_id) const noexcept;
    [[nodiscard]] const ProjectedPublicOutput* find_public_output(
        std::string_view unit_id,
        std::string_view output_role) const noexcept;
    [[nodiscard]] const ProjectedPublicInput* find_public_input(
        std::string_view unit_id,
        std::string_view input_role) const noexcept;
    [[nodiscard]] const ProjectedDisplayOwnership* find_display(
        std::string_view display_id) const noexcept;

    bool operator==(const ProjectionIndex&) const = default;
};

struct ProjectProjectionResult {
    core::ModuleGraphDocument graph;
    ProjectionIndex index;
    /**
     * Validated global Array Manager geometry. Localisation children consume
     * this projection instead of persisting a private geometry copy.
     */
    std::optional<core::ArrayConfiguration> array_geometry;
    std::vector<ProjectionIssue> issues;

    [[nodiscard]] ProjectionStatus status() const noexcept;
    [[nodiscard]] bool editor_valid() const noexcept;
    [[nodiscard]] bool runnable() const noexcept;
    [[nodiscard]] bool needs_configuration() const noexcept;
};

[[nodiscard]] std::string projected_runtime_node_id(
    std::string_view unit_id,
    std::string_view child_role_id);
[[nodiscard]] std::string projected_internal_connection_id(
    std::string_view unit_id,
    std::string_view edge_role);
[[nodiscard]] std::string projected_external_connection_id(
    std::string_view target_unit_id,
    std::string_view input_role,
    std::string_view source_unit_id,
    std::string_view output_role);
[[nodiscard]] std::string projected_data_block_id(
    std::string_view runtime_node_id,
    std::string_view port_id);

/**
 * Purely project one normalized document into the low-level runtime graph.
 *
 * Instance-name uniqueness follows Java-class ownership and folds ASCII A-Z
 * only. This is an explicit Phase 1 deviation from full Unicode case folding;
 * non-ASCII UTF-8 bytes are compared unchanged.
 */
[[nodiscard]] ProjectProjectionResult project_document_to_runtime_graph(
    const ProjectDocument& document,
    const ControlledUnitRegistry& controlled_unit_registry,
    const core::ModuleRegistry& runtime_registry);

} // namespace pamguard::project
