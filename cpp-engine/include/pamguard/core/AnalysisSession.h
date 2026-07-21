#pragma once

#include <deque>
#include <optional>
#include <unordered_map>
#include <vector>

#include "pamguard/core/AnalysisConfig.h"
#include "pamguard/core/AudioFrame.h"
#include "pamguard/detectors/ClickDetectorEngine.h"
#include "pamguard/detectors/MhtKernel.h"
#include "pamguard/detectors/MhtSimpleChi2Vars.h"
#include "pamguard/detectors/ConnectedRegionTracker.h"
#include "pamguard/detectors/WhistlePeakDetector.h"
#include "pamguard/dsp/SpectrogramEngine.h"
#include "pamguard/localisation/DelayGroupEstimator.h"
#include "pamguard/localisation/FarFieldBearingLocaliser.h"

namespace pamguard::core {

/** LSQ bearing summary, shared by click and whistle localisation outputs. */
struct LsqBearingResult {
    bool valid = false;
    double azimuth_radians = 0.0;
    double elevation_radians = 0.0;
    double azimuth_error_radians = 0.0;
    double elevation_error_radians = 0.0;
    std::size_t used_pairs = 0;
};

struct ClickLocalisationResult {
    std::size_t click_index = 0;
    std::int64_t click_start_sample = 0;
    std::vector<localisation::ChannelPairDelay> delays;
    LsqBearingResult lsq_bearing;
};

struct ClickBearingResult {
    std::size_t click_index = 0;
    std::int64_t click_start_sample = 0;
    localisation::FarFieldBearingResult bearing;
};

struct ClickTrainBearingSummary {
    std::size_t train_id = 0;
    std::uint32_t channel_bitmap = 0;
    std::int64_t first_start_sample = 0;
    std::int64_t last_start_sample = 0;
    std::size_t click_count = 0;
    std::size_t bearing_count = 0;
    bool valid = false;
    double unit_x = 0.0;
    double unit_y = 0.0;
    double unit_z = 0.0;
    double azimuth_degrees = 0.0;
    double elevation_degrees = 0.0;
    double mean_residual_rms_seconds = 0.0;
};

struct ClickTrainPairDelaySummary {
    std::size_t pair_index = 0;
    std::size_t channel_a = 0;
    std::size_t channel_b = 0;
    std::size_t audio_channel_a = 0;
    std::size_t audio_channel_b = 0;
    bool geometry_constrained = false;
    double max_delay_samples = 0.0;
    double hydrophone_distance_m = 0.0;
    std::size_t delay_count = 0;
    double mean_delay_samples = 0.0;
    double mean_delay_score = 0.0;
    std::size_t pair_bearing_count = 0;
    double mean_pair_bearing_radians = 0.0;
};

struct ClickTrainLocalisationSummary {
    std::size_t train_id = 0;
    std::uint32_t channel_bitmap = 0;
    std::int64_t first_start_sample = 0;
    std::int64_t last_start_sample = 0;
    std::size_t click_count = 0;
    std::size_t localisation_count = 0;
    bool valid = false;
    std::vector<ClickTrainPairDelaySummary> pair_delays;
};

struct WhistleRegionDelayResult {
    std::size_t channel = 0;
    std::size_t region_number = 0;
    std::int64_t start_sample = 0;
    std::vector<localisation::ChannelPairDelay> delays;
    /**
     * Region-level bearing, mirroring PAMGuard's WhistleBearingInfo: the
     * angle set produced by the channel group's bearing localiser, with the
     * ambiguity flag set when that localiser yields a single angle (the pair
     * localiser's cone angle) rather than an azimuth/elevation pair.
     */
    bool bearing_valid = false;
    double bearing_radians = 0.0;
    double bearing_error_radians = 0.0;
    bool bearing_ambiguity = false;
    std::size_t bearing_pair_count = 0;
    /** Populated for groups with four or more fully-geometry hydrophones. */
    LsqBearingResult lsq_bearing;
};

struct MhtClickTrainResult {
    std::size_t train_id = 0;
    std::uint32_t channel_bitmap = 0;
    double chi2 = 0.0;
    std::size_t click_count = 0;
    std::int64_t first_start_sample = 0;
    std::int64_t last_start_sample = 0;
    std::vector<std::int64_t> click_start_samples;
    std::vector<std::int64_t> click_time_ms;
};

struct AnalysisResult {
    std::vector<dsp::SpectrogramFrame> spectrogram_frames;
    std::vector<detectors::ClickDetectionResult> clicks;
    std::vector<detectors::ClickFeatureResult> click_features;
    std::vector<detectors::ClickClassificationResult> click_classifications;
    std::vector<detectors::ClickTrainSummary> click_trains;
    std::vector<ClickLocalisationResult> click_localisations;
    std::vector<ClickTrainLocalisationSummary> click_train_localisations;
    std::vector<ClickBearingResult> click_bearings;
    std::vector<ClickTrainBearingSummary> click_train_bearings;
    std::vector<detectors::WhistlePeak> whistle_peaks;
    std::vector<detectors::ConnectedRegionResult> whistle_regions;
    std::vector<WhistleRegionDelayResult> whistle_delays;
    std::vector<MhtClickTrainResult> mht_click_trains;
};

[[nodiscard]] std::vector<ClickTrainBearingSummary> summarize_click_train_bearings(
    const std::vector<detectors::ClickTrainSummary>& trains,
    const std::vector<ClickBearingResult>& bearings);

[[nodiscard]] std::vector<ClickTrainLocalisationSummary> summarize_click_train_localisations(
    const std::vector<detectors::ClickTrainSummary>& trains,
    const std::vector<ClickLocalisationResult>& localisations);

class AnalysisSession {
public:
    explicit AnalysisSession(AnalysisConfig config);

    [[nodiscard]] const AnalysisConfig& config() const noexcept;

    AnalysisResult process(const AudioChunk& chunk);
    AnalysisResult flush();
    std::vector<dsp::SpectrogramFrame> process_audio(const AudioChunk& chunk);

private:
    AnalysisConfig config_;
    dsp::SpectrogramEngine spectrogram_;
    std::optional<detectors::ClickDetectorEngine> click_detector_;
    std::optional<detectors::ClickFeatureExtractor> click_feature_extractor_;
    std::optional<detectors::BasicClickClassifier> click_basic_classifier_;
    std::optional<detectors::ClickTrainTracker> click_train_tracker_;
    localisation::DelayGroupEstimator click_delay_estimator_;
    std::optional<localisation::FarFieldBearingLocaliser> click_bearing_localiser_;
    std::unordered_map<std::size_t, detectors::WhistlePeakDetector> whistle_peak_detectors_;
    std::unordered_map<std::size_t, detectors::ConnectedRegionTracker> whistle_region_trackers_;
    std::unordered_map<std::size_t, std::deque<dsp::SpectrogramFrame>> whistle_fft_history_;

    struct MhtTrainState {
        std::unique_ptr<detectors::MhtKernel<detectors::MhtChi2Unit>> kernel;
        std::vector<std::int64_t> start_samples;
        std::vector<std::int64_t> time_ms;
        std::size_t consumed_confirmed = 0;
    };
    std::unordered_map<std::uint32_t, MhtTrainState> mht_train_states_;
    std::size_t next_mht_train_id_ = 1;

    [[nodiscard]] bool whistle_delays_enabled() const;
    void retain_whistle_fft_frame(const dsp::SpectrogramFrame& frame);
    void compute_whistle_delays(AnalysisResult& result);
    void process_mht_click_trains(AnalysisResult& result);
    void drain_confirmed_mht_trains(AnalysisResult& result);
};

} // namespace pamguard::core
