#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include "pamguard/core/DataModel.h"
#include "pamguard/core/ModuleNode.h"
#include "pamguard/detectors/ClickDetectorEngine.h"
#include "pamguard/detectors/ClickFeatureExtractor.h"
#include "pamguard/detectors/ClickTrainTracker.h"
#include "pamguard/detectors/BasicClickClassifier.h"
#include "pamguard/detectors/SimpleEchoDetector.h"
#include "pamguard/detectors/SweepClickClassifier.h"
#include "pamguard/detectors/MatchedTemplateClassifier.h"
#include "pamguard/detectors/CtClassifiers.h"
#include "pamguard/detectors/MhtKernel.h"
#include "pamguard/detectors/MhtSimpleChi2Vars.h"
#include "pamguard/detectors/StandardMhtChi2.h"
#include "pamguard/core/LocalisationData.h"

namespace pamguard::core {

inline constexpr const char* kClickDataType = "pamguard.click";
inline constexpr const char* kClickNoiseDataType = "pamguard.click-noise";
inline constexpr const char* kClickTriggerBackgroundDataType =
    "pamguard.click-trigger-background";
inline constexpr const char* kClickTriggerFunctionDataType =
    "pamguard.click-trigger-function";
inline constexpr const char* kClickFeatureDataType = "pamguard.click-feature";
inline constexpr const char* kClickTrainDataType = "pamguard.click-train";
inline constexpr const char* kClickClassificationDataType =
    "pamguard.click-classification";
inline constexpr const char* kMatchedTemplateClassificationDataType =
    "pamguard.matched-template-classification";
inline constexpr const char* kMhtClickTrainDataType =
    "pamguard.mht-click-train";
inline constexpr const char* kClickTrainClassificationDataType =
    "pamguard.click-train-classification";

struct ClickDetectorNodeOutputs {
    std::shared_ptr<DataBlock> clicks;
    std::shared_ptr<DataBlock> noise_samples;
    std::shared_ptr<DataBlock> trigger_background;
    std::shared_ptr<DataBlock> trigger_function;
};

/**
 * ClickControl owns one ChannelGroupDetector, and therefore one independent
 * trigger and SimpleEchoDetector state machine, per configured channel group.
 * The low-level detector settings remain common; this node-level wrapper owns
 * the grouped-source and online echo process semantics.
 */
struct ClickDetectorNodeConfig {
    detectors::ClickDetectorConfig detector;
    /** Empty means one group containing detector.channel_bitmap. */
    std::vector<std::uint32_t> channel_group_bitmaps;
    bool run_echo_online = false;
    bool discard_echoes = false;
    double echo_max_interval_seconds = 0.1;
};

class ClickDetectorNode final : public ModuleNode {
public:
    ClickDetectorNode(
        std::string instance_id,
        ClickDetectorNodeConfig config,
        std::shared_ptr<DataBlock> input,
        ClickDetectorNodeOutputs outputs);

    /** Compatibility overload: one group and no online echo processing. */
    ClickDetectorNode(
        std::string instance_id,
        detectors::ClickDetectorConfig config,
        std::shared_ptr<DataBlock> input,
        ClickDetectorNodeOutputs outputs);
    ~ClickDetectorNode() override;

    [[nodiscard]] const std::string& instance_id() const noexcept override;
    [[nodiscard]] ModuleState state() const noexcept override;
    void prepare() override;
    void start() override;
    void stop() override;
    void reset() override;

private:
    void process(const DataUnit& unit);

    std::string instance_id_;
    ClickDetectorNodeConfig config_;
    std::shared_ptr<DataBlock> input_;
    ClickDetectorNodeOutputs outputs_;
    std::vector<std::unique_ptr<detectors::ClickDetectorEngine>> engines_;
    std::vector<std::optional<detectors::SimpleEchoDetector>>
        echo_detectors_;
    Subscription subscription_;
    ModuleState state_ = ModuleState::Created;
    std::uint64_t next_uid_ = 1;
};

class ClickFeatureNode final : public ModuleNode {
public:
    ClickFeatureNode(
        std::string instance_id,
        detectors::ClickFeatureConfig config,
        std::shared_ptr<DataBlock> input,
        std::shared_ptr<DataBlock> output);
    ~ClickFeatureNode() override;

    [[nodiscard]] const std::string& instance_id() const noexcept override;
    [[nodiscard]] ModuleState state() const noexcept override;
    void prepare() override;
    void start() override;
    void stop() override;
    void reset() override;

private:
    void process(const DataUnit& unit);

