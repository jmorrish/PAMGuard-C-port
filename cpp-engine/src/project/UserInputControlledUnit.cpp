#include "pamguard/project/UserInputControlledUnit.h"

#include <limits>
#include <optional>
#include <set>
#include <utility>

#include <json.hpp>

namespace pamguard::project {

namespace {

using Json = nlohmann::json;

InstanceRulesDescriptor one_in_all_modes() {
    return {
        0,
        std::optional<std::size_t>{1},
        {
            RunMode::Normal,
            RunMode::Mixed,
            RunMode::Viewer,
        },
        {},
    };
}

std::uint32_t utf16_code_units(std::string_view value) {
    std::uint64_t units = 0;
    for (std::size_t offset = 0; offset < value.size();) {
        const auto first =
            static_cast<unsigned char>(value[offset]);
        std::uint32_t code_point = 0;
        std::size_t length = 0;
        std::uint32_t minimum = 0;
        if (first <= 0x7Fu) {
            code_point = first;
            length = 1;
        }
        else if ((first & 0xE0u) == 0xC0u) {
            code_point = first & 0x1Fu;
            length = 2;
            minimum = 0x80u;
        }
        else if ((first & 0xF0u) == 0xE0u) {
            code_point = first & 0x0Fu;
            length = 3;
            minimum = 0x800u;
        }
        else if ((first & 0xF8u) == 0xF0u) {
            code_point = first & 0x07u;
            length = 4;
            minimum = 0x10000u;
        }
        else {
            throw UserInputProjectAdapterError(
                "invalid-user-input-comment-encoding",
                "User Input comment is not valid UTF-8");
        }
        if (offset + length > value.size()) {
            throw UserInputProjectAdapterError(
                "invalid-user-input-comment-encoding",
                "User Input comment ends inside a UTF-8 sequence");
        }
        for (std::size_t index = 1; index < length; ++index) {
            const auto next = static_cast<unsigned char>(
                value[offset + index]);
            if ((next & 0xC0u) != 0x80u) {
                throw UserInputProjectAdapterError(
                    "invalid-user-input-comment-encoding",
                    "User Input comment is not valid UTF-8");
            }
            code_point =
                (code_point << 6u) | (next & 0x3Fu);
        }
        if (code_point < minimum ||
            code_point > 0x10FFFFu ||
            (code_point >= 0xD800u &&
             code_point <= 0xDFFFu)) {
            throw UserInputProjectAdapterError(
                "invalid-user-input-comment-encoding",
                "User Input comment contains an invalid Unicode scalar");
        }
        units += code_point > 0xFFFFu ? 2u : 1u;
        if (units >
            kUserInputMaximumCommentUtf16CodeUnits) {
            throw UserInputProjectAdapterError(
                "user-input-comment-too-long",
                "User Input comment exceeds Java "
                "UserInputController.maxCommentLength");
        }
        offset += length;
    }
    return static_cast<std::uint32_t>(units);
}

Json parse_json(
    std::string_view encoded,
    std::string_view document_name) {
    try {
        return Json::parse(encoded);
    }
    catch (const Json::exception& error) {
        throw UserInputProjectAdapterError(
            "invalid-user-input-json",
            "User Input " + std::string(document_name) +
                " is not valid JSON: " + error.what());
    }
}

} // namespace

UserInputProjectAdapterError::UserInputProjectAdapterError(
    std::string code,
    std::string message)
    : std::invalid_argument(std::move(message)),
      code_(std::move(code)) {}

const std::string&
UserInputProjectAdapterError::code() const noexcept {
    return code_;
}

ControlledUnitDescriptor
make_user_input_controlled_unit_descriptor() {
    return {
        std::string(kUserInputControlledUnitTypeId),
        1,
        {
            "User input",
            "Utilities",
            "UserInput.UserInputController",
            "direct",
            "Creates a form for the user to type comments into",
            "utilities/userInputHelp/docs/userInput.html",
            {
                "src/PamModel/PamModel.java",
                "src/UserInput/UserInputController.java",
                "src/UserInput/UserInputProcess.java",
                "src/UserInput/UserInputDataUnit.java",
                "src/UserInput/UserInputPanel.java",
                "src/UserInput/UserInputSidePanel.java",
                "src/UserInput/UserInputLogger.java",
                "src/UserInput/UserInputOverlayGraphics.java",
            },
        },
        one_in_all_modes(),
        {
            {
                "entries",
                "User Input Data",
                DataRoleDirection::Output,
                std::string(kUserInputDataType),
                RoleCardinality::ExactlyOne,
                {
                    "events",
                    "annotations",
                },
                {},
                std::nullopt,
            },
        },
        {
            1,
            {
                // UserInputController does not implement PamSettings; it is
                // the Java source authority for that deliberate absence.
                "UserInput.UserInputController",
            },
            {
                "src/UserInput/UserInputController.java",
                "src/UserInput/UserInputProcess.java",
                "src/UserInput/UserInputDataUnit.java",
                "src/UserInput/UserInputPanel.java",
                "src/UserInput/UserInputSidePanel.java",
            },
            "{}",
            {
                {
                    "operator.form.sections",
                    {
                        "Enter Comment",
                        "Entries:",
                    },
                },
                {
                    "operator.form.actions",
                    {
                        "Submit comment",
                        "Clear comment",
                    },
                },
            },
            {},
            SettingsChangePolicy::LiveSafe,
            "java-source-validated-no-settings",
            R"({
                "$schema":"https://json-schema.org/draft/2020-12/schema",
                "type":"object",
                "additionalProperties":false,
                "maxProperties":0
            })",
        },
        {
            1,
            {
                {
                    "user-input",
                    std::string(kUserInputControlledUnitTypeId),
                    {
                        "",
                        std::string(
                            kUserInputRuntimeSettingsAdapterId),
                    },
                    true,
                    AvailabilityStatus::Unavailable,
                    "dedicated-user-input-data-runtime-required",
                },
            },
            {
                {
                    "entries",
                    {
                        "user-input",
                        "events",
                    },
                },
            },
            {},
            {},
            "pamguard.user-input.runtime",
        },
        AvailabilityStatus::Unavailable,
        "experimental",
    };
}

