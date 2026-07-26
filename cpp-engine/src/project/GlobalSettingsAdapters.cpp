#include "pamguard/project/GlobalSettingsAdapters.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>

#include <json.hpp>

#include "CanonicalJson.h"
#include "pamguard/project/ProjectDocument.h"

namespace pamguard::project {

namespace {

using Json = nlohmann::json;

[[noreturn]] void fail(const std::string& message) {
    throw GlobalSettingsAdapterError(
        "Array Manager settings: " + message);
}

void require_exact_fields(
    const Json& value,
    std::initializer_list<std::string_view> fields,
    std::string_view context) {
    if (!value.is_object()) {
        fail(std::string(context) + " must be an object");
    }
    if (value.size() != fields.size()) {
        fail(
            std::string(context) +
            " contains missing or unknown fields");
    }
    for (const auto field : fields) {
        if (!value.contains(std::string(field))) {
            fail(
                std::string(context) + " omits '" +
                std::string(field) + "'");
        }
    }
}

double finite_number(
    const Json& value,
    std::string_view context) {
    if (!value.is_number()) {
        fail(std::string(context) + " must be a number");
    }
    const auto number = value.get<double>();
    if (!std::isfinite(number)) {
        fail(std::string(context) + " must be finite");
    }
    return number;
}

std::uint64_t unsigned_integer(
    const Json& value,
    std::string_view context) {
    if (!value.is_number_integer()) {
        fail(std::string(context) + " must be an integer");
    }
    if (value.is_number_unsigned()) {
        return value.get<std::uint64_t>();
    }
    const auto number = value.get<std::int64_t>();
    if (number < 0) {
        fail(std::string(context) + " must be non-negative");
    }
    return static_cast<std::uint64_t>(number);
}

void require_nullable_string(
    const Json& value,
    std::string_view context) {
    if (!value.is_null() && !value.is_string()) {
        fail(
            std::string(context) +
            " must be a string or null");
    }
}

void require_nonempty_string(
    const Json& value,
    std::string_view context) {
    if (!value.is_string() ||
        value.get_ref<const std::string&>().empty()) {
        fail(
            std::string(context) +
            " must be a non-empty string");
    }
}

double nullable_angle(
    const Json& value,
    std::string_view context) {
    if (value.is_null()) {
        return 0.0;
    }
    return finite_number(value, context);
}

} // namespace

core::ArrayConfiguration array_manager_settings_to_geometry(
    std::string_view settings_json,
    std::uint32_t settings_version) {
    if (settings_version != kArrayManagerSettingsVersion) {
        fail(
            "unsupported settings version " +
            std::to_string(settings_version));
    }

    Json settings;
    try {
        settings = detail::parse_strict_json(
            settings_json,
            "Array Manager settings",
            detail::kMaximumEmbeddedSettingsBytes,
            detail::kMaximumEmbeddedSettingsDepth);
    }
    catch (const GlobalSettingsAdapterError&) {
        throw;
    }
    catch (const std::exception& error) {
        fail(error.what());
    }

    require_exact_fields(
        settings,
        {
            "arrayName",
            "instrumentType",
            "instrumentId",
            "speedOfSoundMps",
            "speedOfSoundErrorMps",
            "originInterpolation",
            "hydrophoneInterpolation",
            "streamers",
            "hydrophones",
        },
        "root");

    require_nonempty_string(
        settings.at("arrayName"),
        "arrayName");
    const auto array_name = trim_java_string(
        settings.at("arrayName").get_ref<const std::string&>());
    if (array_name.empty()) {
        fail("arrayName must not be empty after Java trimming");
    }
    require_nullable_string(
        settings.at("instrumentType"),
        "instrumentType");
    require_nullable_string(
        settings.at("instrumentId"),
        "instrumentId");

    const auto speed = finite_number(
        settings.at("speedOfSoundMps"),
        "speedOfSoundMps");
    if (speed <= 0.0) {
        fail("speedOfSoundMps must be positive");
    }
    const auto speed_error = finite_number(
        settings.at("speedOfSoundErrorMps"),
        "speedOfSoundErrorMps");
    if (speed_error < 0.0) {
        fail("speedOfSoundErrorMps must be non-negative");
    }
    const auto origin_interpolation = unsigned_integer(
        settings.at("originInterpolation"),
        "originInterpolation");
    const auto hydrophone_interpolation = unsigned_integer(
        settings.at("hydrophoneInterpolation"),
        "hydrophoneInterpolation");
    if (origin_interpolation > 2 ||
        hydrophone_interpolation > 2) {
        fail(
            "interpolation values must be PamArray "
            "ORIGIN_USE_LATEST..ORIGIN_USE_PRECEEDING (0..2)");
    }

    const auto& streamers = settings.at("streamers");
    if (!streamers.is_array() ||
        streamers.empty() ||
        streamers.size() > 32) {
        fail("streamers must contain 1..32 entries");
    }

    core::ArrayConfiguration geometry;
    geometry.id = array_name;
    geometry.speed_of_sound_mps = speed;
    geometry.speed_of_sound_error_mps = speed_error;
    geometry.streamers.reserve(streamers.size());

    for (std::size_t index = 0;
         index < streamers.size();
         ++index) {
        const auto& streamer = streamers.at(index);
        const auto context =
            "streamers[" + std::to_string(index) + "]";
        require_exact_fields(
            streamer,
            {
                "id",
                "name",
                "xM",
                "yM",
                "zM",
                "xErrorM",
                "yErrorM",
                "zErrorM",
                "headingDegrees",
                "pitchDegrees",
                "rollDegrees",
                "locatorClass",
                "originClass",
            },
            context);
        const auto id = unsigned_integer(
            streamer.at("id"),
            context + ".id");
        if (id != index) {
            fail(
                context +
                ".id must equal its Java list index");
        }
        require_nullable_string(
            streamer.at("name"),
            context + ".name");
        require_nonempty_string(
            streamer.at("locatorClass"),
            context + ".locatorClass");
        require_nonempty_string(
            streamer.at("originClass"),
            context + ".originClass");

        const auto x_error = finite_number(
            streamer.at("xErrorM"),
            context + ".xErrorM");
        const auto y_error = finite_number(
            streamer.at("yErrorM"),
            context + ".yErrorM");
        const auto z_error = finite_number(
            streamer.at("zErrorM"),
            context + ".zErrorM");
        if (x_error < 0.0 ||
            y_error < 0.0 ||
            z_error < 0.0) {
            fail(
                context +
                " coordinate errors must be non-negative");
        }

        geometry.streamers.push_back({
            static_cast<int>(id),
            finite_number(
                streamer.at("xM"),
                context + ".xM"),
            finite_number(
                streamer.at("yM"),
                context + ".yM"),
            finite_number(
                streamer.at("zM"),
                context + ".zM"),
            nullable_angle(
                streamer.at("headingDegrees"),
                context + ".headingDegrees"),
            nullable_angle(
                streamer.at("pitchDegrees"),
                context + ".pitchDegrees"),
            nullable_angle(
                streamer.at("rollDegrees"),
                context + ".rollDegrees"),
            x_error,
            y_error,
            z_error,
        });
    }

    const auto& hydrophones = settings.at("hydrophones");
    if (!hydrophones.is_array() ||
        hydrophones.empty() ||
        hydrophones.size() > 32) {
        fail(
            "hydrophones must contain 1..32 entries "
            "(PamConstants.MAX_CHANNELS)");
    }
    geometry.hydrophones.reserve(hydrophones.size());
    for (std::size_t index = 0;
         index < hydrophones.size();
         ++index) {
        const auto& hydrophone = hydrophones.at(index);
        const auto context =
            "hydrophones[" + std::to_string(index) + "]";
        require_exact_fields(
            hydrophone,
            {
                "channel",
                "streamerId",
                "type",
                "xM",
                "yM",
                "zM",
                "xErrorM",
                "yErrorM",
                "zErrorM",
                "sensitivityDb",
                "preampGainDb",
                "bandwidthHz",
            },
            context);

        const auto channel = unsigned_integer(
            hydrophone.at("channel"),
            context + ".channel");
        if (channel != index) {
            fail(
                context +
                ".channel must equal its Java list index");
        }
        const auto streamer_id = unsigned_integer(
            hydrophone.at("streamerId"),
            context + ".streamerId");
        if (streamer_id >= streamers.size()) {
            fail(
                context +
                ".streamerId refers to no streamer");
        }
        require_nonempty_string(
            hydrophone.at("type"),
            context + ".type");

        const auto x_error = finite_number(
            hydrophone.at("xErrorM"),
            context + ".xErrorM");
        const auto y_error = finite_number(
            hydrophone.at("yErrorM"),
            context + ".yErrorM");
        const auto z_error = finite_number(
            hydrophone.at("zErrorM"),
            context + ".zErrorM");
        if (x_error < 0.0 ||
            y_error < 0.0 ||
            z_error < 0.0) {
            fail(
                context +
                " coordinate errors must be non-negative");
        }

        const auto& bandwidth =
            hydrophone.at("bandwidthHz");
        if (!bandwidth.is_array() ||
            bandwidth.size() != 2) {
            fail(
                context +
                ".bandwidthHz must contain two numbers");
        }
        const auto bandwidth_low = finite_number(
            bandwidth.at(0),
            context + ".bandwidthHz[0]");
        const auto bandwidth_high = finite_number(
            bandwidth.at(1),
            context + ".bandwidthHz[1]");
        if (bandwidth_low < 0.0 ||
            bandwidth_high < bandwidth_low) {
            fail(
                context +
                ".bandwidthHz must be non-negative and ordered");
        }

        geometry.hydrophones.push_back({
            static_cast<std::size_t>(channel),
            finite_number(
                hydrophone.at("xM"),
                context + ".xM"),
            finite_number(
                hydrophone.at("yM"),
                context + ".yM"),
            finite_number(
                hydrophone.at("zM"),
                context + ".zM"),
            finite_number(
                hydrophone.at("sensitivityDb"),
                context + ".sensitivityDb"),
            static_cast<int>(streamer_id),
            x_error,
            y_error,
            z_error,
            finite_number(
                hydrophone.at("preampGainDb"),
                context + ".preampGainDb"),
        });
    }

    return geometry;
}

} // namespace pamguard::project
