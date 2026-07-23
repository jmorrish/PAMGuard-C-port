#include "pamguard/detectors/MatchedTemplateClassifier.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

#include "pamguard/dsp/JtFft.h"

namespace pamguard::detectors {

namespace {

using dsp::JtFft;

// PamArrayUtils.max: NEGATIVE_INFINITY seed, Math.max — NaN propagates.
double pam_max(const std::vector<double>& arr) {
    double max = -std::numeric_limits<double>::infinity();
    for (const double cur : arr) {
        // Java Math.max returns NaN if either argument is NaN.
        max = (std::isnan(max) || std::isnan(cur)) ? std::numeric_limits<double>::quiet_NaN()
                                                   : std::max(max, cur);
    }
    return max;
}

// PamArrayUtils.maxPos: strict > with a NEGATIVE_INFINITY seed (NaN never wins).
int pam_max_pos(const std::vector<double>& arr) {
    double max = -std::numeric_limits<double>::infinity();
    int index = -1;
    int count = 0;
    for (const double cur : arr) {
        if (cur > max) {
            index = count;
            max = cur;
        }
        ++count;
    }
    return index;
}

// Filters.SmoothingFilter.smoothData, verbatim (running sum over the
// pre-divided padded array; even smooth is bumped to odd).
std::vector<double> smooth_data(const std::vector<double>& data, int smooth) {
    if (smooth % 2 == 0) {
        ++smooth;
    }
    const int len = static_cast<int>(data.size());
    const int half_n = (smooth - 1) / 2;
    std::vector<double> padded(static_cast<std::size_t>(len) + 2 * half_n, 0.0);
    for (int i = 0; i < len; ++i) {
        padded[static_cast<std::size_t>(i + half_n)] = data[static_cast<std::size_t>(i)] / smooth;
    }
    std::vector<double> smoothed(static_cast<std::size_t>(len), 0.0);
    for (int i = 0; i <= half_n; ++i) {
        smoothed[0] += padded[static_cast<std::size_t>(i + half_n)];
    }
    for (int i = 1; i < len; ++i) {
        smoothed[static_cast<std::size_t>(i)] = smoothed[static_cast<std::size_t>(i - 1)] +
            padded[static_cast<std::size_t>(i + 2 * half_n)] - padded[static_cast<std::size_t>(i - 1)];
    }
    return smoothed;
}

// FastFFT.nextBinaryExp.
int next_binary_exp(int source) {
    int power = 0;
    for (int i = 0; i < 31; ++i) {
        power = 1 << i;
        if (power >= source) {
            break;
        }
    }
    return power;
}

// signal.Hilbert.getHilbert(signal): envelope of the analytic waveform via
// a power-of-two FFT, the 1-2-...-2-1 half-spectrum multiplier applied to
// the PACKED spectrum, an UNSCALED complex inverse, and mag/fftLength.
std::vector<double> hilbert_envelope(const std::vector<double>& signal) {
    const int data_len = static_cast<int>(signal.size());
    const int fft_length = next_binary_exp(data_len);
    auto packed = JtFft::real_forward(signal, static_cast<std::size_t>(fft_length));

    std::vector<double> full(static_cast<std::size_t>(fft_length) * 2, 0.0);
    for (int j = 0; j < fft_length / 2; ++j) {
        const double factor = (j == 0) ? 1.0 : 2.0;
        full[static_cast<std::size_t>(2 * j)] = packed[static_cast<std::size_t>(2 * j)] * factor;
        full[static_cast<std::size_t>(2 * j) + 1] = packed[static_cast<std::size_t>(2 * j) + 1] * factor;
    }
    JtFft::complex_inverse(full, static_cast<std::size_t>(fft_length), false);

    std::vector<double> envelope(static_cast<std::size_t>(data_len), 0.0);
    for (int i = 0; i < data_len; ++i) {
        const double re = full[static_cast<std::size_t>(2 * i)];
        const double im = full[static_cast<std::size_t>(2 * i) + 1];
        envelope[static_cast<std::size_t>(i)] = std::sqrt(re * re + im * im) / fft_length;
    }
    return envelope;
}

// Complex multiply of packed pairs (a re,im) * (b re,im).
inline void packed_multiply(const std::vector<double>& a, const std::vector<double>& b,
                            std::size_t bins, std::vector<double>& out) {
    for (std::size_t i = 0; i < bins; ++i) {
        const double ar = a[2 * i];
        const double ai = a[2 * i + 1];
        const double br = b[2 * i];
        const double bi = b[2 * i + 1];
        out[2 * i] = ar * br - ai * bi;
        out[2 * i + 1] = ar * bi + ai * br;
    }
}

} // namespace

MatchedTemplateClassifier::MatchedTemplateClassifier(double sample_rate_hz,
                                                     const MatchedTemplateClassifierConfig& config)
    : sample_rate_hz_(sample_rate_hz),
      config_(config),
      template_states_(config.classifiers.size()) {
    for (const auto& pair : config_.classifiers) {
        for (const auto* t : {&pair.match_template, &pair.reject_template}) {
            if (t->sample_rate_hz > sample_rate_hz_) {
                invalid_reason_ = "template '" + t->name + "' is at " +
                    std::to_string(t->sample_rate_hz) + " Hz, above the session rate; the reference " +
                    "decimates via jpamutils WavInterpolator, which is not ported";
            }
            if (t->waveform.empty()) {
                invalid_reason_ = "template '" + t->name + "' has an empty waveform";
            }
        }
    }
}

std::vector<double> MatchedTemplateClassifier::normalise_waveform(const std::vector<double>& waveform,
                                                                  int norm_type) {
    std::vector<double> out = waveform;
    switch (norm_type) {
        case 2: // NORMALIZATION_NONE
            break;
        case 0: { // NORMALIZATION_PEAK: divide by the SIGNED maximum.
            const double max = pam_max(waveform);
            for (auto& value : out) {
                value /= max;
            }
            break;
        }
        case 1: { // NORMALIZATION_RMS: PamArrayUtils.normalise (unit energy).
            double sum = 0.0;
            for (const double value : waveform) {
                sum += value * value;
            }
            sum = std::pow(sum, 0.5);
            for (auto& value : out) {
                value /= sum;
            }
            break;
        }
        default:
            break;
    }
    return out;
}

std::vector<double> MatchedTemplateClassifier::interp_template(const MatchTemplateWaveform& match_template) const {
    if (match_template.sample_rate_hz < sample_rate_hz_) {
        // PamInterp.interpWaveform(waveform, 1/binSize): FFT zero-pad
        // upsampling with ratio-scaled bins and an UNSCALED inverse.
        const double bin_size = match_template.sample_rate_hz / sample_rate_hz_;
        const double ratio = 1.0 / bin_size;
        const std::size_t len = match_template.waveform.size();
        auto packed = JtFft::real_forward(match_template.waveform, len);
        const std::size_t fft_bins = len / 2; // ComplexArray.length()
        const auto interp_len = static_cast<std::size_t>(
            std::floor(static_cast<double>(len) * ratio));
        std::vector<double> interp(interp_len * 2, 0.0);
        for (std::size_t i = 0; i < fft_bins && 2 * i + 1 < interp.size(); ++i) {
            interp[2 * i] = ratio * packed[2 * i];
            interp[2 * i + 1] = ratio * packed[2 * i + 1];
        }
        JtFft::complex_inverse(interp, interp_len, false);
        std::vector<double> out(interp_len, 0.0);
        for (std::size_t i = 0; i < interp_len; ++i) {
            out[i] = interp[2 * i];
        }
        return out;
    }
    // Equal rates: used as-is. (Higher rates are rejected in the ctor.)
    return match_template.waveform;
}

std::vector<double> MatchedTemplateClassifier::template_fft(const MatchTemplateWaveform& match_template,
                                                            int fft_length) const {
    auto interp = interp_template(match_template);
    interp = normalise_waveform(interp, config_.normalisation_type);

    // calcTemplateFFT: a long template is windowed around its peak; the
    // reference's end-EXCLUSIVE subarray leaves it one sample short of the
    // FFT length (zero padded by the transform).
    std::vector<double> to_transform;
    if (static_cast<int>(interp.size()) > fft_length) {
        const int pos = pam_max_pos(interp);
        const int start = std::max(0, pos - fft_length / 2);
        const int end = std::min<int>(static_cast<int>(interp.size()), start + fft_length - 1);
        to_transform.assign(interp.begin() + start, interp.begin() + end);
    }
    else {
        to_transform = std::move(interp);
    }
    auto packed = JtFft::real_forward(to_transform, static_cast<std::size_t>(fft_length));
    // ComplexArray.conj negates every imaginary slot — INCLUDING packed
    // bin 0's, which actually holds the Nyquist real value.
    for (std::size_t i = 1; i < packed.size(); i += 2) {
        packed[i] = -packed[i];
    }
    return packed;
}

MtTemplateResult MatchedTemplateClassifier::correlation_match(std::size_t classifier_index,
                                                              const std::vector<double>& click_fft_packed) {
    const std::size_t click_bins = click_fft_packed.size() / 2;
    // MTClassifier.calcCorrelationMatch: fftLength = click.length()*2.
    const int fft_length = static_cast<int>(click_bins) * 2;

    auto& state = template_states_[classifier_index];
    if (!state.prepared) {
        // The reference caches by sample rate only, so the template FFTs
        // freeze at the FIRST click's FFT length.
        state.match_fft = template_fft(config_.classifiers[classifier_index].match_template, fft_length);
        state.reject_fft = template_fft(config_.classifiers[classifier_index].reject_template, fft_length);
        state.prepared = true;
    }

    const std::size_t match_bins = state.match_fft.size() / 2;
    const std::size_t reject_bins = state.reject_fft.size() / 2;

    std::vector<double> match_result(static_cast<std::size_t>(fft_length) * 2, 0.0);
    packed_multiply(click_fft_packed, state.match_fft, std::min(match_bins, click_bins), match_result);
    std::vector<double> reject_result(static_cast<std::size_t>(fft_length) * 2, 0.0);
    packed_multiply(click_fft_packed, state.reject_fft, std::min(reject_bins, click_bins), reject_result);

    JtFft::complex_inverse(match_result, static_cast<std::size_t>(fft_length), true);
    JtFft::complex_inverse(reject_result, static_cast<std::size_t>(fft_length), true);

    // "need to take the real part of the result and multiply by 2 to get
    // same as ifft function in MATLAB - dunno why this is..."
    std::vector<double> match_real(static_cast<std::size_t>(fft_length), 0.0);
    std::vector<double> reject_real(static_cast<std::size_t>(fft_length), 0.0);
    for (int i = 0; i < fft_length; ++i) {
        match_real[static_cast<std::size_t>(i)] = 2.0 * match_result[static_cast<std::size_t>(2 * i)];
        reject_real[static_cast<std::size_t>(i)] = 2.0 * reject_result[static_cast<std::size_t>(2 * i)];
    }

    const double max_match = pam_max(match_real);
    const double max_reject = pam_max(reject_real);

    MtTemplateResult result;
    result.match_corr = max_match;
    result.reject_corr = max_reject;
    result.threshold = std::isnan(max_reject) ? max_match : max_match - max_reject;
    return result;
}

std::vector<std::vector<int>> MatchedTemplateClassifier::length_data(
    const std::vector<std::vector<double>>& wave_data) const {
    // ClickLength.createLengthData (no FFT filter path): Hilbert envelope,
    // smoothing, then walk out from the peak to the lengthdB threshold.
    const double thresh_ratio = std::pow(10.0, std::abs(config_.length_db) / 20.0);
    std::vector<std::vector<int>> lengths(wave_data.size(), std::vector<int>(2, 0));
    for (std::size_t chan = 0; chan < wave_data.size(); ++chan) {
        auto wave = hilbert_envelope(wave_data[chan]);
        wave = smooth_data(wave, config_.peak_smoothing);
        const int wave_len = static_cast<int>(wave.size());
        double max_val = wave[0];
        int max_index = 0;
        for (int s = 1; s < wave_len; ++s) {
            if (wave[static_cast<std::size_t>(s)] > max_val) {
                max_val = wave[static_cast<std::size_t>(s)];
                max_index = s;
            }
        }
        const double threshold = max_val / thresh_ratio;
        lengths[chan][0] = 0;
        for (int p = max_index - 1; p >= 0; --p) {
            if (wave[static_cast<std::size_t>(p)] < threshold) {
                lengths[chan][0] = p + 1;
                break;
            }
        }
        lengths[chan][1] = wave_len;
        for (int p = max_index + 1; p < wave_len; ++p) {
            if (wave[static_cast<std::size_t>(p)] < threshold) {
                lengths[chan][1] = p - 1;
                break;
            }
        }
    }
    return lengths;
}

const std::vector<double>& MatchedTemplateClassifier::window(int length) {
    if (static_cast<int>(hann_window_.size()) != length) {
        // Spectrogram.WindowFunction.hann: 0.5 - 0.5*cos(2*pi*i/length).
        hann_window_.resize(static_cast<std::size_t>(length));
        for (int i = 0; i < length; ++i) {
            hann_window_[static_cast<std::size_t>(i)] =
                0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * (static_cast<double>(i) / length));
        }
    }
    return hann_window_;
}

