#include "pamguard/core/DataModel.h"
#include "pamguard/core/LevelMeterSettings.h"
#include "pamguard/core/OperatorNodes.h"
#include "pamguard/core/SignalNodes.h"
#include "pamguard/project/LevelMeterControlledUnit.h"

#include <any>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using pamguard::core::AudioChunk;
using pamguard::core::DataBlock;
using pamguard::core::DataBlockDescriptor;
using pamguard::core::DataUnit;
using pamguard::core::DataUnitMetadata;
using pamguard::core::GraphLevelMeasurement;
using pamguard::core::LevelMeterNode;
using pamguard::core::LevelMeterNodeConfig;
using pamguard::core::LevelMeterScaleReference;
using pamguard::core::LevelMeterScaleType;
using pamguard::core::LevelMeterSettingsError;
using pamguard::core::kLevelMeasurementDataType;
using pamguard::core::kRawAudioDataType;
using pamguard::core::level_meter_default_settings_json;
using pamguard::core::level_meter_settings_from_json;
using pamguard::core::level_meter_settings_to_json;
using pamguard::core::make_data_unit;

std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= line.size()) {
        const auto comma = line.find(',', start);
        result.push_back(line.substr(
            start,
            comma == std::string::npos
                ? std::string::npos
                : comma - start));
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    return result;
}

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void check_java_defaults(const std::string& fixture_path) {
    std::ifstream fixture(fixture_path);
    if (!fixture) {
        throw std::runtime_error(
            "Could not open Level Meter Java fixture");
    }
    std::string header;
    std::string row;
    std::getline(fixture, header);
    std::getline(fixture, row);
    const auto values = split_csv(row);
    require(
        header ==
            "minLevel,scaleReference,scaleType,fullScaleValue,"
            "voltsValue,micropascalValue,peakValue,rmsValue" &&
            values.size() == 8,
        "Level Meter Java fixture shape changed");

    const auto settings = level_meter_settings_from_json(
        level_meter_default_settings_json(),
        1);
    require(
        settings.min_level_db == std::stoi(values[0]) &&
            static_cast<std::uint32_t>(settings.scale_reference) ==
                static_cast<std::uint32_t>(std::stoul(values[1])) &&
            static_cast<std::uint32_t>(settings.scale_type) ==
                static_cast<std::uint32_t>(std::stoul(values[2])) &&
            static_cast<std::uint32_t>(
                LevelMeterScaleReference::FullScale) ==
                static_cast<std::uint32_t>(std::stoul(values[3])) &&
            static_cast<std::uint32_t>(
                LevelMeterScaleReference::Volts) ==
                static_cast<std::uint32_t>(std::stoul(values[4])) &&
            static_cast<std::uint32_t>(
                LevelMeterScaleReference::Micropascal) ==
                static_cast<std::uint32_t>(std::stoul(values[5])) &&
            static_cast<std::uint32_t>(LevelMeterScaleType::Peak) ==
                static_cast<std::uint32_t>(std::stoul(values[6])) &&
            static_cast<std::uint32_t>(LevelMeterScaleType::Rms) ==
                static_cast<std::uint32_t>(std::stoul(values[7])),
        "Level Meter portable defaults or enum values diverged from Java");
    require(
        level_meter_settings_to_json(settings) ==
            level_meter_default_settings_json(),
        "Level Meter settings did not round-trip canonically");
}

void check_validation() {
    for (const auto* invalid : {
             R"({"minLevel":0,"scaleReference":0,"scaleType":0})",
             R"({"minLevel":-80,"scaleReference":3,"scaleType":0})",
             R"({"minLevel":-80,"scaleReference":0,"scaleType":2})",
             R"({"minLevel":-80,"scaleReference":0,"scaleType":0,"extra":1})",
         }) {
        bool rejected = false;
        try {
            (void)level_meter_settings_from_json(invalid, 1);
        }
        catch (const LevelMeterSettingsError&) {
            rejected = true;
        }
        require(rejected, "Invalid Level Meter settings were accepted");
    }
}

