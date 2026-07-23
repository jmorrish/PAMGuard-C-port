#include "pamguard/detectors/NoiseBandMonitor.h"

#include <algorithm>
#include <cmath>

namespace pamguard::detectors {

double noise_band_ratio(NoiseBandType type) {
    switch (type) {
    case NoiseBandType::Octave:
        return 2.0;
    case NoiseBandType::ThirdOctave:
        return std::pow(2.0, 1.0 / 3.0);
    case NoiseBandType::Decidecade:
        return std::pow(10.0, 0.1);
    case NoiseBandType::Decade:
        return 10.0;
    case NoiseBandType::TenthOctave:
        return std::pow(2.0, 0.1);
    case NoiseBandType::TwelfthOctave:
        return std::pow(2.0, 1.0 / 12.0);
    }
    return 0.0;
}

int noise_band_decimate_factor(NoiseBandType type) {
    return type == NoiseBandType::Decade ? 10 : 2;
}

std::vector<NoiseBand> calculate_noise_bands(NoiseBandType type, double min_frequency_hz,
                                             double max_frequency_hz, double reference_frequency_hz) {
    const double band_ratio = noise_band_ratio(type);
    const double half_band = std::sqrt(band_ratio);

    // BandData.getMinBand / getMaxBand: walk the reference frequency in band
    // steps until it brackets the requested range.
    int max_band = 0;
    {
        double f = reference_frequency_hz;
        while (f > max_frequency_hz / half_band) {
            f /= band_ratio;
            --max_band;
        }
        while (f <= max_frequency_hz / half_band / band_ratio) {
            f *= band_ratio;
            ++max_band;
        }
    }
    int min_band = 0;
    {
        double f = reference_frequency_hz;
        while (f < min_frequency_hz * half_band) {
            f *= band_ratio;
            ++min_band;
        }
        while (f >= min_frequency_hz * half_band * band_ratio) {
            f /= band_ratio;
            --min_band;
        }
    }
    const int band_count = max_band - min_band + 1;
    std::vector<NoiseBand> bands;
    if (band_count <= 0) {
        return bands;
    }
    bands.reserve(static_cast<std::size_t>(band_count));
    double centre = reference_frequency_hz * std::pow(band_ratio, min_band);
    for (int i = 0; i < band_count; ++i) {
        bands.push_back({centre, centre / half_band, centre * half_band});
        centre *= band_ratio;
    }
    return bands;
}

NoiseBandMonitor::NoiseBandMonitor(double sample_rate_hz, const NoiseBandConfig& config)
    : config_(config), sample_rate_hz_(sample_rate_hz) {
    if (sample_rate_hz <= 0.0 || config.iir_order <= 0 || config.output_interval_seconds <= 0.0) {
        return;
    }
    const double max_frequency = config.max_frequency_hz > 0.0
                                     ? std::min(config.max_frequency_hz, sample_rate_hz / 2.0)
                                     : sample_rate_hz / 2.0;
    bands_ = calculate_noise_bands(config.band_type, config.min_frequency_hz, max_frequency,
                                   config.reference_frequency_hz);
    if (bands_.empty()) {
        return;
    }

    // makeDecimatorFilters: LP Butterworth of order iirOrder + 2 at the next
    // stage's Nyquist, while the decimated Nyquist stays a band-gap above the
    // lowest band's high edge.
    const int decimate_step = noise_band_decimate_factor(config.band_type);
    const double band_gap = std::sqrt(2.0);
    const double min_nyquist = bands_.front().hi_edge_hz * band_gap;
    struct DecimatorPlan {
        double input_rate = 0.0;
        double output_rate = 0.0;
        float low_pass_freq = 0.0F;
    };
    std::vector<DecimatorPlan> plans;
    double current_rate = sample_rate_hz;
    while (current_rate / 2.0 / decimate_step > min_nyquist) {
        DecimatorPlan plan;
        plan.input_rate = current_rate;
        plan.low_pass_freq = static_cast<float>(current_rate / 2.0 / decimate_step);
        current_rate /= decimate_step;
        plan.output_rate = current_rate;
        plans.push_back(plan);
    }

    // Groups: index 0 has no decimator; group d+1 holds decimator d. Bands
    // attach to the deepest decimator whose lowpass clears their high edge by
    // the band gap (findDecimatorIndex).
    groups_.resize(plans.size() + 1);
    for (std::size_t d = 0; d < plans.size(); ++d) {
        dsp::IirFilterParams params;
        params.type = dsp::IirFilterType::Butterworth;
        params.band = dsp::IirFilterBand::LowPass;
        params.order = config.iir_order + 2;
        params.low_pass_freq_hz = plans[d].low_pass_freq;
        groups_[d + 1].decimation_filter.emplace(plans[d].input_rate, params);
        groups_[d + 1].decimate_factor = decimate_step;
        if (!groups_[d + 1].decimation_filter->active()) {
            return;
        }
    }

    outputs_.assign(bands_.size(), {});
    // makeBandFilters iterates bands in DESCENDING frequency; each gets a
    // bandpass at its group's decimated rate. The group's band lists keep
    // that construction order; reporting stays ascending via band_indices.
    for (std::size_t i = bands_.size(); i-- > 0;) {
        int decimator_index = -1;
        for (int d = static_cast<int>(plans.size()) - 1; d >= 0; --d) {
            if (static_cast<double>(plans[static_cast<std::size_t>(d)].low_pass_freq) >
                bands_[i].hi_edge_hz * band_gap) {
                decimator_index = d;
                break;
            }
        }
        const double band_rate = decimator_index < 0
                                     ? sample_rate_hz
                                     : plans[static_cast<std::size_t>(decimator_index)].output_rate;
        dsp::IirFilterParams params;
        params.type = dsp::IirFilterType::Butterworth;
        params.band = dsp::IirFilterBand::BandPass;
        params.order = config.iir_order;
        params.low_pass_freq_hz = static_cast<float>(bands_[i].hi_edge_hz);
        params.high_pass_freq_hz = static_cast<float>(bands_[i].lo_edge_hz);
        auto& group = groups_[static_cast<std::size_t>(decimator_index + 1)];
        group.band_filters.emplace_back(band_rate, params);
        group.band_indices.push_back(i);
        if (!group.band_filters.back().active()) {
            return;
        }
    }

    interval_samples_ = static_cast<std::uint64_t>(config.output_interval_seconds * sample_rate_hz);
    if (interval_samples_ == 0) {
        return;
    }
    valid_ = true;
}

std::optional<NoiseBandLevels> NoiseBandMonitor::process(const std::vector<double>& samples,
                                                         std::int64_t start_sample,
                                                         std::int64_t time_unix_ms) {
    if (!valid_) {
        return std::nullopt;
    }

    // The decimation chain: each group filters + decimates the previous
    // group's output, then runs its band filters into the accumulators.
    const std::vector<double>* input = &samples;
    for (auto& group : groups_) {
        std::size_t out_count = 0;
        if (!group.decimation_filter.has_value()) {
            group.decimated = *input;
            out_count = group.decimated.size();
        }
        else {
            group.decimated.resize(input->size() / static_cast<std::size_t>(group.decimate_factor) + 1);
            int sample_index = group.decimator_offset;
            const int n_samples = static_cast<int>(input->size());
            std::size_t produced = 0;
            // decimateData: lowpass every sample, keep every Nth, carrying the
            // offset so odd chunk lengths stay aligned across calls.
            std::vector<double> prefiltered(input->size());
            for (std::size_t i = 0; i < input->size(); ++i) {
                prefiltered[i] = group.decimation_filter->run_sample((*input)[i]);
            }
            int cursor = sample_index;
            for (; cursor < n_samples; cursor += group.decimate_factor) {
                group.decimated[produced++] = prefiltered[static_cast<std::size_t>(cursor)];
            }
            group.decimator_offset = cursor - n_samples;
            out_count = produced;
        }
        group.decimated.resize(out_count);
        for (std::size_t f = 0; f < group.band_filters.size(); ++f) {
            auto& output = outputs_[group.band_indices[f]];
            for (std::size_t s = 0; s < out_count; ++s) {
                const double value = group.band_filters[f].run_sample(group.decimated[s]);
                ++output.samples;
                output.max_value = std::max(std::abs(value), output.max_value);
                output.sum_squared += value * value;
            }
        }
        input = &group.decimated;
    }

    samples_into_interval_ += samples.size();
    if (samples_into_interval_ < interval_samples_) {
        return std::nullopt;
    }
    samples_into_interval_ = 0;

    NoiseBandLevels levels;
    levels.end_sample = start_sample + static_cast<std::int64_t>(samples.size());
    levels.time_unix_ms = time_unix_ms;
    levels.rms.reserve(bands_.size());
    levels.peak.reserve(bands_.size());
    for (auto& output : outputs_) {
        levels.rms.push_back(output.samples > 0 ? std::sqrt(output.sum_squared / static_cast<double>(output.samples))
                                                : 0.0);
        levels.peak.push_back(output.max_value);
        output = {};
    }
    return levels;
}

} // namespace pamguard::detectors
