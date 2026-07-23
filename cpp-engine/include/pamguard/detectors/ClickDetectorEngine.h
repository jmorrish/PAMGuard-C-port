#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

#include "pamguard/core/AudioFrame.h"
#include "pamguard/dsp/IirFilter.h"

namespace pamguard::detectors {

struct ClickDetectorConfig {
    std::uint32_t channel_bitmap = 0x1;
    std::uint32_t trigger_bitmap = 0xFFFFFFFFu;
    std::size_t min_trigger_channels = 1;
    double threshold_db = 10.0;
    double long_filter = 0.00001;
    double short_filter = 0.1;
    std::size_t pre_sample = 40;
    std::size_t post_sample = 40;
    std::size_t min_sep = 100;
    std::size_t max_length = 1024;
    /**
     * PAMGuard's ClickDetector runs the audio through two IIR filters before
     * anything else sees it: `preFilter` conditions the waveform (and is what
     * click waveforms are captured from), and `triggerFilter` runs on the
     * prefiltered data to feed the trigger alone. ClickParameters' own
     * defaults are a 4th-order 500 Hz Butterworth highpass and a 2nd-order
     * 2 kHz one; the engine defaults to None so existing sessions keep their
     * behaviour, and the project importer carries PAMGuard's values across.
     */
    dsp::IirFilterParams pre_filter;
    dsp::IirFilterParams trigger_filter;
};

struct ClickDetectionResult {
    std::uint32_t channel_bitmap = 0;
    std::uint32_t trigger_bitmap = 0;
    std::int64_t start_sample = 0;
    std::size_t duration_samples = 0;
    std::int64_t time_unix_ms = 0;
    double signal_excess_db = 0.0;
    std::vector<std::size_t> channels;
    std::vector<std::vector<double>> waveform;
    /**
     * Set by the session's echo gate (PAMGuard ClickDetection.setEcho) when
     * online echo detection runs without discarding. Discarded echoes are
     * removed before any consumer sees them, so a true flag only appears in
     * flag-only mode.
     */
    bool echo = false;
};

class ClickDetectorEngine {
public:
    explicit ClickDetectorEngine(ClickDetectorConfig config);

    [[nodiscard]] const ClickDetectorConfig& config() const noexcept;
    std::vector<ClickDetectionResult> process(const core::AudioChunk& chunk);
    void reset();

private:
    enum class ClickStatus {
        ClickOn,
        ClickEnding,
        ClickOff
    };

    class TriggerFilter {
    public:
        TriggerFilter() = default;
        TriggerFilter(double alpha, double initial_value);
        TriggerFilter(double alpha1, double alpha2, double initial_value);

        double run(double new_value, bool over_threshold);
        void set_memory(double memory) noexcept;

    private:
        double alpha_[2]{0.0, 0.0};
        double alpha_1_[2]{1.0, 1.0};
        double memory_ = 0.0;
    };

    ClickDetectorConfig config_;
    std::vector<std::size_t> channels_;
    std::vector<TriggerFilter> short_filters_;
    std::vector<TriggerFilter> long_filters_;
    /** Per-channel IIR state, persistent across chunks like PAMGuard's. */
    std::vector<dsp::FastIirFilter> pre_iir_filters_;
    std::vector<dsp::FastIirFilter> trigger_iir_filters_;
    bool iir_filters_built_ = false;
    /** Scratch: this chunk's prefiltered and trigger-filtered channel data. */
    std::vector<std::vector<double>> prefiltered_;
    std::vector<std::vector<double>> trigger_data_;
    bool filters_initialized_ = false;
    ClickStatus click_status_ = ClickStatus::ClickOff;
    std::uint64_t samples_processed_ = 0;
    std::int64_t click_start_sample_ = 0;
    std::int64_t click_end_sample_ = 0;
    double max_signal_excess_db_ = 0.0;
    std::uint32_t click_triggers_ = 0;
    std::uint32_t over_threshold_ = 0;
    std::size_t down_count_ = 0;
    std::size_t up_count_ = 0;
    std::vector<std::deque<double>> waveform_history_;
    std::uint64_t history_start_sample_ = 0;
    bool history_initialized_ = false;

    void setup_channels();
    void initialize_filters(const core::AudioChunk& chunk);
    void append_waveform_history(const core::AudioChunk& chunk);
    void trim_waveform_history();
    [[nodiscard]] std::vector<std::vector<double>> extract_waveform(std::int64_t start_sample, std::size_t duration) const;
    [[nodiscard]] bool channel_is_triggered(std::size_t channel) const noexcept;
    [[nodiscard]] std::int64_t estimate_time_ms(const core::AudioChunk& chunk, std::int64_t start_sample) const;
};

} // namespace pamguard::detectors
