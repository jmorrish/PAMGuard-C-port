#include "pamguard/core/LocalisationNodes.h"

#include <algorithm>
#include <any>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>

#include "pamguard/core/DetectorNodes.h"
#include "pamguard/detectors/ClickDetectorEngine.h"
#include "pamguard/localisation/ArrayShape.h"
#include "pamguard/localisation/LsqBearingLocaliser.h"
#include "pamguard/localisation/MlGridBearingLocaliser.h"
#include "pamguard/localisation/PairBearingLocaliser.h"

namespace pamguard::core {

namespace {

const ArrayHydrophone* find_hydrophone(
    const ArrayConfiguration& array,
    std::size_t channel) {
    const auto found = std::find_if(
        array.hydrophones.begin(),
        array.hydrophones.end(),
        [&](const auto& hydrophone) {
            return hydrophone.channel == channel;
        });
    return found == array.hydrophones.end() ? nullptr : &*found;
}

double distance(
    const ArrayHydrophone& first,
    const ArrayHydrophone& second) {
    const double dx = second.x_m - first.x_m;
    const double dy = second.y_m - first.y_m;
    const double dz = second.z_m - first.z_m;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

struct PairGeometry {
    std::size_t channel_a = 0;
    std::size_t channel_b = 0;
    bool constrained = false;
    double distance_m = 0.0;
    double max_delay_samples = 0.0;
    double baseline_x_m = 0.0;
    double baseline_y_m = 0.0;
    double baseline_z_m = 0.0;
};

std::vector<PairGeometry> geometry_for(
    const ClickLocaliserNodeConfig& config,
    const std::vector<std::size_t>& channels,
    double sample_rate_hz) {
    std::vector<PairGeometry> result;
    const bool can_constrain =
        config.array.speed_of_sound_mps > 0.0 &&
        std::all_of(
            channels.begin(),
            channels.end(),
            [&](std::size_t channel) {
                return find_hydrophone(config.array, channel) != nullptr;
            });
    for (std::size_t first = 0; first < channels.size(); ++first) {
        for (std::size_t second = first + 1;
             second < channels.size();
             ++second) {
            PairGeometry pair;
            pair.channel_a = channels[first];
            pair.channel_b = channels[second];
            if (can_constrain) {
                pair.distance_m = distance(
                    *find_hydrophone(config.array, pair.channel_a),
                    *find_hydrophone(config.array, pair.channel_b));
                pair.max_delay_samples = std::ceil(
                    pair.distance_m /
                    config.array.speed_of_sound_mps *
                    sample_rate_hz) + 1.0;
                const auto* first_phone =
                    find_hydrophone(config.array, pair.channel_a);
                const auto* second_phone =
                    find_hydrophone(config.array, pair.channel_b);
                pair.baseline_x_m =
                    second_phone->x_m - first_phone->x_m;
                pair.baseline_y_m =
                    second_phone->y_m - first_phone->y_m;
                pair.baseline_z_m =
                    second_phone->z_m - first_phone->z_m;
                pair.constrained = true;
            }
            result.push_back(pair);
        }
    }
    return result;
}

localisation::ArrayShapeType sub_array_shape(
    const ArrayConfiguration& array,
    const std::vector<std::size_t>& channels) {
    std::vector<std::array<double, 3>> positions;
    std::vector<int> streamer_ids;
    for (const auto channel : channels) {
        const auto* hydrophone = find_hydrophone(array, channel);
        if (!hydrophone) {
            return localisation::ArrayShapeType::None;
        }
        positions.push_back({
            hydrophone->x_m,
            hydrophone->y_m,
            hydrophone->z_m,
        });
        streamer_ids.push_back(hydrophone->streamer_id);
    }
    return localisation::array_shape(positions, streamer_ids);
}

std::vector<localisation::WorldVector> earth_frame_vectors(
    const ArrayOrientation& orientation,
    localisation::ArrayShapeType shape,
    const std::vector<localisation::WorldVector>& vectors) {
    if (!orientation.declared || vectors.empty()) {
        return {};
    }
    constexpr double kDegreesToRadians =
        std::numbers::pi / 180.0;
    return localisation::real_world_vectors(
        shape,
        vectors,
        true,
        orientation.heading_degrees * kDegreesToRadians,
        orientation.pitch_degrees * kDegreesToRadians,
        orientation.roll_degrees * kDegreesToRadians);
}

std::vector<double> earth_bearings(
    const std::vector<localisation::WorldVector>& vectors) {
    std::vector<double> result;
    result.reserve(vectors.size());
    for (const auto& vector : vectors) {
        const double east = vector.direction[0];
        const double north = vector.direction[1];
        if (!std::isfinite(east) || !std::isfinite(north) ||
            std::hypot(east, north) <= 0.0) {
            continue;
        }
        double bearing = std::atan2(east, north);
        if (bearing < 0.0) {
            bearing += 2.0 * std::numbers::pi;
        }
        result.push_back(bearing);
    }
    return result;
}

void attach_pair_bearings(
    std::vector<localisation::ChannelPairDelay>& delays,
    const std::vector<PairGeometry>& geometry,
    const ArrayConfiguration& array,
    double sample_rate_hz) {
    if (delays.size() != geometry.size()) {
        return;
    }
    for (std::size_t index = 0; index < delays.size(); ++index) {
        auto& delay = delays[index];
        const auto& pair = geometry[index];
        delay.audio_channel_a = pair.channel_a;
        delay.audio_channel_b = pair.channel_b;
        delay.geometry_constrained = pair.constrained;
        delay.max_delay_samples = pair.max_delay_samples;
        delay.hydrophone_distance_m = pair.distance_m;
        if (!pair.constrained || pair.distance_m <= 0.0) {
            continue;
        }
        const auto* first =
            find_hydrophone(array, pair.channel_a);
        const auto* second =
            find_hydrophone(array, pair.channel_b);
        if (!first || !second) {
            continue;
        }
        const auto axes = localisation::array_directions({
            {first->x_m, first->y_m, first->z_m},
            {second->x_m, second->y_m, second->z_m},
        });
        double spacing = pair.distance_m;
        if (!axes.empty()) {
            const double dot =
                pair.baseline_x_m * axes[0][0] +
                pair.baseline_y_m * axes[0][1] +
                pair.baseline_z_m * axes[0][2];
            if (dot > 0.0) {
                spacing = -spacing;
            }
        }
        localisation::PairBearingConfig config;
        config.spacing_m = spacing;
        config.spacing_error_m = array.spacing_error_m;
        config.speed_of_sound_mps = array.speed_of_sound_mps;
        config.speed_of_sound_error_mps =
            array.speed_of_sound_error_mps;
        config.timing_error_seconds = array.timing_error_seconds;
        config.wobble_radians = array.wobble_radians;
        const localisation::PairBearingLocaliser localiser(config);
        const auto result = localiser.localise({
            delay.delay.delay_samples / sample_rate_hz,
        });
        if (!result) {
            continue;
        }
        delay.pair_bearing_valid = true;
        delay.pair_bearing_radians = result->angle_radians;
        delay.pair_bearing_error_radians = result->error_radians;
        delay.pair_bearing_world_vectors =
            localisation::world_vectors(
                localisation::ArrayShapeType::Line,
                axes,
                {result->angle_radians});
        delay.pair_bearing_earth_world_vectors =
            earth_frame_vectors(
                array.orientation,
                localisation::ArrayShapeType::Line,
                delay.pair_bearing_world_vectors);
    }
}

void attach_lsq_bearing(
    ClickLocalisationResult& result,
    const std::vector<PairGeometry>& geometry,
    const std::vector<std::size_t>& channels,
    const ArrayConfiguration& array,
    double sample_rate_hz) {
    if (result.bearing_localiser !=
            localisation::BearingLocaliserChoice::Grid ||
        channels.size() < 4 ||
        array.spacing_error_m <= 0.0 ||
        result.delays.size() != geometry.size()) {
        return;
    }
    localisation::LsqBearingConfig config;
    config.speed_of_sound_mps = array.speed_of_sound_mps;
    config.speed_of_sound_error_mps =
        array.speed_of_sound_error_mps;
    config.timing_error_seconds = array.timing_error_seconds;
    std::vector<double> delays_seconds;
    for (std::size_t index = 0; index < geometry.size(); ++index) {
        const auto& pair = geometry[index];
        if (!pair.constrained || pair.distance_m <= 0.0) {
            return;
        }
        const double scale =
            array.spacing_error_m / pair.distance_m;
        config.pairs.push_back({
            {
                pair.baseline_x_m,
                pair.baseline_y_m,
                pair.baseline_z_m,
            },
            {
                pair.baseline_x_m * scale,
                pair.baseline_y_m * scale,
                pair.baseline_z_m * scale,
            },
        });
        delays_seconds.push_back(
            result.delays[index].delay.delay_samples /
            sample_rate_hz);
    }
    const localisation::LsqBearingLocaliser localiser(
        std::move(config));
    const auto estimate = localiser.localise(delays_seconds);
    if (!estimate ||
        !std::isfinite(estimate->azimuth_radians) ||
        !std::isfinite(estimate->elevation_radians)) {
        return;
    }
    result.lsq_bearing.valid = true;
    result.lsq_bearing.azimuth_radians =
        estimate->azimuth_radians;
    result.lsq_bearing.elevation_radians =
        estimate->elevation_radians;
    result.lsq_bearing.azimuth_error_radians =
        estimate->azimuth_error_radians;
    result.lsq_bearing.elevation_error_radians =
        estimate->elevation_error_radians;
    result.lsq_bearing.used_pairs = geometry.size();
    result.lsq_bearing.world_vectors = {
        localisation::WorldVector{
            localisation::planar_unit_vector(
                estimate->azimuth_radians,
                estimate->elevation_radians),
            false,
        },
    };
    result.lsq_bearing.earth_world_vectors =
        earth_frame_vectors(
            array.orientation,
            result.array_shape,
            result.lsq_bearing.world_vectors);
}

void attach_grid_bearing(
    ClickLocalisationResult& result,
    const std::vector<std::size_t>& channels,
    const ArrayConfiguration& array,
    double sample_rate_hz) {
    if (result.bearing_localiser !=
            localisation::BearingLocaliserChoice::Grid) {
        return;
    }
    localisation::MlGridBearingConfig config;
    config.speed_of_sound_mps = array.speed_of_sound_mps;
    config.speed_of_sound_error_mps =
        array.speed_of_sound_error_mps;
    config.timing_error_seconds = array.timing_error_seconds;
    std::vector<std::array<double, 3>> positions;
    std::vector<int> streamer_ids;
    for (const auto channel : channels) {
        const auto* hydrophone = find_hydrophone(array, channel);
        if (!hydrophone) {
            return;
        }
        config.hydrophones.push_back({
            {
                hydrophone->x_m,
                hydrophone->y_m,
                hydrophone->z_m,
            },
            {
                hydrophone->x_error_m,
                hydrophone->y_error_m,
                hydrophone->z_error_m,
            },
            hydrophone->streamer_id,
        });
        positions.push_back({
            hydrophone->x_m,
            hydrophone->y_m,
            hydrophone->z_m,
        });
        streamer_ids.push_back(hydrophone->streamer_id);
    }
    std::vector<double> delays_seconds;
    for (const auto& delay : result.delays) {
        delays_seconds.push_back(
            delay.delay.delay_samples / sample_rate_hz);
    }
    const localisation::MlGridBearingLocaliser localiser(
        std::move(config));
    const auto estimate = localiser.localise(delays_seconds);
    if (!estimate ||
        !std::isfinite(estimate->theta_radians)) {
        return;
    }
    result.grid_bearing.valid = true;
    result.grid_bearing.theta_radians =
        estimate->theta_radians;
    result.grid_bearing.phi_radians =
        estimate->phi_radians;
    result.grid_bearing.theta_error_radians =
        estimate->theta_error_radians;
    result.grid_bearing.phi_error_radians =
        estimate->phi_error_radians;
    result.grid_bearing.has_phi = estimate->has_phi;
    result.grid_bearing.used_pairs = delays_seconds.size();
    std::vector<double> angles{estimate->theta_radians};
    if (estimate->has_phi) {
        angles.push_back(estimate->phi_radians);
    }
    result.grid_bearing.world_vectors =
        localisation::world_vectors(
            localiser.array_type(),
            localisation::array_directions(
                positions,
                streamer_ids),
            angles);
    result.grid_bearing.earth_world_vectors =
        earth_frame_vectors(
            array.orientation,
            localiser.array_type(),
            result.grid_bearing.world_vectors);
}

} // namespace

ClickLocaliserNode::ClickLocaliserNode(
    std::string instance_id,
    double sample_rate_hz,
    ClickLocaliserNodeConfig config,
    std::shared_ptr<DataBlock> input,
    ClickLocaliserNodeOutputs outputs)
    : instance_id_(std::move(instance_id)),
      sample_rate_hz_(sample_rate_hz),
      config_(std::move(config)),
      input_(std::move(input)),
      outputs_(std::move(outputs)) {
    if (instance_id_.empty() || !input_ ||
        !outputs_.accepted_clicks ||
        !outputs_.localisations || !outputs_.bearings) {
        throw std::invalid_argument(
            "Click localiser requires an instance id and data blocks");
    }
}

ClickLocaliserNode::~ClickLocaliserNode() { stop(); }

const std::string& ClickLocaliserNode::instance_id() const noexcept {
    return instance_id_;
}

ModuleState ClickLocaliserNode::state() const noexcept {
    return state_;
}

void ClickLocaliserNode::prepare() {
    if (sample_rate_hz_ <= 0.0 ||
        input_->descriptor().data_type != kClickDataType ||
        outputs_.accepted_clicks->descriptor().data_type !=
            kClickDataType ||
        outputs_.localisations->descriptor().data_type !=
            kClickLocalisationDataType ||
        outputs_.bearings->descriptor().data_type !=
            kClickBearingDataType) {
        throw std::invalid_argument(
            "Click localiser block types or sample rate are invalid");
    }
    std::vector<localisation::HydrophonePosition> hydrophones;
    hydrophones.reserve(config_.array.hydrophones.size());
    for (const auto& hydrophone : config_.array.hydrophones) {
        hydrophones.push_back({
            hydrophone.channel,
            hydrophone.x_m,
            hydrophone.y_m,
            hydrophone.z_m,
        });
    }
    bearing_localiser_ =
        std::make_unique<localisation::FarFieldBearingLocaliser>(
            localisation::FarFieldBearingConfig{
                sample_rate_hz_,
                config_.array.speed_of_sound_mps,
                std::move(hydrophones),
            });
    next_uid_ = 1;
    state_ = ModuleState::Prepared;
}

void ClickLocaliserNode::start() {
    if (state_ != ModuleState::Prepared &&
        state_ != ModuleState::Stopped) {
        throw std::logic_error(
            "Click localiser must be prepared before it starts");
    }
    subscription_ = input_->subscribe(
        [this](const DataUnit& unit) { process(unit); });
    state_ = ModuleState::Running;
}

void ClickLocaliserNode::stop() {
    subscription_.cancel();
    if (state_ == ModuleState::Running) {
        state_ = ModuleState::Stopped;
    }
}

void ClickLocaliserNode::reset() {
    stop();
    bearing_localiser_.reset();
    next_uid_ = 1;
    state_ = ModuleState::Created;
}

void ClickLocaliserNode::process(const DataUnit& unit) {
    const auto* click =
        std::any_cast<detectors::ClickDetectionResult>(&unit.payload);
    if (click == nullptr) {
        throw std::invalid_argument(
            "Click localiser input payload is not a click");
    }
    auto accepted_click = *click;
    accepted_click.delays_in_samples.clear();
    accepted_click.earth_bearing_ambiguities_radians.clear();
    auto effective_config = config_;
    if (click->orientation_declared) {
        effective_config.array.orientation.declared = true;
        effective_config.array.orientation.heading_degrees =
            click->orientation_heading_degrees;
        effective_config.array.orientation.pitch_degrees =
            click->orientation_pitch_degrees;
        effective_config.array.orientation.roll_degrees =
            click->orientation_roll_degrees;
    }
    std::optional<ClickLocalisationResult> localisation_result;
    std::optional<ClickBearingResult> bearing_result;
    double legacy_angle_degrees = 0.0;

    if (click->waveform.size() >= 2 &&
        click->channels.size() == click->waveform.size()) {
        const auto geometry =
            geometry_for(
                effective_config,
                click->channels,
                sample_rate_hz_);
        std::vector<double> max_delays;
        if (std::all_of(
                geometry.begin(),
                geometry.end(),
                [](const auto& pair) { return pair.constrained; })) {
            max_delays.reserve(geometry.size());
            for (const auto& pair : geometry) {
                max_delays.push_back(pair.max_delay_samples);
            }
        }
        auto delay_config = config_.delay_measurement;
        if (click->click_type > 0) {
            const auto override_it =
                config_.delay_measurement_by_type.find(
                    click->click_type);
            if (override_it !=
                config_.delay_measurement_by_type.end()) {
                delay_config = override_it->second;
            }
        }
        if (delay_config.use_leading_edge && !max_delays.empty()) {
            const auto largest =
                *std::max_element(max_delays.begin(), max_delays.end());
            delay_config.leading_edge_search_start =
                static_cast<int>(config_.pre_sample) -
                static_cast<int>(largest);
            delay_config.leading_edge_search_end =
                static_cast<int>(config_.pre_sample) +
                static_cast<int>(largest);
        }

        ClickLocalisationResult localisation;
        localisation.click_start_sample = click->start_sample;
        localisation.array_shape =
            sub_array_shape(
                effective_config.array,
                click->channels);
        localisation.bearing_localiser =
            localisation::select_bearing_localiser(
                localisation.array_shape);
        localisation.delays = delay_estimator_.estimate_delays(
            click->waveform,
            max_delays,
            sample_rate_hz_,
            delay_config);
        accepted_click.delays_in_samples.reserve(
            localisation.delays.size());
        for (const auto& delay : localisation.delays) {
            accepted_click.delays_in_samples.push_back(
                delay.delay.delay_samples);
        }

        // Java ClickDetection.getAngle() exposes zero when no bearing is
        // available. For an ordinary two-channel click its first bearing is
        // the constrained first-pair acos(delay / maximum-delay) value used
        // by angleVetoes.AngleVetoes.
        if (!localisation.delays.empty() &&
            !geometry.empty() &&
            geometry.front().constrained &&
            geometry.front().max_delay_samples > 0.0) {
            const double cosine = std::clamp(
                localisation.delays.front().delay.delay_samples /
                    geometry.front().max_delay_samples,
                -1.0,
                1.0);
            legacy_angle_degrees =
                std::acos(cosine) * 180.0 / std::numbers::pi;
        }

        attach_pair_bearings(
            localisation.delays,
            geometry,
            effective_config.array,
            sample_rate_hz_);
        attach_lsq_bearing(
            localisation,
            geometry,
            click->channels,
            effective_config.array,
            sample_rate_hz_);
        attach_grid_bearing(
            localisation,
            click->channels,
            effective_config.array,
            sample_rate_hz_);

        if (!localisation.grid_bearing.
                 earth_world_vectors.empty()) {
            accepted_click.
                earth_bearing_ambiguities_radians =
                    earth_bearings(
                        localisation.grid_bearing.
                            earth_world_vectors);
        }
        else if (!localisation.lsq_bearing.
                      earth_world_vectors.empty()) {
            accepted_click.
                earth_bearing_ambiguities_radians =
                    earth_bearings(
                        localisation.lsq_bearing.
                            earth_world_vectors);
        }
        else {
            const auto pair = std::find_if(
                localisation.delays.begin(),
                localisation.delays.end(),
                [](const auto& delay) {
                    return !delay.
                        pair_bearing_earth_world_vectors.empty();
                });
            if (pair != localisation.delays.end()) {
                accepted_click.
                    earth_bearing_ambiguities_radians =
                        earth_bearings(
                            pair->
                                pair_bearing_earth_world_vectors);
            }
        }

        if (bearing_localiser_) {
            ClickBearingResult bearing;
            bearing.click_start_sample = click->start_sample;
            bearing.bearing = bearing_localiser_->estimate(
                localisation.delays,
                click->channels,
                0,
                click->start_sample);
            bearing_result = std::move(bearing);
        }
        localisation_result = std::move(localisation);
    }

    accepted_click.bearing_radians =
        legacy_angle_degrees * std::numbers::pi / 180.0;
    if (!detectors::ClickAngleVetoes::pass_all(
            config_.angle_vetoes,
            legacy_angle_degrees)) {
        return;
    }

    if (localisation_result.has_value()) {
        auto metadata = unit.metadata;
        metadata.uid = next_uid_;
        metadata.sequence = next_uid_++;
        metadata.type_id.clear();
        metadata.source_block_id.clear();
        outputs_.localisations->publish(make_data_unit(
            std::move(metadata),
            std::move(*localisation_result)));
    }
    if (bearing_result.has_value()) {
        auto metadata = unit.metadata;
        metadata.uid = next_uid_;
        metadata.sequence = next_uid_++;
        metadata.type_id.clear();
        metadata.source_block_id.clear();
        outputs_.bearings->publish(make_data_unit(
            std::move(metadata),
            std::move(*bearing_result)));
    }

    auto metadata = unit.metadata;
    metadata.type_id.clear();
    metadata.source_block_id.clear();
    outputs_.accepted_clicks->publish(make_data_unit(
        std::move(metadata),
        std::move(accepted_click)));
}

} // namespace pamguard::core
