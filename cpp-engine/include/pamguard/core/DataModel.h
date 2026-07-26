#pragma once

#include <any>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pamguard::core {

using DataTypeId = std::string;
using DataBlockId = std::string;

struct DataUnitMetadata {
    DataTypeId type_id;
    std::uint32_t schema_version = 1;
    DataBlockId source_block_id;
    std::uint64_t uid = 0;
    std::uint64_t sequence = 0;
    std::int64_t time_unix_ms = 0;
    std::int64_t start_sample = 0;
    std::uint64_t duration_samples = 0;
    std::uint32_t channel_bitmap = 0;
    std::uint32_t sequence_bitmap = 0;
    std::string clock_domain_id;
    bool discontinuity = false;
};

struct DataUnit {
    DataUnitMetadata metadata;
    std::any payload;
};

template <typename Payload>
DataUnit make_data_unit(DataUnitMetadata metadata, Payload payload) {
    return {std::move(metadata), std::any(std::move(payload))};
}

struct DataBlockDescriptor {
    DataBlockId id;
    std::string name;
    std::string producer_module_id;
    std::string producer_port_id;
    DataTypeId data_type;
    std::uint32_t schema_version = 1;
    double sample_rate_hz = 0.0;
    std::uint32_t channel_bitmap = 0;
    std::uint32_t sequence_bitmap = 0;
    std::optional<double> minimum_frequency_hz;
    std::optional<double> maximum_frequency_hz;
    /**
     * Per-channel additive calibration for converting 20*log10(raw
     * amplitude) to the source's calibrated dB reference. Empty means the
     * source is explicitly uncalibrated.
     */
    std::vector<double> calibration_db_offset_by_channel;
    std::vector<std::string> capabilities;
    std::size_t history_capacity = 0;
    std::string clock_domain_id;
    std::string retention_policy = "bounded-history";
    std::vector<std::string> persistence_providers;
    std::vector<std::string> export_providers;
    /** FFT source geometry; unset for non-FFT or legacy external blocks. */
    std::optional<std::size_t> fft_length;
    std::optional<std::size_t> fft_hop;
    /**
     * Acquisition analogue full-scale voltage, propagated through raw-audio
     * branches. LevelMeterSidePanel uses this for its "Volts" reference.
     */
    std::optional<double> volts_peak_to_peak;
};

enum class DeliveryMode {
    Synchronous,
    QueuedDropOldest,
};

struct SubscriptionOptions {
    DeliveryMode delivery_mode = DeliveryMode::Synchronous;
    std::size_t queue_capacity = 32;
};

struct DataBlockStats {
    std::uint64_t published = 0;
    std::uint64_t delivered = 0;
    std::uint64_t dropped = 0;
    std::uint64_t observer_errors = 0;
    std::size_t subscriber_count = 0;
    std::size_t history_size = 0;
    std::size_t queued_units = 0;
    std::size_t maximum_queued_units = 0;
};

class Subscription {
public:
    Subscription() = default;
    explicit Subscription(std::function<void()> cancel);
    ~Subscription();

    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
    Subscription(Subscription&& other) noexcept;
    Subscription& operator=(Subscription&& other) noexcept;

    void cancel();
    [[nodiscard]] bool active() const noexcept;

private:
    std::function<void()> cancel_;
};

class DataBlock {
public:
    using Observer = std::function<void(const DataUnit&)>;

    explicit DataBlock(DataBlockDescriptor descriptor);
    ~DataBlock();

    DataBlock(const DataBlock&) = delete;
    DataBlock& operator=(const DataBlock&) = delete;

    [[nodiscard]] const DataBlockDescriptor& descriptor() const noexcept;
    [[nodiscard]] Subscription subscribe(
        Observer observer,
        SubscriptionOptions options = {});
    void publish(DataUnit unit);
    [[nodiscard]] std::vector<DataUnit> recent_history() const;
    [[nodiscard]] DataBlockStats stats() const;
    /**
     * Assign an inherited clock domain while a runtime graph is being built.
     * This is rejected after publication or subscription begins.
     */
    void configure_clock_domain(std::string clock_domain_id);
    void configure_calibration(
        std::vector<double> calibration_db_offset_by_channel);
    void configure_voltage_scale(double volts_peak_to_peak);

private:
    class State;
    std::shared_ptr<State> state_;
};

} // namespace pamguard::core
