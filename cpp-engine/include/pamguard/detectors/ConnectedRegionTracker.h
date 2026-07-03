#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace pamguard::detectors {

struct ConnectedRegionConfig {
    std::size_t channel = 0;
    std::size_t slice_height = 0;
    std::uint32_t sample_rate_hz = 48000;
    std::size_t min_pixels = 20;
    std::size_t min_length = 10;
    int connect_type = 8;
    bool reject_first_quarter_second = true;
    bool keep_shape_stubs = false;
    int fragmentation_method = 0;
    std::size_t max_cross_length = 5;
};

struct ConnectedRegionSlice {
    std::size_t slice_number = 0;
    std::int64_t start_sample = 0;
    std::int64_t time_ms = 0;
    std::vector<int> peak_bins;
    std::vector<std::vector<int>> peak_info;
};

struct ConnectedRegionResult {
    std::size_t channel = 0;
    std::size_t region_number = 0;
    std::size_t first_slice = 0;
    std::int64_t start_sample = 0;
    std::int64_t last_start_sample = 0;
    std::int64_t time_ms = 0;
    std::int64_t time_span_samples = 0;
    std::int64_t duration_samples = 0;
    std::int64_t time_span_ms = 0;
    double time_span_seconds = 0.0;
    double duration_seconds = 0.0;
    std::size_t total_pixels = 0;
    int min_frequency_bin = 0;
    int max_frequency_bin = 0;
    int frequency_span_bins = 0;
    int min_peak_bin = 0;
    int max_peak_bin = 0;
    double mean_peak_bin = 0.0;
    int start_peak_bin = 0;
    int end_peak_bin = 0;
    double peak_sweep_rate_bins_per_second = 0.0;
    std::vector<int> freq_range;
    std::vector<int> times_bins;
    std::vector<int> peak_freqs_bins;
    std::vector<ConnectedRegionSlice> slices;
};

class ConnectedRegionTracker {
public:
    explicit ConnectedRegionTracker(ConnectedRegionConfig config);

    [[nodiscard]] const ConnectedRegionConfig& config() const noexcept;
    std::vector<ConnectedRegionResult> process_slice(
        std::size_t slice_number,
        std::int64_t start_sample,
        std::int64_t time_ms,
        const std::vector<bool>& active_bins,
        const std::vector<double>& magnitude_squared);
    std::vector<ConnectedRegionResult> flush();
    void reset();

private:
    struct SliceState {
        std::size_t slice_number = 0;
        std::int64_t start_sample = 0;
        std::int64_t time_ms = 0;
        std::vector<bool> pixels;
        std::vector<double> magnitude_squared;
        std::vector<std::vector<int>> peak_info;
        int peak_bin = 0;
    };

    struct Region {
        std::size_t channel = 0;
        std::size_t region_number = 0;
        std::size_t first_slice = 0;
        std::size_t slice_height = 0;
        std::size_t total_pixels = 0;
        bool growing = false;
        std::vector<SliceState> slices;
        std::vector<int> freq_range;
        std::vector<int> peak_freqs_bins;
        std::vector<int> times_bins;
    };

    ConnectedRegionConfig config_;
    std::size_t next_region_number_ = 0;
    std::vector<std::shared_ptr<Region>> previous_column_;
    std::vector<std::shared_ptr<Region>> growing_regions_;

    std::shared_ptr<Region> create_region(std::size_t slice_number, int row, std::int64_t start_sample, std::int64_t time_ms, const std::vector<double>& magnitude_squared);
    void add_pixel(const std::shared_ptr<Region>& region, std::size_t slice_number, int row, std::int64_t start_sample, std::int64_t time_ms, const std::vector<double>& magnitude_squared);
    std::shared_ptr<Region> merge_regions(const std::shared_ptr<Region>& r1, const std::shared_ptr<Region>& r2, std::vector<std::shared_ptr<Region>>& current_column);
    std::vector<ConnectedRegionResult> find_complete_regions(bool force_all);
    [[nodiscard]] bool want_region(const Region& region) const;
    [[nodiscard]] std::vector<ConnectedRegionResult> condense_region(Region& region) const;
    void remove_stubs(Region& region) const;
    int search_stub_size(const std::vector<SliceState>& slices, int current_slice, int peak_index, int search_direction, int diag_gap, int current_size) const;
    bool discard_condensed_region(const Region& region) const;
    static void condense_slice(SliceState& slice, const SliceState* previous_slice);
    static int find_overlapping_peak(const std::vector<int>& peak_info, const SliceState* previous_slice);
};

} // namespace pamguard::detectors
