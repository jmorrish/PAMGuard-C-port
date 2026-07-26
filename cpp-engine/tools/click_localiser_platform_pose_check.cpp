#include <algorithm>
#include <any>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "pamguard/core/BuiltInModules.h"
#include "pamguard/core/DetectorNodes.h"
#include "pamguard/core/LocalisationNodes.h"
#include "pamguard/detectors/ClickDetectorEngine.h"

namespace {

using pamguard::core::ClickLocaliserNode;
using pamguard::core::ClickLocaliserNodeConfig;
using pamguard::core::ClickLocaliserNodeOutputs;
using pamguard::core::DataBlock;
using pamguard::core::DataBlockDescriptor;
using pamguard::core::DataUnitMetadata;
using pamguard::detectors::ClickDetectionResult;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::shared_ptr<DataBlock> block(
    std::string id,
    std::string data_type) {
    DataBlockDescriptor descriptor;
    descriptor.id = std::move(id);
    descriptor.name = descriptor.id;
    descriptor.producer_module_id = "fixture";
    descriptor.producer_port_id = descriptor.id;
    descriptor.data_type = std::move(data_type);
    descriptor.schema_version = 1;
    descriptor.sample_rate_hz = 48'000.0;
    descriptor.channel_bitmap = 3;
    descriptor.history_capacity = 16;
    return std::make_shared<DataBlock>(std::move(descriptor));
}

ClickDetectionResult run(
    const double static_heading_degrees,
    const bool click_orientation_declared,
    const double click_heading_degrees) {
    auto input = block("click-input", pamguard::core::kClickDataType);
    auto accepted =
        block("accepted", pamguard::core::kClickDataType);
    auto localisations =
        block(
            "localisations",
            pamguard::core::kClickLocalisationDataType);
    auto bearings =
        block(
            "bearings",
            pamguard::core::kClickBearingDataType);

    ClickLocaliserNodeConfig config;
    config.array.speed_of_sound_mps = 1500.0;
    config.array.orientation.declared = true;
    config.array.orientation.heading_degrees =
        static_heading_degrees;
    config.array.hydrophones = {
        {
            .channel = 0,
            .x_m = -0.5,
            .y_m = 0.0,
            .z_m = 0.0,
        },
        {
            .channel = 1,
            .x_m = 0.5,
            .y_m = 0.0,
            .z_m = 0.0,
        },
    };
    config.pre_sample = 16;

    ClickLocaliserNode node(
        "localiser",
        48'000.0,
        std::move(config),
        input,
        ClickLocaliserNodeOutputs{
            accepted,
            localisations,
            bearings,
        });
    std::vector<ClickDetectionResult> observed;
    auto subscription =
        accepted->subscribe([&](const auto& unit) {
            observed.push_back(
                std::any_cast<ClickDetectionResult>(
                    unit.payload));
        });
    node.prepare();
    node.start();

    ClickDetectionResult click;
    click.channel_bitmap = 3;
    click.trigger_bitmap = 3;
    click.start_sample = 100;
    click.duration_samples = 64;
    click.time_unix_ms = 1'700'000'000'000;
    click.channels = {0, 1};
    click.waveform.assign(2, std::vector<double>(64, 0.0));
    click.waveform[0][24] = 1.0;
    click.waveform[1][24] = 1.0;
    click.orientation_declared = click_orientation_declared;
    click.orientation_heading_degrees = click_heading_degrees;
    click.orientation_pitch_degrees = 0.0;
    click.orientation_roll_degrees = 0.0;
    click.navigation_origin_declared = true;
    click.navigation_origin_east_metres = 123.5;
    click.navigation_origin_north_metres = -88.25;
    click.navigation_origin_height_metres = -12.0;
    click.navigation_reference_id = "fixture-acquisition";

    DataUnitMetadata metadata;
    metadata.uid = 99;
    metadata.sequence = 99;
    metadata.time_unix_ms = click.time_unix_ms;
    metadata.start_sample = click.start_sample;
    metadata.duration_samples = click.duration_samples;
    metadata.channel_bitmap = click.channel_bitmap;
    input->publish(
        pamguard::core::make_data_unit(
            std::move(metadata),
            std::move(click)));
    node.stop();

    require(
        observed.size() == 1,
        "Click localiser did not publish one accepted click");
    return std::move(observed.front());
}

} // namespace

int main() {
    try {
        const auto dynamic = run(13.0, true, 47.0);
        const auto equivalent_static = run(47.0, false, 0.0);
        require(
            dynamic.navigation_origin_declared &&
                dynamic.navigation_origin_east_metres == 123.5 &&
                dynamic.navigation_origin_north_metres == -88.25 &&
                dynamic.navigation_origin_height_metres == -12.0 &&
                dynamic.navigation_reference_id ==
                    "fixture-acquisition",
            "Accepted click lost its trigger-onset navigation origin");
        require(
            !dynamic.earth_bearing_ambiguities_radians.empty() &&
                dynamic.earth_bearing_ambiguities_radians.size() ==
                    equivalent_static.
                        earth_bearing_ambiguities_radians.size(),
            "Click localiser did not attach ordered earth bearings");
        for (std::size_t index = 0;
             index <
                 dynamic.earth_bearing_ambiguities_radians.size();
             ++index) {
            const double actual =
                dynamic.earth_bearing_ambiguities_radians[index];
            const double expected =
                equivalent_static.
                    earth_bearing_ambiguities_radians[index];
            require(
                std::isfinite(actual) &&
                    std::abs(actual - expected) < 1.0e-12,
                "Per-click orientation did not override the static "
                "array pose deterministically");
        }
        std::cout
            << "Click localiser trigger-onset platform pose and "
               "ordered earth-bearing checks passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