MtClassification MatchedTemplateClassifier::classify(const std::vector<std::vector<double>>& wave_data) {
    MtClassification outcome;
    const std::size_t n_classifiers = config_.classifiers.size();
    outcome.best_results.assign(n_classifiers, MtTemplateResult{});
    std::vector<bool> have_best(n_classifiers, false);
    if (wave_data.empty() || !valid()) {
        return outcome;
    }

    std::vector<std::vector<int>> lengths;
    if (config_.peak_search) {
        lengths = length_data(wave_data);
    }

    bool classify = false;
    std::size_t classify_count = 0;

    for (std::size_t chan = 0; chan < wave_data.size(); ++chan) {
        // getWaveData: restricted window around the peak, then normalise.
        std::vector<double> wave;
        if (config_.peak_search) {
            // createRestrictedLenghtWave, verbatim int arithmetic.
            const auto& src = wave_data[chan];
            const int restricted = config_.restricted_bins;
            int start_bin = (lengths[chan][0] + lengths[chan][1] - restricted) / 2;
            start_bin = std::max(0, start_bin);
            int end_bin = std::min<int>(start_bin + restricted, static_cast<int>(src.size()));
            wave.assign(src.begin() + start_bin, src.begin() + end_bin);
            wave.resize(static_cast<std::size_t>(restricted), 0.0);
            const auto& win = window(restricted);
            for (int i = 0; i < restricted; ++i) {
                wave[static_cast<std::size_t>(i)] *= win[static_cast<std::size_t>(i)];
            }
        }
        else {
            wave = wave_data[chan];
        }
        wave = normalise_waveform(wave, config_.normalisation_type);

        const auto click_fft = dsp::JtFft::real_forward(wave, wave.size());

        std::vector<bool> channel_classify(n_classifiers, false);
        for (std::size_t j = 0; j < n_classifiers; ++j) {
            const auto result = correlation_match(j, click_fft);
            if (!have_best[j] || result.threshold > outcome.best_results[j].threshold) {
                outcome.best_results[j] = result;
                have_best[j] = true;
            }
            channel_classify[j] = result.threshold > config_.classifiers[j].threshold_to_accept;
        }

        const bool any = std::find(channel_classify.begin(), channel_classify.end(), true) !=
            channel_classify.end();
        if (config_.channel_classification == 1 /* CHANNELS_REQUIRE_ONE */ && any) {
            classify = true;
            break;
        }
        if (any) {
            ++classify_count;
        }
    }

    if (classify_count == wave_data.size() && config_.channel_classification == 0 /* REQUIRE_ALL */) {
        classify = true;
    }
    outcome.classified = classify;
    return outcome;
}

} // namespace pamguard::detectors
