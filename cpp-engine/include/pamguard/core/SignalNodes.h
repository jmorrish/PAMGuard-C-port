#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "pamguard/core/AnalysisConfig.h"
#include "pamguard/core/AudioFrame.h"
#include "pamguard/core/DataModel.h"
#include "pamguard/core/ModuleNode.h"
#include "pamguard/dsp/IirFilter.h"
#include "pamguard/dsp/SpectrogramEngine.h"

namespace pamguard::core {

inline constexpr const char* kRawAudioDataType = "pamguard.raw-audio";
inline constexpr const char* kFftDataType = "pamguard.fft";

struct AudioSourceNodeConfig {
    /** AcquisitionParameters.subtractDC. */
    bool subtract_dc = true;
    /** AcquisitionParameters.dcTimeConstant, in seconds. */
    double dc_time_constant_seconds = 1.0;
};

class AudioSourceNode final : public ModuleNode {
public:
    AudioSourceNode(
        std::string instance_id,
        AudioSourceNodeConfig config,
        std::shared_ptr<DataBlock> output);

    [[nodiscard]] const std::string& instance_id() const noexcept override;
    [[nodiscard]] ModuleState state() const noexcept override;
    void prepare() override;
    void start() override;
    void stop() override;
    void reset() override;
    void ingest(AudioChunk chunk);

private:
    std::string instance_id_;
    AudioSourceNodeConfig config_;
    std::shared_ptr<DataBlock> output_;
    ModuleState state_ = ModuleState::Created;
    std::uint64_t next_uid_ = 1;
    std::uint64_t next_sequence_ = 1;
    std::optional<std::uint64_t> expected_start_sample_;
    double dc_alpha_ = 0.0;
    std::vector<double> dc_background_by_channel_;
};

struct AmplifierNodeConfig {
    std::vector<double> channel_gains;
};

class AmplifierNode final : public ModuleNode {
public:
    AmplifierNode(
        std::string instance_id,
        AmplifierNodeConfig config,
        std::shared_ptr<DataBlock> input,
        std::shared_ptr<DataBlock> output);
    ~AmplifierNode() override;

    [[nodiscard]] const std::string& instance_id() const noexcept override;
    [[nodiscard]] ModuleState state() const noexcept override;
    void prepare() override;
    void start() override;
    void stop() override;
    void reset() override;

private:
    void process(const DataUnit& unit);

    std::string instance_id_;
    AmplifierNodeConfig config_;
    std::shared_ptr<DataBlock> input_;
    std::shared_ptr<DataBlock> output_;
    Subscription subscription_;
    ModuleState state_ = ModuleState::Created;
};

struct PatchPanelNodeConfig {
    /** Java PatchPanelParameters.patches[input channel][output channel]. */
    std::vector<std::vector<double>> patches;
};

class PatchPanelNode final : public ModuleNode {
public:
    PatchPanelNode(
        std::string instance_id,
        PatchPanelNodeConfig config,
        std::shared_ptr<DataBlock> input,
        std::shared_ptr<DataBlock> output);
    ~PatchPanelNode() override;

    [[nodiscard]] const std::string& instance_id() const noexcept override;
    [[nodiscard]] ModuleState state() const noexcept override;
    void prepare() override;
    void start() override;
    void stop() override;
    void reset() override;

private:
    void process(const DataUnit& unit);

    std::string instance_id_;
    PatchPanelNodeConfig config_;
    std::shared_ptr<DataBlock> input_;
    std::shared_ptr<DataBlock> output_;
    Subscription subscription_;
    ModuleState state_ = ModuleState::Created;
};

struct FilterNodeConfig {
    /** FilterParameters_2.channelBitmap has no initializer. */
    std::uint32_t channel_bitmap = 0;
    dsp::IirFilterParams filter;
};

class FilterNode final : public ModuleNode {
public:
    FilterNode(
        std::string instance_id,
        FilterNodeConfig config,
        std::shared_ptr<DataBlock> input,
        std::shared_ptr<DataBlock> output);
    ~FilterNode() override;

    [[nodiscard]] const std::string& instance_id() const noexcept override;
    [[nodiscard]] ModuleState state() const noexcept override;
    void prepare() override;
    void start() override;
    void stop() override;
    void reset() override;

private:
    void process(const DataUnit& unit);

    std::string instance_id_;
    FilterNodeConfig config_;
    std::shared_ptr<DataBlock> input_;
    std::shared_ptr<DataBlock> output_;
    std::vector<std::unique_ptr<dsp::FastIirFilter>> filters_;
    Subscription subscription_;
    ModuleState state_ = ModuleState::Created;
};

/** PAMGuard DecimatorParams(newSampleRate, 6) anti-alias defaults. */
[[nodiscard]] dsp::IirFilterParams default_decimator_filter_params(
    double output_sample_rate_hz = 2000.0);

struct DecimatorNodeConfig {
    double output_sample_rate_hz = 2000.0;
    dsp::IirFilterParams filter = default_decimator_filter_params();
    /** PAMGuard DecimatorParams.interpolation: 0 nearest, 1 linear, 2 quadratic. */
    int interpolation = 0;
    /** DecimatorParams.channelMap has no initializer. */
    std::uint32_t channel_bitmap = 0;
};

class DecimatorNode final : public ModuleNode {
public:
    DecimatorNode(
        std::string instance_id,
        DecimatorNodeConfig config,
        std::shared_ptr<DataBlock> input,
        std::shared_ptr<DataBlock> output);
    ~DecimatorNode() override;

    [[nodiscard]] const std::string& instance_id() const noexcept override;
    [[nodiscard]] ModuleState state() const noexcept override;
    void prepare() override;
    void start() override;
    void stop() override;
    void reset() override;

private:
    struct ChannelState {
        std::unique_ptr<dsp::FastIirFilter> filter;
        std::vector<double> interpolation_history;
        double pick_sample = 0.0;
    };

    void process(const DataUnit& unit);
    [[nodiscard]] double interpolate(
        ChannelState& state,
        const std::vector<double>& samples,
        double position) const;

    std::string instance_id_;
    DecimatorNodeConfig config_;
    std::shared_ptr<DataBlock> input_;
    std::shared_ptr<DataBlock> output_;
    std::vector<ChannelState> channels_;
    Subscription subscription_;
    ModuleState state_ = ModuleState::Created;
    bool initialized_ = false;
    std::uint64_t next_output_sample_ = 0;
};

class FftNode final : public ModuleNode {
public:
    FftNode(
        std::string instance_id,
        FftConfig config,
        std::shared_ptr<DataBlock> input,
        std::shared_ptr<DataBlock> output);
    ~FftNode() override;

    [[nodiscard]] const std::string& instance_id() const noexcept override;
    [[nodiscard]] ModuleState state() const noexcept override;
    void prepare() override;
    void start() override;
    void stop() override;
    void reset() override;

private:
    void process(const DataUnit& unit);

    std::string instance_id_;
    FftConfig config_;
    std::shared_ptr<DataBlock> input_;
    std::shared_ptr<DataBlock> output_;
    std::unique_ptr<dsp::SpectrogramEngine> engine_;
    Subscription subscription_;
    ModuleState state_ = ModuleState::Created;
};

} // namespace pamguard::core