    std::string instance_id_;
    detectors::ClickFeatureConfig config_;
    std::shared_ptr<DataBlock> input_;
    std::shared_ptr<DataBlock> output_;
    std::unique_ptr<detectors::ClickFeatureExtractor> extractor_;
    Subscription subscription_;
    ModuleState state_ = ModuleState::Created;
};

struct ClickTrainNodeConfig {
    bool enabled = false;
    detectors::ClickTrainConfig tracker;
};

class ClickTrainNode final : public ModuleNode {
public:
    ClickTrainNode(
        std::string instance_id,
        ClickTrainNodeConfig config,
        std::shared_ptr<DataBlock> input,
        std::shared_ptr<DataBlock> output);
    /** Compatibility overload for callers that explicitly create a tracker. */
    ClickTrainNode(
        std::string instance_id,
        detectors::ClickTrainConfig config,
        std::shared_ptr<DataBlock> input,
        std::shared_ptr<DataBlock> output);
    ~ClickTrainNode() override;

    [[nodiscard]] const std::string& instance_id() const noexcept override;
    [[nodiscard]] ModuleState state() const noexcept override;
    void prepare() override;
    void start() override;
    void stop() override;
    void flush() override;
    void reset() override;

private:
    void process(const DataUnit& unit);
    void publish(std::vector<detectors::ClickTrainSummary> trains);

    std::string instance_id_;
    ClickTrainNodeConfig config_;
    std::shared_ptr<DataBlock> input_;
    std::shared_ptr<DataBlock> output_;
    std::unique_ptr<detectors::ClickTrainTracker> tracker_;
    Subscription subscription_;
    ModuleState state_ = ModuleState::Created;
    std::uint64_t next_uid_ = 1;
};

enum class ClickClassifierNodeType {
    Basic,
    Sweep,
};

struct ClickClassifierNodeConfig {
    bool enabled = false;
    ClickClassifierNodeType type = ClickClassifierNodeType::Basic;
    bool discard_unclassified = false;
    detectors::BasicClickClassifierConfig basic;
    detectors::SweepClickClassifierConfig sweep;
};

struct ClickClassifierNodeOutputs {
    std::shared_ptr<DataBlock> accepted_clicks;
    std::shared_ptr<DataBlock> classifications;
};

class ClickClassifierNode final : public ModuleNode {
public:
    ClickClassifierNode(
        std::string instance_id,
        ClickClassifierNodeConfig config,
        std::shared_ptr<DataBlock> input,
        ClickClassifierNodeOutputs outputs);
    ~ClickClassifierNode() override;

    [[nodiscard]] const std::string& instance_id() const noexcept override;
    [[nodiscard]] ModuleState state() const noexcept override;
    void prepare() override;
    void start() override;
    void stop() override;
    void reset() override;

private:
    void process(const DataUnit& unit);

    std::string instance_id_;
    ClickClassifierNodeConfig config_;
    std::shared_ptr<DataBlock> input_;
    ClickClassifierNodeOutputs outputs_;
    std::unique_ptr<detectors::BasicClickClassifier> basic_;
    std::unique_ptr<detectors::SweepClickClassifier> sweep_;
    Subscription subscription_;
    ModuleState state_ = ModuleState::Created;
};

struct MatchedTemplateClassificationResult {
    std::int64_t click_start_sample = 0;
    std::string classifier_instance_id;
    int click_type = 0;
    detectors::MtClassification classification;
};

struct MatchedTemplateNodeConfig {
    detectors::MatchedTemplateClassifierConfig classifier;
    /** Unsigned view of MatchedTemplateParams.type. */
    int click_type = 101;
};

class MatchedTemplateNode final : public ModuleNode {
public:
    MatchedTemplateNode(
        std::string instance_id,
        double sample_rate_hz,
        MatchedTemplateNodeConfig config,
        std::shared_ptr<DataBlock> input,
        ClickClassifierNodeOutputs outputs);
    ~MatchedTemplateNode() override;

    [[nodiscard]] const std::string& instance_id() const noexcept override;
    [[nodiscard]] ModuleState state() const noexcept override;
    void prepare() override;
    void start() override;
    void stop() override;
    void reset() override;

private:
    void process(const DataUnit& unit);

