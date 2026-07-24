#include "pamguard/core/AnalysisSession.h"
#include "pamguard/core/GroupedSource.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <iterator>
#include <numbers>
#include <unordered_set>
#include <utility>

#include "pamguard/detectors/CtTrainSpectrum.h"
#include "pamguard/detectors/ClickAngleVeto.h"
#include "pamguard/detectors/NoiseBandMonitor.h"
#include "pamguard/detectors/SimpleEchoDetector.h"
#include "pamguard/detectors/StandardMhtChi2.h"
#include "pamguard/detectors/WhistleDetectionGrouper.h"
#include "pamguard/localisation/ArrayShape.h"
#include "pamguard/localisation/BearingLocaliserSelector.h"
#include "pamguard/localisation/LsqBearingLocaliser.h"
#include "pamguard/localisation/MlGridBearingLocaliser.h"
#include "pamguard/localisation/PairBearingLocaliser.h"
#include "pamguard/localisation/StreamerOrientation.h"
#include "pamguard/localisation/WhistleDelayEstimator.h"

namespace pamguard::core {

namespace {

std::vector<double> pamguard_packed_magnitude_squared(const dsp::ComplexSpectrum& bins) {
    if (bins.size() < 2) {
        return {};
    }
    const auto fft_length = (bins.size() - 1) * 2;
    std::vector<double> magsq(fft_length / 2, 0.0);
    magsq[0] = bins[0].real() * bins[0].real() + bins[fft_length / 2].real() * bins[fft_length / 2].real();
    for (std::size_t i = 1; i < magsq.size(); ++i) {
        magsq[i] = std::norm(bins[i]);
    }
    return magsq;
}

const ArrayHydrophone* find_hydrophone(const ArrayConfiguration& array, std::size_t channel) {
    const auto found = std::find_if(array.hydrophones.begin(), array.hydrophones.end(), [&](const auto& hydrophone) {
        return hydrophone.channel == channel;
    });
    return found == array.hydrophones.end() ? nullptr : &(*found);
}

double hydrophone_distance_m(const ArrayHydrophone& a, const ArrayHydrophone& b) {
    const double dx = b.x_m - a.x_m;
    const double dy = b.y_m - a.y_m;
    const double dz = b.z_m - a.z_m;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

struct ClickPairGeometry {
    std::size_t audio_channel_a = 0;
    std::size_t audio_channel_b = 0;
    bool constrained = false;
    double max_delay_samples = 0.0;
    double hydrophone_distance_m = 0.0;
    double baseline_x_m = 0.0;
    double baseline_y_m = 0.0;
    double baseline_z_m = 0.0;
};

bool can_constrain_click_geometry(const AnalysisConfig& config, const std::vector<std::size_t>& click_channels) {
    if (click_channels.size() < 2 || config.array.hydrophones.size() < 2 || config.array.speed_of_sound_mps <= 0.0 || config.sample_rate_hz == 0) {
        return false;
    }
    for (const auto channel : click_channels) {
        if (find_hydrophone(config.array, channel) == nullptr) {
            return false;
        }
    }
    return true;
}

std::vector<ClickPairGeometry> click_pair_geometry(const AnalysisConfig& config, const std::vector<std::size_t>& click_channels) {
    if (click_channels.size() < 2) {
        return {};
    }

    const bool constrain_geometry = can_constrain_click_geometry(config, click_channels);
    std::vector<ClickPairGeometry> geometry;
    geometry.reserve((click_channels.size() - 1) * click_channels.size() / 2);
    for (std::size_t i = 0; i < click_channels.size(); ++i) {
        for (std::size_t j = i + 1; j < click_channels.size(); ++j) {
            ClickPairGeometry pair;
            pair.audio_channel_a = click_channels[i];
            pair.audio_channel_b = click_channels[j];
            if (constrain_geometry) {
                const auto* hydrophone_a = find_hydrophone(config.array, click_channels[i]);
                const auto* hydrophone_b = find_hydrophone(config.array, click_channels[j]);
                if (hydrophone_a != nullptr && hydrophone_b != nullptr) {
                    const double distance_m = hydrophone_distance_m(*hydrophone_a, *hydrophone_b);
                    const double seconds = distance_m / config.array.speed_of_sound_mps;
                    pair.constrained = true;
                    pair.max_delay_samples = std::ceil(seconds * static_cast<double>(config.sample_rate_hz)) + 1.0;
                    pair.hydrophone_distance_m = distance_m;
                    pair.baseline_x_m = hydrophone_b->x_m - hydrophone_a->x_m;
                    pair.baseline_y_m = hydrophone_b->y_m - hydrophone_a->y_m;
                    pair.baseline_z_m = hydrophone_b->z_m - hydrophone_a->z_m;
                }
            }
            geometry.push_back(pair);
        }
    }
    return geometry;
}

/**
 * BearingLocaliserSelector.createBearingLocaliser is called on the hydrophone
 * subset taking part in one localisation, so the shape that matters is the
 * sub-array's, not the whole array's. A channel with no declared hydrophone
 * leaves the shape unknown, which is `None` — the same answer PAMGuard gives
 * for a null array, and one that selects no localiser.
 */
localisation::ArrayShapeType sub_array_shape(const AnalysisConfig& config,
                                             const std::vector<std::size_t>& channels) {
    std::vector<std::array<double, 3>> positions;
    std::vector<int> streamer_ids;
    positions.reserve(channels.size());
    streamer_ids.reserve(channels.size());
    for (const auto channel : channels) {
        const auto* hydrophone = find_hydrophone(config.array, channel);
        if (hydrophone == nullptr) {
            return localisation::ArrayShapeType::None;
        }
        positions.push_back({hydrophone->x_m, hydrophone->y_m, hydrophone->z_m});
        streamer_ids.push_back(hydrophone->streamer_id);
    }
    return localisation::array_shape(positions, streamer_ids);
}

std::vector<double> max_delay_samples_from_geometry(const std::vector<ClickPairGeometry>& geometry) {
    if (geometry.empty() || std::any_of(geometry.begin(), geometry.end(), [](const auto& pair) { return !pair.constrained; })) {
        return {};
    }
    std::vector<double> max_delays;
    max_delays.reserve(geometry.size());
    for (const auto& pair : geometry) {
        max_delays.push_back(pair.max_delay_samples);
    }
    return max_delays;
}

/**
 * AbstractLocalisation.getRealWorldVectors over the session's declared array
 * orientation. With none declared the reference returns the array-frame
 * vectors unrotated; the engine reports nothing instead, so a consumer cannot
 * mistake array-frame numbers for earth-frame ones.
 */
std::vector<localisation::WorldVector> earth_frame_vectors(
    const ArrayOrientation& orientation, localisation::ArrayShapeType shape,
    const std::vector<localisation::WorldVector>& array_frame_vectors) {
    if (!orientation.declared || array_frame_vectors.empty()) {
        return {};
    }
    constexpr double kDegreesToRadians = std::numbers::pi / 180.0;
    return localisation::real_world_vectors(shape, array_frame_vectors, true,
                                            orientation.heading_degrees * kDegreesToRadians,
                                            orientation.pitch_degrees * kDegreesToRadians,
                                            orientation.roll_degrees * kDegreesToRadians);
}

void attach_pair_geometry(std::vector<localisation::ChannelPairDelay>& delays, const std::vector<ClickPairGeometry>& geometry, const AnalysisConfig& config,
                          const ArrayOrientation& orientation) {
    if (delays.size() != geometry.size()) {
        return;
    }
    for (std::size_t i = 0; i < delays.size(); ++i) {
        delays[i].audio_channel_a = geometry[i].audio_channel_a;
        delays[i].audio_channel_b = geometry[i].audio_channel_b;
        delays[i].geometry_constrained = geometry[i].constrained;
        delays[i].max_delay_samples = geometry[i].max_delay_samples;
        delays[i].hydrophone_distance_m = geometry[i].hydrophone_distance_m;
        if (geometry[i].constrained && geometry[i].hydrophone_distance_m > 0.0 && config.sample_rate_hz != 0) {
            // PAMGuard PairBearingLocaliser.prepare() negates the spacing when
            // the pair vector points along the pair's principal array axis
            // (ArrayManager.getArrayDirections over the pair's two phones).
            double spacing_m = geometry[i].hydrophone_distance_m;
            std::vector<std::array<double, 3>> pair_axes;
            const auto* hydrophone_a = find_hydrophone(config.array, geometry[i].audio_channel_a);
            const auto* hydrophone_b = find_hydrophone(config.array, geometry[i].audio_channel_b);
            if (hydrophone_a != nullptr && hydrophone_b != nullptr) {
                pair_axes = localisation::array_directions({
                    {hydrophone_a->x_m, hydrophone_a->y_m, hydrophone_a->z_m},
                    {hydrophone_b->x_m, hydrophone_b->y_m, hydrophone_b->z_m},
                });
                if (!pair_axes.empty()) {
                    const double axis_dot = geometry[i].baseline_x_m * pair_axes[0][0] +
                        geometry[i].baseline_y_m * pair_axes[0][1] +
                        geometry[i].baseline_z_m * pair_axes[0][2];
                    if (axis_dot > 0.0) {
                        spacing_m = -spacing_m;
                    }
                }
            }
            localisation::PairBearingConfig pair_config;
            pair_config.spacing_m = spacing_m;
            pair_config.spacing_error_m = config.array.spacing_error_m;
            pair_config.speed_of_sound_mps = config.array.speed_of_sound_mps;
            pair_config.speed_of_sound_error_mps = config.array.speed_of_sound_error_mps;
            pair_config.timing_error_seconds = config.array.timing_error_seconds;
            pair_config.wobble_radians = config.array.wobble_radians;
            const localisation::PairBearingLocaliser pair_localiser(pair_config);
            const double delay_seconds = delays[i].delay.delay_samples / static_cast<double>(config.sample_rate_hz);
            if (const auto pair_bearing = pair_localiser.localise({delay_seconds})) {
                delays[i].pair_bearing_valid = true;
                delays[i].pair_bearing_radians = pair_bearing->angle_radians;
                delays[i].pair_bearing_error_radians = pair_bearing->error_radians;
                // The pair's cone angle is measured against its own principal
                // axis, so it goes through getWorldVectors' line branch with
                // that axis — giving the left/right pair explicitly.
                delays[i].pair_bearing_world_vectors = localisation::world_vectors(
                    localisation::ArrayShapeType::Line, pair_axes, {pair_bearing->angle_radians});
                delays[i].pair_bearing_earth_world_vectors = earth_frame_vectors(
                    orientation, localisation::ArrayShapeType::Line, delays[i].pair_bearing_world_vectors);
            }
        }
    }
}

void attach_lsq_bearing(ClickLocalisationResult& localisation, const std::vector<ClickPairGeometry>& geometry,
                        std::size_t click_channel_count, const AnalysisConfig& config,
                        const ArrayOrientation& orientation) {
    // The selector decides whether a grid-class localiser applies at all;
    // PAMGuard reserves those for plane and volume sub-arrays and gives a line
    // array only pair bearings.
    if (localisation.bearing_localiser != localisation::BearingLocaliserChoice::Grid) {
        return;
    }
    // PAMGuard's LSQ bearing localiser needs at least four hydrophones (any
    // three-hydrophone pair set is rank deficient) and nonzero separation
    // errors for its fit weights, so LSQ output requires spacingErrorM > 0.
    if (click_channel_count < 4 || config.sample_rate_hz == 0 || config.array.spacing_error_m <= 0.0 ||
        localisation.delays.size() != geometry.size() || geometry.empty()) {
        return;
    }

    localisation::LsqBearingConfig lsq_config;
    lsq_config.speed_of_sound_mps = config.array.speed_of_sound_mps;
    lsq_config.speed_of_sound_error_mps = config.array.speed_of_sound_error_mps;
    lsq_config.timing_error_seconds = config.array.timing_error_seconds;
    std::vector<double> delays_seconds;
    delays_seconds.reserve(geometry.size());
    for (std::size_t i = 0; i < geometry.size(); ++i) {
        const auto& pair = geometry[i];
        if (!pair.constrained || pair.hydrophone_distance_m <= 0.0) {
            return;
        }
        localisation::LsqPairGeometry lsq_pair;
        lsq_pair.baseline_m = {pair.baseline_x_m, pair.baseline_y_m, pair.baseline_z_m};
        const double error_scale = config.array.spacing_error_m / pair.hydrophone_distance_m;
        lsq_pair.error_m = {pair.baseline_x_m * error_scale, pair.baseline_y_m * error_scale, pair.baseline_z_m * error_scale};
        lsq_config.pairs.push_back(lsq_pair);
        delays_seconds.push_back(localisation.delays[i].delay.delay_samples / static_cast<double>(config.sample_rate_hz));
    }

    const localisation::LsqBearingLocaliser lsq_localiser(std::move(lsq_config));
    const auto lsq = lsq_localiser.localise(delays_seconds);
    if (lsq.has_value() && std::isfinite(lsq->azimuth_radians) && std::isfinite(lsq->elevation_radians)) {
        localisation.lsq_bearing.valid = true;
        localisation.lsq_bearing.azimuth_radians = lsq->azimuth_radians;
        localisation.lsq_bearing.elevation_radians = lsq->elevation_radians;
        localisation.lsq_bearing.azimuth_error_radians = lsq->azimuth_error_radians;
        localisation.lsq_bearing.elevation_error_radians = lsq->elevation_error_radians;
        localisation.lsq_bearing.used_pairs = geometry.size();
        // No array-axis rotation: LSQ fits raw inter-hydrophone vectors, so its
        // angles are already in the array's xyz frame.
        localisation.lsq_bearing.world_vectors = {localisation::WorldVector{
            localisation::planar_unit_vector(lsq->azimuth_radians, lsq->elevation_radians), false}};
        localisation.lsq_bearing.earth_world_vectors = earth_frame_vectors(
            orientation, localisation.array_shape, localisation.lsq_bearing.world_vectors);
    }
}

/**
 * Run PAMGuard's own choice of localiser for a plane or volume sub-array.
 * The grid table is built from the participating hydrophones' absolute
 * positions and per-axis position errors, which is what
 * MLGridBearingLocaliser2's `prepare` reads off the array.
 */
GridBearingResult grid_bearing_for_channels(const AnalysisConfig& config,
                                            const std::vector<std::size_t>& channels,
                                            localisation::BearingLocaliserChoice choice,
                                            const std::vector<double>& delays_seconds,
                                            const ArrayOrientation& orientation) {
    GridBearingResult result;
    if (choice != localisation::BearingLocaliserChoice::Grid || config.sample_rate_hz == 0) {
        return result;
    }
    const auto expected_pairs = channels.size() * (channels.size() - 1) / 2;
    if (delays_seconds.size() != expected_pairs || expected_pairs == 0) {
        return result;
    }

    localisation::MlGridBearingConfig grid_config;
    grid_config.speed_of_sound_mps = config.array.speed_of_sound_mps;
    grid_config.speed_of_sound_error_mps = config.array.speed_of_sound_error_mps;
    grid_config.timing_error_seconds = config.array.timing_error_seconds;
    for (const auto channel : channels) {
        const auto* hydrophone = find_hydrophone(config.array, channel);
        if (hydrophone == nullptr) {
            return result;
        }
        localisation::MlGridHydrophone grid_hydrophone;
        grid_hydrophone.position_m = {hydrophone->x_m, hydrophone->y_m, hydrophone->z_m};
        grid_hydrophone.position_error_m = {hydrophone->x_error_m, hydrophone->y_error_m, hydrophone->z_error_m};
        grid_hydrophone.streamer_id = hydrophone->streamer_id;
        grid_config.hydrophones.push_back(grid_hydrophone);
    }

    const localisation::MlGridBearingLocaliser localiser(std::move(grid_config));
    const auto grid = localiser.localise(delays_seconds);
    if (!grid.has_value() || !std::isfinite(grid->theta_radians)) {
        return result;
    }
    result.valid = true;
    result.theta_radians = grid->theta_radians;
    result.theta_error_radians = grid->theta_error_radians;
    result.phi_radians = grid->phi_radians;
    result.phi_error_radians = grid->phi_error_radians;
    result.has_phi = grid->has_phi;
    result.used_pairs = expected_pairs;

    // Express the same direction in the array's xyz frame, which is what makes
    // theta and phi interpretable without an external orientation reference.
    std::vector<std::array<double, 3>> positions;
    std::vector<int> streamer_ids;
    positions.reserve(channels.size());
    streamer_ids.reserve(channels.size());
    for (const auto channel : channels) {
        const auto* hydrophone = find_hydrophone(config.array, channel);
        positions.push_back({hydrophone->x_m, hydrophone->y_m, hydrophone->z_m});
        streamer_ids.push_back(hydrophone->streamer_id);
    }
    std::vector<double> angles{grid->theta_radians};
    if (grid->has_phi) {
        angles.push_back(grid->phi_radians);
    }
    result.world_vectors = localisation::world_vectors(
        localiser.array_type(), localisation::array_directions(positions, streamer_ids), angles);
    result.earth_world_vectors = earth_frame_vectors(orientation, localiser.array_type(), result.world_vectors);
    return result;
}

double degrees_from_radians(double radians) {
    return radians * 180.0 / 3.141592653589793238462643383279502884;
}

bool train_contains_click(const detectors::ClickTrainSummary& train, std::int64_t click_start_sample) {
    return std::find(train.click_start_samples.begin(), train.click_start_samples.end(), click_start_sample) != train.click_start_samples.end();
}

} // namespace

std::vector<ClickTrainBearingSummary> summarize_click_train_bearings(
    const std::vector<detectors::ClickTrainSummary>& trains,
    const std::vector<ClickBearingResult>& bearings) {
    std::vector<ClickTrainBearingSummary> summaries;
    summaries.reserve(trains.size());

    for (const auto& train : trains) {
        ClickTrainBearingSummary summary;
        summary.train_id = train.train_id;
        summary.channel_bitmap = train.channel_bitmap;
        summary.first_start_sample = train.first_start_sample;
        summary.last_start_sample = train.last_start_sample;
        summary.click_count = train.click_count;

        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        double residual_sum = 0.0;
        for (const auto& bearing : bearings) {
            if (!bearing.bearing.valid || !train_contains_click(train, bearing.click_start_sample)) {
                continue;
            }
            x += bearing.bearing.unit_x;
            y += bearing.bearing.unit_y;
            z += bearing.bearing.unit_z;
            residual_sum += bearing.bearing.residual_rms_seconds;
            summary.bearing_count += 1;
        }

        if (summary.bearing_count > 0) {
            const double length = std::sqrt(x * x + y * y + z * z);
            if (length > 0.0) {
                summary.valid = true;
                summary.unit_x = x / length;
                summary.unit_y = y / length;
                summary.unit_z = z / length;
                summary.azimuth_degrees = degrees_from_radians(std::atan2(summary.unit_y, summary.unit_x));
                summary.elevation_degrees = degrees_from_radians(std::asin(std::clamp(summary.unit_z, -1.0, 1.0)));
                summary.mean_residual_rms_seconds = residual_sum / static_cast<double>(summary.bearing_count);
            }
        }

        summaries.push_back(summary);
    }

    return summaries;
}

std::vector<ClickTrainLocalisationSummary> summarize_click_train_localisations(
    const std::vector<detectors::ClickTrainSummary>& trains,
    const std::vector<ClickLocalisationResult>& localisations) {
    std::vector<ClickTrainLocalisationSummary> summaries;
    summaries.reserve(trains.size());

    for (const auto& train : trains) {
        ClickTrainLocalisationSummary summary;
        summary.train_id = train.train_id;
        summary.channel_bitmap = train.channel_bitmap;
        summary.first_start_sample = train.first_start_sample;
        summary.last_start_sample = train.last_start_sample;
        summary.click_count = train.click_count;

        for (const auto& localisation : localisations) {
            if (!train_contains_click(train, localisation.click_start_sample)) {
                continue;
            }
            summary.localisation_count += 1;
            for (const auto& delay : localisation.delays) {
                auto found = std::find_if(summary.pair_delays.begin(), summary.pair_delays.end(), [&](const ClickTrainPairDelaySummary& pair) {
                    return pair.pair_index == delay.pair_index &&
                        pair.channel_a == delay.channel_a &&
                        pair.channel_b == delay.channel_b &&
                        pair.audio_channel_a == delay.audio_channel_a &&
                        pair.audio_channel_b == delay.audio_channel_b;
                });
                if (found == summary.pair_delays.end()) {
                    ClickTrainPairDelaySummary pair;
                    pair.pair_index = delay.pair_index;
                    pair.channel_a = delay.channel_a;
                    pair.channel_b = delay.channel_b;
                    pair.audio_channel_a = delay.audio_channel_a;
                    pair.audio_channel_b = delay.audio_channel_b;
                    pair.geometry_constrained = delay.geometry_constrained;
                    pair.max_delay_samples = delay.max_delay_samples;
                    pair.hydrophone_distance_m = delay.hydrophone_distance_m;
                    pair.delay_count = 1;
                    pair.mean_delay_samples = delay.delay.delay_samples;
                    pair.mean_delay_score = delay.delay.delay_score;
                    if (delay.pair_bearing_valid && std::isfinite(delay.pair_bearing_radians)) {
                        pair.pair_bearing_count = 1;
                        pair.mean_pair_bearing_radians = delay.pair_bearing_radians;
                    }
                    summary.pair_delays.push_back(pair);
                }
                else {
                    if (delay.geometry_constrained && !found->geometry_constrained) {
                        found->geometry_constrained = true;
                        found->max_delay_samples = delay.max_delay_samples;
                        found->hydrophone_distance_m = delay.hydrophone_distance_m;
                    }
                    found->delay_count += 1;
                    found->mean_delay_samples += delay.delay.delay_samples;
                    found->mean_delay_score += delay.delay.delay_score;
                    if (delay.pair_bearing_valid && std::isfinite(delay.pair_bearing_radians)) {
                        found->pair_bearing_count += 1;
                        found->mean_pair_bearing_radians += delay.pair_bearing_radians;
                    }
                }
            }
        }

        for (auto& pair : summary.pair_delays) {
            if (pair.delay_count > 0) {
                pair.mean_delay_samples /= static_cast<double>(pair.delay_count);
                pair.mean_delay_score /= static_cast<double>(pair.delay_count);
            }
            if (pair.pair_bearing_count > 0) {
                pair.mean_pair_bearing_radians /= static_cast<double>(pair.pair_bearing_count);
            }
        }
        summary.valid = summary.localisation_count > 0 && !summary.pair_delays.empty();
        summaries.push_back(std::move(summary));
    }

    return summaries;
}

namespace {

/**
 * HydrophoneLocator.getPhoneLatLong: a hydrophone's absolute position is its
 * own coordinates rotated by its streamer's heading/pitch/roll quaternion and
 * then offset by the streamer position. With all-zero angles this reduces to
 * PamArray.getAbsHydrophoneVector, which performs the translation alone.
 * Resolving once here means every downstream consumer (delay limits,
 * bearings, array shape) sees absolute positions without needing streamer
 * awareness.
 */
void resolve_streamer_offsets(ArrayConfiguration& array) {
    if (array.streamers.empty()) {
        return;
    }
    constexpr double kDegreesToRadians = std::numbers::pi / 180.0;
    for (auto& hydrophone : array.hydrophones) {
        for (const auto& streamer : array.streamers) {
            if (streamer.id != hydrophone.streamer_id) {
                continue;
            }
            const auto rotated = localisation::rotate_by_streamer_orientation(
                {hydrophone.x_m, hydrophone.y_m, hydrophone.z_m},
                streamer.heading_degrees * kDegreesToRadians,
                streamer.pitch_degrees * kDegreesToRadians,
                streamer.roll_degrees * kDegreesToRadians);
            hydrophone.x_m = rotated[0] + streamer.x_m;
            hydrophone.y_m = rotated[1] + streamer.y_m;
            hydrophone.z_m = rotated[2] + streamer.z_m;
            break;
        }
    }
}

} // namespace

AnalysisSession::AnalysisSession(AnalysisConfig config)
    : config_(std::move(config)),
      spectrogram_(config_.detector.fft) {
    resolve_streamer_offsets(config_.array);
    current_orientation_ = config_.array.orientation;
    if (config_.detector.noise_band.enabled && config_.sample_rate_hz != 0) {
        for (std::size_t channel = 0; channel < config_.channel_count; ++channel) {
            noise_band_monitors_.emplace(channel,
                                         detectors::NoiseBandMonitor(static_cast<double>(config_.sample_rate_hz),
                                                                     config_.detector.noise_band));
        }
    }
    if (config_.detector.ltsa.enabled && config_.detector.fft.fft_length >= 2) {
        // LtsaProcess.setupProcess: one ChannelProcess per channel in the
        // map; the source is the session's FFT stream, so the channel set
        // follows the FFT config.
        if (config_.detector.fft.channels.empty()) {
            for (std::size_t channel = 0; channel < config_.channel_count; ++channel) {
                ltsa_monitors_.emplace(channel, detectors::LtsaMonitor(config_.detector.ltsa));
            }
        }
        else {
            for (const auto channel : config_.detector.fft.channels) {
                ltsa_monitors_.emplace(channel, detectors::LtsaMonitor(config_.detector.ltsa));
            }
        }
    }
    if (config_.detector.ishmael.enabled && config_.detector.fft.fft_length >= 2 &&
        config_.detector.fft.fft_hop > 0 && config_.sample_rate_hz != 0) {
        ishmael_energy_.emplace(static_cast<double>(config_.sample_rate_hz), config_.detector.ishmael);
        ishmael_picker_.emplace(static_cast<double>(config_.sample_rate_hz),
                                config_.detector.fft.fft_hop, config_.detector.ishmael);
    }
    if (config_.detector.matched_template.enabled && config_.sample_rate_hz != 0) {
        matched_template_classifier_.emplace(static_cast<double>(config_.sample_rate_hz),
                                             config_.detector.matched_template);
        if (!matched_template_classifier_->valid()) {
            throw std::invalid_argument("matchedTemplate: " + matched_template_classifier_->invalid_reason());
        }
    }
    if (config_.detector.sgram_corr.enabled && !config_.detector.sgram_corr.segments.empty() &&
        config_.detector.fft.fft_length >= 2 && config_.detector.fft.fft_hop > 0 &&
        config_.sample_rate_hz != 0) {
        sgram_corr_detector_.emplace(static_cast<double>(config_.sample_rate_hz),
                                     config_.detector.fft.fft_length, config_.detector.fft.fft_hop,
                                     config_.detector.sgram_corr);
        // The detection function feeds the same IshPeakProcess machinery as
        // the energy sum; the reported band is the kernel's span.
        detectors::IshmaelEnergySumConfig picker_config;
        picker_config.thresh = config_.detector.sgram_corr.thresh;
        picker_config.min_time_s = config_.detector.sgram_corr.min_time_s;
        picker_config.max_time_s = config_.detector.sgram_corr.max_time_s;
        picker_config.refractory_time_s = config_.detector.sgram_corr.refractory_time_s;
        picker_config.f0 = sgram_corr_detector_->min_frequency_hz();
        picker_config.f1 = sgram_corr_detector_->max_frequency_hz();
        sgram_corr_picker_.emplace(static_cast<double>(config_.sample_rate_hz),
                                   config_.detector.fft.fft_hop, picker_config);
    }
    if (config_.detector.match_filt.enabled && !config_.detector.match_filt.kernel.empty() &&
        config_.sample_rate_hz != 0) {
        match_filt_detector_.emplace(static_cast<double>(config_.sample_rate_hz),
                                     config_.detector.match_filt);
        detectors::IshmaelEnergySumConfig picker_config;
        picker_config.thresh = config_.detector.match_filt.thresh;
        picker_config.min_time_s = config_.detector.match_filt.min_time_s;
        picker_config.max_time_s = config_.detector.match_filt.max_time_s;
        picker_config.refractory_time_s = config_.detector.match_filt.refractory_time_s;
        // getLoFreq/getHiFreq: the full band; detection rate is the AUDIO
        // rate (getDetSampleRate returns getSampleRate), hence hop 1.
        picker_config.f0 = 0.0;
        picker_config.f1 = static_cast<double>(config_.sample_rate_hz) / 2.0;
        match_filt_picker_.emplace(static_cast<double>(config_.sample_rate_hz), 1, picker_config);
    }
    if (config_.detector.fft_noise.enabled && config_.sample_rate_hz != 0) {
        fft_noise_monitor_.emplace(
            static_cast<double>(config_.sample_rate_hz),
            config_.detector.fft.fft_length,
            config_.detector.fft.fft_hop,
            config_.detector.fft_noise);
    }
    if (config_.detector.click_detector_enabled) {
        if (config_.detector.click_groups.empty()) {
            click_detectors_.emplace_back(config_.detector.click);
        }
        else {
            click_detectors_.reserve(config_.detector.click_groups.size());
            for (const auto& group : config_.detector.click_groups) {
                click_detectors_.emplace_back(group);
            }
        }
        click_echo_detectors_.resize(click_detectors_.size());
        if (config_.detector.click_echo_enabled && config_.sample_rate_hz != 0) {
            for (auto& echo : click_echo_detectors_) {
                echo.emplace(static_cast<double>(config_.sample_rate_hz),
                             config_.detector.click_echo_max_interval_seconds);
            }
        }
    }
    if (config_.detector.click_detector_enabled && config_.detector.click_features_enabled) {
        auto click_feature_config = config_.detector.click_features;
        click_feature_config.sample_rate_hz = config_.sample_rate_hz;
        if (click_feature_config.fft_length == 0) {
            click_feature_config.fft_length = config_.detector.fft.fft_length;
        }
        click_feature_extractor_.emplace(std::move(click_feature_config));
    }
    if (config_.detector.click_detector_enabled &&
        config_.detector.click_classify_online &&
        config_.detector.click_classifier_type == DetectorConfig::ClickClassifierType::Basic &&
        config_.detector.click_basic_classifier_enabled) {
        auto click_classifier_config = config_.detector.click_basic_classifier;
        click_classifier_config.sample_rate_hz = config_.sample_rate_hz;
        click_basic_classifier_.emplace(std::move(click_classifier_config));
    }
    if (config_.detector.click_detector_enabled &&
        config_.detector.click_classify_online &&
        config_.detector.click_classifier_type == DetectorConfig::ClickClassifierType::Sweep &&
        config_.detector.click_sweep_classifier_enabled) {
        auto classifier_config = config_.detector.click_sweep_classifier;
        classifier_config.sample_rate_hz = config_.sample_rate_hz;
        classifier_config.amplitude_db_offset_by_channel.assign(config_.channel_count, 0.0);
        const double acquisition_offset =
            20.0 * std::log10(config_.acquisition.volts_peak_to_peak / 2.0) -
            config_.acquisition.preamp_gain_db;
        for (std::size_t channel = 0; channel < config_.channel_count; ++channel) {
            double offset = acquisition_offset;
            const auto hydrophone = std::find_if(
                config_.array.hydrophones.begin(), config_.array.hydrophones.end(),
                [channel](const auto& item) { return item.channel == channel; });
            if (hydrophone != config_.array.hydrophones.end()) {
                offset -= hydrophone->sensitivity_db + hydrophone->preamp_gain_db;
            }
            classifier_config.amplitude_db_offset_by_channel[channel] = offset;
        }
        click_sweep_classifier_.emplace(std::move(classifier_config));
    }
    if (config_.detector.click_detector_enabled && config_.detector.click_train_tracker_enabled &&
        !config_.detector.click_train_mht) {
        auto click_train_config = config_.detector.click_train;
        click_train_config.sample_rate_hz = config_.sample_rate_hz;
        click_train_tracker_.emplace(std::move(click_train_config));
    }
    if (config_.detector.click_localisation_enabled && config_.array.hydrophones.size() >= 2) {
        localisation::FarFieldBearingConfig bearing_config;
        bearing_config.sample_rate_hz = config_.sample_rate_hz;
        bearing_config.speed_of_sound_mps = config_.array.speed_of_sound_mps;
        for (const auto& hydrophone : config_.array.hydrophones) {
            bearing_config.hydrophones.push_back(localisation::HydrophonePosition{
                hydrophone.channel,
                hydrophone.x_m,
                hydrophone.y_m,
                hydrophone.z_m,
            });
        }
        click_bearing_localiser_.emplace(std::move(bearing_config));
    }
    {
        detectors::SpectrogramNoiseReducer reducer(config_.detector.whistle_noise);
        if (reducer.active() &&
            (config_.detector.whistle_peak_detector_enabled || config_.detector.whistle_region_detector_enabled)) {
            whistle_noise_reducer_.emplace(config_.detector.whistle_noise);
        }
    }
    if (config_.detector.whistle_peak_detector_enabled) {
        auto whistle_config = config_.detector.whistle_peak;
        whistle_config.fft_length = config_.detector.fft.fft_length;
        whistle_config.fft_hop = config_.detector.fft.fft_hop;
        whistle_config.sample_rate_hz = config_.sample_rate_hz;

        if (config_.detector.fft.channels.empty()) {
            for (std::size_t channel = 0; channel < config_.channel_count; ++channel) {
                whistle_peak_detectors_.try_emplace(channel, whistle_config);
            }
        }
        else {
            for (const auto channel : config_.detector.fft.channels) {
                whistle_peak_detectors_.try_emplace(channel, whistle_config);
            }
        }
    }
    if (config_.detector.whistle_region_detector_enabled) {
        auto region_config = config_.detector.whistle_region;
        region_config.slice_height = config_.detector.fft.fft_length / 2;
        region_config.sample_rate_hz = config_.sample_rate_hz;

        std::vector<std::size_t> selected_channels;
        if (config_.detector.whistle_channel_bitmap != 0) {
            for (std::size_t channel = 0; channel < config_.channel_count;
                 ++channel) {
                if ((config_.detector.whistle_channel_bitmap &
                     (std::uint32_t{1} << channel)) != 0) {
                    selected_channels.push_back(channel);
                }
            }
        }
        else if (!config_.detector.fft.channels.empty()) {
            selected_channels = config_.detector.fft.channels;
        }
        else {
            for (std::size_t channel = 0; channel < config_.channel_count;
                 ++channel) {
                selected_channels.push_back(channel);
            }
        }

        std::vector<int> group_by_channel(config_.channel_count, 0);
        for (const auto channel : selected_channels) {
            switch (config_.detector.whistle_grouping_type) {
            case DetectorConfig::ClickGroupingType::Singles:
                group_by_channel[channel] = static_cast<int>(channel);
                break;
            case DetectorConfig::ClickGroupingType::All:
                group_by_channel[channel] = 0;
                break;
            case DetectorConfig::ClickGroupingType::User:
                if (channel <
                    config_.detector.whistle_channel_groups.size()) {
                    group_by_channel[channel] =
                        config_.detector.whistle_channel_groups[channel];
                }
                break;
            }
        }
        std::uint32_t selected_bitmap = 0;
        for (const auto channel : selected_channels) {
            selected_bitmap |= std::uint32_t{1} << channel;
        }
        for (auto channels : grouped_source_channels(
                 selected_bitmap, group_by_channel, config_.channel_count)) {
            if (channels.empty()) {
                continue;
            }
            const auto first_channel = channels.front();
            region_config.channel = first_channel;
            whistle_region_trackers_.try_emplace(first_channel, region_config);
            whistle_group_channels_.emplace(first_channel, channels);
            whistle_backgrounds_.try_emplace(
                first_channel, static_cast<double>(config_.sample_rate_hz),
                config_.detector.fft.fft_hop,
                config_.detector.fft.fft_length / 2, 10.0);
        }
    }
}

const AnalysisConfig& AnalysisSession::config() const noexcept {
    return config_;
}

AnalysisResult AnalysisSession::process(const AudioChunk& chunk) {
    if (chunk.orientation_declared) {
        // Attitude sampled at chunk cadence: the declaration holds until the
        // next one, so a source can report heading as often or as rarely as
        // it measures it. Geometry itself stays static.
        current_orientation_.declared = true;
        current_orientation_.heading_degrees = chunk.orientation_heading_degrees;
        current_orientation_.pitch_degrees = chunk.orientation_pitch_degrees;
        current_orientation_.roll_degrees = chunk.orientation_roll_degrees;
    }
    AnalysisResult result;
    result.spectrogram_frames = spectrogram_.process(chunk);
    if (fft_noise_monitor_.has_value()) {
        // NoiseProcess increments its shared measurement index when the
        // highest selected channel arrives, so preserve PAMGuard's
        // slice-major/channel-minor FFT delivery order.
        std::vector<const dsp::SpectrogramFrame*> ordered;
        ordered.reserve(result.spectrogram_frames.size());
        for (const auto& frame : result.spectrogram_frames) {
            ordered.push_back(&frame);
        }
        std::stable_sort(
            ordered.begin(), ordered.end(),
            [](const auto* a, const auto* b) {
                if (a->fft_slice != b->fft_slice) {
                    return a->fft_slice < b->fft_slice;
                }
                return a->channel < b->channel;
            });
        const double process_gain_db =
            20.0 * std::log10(std::abs(spectrogram_.window_gain()));
        for (const auto* frame : ordered) {
            auto periods = fft_noise_monitor_->process_frame(
                frame->channel, frame->time_unix_ms, frame->start_sample,
                pamguard_packed_magnitude_squared(frame->bins));
            for (auto& period : periods) {
                const auto* hydrophone =
                    find_hydrophone(config_.array, period.channel);
                double constant_db = config_.acquisition.preamp_gain_db;
                if (hydrophone != nullptr) {
                    constant_db += hydrophone->sensitivity_db +
                                   hydrophone->preamp_gain_db;
                }
                const auto calibrate = [&](double power) {
                    const double raw =
                        std::sqrt(power) /
                        static_cast<double>(config_.detector.fft.fft_length) *
                        std::sqrt(2.0);
                    double db = 20.0 * std::log10(
                        raw * config_.acquisition.volts_peak_to_peak / 2.0) -
                        constant_db;
                    if (!std::isfinite(db)) {
                        db = 0.0;
                    }
                    return db - process_gain_db;
                };
                FftNoiseResult output;
                output.channel = period.channel;
                output.end_sample = period.end_sample;
                output.time_unix_ms = period.time_unix_ms;
                output.n_measurements = period.n_measurements;
                for (std::size_t i = 0; i < period.bands.size(); ++i) {
                    auto stats = period.bands[i];
                    stats.mean = calibrate(stats.mean);
                    stats.median = calibrate(stats.median);
                    stats.low_95 = calibrate(stats.low_95);
                    stats.high_95 = calibrate(stats.high_95);
                    stats.minimum = calibrate(stats.minimum);
                    stats.maximum = calibrate(stats.maximum);
                    const auto& band = config_.detector.fft_noise.bands[i];
                    output.bands.push_back({
                        band.name, band.low_frequency_hz,
                        band.high_frequency_hz, stats});
                }
                result.fft_noise.push_back(std::move(output));
            }
        }
    }
    if (!ltsa_monitors_.empty()) {
        // LTSA sources the FFT data block directly, BEFORE any whistle-path
        // noise reduction, and PAMGuard stamps each FFT unit with a duration
        // of fftLength samples.
        for (const auto& frame : result.spectrogram_frames) {
            auto monitor = ltsa_monitors_.find(frame.channel);
            if (monitor == ltsa_monitors_.end()) {
                continue;
            }
            auto closed = monitor->second.process_frame(
                frame.time_unix_ms,
                frame.start_sample,
                static_cast<std::int64_t>(config_.detector.fft.fft_length),
                pamguard_packed_magnitude_squared(frame.bins));
            if (closed.has_value()) {
                result.ltsa.push_back({frame.channel, std::move(*closed)});
            }
        }
    }
    if (ishmael_energy_.has_value()) {
        // PAMGuard's FFT units arrive slice-major across channels (ch0 ch1
        // ch0 ch1 ...); the engine's frames are channel-major per chunk, so
        // they are re-ordered to feed the shared-state energy sum the way
        // the reference sees them.
        std::vector<const dsp::SpectrogramFrame*> ordered;
        ordered.reserve(result.spectrogram_frames.size());
        for (const auto& frame : result.spectrogram_frames) {
            ordered.push_back(&frame);
        }
        std::stable_sort(ordered.begin(), ordered.end(),
                         [](const dsp::SpectrogramFrame* a, const dsp::SpectrogramFrame* b) {
                             if (a->fft_slice != b->fft_slice) {
                                 return a->fft_slice < b->fft_slice;
                             }
                             return a->channel < b->channel;
                         });
        for (const auto* frame : ordered) {
            const auto sample = ishmael_energy_->process_frame(
                pamguard_packed_magnitude_squared(frame->bins));
            auto detection = ishmael_picker_->process(frame->channel, frame->start_sample,
                                                      sample.det_value);
            if (detection.has_value()) {
                result.ishmael_detections.push_back(*detection);
            }
        }
    }
    if (sgram_corr_detector_.has_value()) {
        // Per-channel gram buffers keep their own state, so plain frame
        // order (time-ordered within each channel) is sufficient.
        for (const auto& frame : result.spectrogram_frames) {
            const auto fn = sgram_corr_detector_->process_frame(
                frame.channel, pamguard_packed_magnitude_squared(frame.bins));
            if (fn.has_value()) {
                auto detection = sgram_corr_picker_->process(frame.channel, frame.start_sample, *fn);
                if (detection.has_value()) {
                    result.sgram_corr_detections.push_back(*detection);
                }
            }
        }
    }
    if (match_filt_detector_.has_value() && match_filt_detector_->valid()) {
        // Raw-audio path. Empty channel list means channel 0 only — the
        // reference's behaviour with no channel groups.
        static const std::vector<std::size_t> default_channels{0};
        const auto& mf_channels = config_.detector.match_filt.channels.empty()
            ? default_channels : config_.detector.match_filt.channels;
        std::vector<double> channel_samples;
        for (const auto channel : mf_channels) {
            if (channel >= chunk.channel_count) {
                continue;
            }
            const auto frames = chunk.frame_count();
            channel_samples.resize(frames);
            for (std::size_t i = 0; i < frames; ++i) {
                channel_samples[i] = chunk.sample(i, channel);
            }
            const auto blocks = match_filt_detector_->process(channel, channel_samples);
            for (const auto& block : blocks) {
                for (std::size_t i = 0; i < block.values.size(); ++i) {
                    auto detection = match_filt_picker_->process(
                        channel, block.start_sample + static_cast<std::int64_t>(i), block.values[i]);
                    if (detection.has_value()) {
                        result.match_filt_detections.push_back(*detection);
                    }
                }
            }
        }
    }
    if (!click_detectors_.empty()) {
        for (std::size_t group_index = 0; group_index < click_detectors_.size(); ++group_index) {
            auto group_clicks = click_detectors_[group_index].process(chunk);
            const auto& noise_samples = click_detectors_[group_index].noise_samples();
            result.click_noise_samples.insert(result.click_noise_samples.end(),
                                              noise_samples.begin(), noise_samples.end());
            const auto& background = click_detectors_[group_index].trigger_background();
            result.click_trigger_background.insert(
                result.click_trigger_background.end(),
                background.begin(), background.end());
            const auto& trigger_function = click_detectors_[group_index].trigger_function();
            result.click_trigger_function.insert(result.click_trigger_function.end(),
                                                 trigger_function.begin(),
                                                 trigger_function.end());
            if (click_echo_detectors_[group_index].has_value()) {
                // PAMGuard owns one echo detector per ChannelGroupDetector.
                // Its anchor state therefore never leaks between groups.
                std::vector<detectors::ClickDetectionResult> kept;
                kept.reserve(group_clicks.size());
                for (auto& click : group_clicks) {
                    const bool echo =
                        click_echo_detectors_[group_index]->is_echo(click.start_sample);
                    if (echo && config_.detector.click_echo_discard) {
                        continue;
                    }
                    click.echo = echo;
                    kept.push_back(std::move(click));
                }
                group_clicks = std::move(kept);
            }
            result.clicks.insert(result.clicks.end(),
                                 std::make_move_iterator(group_clicks.begin()),
                                 std::make_move_iterator(group_clicks.end()));
        }
        std::stable_sort(result.clicks.begin(), result.clicks.end(),
                         [](const auto& a, const auto& b) {
                             if (a.start_sample != b.start_sample) {
                                 return a.start_sample < b.start_sample;
                             }
                             return a.channel_bitmap < b.channel_bitmap;
                         });
    }
    if (click_basic_classifier_ || click_sweep_classifier_) {
        // PAMGuard classifies before delay measurement and removes clicks
        // rejected by either the matched type or discardUnclassifiedClicks
        // before any downstream consumer sees them.
        std::vector<detectors::ClickDetectionResult> kept_clicks;
        std::vector<detectors::ClickClassificationResult> kept_classifications;
        kept_clicks.reserve(result.clicks.size());
        kept_classifications.reserve(result.clicks.size());
        for (auto& click : result.clicks) {
            if (click.waveform.empty()) {
                kept_clicks.push_back(std::move(click));
                continue;
            }
            auto classification = click_basic_classifier_
                ? click_basic_classifier_->identify(click)
                : click_sweep_classifier_->identify(click);
            if (classification.discard ||
                (classification.click_type == 0 && config_.detector.click_discard_unclassified)) {
                continue;
            }
            classification.click_index = kept_clicks.size();
            kept_clicks.push_back(std::move(click));
            kept_classifications.push_back(classification);
        }
        result.clicks = std::move(kept_clicks);
        result.click_classifications = std::move(kept_classifications);
    }
    if (config_.detector.click_localisation_enabled ||
        !config_.detector.click_angle_vetoes.empty()) {
        std::vector<detectors::ClickDetectionResult> kept_clicks;
        std::vector<detectors::ClickClassificationResult> kept_classifications;
        kept_clicks.reserve(result.clicks.size());
        kept_classifications.reserve(result.click_classifications.size());
        for (std::size_t i = 0; i < result.clicks.size(); ++i) {
            auto& click = result.clicks[i];
            std::optional<ClickLocalisationResult> click_localisation;
            std::optional<ClickBearingResult> click_bearing;
            double legacy_angle_degrees = 0.0;
            const auto classification = std::find_if(
                result.click_classifications.begin(),
                result.click_classifications.end(),
                [i](const auto& item) { return item.click_index == i; });

            if (click.waveform.size() >= 2) {
                ClickLocalisationResult localisation;
                localisation.click_start_sample = click.start_sample;
                localisation.array_shape = sub_array_shape(config_, click.channels);
                localisation.bearing_localiser =
                    localisation::select_bearing_localiser(localisation.array_shape);
                const auto pair_geometry =
                    click_pair_geometry(config_, click.channels);
                const auto max_delays =
                    max_delay_samples_from_geometry(pair_geometry);
                const int click_type =
                    classification == result.click_classifications.end()
                    ? 0
                    : classification->click_type;
                auto delay_config = config_.detector.click_delay_measurement;
                if (const auto override_it =
                        config_.detector.click_delay_measurement_by_type.find(click_type);
                    override_it !=
                    config_.detector.click_delay_measurement_by_type.end()) {
                    delay_config = override_it->second;
                }
                if (delay_config.use_leading_edge && !max_delays.empty()) {
                    const double largest_delay =
                        *std::max_element(max_delays.begin(), max_delays.end());
                    delay_config.leading_edge_search_start =
                        static_cast<int>(config_.detector.click.pre_sample) -
                        static_cast<int>(largest_delay);
                    delay_config.leading_edge_search_end =
                        static_cast<int>(config_.detector.click.pre_sample) +
                        static_cast<int>(largest_delay);
                }
                localisation.delays = click_delay_estimator_.estimate_delays(
                    click.waveform,
                    max_delays,
                    static_cast<double>(config_.sample_rate_hz),
                    delay_config);

                // ClickDetection.getAngle() is legacy behavior: it ignores
                // setAnglesAndErrors and derives the bearing from the first
                // pair delay and hydrophone separation. Preserve that exact
                // acos/clamp conversion for Detector Vetoes.
                if (!localisation.delays.empty() &&
                    !pair_geometry.empty() &&
                    pair_geometry.front().constrained &&
                    pair_geometry.front().max_delay_samples > 0.0) {
                    const double cosine = std::clamp(
                        localisation.delays.front().delay.delay_samples /
                            pair_geometry.front().max_delay_samples,
                        -1.0, 1.0);
                    legacy_angle_degrees =
                        std::acos(cosine) * 180.0 / std::numbers::pi;
                }

                attach_pair_geometry(localisation.delays, pair_geometry,
                                     config_, current_orientation_);
                attach_lsq_bearing(localisation, pair_geometry,
                                   click.channels.size(), config_,
                                   current_orientation_);
                {
                    std::vector<double> grid_delays_seconds;
                    grid_delays_seconds.reserve(localisation.delays.size());
                    for (const auto& delay : localisation.delays) {
                        grid_delays_seconds.push_back(
                            delay.delay.delay_samples /
                            static_cast<double>(config_.sample_rate_hz));
                    }
                    localisation.grid_bearing = grid_bearing_for_channels(
                        config_, click.channels,
                        localisation.bearing_localiser,
                        grid_delays_seconds, current_orientation_);
                }
                click_localisation = std::move(localisation);
                if (click_bearing_localiser_) {
                    ClickBearingResult bearing;
                    bearing.click_start_sample = click.start_sample;
                    bearing.bearing = click_bearing_localiser_->estimate(
                        click_localisation->delays,
                        click.channels,
                        i,
                        click.start_sample);
                    if (bearing.bearing.valid) {
                        click_bearing = std::move(bearing);
                    }
                }
            }

            if (!detectors::ClickAngleVetoes::pass_all(
                    config_.detector.click_angle_vetoes,
                    legacy_angle_degrees)) {
                continue;
            }

            const auto kept_index = kept_clicks.size();
            kept_clicks.push_back(std::move(click));
            if (classification != result.click_classifications.end()) {
                auto kept_classification = *classification;
                kept_classification.click_index = kept_index;
                kept_classifications.push_back(std::move(kept_classification));
            }
            if (config_.detector.click_localisation_enabled &&
                click_localisation.has_value()) {
                click_localisation->click_index = kept_index;
                result.click_localisations.push_back(
                    std::move(*click_localisation));
            }
            if (config_.detector.click_localisation_enabled &&
                click_bearing.has_value()) {
                click_bearing->click_index = kept_index;
                result.click_bearings.push_back(std::move(*click_bearing));
            }
        }
        result.clicks = std::move(kept_clicks);
        result.click_classifications = std::move(kept_classifications);
    }
    if (click_feature_extractor_) {
        for (std::size_t i = 0; i < result.clicks.size(); ++i) {
            if (result.clicks[i].waveform.empty()) {
                continue;
            }
            auto features = click_feature_extractor_->extract(result.clicks[i]);
            features.click_index = i;
            result.click_features.push_back(std::move(features));
        }
    }
    if (matched_template_classifier_.has_value()) {
        // MTProcess consumes finished clicks after the angle-veto gate, one
        // annotation per click carrying the best result per template pair.
        for (std::size_t i = 0; i < result.clicks.size(); ++i) {
            if (result.clicks[i].waveform.empty()) {
                continue;
            }
            auto outcome =
                matched_template_classifier_->classify(result.clicks[i].waveform);
            MatchedTemplateClickResult entry;
            entry.click_index = i;
            entry.click_start_sample = result.clicks[i].start_sample;
            entry.classified = outcome.classified;
            entry.results = std::move(outcome.best_results);
            result.matched_template_classifications.push_back(std::move(entry));
        }
    }
    if (click_train_tracker_) {
        result.click_trains = click_train_tracker_->process(result.clicks);
    }
    if (!result.click_trains.empty() && !result.click_localisations.empty()) {
        result.click_train_localisations = summarize_click_train_localisations(result.click_trains, result.click_localisations);
    }
    if (!result.click_trains.empty() && !result.click_bearings.empty()) {
        result.click_train_bearings = summarize_click_train_bearings(result.click_trains, result.click_bearings);
    }
    // After localisation so per-click bearings and features are available to
    // the optional MHT bearing/peak-frequency chi2 variables.
    process_mht_click_trains(result);
    classify_ici_click_trains(result);
    if (!noise_band_monitors_.empty()) {
        // rawAmplitude2dB: 20*log10(raw * vp2p / 2) - (hydrophone sensitivity
        // + hydrophone preamp gain + acquisition preamp gain); non-finite -> 0.
        const auto amplitude_db = [&](double raw, std::size_t channel) {
            double constant_term = config_.acquisition.preamp_gain_db;
            const auto* hydrophone = find_hydrophone(config_.array, channel);
            if (hydrophone != nullptr) {
                constant_term += hydrophone->sensitivity_db + hydrophone->preamp_gain_db;
            }
            const double db = 20.0 * std::log10(raw * config_.acquisition.volts_peak_to_peak / 2.0) - constant_term;
            return std::isfinite(db) ? db : 0.0;
        };
        std::vector<double> channel_samples;
        for (auto& [channel, monitor] : noise_band_monitors_) {
            if (channel >= chunk.channel_count || !monitor.valid()) {
                continue;
            }
            const auto frames = chunk.frame_count();
            channel_samples.resize(frames);
            for (std::size_t i = 0; i < frames; ++i) {
                channel_samples[i] = chunk.sample(i, channel);
            }
            const auto levels = monitor.process(channel_samples, chunk.start_sample, chunk.time_unix_ms);
            if (levels.has_value()) {
                NoiseBandResult noise;
                noise.channel = channel;
                noise.end_sample = levels->end_sample;
                noise.time_unix_ms = levels->time_unix_ms;
                noise.rms_db.reserve(levels->rms.size());
                noise.peak_db.reserve(levels->peak.size());
                for (const double value : levels->rms) {
                    noise.rms_db.push_back(amplitude_db(value, channel));
                }
                for (const double value : levels->peak) {
                    noise.peak_db.push_back(amplitude_db(value, channel));
                }
                result.noise_bands.push_back(std::move(noise));
            }
        }
    }
    if (!whistle_peak_detectors_.empty() || !whistle_region_trackers_.empty()) {
        if (!whistle_backgrounds_.empty()) {
            // The observer is attached to the RAW FFT block. PAMGuard
            // delivers FFT units slice-major/channel-minor, and its single
            // background emission clock snapshots every channel when the
            // first frame reaches the interval.
            std::vector<const dsp::SpectrogramFrame*> ordered;
            ordered.reserve(result.spectrogram_frames.size());
            for (const auto& frame : result.spectrogram_frames) {
                ordered.push_back(&frame);
            }
            std::stable_sort(
                ordered.begin(), ordered.end(),
                [](const auto* a, const auto* b) {
                    if (a->fft_slice != b->fft_slice) {
                        return a->fft_slice < b->fft_slice;
                    }
                    return a->channel < b->channel;
                });
            const double interval_seconds =
                config_.detector.whistle_region.background_interval_seconds;
            for (const auto* frame : ordered) {
                const auto monitor = whistle_backgrounds_.find(frame->channel);
                if (monitor == whistle_backgrounds_.end()) {
                    continue;
                }
                monitor->second.process(
                    pamguard_packed_magnitude_squared(frame->bins));
                if (!last_whistle_background_time_ms_.has_value()) {
                    last_whistle_background_time_ms_ = frame->time_unix_ms;
                    continue;
                }
                if (static_cast<double>(frame->time_unix_ms) <
                    static_cast<double>(*last_whistle_background_time_ms_) +
                        interval_seconds * 1000.0) {
                    continue;
                }
                last_whistle_background_time_ms_ = frame->time_unix_ms;
                const double scale =
                    2.0 / static_cast<double>(config_.detector.fft.fft_length);
                for (std::size_t channel = 0; channel < config_.channel_count;
                     ++channel) {
                    const auto found = whistle_backgrounds_.find(channel);
                    if (found == whistle_backgrounds_.end()) {
                        continue;
                    }
                    const auto& background = found->second;
                    WhistleBackgroundResult output;
                    output.channel = channel;
                    output.time_ms = frame->time_unix_ms;
                    output.start_sample = static_cast<std::int64_t>(
                        static_cast<double>(frame->start_sample) -
                        interval_seconds *
                            static_cast<double>(config_.sample_rate_hz));
                    output.duration_ms = static_cast<double>(
                        static_cast<std::int64_t>(
                            interval_seconds * 1000.0));
                    output.spectrum.reserve(background.data().size());
                    for (const double power : background.data()) {
                        output.spectrum.push_back(std::sqrt(power * scale));
                    }
                    result.whistle_backgrounds.push_back(std::move(output));
                }
            }
        }
        for (const auto& raw_frame : result.spectrogram_frames) {
            // PAMGuard's whistle chain is FFT -> SpectrogramNoiseProcess ->
            // WhistleToneConnectProcess, and WhistleDelays correlates on the
            // noise process's OUTPUT block, so the reduced slice feeds both
            // the detectors and the retained FFT history.
            dsp::SpectrogramFrame frame = raw_frame;
            if (whistle_noise_reducer_.has_value() && frame.bins.size() >= 2) {
                const std::size_t half = frame.bins.size() - 1;
                std::vector<std::complex<double>> packed(half);
                packed[0] = {frame.bins[0].real(), frame.bins[half].real()};
                for (std::size_t bin = 1; bin < half; ++bin) {
                    packed[bin] = frame.bins[bin];
                }
                const auto reduced = whistle_noise_reducer_->process(frame.channel, packed);
                frame.bins[0] = {reduced[0].real(), 0.0};
                frame.bins[half] = {reduced[0].imag(), 0.0};
                for (std::size_t bin = 1; bin < half; ++bin) {
                    frame.bins[bin] = reduced[bin];
                }
            }
            retain_whistle_fft_frame(frame);
            const auto magnitude_squared = pamguard_packed_magnitude_squared(frame.bins);
            std::vector<detectors::WhistlePeak> frame_peaks;

            auto peak_detector = whistle_peak_detectors_.find(frame.channel);
            if (peak_detector != whistle_peak_detectors_.end()) {
                frame_peaks = peak_detector->second.process_magnitude_slice(
                    magnitude_squared,
                    frame.start_sample,
                    frame.time_unix_ms,
                    frame.fft_slice);
                for (auto& peak : frame_peaks) {
                    peak.channel = frame.channel;
                    result.whistle_peaks.push_back(peak);
                }
            }

            auto region_tracker = whistle_region_trackers_.find(frame.channel);
            if (region_tracker != whistle_region_trackers_.end()) {
                std::vector<bool> active_bins(magnitude_squared.size(), false);
                // WhistleToneConnectProcess does not consume BetterPeakDetector
                // output. It converts the noise-reduced FFT slice directly:
                //     newCol[i] = complexData.magsq(i) > 0
                // over [searchBin1, searchBin2). Keep the older peak output as
                // an independent optional result, but never use it to gate
                // connected-region pixels.
                const auto& region_config = region_tracker->second.config();
                const auto [min_bin, max_bin_exclusive] =
                    detectors::whistle_frequency_bin_range(
                        region_config.min_frequency_hz,
                        region_config.max_frequency_hz,
                        static_cast<double>(config_.sample_rate_hz),
                        config_.detector.fft.fft_length,
                        magnitude_squared.size());
                for (std::size_t bin = min_bin; bin < max_bin_exclusive; ++bin) {
                    active_bins[bin] = magnitude_squared[bin] > 0.0;
                }
                auto regions = region_tracker->second.process_slice(
                    frame.fft_slice,
                    frame.start_sample,
                    frame.time_unix_ms,
                    active_bins,
                    magnitude_squared);
                result.whistle_regions.insert(result.whistle_regions.end(), regions.begin(), regions.end());
            }
        }
    }
    compute_whistle_delays(result);
    group_whistle_regions(result);
    return result;
}

AnalysisResult AnalysisSession::flush() {
    AnalysisResult result;
    if (click_train_tracker_) {
        result.click_trains = click_train_tracker_->flush();
    }
    for (auto& [_, state] : mht_train_states_) {
        state.kernel->confirm_remaining_tracks();
    }
    drain_confirmed_mht_trains(result);
    classify_ici_click_trains(result);
    for (auto& [_, tracker] : whistle_region_trackers_) {
        auto regions = tracker.flush();
        result.whistle_regions.insert(result.whistle_regions.end(), regions.begin(), regions.end());
    }
    // flushDataBlockBuffers: close the in-progress LTSA period per channel.
    for (auto& [channel, monitor] : ltsa_monitors_) {
        auto closed = monitor.flush();
        if (closed.has_value()) {
            result.ltsa.push_back({channel, std::move(*closed)});
        }
    }
    compute_whistle_delays(result);
    group_whistle_regions(result);
    return result;
}

bool AnalysisSession::whistle_delays_enabled() const {
    const bool has_multi_channel_group = std::any_of(
        whistle_group_channels_.begin(), whistle_group_channels_.end(),
        [](const auto& item) { return item.second.size() >= 2; });
    return has_multi_channel_group &&
        config_.array.hydrophones.size() >= 2 &&
        config_.array.speed_of_sound_mps > 0.0 &&
        config_.sample_rate_hz != 0 &&
        config_.detector.fft.fft_length >= 4;
}

void AnalysisSession::retain_whistle_fft_frame(const dsp::SpectrogramFrame& frame) {
    if (!whistle_delays_enabled()) {
        return;
    }
    const bool grouped_channel = std::any_of(
        whistle_group_channels_.begin(), whistle_group_channels_.end(),
        [&](const auto& item) {
            return std::find(item.second.begin(), item.second.end(),
                             frame.channel) != item.second.end();
        });
    if (!grouped_channel) {
        return;
    }
    // PAMGuard searches backwards through the retained FFT data block and
    // accepts partial coverage when older slices have been discarded; the
    // engine keeps a bounded per-channel history with the same effect.
    constexpr std::size_t max_history_slices = 512;
    auto& history = whistle_fft_history_[frame.channel];
    history.push_back(frame);
    while (history.size() > max_history_slices) {
        history.pop_front();
    }
}

void AnalysisSession::compute_whistle_delays(AnalysisResult& result) {
    if (!whistle_delays_enabled() || result.whistle_regions.empty()) {
        return;
    }

    const auto fft_length = config_.detector.fft.fft_length;
    for (const auto& region : result.whistle_regions) {
        const auto group = whistle_group_channels_.find(region.channel);
        if (group == whistle_group_channels_.end() ||
            group->second.size() < 2) {
            continue;
        }
        const auto& whistle_channels = group->second;
        WhistleRegionDelayResult delay_result;
        delay_result.channel = region.channel;
        delay_result.region_number = region.region_number;
        delay_result.start_sample = region.start_sample;
        delay_result.array_shape = sub_array_shape(config_, whistle_channels);
        delay_result.bearing_localiser = localisation::select_bearing_localiser(delay_result.array_shape);

        // PAMGuard WhistleDelays measures every channel pair in the group
        // (0-1, 0-2, 1-2, ...), which is also what an LSQ solve needs.
        std::vector<localisation::LsqPairGeometry> lsq_pairs;
        std::vector<double> lsq_delays_seconds;
        for (std::size_t index_a = 0; index_a < whistle_channels.size(); ++index_a) {
        for (std::size_t index_b = index_a + 1; index_b < whistle_channels.size(); ++index_b) {
            const auto channel_a = whistle_channels[index_a];
            const auto channel_b = whistle_channels[index_b];
            auto pair_geometry = click_pair_geometry(config_, {channel_a, channel_b});
            if (pair_geometry.size() != 1 || !pair_geometry[0].constrained ||
                pair_geometry[0].hydrophone_distance_m <= 0.0) {
                continue;
            }

            const auto history_a = whistle_fft_history_.find(channel_a);
            const auto history_b = whistle_fft_history_.find(channel_b);
            if (history_a == whistle_fft_history_.end() || history_b == whistle_fft_history_.end()) {
                continue;
            }

            localisation::WhistleDelayEstimator estimator(fft_length);
            bool accumulated = false;
            for (const auto& slice : region.slices) {
                const dsp::SpectrogramFrame* frame_a = nullptr;
                const dsp::SpectrogramFrame* frame_b = nullptr;
                for (auto it = history_a->second.rbegin(); it != history_a->second.rend(); ++it) {
                    if (it->fft_slice == slice.slice_number) {
                        frame_a = &*it;
                        break;
                    }
                }
                for (auto it = history_b->second.rbegin(); it != history_b->second.rend(); ++it) {
                    if (it->fft_slice == slice.slice_number) {
                        frame_b = &*it;
                        break;
                    }
                }
                if (frame_a == nullptr || frame_b == nullptr) {
                    continue;
                }
                for (const auto& peak : slice.peak_info) {
                    if (peak.size() < 3) {
                        continue;
                    }
                    for (int bin = peak[0]; bin <= peak[2]; ++bin) {
                        if (bin < 0 || static_cast<std::size_t>(bin) >= fft_length / 2 ||
                            static_cast<std::size_t>(bin) >= frame_a->bins.size() ||
                            static_cast<std::size_t>(bin) >= frame_b->bins.size()) {
                            continue;
                        }
                        estimator.add_fft_data(frame_a->bins[static_cast<std::size_t>(bin)],
                                               frame_b->bins[static_cast<std::size_t>(bin)],
                                               static_cast<std::size_t>(bin));
                        accumulated = true;
                    }
                }
            }
            if (!accumulated) {
                continue;
            }

            std::vector<localisation::ChannelPairDelay> pair_delays(1);
            pair_delays[0].channel_a = 0;
            pair_delays[0].channel_b = 1;
            pair_delays[0].delay = estimator.get_delay(pair_geometry[0].max_delay_samples);
            attach_pair_geometry(pair_delays, pair_geometry, config_, current_orientation_);
            pair_delays[0].pair_index = delay_result.delays.size();

            const auto* hydrophone_a = find_hydrophone(config_.array, channel_a);
            const auto* hydrophone_b = find_hydrophone(config_.array, channel_b);
            if (hydrophone_a != nullptr && hydrophone_b != nullptr && config_.array.spacing_error_m > 0.0) {
                localisation::LsqPairGeometry lsq_pair;
                lsq_pair.baseline_m = {hydrophone_b->x_m - hydrophone_a->x_m,
                                       hydrophone_b->y_m - hydrophone_a->y_m,
                                       hydrophone_b->z_m - hydrophone_a->z_m};
                const double distance = pair_geometry[0].hydrophone_distance_m;
                const double error_scale = config_.array.spacing_error_m / distance;
                lsq_pair.error_m = {lsq_pair.baseline_m[0] * error_scale,
                                    lsq_pair.baseline_m[1] * error_scale,
                                    lsq_pair.baseline_m[2] * error_scale};
                lsq_pairs.push_back(lsq_pair);
                lsq_delays_seconds.push_back(pair_delays[0].delay.delay_samples
                    / static_cast<double>(config_.sample_rate_hz));
            }

            delay_result.delays.push_back(std::move(pair_delays[0]));
        }
        }

        if (!delay_result.delays.empty()) {
            // WhistleBearingInfo equivalent: the group's bearing localiser
            // result for the region. PAMGuard selects LSQ for groups with
            // enough non-coplanar hydrophones (an unambiguous azimuth and
            // elevation) and the pair localiser otherwise, whose single cone
            // angle carries the left/right ambiguity flag.
            const auto expected_pairs = whistle_channels.size() * (whistle_channels.size() - 1) / 2;
            if (delay_result.delays.size() == expected_pairs) {
                std::vector<double> grid_delays_seconds;
                grid_delays_seconds.reserve(expected_pairs);
                for (const auto& delay : delay_result.delays) {
                    grid_delays_seconds.push_back(delay.delay.delay_samples / static_cast<double>(config_.sample_rate_hz));
                }
                delay_result.grid_bearing = grid_bearing_for_channels(config_, whistle_channels,
                                                                     delay_result.bearing_localiser,
                                                                     grid_delays_seconds, current_orientation_);
            }
            if (delay_result.bearing_localiser == localisation::BearingLocaliserChoice::Grid &&
                whistle_channels.size() >= 4 && lsq_pairs.size() == expected_pairs &&
                lsq_delays_seconds.size() == lsq_pairs.size()) {
                localisation::LsqBearingConfig lsq_config;
                lsq_config.speed_of_sound_mps = config_.array.speed_of_sound_mps;
                lsq_config.speed_of_sound_error_mps = config_.array.speed_of_sound_error_mps;
                lsq_config.timing_error_seconds = config_.array.timing_error_seconds;
                lsq_config.pairs = lsq_pairs;
                const localisation::LsqBearingLocaliser lsq_localiser(std::move(lsq_config));
                if (const auto lsq = lsq_localiser.localise(lsq_delays_seconds)) {
                    if (std::isfinite(lsq->azimuth_radians) && std::isfinite(lsq->elevation_radians)) {
                        delay_result.lsq_bearing.valid = true;
                        delay_result.lsq_bearing.azimuth_radians = lsq->azimuth_radians;
                        delay_result.lsq_bearing.elevation_radians = lsq->elevation_radians;
                        delay_result.lsq_bearing.azimuth_error_radians = lsq->azimuth_error_radians;
                        delay_result.lsq_bearing.elevation_error_radians = lsq->elevation_error_radians;
                        delay_result.lsq_bearing.used_pairs = lsq_pairs.size();
                        delay_result.lsq_bearing.world_vectors = {localisation::WorldVector{
                            localisation::planar_unit_vector(lsq->azimuth_radians, lsq->elevation_radians), false}};
                        delay_result.lsq_bearing.earth_world_vectors = earth_frame_vectors(
                            current_orientation_, delay_result.array_shape, delay_result.lsq_bearing.world_vectors);
                        delay_result.bearing_valid = true;
                        delay_result.bearing_radians = lsq->azimuth_radians;
                        delay_result.bearing_error_radians = lsq->azimuth_error_radians;
                        delay_result.bearing_ambiguity = false;
                        delay_result.bearing_pair_count = lsq_pairs.size();
                    }
                }
            }

            if (!delay_result.bearing_valid) {
                for (const auto& delay : delay_result.delays) {
                    if (delay.pair_bearing_valid && std::isfinite(delay.pair_bearing_radians)) {
                        delay_result.bearing_valid = true;
                        delay_result.bearing_radians = delay.pair_bearing_radians;
                        delay_result.bearing_error_radians = delay.pair_bearing_error_radians;
                        delay_result.bearing_ambiguity = true;
                        delay_result.bearing_pair_count = delay_result.delays.size();
                        break;
                    }
                }
            }
            result.whistle_delays.push_back(std::move(delay_result));
        }
    }
}

std::vector<dsp::SpectrogramFrame> AnalysisSession::process_audio(const AudioChunk& chunk) {
    return process(chunk).spectrogram_frames;
}

void AnalysisSession::process_mht_click_trains(AnalysisResult& result) {
    if (!config_.detector.click_train_tracker_enabled || !config_.detector.click_train_mht ||
        config_.sample_rate_hz == 0) {
        return;
    }

    for (std::size_t click_index = 0; click_index < result.clicks.size(); ++click_index) {
        const auto& click = result.clicks[click_index];
        const auto key = click.channel_bitmap != 0 ? click.channel_bitmap : click.trigger_bitmap;
        auto found = mht_train_states_.find(key);
        if (found == mht_train_states_.end()) {
            auto chi2_params = config_.detector.click_train_mht_chi2;
            chi2_params.sample_rate_hz = static_cast<double>(config_.sample_rate_hz);
            const auto kernel_params = config_.detector.click_train_mht_kernel;
            MhtTrainState state;
            state.kernel = std::make_unique<detectors::MhtKernel<detectors::MhtChi2Unit>>(
                std::make_unique<detectors::StandardMhtChi2Provider>(chi2_params, kernel_params), kernel_params);
            found = mht_train_states_.emplace(key, std::move(state)).first;
        }

        auto& state = found->second;
        detectors::MhtChi2Unit unit;
        unit.time_ns = static_cast<std::int64_t>(static_cast<double>(click.start_sample)
            / static_cast<double>(config_.sample_rate_hz) * 1E9);

        // MHTGarbageBot hard limit: a gap longer than maxCoast * maxICI (or
        // hitting the detection hard limit) confirms and resets the kernel.
        constexpr std::size_t detection_hard_limit = 10000;
        const auto* last_unit = state.kernel->last_data_unit();
        if (state.kernel->kcount() > 5 && last_unit != nullptr) {
            const double gap_seconds = static_cast<double>(unit.time_ns / 1'000'000 - last_unit->time_ns / 1'000'000) / 1000.0;
            const double max_gap = 3.0 * 0.4; // maxCoast * StandardMHTChi2Params.maxICI defaults
            if (gap_seconds > max_gap || state.kernel->kcount() > detection_hard_limit) {
                state.kernel->confirm_remaining_tracks();
                drain_confirmed_mht_trains(result);
                state.kernel->clear_kernel();
                state.start_samples.clear();
                state.time_ms.clear();
                state.consumed_confirmed = 0;
            }
        }
        // Uncalibrated peak level: the MHT amplitude chi2 only scores dB
        // differences, so a constant calibration offset cancels.
        double peak = 0.0;
        for (const auto& channel_waveform : click.waveform) {
            for (const auto sample : channel_waveform) {
                peak = std::max(peak, std::abs(sample));
            }
        }
        unit.amplitude_db = 20.0 * std::log10(std::max(peak, 1e-12));
        unit.duration_ms = static_cast<double>(click.duration_samples)
            / static_cast<double>(config_.sample_rate_hz) * 1000.0;
        for (const auto& features : result.click_features) {
            if (features.click_index == click_index) {
                unit.peak_frequency_hz = features.peak_frequency_hz;
                break;
            }
        }
        for (const auto& bearing : result.click_bearings) {
            if (bearing.click_index == click_index && bearing.bearing.valid) {
                unit.bearing_radians = bearing.bearing.azimuth_degrees * 3.141592653589793238462643383279502884 / 180.0;
                break;
            }
        }
        for (const auto& localisation : result.click_localisations) {
            if (localisation.click_index != click_index) {
                continue;
            }
            // Pair order is the click's channel-pair order, stable across
            // clicks in a session, which the time-delay variable requires.
            unit.pair_delays_seconds.reserve(localisation.delays.size());
            for (const auto& delay : localisation.delays) {
                unit.pair_delays_seconds.push_back(delay.delay.delay_samples
                    / static_cast<double>(config_.sample_rate_hz));
            }
            break;
        }
        if (config_.detector.click_train_mht_chi2.enable_correlation && !click.waveform.empty()) {
            unit.waveform = std::make_shared<const std::vector<double>>(click.waveform.front());
        }

        state.kernel->add_detection(unit);
        state.start_samples.push_back(click.start_sample);
        state.time_ms.push_back(click.time_unix_ms);
        if (config_.detector.click_train_classifier_enabled) {
            state.waveforms.push_back(click.waveform.empty() ? std::vector<double>{} : click.waveform.front());
            state.bearings_radians.push_back(unit.bearing_radians);
            state.has_bearing.push_back(unit.bearing_radians != 0.0);
        }

        // MHTGarbageBot trailing-zeros trim every twenty detections.
        constexpr std::size_t garbage_count_n_test = 20;
        constexpr std::size_t min_trim_count = 100;
        if (state.kernel->kcount() != 0 && state.kernel->kcount() % garbage_count_n_test == 0 &&
            state.kernel->kcount() > 5) {
            const auto new_ref_index = state.kernel->first_detection_index();
            if (new_ref_index == state.kernel->kcount()) {
                drain_confirmed_mht_trains(result);
                state.kernel->clear_kernel();
                state.start_samples.clear();
                state.time_ms.clear();
                state.consumed_confirmed = 0;
            }
            else if (new_ref_index > min_trim_count && new_ref_index <= state.kernel->kcount()) {
                drain_confirmed_mht_trains(result);
                state.kernel->clear_kernel_garbage(new_ref_index);
                state.start_samples.erase(state.start_samples.begin(),
                                          state.start_samples.begin() + static_cast<std::ptrdiff_t>(new_ref_index));
                state.time_ms.erase(state.time_ms.begin(),
                                    state.time_ms.begin() + static_cast<std::ptrdiff_t>(new_ref_index));
                state.consumed_confirmed = 0;
            }
        }
    }

    drain_confirmed_mht_trains(result);
}

std::vector<std::shared_ptr<const detectors::CtClassifier>> AnalysisSession::build_ct_classifiers() const {
    std::vector<std::shared_ptr<const detectors::CtClassifier>> classifiers;
    if (config_.detector.click_train_idi_classifier_enabled) {
        classifiers.push_back(std::make_shared<const detectors::CtIdiClassifierAdapter>(
            config_.detector.click_train_idi_classifier));
    }
    if (config_.detector.click_train_bearing_classifier_enabled) {
        classifiers.push_back(std::make_shared<const detectors::CtBearingClassifierAdapter>(
            config_.detector.click_train_bearing_classifier));
    }
    if (config_.detector.click_train_template_classifier_enabled) {
        classifiers.push_back(std::make_shared<const detectors::CtTemplateClassifierAdapter>(
            config_.detector.click_train_template_classifier));
    }
    return classifiers;
}

double AnalysisSession::template_correlation_for(const detectors::CtTrainSummary& summary) const {
    if (!config_.detector.click_train_template_classifier_enabled) {
        return 0.0;
    }
    const detectors::CtTemplateClassifier template_classifier(
        config_.detector.click_train_template_classifier);
    return template_classifier.classify_detailed(summary).correlation;
}

void AnalysisSession::group_whistle_regions(AnalysisResult& result) {
    if (whistle_region_trackers_.size() < 2 || result.whistle_regions.empty() ||
        config_.sample_rate_hz == 0 || config_.detector.fft.fft_length == 0) {
        return;
    }

    const double bin_hz = static_cast<double>(config_.sample_rate_hz)
        / static_cast<double>(config_.detector.fft.fft_length);
    std::vector<detectors::WhistleGroupCandidate> candidates;
    candidates.reserve(result.whistle_regions.size());
    for (const auto& region : result.whistle_regions) {
        detectors::WhistleGroupCandidate candidate;
        candidate.sequence_bitmap = static_cast<std::uint32_t>(1u << region.channel);
        candidate.start_sample = region.start_sample;
        candidate.last_sample = region.start_sample + region.duration_samples;
        candidate.time_ms = region.time_ms;
        candidate.min_frequency_hz = static_cast<double>(region.min_frequency_bin) * bin_hz;
        candidate.max_frequency_hz = static_cast<double>(region.max_frequency_bin) * bin_hz;
        candidates.push_back(candidate);
    }

    // Each new region is matched against the retained history of regions
    // completed earlier, so contours finishing in different chunks still
    // group — PAMGuard's grouper likewise scans a data block spanning
    // earlier processing rather than only the current batch.
    // Everything retained before this call belongs to an earlier chunk.
    for (auto& retained : whistle_group_history_) {
        retained.from_current_chunk = false;
    }

    std::unordered_map<std::size_t, std::size_t> group_index_by_id;
    const auto touch_group = [&](std::size_t group_id) -> WhistleRegionGroup& {
        const auto found = group_index_by_id.find(group_id);
        if (found != group_index_by_id.end()) {
            return result.whistle_groups[found->second];
        }
        WhistleRegionGroup group;
        group.group_id = group_id;
        // A group carried over from an earlier chunk describes its whole self:
        // its true first sample and every channel seen so far, not just the
        // members this chunk happens to contribute.
        const auto state = whistle_group_states_.find(group_id);
        if (state != whistle_group_states_.end()) {
            group.first_start_sample = state->second.first_start_sample;
            group.channels = state->second.channels;
            group.earlier_region_count = state->second.region_count;
        }
        result.whistle_groups.push_back(std::move(group));
        group_index_by_id.emplace(group_id, result.whistle_groups.size() - 1);
        return result.whistle_groups.back();
    };

    const auto add_channel = [](WhistleRegionGroup& group, std::size_t channel) {
        if (std::find(group.channels.begin(), group.channels.end(), channel) == group.channels.end()) {
            group.channels.push_back(channel);
        }
    };

    for (std::size_t i = 0; i < candidates.size(); ++i) {
        std::vector<detectors::WhistleGroupCandidate> history;
        history.reserve(whistle_group_history_.size());
        for (const auto& retained : whistle_group_history_) {
            history.push_back(retained.candidate);
        }
        const auto matches = detectors::find_whistle_groups(candidates[i], history,
                                                           whistle_region_trackers_.size());

        std::size_t group_id = 0;
        for (const auto match : matches) {
            if (whistle_group_history_[match].group_id != 0) {
                group_id = whistle_group_history_[match].group_id;
                break;
            }
        }
        if (group_id == 0 && !matches.empty()) {
            group_id = next_whistle_group_id_++;
        }

        if (group_id != 0) {
            auto& group = touch_group(group_id);
            auto& state = whistle_group_states_[group_id];
            for (const auto match : matches) {
                auto& retained = whistle_group_history_[match];
                if (retained.group_id == 0) {
                    retained.group_id = group_id;
                    add_channel(group, retained.channel);
                    if (state.region_count == 0 || retained.candidate.start_sample < state.first_start_sample) {
                        state.first_start_sample = retained.candidate.start_sample;
                    }
                    ++state.region_count;
                    if (retained.from_current_chunk) {
                        // Processed earlier in this same chunk and ungrouped
                        // until now, so it is indexable in this result.
                        group.region_indices.push_back(retained.result_index);
                    }
                    else {
                        ++group.earlier_region_count;
                    }
                }
            }
            group.region_indices.push_back(i);
            add_channel(group, result.whistle_regions[i].channel);
            const auto start_sample = result.whistle_regions[i].start_sample;
            if (state.region_count == 0 || start_sample < state.first_start_sample) {
                state.first_start_sample = start_sample;
            }
            ++state.region_count;
            state.channels = group.channels;
            group.first_start_sample = state.first_start_sample;
            group.last_start_sample = start_sample;
        }

        RetainedWhistleRegion retained;
        retained.candidate = candidates[i];
        retained.channel = result.whistle_regions[i].channel;
        retained.group_id = group_id;
        retained.result_index = i;
        retained.from_current_chunk = true;
        whistle_group_history_.push_back(std::move(retained));
    }

    // Bound the history: PAMGuard's grouper stops scanning at matches older
    // than two seconds, so retaining far beyond that cannot change results.
    constexpr std::size_t max_history_regions = 256;
    while (whistle_group_history_.size() > max_history_regions) {
        whistle_group_history_.pop_front();
    }

    // Group state outlives a chunk but must not outlive the history that can
    // still extend the group, or a long session would accumulate one entry per
    // group ever formed.
    std::unordered_set<std::size_t> live_group_ids;
    for (const auto& retained : whistle_group_history_) {
        if (retained.group_id != 0) {
            live_group_ids.insert(retained.group_id);
        }
    }
    for (auto it = whistle_group_states_.begin(); it != whistle_group_states_.end();) {
        it = live_group_ids.count(it->first) == 0 ? whistle_group_states_.erase(it) : std::next(it);
    }
}

void AnalysisSession::classify_ici_click_trains(AnalysisResult& result) const {
    if (!config_.detector.click_train_classifier_enabled || result.click_trains.empty() ||
        config_.sample_rate_hz == 0) {
        return;
    }

    for (const auto& train : result.click_trains) {
        detectors::CtTrainSummary summary;
        // The ICI tracker has no MHT chi2. Zero passes the pre-classifier's
        // chi2 test (which only rejects when chi2 exceeds the threshold), so
        // the pre-classifier still gates on click count and duration.
        summary.chi2 = 0.0;
        summary.click_count = train.click_count;
        summary.duration_ms = train.duration_seconds * 1000.0;
        // The tracker's ICI statistics already have bitwise Java parity.
        summary.idi.median_idi = train.median_ici_seconds;
        summary.idi.mean_idi = train.mean_ici_seconds;
        summary.idi.std_idi = train.std_ici_seconds;

        std::vector<std::vector<double>> member_waveforms;
        for (const auto start_sample : train.click_start_samples) {
            for (const auto& click : result.clicks) {
                if (click.start_sample != start_sample) {
                    continue;
                }
                if (!click.waveform.empty()) {
                    member_waveforms.push_back(click.waveform.front());
                }
                break;
            }
            detectors::CtBearingClick bearing_click;
            bearing_click.time_ms = start_sample * 1000 / static_cast<std::int64_t>(config_.sample_rate_hz);
            for (const auto& bearing : result.click_bearings) {
                if (bearing.click_start_sample == start_sample && bearing.bearing.valid) {
                    bearing_click.bearing_radians =
                        bearing.bearing.azimuth_degrees * 3.141592653589793238462643383279502884 / 180.0;
                    break;
                }
            }
            summary.bearing_clicks.push_back(bearing_click);
        }
        if (!member_waveforms.empty()) {
            summary.average_spectrum = detectors::ct_train_average_spectrum(
                member_waveforms, config_.detector.click_train_average_spectrum_fft_length);
            summary.average_spectrum_sample_rate_hz = static_cast<double>(config_.sample_rate_hz);
        }

        const detectors::CtClassifierChain chain(config_.detector.click_train_pre_classifier,
                                                 build_ct_classifiers());
        const auto chain_result = chain.classify(summary);
        ClickTrainClassificationResult classification;
        classification.train_id = train.train_id;
        classification.junk_train = chain_result.junk_train;
        classification.species_id = chain_result.species_id;
        classification.classifier_species_ids = chain_result.classifications;
        classification.template_correlation = template_correlation_for(summary);
        result.click_train_classifications.push_back(std::move(classification));
    }
}

void AnalysisSession::classify_mht_train(MhtClickTrainResult& train, const MhtTrainState& state,
                                         const detectors::MhtBitset& track_bits) const {
    if (!config_.detector.click_train_classifier_enabled) {
        return;
    }

    detectors::CtTrainSummary summary;
    summary.chi2 = train.chi2;
    summary.click_count = train.click_count;
    summary.duration_ms = static_cast<double>(train.last_start_sample - train.first_start_sample)
        / static_cast<double>(config_.sample_rate_hz) * 1000.0;

    std::vector<double> idis;
    std::vector<std::vector<double>> member_waveforms;
    for (std::size_t bit = 0; bit < state.start_samples.size(); ++bit) {
        if (!track_bits.get(bit)) {
            continue;
        }
        if (bit < state.waveforms.size() && !state.waveforms[bit].empty()) {
            member_waveforms.push_back(state.waveforms[bit]);
        }
        detectors::CtBearingClick bearing_click;
        bearing_click.time_ms = state.time_ms[bit];
        if (bit < state.has_bearing.size() && state.has_bearing[bit]) {
            bearing_click.bearing_radians = state.bearings_radians[bit];
        }
        summary.bearing_clicks.push_back(bearing_click);
    }
    for (std::size_t i = 1; i < train.click_start_samples.size(); ++i) {
        idis.push_back(static_cast<double>(train.click_start_samples[i] - train.click_start_samples[i - 1])
            / static_cast<double>(config_.sample_rate_hz));
    }
    if (!idis.empty()) {
        summary.idi.median_idi = detectors::ct_median(idis);
        summary.idi.mean_idi = detectors::ct_mean(idis);
        summary.idi.std_idi = detectors::ct_std(idis);
    }
    if (!member_waveforms.empty()) {
        summary.average_spectrum = detectors::ct_train_average_spectrum(
            member_waveforms, config_.detector.click_train_average_spectrum_fft_length);
        summary.average_spectrum_sample_rate_hz = static_cast<double>(config_.sample_rate_hz);
    }

    train.template_correlation = template_correlation_for(summary);
    const detectors::CtClassifierChain chain(config_.detector.click_train_pre_classifier,
                                             build_ct_classifiers());
    const auto chain_result = chain.classify(summary);
    train.classified = true;
    train.junk_train = chain_result.junk_train;
    train.species_id = chain_result.species_id;
    train.classifier_species_ids = chain_result.classifications;
}

void AnalysisSession::drain_confirmed_mht_trains(AnalysisResult& result) {
    for (auto& [key, state] : mht_train_states_) {
        for (std::size_t i = state.consumed_confirmed; i < state.kernel->confirmed_track_count(); ++i) {
            const auto& track = state.kernel->confirmed_track(i);
            const auto click_count = track.bits.cardinality();
            if (click_count < config_.detector.click_train.min_clicks) {
                continue;
            }

            MhtClickTrainResult train;
            train.train_id = next_mht_train_id_++;
            train.channel_bitmap = key;
            train.chi2 = track.get_chi2();
            train.click_count = click_count;
            for (std::size_t bit = 0; bit < state.start_samples.size(); ++bit) {
                if (track.bits.get(bit)) {
                    train.click_start_samples.push_back(state.start_samples[bit]);
                    train.click_time_ms.push_back(state.time_ms[bit]);
                }
            }
            if (!train.click_start_samples.empty()) {
                train.first_start_sample = train.click_start_samples.front();
                train.last_start_sample = train.click_start_samples.back();
            }
            classify_mht_train(train, state, track.bits);
            result.mht_click_trains.push_back(std::move(train));
        }
        state.consumed_confirmed = state.kernel->confirmed_track_count();
    }
}

} // namespace pamguard::core
