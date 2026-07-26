#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pamguard::project {

inline constexpr std::uint32_t kProjectSchemaVersion = 1;
inline constexpr std::uint32_t kGlobalSettingsSchemaVersion = 1;
inline constexpr std::size_t kMaximumJavaItemNameUtf16Units = 50;

enum class ProjectMode {
    Normal,
    Mixed,
    Viewer,
};

struct ProjectMetadata {
    std::string name;
    std::string description;

    bool operator==(const ProjectMetadata&) const = default;
};

struct DescriptorSetReference {
    std::string id;
    std::uint32_t version = 1;

    bool operator==(const DescriptorSetReference&) const = default;
};

struct ExpansionRecipeReference {
    std::string id;
    std::uint32_t version = 1;

    bool operator==(const ExpansionRecipeReference&) const = default;
};

struct SourceReference {
    std::string unit_id;
    std::string output_role;

    bool operator==(const SourceReference&) const = default;
};

struct InputBinding {
    std::string input_role;
    std::vector<SourceReference> sources;

    bool operator==(const InputBinding&) const = default;
};

struct ControlledUnitInstance {
    std::string id;
    std::string type_id;
    std::uint32_t descriptor_version = 1;
    ExpansionRecipeReference recipe;
    std::string name;
    std::uint32_t settings_version = 1;
    /** Canonical JSON object owned and validated by the unit adapter. */
    std::string settings_json = "{}";
    std::vector<InputBinding> bindings;

    bool operator==(const ControlledUnitInstance&) const = default;
};

struct GlobalSettingsComponent {
    std::string type_id;
    std::uint32_t settings_version = 1;
    /** Canonical JSON object owned and validated by the global adapter. */
    std::string settings_json = "{}";

    bool operator==(const GlobalSettingsComponent&) const = default;
};

struct GlobalSettings {
    std::uint32_t schema_version = kGlobalSettingsSchemaVersion;
    std::vector<GlobalSettingsComponent> components;

    bool operator==(const GlobalSettings&) const = default;
};

struct DisplayOwner {
    std::string unit_id;
    std::string role;

    bool operator==(const DisplayOwner&) const = default;
};

struct DisplayInstance {
    std::string id;
    std::string provider_type_id;
    std::uint32_t provider_version = 1;
    DisplayOwner owner;
    std::optional<SourceReference> source;
    std::uint32_t settings_version = 1;
    /** Canonical JSON object owned and validated by the display adapter. */
    std::string settings_json = "{}";

    bool operator==(const DisplayInstance&) const = default;
};

enum class DisplayLayoutMode {
    Grid,
    Tabs,
};

struct DisplayGridItem {
    std::string display_id;
    std::uint32_t column = 0;
    std::uint32_t row = 0;
    std::uint32_t width = 1;
    std::uint32_t height = 1;

    bool operator==(const DisplayGridItem&) const = default;
};

struct DisplayTabLayout {
    DisplayLayoutMode mode = DisplayLayoutMode::Grid;
    std::uint32_t columns = 12;
    std::optional<std::string> selected_display_id;
    std::vector<DisplayGridItem> items;

    bool operator==(const DisplayTabLayout&) const = default;
};

struct DisplayTab {
    std::string id;
    std::string name;
    DisplayOwner owner;
    std::vector<DisplayInstance> displays;
    DisplayTabLayout layout;

    bool operator==(const DisplayTab&) const = default;
};

struct DataModelNodePosition {
    std::string unit_id;
    double x = 0.0;
    double y = 0.0;

    bool operator==(const DataModelNodePosition&) const = default;
};

struct DataModelViewport {
    double x = 0.0;
    double y = 0.0;
    double zoom = 1.0;

    bool operator==(const DataModelViewport&) const = default;
};

struct DataModelLayout {
    std::vector<DataModelNodePosition> nodes;
    DataModelViewport viewport;

    bool operator==(const DataModelLayout&) const = default;
};

struct ProjectDocument {
    std::uint32_t schema_version = kProjectSchemaVersion;
    std::string project_id;
    ProjectMetadata metadata;
    ProjectMode mode = ProjectMode::Normal;
    DescriptorSetReference descriptor_set;
    std::vector<ControlledUnitInstance> controlled_units;
    GlobalSettings global_settings;
    std::vector<DisplayTab> display_tabs;
    DataModelLayout data_model_layout;

    bool operator==(const ProjectDocument&) const = default;
};

/** Exact lowercase RFC 4122 UUIDv4 text syntax. */
[[nodiscard]] bool is_uuid_v4(std::string_view value) noexcept;

/** Stable project/type/display identifier syntax used by schema version 1. */
[[nodiscard]] bool is_entity_id(std::string_view value) noexcept;

/** Stable lower-camel public/owner role syntax used by schema version 1. */
[[nodiscard]] bool is_role_id(std::string_view value) noexcept;

/**
 * Count Java UTF-16 code units represented by strict UTF-8.
 *
 * Throws std::invalid_argument for malformed UTF-8.
 */
[[nodiscard]] std::size_t java_utf16_code_unit_length(
    std::string_view utf8);

/**
 * Reproduce Java String.trim(): remove leading and trailing code units whose
 * values are at most U+0020. Throws std::invalid_argument for malformed UTF-8.
 */
[[nodiscard]] std::string trim_java_string(std::string_view utf8);

/** PAMGuard item-name length/non-empty check after Java trimming. */
[[nodiscard]] bool is_valid_java_item_name(std::string_view utf8);

} // namespace pamguard::project
