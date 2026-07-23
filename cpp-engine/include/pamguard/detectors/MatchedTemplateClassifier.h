#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace pamguard::detectors {

/** A matchedTemplateClassifer.MatchTemplate: a waveform at its own rate. */
struct MatchTemplateWaveform {
    std::string name;
    double sample_rate_hz = 0.0;
    std::vector<double> waveform;
};

/** One MTClassifier: a match/reject template pair with a threshold. */
struct MtTemplatePair {
    double threshold_to_accept = 0.01;
    MatchTemplateWaveform match_template;
    MatchTemplateWaveform reject_template;
};

/** MatchedTemplateParams (defaults as the reference constructs them). */
struct MatchedTemplateClassifierConfig {
    bool enabled = false;
    /** 0 peak, 1 RMS (reference default), 2 none. */
    int normalisation_type = 1;
    bool peak_search = true;
    int peak_smoothing = 5;
    double length_db = 6.0;
    int restricted_bins = 2048;
    /** 0 require all channels, 1 require one; 2 (means) never classifies —
     * the reference's aggregation has no branch for it. */
    int channel_classification = 0;
    std::vector<MtTemplatePair> classifiers;
};

/** One MatchedTemplateResult. */
struct MtTemplateResult {
    double threshold = 0.0;
    double match_corr = 0.0;
    double reject_corr = 0.0;
};

/** The per-click outcome: the annotation's best result per classifier. */
struct MtClassification {
    bool classified = false;
    std::vector<MtTemplateResult> best_results;
};

/**
 * Port of PAMGuard's matched-template click classifier
 * (matchedTemplateClassifer.MTProcess + MTClassifier): per channel, the
 * click waveform is optionally windowed around its Hilbert-envelope peak
 * (ClickLength + createRestrictedLenghtWave), normalised, FFT'd, and
 * cross-correlated against match and reject templates; the score is
 * max match correlation minus max reject correlation, classified against a
 * threshold and aggregated across channels.
 *
 * Reference quirks preserved: the template FFT is cached by SAMPLE RATE
 * only, so it freezes at the FIRST click's FFT length and later clicks of
 * other lengths correlate against it over min(template, click) bins; the
 * JTransforms packed bin 0 (DC and Nyquist sharing a complex slot) is
 * multiplied as if it were an ordinary complex bin, and conj() negates the
 * packed Nyquist; a long template is windowed around its peak with an
 * end-exclusive subarray that leaves it one sample short; the ifft result
 * is doubled "to get same as ifft function in MATLAB"; peak normalisation
 * divides by the SIGNED maximum; templates are upsampled by the ported
 * PamInterp.interpWaveform (FFT zero-padding, unscaled inverse).
 *
 * Not supported: template sample rates ABOVE the session rate — the
 * reference decimates via an external library (jpamutils WavInterpolator)
 * that is not ported; valid() reports false and the reason.
 */
class MatchedTemplateClassifier {
public:
    MatchedTemplateClassifier(double sample_rate_hz, const MatchedTemplateClassifierConfig& config);

    [[nodiscard]] bool valid() const noexcept { return invalid_reason_.empty(); }
    [[nodiscard]] const std::string& invalid_reason() const noexcept { return invalid_reason_; }

    /** Classify one click's per-channel waveforms (RawDataHolder.getWaveData). */
    MtClassification classify(const std::vector<std::vector<double>>& wave_data);

    /** MTClassifier.normaliseWaveform. */
    [[nodiscard]] static std::vector<double> normalise_waveform(const std::vector<double>& waveform,
                                                                int norm_type);

private:
    struct TemplateState {
        // Packed conjugated FFTs, frozen at the first click's FFT length.
        std::vector<double> match_fft;
        std::vector<double> reject_fft;
        bool prepared = false;
    };

    double sample_rate_hz_;
    MatchedTemplateClassifierConfig config_;
    std::vector<TemplateState> template_states_;
    std::vector<double> hann_window_;
    std::string invalid_reason_;

    [[nodiscard]] std::vector<double> interp_template(const MatchTemplateWaveform& match_template) const;
    [[nodiscard]] std::vector<double> template_fft(const MatchTemplateWaveform& match_template,
                                                   int fft_length) const;
    MtTemplateResult correlation_match(std::size_t classifier_index,
                                       const std::vector<double>& click_fft_packed);
    [[nodiscard]] std::vector<std::vector<int>> length_data(const std::vector<std::vector<double>>& wave_data) const;
    [[nodiscard]] const std::vector<double>& window(int length);
};

} // namespace pamguard::detectors
