#include <array>
#include <iostream>
#include <regex>
#include <stdexcept>

#include "pamguard/project/ProjectDocument.h"
#include "pamguard/project/ProjectIdentity.h"

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

int main() {
    try {
        std::array<std::uint8_t, 16> zeroes{};
        const auto deterministic =
            pamguard::project::uuid_v4_from_bytes(zeroes);
        require(
            deterministic ==
                "00000000-0000-4000-8000-000000000000",
            "UUID version/variant formatting changed");
        require(
            pamguard::project::is_uuid_v4(deterministic),
            "Formatted UUIDv4 was rejected");
        const auto generated =
            pamguard::project::generate_uuid_v4();
        require(
            pamguard::project::is_uuid_v4(generated),
            "Generated entity ID is not UUIDv4");

        require(
            pamguard::project::strong_sha256_etag(
                "test1",
                "abc") ==
                "\"test1-ungWv48Bz-pBQUDeXa4iI7ADYaOWF3qctBD_YfIAFa0\"",
            "Strong ETag SHA-256/base64url fixture changed");

        const auto first =
            pamguard::project::project_authority_etag(
                deterministic,
                1,
                "sha256:working",
                std::nullopt);
        const auto same =
            pamguard::project::project_authority_etag(
                deterministic,
                1,
                "sha256:working",
                std::nullopt);
        const auto saved =
            pamguard::project::project_authority_etag(
                deterministic,
                1,
                "sha256:working",
                std::string("sha256:working"));
        const auto revised =
            pamguard::project::project_authority_etag(
                deterministic,
                2,
                "sha256:working",
                std::nullopt);
        require(first == same, "Project ETag is not deterministic");
        require(
            first != saved && first != revised,
            "Project ETag omitted saved state or authority revision");
        require(
            std::regex_match(
                first,
                std::regex(
                    "^\"pgp1-[A-Za-z0-9_-]{43}\"$")),
            "Project ETag is not a quoted strong opaque token");

        std::cout
            << "Project identity check passed: UUIDv4 and strong "
               "SHA-256/base64url authority ETags\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr
            << "Project identity check failed: "
            << error.what() << "\n";
        return 1;
    }
}
