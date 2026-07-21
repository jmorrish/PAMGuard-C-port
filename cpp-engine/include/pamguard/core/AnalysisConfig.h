#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "pamguard/detectors/ClickDetectorEngine.h"
#include "pamguard/detectors/ClickFeatureExtractor.h"
#include "pamguard/detectors/BasicClickClassifier.h"
#include "pamguard/detectors/ClickTrainTracker.h"
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
    double x_m = 0.0;
    double y_m = 0.0;
    double z_m = 0.0;
    double sensitivity_db = 0.0;
};

struct ArrayConfiguration {
    std::string id;
    std::vector<ArrayHydrophone> hydrophones;
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
    bool whistle_peak_detector_enabled = false;
    detectors::WhistlePeakConfig whistle_peak;
    bool whistle_region_detector_enabled = false;
    detectors::ConnectedRegionConfig whistle_region;
    std::vector<ChannelGroup> channel_groups;
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
};

} // namespace pamguard::core
