#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <numbers>
#include <vector>

#include "pamguard/core/AnalysisSession.h"
#include "pamguard/dsp/IirFilter.h"

namespace {

constexpr double kPi = std::numbers::pi;

using pamguard::dsp::FastIirFilter;
using pamguard::dsp::IirFilterBand;
using pamguard::dsp::IirFilterParams;
using pamguard::dsp::IirFilterType;

IirFilterParams make_params(IirFilterType type, IirFilterBand band, int order, float high_pass,
                            float low_pass, double ripple) {
    IirFilterParams params;
    params.type = type;
    params.band = band;
    params.order = order;
    params.high_pass_freq_hz = high_pass;
    params.low_pass_freq_hz = low_pass;
    params.pass_band_ripple_db = ripple;
    return params;
}

std::map<std::string, IirFilterParams> case_catalogue() {
    using T = IirFilterType;
    using B = IirFilterBand;
    return {
        {"click-prefilter-hp4-500", make_params(T::Butterworth, B::HighPass, 4, 500.0F, 0.0F, 2.0)},
        {"click-trigger-hp2-2000", make_params(T::Butterworth, B::HighPass, 2, 2000.0F, 0.0F, 2.0)},
        {"butter-lp6-8000", make_params(T::Butterworth, B::LowPass, 6, 0.0F, 8000.0F, 2.0)},
        {"butter-lp3-4000", make_params(T::Butterworth, B::LowPass, 3, 0.0F, 4000.0F, 2.0)},
        {"butter-hp5-1000", make_params(T::Butterworth, B::HighPass, 5, 1000.0F, 0.0F, 2.0)},
        {"butter-bp4-2000-5000", make_params(T::Butterworth, B::BandPass, 4, 2000.0F, 5000.0F, 2.0)},
        {"butter-bs4-2000-5000", make_params(T::Butterworth, B::BandStop, 4, 2000.0F, 5000.0F, 2.0)},
        {"cheby-lp4-6000-r2", make_params(T::Chebyshev, B::LowPass, 4, 0.0F, 6000.0F, 2.0)},
        {"cheby-hp3-1500-r1", make_params(T::Chebyshev, B::HighPass, 3, 1500.0F, 0.0F, 1.0)},
        {"cheby-bp4-1000-4000-r2", make_params(T::Chebyshev, B::BandPass, 4, 1000.0F, 4000.0F, 2.0)},
    };
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: iir_filter_fixture_check <fixture.csv>\n";
        return 2;
    }

