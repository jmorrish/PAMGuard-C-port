#pragma once

#include <deque>
#include <optional>
#include <unordered_map>
#include <vector>

#include "pamguard/core/AnalysisConfig.h"
#include "pamguard/core/AudioFrame.h"
#include "pamguard/detectors/ClickDetectorEngine.h"
#include "pamguard/detectors/MhtKernel.h"
#include "pamguard/detectors/SimpleEchoDetector.h"
#include "pamguard/detectors/MhtSimpleChi2Vars.h"
#include "pamguard/detectors/WhistleDetectionGrouper.h"
#include "pamguard/detectors/ConnectedRegionTracker.h"
#include "pamguard/detectors/WhistlePeakDetector.h"
#include "pamguard/dsp/SpectrogramEngine.h"
#include "pamguard/localisation/BearingLocaliserSelector.h"
#include "pamguard/localisation/DelayGroupEstimator.h"
#include "pamguard/localisation/WorldVectors.h"
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
    /**
     * The same direction as a unit vector in the hydrophone array's xyz frame.
     * Unlike the grid localiser's, this needs **no** array-axis rotation:
     * LSQBearingLocaliser fits raw inter-hydrophone vectors, so its azimuth and
     * elevation are already in that frame, and PAMGuard's getPlanarVector
     * round-trips them back to the fitted unit vector exactly. Always one
     * vector; the ambiguity a plane sub-array carries is not expressed here.
     */
    std::vector<localisation::WorldVector> world_vectors;
    /** The same vectors rotated into the earth frame; empty when the array declares no orientation. */
    std::vector<localisation::WorldVector> earth_world_vectors;
};

/**
 * PAMGuard MLGridBearingLocaliser2 output. The angles are the reference's own
 * theta and phi, measured in the sub-array's principal axis frame rather than
 * as compass azimuth and elevation, so they are reported under those names
 * rather than converted.
 */
struct GridBearingResult {
    bool valid = false;
    double theta_radians = 0.0;
    double phi_radians = 0.0;
    double theta_error_radians = 0.0;
    double phi_error_radians = 0.0;
    /** False for a line sub-array, where the reference returns theta alone. */
    bool has_phi = false;
    std::size_t used_pairs = 0;
    /**
     * The same direction expressed as unit vectors in the hydrophone array's
     * xyz frame, via AbstractLocalisation.getWorldVectors. One vector for a
     * volume sub-array; two for a plane or line, carrying the mirror or
     * left/right ambiguity that shape cannot resolve.
     */
    std::vector<localisation::WorldVector> world_vectors;
    /** The same vectors rotated into the earth frame; empty when the array declares no orientation. */
    std::vector<localisation::WorldVector> earth_world_vectors;
};

struct ClickLocalisationResult {
    std::size_t click_index = 0;
    std::int64_t click_start_sample = 0;
    std::vector<localisation::ChannelPairDelay> delays;
    LsqBearingResult lsq_bearing;
    /** PAMGuard's selected localiser for a plane or volume sub-array. */
    GridBearingResult grid_bearing;
    /** Shape of the sub-array formed by this click's channels. */
    localisation::ArrayShapeType array_shape = localisation::ArrayShapeType::None;
    /** Localiser class PAMGuard's selector picks for that shape. */
    localisation::BearingLocaliserChoice bearing_localiser = localisation::BearingLocaliserChoice::None;
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
    /** PAMGuard's selected localiser for a plane or volume sub-array. */
    GridBearingResult grid_bearing;
    /** Shape of the sub-array formed by the group's channels. */
    localisation::ArrayShapeType array_shape = localisation::ArrayShapeType::None;
    /** Localiser class PAMGuard's selector picks for that shape. */
    localisation::BearingLocaliserChoice bearing_localiser = localisation::BearingLocaliserChoice::None;
};

/** Whistle contours on different channels associated as one call. */
struct WhistleRegionGroup {
    std::size_t group_id = 0;
    /**
     * Indices into AnalysisResult::whistle_regions — **this chunk's members
     * only**. A group whose contours completed in different chunks is reported
     * once per contributing chunk, so earlier members are not indexable here;
     * `earlier_region_count` says how many there were.
     */
    std::vector<std::size_t> region_indices;
    /** Every channel in the group, including earlier chunks' members. */
    std::vector<std::size_t> channels;
    /**
     * The group's true first sample, carried across chunks — not the first
     * sample of this chunk's earliest member.
     */
    std::int64_t first_start_sample = 0;
    std::int64_t last_start_sample = 0;
    /** Members that completed in earlier chunks and are not in region_indices. */
    std::size_t earlier_region_count = 0;
};

/**
 * One noise band measurement interval for one channel: ANSI band levels in
 * dB (calibrated via AcquisitionCalibration; uncalibrated-relative when no
 * sensitivity is configured), ascending frequency.
 */
struct NoiseBandResult {
    std::size_t channel = 0;
    std::int64_t end_sample = 0;
    std::int64_t time_unix_ms = 0;
    std::vector<double> rms_db;
    std::vector<double> peak_db;
};

/**
 * One completed LTSA averaging period for one channel (PAMGuard
 * LtsaDataUnit): RMS spectral magnitude per FFT bin, uncalibrated exactly
 * as the reference stores it.
 */
struct LtsaResult {
    std::size_t channel = 0;
    detectors::LtsaInterval interval;
};

