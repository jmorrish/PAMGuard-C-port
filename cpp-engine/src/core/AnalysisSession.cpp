#include "pamguard/core/AnalysisSession.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <utility>

#include "pamguard/detectors/StandardMhtChi2.h"
#include "pamguard/localisation/ArrayShape.h"
#include "pamguard/localisation/LsqBearingLocaliser.h"
#include "pamguard/localisation/PairBearingLocaliser.h"
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

void attach_pair_geometry(std::vector<localisation::ChannelPairDelay>& delays, const std::vector<ClickPairGeometry>& geometry, const AnalysisConfig& config) {
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
            const auto* hydrophone_a = find_hydrophone(config.array, geometry[i].audio_channel_a);
            const auto* hydrophone_b = find_hydrophone(config.array, geometry[i].audio_channel_b);
            if (hydrophone_a != nullptr && hydrophone_b != nullptr) {
                const auto directions = localisation::array_directions({
                    {hydrophone_a->x_m, hydrophone_a->y_m, hydrophone_a->z_m},
                    {hydrophone_b->x_m, hydrophone_b->y_m, hydrophone_b->z_m},
                });
                if (!directions.empty()) {
                    const double axis_dot = geometry[i].baseline_x_m * directions[0][0] +
                        geometry[i].baseline_y_m * directions[0][1] +
                        geometry[i].baseline_z_m * directions[0][2];
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
            }
        }
    }
}

void attach_lsq_bearing(ClickLocalisationResult& localisation, const std::vector<ClickPairGeometry>& geometry,
                        std::size_t click_channel_count, const AnalysisConfig& config) {
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
    }
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

AnalysisSession::AnalysisSession(AnalysisConfig config)
    : config_(std::move(config)),
      spectrogram_(config_.detector.fft) {
    if (config_.detector.click_detector_enabled) {
        click_detector_.emplace(config_.detector.click);
    }
    if (config_.detector.click_detector_enabled && config_.detector.click_features_enabled) {
        auto click_feature_config = config_.detector.click_features;
        click_feature_config.sample_rate_hz = config_.sample_rate_hz;
        if (click_feature_config.fft_length == 0) {
            click_feature_config.fft_length = config_.detector.fft.fft_length;
        }
        click_feature_extractor_.emplace(std::move(click_feature_config));
    }
    if (config_.detector.click_detector_enabled && config_.detector.click_basic_classifier_enabled) {
        auto click_classifier_config = config_.detector.click_basic_classifier;
        click_classifier_config.sample_rate_hz = config_.sample_rate_hz;
        click_basic_classifier_.emplace(std::move(click_classifier_config));
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

        if (config_.detector.fft.channels.empty()) {
            for (std::size_t channel = 0; channel < config_.channel_count; ++channel) {
                region_config.channel = channel;
                whistle_region_trackers_.try_emplace(channel, region_config);
            }
        }
        else {
            for (const auto channel : config_.detector.fft.channels) {
                region_config.channel = channel;
                whistle_region_trackers_.try_emplace(channel, region_config);
            }
        }
    }
}

const AnalysisConfig& AnalysisSession::config() const noexcept {
    return config_;
}

