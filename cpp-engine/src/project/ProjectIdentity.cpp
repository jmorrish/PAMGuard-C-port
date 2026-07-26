#include "pamguard/project/ProjectIdentity.h"

#include <array>
#include <charconv>
#include <random>
#include <stdexcept>

#include "pamguard/project/ProjectJson.h"

namespace pamguard::project {

namespace {

std::string bytes_from_hex(std::string_view hex) {
    if (hex.size() % 2 != 0) {
        throw std::invalid_argument(
            "Hex digest must contain complete bytes");
    }
    const auto nibble = [](char character) -> std::uint8_t {
        if (character >= '0' && character <= '9') {
            return static_cast<std::uint8_t>(character - '0');
        }
        if (character >= 'a' && character <= 'f') {
            return static_cast<std::uint8_t>(
                character - 'a' + 10);
        }
        throw std::invalid_argument(
            "Hex digest must be lowercase hexadecimal");
    };
    std::string bytes(hex.size() / 2, '\0');
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = static_cast<char>(
            (nibble(hex[index * 2]) << 4U) |
            nibble(hex[index * 2 + 1]));
    }
    return bytes;
}

std::string base64url_no_padding(std::string_view bytes) {
    static constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789-_";
    std::string encoded;
    encoded.reserve((bytes.size() * 4 + 2) / 3);
    for (std::size_t offset = 0; offset < bytes.size(); offset += 3) {
        const auto remaining = bytes.size() - offset;
        const auto first = static_cast<std::uint8_t>(
            static_cast<unsigned char>(bytes[offset]));
        const auto second = remaining > 1
            ? static_cast<std::uint8_t>(
                  static_cast<unsigned char>(bytes[offset + 1]))
            : std::uint8_t{0};
        const auto third = remaining > 2
            ? static_cast<std::uint8_t>(
                  static_cast<unsigned char>(bytes[offset + 2]))
            : std::uint8_t{0};
        const auto bits =
            (static_cast<std::uint32_t>(first) << 16U) |
            (static_cast<std::uint32_t>(second) << 8U) |
            static_cast<std::uint32_t>(third);
        encoded.push_back(alphabet[(bits >> 18U) & 0x3FU]);
        encoded.push_back(alphabet[(bits >> 12U) & 0x3FU]);
        if (remaining > 1) {
            encoded.push_back(alphabet[(bits >> 6U) & 0x3FU]);
        }
        if (remaining > 2) {
            encoded.push_back(alphabet[bits & 0x3FU]);
        }
    }
    return encoded;
}

void append_part(
    std::string& destination,
    std::string_view value) {
    std::array<char, 32> size_buffer{};
    const auto [end, error] = std::to_chars(
        size_buffer.data(),
        size_buffer.data() + size_buffer.size(),
        value.size());
    if (error != std::errc{}) {
        throw std::runtime_error(
            "Could not encode ETag component length");
    }
    destination.append(size_buffer.data(), end);
    destination.push_back(':');
    destination.append(value);
    destination.push_back(';');
}

} // namespace

std::string uuid_v4_from_bytes(
    std::array<std::uint8_t, 16> bytes) {
    bytes[6] = static_cast<std::uint8_t>(
        (bytes[6] & 0x0FU) | 0x40U);
    bytes[8] = static_cast<std::uint8_t>(
        (bytes[8] & 0x3FU) | 0x80U);
    static constexpr std::array<char, 16> hexadecimal = {
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
    };
    std::string result;
    result.reserve(36);
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index == 4 || index == 6 ||
            index == 8 || index == 10) {
            result.push_back('-');
        }
        result.push_back(hexadecimal[bytes[index] >> 4U]);
        result.push_back(hexadecimal[bytes[index] & 0x0FU]);
    }
    return result;
}

std::string generate_uuid_v4() {
    std::random_device source;
    std::uniform_int_distribution<unsigned int> byte(0, 255);
    std::array<std::uint8_t, 16> bytes{};
    for (auto& value : bytes) {
        value = static_cast<std::uint8_t>(byte(source));
    }
    return uuid_v4_from_bytes(bytes);
}

std::string strong_sha256_etag(
    std::string_view prefix,
    std::string_view canonical_bytes) {
    if (prefix.empty()) {
        throw std::invalid_argument(
            "ETag prefix must not be empty");
    }
    for (const auto character : prefix) {
        if (!((character >= 'a' && character <= 'z') ||
              (character >= '0' && character <= '9'))) {
            throw std::invalid_argument(
                "ETag prefix must be lowercase alphanumeric");
        }
    }
    const auto digest =
        bytes_from_hex(sha256_hex(canonical_bytes));
    return "\"" + std::string(prefix) + "-" +
        base64url_no_padding(digest) + "\"";
}

std::string project_authority_etag(
    std::string_view project_id,
    std::uint64_t authority_revision,
    std::string_view working_content_hash,
    const std::optional<std::string>& saved_content_hash) {
    if (!is_uuid_v4(project_id)) {
        throw std::invalid_argument(
            "Project ETag requires a lowercase UUIDv4");
    }
    std::array<char, 32> revision_buffer{};
    const auto [revision_end, revision_error] =
        std::to_chars(
            revision_buffer.data(),
            revision_buffer.data() + revision_buffer.size(),
            authority_revision);
    if (revision_error != std::errc{}) {
        throw std::runtime_error(
            "Could not encode project authority revision");
    }

    std::string canonical;
    canonical.reserve(
        project_id.size() +
        working_content_hash.size() +
        (saved_content_hash
             ? saved_content_hash->size()
             : 1) +
        96);
    append_part(canonical, "pamguard-project-authority-etag-v1");
    append_part(canonical, project_id);
    append_part(
        canonical,
        std::string_view(
            revision_buffer.data(),
            static_cast<std::size_t>(
                revision_end - revision_buffer.data())));
    append_part(canonical, working_content_hash);
    append_part(
        canonical,
        saved_content_hash
            ? std::string_view(*saved_content_hash)
            : std::string_view("-"));
    return strong_sha256_etag("pgp1", canonical);
}

} // namespace pamguard::project
