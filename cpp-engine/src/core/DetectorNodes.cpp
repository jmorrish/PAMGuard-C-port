#include "pamguard/core/DetectorNodes.h"

#include <algorithm>
#include <any>
#include <cmath>
#include <stdexcept>
#include <utility>

#include "pamguard/core/SignalNodes.h"
#include "pamguard/detectors/CtTrainSpectrum.h"

namespace pamguard::core {

namespace {

void validate_output(
    const std::shared_ptr<DataBlock>& block,
    const char* data_type,
    const char* name) {
    if (!block || block->descriptor().data_type != data_type) {
        throw std::invalid_argument(
            std::string("Click detector ") + name +
            " output has the wrong data type");
    }
}

DataUnitMetadata click_metadata(
    std::uint64_t uid,
    std::int64_t start_sample,
    std::uint64_t duration,
    std::int64_t time_ms,
    std::uint32_t channel_bitmap,
    const DataUnitMetadata& input) {
    DataUnitMetadata metadata;
    metadata.uid = uid;
    metadata.sequence = uid;
    metadata.start_sample = start_sample;
    metadata.duration_samples = duration;
    metadata.time_unix_ms = time_ms;
    metadata.channel_bitmap = channel_bitmap;
    metadata.clock_domain_id = input.clock_domain_id;
    metadata.discontinuity = input.discontinuity;
    return metadata;
}

} // namespace

ClickDetectorNode::ClickDetectorNode(
    std::string instance_id,
    ClickDetectorNodeConfig config,
    std::shared_ptr<DataBlock> input,
    ClickDetectorNodeOutputs outputs)
    : instance_id_(std::move(instance_id)),
      config_(std::move(config)),
      input_(std::move(input)),
      outputs_(std::move(outputs)) {
    if (instance_id_.empty() || !input_) {
        throw std::invalid_argument(
            "Click detector requires an instance id and raw-audio input");
    }
}

ClickDetectorNode::ClickDetectorNode(
    std::string instance_id,
    detectors::ClickDetectorConfig config,
    std::shared_ptr<DataBlock> input,
    ClickDetectorNodeOutputs outputs)
    : ClickDetectorNode(
          std::move(instance_id),
          ClickDetectorNodeConfig{
              .detector = std::move(config),
          },
          std::move(input),
          std::move(outputs)) {}

ClickDetectorNode::~ClickDetectorNode() { stop(); }
const std::string& ClickDetectorNode::instance_id() const noexcept {
    return instance_id_;
}
ModuleState ClickDetectorNode::state() const noexcept { return state_; }

void ClickDetectorNode::prepare() {
    if (input_->descriptor().data_type != kRawAudioDataType) {
        throw std::invalid_argument("Click detector input must carry raw audio");
    }
    validate_output(outputs_.clicks, kClickDataType, "click");
    validate_output(outputs_.noise_samples, kClickNoiseDataType, "noise");
    validate_output(
        outputs_.trigger_background,
        kClickTriggerBackgroundDataType,
        "background");
    validate_output(
        outputs_.trigger_function,
        kClickTriggerFunctionDataType,
        "trigger function");
    if (!std::isfinite(config_.echo_max_interval_seconds) ||
        config_.echo_max_interval_seconds < 0.0) {
        throw std::invalid_argument(
            "Click detector echo interval must be finite and non-negative");
    }
    auto group_bitmaps = config_.channel_group_bitmaps;
    if (group_bitmaps.empty()) {
        group_bitmaps.push_back(config_.detector.channel_bitmap);
    }
    std::uint32_t grouped_channels = 0;
    engines_.clear();
    echo_detectors_.clear();
    engines_.reserve(group_bitmaps.size());
    echo_detectors_.reserve(group_bitmaps.size());
    for (const auto bitmap : group_bitmaps) {
        if (bitmap == 0 ||
            (bitmap & ~config_.detector.channel_bitmap) != 0 ||
            (bitmap & grouped_channels) != 0) {
            throw std::invalid_argument(
                "Click detector channel groups must be non-empty, "
                "disjoint subsets of channel_bitmap");
        }
        grouped_channels |= bitmap;
        auto detector = config_.detector;
        detector.channel_bitmap = bitmap;
        detector.trigger_bitmap &= bitmap;
        engines_.push_back(
            std::make_unique<detectors::ClickDetectorEngine>(
                std::move(detector)));
        if (config_.run_echo_online) {
            echo_detectors_.emplace_back(std::in_place,
                input_->descriptor().sample_rate_hz,
                config_.echo_max_interval_seconds);
        }
        else {
            echo_detectors_.emplace_back(std::nullopt);
        }
    }
    if (grouped_channels != config_.detector.channel_bitmap) {
        throw std::invalid_argument(
            "Click detector channel groups must cover channel_bitmap");
    }
    state_ = ModuleState::Prepared;
}

void ClickDetectorNode::start() {
    if (state_ != ModuleState::Prepared && state_ != ModuleState::Stopped) {
        throw std::logic_error("Click detector must be prepared before it starts");
    }
    subscription_ = input_->subscribe([this](const DataUnit& unit) { process(unit); });
    state_ = ModuleState::Running;
}

void ClickDetectorNode::stop() {
    subscription_.cancel();
    if (state_ == ModuleState::Running) {
        state_ = ModuleState::Stopped;
    }
}

void ClickDetectorNode::reset() {
    stop();
    for (auto& engine : engines_) {
        engine->reset();
    }
    engines_.clear();
    echo_detectors_.clear();
    next_uid_ = 1;
    state_ = ModuleState::Created;
}

void ClickDetectorNode::process(const DataUnit& unit) {
    const auto* audio = std::any_cast<AudioChunk>(&unit.payload);
    if (audio == nullptr) {
        throw std::invalid_argument(
            "Click detector raw-audio payload is not an AudioChunk");
    }
    std::vector<detectors::ClickDetectionResult> clicks;
    for (std::size_t group_index = 0;
         group_index < engines_.size();
         ++group_index) {
        auto group_clicks = engines_[group_index]->process(*audio);
        if (echo_detectors_[group_index]) {
            std::erase_if(
                group_clicks,
                [&](auto& click) {
                    const bool echo =
                        echo_detectors_[group_index]->is_echo(
                            click.start_sample);
                    click.echo = echo;
                    return echo && config_.discard_echoes;
                });
        }
        clicks.insert(
            clicks.end(),
            std::make_move_iterator(group_clicks.begin()),
            std::make_move_iterator(group_clicks.end()));
    }
    std::stable_sort(
        clicks.begin(),
        clicks.end(),
        [](const auto& left, const auto& right) {
            if (left.start_sample != right.start_sample) {
                return left.start_sample < right.start_sample;
            }
            return left.channel_bitmap < right.channel_bitmap;
        });
    for (auto& click : clicks) {
        const auto uid = next_uid_++;
        outputs_.clicks->publish(make_data_unit(
            click_metadata(
                uid,
                click.start_sample,
                click.duration_samples,
                click.time_unix_ms,
                click.channel_bitmap,
                unit.metadata),
            std::move(click)));
    }
    for (const auto& engine : engines_) {
        for (auto noise : engine->noise_samples()) {
            const auto uid = next_uid_++;
            outputs_.noise_samples->publish(make_data_unit(
                click_metadata(
                    uid,
                    noise.start_sample,
                    noise.duration_samples,
                    noise.time_unix_ms,
                    noise.channel_bitmap,
                    unit.metadata),
                std::move(noise)));
        }
        for (auto background : engine->trigger_background()) {
            const auto uid = next_uid_++;
            outputs_.trigger_background->publish(make_data_unit(
                click_metadata(
                    uid,
                    static_cast<std::int64_t>(
                        audio->start_sample + audio->frame_count()),
                    0,
                    background.time_unix_ms,
                    background.channel_bitmap,
                    unit.metadata),
                std::move(background)));
        }
        for (auto trigger : engine->trigger_function()) {
            const auto uid = next_uid_++;
            outputs_.trigger_function->publish(make_data_unit(
                click_metadata(
                    uid,
                    trigger.start_sample,
                    audio->frame_count(),
                    trigger.time_unix_ms,
                    trigger.channel_bitmap,
                    unit.metadata),
                std::move(trigger)));
        }
    }
}

ClickFeatureNode::ClickFeatureNode(
    std::string instance_id,
    detectors::ClickFeatureConfig config,
    std::shared_ptr<DataBlock> input,
    std::shared_ptr<DataBlock> output)
    : instance_id_(std::move(instance_id)),
      config_(std::move(config)),
      input_(std::move(input)),
      output_(std::move(output)) {
    if (instance_id_.empty() || !input_ || !output_) {
        throw std::invalid_argument(
            "Click feature node requires an instance id and data blocks");
    }
}

ClickFeatureNode::~ClickFeatureNode() { stop(); }
const std::string& ClickFeatureNode::instance_id() const noexcept {
    return instance_id_;
}
ModuleState ClickFeatureNode::state() const noexcept { return state_; }

void ClickFeatureNode::prepare() {
    if (input_->descriptor().data_type != kClickDataType ||
        output_->descriptor().data_type != kClickFeatureDataType) {
        throw std::invalid_argument(
            "Click feature node requires click input and feature output");
    }
    extractor_ =
        std::make_unique<detectors::ClickFeatureExtractor>(config_);
    state_ = ModuleState::Prepared;
}

void ClickFeatureNode::start() {
    if (state_ != ModuleState::Prepared && state_ != ModuleState::Stopped) {
        throw std::logic_error(
            "Click feature node must be prepared before it starts");
    }
    subscription_ = input_->subscribe([this](const DataUnit& unit) {
        process(unit);
    });
    state_ = ModuleState::Running;
}

void ClickFeatureNode::stop() {
    subscription_.cancel();
    if (state_ == ModuleState::Running) {
        state_ = ModuleState::Stopped;
    }
}

void ClickFeatureNode::reset() {
    stop();
    extractor_.reset();
    state_ = ModuleState::Created;
}

void ClickFeatureNode::process(const DataUnit& unit) {
    const auto* click =
        std::any_cast<detectors::ClickDetectionResult>(&unit.payload);
    if (click == nullptr || click->waveform.empty()) {
        return;
    }
    auto feature = extractor_->extract(*click);
    feature.click_start_sample = click->start_sample;
    auto metadata = unit.metadata;
    metadata.type_id.clear();
    metadata.source_block_id.clear();
    output_->publish(make_data_unit(
        std::move(metadata),
        std::move(feature)));
}

ClickTrainNode::ClickTrainNode(
    std::string instance_id,
    ClickTrainNodeConfig config,
    std::shared_ptr<DataBlock> input,
    std::shared_ptr<DataBlock> output)
    : instance_id_(std::move(instance_id)),
      config_(std::move(config)),
      input_(std::move(input)),
      output_(std::move(output)) {
    if (instance_id_.empty() || !input_ || !output_) {
        throw std::invalid_argument(
            "Click train node requires an instance id and data blocks");
    }
}

ClickTrainNode::ClickTrainNode(
    std::string instance_id,
    detectors::ClickTrainConfig config,
    std::shared_ptr<DataBlock> input,
    std::shared_ptr<DataBlock> output)
    : ClickTrainNode(
          std::move(instance_id),
          ClickTrainNodeConfig{
              .enabled = true,
              .tracker = std::move(config),
          },
          std::move(input),
          std::move(output)) {}

ClickTrainNode::~ClickTrainNode() { stop(); }
const std::string& ClickTrainNode::instance_id() const noexcept {
    return instance_id_;
}
ModuleState ClickTrainNode::state() const noexcept { return state_; }

void ClickTrainNode::prepare() {
    if (input_->descriptor().data_type != kClickDataType ||
        output_->descriptor().data_type != kClickTrainDataType) {
        throw std::invalid_argument(
            "Click train node requires click input and train output");
    }
    tracker_ = std::make_unique<detectors::ClickTrainTracker>(
        config_.tracker);
    next_uid_ = 1;
    state_ = ModuleState::Prepared;
}

void ClickTrainNode::start() {
    if (state_ != ModuleState::Prepared && state_ != ModuleState::Stopped) {
        throw std::logic_error(
            "Click train node must be prepared before it starts");
    }
    if (config_.enabled) {
        subscription_ = input_->subscribe([this](const DataUnit& unit) {
            process(unit);
        });
    }
    state_ = ModuleState::Running;
}

void ClickTrainNode::stop() {
    subscription_.cancel();
    if (tracker_ && state_ == ModuleState::Running) {
        flush();
        state_ = ModuleState::Stopped;
    }
}

void ClickTrainNode::flush() {
    if (config_.enabled && tracker_) {
        publish(tracker_->flush());
    }
}

void ClickTrainNode::reset() {
    stop();
    if (tracker_) {
        tracker_->reset();
    }
    next_uid_ = 1;
    state_ = ModuleState::Created;
}

void ClickTrainNode::process(const DataUnit& unit) {
    const auto* click =
        std::any_cast<detectors::ClickDetectionResult>(&unit.payload);
    if (click == nullptr) {
        throw std::invalid_argument(
            "Click train input payload is not a click");
    }
    publish(tracker_->process({*click}));
}

void ClickTrainNode::publish(
    std::vector<detectors::ClickTrainSummary> trains) {
    for (auto& train : trains) {
        DataUnitMetadata metadata;
        metadata.uid = next_uid_;
        metadata.sequence = next_uid_++;
        metadata.start_sample = train.first_start_sample;
        metadata.duration_samples =
            static_cast<std::uint64_t>(std::max<std::int64_t>(
                0,
                train.duration_samples));
        metadata.time_unix_ms = train.first_time_ms;
        metadata.channel_bitmap = train.channel_bitmap;
        output_->publish(make_data_unit(
            std::move(metadata),
            std::move(train)));
    }
}

ClickClassifierNode::ClickClassifierNode(
    std::string instance_id,
    ClickClassifierNodeConfig config,
    std::shared_ptr<DataBlock> input,
    ClickClassifierNodeOutputs outputs)
    : instance_id_(std::move(instance_id)),
      config_(std::move(config)),
      input_(std::move(input)),
      outputs_(std::move(outputs)) {
    if (instance_id_.empty() || !input_ ||
        !outputs_.accepted_clicks || !outputs_.classifications) {
        throw std::invalid_argument(
            "Click classifier requires an instance id and data blocks");
    }
}

ClickClassifierNode::~ClickClassifierNode() { stop(); }
const std::string& ClickClassifierNode::instance_id() const noexcept {
    return instance_id_;
}
ModuleState ClickClassifierNode::state() const noexcept { return state_; }

void ClickClassifierNode::prepare() {
    if (input_->descriptor().data_type != kClickDataType ||
        outputs_.accepted_clicks->descriptor().data_type != kClickDataType ||
        outputs_.classifications->descriptor().data_type !=
            kClickClassificationDataType) {
        throw std::invalid_argument(
            "Click classifier block types are incompatible");
    }
    basic_.reset();
    sweep_.reset();
    if (!config_.enabled) {
        state_ = ModuleState::Prepared;
        return;
    }
    if (config_.type == ClickClassifierNodeType::Basic) {
        basic_ =
            std::make_unique<detectors::BasicClickClassifier>(config_.basic);
    }
    else {
        sweep_ =
            std::make_unique<detectors::SweepClickClassifier>(config_.sweep);
    }
    state_ = ModuleState::Prepared;
}

void ClickClassifierNode::start() {
    if (state_ != ModuleState::Prepared && state_ != ModuleState::Stopped) {
        throw std::logic_error(
            "Click classifier must be prepared before it starts");
    }
    subscription_ = input_->subscribe([this](const DataUnit& unit) {
        process(unit);
    });
    state_ = ModuleState::Running;
}

void ClickClassifierNode::stop() {
    subscription_.cancel();
    if (state_ == ModuleState::Running) {
        state_ = ModuleState::Stopped;
    }
}

void ClickClassifierNode::reset() {
    stop();
    basic_.reset();
    sweep_.reset();
    state_ = ModuleState::Created;
}

void ClickClassifierNode::process(const DataUnit& unit) {
    const auto* click =
        std::any_cast<detectors::ClickDetectionResult>(&unit.payload);
    if (click == nullptr) {
        return;
    }
    if (!config_.enabled) {
        auto metadata = unit.metadata;
        metadata.type_id.clear();
        metadata.source_block_id.clear();
        outputs_.accepted_clicks->publish(make_data_unit(
            std::move(metadata),
            *click));
        return;
    }
    if (click->waveform.empty()) {
        return;
    }
    auto classification = basic_
        ? basic_->identify(*click)
        : sweep_->identify(*click);
    classification.click_start_sample = click->start_sample;
    const bool discarded =
        classification.discard ||
        (classification.click_type == 0 && config_.discard_unclassified);
    if (discarded) {
        return;
    }
    auto accepted_click = *click;
    accepted_click.click_type = classification.click_type;
    accepted_click.classifiers_passed =
        classification.classifiers_passed;
    auto metadata = unit.metadata;
    metadata.type_id.clear();
    metadata.source_block_id.clear();
    outputs_.classifications->publish(make_data_unit(
        metadata,
        std::move(classification)));
    metadata.type_id.clear();
    metadata.source_block_id.clear();
    outputs_.accepted_clicks->publish(make_data_unit(
        std::move(metadata),
        std::move(accepted_click)));
}

MatchedTemplateNode::MatchedTemplateNode(
    std::string instance_id,
    double sample_rate_hz,
    MatchedTemplateNodeConfig config,
    std::shared_ptr<DataBlock> input,
    ClickClassifierNodeOutputs outputs)
    : instance_id_(std::move(instance_id)),
      sample_rate_hz_(sample_rate_hz),
      config_(std::move(config)),
      input_(std::move(input)),
      outputs_(std::move(outputs)) {
    if (instance_id_.empty() || !input_ ||
        !outputs_.accepted_clicks || !outputs_.classifications) {
        throw std::invalid_argument(
            "Matched-template classifier requires data blocks");
    }
}

MatchedTemplateNode::~MatchedTemplateNode() { stop(); }
const std::string& MatchedTemplateNode::instance_id() const noexcept {
    return instance_id_;
}
ModuleState MatchedTemplateNode::state() const noexcept { return state_; }

void MatchedTemplateNode::prepare() {
    if (input_->descriptor().data_type != kClickDataType ||
        outputs_.accepted_clicks->descriptor().data_type != kClickDataType ||
        outputs_.classifications->descriptor().data_type !=
            kMatchedTemplateClassificationDataType) {
        throw std::invalid_argument(
            "Matched-template classifier block types are incompatible");
    }
    classifier_ =
        std::make_unique<detectors::MatchedTemplateClassifier>(
            sample_rate_hz_,
            config_.classifier);
    if (!classifier_->valid()) {
        throw std::invalid_argument(
            "Matched-template classifier: " +
            classifier_->invalid_reason());
    }
    state_ = ModuleState::Prepared;
}

void MatchedTemplateNode::start() {
    if (state_ != ModuleState::Prepared && state_ != ModuleState::Stopped) {
        throw std::logic_error(
            "Matched-template classifier must be prepared before it starts");
    }
    subscription_ = input_->subscribe(
        [this](const DataUnit& unit) { process(unit); });
    state_ = ModuleState::Running;
}

void MatchedTemplateNode::stop() {
    subscription_.cancel();
    if (state_ == ModuleState::Running) {
        state_ = ModuleState::Stopped;
    }
}

void MatchedTemplateNode::reset() {
    stop();
    classifier_.reset();
    state_ = ModuleState::Created;
}

void MatchedTemplateNode::process(const DataUnit& unit) {
    const auto* click =
        std::any_cast<detectors::ClickDetectionResult>(&unit.payload);
    if (click == nullptr || click->waveform.empty()) {
        return;
    }
    MatchedTemplateClassificationResult result;
    result.click_start_sample = click->start_sample;
    result.classifier_instance_id = instance_id_;
    result.click_type = config_.click_type;
    result.classification = classifier_->classify(click->waveform);
    const bool classified = result.classification.classified;
    auto metadata = unit.metadata;
    auto annotated_click = *click;
    // BeskopeClassifierManager.bespokeDataUnitFlags: a positive match
    // overrides any prior Click Detector classification with the configured
    // type. A negative result preserves a prior non-MT type, but resets a
    // stale type written by this same MT classifier to zero.
    if (classified) {
        annotated_click.click_type =
            config_.click_type;
    }
    else if (annotated_click.click_type ==
             config_.click_type) {
        annotated_click.click_type = 0;
    }
    detectors::ClickDetectionResult::
        MatchedTemplateAnnotation annotation;
    annotation.classifier_instance_id =
        instance_id_;
    annotation.click_type =
        config_.click_type;
    annotation.classified = classified;
    annotation.best_results.reserve(
        result.classification.best_results.size());
    for (const auto& item :
         result.classification.best_results) {
        annotation.best_results.push_back({
            item.threshold,
            item.match_corr,
            item.reject_corr,
        });
    }
    annotated_click.matched_template_annotations.push_back(
        std::move(annotation));
    metadata.type_id.clear();
    metadata.source_block_id.clear();
    outputs_.classifications->publish(make_data_unit(
        metadata,
        std::move(result)));
    metadata.type_id.clear();
    metadata.source_block_id.clear();
    outputs_.accepted_clicks->publish(make_data_unit(
        std::move(metadata),
        std::move(annotated_click)));
}

MhtClickTrainNode::MhtClickTrainNode(
    std::string instance_id,
    MhtClickTrainNodeConfig config,
    MhtClickTrainNodeInputs inputs,
    MhtClickTrainNodeOutputs outputs)
    : instance_id_(std::move(instance_id)),
      config_(std::move(config)),
      inputs_(std::move(inputs)),
      outputs_(std::move(outputs)) {
    if (instance_id_.empty() || !inputs_.clicks ||
        !outputs_.trains || !outputs_.classifications) {
        throw std::invalid_argument(
            "MHT click train node requires click input and output blocks");
    }
}

MhtClickTrainNode::~MhtClickTrainNode() { stop(); }

const std::string& MhtClickTrainNode::instance_id() const noexcept {
    return instance_id_;
}

ModuleState MhtClickTrainNode::state() const noexcept {
    return state_;
}

std::string MhtClickTrainNode::pending_key(
    const DataUnitMetadata& metadata) {
    return std::to_string(metadata.start_sample) + ":" +
        std::to_string(metadata.channel_bitmap);
}

void MhtClickTrainNode::prepare() {
    if (config_.sample_rate_hz <= 0.0 ||
        inputs_.clicks->descriptor().data_type != kClickDataType ||
        outputs_.trains->descriptor().data_type !=
            kMhtClickTrainDataType ||
        outputs_.classifications->descriptor().data_type !=
            kClickTrainClassificationDataType) {
        throw std::invalid_argument(
            "MHT click train block types or sample rate are invalid");
    }
    if (config_.chi2.enable_peak_frequency &&
        (!inputs_.features ||
         inputs_.features->descriptor().data_type !=
             kClickFeatureDataType)) {
        throw std::invalid_argument(
            "MHT peak-frequency scoring requires a click-feature input");
    }
    if (config_.chi2.enable_time_delay &&
        (!inputs_.localisations ||
         inputs_.localisations->descriptor().data_type !=
             kClickLocalisationDataType)) {
        throw std::invalid_argument(
            "MHT time-delay scoring requires a localisation input");
    }
    if ((config_.chi2.enable_bearing ||
         (config_.classify &&
          config_.bearing_classifier_enabled)) &&
        (!inputs_.bearings ||
         inputs_.bearings->descriptor().data_type !=
             kClickBearingDataType)) {
        throw std::invalid_argument(
            "MHT bearing scoring requires a bearing input");
    }
    if (config_.min_clicks == 0 ||
        config_.average_spectrum_fft_length == 0) {
        throw std::invalid_argument(
            "MHT minimum clicks and spectrum FFT length must be positive");
    }
    config_.chi2.sample_rate_hz = config_.sample_rate_hz;
    pending_.clear();
    states_.clear();
    next_train_id_ = 1;
    next_uid_ = 1;
    last_metadata_ = {};
    state_ = ModuleState::Prepared;
}

void MhtClickTrainNode::start() {
    if (state_ != ModuleState::Prepared &&
        state_ != ModuleState::Stopped) {
        throw std::logic_error(
            "MHT click train node must be prepared before it starts");
    }
    click_subscription_ = inputs_.clicks->subscribe(
        [this](const DataUnit& unit) { receive_click(unit); });
    if (inputs_.features) {
        feature_subscription_ = inputs_.features->subscribe(
            [this](const DataUnit& unit) { receive_feature(unit); });
    }
    if (inputs_.localisations) {
        localisation_subscription_ = inputs_.localisations->subscribe(
            [this](const DataUnit& unit) {
                receive_localisation(unit);
            });
    }
    if (inputs_.bearings) {
        bearing_subscription_ = inputs_.bearings->subscribe(
            [this](const DataUnit& unit) { receive_bearing(unit); });
    }
    state_ = ModuleState::Running;
}

void MhtClickTrainNode::stop() {
    click_subscription_.cancel();
    feature_subscription_.cancel();
    localisation_subscription_.cancel();
    bearing_subscription_.cancel();
    if (state_ == ModuleState::Running) {
        flush();
        pending_.clear();
        state_ = ModuleState::Stopped;
    }
}

void MhtClickTrainNode::flush() {
    for (auto& [channel_bitmap, train_state] : states_) {
        train_state.kernel->confirm_remaining_tracks();
        drain(train_state, channel_bitmap);
        train_state.kernel->clear_kernel();
        train_state.start_samples.clear();
        train_state.time_ms.clear();
        train_state.waveforms.clear();
        train_state.bearings_radians.clear();
        train_state.has_bearing.clear();
        train_state.consumed_confirmed = 0;
    }
}

void MhtClickTrainNode::reset() {
    stop();
    pending_.clear();
    states_.clear();
    next_train_id_ = 1;
    next_uid_ = 1;
    state_ = ModuleState::Created;
}

void MhtClickTrainNode::receive_click(const DataUnit& unit) {
    const auto* click =
        std::any_cast<detectors::ClickDetectionResult>(&unit.payload);
    if (!click) {
        throw std::invalid_argument(
            "MHT click input payload is not a click");
    }
    auto& pending = pending_[pending_key(unit.metadata)];
    pending.click = *click;
    pending.metadata = unit.metadata;
    try_process(pending_key(unit.metadata));
}

void MhtClickTrainNode::receive_feature(const DataUnit& unit) {
    const auto* feature =
        std::any_cast<detectors::ClickFeatureResult>(&unit.payload);
    if (!feature) {
        throw std::invalid_argument(
            "MHT feature input payload is not a click feature");
    }
    pending_[pending_key(unit.metadata)].features = *feature;
    try_process(pending_key(unit.metadata));
}

void MhtClickTrainNode::receive_localisation(const DataUnit& unit) {
    const auto* localisation =
        std::any_cast<ClickLocalisationResult>(&unit.payload);
    if (!localisation) {
        throw std::invalid_argument(
            "MHT localisation input payload is incompatible");
    }
    pending_[pending_key(unit.metadata)].localisation = *localisation;
    try_process(pending_key(unit.metadata));
}

void MhtClickTrainNode::receive_bearing(const DataUnit& unit) {
    const auto* bearing =
        std::any_cast<ClickBearingResult>(&unit.payload);
    if (!bearing) {
        throw std::invalid_argument(
            "MHT bearing input payload is incompatible");
    }
    pending_[pending_key(unit.metadata)].bearing = *bearing;
    try_process(pending_key(unit.metadata));
}

void MhtClickTrainNode::try_process(const std::string& key) {
    const auto found = pending_.find(key);
    if (found == pending_.end() || !found->second.click) {
        return;
    }
    const auto& pending = found->second;
    if (config_.chi2.enable_peak_frequency && !pending.features) {
        return;
    }
    if (config_.chi2.enable_time_delay && !pending.localisation) {
        return;
    }
    if ((config_.chi2.enable_bearing ||
         (config_.classify &&
          config_.bearing_classifier_enabled)) &&
        !pending.bearing) {
        return;
    }
    auto ready = std::move(found->second);
    pending_.erase(found);
    process_ready(std::move(ready));
}

void MhtClickTrainNode::process_ready(PendingClick pending) {
    const auto& click = *pending.click;
    if (!passes_data_selector(click)) {
        return;
    }
    const auto click_bitmap =
        click.channel_bitmap != 0
        ? click.channel_bitmap
        : click.trigger_bitmap;
    const auto selected_group =
        selected_channel_group(click_bitmap);
    if (!selected_group) {
        return;
    }
    const auto channel_bitmap = *selected_group;
    auto [found, inserted] = states_.try_emplace(channel_bitmap);
    auto& train_state = found->second;
    if (inserted) {
        train_state.kernel = std::make_unique<
            detectors::MhtKernel<detectors::MhtChi2Unit>>(
                std::make_unique<
                    detectors::StandardMhtChi2Provider>(
                        config_.chi2,
                        config_.kernel),
                config_.kernel);
    }

    detectors::MhtChi2Unit score;
    score.time_ns = static_cast<std::int64_t>(
        static_cast<double>(click.start_sample) /
        config_.sample_rate_hz * 1E9);
    const auto* last = train_state.kernel->last_data_unit();
    const double max_gap =
        static_cast<double>(config_.kernel.max_coast) *
        config_.chi2.max_ici;
    if (train_state.kernel->kcount() > 5 && last) {
        const double gap_seconds =
            static_cast<double>(
                score.time_ns / 1'000'000 -
                last->time_ns / 1'000'000) /
            1000.0;
        if (gap_seconds > max_gap ||
            train_state.kernel->kcount() > 10000) {
            train_state.kernel->confirm_remaining_tracks();
            drain(train_state, channel_bitmap);
            train_state.kernel->clear_kernel();
            train_state.start_samples.clear();
            train_state.time_ms.clear();
            train_state.waveforms.clear();
            train_state.bearings_radians.clear();
            train_state.has_bearing.clear();
            train_state.consumed_confirmed = 0;
        }
    }

    double peak = 0.0;
    for (const auto& waveform : click.waveform) {
        for (const auto sample : waveform) {
            peak = std::max(peak, std::abs(sample));
        }
    }
    score.amplitude_db =
        20.0 * std::log10(std::max(peak, 1e-12));
    score.duration_ms =
        static_cast<double>(click.duration_samples) /
        config_.sample_rate_hz * 1000.0;
    if (pending.features) {
        score.peak_frequency_hz =
            pending.features->peak_frequency_hz;
    }
    if (pending.bearing && pending.bearing->bearing.valid) {
        constexpr double kDegreesToRadians =
            3.141592653589793238462643383279502884 / 180.0;
        score.bearing_radians =
            pending.bearing->bearing.azimuth_degrees *
            kDegreesToRadians;
    }
    if (pending.localisation) {
        for (const auto& delay : pending.localisation->delays) {
            score.pair_delays_seconds.push_back(
                delay.delay.delay_samples /
                config_.sample_rate_hz);
        }
    }
    if (config_.chi2.enable_correlation &&
        !click.waveform.empty()) {
        score.waveform =
            std::make_shared<const std::vector<double>>(
                click.waveform.front());
    }

    train_state.kernel->add_detection(score);
    train_state.start_samples.push_back(click.start_sample);
    train_state.time_ms.push_back(click.time_unix_ms);
    train_state.waveforms.push_back(
        click.waveform.empty()
        ? std::vector<double>{}
        : click.waveform.front());
    train_state.bearings_radians.push_back(score.bearing_radians);
    train_state.has_bearing.push_back(
        pending.bearing.has_value() &&
        pending.bearing->bearing.valid);
    last_metadata_ = pending.metadata;

    if (train_state.kernel->kcount() > 5 &&
        train_state.kernel->kcount() % 20 == 0) {
        const auto reference =
            train_state.kernel->first_detection_index();
        if (reference == train_state.kernel->kcount()) {
            drain(train_state, channel_bitmap);
            train_state.kernel->clear_kernel();
            train_state.start_samples.clear();
            train_state.time_ms.clear();
            train_state.waveforms.clear();
            train_state.bearings_radians.clear();
            train_state.has_bearing.clear();
            train_state.consumed_confirmed = 0;
        }
        else if (reference > 100 &&
                 reference <= train_state.kernel->kcount()) {
            drain(train_state, channel_bitmap);
            train_state.kernel->clear_kernel_garbage(reference);
            const auto erase_prefix = [reference](auto& values) {
                values.erase(
                    values.begin(),
                    values.begin() +
                        static_cast<std::ptrdiff_t>(reference));
            };
            erase_prefix(train_state.start_samples);
            erase_prefix(train_state.time_ms);
            erase_prefix(train_state.waveforms);
            erase_prefix(train_state.bearings_radians);
            erase_prefix(train_state.has_bearing);
            train_state.consumed_confirmed = 0;
        }
    }
    drain(train_state, channel_bitmap);
}

std::optional<std::uint32_t>
MhtClickTrainNode::selected_channel_group(
    std::uint32_t click_bitmap) const noexcept {
    if (config_.channel_groups.empty()) {
        return click_bitmap;
    }
    for (const auto group : config_.channel_groups) {
        // PamUtils.hasChannelMap: any common channel is sufficient, and the
        // Java algorithm stops at the first matching group.
        if ((group & click_bitmap) != 0) {
            return group;
        }
    }
    return std::nullopt;
}

bool MhtClickTrainNode::passes_data_selector(
    const detectors::ClickDetectionResult& click) const noexcept {
    if (!config_.data_selector_enabled) {
        return true;
    }
    if (!config_.data_selector_use_echoes && click.echo) {
        return false;
    }
    if (config_.data_selector_included_click_types.empty()) {
        return true;
    }
    return std::find(
               config_.data_selector_included_click_types.begin(),
               config_.data_selector_included_click_types.end(),
               click.click_type) !=
        config_.data_selector_included_click_types.end();
}

std::vector<std::shared_ptr<const detectors::CtClassifier>>
MhtClickTrainNode::classifiers() const {
    std::vector<std::shared_ptr<const detectors::CtClassifier>> result;
    if (config_.idi_classifier_enabled) {
        result.push_back(std::make_shared<
            const detectors::CtIdiClassifierAdapter>(
                config_.idi_classifier));
    }
    if (config_.bearing_classifier_enabled) {
        result.push_back(std::make_shared<
            const detectors::CtBearingClassifierAdapter>(
                config_.bearing_classifier));
    }
    if (config_.template_classifier_enabled) {
        result.push_back(std::make_shared<
            const detectors::CtTemplateClassifierAdapter>(
                config_.template_classifier));
    }
    return result;
}

void MhtClickTrainNode::classify(
    GraphMhtClickTrainResult& train,
    const TrainState& state,
    const detectors::MhtBitset& bits) const {
    detectors::CtTrainSummary summary;
    summary.chi2 = train.chi2;
    summary.click_count = train.click_count;
    summary.duration_ms =
        static_cast<double>(
            train.last_start_sample -
            train.first_start_sample) /
        config_.sample_rate_hz * 1000.0;
    std::vector<double> idis;
    std::vector<std::vector<double>> waveforms;
    for (std::size_t bit = 0;
         bit < state.start_samples.size();
         ++bit) {
        if (!bits.get(bit)) {
            continue;
        }
        if (bit < state.waveforms.size() &&
            !state.waveforms[bit].empty()) {
            waveforms.push_back(state.waveforms[bit]);
        }
        detectors::CtBearingClick bearing;
        bearing.time_ms = state.time_ms[bit];
        if (bit < state.has_bearing.size() &&
            state.has_bearing[bit]) {
            bearing.bearing_radians =
                state.bearings_radians[bit];
        }
        summary.bearing_clicks.push_back(bearing);
    }
    for (std::size_t index = 1;
         index < train.click_start_samples.size();
         ++index) {
        idis.push_back(
            static_cast<double>(
                train.click_start_samples[index] -
                train.click_start_samples[index - 1]) /
            config_.sample_rate_hz);
    }
    if (!idis.empty()) {
        summary.idi.median_idi = detectors::ct_median(idis);
        summary.idi.mean_idi = detectors::ct_mean(idis);
        summary.idi.std_idi = detectors::ct_std(idis);
    }
    if (!waveforms.empty()) {
        summary.average_spectrum =
            detectors::ct_train_average_spectrum(
                waveforms,
                config_.average_spectrum_fft_length);
        summary.average_spectrum_sample_rate_hz =
            config_.sample_rate_hz;
    }
    if (config_.classify &&
        config_.template_classifier_enabled) {
        const detectors::CtTemplateClassifier classifier(
            config_.template_classifier);
        train.template_correlation =
            classifier.classify_detailed(summary).correlation;
    }
    const detectors::CtClassifierChain chain(
        config_.pre_classifier,
        classifiers(),
        config_.classify);
    const auto result = chain.classify(summary);
    train.classified = true;
    train.junk_train = result.junk_train;
    train.species_id = result.species_id;
    train.classifier_species_ids = result.classifications;
}

void MhtClickTrainNode::drain(
    TrainState& state,
    std::uint32_t channel_bitmap) {
    for (std::size_t index = state.consumed_confirmed;
         index < state.kernel->confirmed_track_count();
         ++index) {
        const auto& track = state.kernel->confirmed_track(index);
        const auto click_count = track.bits.cardinality();
        if (click_count < config_.min_clicks) {
            continue;
        }
        GraphMhtClickTrainResult train;
        train.train_id = next_train_id_++;
        train.channel_bitmap = channel_bitmap;
        train.chi2 = track.get_chi2();
        train.click_count = click_count;
        for (std::size_t bit = 0;
             bit < state.start_samples.size();
             ++bit) {
            if (track.bits.get(bit)) {
                train.click_start_samples.push_back(
                    state.start_samples[bit]);
                train.click_time_ms.push_back(
                    state.time_ms[bit]);
            }
        }
        if (!train.click_start_samples.empty()) {
            train.first_start_sample =
                train.click_start_samples.front();
            train.last_start_sample =
                train.click_start_samples.back();
        }
        classify(train, state, track.bits);
        publish_train(std::move(train), last_metadata_);
    }
    state.consumed_confirmed =
        state.kernel->confirmed_track_count();
}

void MhtClickTrainNode::publish_train(
    GraphMhtClickTrainResult train,
    const DataUnitMetadata& source_metadata) {
    DataUnitMetadata metadata = source_metadata;
    metadata.uid = next_uid_;
    metadata.sequence = next_uid_++;
    metadata.start_sample = train.first_start_sample;
    metadata.duration_samples =
        static_cast<std::uint64_t>(
            std::max<std::int64_t>(
                0,
                train.last_start_sample -
                train.first_start_sample));
    metadata.time_unix_ms =
        train.click_time_ms.empty()
        ? 0
        : train.click_time_ms.front();
    metadata.channel_bitmap = train.channel_bitmap;
    metadata.type_id.clear();
    metadata.source_block_id.clear();
    outputs_.trains->publish(make_data_unit(metadata, train));
    if (train.classified) {
        GraphClickTrainClassificationResult classification;
        classification.train_id = train.train_id;
        classification.junk_train = train.junk_train;
        classification.species_id = train.species_id;
        classification.classifier_species_ids =
            train.classifier_species_ids;
        classification.template_correlation =
            train.template_correlation;
        metadata.type_id.clear();
        metadata.source_block_id.clear();
        outputs_.classifications->publish(make_data_unit(
            std::move(metadata),
            std::move(classification)));
    }
}

} // namespace pamguard::core
