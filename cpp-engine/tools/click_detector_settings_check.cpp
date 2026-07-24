#include <cmath>
#include <iostream>
#include <vector>

#include "pamguard/core/AnalysisSession.h"
#include "pamguard/detectors/ClickDetectorEngine.h"

namespace {

pamguard::core::AudioChunk click_chunk() {
    pamguard::core::AudioChunk chunk;
    chunk.start_sample = 0;
    chunk.time_unix_ms = 0;
    chunk.sample_rate_hz = 48000;
    chunk.channel_count = 2;
    chunk.interleaved_pcm.resize(256 * chunk.channel_count);
    for (std::size_t sample = 0; sample < 256; ++sample) {
        for (std::size_t channel = 0; channel < chunk.channel_count; ++channel) {
            double value =
                0.01 * std::sin(static_cast<double>(sample) * 0.13 +
                                static_cast<double>(channel) * 0.31);
            if (sample >= 80 && sample <= 86) {
                value += ((sample & 1u) == 0 ? 1.0 : -1.0) *
                         (channel == 0 ? 1.0 : 0.82);
            }
            chunk.interleaved_pcm[sample * chunk.channel_count + channel] = value;
        }
    }
    return chunk;
}

pamguard::detectors::ClickDetectorConfig unfiltered_click_config() {
    pamguard::detectors::ClickDetectorConfig config;
    config.channel_bitmap = 3;
    config.trigger_bitmap = 3;
    config.pre_sample = 10;
    config.post_sample = 12;
    config.min_sep = 8;
    config.max_length = 128;
    config.pre_filter.type = pamguard::dsp::IirFilterType::None;
    config.trigger_filter.type = pamguard::dsp::IirFilterType::None;
    return config;
}

bool same_clicks(const std::vector<pamguard::detectors::ClickDetectionResult>& a,
                 const std::vector<pamguard::detectors::ClickDetectionResult>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].start_sample != b[i].start_sample ||
            a[i].duration_samples != b[i].duration_samples ||
            a[i].channel_bitmap != b[i].channel_bitmap ||
            a[i].trigger_bitmap != b[i].trigger_bitmap ||
            a[i].signal_excess_db != b[i].signal_excess_db ||
            a[i].waveform != b[i].waveform) {
            return false;
        }
    }
    return true;
}

pamguard::core::AnalysisConfig base_session() {
    pamguard::core::AnalysisConfig config;
    config.session_id = "click-settings-check";
    config.sample_rate_hz = 48000;
    config.channel_count = 2;
    config.detector.click_detector_enabled = true;
    config.detector.click = unfiltered_click_config();
    return config;
}

} // namespace

