#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace pamguard::project {

/** Format 16 bytes as a lowercase RFC 4122 UUIDv4, forcing version/variant. */
[[nodiscard]] std::string uuid_v4_from_bytes(
    std::array<std::uint8_t, 16> bytes);

/** Generate a non-secret, process-local UUIDv4 entity identity. */
[[nodiscard]] std::string generate_uuid_v4();

/**
 * Return a quoted strong ETag containing a base64url SHA-256 digest.
 *
 * `prefix` is a short protocol/version token such as `pgp1`.
 */
[[nodiscard]] std::string strong_sha256_etag(
    std::string_view prefix,
    std::string_view canonical_bytes);

/** Strong active-project authority token. */
[[nodiscard]] std::string project_authority_etag(
    std::string_view project_id,
    std::uint64_t authority_revision,
    std::string_view working_content_hash,
    const std::optional<std::string>& saved_content_hash);

} // namespace pamguard::project
