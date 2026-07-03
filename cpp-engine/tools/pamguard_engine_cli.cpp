#include <iostream>

#include "pamguard/core/AnalysisConfig.h"
#include "pamguard/core/AnalysisSession.h"
#include "pamguard/dsp/WindowFunction.h"

int main() {
    pamguard::core::AnalysisConfig config;
    config.session_id = "local-cli";
    config.source_id = "synthetic";
    config.sample_rate_hz = 48000;
    config.channel_count = 1;
    config.detector.fft.fft_length = 1024;
    config.detector.fft.fft_hop = 512;
    config.detector.fft.channels = {0};
    config.detector.fft.window_type = pamguard::dsp::WindowType::Hann;

    pamguard::core::AnalysisSession session(config);

    std::cout << "PAMGuard C++ engine scaffold\n";
    std::cout << "Window: " << pamguard::dsp::window_name(config.detector.fft.window_type) << "\n";
    std::cout << "FFT length: " << config.detector.fft.fft_length << "\n";
    std::cout << "FFT hop: " << config.detector.fft.fft_hop << "\n";
    return 0;
}