    std::string instance_id_;
    double sample_rate_hz_ = 0.0;
    MatchedTemplateNodeConfig config_;
    std::shared_ptr<DataBlock> input_;
    ClickClassifierNodeOutputs outputs_;
    std::unique_ptr<detectors::MatchedTemplateClassifier> classifier_;
    Subscription subscription_;
    ModuleState state_ = ModuleState::Created;
};

struct GraphMhtClickTrainResult {
    std::size_t train_id = 0;
    std::uint32_t channel_bitmap = 0;
    double chi2 = 0.0;
    std::size_t click_count = 0;
    std::int64_t first_start_sample = 0;
    std::int64_t last_start_sample = 0;
    std::vector<std::int64_t> click_start_samples;
    std::vector<std::int64_t> click_time_ms;
    bool classified = false;
    bool junk_train = false;
    int species_id = 0;
    std::vector<int> classifier_species_ids;
    double template_correlation = 0.0;
};

struct GraphClickTrainClassificationResult {
    std::size_t train_id = 0;
    bool junk_train = false;
    int species_id = 0;
    std::vector<int> classifier_species_ids;
    double template_correlation = 0.0;
};

struct MhtClickTrainNodeConfig {
    double sample_rate_hz = 0.0;
    std::size_t min_clicks = 3;
    /**
     * ClickTrainParams.channelGroups: one independent MHT kernel per bitmap.
     * Empty retains the low-level diagnostic behaviour of keying by each
     * incoming click bitmap; controlled-unit projects always supply this.
     */
    std::vector<std::uint32_t> channel_groups;
    bool data_selector_enabled = false;
    bool data_selector_use_echoes = true;
    /**
     * Empty is Java ClickAlarmParameters' null useSpeciesList: all click
     * type codes are selected.
     */
    std::vector<int> data_selector_included_click_types;
    detectors::StandardMhtChi2Params chi2;
    detectors::MhtKernelParams kernel;
    bool classify = false;
    detectors::CtChi2ClassifierConfig pre_classifier;
    bool idi_classifier_enabled = false;
    detectors::CtIdiClassifierConfig idi_classifier;
    bool bearing_classifier_enabled = false;
    detectors::CtBearingClassifierConfig bearing_classifier;
    bool template_classifier_enabled = false;
    detectors::CtTemplateClassifierConfig template_classifier;
    std::size_t average_spectrum_fft_length = 256;
};

struct MhtClickTrainNodeInputs {
    std::shared_ptr<DataBlock> clicks;
    std::shared_ptr<DataBlock> features;
    std::shared_ptr<DataBlock> localisations;
    std::shared_ptr<DataBlock> bearings;
};

struct MhtClickTrainNodeOutputs {
    std::shared_ptr<DataBlock> trains;
    std::shared_ptr<DataBlock> classifications;
};

/** Graph wrapper around the parity-tested PAMGuard MHT and classifier stack. */
class MhtClickTrainNode final : public ModuleNode {
public:
    MhtClickTrainNode(
        std::string instance_id,
        MhtClickTrainNodeConfig config,
        MhtClickTrainNodeInputs inputs,
        MhtClickTrainNodeOutputs outputs);
    ~MhtClickTrainNode() override;

    [[nodiscard]] const std::string& instance_id() const noexcept override;
    [[nodiscard]] ModuleState state() const noexcept override;
    void prepare() override;
    void start() override;
    void stop() override;
    void flush() override;
    void reset() override;

private:
    struct PendingClick {
        std::optional<detectors::ClickDetectionResult> click;
        std::optional<detectors::ClickFeatureResult> features;
        std::optional<ClickLocalisationResult> localisation;
        std::optional<ClickBearingResult> bearing;
        DataUnitMetadata metadata;
    };

    struct TrainState {
        std::unique_ptr<
            detectors::MhtKernel<detectors::MhtChi2Unit>> kernel;
        std::vector<std::int64_t> start_samples;
        std::vector<std::int64_t> time_ms;
        std::vector<std::vector<double>> waveforms;
        std::vector<double> bearings_radians;
        std::vector<bool> has_bearing;
        std::size_t consumed_confirmed = 0;
    };

    static std::string pending_key(const DataUnitMetadata& metadata);
    void receive_click(const DataUnit& unit);
    void receive_feature(const DataUnit& unit);
    void receive_localisation(const DataUnit& unit);
    void receive_bearing(const DataUnit& unit);
    void try_process(const std::string& key);
    void process_ready(PendingClick pending);
    [[nodiscard]] std::optional<std::uint32_t>
        selected_channel_group(
            std::uint32_t click_bitmap) const noexcept;
    [[nodiscard]] bool passes_data_selector(
        const detectors::ClickDetectionResult& click) const noexcept;
    void drain(TrainState& state, std::uint32_t channel_bitmap);
    void publish_train(
        GraphMhtClickTrainResult train,
        const DataUnitMetadata& source_metadata);
    void classify(
        GraphMhtClickTrainResult& train,
        const TrainState& state,
        const detectors::MhtBitset& bits) const;
    [[nodiscard]] std::vector<std::shared_ptr<const detectors::CtClassifier>>
        classifiers() const;

    std::string instance_id_;
    MhtClickTrainNodeConfig config_;
    MhtClickTrainNodeInputs inputs_;
    MhtClickTrainNodeOutputs outputs_;
    std::unordered_map<std::string, PendingClick> pending_;
    std::unordered_map<std::uint32_t, TrainState> states_;
    Subscription click_subscription_;
    Subscription feature_subscription_;
    Subscription localisation_subscription_;
    Subscription bearing_subscription_;
    ModuleState state_ = ModuleState::Created;
    std::size_t next_train_id_ = 1;
    std::uint64_t next_uid_ = 1;
    DataUnitMetadata last_metadata_;
};

} // namespace pamguard::core