std::string user_input_runtime_settings_json(
    std::string_view portable_settings_json,
    std::uint32_t settings_version) {
    if (settings_version != 1) {
        throw UserInputProjectAdapterError(
            "unsupported-user-input-settings-version",
            "Unsupported User Input settings version");
    }
    const auto settings =
        parse_json(portable_settings_json, "settings");
    if (!settings.is_object() || !settings.empty()) {
        throw UserInputProjectAdapterError(
            "invalid-user-input-settings",
            "User Input has no Java settings; its portable settings "
            "document must be an empty object");
    }
    return Json{
        {"channelBitmap", kUserInputChannelBitmap},
        {
            "maximumCommentUtf16CodeUnits",
            kUserInputMaximumCommentUtf16CodeUnits,
        },
        {
            "naturalLifetimeSeconds",
            kUserInputNaturalLifetimeSeconds,
        },
        {
            "requiredHistoryMilliseconds",
            kUserInputRequiredHistoryMilliseconds,
        },
    }.dump();
}

UserInputDataEntry
user_input_data_entry_from_submit_action_json(
    std::string_view action_json,
    std::int64_t time_milliseconds) {
    const auto action = parse_json(action_json, "submit action");
    if (!action.is_object() ||
        action.size() != 1 ||
        !action.contains("comment") ||
        !action.at("comment").is_string()) {
        throw UserInputProjectAdapterError(
            "invalid-user-input-submit-action",
            "User Input submit action must contain exactly one string "
            "field named 'comment'");
    }
    const auto comment =
        action.at("comment").get<std::string>();
    if (comment.empty()) {
        throw UserInputProjectAdapterError(
            "empty-user-input-comment",
            "User Input cannot submit an empty comment");
    }
    static_cast<void>(utf16_code_units(comment));
    return {
        time_milliseconds,
        comment,
        kUserInputChannelBitmap,
    };
}

std::string user_input_data_entry_to_json(
    const UserInputDataEntry& entry) {
    if (entry.user_string.empty()) {
        throw UserInputProjectAdapterError(
            "empty-user-input-comment",
            "User Input data entry cannot contain an empty comment");
    }
    static_cast<void>(utf16_code_units(entry.user_string));
    if (entry.channel_bitmap != kUserInputChannelBitmap) {
        throw UserInputProjectAdapterError(
            "invalid-user-input-channel-bitmap",
            "User Input data entries must use Java's 16-channel bitmap");
    }
    return Json{
        {"channelBitmap", entry.channel_bitmap},
        {"timeMilliseconds", entry.time_milliseconds},
        {"userString", entry.user_string},
    }.dump();
}

} // namespace pamguard::project
