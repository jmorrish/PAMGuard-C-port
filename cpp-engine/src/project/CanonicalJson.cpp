#include "CanonicalJson.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

#include "pamguard/project/ProjectJson.h"

namespace pamguard::project::detail {

namespace {

using Json = nlohmann::json;

[[noreturn]] void fail(
    std::string_view context,
    const std::string& message) {
    throw ProjectJsonError(std::string(context) + ": " + message);
}

template <typename Callback>
void for_each_utf8_scalar(
    std::string_view value,
    std::string_view context,
    Callback&& callback) {
    const auto continuation = [&](std::size_t index) -> std::uint8_t {
        if (index >= value.size()) {
            fail(context, "truncated UTF-8 sequence");
        }
        const auto byte =
            static_cast<std::uint8_t>(
                static_cast<unsigned char>(value[index]));
        if ((byte & 0xC0U) != 0x80U) {
            fail(context, "invalid UTF-8 continuation byte");
        }
        return byte;
    };

    for (std::size_t index = 0; index < value.size();) {
        const auto first =
            static_cast<std::uint8_t>(
                static_cast<unsigned char>(value[index]));
        std::uint32_t scalar = 0;
        std::size_t length = 0;

        if (first <= 0x7FU) {
            scalar = first;
            length = 1;
        }
        else if (first >= 0xC2U && first <= 0xDFU) {
            const auto second = continuation(index + 1);
            scalar =
                (static_cast<std::uint32_t>(first & 0x1FU) << 6U) |
                static_cast<std::uint32_t>(second & 0x3FU);
            length = 2;
        }
        else if (first >= 0xE0U && first <= 0xEFU) {
            const auto second = continuation(index + 1);
            const auto third = continuation(index + 2);
            if ((first == 0xE0U && second < 0xA0U) ||
                (first == 0xEDU && second >= 0xA0U)) {
                fail(
                    context,
                    "overlong UTF-8 or encoded surrogate code point");
            }
            scalar =
                (static_cast<std::uint32_t>(first & 0x0FU) << 12U) |
                (static_cast<std::uint32_t>(second & 0x3FU) << 6U) |
                static_cast<std::uint32_t>(third & 0x3FU);
            length = 3;
        }
        else if (first >= 0xF0U && first <= 0xF4U) {
            const auto second = continuation(index + 1);
            const auto third = continuation(index + 2);
            const auto fourth = continuation(index + 3);
            if ((first == 0xF0U && second < 0x90U) ||
                (first == 0xF4U && second > 0x8FU)) {
                fail(
                    context,
                    "overlong UTF-8 or code point above U+10FFFF");
            }
            scalar =
                (static_cast<std::uint32_t>(first & 0x07U) << 18U) |
                (static_cast<std::uint32_t>(second & 0x3FU) << 12U) |
                (static_cast<std::uint32_t>(third & 0x3FU) << 6U) |
                static_cast<std::uint32_t>(fourth & 0x3FU);
            length = 4;
        }
        else {
            fail(context, "invalid UTF-8 leading byte");
        }

        callback(scalar);
        index += length;
    }
}

void check_json_nesting(
    std::string_view value,
    std::string_view context,
    std::size_t maximum_depth) {
    std::size_t depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (const auto character : value) {
        if (in_string) {
            if (escaped) {
                escaped = false;
            }
            else if (character == '\\') {
                escaped = true;
            }
            else if (character == '"') {
                in_string = false;
            }
            continue;
        }
        if (character == '"') {
            in_string = true;
        }
        else if (character == '{' || character == '[') {
            ++depth;
            if (depth > maximum_depth) {
                fail(
                    context,
                    "JSON nesting exceeds " +
                        std::to_string(maximum_depth));
            }
        }
        else if ((character == '}' || character == ']') && depth > 0) {
            --depth;
        }
    }
}

void normalize_value(
    Json& value,
    std::string_view context,
    std::size_t depth,
    std::size_t maximum_depth) {
    if (depth > maximum_depth) {
        fail(
            context,
            "JSON value nesting exceeds " +
                std::to_string(maximum_depth));
    }

    if (value.is_number_float()) {
        const auto number = value.get<double>();
        if (!std::isfinite(number)) {
            fail(context, "JSON contains a non-finite number");
        }
        if (number == 0.0) {
            value = std::int64_t{0};
        }
        else if (std::trunc(number) == number) {
            // Integral-looking binary64 values outside this exact domain may
            // already represent a neighbouring decimal token. Reject them so
            // canonical hashes cannot depend on lossy parser rounding or on
            // spelling an integer as a floating-point number.
            constexpr double kMaximumExactBinary64Integer =
                9007199254740991.0; // 2^53 - 1
            if (std::abs(number) >
                kMaximumExactBinary64Integer) {
                fail(
                    context,
                    "integral floating-point JSON values must be within "
                    "the exact IEEE-754 integer range");
            }
            value = static_cast<std::int64_t>(number);
        }
        return;
    }

    if (value.is_string()) {
        validate_utf8(
            value.get_ref<const std::string&>(),
            context);
        return;
    }

    if (value.is_object()) {
        for (auto iterator = value.begin();
             iterator != value.end();
             ++iterator) {
            validate_utf8(iterator.key(), context);
            normalize_value(
                iterator.value(),
                context,
                depth + 1,
                maximum_depth);
        }
        return;
    }

    if (value.is_array()) {
        for (auto& item : value) {
            normalize_value(
                item,
                context,
                depth + 1,
                maximum_depth);
        }
    }
}

} // namespace

void validate_utf8(
    std::string_view value,
    std::string_view context) {
    for_each_utf8_scalar(
        value,
        context,
        [](std::uint32_t) {});
}

Json parse_strict_json(
    std::string_view value,
    std::string_view context,
    std::size_t maximum_bytes,
    std::size_t maximum_depth) {
    if (value.size() > maximum_bytes) {
        fail(
            context,
            "JSON exceeds " + std::to_string(maximum_bytes) +
                " bytes");
    }
    validate_utf8(value, context);
    if (value.size() >= 3 &&
        static_cast<unsigned char>(value[0]) == 0xEFU &&
        static_cast<unsigned char>(value[1]) == 0xBBU &&
        static_cast<unsigned char>(value[2]) == 0xBFU) {
        fail(context, "UTF-8 byte-order marks are not accepted");
    }
    check_json_nesting(value, context, maximum_depth);

    std::vector<std::unordered_set<std::string>> object_keys;
    const Json::parser_callback_t callback =
        [&](int, Json::parse_event_t event, Json& parsed) {
            switch (event) {
            case Json::parse_event_t::object_start:
                object_keys.emplace_back();
                break;
            case Json::parse_event_t::key: {
                if (object_keys.empty()) {
                    fail(context, "internal duplicate-key parser error");
                }
                const auto& key =
                    parsed.get_ref<const std::string&>();
                if (!object_keys.back().insert(key).second) {
                    fail(
                        context,
                        "duplicate object key '" + key + "'");
                }
                break;
            }
            case Json::parse_event_t::object_end:
                if (!object_keys.empty()) {
                    object_keys.pop_back();
                }
                break;
            default:
                break;
            }
            return true;
        };

    try {
        auto parsed = Json::parse(
            value.begin(),
            value.end(),
            callback,
            true,
            false);
        normalize_json_numbers(parsed, context, maximum_depth);
        return parsed;
    }
    catch (const ProjectJsonError&) {
        throw;
    }
    catch (const nlohmann::json::exception& error) {
        fail(context, error.what());
    }
}

void normalize_json_numbers(
    Json& value,
    std::string_view context,
    std::size_t maximum_depth) {
    normalize_value(value, context, 1, maximum_depth);
}

std::string canonical_json_dump(Json value) {
    normalize_json_numbers(
        value,
        "Canonical JSON",
        kMaximumJsonDepth);
    try {
        return value.dump(
            -1,
            ' ',
            false,
            Json::error_handler_t::strict);
    }
    catch (const nlohmann::json::exception& error) {
        fail("Canonical JSON", error.what());
    }
}

std::size_t utf8_scalar_count(
    std::string_view value,
    std::string_view context) {
    std::size_t count = 0;
    for_each_utf8_scalar(
        value,
        context,
        [&](std::uint32_t) { ++count; });
    return count;
}

} // namespace pamguard::project::detail
