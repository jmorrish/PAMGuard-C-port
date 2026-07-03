#include "pamguard/core/AnalysisSession.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <utility>

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

void attach_pair_geometry(std::vector<localisation::ChannelPairDelay>& delays, const std::vector<ClickPairGeometry>& geometry) {
    if (delays.size() != geometry.size()) {
        return;
    }
    for (std::size_t i = 0; i < delays.size(); ++i) {
        delays[i].audio_channel_a = geometry[i].audio_channel_a;
        delays[i].audio_channel_b = geometry[i].audio_channel_b;
        delays[i].geometry_constrained = geometry[i].constrained;
        delays[i].max_delay_samples = geometry[i].max_delay_samples;
        delays[i].hydrophone_distance_m = geometry[i].hydrophone_distance_m;
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
                }
            }
        }

        for (auto& pair : summary.pair_delays) {
            if (pair.delay_count > 0) {
                pair.mean_delay_samples /= static_cast<double>(pair.delay_count);
                pair.mean_delay_score /= static_cast<double>(pair.delay_count);
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
    if (config_.detector.click_detector_enabled && config_.detector.click_train_tracker_enabled) {
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
            attach_pair_geometry(localisation.delays, pair_geometry);
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
    if (!whistle_peak_detectors_.empty() || !whistle_region_trackers_.empty()) {
        for (const auto& frame : result.spectrogram_frames) {
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
    return result;
}

AnalysisResult AnalysisSession::flush() {
    AnalysisResult result;
    if (click_train_tracker_) {
        result.click_trains = click_train_tracker_->flush();
    }
    for (auto& [_, tracker] : whistle_region_trackers_) {
        auto regions = tracker.flush();
        result.whistle_regions.insert(result.whistle_regions.end(), regions.begin(), regions.end());
    }
    return result;
}

std::vector<dsp::SpectrogramFrame> AnalysisSession::process_audio(const AudioChunk& chunk) {
    return process(chunk).spectrogram_frames;
}

} // namespace pamguard::core