/**
 * Matched-template classifier outcome for one click (the PAMGuard
 * MatchedClickAnnotation equivalent): the best result per template pair
 * across channels, plus the aggregated classification flag.
 */
struct MatchedTemplateClickResult {
    std::size_t click_index = 0;
    std::int64_t click_start_sample = 0;
    bool classified = false;
    std::vector<detectors::MtTemplateResult> results;
};

/** Classifier chain verdict for an ICI-tracker click train. */
struct ClickTrainClassificationResult {
    std::size_t train_id = 0;
    bool junk_train = false;
    int species_id = 0;
    std::vector<int> classifier_species_ids;
    double template_correlation = 0.0;
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
    /** Populated when the classifier chain runs for this session. */
    bool classified = false;
    bool junk_train = false;
    int species_id = 0;
    std::vector<int> classifier_species_ids;
    double template_correlation = 0.0;
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
    std::vector<ClickTrainClassificationResult> click_train_classifications;
    std::vector<WhistleRegionGroup> whistle_groups;
    std::vector<NoiseBandResult> noise_bands;
    std::vector<LtsaResult> ltsa;
    std::vector<detectors::IshmaelDetection> ishmael_detections;
    std::vector<detectors::IshmaelDetection> sgram_corr_detections;
    std::vector<MatchedTemplateClickResult> matched_template_classifications;
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
        /** Per-click data the classifier chain needs, parallel to the above. */
        std::vector<std::vector<double>> waveforms;
        std::vector<double> bearings_radians;
        std::vector<bool> has_bearing;
        std::size_t consumed_confirmed = 0;
    };
    std::unordered_map<std::uint32_t, MhtTrainState> mht_train_states_;
    std::size_t next_mht_train_id_ = 1;

    /**
     * Whistle regions completed in earlier chunks, so cross-channel grouping
     * can associate contours that finish in different chunks — PAMGuard's
     * grouper scans a live data block spanning earlier processing.
     */
    struct RetainedWhistleRegion {
        detectors::WhistleGroupCandidate candidate;
        std::size_t channel = 0;
        std::size_t group_id = 0;
        /**
         * Where this region sits in the current result, valid only while
         * `from_current_chunk` holds. A region processed earlier in this same
         * chunk can be pulled into a group by a later one, and when that
         * happens it belongs in the group's region_indices rather than being
         * counted as an earlier-chunk member.
         */
        std::size_t result_index = 0;
        bool from_current_chunk = false;
    };
    std::deque<RetainedWhistleRegion> whistle_group_history_;
    std::size_t next_whistle_group_id_ = 1;
    /**
     * Per-group facts that outlive the chunk a group was formed in, so a group
     * re-reported in a later chunk still describes its whole self rather than
     * only its newest members. Keyed by group id.
     */
    struct WhistleGroupState {
        std::int64_t first_start_sample = 0;
        std::size_t region_count = 0;
        std::vector<std::size_t> channels;
    };
    std::unordered_map<std::size_t, WhistleGroupState> whistle_group_states_;
    /**
     * The array attitude in force: the session's static orientation until a
     * chunk declares one, then the most recent declaration. Geometry stays
     * static; only the earth-frame rotation follows it.
     */
    ArrayOrientation current_orientation_;
    /** Cross-chunk echo state: the anchor click survives chunk boundaries. */
    std::optional<detectors::SimpleEchoDetector> echo_detector_;
    /** Per-channel state lives inside; one reducer serves the session. */
    std::optional<detectors::SpectrogramNoiseReducer> whistle_noise_reducer_;
    /** One per channel, keyed by channel number. */
    std::unordered_map<std::size_t, detectors::NoiseBandMonitor> noise_band_monitors_;
    /** One per channel, keyed by channel number (LtsaProcess.ChannelProcess). */
    std::unordered_map<std::size_t, detectors::LtsaMonitor> ltsa_monitors_;
    /**
     * ONE energy sum for the session, not one per channel: the reference's
     * noise floor and smoothing state are single fields shared across every
     * channel the process serves. The peak picker keeps per-channel state
     * internally, as IshPeakProcess does.
     */
    std::optional<detectors::IshmaelEnergySum> ishmael_energy_;
    std::optional<detectors::IshmaelPeakPicker> ishmael_picker_;
    /** One per session; per-classifier template FFTs cache inside. */
    std::optional<detectors::MatchedTemplateClassifier> matched_template_classifier_;
    /** Kernel shared across channels; per-channel gram buffers inside. */
    std::optional<detectors::SgramCorrDetector> sgram_corr_detector_;
    std::optional<detectors::IshmaelPeakPicker> sgram_corr_picker_;

    [[nodiscard]] bool whistle_delays_enabled() const;
    void retain_whistle_fft_frame(const dsp::SpectrogramFrame& frame);
    void compute_whistle_delays(AnalysisResult& result);
    void process_mht_click_trains(AnalysisResult& result);
    void drain_confirmed_mht_trains(AnalysisResult& result);
    void classify_mht_train(MhtClickTrainResult& train, const MhtTrainState& state,
                            const detectors::MhtBitset& track_bits) const;
    void classify_ici_click_trains(AnalysisResult& result) const;
    void group_whistle_regions(AnalysisResult& result);
    [[nodiscard]] std::vector<std::shared_ptr<const detectors::CtClassifier>> build_ct_classifiers() const;
    [[nodiscard]] double template_correlation_for(const detectors::CtTrainSummary& summary) const;
};

} // namespace pamguard::core