void check_descriptor() {
    const auto descriptor =
        pamguard::project::make_level_meter_controlled_unit_descriptor();
    require(
        descriptor.id == "pamguard.level-meter" &&
            descriptor.java_authority.class_name ==
                "levelMeter.LevelMeterControl" &&
            descriptor.java_authority.menu_group == "Displays" &&
            descriptor.public_roles.size() == 2 &&
            descriptor.public_roles[0].id == "rawAudio" &&
            descriptor.public_roles[0].
                    default_provider_controlled_unit_type_id ==
                "pamguard.acquisition" &&
            descriptor.settings.default_settings_json ==
                level_meter_default_settings_json() &&
            descriptor.runtime_recipe.children.size() == 1 &&
            descriptor.runtime_recipe.children[0].settings.adapter_id ==
                "pamguard.level-meter-settings.v1" &&
            descriptor.runtime_recipe.display_provider_ids ==
                std::vector<std::string>{
                    "pamguard.level-meter-display",
                },
        "Level Meter controlled-unit descriptor is incomplete");

    const auto provider =
        pamguard::project::make_level_meter_display_provider_descriptor();
    require(
        provider.owner_controlled_unit_type_id ==
                "pamguard.level-meter" &&
            provider.minimum_instances == 1 &&
            provider.maximum_instances ==
                std::optional<std::size_t>{1} &&
            provider.public_roles.size() == 1 &&
            provider.public_roles[0].data_type ==
                "pamguard.level-measurement",
        "Level Meter static display ownership is incomplete");
}

void check_runtime_math() {
    auto input = std::make_shared<DataBlock>(DataBlockDescriptor{
        "level-input",
        "Level input",
        "source",
        "audio",
        kRawAudioDataType,
        1,
        4.0,
        3,
    });
    auto output = std::make_shared<DataBlock>(DataBlockDescriptor{
        "level-output",
        "Level output",
        "meter",
        "levels",
        kLevelMeasurementDataType,
        1,
        4.0,
        3,
    });
    LevelMeterNode meter(
        "meter",
        LevelMeterNodeConfig{1.0, 3},
        input,
        output);
    GraphLevelMeasurement result;
    auto subscription = output->subscribe(
        [&](const DataUnit& unit) {
            result =
                std::any_cast<GraphLevelMeasurement>(unit.payload);
        });
    meter.prepare();
    meter.start();

    AudioChunk audio;
    audio.sample_rate_hz = 4;
    audio.channel_count = 2;
    audio.interleaved_pcm = {
        0.5, 0.0,
        -0.5, 1.0,
        0.5, 0.0,
        -0.5, -1.0,
    };
    DataUnitMetadata metadata;
    metadata.duration_samples = 4;
    metadata.channel_bitmap = 3;
    input->publish(make_data_unit(
        std::move(metadata),
        std::move(audio)));
    meter.stop();

    constexpr double tolerance = 1e-12;
    require(
        result.measured_frames == 4 &&
            result.peak_dbfs.size() == 2 &&
            result.rms_dbfs.size() == 2 &&
            std::abs(
                result.peak_dbfs[0] -
                20.0 * std::log10(0.5)) < tolerance &&
            std::abs(
                result.rms_dbfs[0] -
                20.0 * std::log10(0.5)) < tolerance &&
            std::abs(result.peak_dbfs[1]) < tolerance &&
            std::abs(
                result.rms_dbfs[1] -
                20.0 * std::log10(std::sqrt(0.5))) < tolerance,
        "Level Meter peak/RMS runtime math diverged from Java semantics");
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            std::cerr
                << "Usage: level_meter_settings_check "
                   "<settings-defaults.csv>\n";
            return 2;
        }
        check_java_defaults(argv[1]);
        check_validation();
        check_descriptor();
        check_runtime_math();
        std::cout
            << "Level Meter Java defaults, canonical settings, "
               "descriptor/display ownership, and peak/RMS math passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
