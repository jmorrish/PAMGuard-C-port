#pragma once

#include <string>
#include <string_view>

namespace pamguard::project::detail {

[[nodiscard]] std::string sha256_hex_digest(std::string_view bytes);

} // namespace pamguard::project::detail
