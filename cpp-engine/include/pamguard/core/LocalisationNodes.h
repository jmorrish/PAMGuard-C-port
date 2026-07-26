#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "pamguard/core/AnalysisConfig.h"
#include "pamguard/core/DataModel.h"
#include "pamguard/core/LocalisationData.h"
#include "pamguard/core/ModuleNode.h"
#include "pamguard/detectors/ClickAngleVeto.h"

namespace pamguard::core {

struct ClickLocaliserNodeConfig {
    ArrayConfiguration array;
    localisation::DelayMeasurementConfig delay_measurement;
    std::map<int, localisation::DelayMeasurementConfig>
        delay_measurement_by_type;
    std::vector<detectors::ClickAngleVeto> angle_vetoes;
    std::size_t pre_sample = 40;
};

struct ClickLocaliserNodeOutputs {
    std::shared_ptr<DataBlock> accepted_clicks;
    std::shared_ptr<DataBlock> localisations;
    std::shared_ptr<DataBlock> bearings;
};

/**
 * Graph wrapper for PAMGuard click delay measurement and the ported
 * geometry-aware far-field bearing process.
 */
class ClickLocaliserNode final : public ModuleNode {
public:
    ClickLocaliserNode(
        std::string instance_id,
        double sample_rate_hz,
        ClickLocaliserNodeConfig config,
        std::shared_ptr<DataBlock> input,
        ClickLocaliserNodeOutputs outputs);
    ~ClickLocaliserNode() override;

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
    ClickLocaliserNodeConfig config_;
    std::shared_ptr<DataBlock> input_;
    ClickLocaliserNodeOutputs outputs_;
    localisation::DelayGroupEstimator delay_estimator_;
    std::unique_ptr<localisation::FarFieldBearingLocaliser>
        bearing_localiser_;
    Subscription subscription_;
    ModuleState state_ = ModuleState::Created;
    std::uint64_t next_uid_ = 1;
};

} // namespace pamguard::core