    try {
        std::ifstream input(argv[1]);
        if (!input) {
            throw std::runtime_error(std::string("could not open fixture: ") + argv[1]);
        }
        std::map<std::string, std::vector<double>> series;
        std::string line;
        while (std::getline(input, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
                line.pop_back();
            }
            if (line.empty() || line.rfind("case,", 0) == 0) {
                continue;
            }
            std::stringstream stream(line);
            std::string name;
            std::string index_text;
            std::string value_text;
            std::getline(stream, name, ',');
            std::getline(stream, index_text, ',');
            std::getline(stream, value_text, ',');
            series[name].push_back(std::stod(value_text));
        }
        const auto signal_found = series.find("input");
        if (signal_found == series.end()) {
            throw std::runtime_error("fixture is missing the input signal");
        }
        const auto& signal = signal_found->second;

        const auto catalogue = case_catalogue();
        // The recursion feeds back through the state, so late samples carry
        // hundreds of accumulated rounding steps; 1e-9 against O(1) values
        // still catches any real design or ordering mistake instantly.
        constexpr double tolerance = 1e-9;
        double max_abs_error = 0.0;
        std::size_t checked = 0;
        for (const auto& [name, expected] : series) {
            if (name == "input") {
                continue;
            }
            const auto params = catalogue.find(name);
            if (params == catalogue.end()) {
                std::cerr << "Fixture case " << name << " has no catalogue entry\n";
                return 1;
            }
            FastIirFilter filter(48000.0, params->second);
            if (!filter.active()) {
                std::cerr << "Case " << name << " designed no usable filter\n";
                return 1;
            }
            std::vector<double> produced;
            filter.run(signal, produced);
            if (produced.size() != expected.size()) {
                std::cerr << "Case " << name << " length mismatch\n";
                return 1;
            }
            for (std::size_t i = 0; i < produced.size(); ++i) {
                const double error = std::abs(produced[i] - expected[i]);
                max_abs_error = std::max(max_abs_error, error);
                if (error > tolerance) {
                    std::cerr << "Case " << name << " sample " << i << ": fixture=" << expected[i]
                              << " ported=" << produced[i] << "\n";
                    return 1;
                }
            }
            ++checked;
        }

        // The parity gap this port closes, demonstrated end to end: a strong
        // low-frequency rumble burst triggers an unfiltered click detector,
        // and PAMGuard's default prefilter + trigger filter reject it while
        // a genuine broadband transient still gets through.
        const auto make_session = [](bool with_filters) {
            pamguard::core::AnalysisConfig config;
            config.session_id = "iir-gate-check";
            config.sample_rate_hz = 48000;
            config.channel_count = 1;
            config.detector.click_detector_enabled = true;
            config.detector.click.channel_bitmap = 1;
            config.detector.click.trigger_bitmap = 1;
            config.detector.click.threshold_db = 10.0;
            config.detector.click.short_filter = 0.1;
            config.detector.click.long_filter = 0.00001;
            config.detector.click.pre_sample = 10;
            config.detector.click.post_sample = 12;
            config.detector.click.min_sep = 48;
            config.detector.click.max_length = 512;
            config.detector.click.min_trigger_channels = 1;
            if (with_filters) {
                config.detector.click.pre_filter =
                    {IirFilterType::Butterworth, IirFilterBand::HighPass, 4, 0.0F, 500.0F, 2.0};
                config.detector.click.pre_filter.high_pass_freq_hz = 500.0F;
                config.detector.click.pre_filter.low_pass_freq_hz = 0.0F;
                config.detector.click.trigger_filter =
                    {IirFilterType::Butterworth, IirFilterBand::HighPass, 2, 0.0F, 0.0F, 2.0};
                config.detector.click.trigger_filter.high_pass_freq_hz = 2000.0F;
            }
            return config;
        };
        const auto make_gate_chunk = [](bool rumble, bool transient) {
            pamguard::core::AudioChunk chunk;
            chunk.start_sample = 0;
            chunk.sample_rate_hz = 48000;
            chunk.channel_count = 1;
            chunk.interleaved_pcm.assign(8192, 0.0);
            for (std::size_t n = 0; n < chunk.interleaved_pcm.size(); ++n) {
                double value = 0.002 * std::sin(static_cast<double>(n) * 2.1);
                if (rumble && n >= 2000 && n < 4400) {
                    // 100 Hz burst: energetic, but entirely below the 500 Hz
                    // prefilter and 2 kHz trigger filter.
                    value += 0.9 * std::sin(2.0 * kPi * 100.0 * static_cast<double>(n) / 48000.0);
                }
                if (transient && n >= 6000 && n <= 6006) {
                    value += ((n & 1u) == 0 ? 0.9 : -0.9);
                }
                chunk.interleaved_pcm[n] = value;
            }
            return chunk;
        };

        {
            pamguard::core::AnalysisSession unfiltered(make_session(false));
            const auto rumble_hits = unfiltered.process(make_gate_chunk(true, false));
            if (rumble_hits.clicks.empty()) {
                std::cerr << "An unfiltered detector should trigger on the low-frequency rumble\n";
                return 1;
            }
            pamguard::core::AnalysisSession filtered(make_session(true));
            const auto rumble_rejected = filtered.process(make_gate_chunk(true, false));
            if (!rumble_rejected.clicks.empty()) {
                std::cerr << "PAMGuard's default filters should reject the rumble, got "
                          << rumble_rejected.clicks.size() << " clicks\n";
                return 1;
            }
            pamguard::core::AnalysisSession filtered_transient(make_session(true));
            const auto transient_kept = filtered_transient.process(make_gate_chunk(true, true));
            if (transient_kept.clicks.empty()) {
                std::cerr << "A broadband transient should survive the filters\n";
                return 1;
            }
        }

        std::cout << "IIR filter parity passed\n";
        std::cout << "cases=" << checked << " samples=" << signal.size()
                  << " max_abs_error=" << max_abs_error << "\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
