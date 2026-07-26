#pragma once

#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "pamguard/core/DataModel.h"
#include "pamguard/core/ModuleGraph.h"
#include "pamguard/core/ModuleNode.h"
#include "pamguard/core/OperatorNodes.h"
#include "pamguard/core/SignalNodes.h"

namespace pamguard::core {

struct ModuleRuntimeStatus {
    ModuleInstanceId instance_id;
    ModuleState state = ModuleState::Created;
};

class ModuleRuntime {
public:
    ModuleRuntime() = default;
    ~ModuleRuntime();

    ModuleRuntime(const ModuleRuntime&) = delete;
    ModuleRuntime& operator=(const ModuleRuntime&) = delete;

    /** Build and prepare an entire graph transactionally. Runtime must be stopped. */
    void configure(const ModuleGraphDocument& document);
    /**
     * Exchange two fully prepared, stopped runtimes without rebuilding nodes.
     *
     * This is the commit/rollback primitive used after project preflight.
     * Both runtimes remain valid; the other instance receives the previous
     * prepared graph.
     */
    void swap_stopped(ModuleRuntime& other);
    void start();
    void stop();
    void flush();
    void reset();

    [[nodiscard]] bool running() const;
    [[nodiscard]] std::uint64_t revision() const;
    [[nodiscard]] std::vector<DataBlockDescriptor> data_blocks() const;
    [[nodiscard]] std::vector<ModuleRuntimeStatus> module_statuses() const;
    [[nodiscard]] std::shared_ptr<DataBlock> find_block(
        const DataBlockId& id) const;

    void ingest(const ModuleInstanceId& acquisition_module_id, AudioChunk chunk);
    void publish_operator_event(
        const ModuleInstanceId& module_id,
        GraphOperatorEvent event,
        std::int64_t time_unix_ms = 0,
        std::int64_t start_sample = 0);
    [[nodiscard]] SoundRecorderCommandResult
    set_sound_recorder_transport(
        const ModuleInstanceId& module_id,
        SoundRecorderTransportState state);
    [[nodiscard]] SoundRecorderNodeStatus
    sound_recorder_status(
        const ModuleInstanceId& module_id) const;

    [[nodiscard]] static DataBlockId block_id(
        const ModuleInstanceId& module_id,
        const PortId& port_id);

private:
    struct PreparedRuntime {
        std::uint64_t revision = 0;
        std::vector<std::unique_ptr<ModuleNode>> nodes;
        std::unordered_map<ModuleInstanceId, AudioSourceNode*> sources;
        std::unordered_map<ModuleInstanceId, OperatorInputNode*>
            operator_inputs;
        std::unordered_map<ModuleInstanceId, SoundRecorderNode*>
            sound_recorders;
        std::unordered_map<DataBlockId, std::shared_ptr<DataBlock>> blocks;
    };

    [[nodiscard]] static PreparedRuntime build(
        const ModuleGraphDocument& document);

    mutable std::shared_mutex mutex_;
    PreparedRuntime runtime_;
    bool running_ = false;
};

} // namespace pamguard::core