int main() {
    try {
        {
            const pamguard::detectors::ClickDetectorConfig defaults;
            if (defaults.channel_bitmap != 1 || defaults.trigger_bitmap != 0xFFFFFFFFu ||
                defaults.min_trigger_channels != 1 || defaults.threshold_db != 10.0 ||
                defaults.long_filter != 0.00001 || defaults.long_filter_2 != 0.000001 ||
                defaults.short_filter != 0.1 || defaults.pre_sample != 40 ||
                defaults.post_sample != 40 || defaults.min_sep != 100 ||
                defaults.max_length != 1024 ||
                !defaults.sample_noise ||
                defaults.noise_sample_interval_seconds != 5.0 ||
                !defaults.store_background ||
                defaults.background_interval_milliseconds != 5000 ||
                defaults.publish_trigger_function ||
                defaults.pre_filter.type != pamguard::dsp::IirFilterType::Butterworth ||
                defaults.pre_filter.band != pamguard::dsp::IirFilterBand::HighPass ||
                defaults.pre_filter.order != 4 ||
                defaults.pre_filter.high_pass_freq_hz != 500.0F ||
                defaults.pre_filter.low_pass_freq_hz != 20000.0F ||
                defaults.trigger_filter.type != pamguard::dsp::IirFilterType::Butterworth ||
                defaults.trigger_filter.band != pamguard::dsp::IirFilterBand::HighPass ||
                defaults.trigger_filter.order != 2 ||
                defaults.trigger_filter.high_pass_freq_hz != 2000.0F ||
                defaults.trigger_filter.low_pass_freq_hz != 20000.0F) {
                std::cerr << "C++ click defaults do not match ClickParameters\n";
                return 1;
            }
        }

        {
            auto first = unfiltered_click_config();
            auto second = first;
            first.long_filter_2 = 0.0000001;
            second.long_filter_2 = 0.1;
            pamguard::detectors::ClickDetectorEngine engine_a(first);
            pamguard::detectors::ClickDetectorEngine engine_b(second);
            if (!same_clicks(engine_a.process(click_chunk()),
                             engine_b.process(click_chunk()))) {
                std::cerr << "longFilter2 must remain a pinned-Java runtime no-op\n";
                return 1;
            }
        }

        {
            auto config = unfiltered_click_config();
            config.publish_trigger_function = true;
            pamguard::detectors::ClickDetectorEngine engine(config);
            (void) engine.process(click_chunk());
            if (engine.noise_samples().size() != 1 ||
                engine.noise_samples()[0].start_sample != 0 ||
                engine.noise_samples()[0].duration_samples != config.max_length ||
                engine.trigger_background().size() != 1 ||
                engine.trigger_background()[0].values.size() != 2 ||
                engine.trigger_function().size() != 1 ||
                engine.trigger_function()[0].signal_excess_db.size() != 2 ||
                engine.trigger_function()[0].signal_excess_db[0].size() != 256) {
                std::cerr << "click noise/background/trigger products do not match settings\n";
                return 1;
            }
        }

        {
            auto config = base_session();
            config.detector.click_grouping_type =
                pamguard::core::DetectorConfig::ClickGroupingType::Singles;
            config.detector.click_channel_groups = {0, 1};
            auto group_0 = config.detector.click;
            group_0.channel_bitmap = 1;
            group_0.trigger_bitmap = 1;
            auto group_1 = config.detector.click;
            group_1.channel_bitmap = 2;
            group_1.trigger_bitmap = 2;
            config.detector.click_groups = {group_0, group_1};
            pamguard::core::AnalysisSession session(config);
            const auto result = session.process(click_chunk());
            if (result.clicks.size() != 2 ||
                result.clicks[0].channel_bitmap != 1 ||
                result.clicks[1].channel_bitmap != 2) {
                std::cerr << "single-channel grouping did not create independent clicks\n";
                return 1;
            }
        }

        {
            auto config = base_session();
            config.detector.click_classifier_type =
                pamguard::core::DetectorConfig::ClickClassifierType::Basic;
            config.detector.click_classify_online = true;
            config.detector.click_basic_classifier_enabled = true;
            pamguard::detectors::BasicClickTypeConfig discard_type;
            discard_type.species_code = 7;
            discard_type.discard = true;
            discard_type.which_selections = 0;
            config.detector.click_basic_classifier.click_types = {discard_type};
            pamguard::core::AnalysisSession session(config);
            if (!session.process(click_chunk()).clicks.empty()) {
                std::cerr << "a classifier type marked discard must remove the click\n";
                return 1;
            }
        }

        {
            auto config = base_session();
            config.detector.click_classifier_type =
                pamguard::core::DetectorConfig::ClickClassifierType::Basic;
            config.detector.click_classify_online = true;
            config.detector.click_discard_unclassified = true;
            config.detector.click_basic_classifier_enabled = true;
            pamguard::core::AnalysisSession session(config);
            if (!session.process(click_chunk()).clicks.empty()) {
                std::cerr << "discardUnclassifiedClicks must remove type-zero clicks\n";
                return 1;
            }
        }

        std::cout << "Click detector settings parity check passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
