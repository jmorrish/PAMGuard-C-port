#include "pamguard/detectors/ConnectedRegionTracker.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <utility>

namespace pamguard::detectors {

ConnectedRegionTracker::ConnectedRegionTracker(ConnectedRegionConfig config)
    : config_(std::move(config)) {
    if (config_.slice_height == 0) {
        throw std::invalid_argument("connected region slice_height must be non-zero");
    }
    if (config_.connect_type != 4 && config_.connect_type != 8) {
        config_.connect_type = 8;
    }
    reset();
}

const ConnectedRegionConfig& ConnectedRegionTracker::config() const noexcept {
    return config_;
}

void ConnectedRegionTracker::reset() {
    next_region_number_ = 0;
    previous_column_.assign(config_.slice_height, {});
    growing_regions_.clear();
}

std::vector<ConnectedRegionResult> ConnectedRegionTracker::process_slice(
    std::size_t slice_number,
    std::int64_t start_sample,
    std::int64_t time_ms,
    const std::vector<bool>& active_bins,
    const std::vector<double>& magnitude_squared) {
    if (active_bins.size() != config_.slice_height || magnitude_squared.size() != config_.slice_height) {
        throw std::invalid_argument("connected region slice vectors must match slice_height");
    }

    std::vector<std::shared_ptr<Region>> current_column(config_.slice_height);
    for (auto& region : growing_regions_) {
        region->growing = false;
    }

    for (std::size_t row = 0; row < active_bins.size(); ++row) {
        if (!active_bins[row]) {
            continue;
        }

        std::vector<std::shared_ptr<Region>> candidates;
        if (row > 0 && current_column[row - 1]) {
            candidates.push_back(current_column[row - 1]);
        }
        if (config_.connect_type == 8 && row > 0 && previous_column_[row - 1]) {
            candidates.push_back(previous_column_[row - 1]);
        }
        if (previous_column_[row]) {
            candidates.push_back(previous_column_[row]);
        }
        if (config_.connect_type == 8 && row + 1 < previous_column_.size() && previous_column_[row + 1]) {
            candidates.push_back(previous_column_[row + 1]);
        }

        std::shared_ptr<Region> assigned;
        for (const auto& candidate : candidates) {
            if (!candidate) {
                continue;
            }
            if (!assigned) {
                assigned = candidate;
                add_pixel(assigned, slice_number, static_cast<int>(row), start_sample, time_ms, magnitude_squared);
            }
            else if (assigned != candidate) {
                assigned = merge_regions(assigned, candidate, current_column);
            }
        }
        current_column[row] = assigned;
    }

    for (std::size_t row = 0; row < active_bins.size(); ++row) {
        if (!active_bins[row] || current_column[row]) {
            continue;
        }
        if (row > 0 && current_column[row - 1]) {
            current_column[row] = current_column[row - 1];
            add_pixel(current_column[row], slice_number, static_cast<int>(row), start_sample, time_ms, magnitude_squared);
        }
        else {
            current_column[row] = create_region(slice_number, static_cast<int>(row), start_sample, time_ms, magnitude_squared);
        }
    }

    auto completed = find_complete_regions(false);
    previous_column_ = std::move(current_column);
    return completed;
}

std::vector<ConnectedRegionResult> ConnectedRegionTracker::flush() {
    return find_complete_regions(true);
}

std::shared_ptr<ConnectedRegionTracker::Region> ConnectedRegionTracker::create_region(
    std::size_t slice_number,
    int row,
    std::int64_t start_sample,
    std::int64_t time_ms,
    const std::vector<double>& magnitude_squared) {
    auto region = std::make_shared<Region>();
    region->channel = config_.channel;
    region->region_number = next_region_number_++;
    region->first_slice = slice_number;
    region->slice_height = config_.slice_height;
    add_pixel(region, slice_number, row, start_sample, time_ms, magnitude_squared);
    growing_regions_.push_back(region);
    return region;
}

void ConnectedRegionTracker::add_pixel(
    const std::shared_ptr<Region>& region,
    std::size_t slice_number,
    int row,
    std::int64_t start_sample,
    std::int64_t time_ms,
    const std::vector<double>& magnitude_squared) {
    auto found = std::find_if(region->slices.begin(), region->slices.end(), [&](const SliceState& slice) {
        return slice.slice_number == slice_number;
    });
    if (found == region->slices.end()) {
        SliceState slice;
        slice.slice_number = slice_number;
        slice.start_sample = start_sample;
        slice.time_ms = time_ms;
        slice.pixels.assign(config_.slice_height, false);
        slice.magnitude_squared = magnitude_squared;
        region->slices.push_back(std::move(slice));
        found = std::prev(region->slices.end());
    }
    if (!found->pixels[static_cast<std::size_t>(row)]) {
        found->pixels[static_cast<std::size_t>(row)] = true;
        ++region->total_pixels;
    }
    region->growing = true;
}

std::shared_ptr<ConnectedRegionTracker::Region> ConnectedRegionTracker::merge_regions(
    const std::shared_ptr<Region>& r1,
    const std::shared_ptr<Region>& r2,
    std::vector<std::shared_ptr<Region>>& current_column) {
    auto master = r1->first_slice <= r2->first_slice ? r1 : r2;
    auto secondary = master == r1 ? r2 : r1;

    for (const auto& slice : secondary->slices) {
        for (std::size_t row = 0; row < slice.pixels.size(); ++row) {
            if (slice.pixels[row]) {
                add_pixel(master, slice.slice_number, static_cast<int>(row), slice.start_sample, slice.time_ms, slice.magnitude_squared);
            }
        }
    }

    growing_regions_.erase(std::remove(growing_regions_.begin(), growing_regions_.end(), secondary), growing_regions_.end());
    for (auto& region : previous_column_) {
        if (region == secondary) {
            region = master;
        }
    }
    for (auto& region : current_column) {
        if (region == secondary) {
            region = master;
        }
    }
    return master;
}

std::vector<ConnectedRegionResult> ConnectedRegionTracker::find_complete_regions(bool force_all) {
    std::vector<ConnectedRegionResult> completed;
    auto iterator = growing_regions_.begin();
    while (iterator != growing_regions_.end()) {
        const auto& region = *iterator;
        if (!force_all && region->growing) {
            ++iterator;
            continue;
        }
        if (want_region(*region)) {
            auto condensed = condense_region(*region);
            completed.insert(
                completed.end(),
                std::make_move_iterator(condensed.begin()),
                std::make_move_iterator(condensed.end()));
        }
        iterator = growing_regions_.erase(iterator);
    }
    return completed;
}

bool ConnectedRegionTracker::want_region(const Region& region) const {
    if (region.total_pixels < config_.min_pixels) {
        return false;
    }
    if (region.slices.size() < config_.min_length) {
        return false;
    }
    if (config_.reject_first_quarter_second && region.slices.front().start_sample < static_cast<std::int64_t>(config_.sample_rate_hz / 4)) {
        return false;
    }
    return true;
}

std::vector<ConnectedRegionResult> ConnectedRegionTracker::condense_region(Region& region) const {
    region.freq_range = {static_cast<int>(config_.slice_height), 0};
    region.peak_freqs_bins.clear();
    region.times_bins.clear();

    SliceState* previous = nullptr;
    for (auto& slice : region.slices) {
        condense_slice(slice, previous);
        if (!slice.peak_info.empty()) {
            region.freq_range[0] = std::min(region.freq_range[0], slice.peak_info.front()[0]);
            region.freq_range[1] = std::max(region.freq_range[1], slice.peak_info.back()[2]);
        }
        region.times_bins.push_back(static_cast<int>(slice.slice_number));
        region.peak_freqs_bins.push_back(slice.peak_bin);
        previous = &slice;
    }
    if (!config_.keep_shape_stubs) {
        remove_stubs(region);
    }
    if (discard_condensed_region(region)) {
        return {};
    }

    auto build_result = [&](const std::vector<SliceState>& slices, std::size_t total_pixels, std::size_t first_slice) {
        ConnectedRegionResult result;
        result.channel = region.channel;
        result.region_number = region.region_number;
        result.first_slice = first_slice;
        result.start_sample = slices.empty() ? 0 : slices.front().start_sample;
        result.last_start_sample = slices.empty() ? 0 : slices.back().start_sample;
        result.time_ms = slices.empty() ? 0 : slices.front().time_ms;
        result.total_pixels = total_pixels;
        result.freq_range = {static_cast<int>(config_.slice_height), 0};

        std::int64_t inferred_slice_samples = 0;
        for (std::size_t i = 1; i < slices.size(); ++i) {
            const auto step = slices[i].start_sample - slices[i - 1].start_sample;
            if (step > 0 && (inferred_slice_samples == 0 || step < inferred_slice_samples)) {
                inferred_slice_samples = step;
            }
        }

        bool have_peak = false;
        double peak_sum = 0.0;
        std::size_t peak_count = 0;
        auto note_peak = [&](int peak_bin) {
            if (!have_peak) {
                result.min_peak_bin = peak_bin;
                result.max_peak_bin = peak_bin;
                result.start_peak_bin = peak_bin;
                have_peak = true;
            }
            result.min_peak_bin = std::min(result.min_peak_bin, peak_bin);
            result.max_peak_bin = std::max(result.max_peak_bin, peak_bin);
            result.end_peak_bin = peak_bin;
            peak_sum += static_cast<double>(peak_bin);
            ++peak_count;
        };

        for (const auto& slice : slices) {
            if (!slice.peak_info.empty()) {
                result.freq_range[0] = std::min(result.freq_range[0], slice.peak_info.front()[0]);
                result.freq_range[1] = std::max(result.freq_range[1], slice.peak_info.back()[2]);
            }
            for (const auto& peak : slice.peak_info) {
                if (peak.size() > 1) {
                    note_peak(peak[1]);
                }
            }
            result.times_bins.push_back(static_cast<int>(slice.slice_number));
            result.peak_freqs_bins.push_back(slice.peak_bin);

            ConnectedRegionSlice out_slice;
            out_slice.slice_number = slice.slice_number;
            out_slice.start_sample = slice.start_sample;
            out_slice.time_ms = slice.time_ms;
            out_slice.peak_bins = {slice.peak_bin};
            out_slice.peak_info = slice.peak_info;
            result.slices.push_back(std::move(out_slice));
        }
        if (!slices.empty()) {
            result.time_span_samples = std::max<std::int64_t>(0, result.last_start_sample - result.start_sample);
            result.duration_samples = result.time_span_samples + inferred_slice_samples;
            result.time_span_ms = std::max<std::int64_t>(0, slices.back().time_ms - result.time_ms);
            if (config_.sample_rate_hz > 0) {
                const auto sample_rate = static_cast<double>(config_.sample_rate_hz);
                result.time_span_seconds = static_cast<double>(result.time_span_samples) / sample_rate;
                result.duration_seconds = static_cast<double>(result.duration_samples) / sample_rate;
            }
        }
        if (result.freq_range.size() >= 2 && result.freq_range[0] <= result.freq_range[1]) {
            result.min_frequency_bin = result.freq_range[0];
            result.max_frequency_bin = result.freq_range[1];
            result.frequency_span_bins = result.max_frequency_bin - result.min_frequency_bin;
        }
        if (peak_count > 0) {
            result.mean_peak_bin = peak_sum / static_cast<double>(peak_count);
        }
        if (result.time_span_seconds > 0.0) {
            result.peak_sweep_rate_bins_per_second =
                static_cast<double>(result.end_peak_bin - result.start_peak_bin) / result.time_span_seconds;
        }
        return result;
    };

    const bool relink_fragments = config_.fragmentation_method == 3;
    if (config_.fragmentation_method != 2 && !relink_fragments) {
        return {build_result(region.slices, region.total_pixels, region.first_slice)};
    }

    std::size_t max_peaks = 0;
    for (const auto& slice : region.slices) {
        max_peaks = std::max(max_peaks, slice.peak_info.size());
    }
    if (max_peaks <= 1 || region.slices.size() < 2) {
        return {build_result(region.slices, region.total_pixels, region.first_slice)};
    }

    struct Fragment {
        std::vector<SliceState> slices;
        std::size_t total_pixels = 0;
        std::size_t n_joined_start = 0;
        std::size_t n_joined_end = 0;
    };

    auto match_peak = [&](const std::vector<int>& peak1, const std::vector<int>& peak2) {
        if (config_.connect_type == 4) {
            return !(peak1[0] > peak2[2] || peak2[0] > peak1[2]);
        }
        return !(peak1[0] > peak2[2] + 1 || peak2[0] > peak1[2] + 1);
    };

    auto make_fragment_slice = [](const SliceState& source, std::size_t peak_index) {
        SliceState slice;
        slice.slice_number = source.slice_number;
        slice.start_sample = source.start_sample;
        slice.time_ms = source.time_ms;
        slice.magnitude_squared = source.magnitude_squared;
        slice.pixels.assign(source.pixels.size(), false);
        slice.peak_info = {source.peak_info[peak_index]};
        slice.peak_info[0][3] = 0;
        slice.peak_bin = slice.peak_info[0][1];
        for (int bin = slice.peak_info[0][0]; bin <= slice.peak_info[0][2]; ++bin) {
            slice.pixels[static_cast<std::size_t>(bin)] = true;
        }
        return slice;
    };

    auto add_peak = [&](const std::shared_ptr<Fragment>& fragment, const SliceState& slice, std::size_t peak_index) {
        auto out_slice = make_fragment_slice(slice, peak_index);
        fragment->total_pixels += static_cast<std::size_t>(out_slice.peak_info[0][2] - out_slice.peak_info[0][0] + 1);
        fragment->slices.push_back(std::move(out_slice));
    };

    std::vector<std::shared_ptr<Fragment>> fragments;
    std::vector<std::shared_ptr<Fragment>> last_slice_regions(max_peaks);
    std::vector<std::shared_ptr<Fragment>> this_slice_regions(max_peaks);

    auto close_fragment = [&](const std::shared_ptr<Fragment>& fragment, int n_joined_end) {
        if (!fragment || fragment->slices.empty()) {
            return;
        }
        fragment->n_joined_end = static_cast<std::size_t>(std::max(n_joined_end, 0));
        if (!relink_fragments && (fragment->total_pixels < config_.min_pixels || fragment->slices.size() < config_.min_length)) {
            return;
        }
        fragments.push_back(fragment);
    };

    const SliceState* previous_slice = nullptr;
    for (const auto& this_slice : region.slices) {
        std::fill(this_slice_regions.begin(), this_slice_regions.end(), nullptr);
        if (previous_slice == nullptr) {
            for (std::size_t peak = 0; peak < this_slice.peak_info.size(); ++peak) {
                auto fragment = std::make_shared<Fragment>();
                add_peak(fragment, this_slice, peak);
                this_slice_regions[peak] = fragment;
            }
        }
        else {
            bool match_all = false;
            if (previous_slice->peak_info.size() == this_slice.peak_info.size()) {
                match_all = true;
                for (std::size_t peak = 0; peak < previous_slice->peak_info.size(); ++peak) {
                    if (!match_peak(previous_slice->peak_info[peak], this_slice.peak_info[peak])) {
                        match_all = false;
                        break;
                    }
                }
                if (match_all) {
                    for (std::size_t peak = 0; peak < previous_slice->peak_info.size(); ++peak) {
                        add_peak(last_slice_regions[peak], this_slice, peak);
                        this_slice_regions[peak] = last_slice_regions[peak];
                    }
                }
            }

            if (!match_all) {
                std::vector<int> forward_links(max_peaks, -1);
                std::vector<int> back_links(max_peaks, -1);
                std::vector<int> n_forward_links(max_peaks, 0);
                std::vector<int> n_back_links(max_peaks, 0);

                for (std::size_t prev_peak = 0; prev_peak < previous_slice->peak_info.size(); ++prev_peak) {
                    for (std::size_t this_peak = 0; this_peak < this_slice.peak_info.size(); ++this_peak) {
                        if (match_peak(previous_slice->peak_info[prev_peak], this_slice.peak_info[this_peak])) {
                            forward_links[prev_peak] = static_cast<int>(this_peak);
                            ++n_forward_links[prev_peak];
                            back_links[this_peak] = static_cast<int>(prev_peak);
                            ++n_back_links[this_peak];
                        }
                    }
                }

                for (std::size_t prev_peak = 0; prev_peak < previous_slice->peak_info.size(); ++prev_peak) {
                    const auto forward_peak = forward_links[prev_peak];
                    if (n_forward_links[prev_peak] == 1 && forward_peak >= 0 && n_back_links[static_cast<std::size_t>(forward_peak)] == 1) {
                        add_peak(last_slice_regions[prev_peak], this_slice, static_cast<std::size_t>(forward_peak));
                        this_slice_regions[static_cast<std::size_t>(forward_peak)] = last_slice_regions[prev_peak];
                    }
                    else {
                        close_fragment(last_slice_regions[prev_peak], n_forward_links[prev_peak]);
                    }
                }

                for (std::size_t this_peak = 0; this_peak < this_slice.peak_info.size(); ++this_peak) {
                    const auto back_peak = back_links[this_peak];
                    if (n_back_links[this_peak] == 1 && back_peak >= 0 && n_forward_links[static_cast<std::size_t>(back_peak)] == 1) {
                        continue;
                    }
                    auto fragment = std::make_shared<Fragment>();
                    fragment->n_joined_start = static_cast<std::size_t>(std::max(n_back_links[this_peak], 0));
                    add_peak(fragment, this_slice, this_peak);
                    this_slice_regions[this_peak] = fragment;
                }
            }
        }

        previous_slice = &this_slice;
        last_slice_regions = this_slice_regions;
    }

    if (!region.slices.empty()) {
        const auto& last_slice = region.slices.back();
        for (std::size_t peak = 0; peak < last_slice.peak_info.size(); ++peak) {
            close_fragment(last_slice_regions[peak], 0);
        }
    }

    auto clean_fragment = [](const std::shared_ptr<Fragment>& fragment) {
        for (auto& slice : fragment->slices) {
            if (!slice.peak_info.empty()) {
                slice.peak_info[0][3] = 0;
                slice.peak_bin = slice.peak_info[0][1];
            }
        }
    };

    auto is_short_fragment = [&](const std::shared_ptr<Fragment>& fragment) {
        return fragment->total_pixels < config_.min_pixels || fragment->slices.size() < config_.min_length;
    };

    auto match_slice_peaks = [&](const SliceState& first, const SliceState& second) {
        if (first.peak_info.empty() || second.peak_info.empty()) {
            return false;
        }
        if (second.slice_number != first.slice_number + 1) {
            return false;
        }
        return match_peak(first.peak_info[0], second.peak_info[0]);
    };

    auto start_gradient = [](const std::shared_ptr<Fragment>& fragment, std::size_t n_bins) {
        n_bins = std::min(n_bins, fragment->slices.size());
        return static_cast<double>(fragment->slices[n_bins - 1].peak_info[0][1] - fragment->slices.front().peak_info[0][1]) /
            static_cast<double>(n_bins - 1);
    };

    auto end_gradient = [](const std::shared_ptr<Fragment>& fragment, std::size_t n_bins) {
        const auto total_bins = fragment->slices.size();
        n_bins = std::min(n_bins, total_bins);
        return static_cast<double>(fragment->slices.back().peak_info[0][1] - fragment->slices[total_bins - n_bins].peak_info[0][1]) /
            static_cast<double>(n_bins - 1);
    };

    auto short_penalty = [](std::size_t fragment_length) {
        if (fragment_length > 10) {
            return 0.0;
        }
        return static_cast<double>((10 - static_cast<int>(fragment_length)) / 5);
    };

    auto is_cross = [&](const std::shared_ptr<Fragment>& fragment) {
        return fragment->n_joined_end == fragment->n_joined_start &&
            fragment->n_joined_start > 1 &&
            fragment->slices.size() <= config_.max_cross_length;
    };

    auto is_merge = [&](const std::shared_ptr<Fragment>& fragment) {
        return !is_cross(fragment) && fragment->n_joined_start > 1;
    };

    auto is_split = [&](const std::shared_ptr<Fragment>& fragment) {
        return !is_cross(fragment) && fragment->n_joined_end > 1;
    };

    auto merge_fragments = [](const std::shared_ptr<Fragment>& first, const std::shared_ptr<Fragment>& second) {
        first->slices.insert(first->slices.end(), second->slices.begin(), second->slices.end());
        first->total_pixels += second->total_pixels;
        first->n_joined_end = second->n_joined_end;
    };

    auto remove_fragment_at = [&](std::size_t index) {
        fragments.erase(fragments.begin() + static_cast<std::ptrdiff_t>(index));
    };

    auto remove_fragment = [&](const std::shared_ptr<Fragment>& fragment) {
        fragments.erase(std::remove(fragments.begin(), fragments.end(), fragment), fragments.end());
    };

    auto merge_into_best_input = [&](std::size_t merge_index) -> std::shared_ptr<Fragment> {
        const auto& merge_region = fragments[merge_index];
        const auto n_in_out = merge_region->n_joined_start;
        std::vector<std::size_t> ins;
        ins.reserve(n_in_out);
        const auto& merge_slice = merge_region->slices.front();
        for (std::size_t i = 0; i < merge_index; ++i) {
            if (match_slice_peaks(fragments[i]->slices.back(), merge_slice)) {
                ins.push_back(i);
            }
            if (ins.size() == n_in_out) {
                break;
            }
        }
        if (ins.empty()) {
            return nullptr;
        }

        constexpr std::size_t grad_length = 20;
        const double merge_gradient = start_gradient(merge_region, grad_length);
        double best_gradient = std::abs(merge_gradient - end_gradient(fragments[ins[0]], grad_length)) +
            short_penalty(fragments[ins[0]]->slices.size());
        std::size_t best_index = 0;
        for (std::size_t i = 1; i < ins.size(); ++i) {
            const double new_gradient = std::abs(merge_gradient - end_gradient(fragments[ins[i]], grad_length)) +
                short_penalty(fragments[ins[i]]->slices.size());
            if (new_gradient < best_gradient) {
                best_gradient = new_gradient;
                best_index = i;
            }
        }

        auto target = fragments[ins[best_index]];
        merge_fragments(target, merge_region);
        remove_fragment_at(merge_index);
        return target;
    };

    auto branch_to_best_output = [&](std::size_t branch_index) {
        const auto& branch_region = fragments[branch_index];
        const auto n_in_out = branch_region->n_joined_end;
        std::vector<std::size_t> outs;
        outs.reserve(n_in_out);
        const auto& branch_slice = branch_region->slices.back();
        for (std::size_t i = branch_index + 1; i < fragments.size(); ++i) {
            if (match_slice_peaks(branch_slice, fragments[i]->slices.front())) {
                outs.push_back(i);
            }
            if (outs.size() == n_in_out) {
                break;
            }
        }
        if (outs.empty()) {
            return false;
        }

        constexpr std::size_t grad_length = 20;
        const double branch_gradient = end_gradient(branch_region, grad_length);
        double best_gradient = std::abs(branch_gradient - start_gradient(fragments[outs[0]], grad_length)) +
            short_penalty(fragments[outs[0]]->slices.size());
        std::size_t best_index = 0;
        for (std::size_t i = 1; i < outs.size(); ++i) {
            const double new_gradient = std::abs(branch_gradient - start_gradient(fragments[outs[i]], grad_length)) +
                short_penalty(fragments[outs[i]]->slices.size());
            if (new_gradient < best_gradient) {
                best_gradient = new_gradient;
                best_index = i;
            }
        }

        auto target = fragments[outs[best_index]];
        merge_fragments(branch_region, target);
        remove_fragment(target);
        return true;
    };

    auto sorted_indices = [](const std::vector<int>& values) {
        std::vector<std::size_t> indices(values.size());
        for (std::size_t i = 0; i < indices.size(); ++i) {
            indices[i] = i;
        }
        std::sort(indices.begin(), indices.end(), [&](std::size_t left, std::size_t right) {
            return values[left] < values[right];
        });
        return indices;
    };

    auto jump_cross = [&](std::size_t cross_index) {
        const auto& cross_region = fragments[cross_index];
        const auto n_in_out = cross_region->n_joined_end;
        std::vector<std::size_t> ins;
        std::vector<std::size_t> outs;
        std::vector<int> in_freq;
        std::vector<int> out_freq;
        ins.reserve(n_in_out);
        outs.reserve(n_in_out);
        in_freq.reserve(n_in_out);
        out_freq.reserve(n_in_out);

        const auto& first_cross_slice = cross_region->slices.front();
        for (std::size_t i = 0; i < cross_index; ++i) {
            if (match_slice_peaks(fragments[i]->slices.back(), first_cross_slice)) {
                in_freq.push_back(fragments[i]->slices.back().peak_info[0][1]);
                ins.push_back(i);
            }
            if (ins.size() == n_in_out) {
                break;
            }
        }

        const auto& last_cross_slice = cross_region->slices.back();
        for (std::size_t i = cross_index + 1; i < fragments.size(); ++i) {
            if (match_slice_peaks(last_cross_slice, fragments[i]->slices.front())) {
                out_freq.push_back(fragments[i]->slices.front().peak_info[0][1]);
                outs.push_back(i);
            }
            if (outs.size() == n_in_out) {
                break;
            }
        }

        if (outs.size() != ins.size() || outs.size() != n_in_out) {
            return false;
        }

        const auto sorted_ins = sorted_indices(in_freq);
        const auto sorted_outs = sorted_indices(out_freq);
        for (std::size_t i = 0; i < n_in_out; ++i) {
            const auto out_index = n_in_out - 1 - i;
            merge_fragments(fragments[ins[sorted_ins[i]]], fragments[outs[sorted_outs[out_index]]]);
        }
        for (std::size_t i = n_in_out; i > 0; --i) {
            remove_fragment_at(outs[i - 1]);
        }
        remove_fragment_at(cross_index);
        return true;
    };

    if (relink_fragments && fragments.size() >= 3) {
        for (std::size_t i = 0; i < fragments.size(); ++i) {
            auto fragment = fragments[i];
            if (is_cross(fragment)) {
                if (jump_cross(i)) {
                    if (i == 0) {
                        i = static_cast<std::size_t>(-1);
                    }
                    else {
                        --i;
                    }
                }
            }
            else {
                if (is_merge(fragment)) {
                    if (auto merged = merge_into_best_input(i)) {
                        fragment = merged;
                        const auto found = std::find(fragments.begin(), fragments.end(), fragment);
                        if (found != fragments.end()) {
                            i = static_cast<std::size_t>(std::distance(fragments.begin(), found));
                        }
                    }
                }
                if (is_split(fragment)) {
                    branch_to_best_output(i);
                }
            }
        }
    }

    for (auto& fragment : fragments) {
        clean_fragment(fragment);
    }

    if (relink_fragments && fragments.size() >= 2) {
        for (std::size_t i = fragments.size(); i > 0; --i) {
            if (is_short_fragment(fragments[i - 1])) {
                remove_fragment_at(i - 1);
            }
        }
    }

    std::vector<ConnectedRegionResult> results;
    results.reserve(fragments.size());
    for (const auto& fragment : fragments) {
        if (!fragment->slices.empty()) {
            results.push_back(build_result(fragment->slices, fragment->total_pixels, fragment->slices.front().slice_number));
        }
    }
    return results;
}

bool ConnectedRegionTracker::discard_condensed_region(const Region& region) const {
    if (config_.fragmentation_method != 1) {
        return false;
    }
    for (const auto& slice : region.slices) {
        if (slice.peak_info.size() > 1) {
            return true;
        }
    }
    return false;
}

void ConnectedRegionTracker::remove_stubs(Region& region) const {
    const auto n_slices = static_cast<int>(region.slices.size());
    if (n_slices < 2) {
        return;
    }

    const int diag_gap = config_.connect_type == 4 ? 0 : 1;
    auto remove_small_stubs = [&](SliceState& slice, const std::vector<int>& sizes) {
        if (sizes.empty()) {
            return;
        }
        int biggest_size = sizes[0];
        for (std::size_t i = 1; i < sizes.size(); ++i) {
            if (sizes[i] > biggest_size) {
                biggest_size = sizes[i];
            }
        }

        std::vector<std::vector<int>> kept;
        for (std::size_t i = 0; i < sizes.size(); ++i) {
            if (sizes[i] >= static_cast<int>(config_.min_pixels) || sizes[i] == biggest_size) {
                kept.push_back(slice.peak_info[i]);
            }
        }
        slice.peak_info = std::move(kept);
    };

    for (int i = 0; i < n_slices; ++i) {
        auto& slice = region.slices[static_cast<std::size_t>(i)];
        if (slice.peak_info.size() <= 1) {
            continue;
        }
        std::vector<int> sizes(slice.peak_info.size(), 0);
        for (std::size_t p = 0; p < slice.peak_info.size(); ++p) {
            sizes[p] = slice.peak_info[p][2] - slice.peak_info[p][0] + 1;
            sizes[p] = search_stub_size(region.slices, i, static_cast<int>(p), 1, diag_gap, sizes[p]);
        }
        remove_small_stubs(slice, sizes);
    }

    for (int i = n_slices - 1; i >= 0; --i) {
        auto& slice = region.slices[static_cast<std::size_t>(i)];
        if (slice.peak_info.size() <= 1) {
            continue;
        }
        std::vector<int> sizes(slice.peak_info.size(), 0);
        for (std::size_t p = 0; p < slice.peak_info.size(); ++p) {
            sizes[p] = slice.peak_info[p][2] - slice.peak_info[p][0] + 1;
            sizes[p] = search_stub_size(region.slices, i, static_cast<int>(p), -1, diag_gap, sizes[p]);
        }
        remove_small_stubs(slice, sizes);
    }
}

int ConnectedRegionTracker::search_stub_size(
    const std::vector<SliceState>& slices,
    int current_slice,
    int peak_index,
    int search_direction,
    int diag_gap,
    int current_size) const {
    const int n_slices = static_cast<int>(slices.size());
    const int next_slice_index = current_slice + search_direction;
    if (next_slice_index < 0 || next_slice_index >= n_slices - 1 || current_size > static_cast<int>(config_.min_pixels)) {
        return current_size;
    }

    const auto& this_peak = slices[static_cast<std::size_t>(current_slice)].peak_info[static_cast<std::size_t>(peak_index)];
    const auto& next_slice = slices[static_cast<std::size_t>(next_slice_index)];
    const bool end_slice = next_slice_index <= 0 || next_slice_index >= n_slices - 1;
    for (std::size_t p = 0; p < next_slice.peak_info.size(); ++p) {
        const auto& next_peak = next_slice.peak_info[p];
        if (next_peak[0] - this_peak[2] > diag_gap || this_peak[0] - next_peak[2] > diag_gap) {
            continue;
        }
        current_size += next_peak[2] - next_peak[0] + 1;
        if (!end_slice) {
            current_size += search_stub_size(slices, next_slice_index, static_cast<int>(p), search_direction, diag_gap, current_size);
        }
    }
    return current_size;
}

void ConnectedRegionTracker::condense_slice(SliceState& slice, const SliceState* previous_slice) {
    slice.peak_info.clear();
    bool on = false;
    int max_index = 0;
    double max_value = 0.0;
    double peak_max_value = 0.0;
    for (std::size_t i = 0; i < slice.pixels.size(); ++i) {
        if (!slice.pixels[i]) {
            continue;
        }
        const double value = slice.magnitude_squared[i];
        if (value > peak_max_value) {
            peak_max_value = value;
            slice.peak_bin = static_cast<int>(i);
        }
        if (!on) {
            slice.peak_info.push_back({static_cast<int>(i), static_cast<int>(i), static_cast<int>(i), -1});
            max_index = static_cast<int>(i);
            max_value = value;
            on = true;
        }
        else if (value > max_value) {
            max_value = value;
            max_index = static_cast<int>(i);
        }
        if (i + 1 == slice.pixels.size() || !slice.pixels[i + 1]) {
            auto& peak = slice.peak_info.back();
            peak[1] = max_index;
            peak[2] = static_cast<int>(i);
            peak[3] = find_overlapping_peak(peak, previous_slice);
            on = false;
        }
    }
}

int ConnectedRegionTracker::find_overlapping_peak(const std::vector<int>& peak_info, const SliceState* previous_slice) {
    if (!previous_slice) {
        return -1;
    }
    for (std::size_t i = 0; i < previous_slice->peak_info.size(); ++i) {
        const auto& previous_peak = previous_slice->peak_info[i];
        if (previous_peak[0] > peak_info[2] || previous_peak[2] < peak_info[0]) {
            continue;
        }
        return static_cast<int>(i);
    }
    return -1;
}

} // namespace pamguard::detectors
