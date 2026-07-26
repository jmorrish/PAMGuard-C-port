#pragma once

#include <map>
#include <memory>
#include <string>
#include <unordered_map>

#include "pamguard/core/DataModel.h"
#include "pamguard/core/ModuleNode.h"
#include "pamguard/detectors/FftNoiseMonitor.h"
#include "pamguard/detectors/IshmaelDetector.h"
#include "pamguard/detectors/LtsaMonitor.h"
#include "pamguard/detectors/NoiseBandMonitor.h"
#include "pamguard/detectors/ConnectedRegionTracker.h"
#include "pamguard/detectors/SpectrogramNoiseReducer.h"
#include "pamguard/detectors/WhistlePeakDetector.h"
#include "pamguard/detectors/SgramCorrDetector.h"
#include "pamguard/detectors/MatchFiltDetector.h"

namespace pamguard::core {

inline constexpr const char* kFftNoiseDataType = "pamguard.fft-noise";
inline constexpr const char* kLtsaDataType = "pamguard.ltsa";
inline constexpr const char* kIshmaelFunctionDataType =
    "pamguard.ishmael-detection-function";
inline constexpr const char* kIshmaelDetectionDataType =
    "pamguard.ishmael-detection";
inline constexpr const char* kNoiseBandDataType =
    "pamguard.noise-band";
inline constexpr const char* kWhistlePeakDataType =
    "pamguard.whistle-peak";
inline constexpr const char* kWhistleContourDataType =
    "pamguard.whistle-contour";

struct LtsaChannelInterval {
    std::size_t channel = 0;
    detectors::LtsaInterval interval;
};

struct NoiseBandMeasurement {
    std::size_t channel = 0;
    std::vector<detectors::NoiseBand> bands;
    std::vector<double> rms_db;
    std::vector<double> peak_db;
    std::int64_t end_sample = 0;
    std::int64_t time_unix_ms = 0;
};

struct NoiseBandNodeConfig {
    detectors::NoiseBandConfig monitor;
    std::uint32_t channel_bitmap = 0xFFFFFFFFu;
    /** Optional direct-runtime override; controlled units derive the source. */
    std::vector<double> calibration_db_offset_by_channel;
};

class NoiseBandNode final : public ModuleNode {
public:
    NoiseBandNode(
        std::string instance_id,
        double sample_rate_hz,
        NoiseBandNodeConfig config,
        std::shared_ptr<DataBlock> input,
        std::shared_ptr<DataBlock> output);
    ~NoiseBandNode() override;

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
    NoiseBandNodeConfig config_;
    std::shared_ptr<DataBlock> input_;
    std::shared_ptr<DataBlock> output_;
    std::unordered_map<
        std::size_t,
        std::unique_ptr<detectors::NoiseBandMonitor>> monitors_;
    Subscription subscription_;
    ModuleState state_ = ModuleState::Created;
    std::uint64_t next_uid_ = 1;
};

class FftNoiseNode final : public ModuleNode {
public:
    FftNoiseNode(
        std::string instance_id,
        double sample_rate_hz,
        std::size_t fft_length,
        std::size_t fft_hop,
        detectors::FftNoiseConfig config,
        std::shared_ptr<DataBlock> input,
        std::shared_ptr<DataBlock> output);
    ~FftNoiseNode() override;

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
    std::size_t fft_length_ = 0;
    std::size_t fft_hop_ = 0;
    detectors::FftNoiseConfig config_;
    std::shared_ptr<DataBlock> input_;
    std::shared_ptr<DataBlock> output_;
    std::unique_ptr<detectors::FftNoiseMonitor> monitor_;
    Subscription subscription_;
    ModuleState state_ = ModuleState::Created;
    std::uint64_t next_uid_ = 1;
};

class LtsaNode final : public ModuleNode {
public:
    LtsaNode(
        std::string instance_id,
        detectors::LtsaConfig config,
        std::shared_ptr<DataBlock> input,
        std::shared_ptr<DataBlock> output);
    ~LtsaNode() override;

    [[nodiscard]] const std::string& instance_id() const noexcept override;
    [[nodiscard]] ModuleState state() const noexcept override;
    void prepare() override;
    void start() override;
    void stop() override;
    void flush() override;
    void reset() override;

private:
    void process(const DataUnit& unit);
    void publish(std::size_t channel, detectors::LtsaInterval interval);

