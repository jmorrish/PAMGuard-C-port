#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "pamguard/detectors/ClickDetectorEngine.h"
#include "pamguard/detectors/IshmaelDetector.h"
#include "pamguard/detectors/LtsaMonitor.h"
#include "pamguard/detectors/MatchedTemplateClassifier.h"
#include "pamguard/detectors/SgramCorrDetector.h"
#include "pamguard/detectors/NoiseBandMonitor.h"
#include "pamguard/detectors/SpectrogramNoiseReducer.h"
#include "pamguard/detectors/ClickFeatureExtractor.h"
#include "pamguard/detectors/BasicClickClassifier.h"
#include "pamguard/detectors/ClickTrainTracker.h"
#include "pamguard/detectors/CtClassifiers.h"
#include "pamguard/detectors/StandardMhtChi2.h"
#include "pamguard/detectors/ConnectedRegionTracker.h"
#include "pamguard/detectors/WhistlePeakDetector.h"
#include "pamguard/dsp/WindowFunction.h"

namespace pamguard::core {

struct ChannelGroup {
    std::string name;
    std::vector<std::size_t> channels;
};

struct ArrayHydrophone {
    std::size_t channel = 0;
    /** Coordinates relative to the hydrophone's streamer origin. */
    double x_m = 0.0;
    double y_m = 0.0;
    double z_m = 0.0;
    double sensitivity_db = 0.0;
    /** Streamer this hydrophone belongs to; 0 when the array has one. */
    int streamer_id = 0;
    /**
     * PAMGuard Hydrophone.getCoordinateErrors: per-axis position uncertainty,
     * used by the ML grid localiser to weight each grid bin. Zero means the
     * position is treated as exact.
     */
    double x_error_m = 0.0;
    double y_error_m = 0.0;
    double z_error_m = 0.0;
    /** Hydrophone.preampGain, part of the calibration constant. */
    double preamp_gain_db = 0.0;
};

/**
 * A towed or moored streamer carrying hydrophones. PAMGuard's
 * HydrophoneLocator.getPhoneLatLong rotates a hydrophone's coordinates by
 * its streamer's heading/pitch/roll quaternion and then offsets by the
 * streamer position; PamArray.getAbsHydrophoneVector performs only the
 * translation half of that. Angles are degrees, and all-zero angles give the
 * translation-only behaviour.
 */
struct ArrayStreamer {
    int id = 0;
    double x_m = 0.0;
    double y_m = 0.0;
    double z_m = 0.0;
    double heading_degrees = 0.0;
    double pitch_degrees = 0.0;
    double roll_degrees = 0.0;
};

/**
 * The array's orientation in the world, which PAMGuard reads from GPS as
 * `GpsData.getQuaternion()` and uses to turn array-frame directions into earth
 * frame ones. Undeclared means directions stay in the array frame, matching
 * the reference's behaviour when it finds no origin position.
 */
struct ArrayOrientation {
    bool declared = false;
    double heading_degrees = 0.0;
    double pitch_degrees = 0.0;
    double roll_degrees = 0.0;
};

struct ArrayConfiguration {
    std::string id;
    /** World-relative orientation of the whole array; see ArrayOrientation. */
    ArrayOrientation orientation;
    std::vector<ArrayHydrophone> hydrophones;
    std::vector<ArrayStreamer> streamers;
    double speed_of_sound_mps = 1500.0;
    double speed_of_sound_error_mps = 0.0;
    double timing_error_seconds = 0.0;
    double spacing_error_m = 0.0;
    double wobble_radians = 0.0;
};

struct FftConfig {
    std::size_t fft_length = 1024;
    std::size_t fft_hop = 512;
    std::vector<std::size_t> channels;
    dsp::WindowType window_type = dsp::WindowType::Hann;
    bool click_removal = false;
    double click_threshold = 5.0;
    int click_power = 6;
};

struct DetectorConfig {
    std::string id;
    FftConfig fft;
    bool click_detector_enabled = false;
    detectors::ClickDetectorConfig click;
    /**
     * PAMGuard's online echo detection gate (ClickParameters.runEchoOnline /
     * discardEchoes + SimpleEchoParams.maxIntervalSeconds). When running, a
     * discarded echo never reaches any downstream consumer — features,
     * classifier, trains, localisation — exactly as the reference's early
     * return keeps it out of everything.
     */
    bool click_echo_enabled = false;
    bool click_echo_discard = false;
    double click_echo_max_interval_seconds = 0.1;
    bool click_localisation_enabled = false;
    bool click_features_enabled = false;
    detectors::ClickFeatureConfig click_features;
    bool click_basic_classifier_enabled = false;
    detectors::BasicClickClassifierConfig click_basic_classifier;
    bool click_train_tracker_enabled = false;
    /** False: PAMGuard-offline-style max-ICI tracker. True: the ported MHT stack. */
    bool click_train_mht = false;
    detectors::ClickTrainConfig click_train;
    detectors::StandardMhtChi2Params click_train_mht_chi2;
    detectors::MhtKernelParams click_train_mht_kernel;
    /** Runs PAMGuard's click train classifier chain over MHT trains. */
    bool click_train_classifier_enabled = false;
    detectors::CtChi2ClassifierConfig click_train_pre_classifier;
    bool click_train_idi_classifier_enabled = false;
    detectors::CtIdiClassifierConfig click_train_idi_classifier;
    bool click_train_bearing_classifier_enabled = false;
    detectors::CtBearingClassifierConfig click_train_bearing_classifier;
    bool click_train_template_classifier_enabled = false;
    detectors::CtTemplateClassifierConfig click_train_template_classifier;
    /** FFT length for the train average spectrum the template classifier reads. */
    std::size_t click_train_average_spectrum_fft_length = 256;
    /**
     * PAMGuard's spectrogram noise reduction chain, feeding the whistle path
     * exactly as SpectrogramNoiseProcess sits between the FFT and
     * WhistleToneConnectProcess. When active it also feeds the retained FFT
     * history, because WhistleDelays correlates on the noise-reduced block.
     */
    detectors::SpectrogramNoiseConfig whistle_noise;
    /** PAMGuard noiseBandMonitor: octave-family band noise levels. */
    detectors::NoiseBandConfig noise_band;
    /** PAMGuard ltsa: long-term spectral average over the FFT stream. */
    detectors::LtsaConfig ltsa;
    /** PAMGuard IshmaelDetector energy sum + peak picker. */
    detectors::IshmaelEnergySumConfig ishmael;
    /** PAMGuard matched-template click classifier. */
    detectors::MatchedTemplateClassifierConfig matched_template;
    /** PAMGuard Ishmael spectrogram correlation detector. */
    detectors::SgramCorrConfig sgram_corr;
    bool whistle_peak_detector_enabled = false;
    detectors::WhistlePeakConfig whistle_peak;
    bool whistle_region_detector_enabled = false;
    detectors::ConnectedRegionConfig whistle_region;
    std::vector<ChannelGroup> channel_groups;
};

/**
 * Acquisition calibration (AcquisitionParameters + AcquisitionProcess):
 * rawAmplitude2dB converts a -1..1 sample to dB re 1 uPa as
 * 20*log10(raw * voltsPeak2Peak / 2) - (hydrophone sensitivity +
 * hydrophone preamp gain + system preamp gain). The defaults give
 * uncalibrated relative dB (20*log10(raw)) when nothing is configured.
 */
struct AcquisitionCalibration {
    double volts_peak_to_peak = 2.0;
    double preamp_gain_db = 0.0;
};

struct AnalysisConfig {
    std::string session_id;
    std::string source_id;
    std::string owner_id;
    std::string tenant_id;
    std::uint32_t sample_rate_hz = 0;
    std::size_t channel_count = 0;
    DetectorConfig detector;
    ArrayConfiguration array;
    AcquisitionCalibration acquisition;
};

} // namespace pamguard::core