AnalysisResult AnalysisSession::process(const AudioChunk& chunk) {
    AnalysisResult result;
    result.spectrogram_frames = spectrogram_.process(chunk);
    if (click_detector_) {
        result.clicks = click_detector_->process(chunk);
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
    if (click_basic_classifier_) {
        for (std::size_t i = 0; i < result.clicks.size(); ++i) {
            if (result.clicks[i].waveform.empty()) {
                continue;
            }
            auto classification = click_basic_classifier_->identify(result.clicks[i]);
            classification.click_index = i;
            result.click_classifications.push_back(classification);
        }
    }
    if (click_train_tracker_) {
        result.click_trains = click_train_tracker_->process(result.clicks);
    }
    if (config_.detector.click_localisation_enabled) {
        for (std::size_t i = 0; i < result.clicks.size(); ++i) {
            const auto& click = result.clicks[i];
            if (click.waveform.size() < 2) {
                continue;
            }
            ClickLocalisationResult localisation;
            localisation.click_index = i;
            localisation.click_start_sample = click.start_sample;
            const auto pair_geometry = click_pair_geometry(config_, click.channels);
            localisation.delays = click_delay_estimator_.estimate_delays(
                click.waveform,
                max_delay_samples_from_geometry(pair_geometry));
            attach_pair_geometry(localisation.delays, pair_geometry, config_);
            attach_lsq_bearing(localisation, pair_geometry, click.channels.size(), config_);
            result.click_localisations.push_back(std::move(localisation));
            if (click_bearing_localiser_) {
                ClickBearingResult bearing;
                bearing.click_index = i;
                bearing.click_start_sample = click.start_sample;
                bearing.bearing = click_bearing_localiser_->estimate(
                    result.click_localisations.back().delays,
                    click.channels,
                    i,
                    click.start_sample);
                if (bearing.bearing.valid) {
                    result.click_bearings.push_back(std::move(bearing));
                }
            }
        }
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
    if (!whistle_peak_detectors_.empty() || !whistle_region_trackers_.empty()) {
        for (const auto& frame : result.spectrogram_frames) {
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
                for (const auto& peak : frame_peaks) {
                    const auto hi = std::min<std::size_t>(peak.max_freq, active_bins.size() - 1);
                    for (std::size_t bin = peak.min_freq; bin <= hi; ++bin) {
                        active_bins[bin] = true;
                    }
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
    for (auto& [_, tracker] : whistle_region_trackers_) {
        auto regions = tracker.flush();
        result.whistle_regions.insert(result.whistle_regions.end(), regions.begin(), regions.end());
    }
    compute_whistle_delays(result);
    return result;
}

bool AnalysisSession::whistle_delays_enabled() const {
    return whistle_region_trackers_.size() >= 2 && config_.array.hydrophones.size() >= 2 &&
        config_.array.speed_of_sound_mps > 0.0 && config_.sample_rate_hz != 0 &&
        config_.detector.fft.fft_length >= 4;
}

void AnalysisSession::retain_whistle_fft_frame(const dsp::SpectrogramFrame& frame) {
    if (!whistle_delays_enabled()) {
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

    std::vector<std::size_t> whistle_channels;
    whistle_channels.reserve(whistle_region_trackers_.size());
    for (const auto& [channel, _] : whistle_region_trackers_) {
        whistle_channels.push_back(channel);
    }
    std::sort(whistle_channels.begin(), whistle_channels.end());

    const auto fft_length = config_.detector.fft.fft_length;
    for (const auto& region : result.whistle_regions) {
        WhistleRegionDelayResult delay_result;
        delay_result.channel = region.channel;
        delay_result.region_number = region.region_number;
        delay_result.start_sample = region.start_sample;

        for (const auto other_channel : whistle_channels) {
            if (other_channel == region.channel) {
                continue;
            }
            const auto channel_a = std::min(region.channel, other_channel);
            const auto channel_b = std::max(region.channel, other_channel);
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
            pair_delays[0].pair_index = delay_result.delays.size();
            pair_delays[0].channel_a = 0;
            pair_delays[0].channel_b = 1;
            pair_delays[0].delay = estimator.get_delay(pair_geometry[0].max_delay_samples);
            attach_pair_geometry(pair_delays, pair_geometry, config_);
            pair_delays[0].pair_index = delay_result.delays.size();
            delay_result.delays.push_back(std::move(pair_delays[0]));
        }

        if (!delay_result.delays.empty()) {
            // WhistleBearingInfo equivalent: the group's bearing localiser
            // result for the region. With a two-element group PAMGuard uses
            // the pair localiser, whose single cone angle carries the
            // left/right ambiguity flag.
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

        state.kernel->add_detection(unit);
        state.start_samples.push_back(click.start_sample);
        state.time_ms.push_back(click.time_unix_ms);

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
            result.mht_click_trains.push_back(std::move(train));
        }
        state.consumed_confirmed = state.kernel->confirmed_track_count();
    }
}

} // namespace pamguard::core