    std::string instance_id_;
    detectors::LtsaConfig config_;
    std::shared_ptr<DataBlock> input_;
    std::shared_ptr<DataBlock> output_;
    std::unordered_map<std::size_t, detectors::LtsaMonitor> monitors_;
    Subscription subscription_;
    ModuleState state_ = ModuleState::Created;
    std::uint64_t next_uid_ = 1;
};

struct IshmaelNodeOutputs {
    std::shared_ptr<DataBlock> detection_function;
    std::shared_ptr<DataBlock> detections;
};

class IshmaelNode final : public ModuleNode {
public:
    IshmaelNode(
        std::string instance_id,
        double sample_rate_hz,
        std::size_t fft_hop,
        detectors::IshmaelEnergySumConfig config,
        std::shared_ptr<DataBlock> input,
        IshmaelNodeOutputs outputs);
    ~IshmaelNode() override;

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
    std::size_t fft_hop_ = 0;
    detectors::IshmaelEnergySumConfig config_;
    std::shared_ptr<DataBlock> input_;
    IshmaelNodeOutputs outputs_;
    std::unique_ptr<detectors::IshmaelEnergySum> energy_;
    std::unique_ptr<detectors::IshmaelPeakPicker> picker_;
    Subscription subscription_;
    ModuleState state_ = ModuleState::Created;
    std::uint64_t next_uid_ = 1;
};

enum class WhistleGroupingType {
    Singles,
    All,
    User,
};

struct WhistleMoanNodeConfig {
    detectors::ConnectedRegionConfig contours;
    std::uint32_t channel_bitmap = 0;
    WhistleGroupingType grouping = WhistleGroupingType::All;
    std::vector<int> channel_groups;
};

struct WhistleMoanNodeOutputs {
    std::shared_ptr<DataBlock> contours;
};

class SpectrogramNoiseNode final : public ModuleNode {
public:
    SpectrogramNoiseNode(
        std::string instance_id,
        detectors::SpectrogramNoiseConfig config,
        std::shared_ptr<DataBlock> input,
        std::shared_ptr<DataBlock> output);
    ~SpectrogramNoiseNode() override;

    [[nodiscard]] const std::string& instance_id() const noexcept override;
    [[nodiscard]] ModuleState state() const noexcept override;
    void prepare() override;
    void start() override;
    void stop() override;
    void reset() override;

private:
    void process(const DataUnit& unit);

    std::string instance_id_;
    detectors::SpectrogramNoiseConfig config_;
    std::shared_ptr<DataBlock> input_;
    std::shared_ptr<DataBlock> output_;
    std::unique_ptr<detectors::SpectrogramNoiseReducer> reducer_;
    Subscription subscription_;
    ModuleState state_ = ModuleState::Created;
};

class WhistleMoanNode final : public ModuleNode {
public:
    WhistleMoanNode(
        std::string instance_id,
        WhistleMoanNodeConfig config,
        std::shared_ptr<DataBlock> input,
        WhistleMoanNodeOutputs outputs);
    ~WhistleMoanNode() override;

    [[nodiscard]] const std::string& instance_id() const noexcept override;
    [[nodiscard]] ModuleState state() const noexcept override;
    void prepare() override;
    void start() override;
    void stop() override;
    void flush() override;
    void reset() override;

private:
    void process(const DataUnit& unit);
    void publish_contours(
        std::vector<detectors::ConnectedRegionResult> contours,
        const DataUnitMetadata* source_metadata);

    std::string instance_id_;
    WhistleMoanNodeConfig config_;
    std::shared_ptr<DataBlock> input_;
    WhistleMoanNodeOutputs outputs_;
    std::map<
        std::size_t,
        std::unique_ptr<detectors::ConnectedRegionTracker>> region_trackers_;
    std::unordered_map<std::size_t, std::uint32_t>
        group_bitmap_by_first_channel_;
    Subscription subscription_;
    ModuleState state_ = ModuleState::Created;
    std::uint64_t next_uid_ = 1;
};

class SgramCorrNode final : public ModuleNode {
public:
    SgramCorrNode(
        std::string instance_id,
        double sample_rate_hz,
        std::size_t fft_length,
        std::size_t fft_hop,
        detectors::SgramCorrConfig config,
        std::shared_ptr<DataBlock> input,
        IshmaelNodeOutputs outputs);
    ~SgramCorrNode() override;

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
    std::size_t fft_length_ = 0;
    std::size_t fft_hop_ = 0;
    detectors::SgramCorrConfig config_;
    std::shared_ptr<DataBlock> input_;
    IshmaelNodeOutputs outputs_;
    std::unique_ptr<detectors::SgramCorrDetector> detector_;
    std::unique_ptr<detectors::IshmaelPeakPicker> picker_;
    Subscription subscription_;
    ModuleState state_ = ModuleState::Created;
    std::uint64_t next_uid_ = 1;
};

class MatchFiltNode final : public ModuleNode {
public:
    MatchFiltNode(
        std::string instance_id,
        double sample_rate_hz,
        detectors::MatchFiltConfig config,
        std::shared_ptr<DataBlock> input,
        IshmaelNodeOutputs outputs);
    ~MatchFiltNode() override;

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
    detectors::MatchFiltConfig config_;
    std::shared_ptr<DataBlock> input_;
    IshmaelNodeOutputs outputs_;
    std::unique_ptr<detectors::MatchFiltDetector> detector_;
    std::unique_ptr<detectors::IshmaelPeakPicker> picker_;
    Subscription subscription_;
    ModuleState state_ = ModuleState::Created;
    std::uint64_t next_uid_ = 1;
};

} // namespace pamguard::core
