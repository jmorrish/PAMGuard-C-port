#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

#include "pamguard/project/ProjectDocument.h"

namespace pamguard::project {

class ProjectJsonError : public std::invalid_argument {
public:
    explicit ProjectJsonError(const std::string& message)
        : std::invalid_argument(message) {}
};

/**
 * Validate one standalone JSON value with the project parser's UTF-8, size,
 * depth, duplicate-key, syntax, and finite-number rules.
 *
 * This intentionally exposes no JSON-library type; persistence envelopes can
 * validate raw input before using their private DOM representation.
 */
void validate_strict_json(std::string_view json);

/**
 * Parse, normalize, and validate a schemaVersion 1 project document.
 *
 * Parsing rejects malformed UTF-8, duplicate object keys, unknown structured
 * fields, non-finite numbers, excessive nesting/collection sizes, and broken
 * document-local references. Per-controlled-unit settings semantics are
 * deliberately left to the controlled-unit registry.
 */
[[nodiscard]] ProjectDocument project_document_from_json(
    std::string_view json);

/**
 * Serialize a normalized and validated project document.
 *
 * Object keys remain lexicographically ordered. `pretty` only adds whitespace;
 * it does not alter value or array ordering.
 */
[[nodiscard]] std::string project_document_to_json(
    const ProjectDocument& document,
    bool pretty = false);

/** Compact deterministic pamguard-canonical-json-v1 representation. */
[[nodiscard]] std::string project_document_to_canonical_json(
    const ProjectDocument& document);

/** `sha256:` followed by the canonical project JSON digest in lowercase hex. */
[[nodiscard]] std::string project_content_hash(
    const ProjectDocument& document);

/** Lowercase hexadecimal SHA-256, exposed for standard fixture verification. */
[[nodiscard]] std::string sha256_hex(std::string_view bytes);

} // namespace pamguard::project
