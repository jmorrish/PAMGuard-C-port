#include "pamguard/core/ModuleRuntime.h"
#include "pamguard/core/DetectorNodes.h"
#include "pamguard/core/FftDetectorNodes.h"
#include "pamguard/core/FilterDecimatorSettings.h"
#include "pamguard/core/LocalisationNodes.h"
#include "pamguard/core/MatchedTemplateSettings.h"
#include "pamguard/core/OperatorNodes.h"
#include "pamguard/core/SignalRoutingSettings.h"
#include "pamguard/core/WhistleMoanSettings.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <map>
#include <mutex>
#include <queue>
#include <set>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#include <json.hpp>

namespace pamguard::core {

namespace {

using Json = nlohmann::json;

const ModuleConnection* input_connection(
    const ModuleGraphDocument& document,
    const std::string& module_id,
    const std::string& port_id) {
    const auto found = std::find_if(
        document.connections.begin(),
        document.connections.end(),
        [&](const ModuleConnection& connection) {
            return connection.target.module_id == module_id &&
                   connection.target.port_id == port_id;
        });
    return found == document.connections.end() ? nullptr : &*found;
}

std::vector<const ModuleConnection*> input_connections(
    const ModuleGraphDocument& document,
    const std::string& module_id,
    const std::string& port_id) {
    std::vector<const ModuleConnection*> result;
    for (const auto& connection : document.connections) {
        if (connection.target.module_id == module_id &&
            connection.target.port_id == port_id) {
            result.push_back(&connection);
        }
    }
    return result;
}

std::vector<const ModuleInstance*> topological_modules(
    const ModuleGraphDocument& document) {
    std::unordered_map<std::string, const ModuleInstance*> modules;
    std::unordered_map<std::string, std::size_t> indegree;
    std::unordered_map<std::string, std::vector<std::string>> adjacency;
    for (const auto& module : document.modules) {
        modules.emplace(module.id, &module);
        indegree.emplace(module.id, 0);
    }
    for (const auto& connection : document.connections) {
        adjacency[connection.source.module_id].push_back(connection.target.module_id);
        ++indegree[connection.target.module_id];
    }
    std::queue<std::string> ready;
    for (const auto& module : document.modules) {
        if (indegree[module.id] == 0) {
            ready.push(module.id);
        }
    }
    std::vector<const ModuleInstance*> result;
    while (!ready.empty()) {
        const auto id = ready.front();
        ready.pop();
        result.push_back(modules.at(id));
        for (const auto& target : adjacency[id]) {
            if (--indegree[target] == 0) {
                ready.push(target);
            }
        }
    }
    if (result.size() != document.modules.size()) {
        throw std::invalid_argument("Cannot build a cyclic module graph");
    }
    return result;
}

std::shared_ptr<DataBlock> required_input(
    const ModuleGraphDocument& document,
    const ModuleInstance& module,
    const std::string& port,
    const std::unordered_map<DataBlockId, std::shared_ptr<DataBlock>>& blocks) {
    const auto* connection = input_connection(document, module.id, port);
    if (connection == nullptr) {
        throw std::invalid_argument(
            "Module " + module.id + " has no connection for input " + port);
    }
    const auto id = ModuleRuntime::block_id(
        connection->source.module_id,
        connection->source.port_id);
    const auto found = blocks.find(id);
    if (found == blocks.end()) {
        throw std::invalid_argument(
            "Module " + module.id + " references an unavailable source block " + id);
    }
    return found->second;
}

std::shared_ptr<DataBlock> optional_input(
    const ModuleGraphDocument& document,
    const ModuleInstance& module,
    const std::string& port,
    const std::unordered_map<DataBlockId, std::shared_ptr<DataBlock>>& blocks) {
    const auto* connection =
        input_connection(document, module.id, port);
    if (connection == nullptr) {
        return {};
    }
    const auto id = ModuleRuntime::block_id(
        connection->source.module_id,
        connection->source.port_id);
    const auto found = blocks.find(id);
    if (found == blocks.end()) {
        throw std::invalid_argument(
            "Module " + module.id +
            " references an unavailable optional source block " + id);
    }
    return found->second;
}

std::uint32_t contiguous_channel_bitmap(std::size_t channel_count) {
    if (channel_count >= 32) {
        return 0xFFFFFFFFu;
    }
    return channel_count == 0
        ? 0
        : (std::uint32_t{1} << channel_count) - 1;
}

std::size_t channel_count_from_bitmap(std::uint32_t bitmap) {
    std::size_t highest = 0;
    for (std::size_t channel = 0; channel < 32; ++channel) {
        if ((bitmap & (std::uint32_t{1} << channel)) != 0) {
            highest = channel + 1;
        }
    }
    return highest;
}

std::uint32_t selected_channel_bitmap(
    const std::vector<std::size_t>& channels,
    std::uint32_t available,
    const std::string& module_id) {
    if (channels.empty()) {
        throw std::invalid_argument(
            "Module " + module_id +
            " must select at least one channel");
    }
    std::uint32_t selected = 0;
    for (const auto channel : channels) {
        if (channel >= 32 ||
            (available & (std::uint32_t{1} << channel)) == 0) {
            throw std::invalid_argument(
                "Module " + module_id +
                " selects a channel unavailable from its input block");
        }
        selected |= std::uint32_t{1} << channel;
    }
    return selected;
}

std::uint32_t intersect_selected_channels(
    std::uint32_t requested,
    std::uint32_t available,
    const std::string& module_id) {
    const auto selected = requested & available;
    if (selected == 0) {
        throw std::invalid_argument(
            "Module " + module_id +
            " has no selected channels available from its input block");
    }
    return selected;
}

DataBlockDescriptor output_descriptor(
    const ModuleInstance& module,
    std::string port,
    std::string name,
    std::string data_type,
    double sample_rate,
    std::uint32_t channel_bitmap,
    std::vector<std::string> capabilities,
    std::string clock_domain_id = {}) {
    std::size_t history_capacity = 256;
    if (data_type == kRawAudioDataType) {
        history_capacity = 16;
    }
    else if (data_type == kFftDataType) {
        history_capacity = 512;
    }
    else if (std::find(
                 capabilities.begin(),
                 capabilities.end(),
                 "detections") != capabilities.end()) {
        history_capacity = 2048;
    }
    DataBlockDescriptor descriptor{
        ModuleRuntime::block_id(module.id, port),
        module.name + " / " + name,
        module.id,
        std::move(port),
        std::move(data_type),
        1,
        sample_rate,
        channel_bitmap,
        0,
        {},
        {},
        {},
        std::move(capabilities),
        history_capacity,
    };
    descriptor.clock_domain_id = std::move(clock_domain_id);
    descriptor.retention_policy =
        history_capacity == 0
        ? "none"
        : "bounded-history";
    return descriptor;
}

dsp::IirFilterType filter_type(const std::string& value) {
    if (value == "none") return dsp::IirFilterType::None;
    if (value == "butterworth") return dsp::IirFilterType::Butterworth;
    if (value == "chebyshev") return dsp::IirFilterType::Chebyshev;
    if (value == "firWindow") return dsp::IirFilterType::FirWindow;
    if (value == "firArbitrary") return dsp::IirFilterType::FirArbitrary;
    if (value == "fft") return dsp::IirFilterType::Fft;
    throw std::invalid_argument("Unknown filter type: " + value);
}

dsp::IirFilterBand filter_band(const std::string& value) {
    if (value == "lowPass") return dsp::IirFilterBand::LowPass;
    if (value == "highPass") return dsp::IirFilterBand::HighPass;
    if (value == "bandPass") return dsp::IirFilterBand::BandPass;
    if (value == "bandStop") return dsp::IirFilterBand::BandStop;
    throw std::invalid_argument("Unknown filter band: " + value);
}

dsp::IirFilterParams filter_params_from_json(
    const Json& settings,
    dsp::IirFilterParams defaults = {}) {
    if (!settings.is_object()) {
        throw std::invalid_argument("Filter settings must be an object");
    }
    if (settings.contains("type")) {
        defaults.type = filter_type(settings.at("type").get<std::string>());
    }
    if (settings.contains("band")) {
        defaults.band = filter_band(settings.at("band").get<std::string>());
    }
    defaults.order = settings.value("order", defaults.order);
    defaults.low_pass_freq_hz =
        settings.value("lowPassFreqHz", defaults.low_pass_freq_hz);
    defaults.high_pass_freq_hz =
        settings.value("highPassFreqHz", defaults.high_pass_freq_hz);
    defaults.pass_band_ripple_db =
        settings.value("passBandRippleDb", defaults.pass_band_ripple_db);
    defaults.stop_band_ripple_db =
        settings.value("stopBandRippleDb", defaults.stop_band_ripple_db);
    defaults.cheby_gamma =
        settings.value("chebyGamma", defaults.cheby_gamma);
    if (settings.contains("arbitraryFrequenciesHz")) {
        defaults.arbitrary_frequencies_hz =
            settings.at("arbitraryFrequenciesHz")
                .get<std::vector<double>>();
    }
    if (settings.contains("arbitraryGainsDb")) {
        defaults.arbitrary_gains_db =
            settings.at("arbitraryGainsDb")
                .get<std::vector<double>>();
    }
    dsp::validate_filter_params(defaults);
    return defaults;
}

detectors::FrequencyRange frequency_range_from_json(const Json& value) {
    if (!value.is_array() || value.size() != 2) {
        throw std::invalid_argument(
            "Frequency range must be a two-number array");
    }
    return {
        value.at(0).get<double>(),
        value.at(1).get<double>(),
    };
}

localisation::DelayMeasurementConfig delay_measurement_from_json(
    const Json& delay,
    localisation::DelayMeasurementConfig defaults = {}) {
    defaults.filter_bearings =
        delay.value("filterBearings", defaults.filter_bearings);
    const auto band = delay.value(
        "filterBand",
        std::string("highPass"));
    if (band == "highPass") {
        defaults.filter_band =
            localisation::DelayFilterBand::HighPass;
    }
    else if (band == "lowPass") {
        defaults.filter_band =
            localisation::DelayFilterBand::LowPass;
    }
    else if (band == "bandPass") {
        defaults.filter_band =
            localisation::DelayFilterBand::BandPass;
    }
    else if (band == "bandStop") {
        defaults.filter_band =
            localisation::DelayFilterBand::BandStop;
    }
    else {
        throw std::invalid_argument(
            "Unknown delay filter band: " + band);
    }
    defaults.filter_high_pass_hz =
        delay.value(
            "filterHighPassHz",
            defaults.filter_high_pass_hz);
    defaults.filter_low_pass_hz =
        delay.value(
            "filterLowPassHz",
            defaults.filter_low_pass_hz);
    defaults.envelope_bearings =
        delay.value(
            "envelopeBearings",
            defaults.envelope_bearings);
    defaults.use_leading_edge =
        delay.value(
            "useLeadingEdge",
            defaults.use_leading_edge);
    defaults.up_sample =
        delay.value("upSample", defaults.up_sample);
    defaults.use_restricted_bins =
        delay.value(
            "useRestrictedBins",
            defaults.use_restricted_bins);
    defaults.restricted_bins =
        delay.value(
            "restrictedBins",
            defaults.restricted_bins);
    return defaults;
}

std::string normalized_token(std::string value) {
    value.erase(
        std::remove_if(
            value.begin(),
            value.end(),
            [](unsigned char character) {
                return !std::isalnum(character);
            }),
        value.end());
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    return value;
}

dsp::WindowType window_type_from_json(const Json& value) {
    if (value.is_number_integer()) {
        const auto type = value.get<int>();
        if (type >= 0 && type <= 5) {
            return static_cast<dsp::WindowType>(type);
        }
    }
    if (value.is_string()) {
        const auto type = normalized_token(value.get<std::string>());
        if (type == "rectangular" || type == "rectangle" || type == "none") {
            return dsp::WindowType::Rectangular;
        }
        if (type == "hamming") {
            return dsp::WindowType::Hamming;
        }
        if (type == "hann" || type == "hanning") {
            return dsp::WindowType::Hann;
        }
        if (type == "bartlett" || type == "triangular" ||
            type == "bartletttriangular") {
            return dsp::WindowType::Bartlett;
        }
        if (type == "blackman") {
            return dsp::WindowType::Blackman;
        }
        if (type == "blackmanharris") {
            return dsp::WindowType::BlackmanHarris;
        }
    }
    throw std::invalid_argument(
        "FFT windowType must be Rectangular, Hamming, Hann, Bartlett, "
        "Blackman, Blackman-Harris, or the corresponding integer 0..5");
}

detectors::SpectrogramNoiseConfig spectrogram_noise_from_json(
    const Json& settings) {
    detectors::SpectrogramNoiseConfig config;
    config.run_median_filter =
        settings.value("medianFilter", config.run_median_filter);
    config.median_filter_length = settings.value(
        "medianFilterLength",
        config.median_filter_length);
    config.run_average_subtraction = settings.value(
        "averageSubtraction",
        config.run_average_subtraction);
    config.average_update_constant = settings.value(
        "updateConstant",
        config.average_update_constant);
    config.run_kernel_smoothing = settings.value(
        "kernelSmoothing",
        config.run_kernel_smoothing);
    config.run_threshold =
        settings.value("threshold", config.run_threshold);
    config.threshold_db =
        settings.value("thresholdDb", config.threshold_db);
    config.threshold_final_output = settings.value(
        "finalOutput",
        config.threshold_final_output);
    if (config.median_filter_length < 1) {
        throw std::invalid_argument(
            "spectrogram noise medianFilterLength must be positive");
    }
    if (config.average_update_constant <= 0.0 ||
        config.average_update_constant >= 1.0) {
        throw std::invalid_argument(
            "spectrogram noise updateConstant must be between 0 and 1");
    }
    if (config.threshold_final_output <
            detectors::SpectrogramNoiseConfig::kOutputBinary ||
        config.threshold_final_output >
            detectors::SpectrogramNoiseConfig::kOutputRaw) {
        throw std::invalid_argument(
            "spectrogram noise finalOutput must be 0, 1, or 2");
    }
    return config;
}

detectors::BasicClickStandardType basic_standard_type(
    const std::string& value) {
    const auto token = normalized_token(value);
    if (token == "beakedwhale") {
        return detectors::BasicClickStandardType::BeakedWhale;
    }
    if (token == "porpoise") {
        return detectors::BasicClickStandardType::Porpoise;
    }
    throw std::invalid_argument(
        "Unknown standard click classifier type: " + value);
}

detectors::BasicClickTypeConfig basic_click_type_from_json(
    const Json& value) {
    if (value.is_string() ||
        (value.is_object() && value.contains("standard"))) {
        const auto standard = basic_standard_type(
            value.is_string()
                ? value.get<std::string>()
                : value.at("standard").get<std::string>());
        const int default_code =
            standard == detectors::BasicClickStandardType::BeakedWhale
            ? 1
            : 2;
        auto type = detectors::standard_basic_click_type(
            value.is_object()
                ? value.value("speciesCode", default_code)
                : default_code,
            standard);
        if (value.is_object()) {
            type.name = value.value("name", type.name);
            // The Java BasicClickIdentifier stores this inherited flag but
            // does not consult it during identify(); preserve that quirk.
            type.enabled = value.value("enabled", type.enabled);
            type.discard = value.value("discard", type.discard);
            type.which_selections =
                value.value("whichSelections", type.which_selections);
        }
        return type;
    }
    if (!value.is_object()) {
        throw std::invalid_argument(
            "Basic click classifier type must be an object or standard name");
    }
    detectors::BasicClickTypeConfig type;
    type.name = value.value("name", type.name);
    type.species_code = value.value("speciesCode", type.species_code);
    // Deliberately persisted but not applied by BasicClickClassifier.
    type.enabled = value.value("enabled", type.enabled);
    type.discard = value.value("discard", type.discard);
    type.which_selections =
        value.value("whichSelections", type.which_selections);
    if (value.contains("band1FreqHz")) {
        type.band1_freq_hz = frequency_range_from_json(
            value.at("band1FreqHz"));
    }
    if (value.contains("band2FreqHz")) {
        type.band2_freq_hz = frequency_range_from_json(
            value.at("band2FreqHz"));
    }
    if (value.contains("band1EnergyDb")) {
        type.band1_energy_db = frequency_range_from_json(
            value.at("band1EnergyDb"));
    }
    if (value.contains("band2EnergyDb")) {
        type.band2_energy_db = frequency_range_from_json(
            value.at("band2EnergyDb"));
    }
    type.band_energy_difference_db = value.value(
        "bandEnergyDifferenceDb",
        type.band_energy_difference_db);
    if (value.contains("peakFrequencySearchHz")) {
        type.peak_frequency_search_hz = frequency_range_from_json(
            value.at("peakFrequencySearchHz"));
    }
    if (value.contains("peakFrequencyRangeHz")) {
        type.peak_frequency_range_hz = frequency_range_from_json(
            value.at("peakFrequencyRangeHz"));
    }
    if (value.contains("peakWidthHz")) {
        type.peak_width_hz = frequency_range_from_json(
            value.at("peakWidthHz"));
    }
    type.width_energy_fraction = value.value(
        "widthEnergyFraction",
        type.width_energy_fraction);
    if (value.contains("meanSumRangeHz")) {
        type.mean_sum_range_hz = frequency_range_from_json(
            value.at("meanSumRangeHz"));
    }
    if (value.contains("meanSelectionRangeHz")) {
        type.mean_selection_range_hz = frequency_range_from_json(
            value.at("meanSelectionRangeHz"));
    }
    if (value.contains("clickLengthMs")) {
        type.click_length_ms = frequency_range_from_json(
            value.at("clickLengthMs"));
    }
    type.length_energy_fraction = value.value(
        "lengthEnergyFraction",
        type.length_energy_fraction);
    return type;
}

detectors::SweepRange sweep_range_from_json(const Json& value) {
    const auto range = frequency_range_from_json(value);
    return {range.low_hz, range.high_hz};
}

detectors::SweepChannelChoice sweep_channel_choice_from_json(
    const Json& value) {
    if (value.is_number_integer()) {
        const auto choice = value.get<int>();
        if (choice >= 0 && choice <= 2) {
            return static_cast<detectors::SweepChannelChoice>(choice);
        }
    }
    if (value.is_string()) {
        const auto token = normalized_token(value.get<std::string>());
        if (token == "requireall" || token == "all") {
            return detectors::SweepChannelChoice::RequireAll;
        }
        if (token == "requireone" || token == "one") {
            return detectors::SweepChannelChoice::RequireOne;
        }
        if (token == "usemeans" || token == "means" || token == "mean") {
            return detectors::SweepChannelChoice::UseMeans;
        }
    }
    throw std::invalid_argument(
        "Sweep channelChoice must be requireAll, requireOne, useMeans, or 0..2");
}

detectors::SweepFftFilterBand sweep_filter_band_from_json(
    const Json& value) {
    const auto token = normalized_token(value.get<std::string>());
    if (token == "highpass") {
        return detectors::SweepFftFilterBand::HighPass;
    }
    if (token == "lowpass") {
        return detectors::SweepFftFilterBand::LowPass;
    }
    if (token == "bandpass") {
        return detectors::SweepFftFilterBand::BandPass;
    }
    if (token == "bandstop") {
        return detectors::SweepFftFilterBand::BandStop;
    }
    throw std::invalid_argument(
        "Sweep fftFilter.band must be highPass, lowPass, bandPass, or bandStop");
}

detectors::SweepClickTypeConfig sweep_click_type_from_json(
    const Json& value) {
    if (value.is_string() ||
        (value.is_object() && value.contains("standard"))) {
        const auto standard = basic_standard_type(
            value.is_string()
                ? value.get<std::string>()
                : value.at("standard").get<std::string>());
        const int default_code =
            standard == detectors::BasicClickStandardType::BeakedWhale
            ? 1
            : 2;
        auto type = detectors::standard_sweep_click_type(
            value.is_object()
                ? value.value("speciesCode", default_code)
                : default_code,
            standard);
        if (value.is_object()) {
            type.name = value.value("name", type.name);
            type.discard = value.value("discard", type.discard);
            type.enabled = value.value("enabled", type.enabled);
        }
        return type;
    }
    if (!value.is_object()) {
        throw std::invalid_argument(
            "Sweep click classifier type must be an object or standard name");
    }
    detectors::SweepClickTypeConfig type;
    type.name = value.value("name", type.name);
    type.species_code = value.value("speciesCode", type.species_code);
    type.discard = value.value("discard", type.discard);
    type.enabled = value.value("enabled", type.enabled);
    if (value.contains("channelChoice")) {
        type.channel_choice = sweep_channel_choice_from_json(
            value.at("channelChoice"));
    }
    type.restrict_length = value.value(
        "restrictLength",
        type.restrict_length);
    type.restricted_bins = value.value(
        "restrictedBins",
        type.restricted_bins);
    if (value.contains("restrictedBinType")) {
        const auto& raw = value.at("restrictedBinType");
        type.restricted_bin_type =
            (raw.is_number_integer() && raw.get<int>() == 1) ||
                (raw.is_string() &&
                 (normalized_token(raw.get<std::string>()) == "clickstart" ||
                  normalized_token(raw.get<std::string>()) == "start"))
            ? detectors::SweepRestrictedBinType::ClickStart
            : detectors::SweepRestrictedBinType::ClickCenter;
    }
    type.enable_length = value.value("enableLength", type.enable_length);
    type.length_smoothing =
        value.value("lengthSmoothing", type.length_smoothing);
    type.length_db = value.value("lengthDb", type.length_db);
    if (value.contains("lengthMs")) {
        type.length_ms = sweep_range_from_json(value.at("lengthMs"));
    }
    type.enable_energy_bands =
        value.value("enableEnergyBands", type.enable_energy_bands);
    if (value.contains("testEnergyBandHz")) {
        type.test_energy_band_hz =
            sweep_range_from_json(value.at("testEnergyBandHz"));
    }
    if (value.contains("controlEnergyBand0Hz")) {
        type.control_energy_band_0_hz =
            sweep_range_from_json(value.at("controlEnergyBand0Hz"));
    }
    if (value.contains("controlEnergyBand1Hz")) {
        type.control_energy_band_1_hz =
            sweep_range_from_json(value.at("controlEnergyBand1Hz"));
    }
    type.energy_threshold_0_db = value.value(
        "energyThreshold0Db",
        type.energy_threshold_0_db);
    type.energy_threshold_1_db = value.value(
        "energyThreshold1Db",
        type.energy_threshold_1_db);
    type.test_amplitude =
        value.value("testAmplitude", type.test_amplitude);
    if (value.contains("amplitudeRangeDb")) {
        type.amplitude_range_db =
            sweep_range_from_json(value.at("amplitudeRangeDb"));
    }
    type.enable_fft_filter =
        value.value("enableFftFilter", type.enable_fft_filter);
    if (value.contains("fftFilter")) {
        const auto& filter = value.at("fftFilter");
        if (filter.contains("band")) {
            type.fft_filter.band =
                sweep_filter_band_from_json(filter.at("band"));
        }
        type.fft_filter.low_pass_freq_hz = filter.value(
            "lowPassFreqHz",
            type.fft_filter.low_pass_freq_hz);
        type.fft_filter.high_pass_freq_hz = filter.value(
            "highPassFreqHz",
            type.fft_filter.high_pass_freq_hz);
    }
    type.enable_peak = value.value("enablePeak", type.enable_peak);
    type.enable_width = value.value("enableWidth", type.enable_width);
    type.enable_mean = value.value("enableMean", type.enable_mean);
    if (value.contains("peakSearchRangeHz")) {
        type.peak_search_range_hz =
            sweep_range_from_json(value.at("peakSearchRangeHz"));
    }
    if (value.contains("peakRangeHz")) {
        type.peak_range_hz =
            sweep_range_from_json(value.at("peakRangeHz"));
    }
    if (value.contains("peakWidthRangeHz")) {
        type.peak_width_range_hz =
            sweep_range_from_json(value.at("peakWidthRangeHz"));
    }
    if (value.contains("meanRangeHz")) {
        type.mean_range_hz =
            sweep_range_from_json(value.at("meanRangeHz"));
    }
    type.peak_smoothing =
        value.value("peakSmoothing", type.peak_smoothing);
    type.peak_width_threshold_db = value.value(
        "peakWidthThresholdDb",
        type.peak_width_threshold_db);
    type.enable_zero_crossings =
        value.value("enableZeroCrossings", type.enable_zero_crossings);
    if (value.contains("zeroCrossingCount")) {
        type.zero_crossing_count =
            sweep_range_from_json(value.at("zeroCrossingCount"));
    }
    type.enable_sweep = value.value("enableSweep", type.enable_sweep);
    if (value.contains("zeroCrossingSweepKhzPerMs")) {
        type.zero_crossing_sweep_khz_per_ms =
            sweep_range_from_json(value.at("zeroCrossingSweepKhzPerMs"));
    }
    type.enable_min_cross_correlation = value.value(
        "enableMinCrossCorrelation",
        type.enable_min_cross_correlation);
    type.enable_peak_cross_correlation = value.value(
        "enablePeakCrossCorrelation",
        type.enable_peak_cross_correlation);
    type.min_correlation =
        value.value("minCorrelation", type.min_correlation);
    type.correlation_factor =
        value.value("correlationFactor", type.correlation_factor);
    type.enable_bearing_limits = value.value(
        "enableBearingLimits",
        type.enable_bearing_limits);
    type.exclude_bearing_limits = value.value(
        "excludeBearingLimits",
        type.exclude_bearing_limits);
    if (value.contains("bearingLimitsRadians")) {
        type.bearing_limits_radians =
            sweep_range_from_json(value.at("bearingLimitsRadians"));
    }
    return type;
}

detectors::NoiseBandType noise_band_type_from_json(
    const std::string& value) {
    if (value == "octave") {
        return detectors::NoiseBandType::Octave;
    }
    if (value == "thirdOctave") {
        return detectors::NoiseBandType::ThirdOctave;
    }
    if (value == "decidecade") {
        return detectors::NoiseBandType::Decidecade;
    }
    if (value == "decade") {
        return detectors::NoiseBandType::Decade;
    }
    if (value == "tenthOctave") {
        return detectors::NoiseBandType::TenthOctave;
    }
    if (value == "twelfthOctave") {
        return detectors::NoiseBandType::TwelfthOctave;
    }
    throw std::invalid_argument("Unknown noise band type: " + value);
}

} // namespace

ModuleRuntime::~ModuleRuntime() {
    try {
        stop();
    }
    catch (...) {
        // Destructors cannot surface lifecycle errors. Explicit stop() still
        // reports them to callers.
    }
}

DataBlockId ModuleRuntime::block_id(
    const ModuleInstanceId& module_id,
    const PortId& port_id) {
    return "block:" + module_id + ":" + port_id;
}

ModuleRuntime::PreparedRuntime ModuleRuntime::build(
    const ModuleGraphDocument& document) {
    PreparedRuntime prepared;
    prepared.revision = document.revision;
    for (const auto* module : topological_modules(document)) {
        if (!module->enabled) {
            continue;
        }
        const auto settings = Json::parse(module->settings_json);
        if (!settings.is_object()) {
            throw std::invalid_argument("Module settings must be a JSON object");
        }

        if (module->type_id == "pamguard.acquisition") {
            const auto sample_rate = settings.at("sampleRateHz").get<double>();
            const auto channel_count = settings.at("channelCount").get<std::size_t>();
            if (!std::isfinite(sample_rate) ||
                sample_rate <= 0.0 || channel_count == 0 ||
                channel_count > 32) {
                throw std::invalid_argument(
                    "Acquisition sampleRateHz must be finite and positive and "
                    "channelCount must be in 1..32");
            }
            if (!settings.contains("subtractDC") ||
                !settings.at("subtractDC").is_boolean()) {
                throw std::invalid_argument(
                    "Acquisition subtractDC must be a boolean");
            }
            if (!settings.contains("dcTimeConstantSeconds") ||
                !settings.at("dcTimeConstantSeconds").is_number()) {
                throw std::invalid_argument(
                    "Acquisition dcTimeConstantSeconds must be a number");
            }
            AudioSourceNodeConfig source_config;
            source_config.subtract_dc =
                settings.at("subtractDC").get<bool>();
            source_config.dc_time_constant_seconds =
                settings.at("dcTimeConstantSeconds").get<double>();
            if (!std::isfinite(
                    source_config.dc_time_constant_seconds) ||
                source_config.dc_time_constant_seconds <= 0.0) {
                throw std::invalid_argument(
                    "Acquisition dcTimeConstantSeconds must be finite and "
                    "positive");
            }
            const auto calibration = settings.value(
                "calibrationDbOffsetByChannel",
                std::vector<double>{});
            if ((!calibration.empty() &&
                 calibration.size() < channel_count) ||
                std::any_of(
                    calibration.begin(),
                    calibration.end(),
                    [](double value) { return !std::isfinite(value); })) {
                throw std::invalid_argument(
                    "Acquisition calibration must be empty or define a "
                    "finite dB offset for every input channel");
            }
            std::optional<double> volts_peak_to_peak;
            if (settings.contains("voltsPeak2Peak")) {
                if (!settings.at("voltsPeak2Peak").is_number()) {
                    throw std::invalid_argument(
                        "Acquisition voltsPeak2Peak must be a number");
                }
                const auto value =
                    settings.at("voltsPeak2Peak").get<double>();
                if (!std::isfinite(value) || value <= 0.0) {
                    throw std::invalid_argument(
                        "Acquisition voltsPeak2Peak must be finite and "
                        "positive");
                }
                volts_peak_to_peak = value;
            }
            auto descriptor = output_descriptor(
                *module,
                "audio",
                "Raw audio",
                kRawAudioDataType,
                sample_rate,
                contiguous_channel_bitmap(channel_count),
                {"sampled", "realtime"},
                module->id);
            descriptor.calibration_db_offset_by_channel =
                calibration;
            descriptor.volts_peak_to_peak = volts_peak_to_peak;
            auto output =
                std::make_shared<DataBlock>(std::move(descriptor));
            auto node = std::make_unique<AudioSourceNode>(
                module->id,
                source_config,
                output);
            prepared.sources.emplace(module->id, node.get());
            prepared.blocks.emplace(output->descriptor().id, output);
            prepared.nodes.push_back(std::move(node));
            continue;
        }

        if (module->type_id == "pamguard.effort-monitor" ||
            module->type_id == "pamguard.aural-listening" ||
            module->type_id == "pamguard.user-input") {
            const auto default_category = settings.value(
                "defaultCategory",
                module->type_id == "pamguard.effort-monitor"
                    ? "effort"
                    : module->type_id == "pamguard.aural-listening"
                        ? "listening"
                        : "annotation");
            auto output = std::make_shared<DataBlock>(
                output_descriptor(
                    *module,
                    "events",
                    "Operator events",
                    kOperatorEventDataType,
                    0.0,
                    0,
                    {"events", "annotations"}));
            auto node = std::make_unique<OperatorInputNode>(
                module->id,
                default_category,
                output);
            prepared.operator_inputs.emplace(
                module->id,
                node.get());
            prepared.blocks.emplace(
                output->descriptor().id,
                output);
            prepared.nodes.push_back(std::move(node));
            continue;
        }

        if (module->type_id == "pamguard.storage-health") {
            auto output = std::make_shared<DataBlock>(
                output_descriptor(
                    *module,
                    "status",
                    "Storage status",
                    kStorageHealthDataType,
                    0.0,
                    0,
                    {"status", "monitoring"}));
            prepared.nodes.push_back(
                std::make_unique<StorageHealthNode>(
                    module->id,
                    settings.at("path").get<std::string>(),
                    settings.value("warningFreePercent", 10.0),
                    settings.value("intervalSeconds", 30.0),
                    output));
            prepared.blocks.emplace(
                output->descriptor().id,
                output);
            continue;
        }

        if (module->type_id == "pamguard.clip-generator") {
            auto audio = required_input(
                document,
                *module,
                "audio",
                prepared.blocks);
            /*
             * Compatibility for direct low-level graph documents created
             * before the controlled-unit projection began supplying
             * receiver-owned triggerPolicies. Portable projects always take
             * the strict branch below.
             */
            if (!settings.contains(
                    "requiredHistorySeconds")) {
                auto triggers = required_input(
                    document,
                    *module,
                    "triggers",
                    prepared.blocks);
                ClipGeneratorNodeConfig legacy_config;
                legacy_config.pre_trigger_seconds =
                    settings.value(
                        "preTriggerSeconds",
                        0.25);
                legacy_config.post_trigger_seconds =
                    settings.value(
                        "postTriggerSeconds",
                        0.5);
                legacy_config.maximum_buffer_seconds =
                    settings.value(
                        "maximumBufferSeconds",
                        30.0);
                auto output =
                    std::make_shared<DataBlock>(
                        output_descriptor(
                            *module,
                            "clips",
                            "Detection-triggered clips",
                            kAudioClipDataType,
                            audio->descriptor()
                                .sample_rate_hz,
                            audio->descriptor()
                                .channel_bitmap,
                            {
                                "events",
                                "waveform",
                                "playable",
                            }));
                prepared.nodes.push_back(
                    std::make_unique<
                        ClipGeneratorNode>(
                        module->id,
                        std::move(legacy_config),
                        audio,
                        triggers,
                        output));
                prepared.blocks.emplace(
                    output->descriptor().id,
                    output);
                continue;
            }
            ClipGeneratorNodeConfig config;
            config.maximum_buffer_seconds =
                std::max(
                    30.0,
                    settings.at(
                        "requiredHistorySeconds")
                        .get<double>());
            const auto storage_mode =
                settings.at("storageMode")
                    .get<std::string>();
            if (storage_mode == "binary") {
                config.storage_mode =
                    ClipGeneratorStorageMode::Binary;
            }
            else if (storage_mode == "wav-files") {
                config.storage_mode =
                    ClipGeneratorStorageMode::WavFiles;
            }
            else if (storage_mode == "both") {
                config.storage_mode =
                    ClipGeneratorStorageMode::Both;
            }
            else {
                throw std::invalid_argument(
                    "Clip Generator storageMode is invalid");
            }

            const auto trigger_connections =
                input_connections(
                    document,
                    module->id,
                    "triggers");
            std::map<
                std::string,
                const ModuleConnection*>
                connections_by_block;
            for (const auto* connection :
                 trigger_connections) {
                const auto runtime_block_id =
                    ModuleRuntime::block_id(
                        connection->source.module_id,
                        connection->source.port_id);
                if (!connections_by_block.emplace(
                         runtime_block_id,
                         connection).second) {
                    throw std::invalid_argument(
                        "Clip Generator trigger inputs must map "
                        "one-to-one to runtime blocks");
                }
            }

            const auto& runtime_policies =
                settings.at("triggerPolicies");
            if (!runtime_policies.is_array()) {
                throw std::invalid_argument(
                    "Clip Generator triggerPolicies must be an array");
            }
            std::vector<ClipGeneratorTriggerInput>
                trigger_inputs;
            trigger_inputs.reserve(
                runtime_policies.size());
            std::set<std::string> policy_blocks;
            for (const auto& policy :
                 runtime_policies) {
                if (!policy.is_object() ||
                    !policy.at("triggerSource")
                         .is_object()) {
                    throw std::invalid_argument(
                        "Clip Generator runtime trigger policy "
                        "is invalid");
                }
                const auto runtime_block_id =
                    policy.at("runtimeBlockId")
                        .get<std::string>();
                const auto connection =
                    connections_by_block.find(
                        runtime_block_id);
                const auto block =
                    prepared.blocks.find(
                        runtime_block_id);
                if (connection ==
                        connections_by_block.end() ||
                    block == prepared.blocks.end() ||
                    !policy_blocks.emplace(
                         runtime_block_id).second) {
                    throw std::invalid_argument(
                        "Clip Generator runtimeBlockId must map "
                        "exactly once to a bound trigger source");
                }

                ClipGeneratorTriggerInput trigger;
                trigger.input = block->second;
                trigger.runtime_block_id =
                    runtime_block_id;
                trigger.source_unit_id =
                    policy.at("triggerSource")
                        .at("unitId")
                        .get<std::string>();
                trigger.source_output_role =
                    policy.at("triggerSource")
                        .at("outputRole")
                        .get<std::string>();
                trigger.source_data_type =
                    policy.at("sourceDataType")
                        .get<std::string>();
                trigger.enabled =
                    policy.at("enabled")
                        .get<bool>();
                trigger.pre_trigger_seconds =
                    policy.at("secondsBeforeTrigger")
                        .get<double>();
                trigger.post_trigger_seconds =
                    policy.at("secondsAfterTrigger")
                        .get<double>();
                const auto channel_selection =
                    policy.at("channelSelection")
                        .get<std::string>();
                if (channel_selection ==
                    "detection-channels-only") {
                    trigger.channel_selection =
                        ClipGeneratorChannelSelection::
                            DetectionChannelsOnly;
                }
                else if (channel_selection ==
                    "first-detection-channel-only") {
                    trigger.channel_selection =
                        ClipGeneratorChannelSelection::
                            FirstDetectionChannelOnly;
                }
                else if (channel_selection ==
                    "all-channels") {
                    trigger.channel_selection =
                        ClipGeneratorChannelSelection::
                            AllChannels;
                }
                else {
                    throw std::invalid_argument(
                        "Clip Generator channelSelection "
                        "is invalid");
                }
                if (!policy.at("clipPrefix").is_null()) {
                    trigger.clip_prefix =
                        policy.at("clipPrefix")
                            .get<std::string>();
                }
                trigger.use_data_budget =
                    policy.at("useDataBudget")
                        .get<bool>();
                trigger.data_budget_kilobytes =
                    policy.at("dataBudgetKilobytes")
                        .get<int>();
                trigger.budget_period_hours =
                    policy.at("budgetPeriodHours")
                        .get<double>();
                trigger_inputs.push_back(
                    std::move(trigger));
            }
            if (policy_blocks.size() !=
                connections_by_block.size()) {
                throw std::invalid_argument(
                    "Clip Generator trigger policies and bound "
                    "runtime blocks must correspond one-to-one");
            }
            auto output = std::make_shared<DataBlock>(
                output_descriptor(
                    *module,
                    "clips",
                    "Detection-triggered clips",
                    kAudioClipDataType,
                    audio->descriptor().sample_rate_hz,
                    audio->descriptor().channel_bitmap,
                    {"events", "waveform", "playable"}));
            prepared.nodes.push_back(
                std::make_unique<ClipGeneratorNode>(
                    module->id,
                    std::move(config),
                    audio,
                    std::move(trigger_inputs),
                    output));
            prepared.blocks.emplace(
                output->descriptor().id,
                output);
            continue;
        }

        if (module->type_id == "pamguard.spectrogram-display" ||
            module->type_id == "pamguard.sound-output") {
            // Displays and playback subscribe through their selected source
            // block; they do not create scientific output blocks.
            continue;
        }

        const auto input_port =
            module->type_id == "pamguard.click-features" ||
                module->type_id == "pamguard.click-localiser" ||
                module->type_id == "pamguard.click-train" ||
                module->type_id == "pamguard.mht-click-train" ||
                module->type_id == "pamguard.click-classifier" ||
                module->type_id ==
                    "pamguard.matched-template-classifier"
            ? "clicks"
            : "input";
        auto input = required_input(
            document,
            *module,
            input_port,
            prepared.blocks);
        const auto input_rate = input->descriptor().sample_rate_hz;
        const auto input_channels = input->descriptor().channel_bitmap;
        if (module->type_id == "pamguard.level-meter") {
            LevelMeterNodeConfig config;
            config.interval_seconds =
                settings.value("intervalSeconds", 0.25);
            config.channel_bitmap =
                settings.value("channelBitmap", 0xFFFFFFFFu);
            auto output = std::make_shared<DataBlock>(
                output_descriptor(
                    *module,
                    "levels",
                    "Level measurements",
                    kLevelMeasurementDataType,
                    input_rate,
                    input_channels & config.channel_bitmap,
                    {"timeseries", "monitoring"}));
            prepared.nodes.push_back(
                std::make_unique<LevelMeterNode>(
                    module->id,
                    std::move(config),
                    input,
                    output));
            prepared.blocks.emplace(
                output->descriptor().id,
                output);
        }
        else if (module->type_id == "pamguard.sound-recorder") {
            SoundRecorderNodeConfig config;
            config.directory =
                settings.at("directory").get<std::string>();
            if (!settings.contains("settings") ||
                !settings.at("settings").is_object() ||
                settings.value(
                    "startTransport",
                    std::string{}) != "off") {
                throw std::invalid_argument(
                    "Sound Recorder runtime settings require canonical "
                    "nested settings and safe-idle startTransport");
            }
            config.settings =
                sound_recorder_settings_from_json(
                    settings.at("settings").dump(),
                    1);
            config.file_prefix.clear();
            config.segment_seconds = 0.0;
            auto output = std::make_shared<DataBlock>(
                output_descriptor(
                    *module,
                    "recordings",
                    "Recording events",
                    kRecordingEventDataType,
                    input_rate,
                    input_channels,
                    {"events", "recordings"}));
            auto node = std::make_unique<SoundRecorderNode>(
                module->id,
                std::move(config),
                input,
                output);
            prepared.sound_recorders.emplace(
                module->id,
                node.get());
            prepared.nodes.push_back(std::move(node));
            prepared.blocks.emplace(
                output->descriptor().id,
                output);
        }
        else if (
            module->type_id == "pamguard.alarm-event-counter") {
            AlarmEventCounterNodeConfig config;
            config.count_threshold =
                settings.value("countThreshold", std::uint64_t{1});
            config.window_seconds =
                settings.value("windowSeconds", 10.0);
            config.message =
                settings.value(
                    "message",
                    std::string{"Detection alarm"});
            auto output = std::make_shared<DataBlock>(
                output_descriptor(
                    *module,
                    "alarms",
                    "Alarm states",
                    kAlarmStateDataType,
                    input_rate,
                    input_channels,
                    {"events", "monitoring"}));
            prepared.nodes.push_back(
                std::make_unique<AlarmEventCounterNode>(
                    module->id,
                    std::move(config),
                    input,
                    output));
            prepared.blocks.emplace(
                output->descriptor().id,
                output);
        }
        else if (module->type_id == "pamguard.click-features") {
            detectors::ClickFeatureConfig config;
            config.sample_rate_hz = input_rate;
            config.fft_length = settings.value("fftLength", std::size_t{0});
            config.length_energy_fraction =
                settings.value("lengthEnergyFraction", 90.0);
            config.width_energy_fraction =
                settings.value("widthEnergyFraction", 90.0);
            if (settings.contains("energyBandsHz")) {
                for (const auto& range : settings.at("energyBandsHz")) {
                    config.energy_bands_hz.push_back(
                        frequency_range_from_json(range));
                }
            }
            if (settings.contains("peakFrequencySearchHz")) {
                config.peak_frequency_search_hz =
                    frequency_range_from_json(
                        settings.at("peakFrequencySearchHz"));
            }
            if (settings.contains("meanFrequencyRangeHz")) {
                config.mean_frequency_range_hz =
                    frequency_range_from_json(
                        settings.at("meanFrequencyRangeHz"));
            }
            auto output = std::make_shared<DataBlock>(output_descriptor(
                *module,
                "features",
                "Click features",
                kClickFeatureDataType,
                input_rate,
                input_channels,
                {"measurements"}));
            prepared.nodes.push_back(std::make_unique<ClickFeatureNode>(
                module->id,
                std::move(config),
                input,
                output));
            prepared.blocks.emplace(output->descriptor().id, output);
        }
        else if (module->type_id == "pamguard.click-localiser") {
            ClickLocaliserNodeConfig config;
            config.pre_sample =
                settings.value("preSample", config.pre_sample);
            config.array.speed_of_sound_mps =
                settings.value(
                    "speedOfSoundMps",
                    config.array.speed_of_sound_mps);
            config.array.speed_of_sound_error_mps =
                settings.value("speedOfSoundErrorMps", 0.0);
            config.array.timing_error_seconds =
                settings.value("timingErrorSeconds", 0.0);
            config.array.spacing_error_m =
                settings.value("spacingErrorM", 0.0);
            config.array.wobble_radians =
                settings.value("wobbleRadians", 0.0);
            if (settings.contains("orientation")) {
                const auto& orientation =
                    settings.at("orientation");
                config.array.orientation.declared =
                    orientation.value("declared", false);
                config.array.orientation.heading_degrees =
                    orientation.value("headingDegrees", 0.0);
                config.array.orientation.pitch_degrees =
                    orientation.value("pitchDegrees", 0.0);
                config.array.orientation.roll_degrees =
                    orientation.value("rollDegrees", 0.0);
            }
            for (const auto& value :
                 settings.value("hydrophones", Json::array())) {
                ArrayHydrophone hydrophone;
                hydrophone.channel =
                    value.at("channel").get<std::size_t>();
                hydrophone.x_m = value.at("xM").get<double>();
                hydrophone.y_m = value.at("yM").get<double>();
                hydrophone.z_m = value.at("zM").get<double>();
                hydrophone.streamer_id =
                    value.value("streamerId", 0);
                hydrophone.x_error_m =
                    value.value("xErrorM", 0.0);
                hydrophone.y_error_m =
                    value.value("yErrorM", 0.0);
                hydrophone.z_error_m =
                    value.value("zErrorM", 0.0);
                config.array.hydrophones.push_back(hydrophone);
            }
            if (settings.contains("delayMeasurement")) {
                const auto& delay = settings.at("delayMeasurement");
                config.delay_measurement =
                    delay_measurement_from_json(delay);
                for (const auto& type_setting :
                     delay.value("typeSettings", Json::array())) {
                    const int click_type =
                        type_setting.at("clickType").get<int>();
                    if (click_type <= 0) {
                        throw std::invalid_argument(
                            "Click delay typeSettings clickType must be positive");
                    }
                    config.delay_measurement_by_type.insert_or_assign(
                        click_type,
                        delay_measurement_from_json(type_setting));
                }
            }
            for (const auto& value :
                 settings.value("angleVetoes", Json::array())) {
                detectors::ClickAngleVeto veto;
                veto.channels =
                    value.value("channels", std::uint32_t{0});
                veto.start_angle_degrees =
                    value.at("startAngleDegrees").get<double>();
                veto.end_angle_degrees =
                    value.at("endAngleDegrees").get<double>();
                config.angle_vetoes.push_back(veto);
            }
            auto accepted =
                std::make_shared<DataBlock>(output_descriptor(
                    *module,
                    "accepted",
                    "Accepted localised clicks",
                    kClickDataType,
                    input_rate,
                    input_channels,
                    {
                        "detections",
                        "waveform",
                        "overlay",
                        "classified",
                        "localised",
                    }));
            auto localisations =
                std::make_shared<DataBlock>(output_descriptor(
                    *module,
                    "localisations",
                    "Click delay localisations",
                    kClickLocalisationDataType,
                    input_rate,
                    input_channels,
                    {"detections", "localisation", "overlay"}));
            auto bearings =
                std::make_shared<DataBlock>(output_descriptor(
                    *module,
                    "bearings",
                    "Click bearings",
                    kClickBearingDataType,
                    input_rate,
                    input_channels,
                    {"detections", "bearing", "overlay"}));
            prepared.nodes.push_back(
                std::make_unique<ClickLocaliserNode>(
                    module->id,
                    input_rate,
                    std::move(config),
                    input,
                    ClickLocaliserNodeOutputs{
                        accepted,
                        localisations,
                        bearings,
                    }));
            prepared.blocks.emplace(
                accepted->descriptor().id,
                accepted);
            prepared.blocks.emplace(
                localisations->descriptor().id,
                localisations);
            prepared.blocks.emplace(
                bearings->descriptor().id,
                bearings);
        }
        else if (module->type_id == "pamguard.click-train") {
            ClickTrainNodeConfig node_config;
            node_config.enabled =
                settings.value("enabled", false);
            auto& config = node_config.tracker;
            config.sample_rate_hz = input_rate;
            config.min_ici_seconds =
                settings.value(
                    "minIciSeconds",
                    config.min_ici_seconds);
            config.max_ici_seconds =
                settings.value(
                    "maxIciSeconds",
                    config.max_ici_seconds);
            config.max_ici_change =
                settings.value(
                    "maxIciChange",
                    config.max_ici_change);
            config.ok_angle_error_degrees =
                settings.value(
                    "okAngleErrorDegrees",
                    config.ok_angle_error_degrees);
            config.initial_perpendicular_distance_m =
                settings.value(
                    "initialPerpendicularDistanceM",
                    config.initial_perpendicular_distance_m);
            config.min_clicks =
                settings.value("minClicks", config.min_clicks);
            config.min_angle_change_degrees =
                settings.value(
                    "minAngleChangeDegrees",
                    config.min_angle_change_degrees);
            config.ici_update_ratio =
                settings.value(
                    "iciUpdateRatio",
                    config.ici_update_ratio);
            config.min_update_gap_seconds =
                settings.value(
                    "minUpdateGapSeconds",
                    config.min_update_gap_seconds);
            auto output = std::make_shared<DataBlock>(output_descriptor(
                *module,
                "trains",
                "Click trains",
                kClickTrainDataType,
                input_rate,
                input_channels,
                {"detections", "grouped"}));
            prepared.nodes.push_back(std::make_unique<ClickTrainNode>(
                module->id,
                std::move(node_config),
                input,
                output));
            prepared.blocks.emplace(output->descriptor().id, output);
        }
        else if (module->type_id == "pamguard.mht-click-train") {
            MhtClickTrainNodeConfig config;
            config.sample_rate_hz = input_rate;
            config.min_clicks =
                settings.value("minClicks", config.min_clicks);
            config.channel_groups = settings.value(
                "channelGroups",
                std::vector<std::uint32_t>{});
            if (settings.contains("dataSelector")) {
                const auto& selector = settings.at("dataSelector");
                config.data_selector_enabled =
                    selector.value("enabled", false);
                config.data_selector_use_echoes =
                    selector.value("useEchoes", true);
                const auto minimum_amplitude_db =
                    selector.value("minimumAmplitudeDb", 0.0);
                if (minimum_amplitude_db != 0.0) {
                    throw std::invalid_argument(
                        "MHT click-train minimumAmplitudeDb must remain "
                        "zero until clicks carry calibrated PAMGuard dB");
                }
                config.data_selector_included_click_types =
                    selector.value(
                        "includedClickTypes",
                        std::vector<int>{});
            }
            if (settings.contains("kernel")) {
                const auto& kernel = settings.at("kernel");
                config.kernel.n_hold =
                    kernel.value("nHold", config.kernel.n_hold);
                config.kernel.n_pruneback = kernel.value(
                    "nPruneback",
                    config.kernel.n_pruneback);
                config.kernel.n_pruneback_start = kernel.value(
                    "nPrunebackStart",
                    config.kernel.n_pruneback_start);
                config.kernel.max_coast = kernel.value(
                    "maxCoast",
                    config.kernel.max_coast);
            }
            if (settings.contains("chi2")) {
                const auto& chi2 = settings.at("chi2");
                config.chi2.enable_idi =
                    chi2.value("enableIdi", config.chi2.enable_idi);
                config.chi2.enable_amplitude = chi2.value(
                    "enableAmplitude",
                    config.chi2.enable_amplitude);
                config.chi2.enable_length = chi2.value(
                    "enableLength",
                    config.chi2.enable_length);
                config.chi2.enable_bearing = chi2.value(
                    "enableBearing",
                    config.chi2.enable_bearing);
                config.chi2.enable_peak_frequency = chi2.value(
                    "enablePeakFrequency",
                    config.chi2.enable_peak_frequency);
                config.chi2.enable_time_delay = chi2.value(
                    "enableTimeDelay",
                    config.chi2.enable_time_delay);
                config.chi2.enable_correlation = chi2.value(
                    "enableCorrelation",
                    config.chi2.enable_correlation);
                config.chi2.correlation_fft_length = chi2.value(
                    "correlationFftLength",
                    config.chi2.correlation_fft_length);
                config.chi2.coast_penalty = chi2.value(
                    "coastPenalty",
                    config.chi2.coast_penalty);
                config.chi2.new_track_penalty = chi2.value(
                    "newTrackPenalty",
                    config.chi2.new_track_penalty);
                config.chi2.new_track_n = chi2.value(
                    "newTrackN",
                    config.chi2.new_track_n);
                config.chi2.max_ici = chi2.value(
                    "maxIciSeconds",
                    config.chi2.max_ici);
                config.chi2.low_ici_exponent = chi2.value(
                    "lowIciExponent",
                    config.chi2.low_ici_exponent);
                config.chi2.long_track_exponent = chi2.value(
                    "longTrackExponent",
                    config.chi2.long_track_exponent);
                config.chi2.junk_track_penalty = chi2.value(
                    "junkTrackPenalty",
                    config.chi2.junk_track_penalty);
                config.chi2.max_chi =
                    chi2.value("maxChi", config.chi2.max_chi);
                config.chi2.use_electrical_noise_filter = chi2.value(
                    "useElectricalNoiseFilter",
                    config.chi2.use_electrical_noise_filter);
                config.chi2.electrical_noise_min_chi2 = chi2.value(
                    "electricalNoiseMinChi2",
                    config.chi2.electrical_noise_min_chi2);
                config.chi2.electrical_noise_n_data_units = chi2.value(
                    "electricalNoiseNDataUnits",
                    config.chi2.electrical_noise_n_data_units);
                if (chi2.contains("idi")) {
                    const auto& value = chi2.at("idi");
                    config.chi2.idi.error =
                        value.value("error", config.chi2.idi.error);
                    config.chi2.idi.min_error = value.value(
                        "minimumError",
                        config.chi2.idi.min_error);
                    config.chi2.idi.min_idi = value.value(
                        "minimumIdiSeconds",
                        config.chi2.idi.min_idi);
                }
                if (chi2.contains("amplitude")) {
                    const auto& value = chi2.at("amplitude");
                    config.chi2.amplitude.error = value.value(
                        "error",
                        config.chi2.amplitude.error);
                    config.chi2.amplitude.min_error = value.value(
                        "minimumError",
                        config.chi2.amplitude.min_error);
                    config.chi2.amplitude.amp_jump_enable = value.value(
                        "jumpEnabled",
                        config.chi2.amplitude.amp_jump_enable);
                    config.chi2.amplitude.max_amp_jump_db = value.value(
                        "maximumJumpDb",
                        config.chi2.amplitude.max_amp_jump_db);
                }
                if (chi2.contains("bearing")) {
                    const auto& value = chi2.at("bearing");
                    config.chi2.bearing.error_radians = value.value(
                        "errorRadians",
                        config.chi2.bearing.error_radians);
                    config.chi2.bearing.min_error_radians = value.value(
                        "minimumErrorRadians",
                        config.chi2.bearing.min_error_radians);
                    config.chi2.bearing.bearing_jump_enable = value.value(
                        "jumpEnabled",
                        config.chi2.bearing.bearing_jump_enable);
                    config.chi2.bearing.max_bearing_jump_radians =
                        value.value(
                            "maximumJumpRadians",
                            config.chi2.bearing
                                .max_bearing_jump_radians);
                    const auto direction = value.value(
                        "jumpDirection",
                        std::string{"positive"});
                    if (direction == "both") {
                        config.chi2.bearing.jump_direction =
                            detectors::MhtBearingJumpDirection::Both;
                    }
                    else if (direction == "positive") {
                        config.chi2.bearing.jump_direction =
                            detectors::MhtBearingJumpDirection::Positive;
                    }
                    else if (direction == "negative") {
                        config.chi2.bearing.jump_direction =
                            detectors::MhtBearingJumpDirection::Negative;
                    }
                    else {
                        throw std::invalid_argument(
                            "Unsupported MHT bearing jumpDirection");
                    }
                }
                if (chi2.contains("correlation")) {
                    const auto& value = chi2.at("correlation");
                    config.chi2.correlation.error = value.value(
                        "error",
                        config.chi2.correlation.error);
                    config.chi2.correlation.min_error = value.value(
                        "minimumError",
                        config.chi2.correlation.min_error);
                }
                if (chi2.contains("timeDelay")) {
                    const auto& value = chi2.at("timeDelay");
                    config.chi2.time_delay.error = value.value(
                        "error",
                        config.chi2.time_delay.error);
                    config.chi2.time_delay.min_error = value.value(
                        "minimumError",
                        config.chi2.time_delay.min_error);
                }
                if (chi2.contains("length")) {
                    const auto& value = chi2.at("length");
                    config.chi2.length.error = value.value(
                        "error",
                        config.chi2.length.error);
                    config.chi2.length.min_error = value.value(
                        "minimumError",
                        config.chi2.length.min_error);
                }
                if (chi2.contains("peakFrequency")) {
                    const auto& value = chi2.at("peakFrequency");
                    config.chi2.peak_frequency.error = value.value(
                        "error",
                        config.chi2.peak_frequency.error);
                    config.chi2.peak_frequency.min_error = value.value(
                        "minimumError",
                        config.chi2.peak_frequency.min_error);
                }
            }
            if (settings.contains("classifier")) {
                const auto& classifier = settings.at("classifier");
                config.classify =
                    classifier.value("enabled", config.classify);
                config.average_spectrum_fft_length = classifier.value(
                    "averageSpectrumFftLength",
                    config.average_spectrum_fft_length);
                if (classifier.contains("pre")) {
                    const auto& pre = classifier.at("pre");
                    config.pre_classifier.chi2_threshold = pre.value(
                        "chi2Threshold",
                        config.pre_classifier.chi2_threshold);
                    config.pre_classifier.min_clicks = pre.value(
                        "minClicks",
                        config.pre_classifier.min_clicks);
                    config.pre_classifier.min_time_seconds = pre.value(
                        "minTimeSeconds",
                        config.pre_classifier.min_time_seconds);
                    config.pre_classifier.species_flag = pre.value(
                        "speciesFlag",
                        config.pre_classifier.species_flag);
                }
                if (classifier.contains("idi")) {
                    const auto& idi = classifier.at("idi");
                    config.idi_classifier_enabled =
                        idi.value("enabled", false);
                    config.idi_classifier.use_median_idi = idi.value(
                        "useMedianIdi",
                        config.idi_classifier.use_median_idi);
                    config.idi_classifier.min_median_idi = idi.value(
                        "minMedianIdi",
                        config.idi_classifier.min_median_idi);
                    config.idi_classifier.max_median_idi = idi.value(
                        "maxMedianIdi",
                        config.idi_classifier.max_median_idi);
                    config.idi_classifier.use_mean_idi = idi.value(
                        "useMeanIdi",
                        config.idi_classifier.use_mean_idi);
                    config.idi_classifier.min_mean_idi = idi.value(
                        "minMeanIdi",
                        config.idi_classifier.min_mean_idi);
                    config.idi_classifier.max_mean_idi = idi.value(
                        "maxMeanIdi",
                        config.idi_classifier.max_mean_idi);
                    config.idi_classifier.use_std_idi = idi.value(
                        "useStdIdi",
                        config.idi_classifier.use_std_idi);
                    config.idi_classifier.min_std_idi = idi.value(
                        "minStdIdi",
                        config.idi_classifier.min_std_idi);
                    config.idi_classifier.max_std_idi = idi.value(
                        "maxStdIdi",
                        config.idi_classifier.max_std_idi);
                    config.idi_classifier.species_flag = idi.value(
                        "speciesFlag",
                        config.idi_classifier.species_flag);
                }
                if (classifier.contains("bearing")) {
                    const auto& bearing = classifier.at("bearing");
                    config.bearing_classifier_enabled =
                        bearing.value("enabled", false);
                    config.bearing_classifier.bearing_lim_min =
                        bearing.value(
                            "bearingLimitMinRadians",
                            config.bearing_classifier.bearing_lim_min);
                    config.bearing_classifier.bearing_lim_max =
                        bearing.value(
                            "bearingLimitMaxRadians",
                            config.bearing_classifier.bearing_lim_max);
                    config.bearing_classifier.use_mean =
                        bearing.value(
                            "useMean",
                            config.bearing_classifier.use_mean);
                    config.bearing_classifier
                        .min_mean_bearing_derivative =
                        bearing.value(
                            "minMeanDerivative",
                            config.bearing_classifier
                                .min_mean_bearing_derivative);
                    config.bearing_classifier
                        .max_mean_bearing_derivative =
                        bearing.value(
                            "maxMeanDerivative",
                            config.bearing_classifier
                                .max_mean_bearing_derivative);
                    config.bearing_classifier.use_median =
                        bearing.value(
                            "useMedian",
                            config.bearing_classifier.use_median);
                    config.bearing_classifier
                        .min_median_bearing_derivative =
                        bearing.value(
                            "minMedianDerivative",
                            config.bearing_classifier
                                .min_median_bearing_derivative);
                    config.bearing_classifier
                        .max_median_bearing_derivative =
                        bearing.value(
                            "maxMedianDerivative",
                            config.bearing_classifier
                                .max_median_bearing_derivative);
                    config.bearing_classifier.use_std =
                        bearing.value(
                            "useStd",
                            config.bearing_classifier.use_std);
                    config.bearing_classifier
                        .min_std_bearing_derivative =
                        bearing.value(
                            "minStdDerivative",
                            config.bearing_classifier
                                .min_std_bearing_derivative);
                    config.bearing_classifier
                        .max_std_bearing_derivative =
                        bearing.value(
                            "maxStdDerivative",
                            config.bearing_classifier
                                .max_std_bearing_derivative);
                    config.bearing_classifier.species_flag =
                        bearing.value(
                            "speciesFlag",
                            config.bearing_classifier.species_flag);
                }
                if (classifier.contains("template")) {
                    const auto& value = classifier.at("template");
                    config.template_classifier_enabled =
                        value.value("enabled", false);
                    config.template_classifier.template_spectrum =
                        value.value(
                            "templateSpectrum",
                            std::vector<double>{});
                    config.template_classifier.template_sample_rate_hz =
                        value.value("templateSampleRateHz", 0.0);
                    config.template_classifier.correlation_threshold =
                        value.value(
                            "correlationThreshold",
                            config.template_classifier
                                .correlation_threshold);
                    config.template_classifier.species_flag =
                        value.value(
                            "speciesFlag",
                            config.template_classifier.species_flag);
                }
            }
            auto trains =
                std::make_shared<DataBlock>(output_descriptor(
                    *module,
                    "trains",
                    "MHT click trains",
                    kMhtClickTrainDataType,
                    input_rate,
                    input_channels,
                    {"detections", "grouped"}));
            auto classifications =
                std::make_shared<DataBlock>(output_descriptor(
                    *module,
                    "classifications",
                    "Click-train classifications",
                    kClickTrainClassificationDataType,
                    input_rate,
                    input_channels,
                    {"annotations"}));
            prepared.nodes.push_back(
                std::make_unique<MhtClickTrainNode>(
                    module->id,
                    std::move(config),
                    MhtClickTrainNodeInputs{
                        input,
                        optional_input(
                            document,
                            *module,
                            "features",
                            prepared.blocks),
                        optional_input(
                            document,
                            *module,
                            "localisations",
                            prepared.blocks),
                        optional_input(
                            document,
                            *module,
                            "bearings",
                            prepared.blocks),
                    },
                    MhtClickTrainNodeOutputs{
                        trains,
                        classifications,
                    }));
            prepared.blocks.emplace(
                trains->descriptor().id,
                trains);
            prepared.blocks.emplace(
                classifications->descriptor().id,
                classifications);
        }
        else if (module->type_id == "pamguard.click-classifier") {
            ClickClassifierNodeConfig config;
            config.enabled =
                settings.value("enabled", false);
            const auto mode = settings.value(
                "mode",
                std::string("sweep"));
            if (mode == "basic") {
                config.type = ClickClassifierNodeType::Basic;
                config.basic.sample_rate_hz = input_rate;
                for (const auto& type :
                     settings.value("types", Json::array())) {
                    config.basic.click_types.push_back(
                        basic_click_type_from_json(type));
                }
            }
            else if (mode == "sweep") {
                config.type = ClickClassifierNodeType::Sweep;
                config.sweep.sample_rate_hz = input_rate;
                config.sweep.check_all_classifiers = settings.value(
                    "checkAllClassifiers",
                    false);
                config.sweep.amplitude_db_offset_by_channel =
                    settings.value(
                        "amplitudeDbOffsetByChannel",
                        std::vector<double>{});
                for (const auto& type :
                     settings.value("types", Json::array())) {
                    config.sweep.click_types.push_back(
                        sweep_click_type_from_json(type));
                }
            }
            else {
                throw std::invalid_argument(
                    "Click classifier mode must be basic or sweep");
            }
            config.discard_unclassified = settings.value(
                "discardUnclassified",
                false);
            auto accepted = std::make_shared<DataBlock>(output_descriptor(
                *module,
                "accepted",
                "Accepted clicks",
                kClickDataType,
                input_rate,
                input_channels,
                {
                    "detections",
                    "waveform",
                    "overlay",
                    "classified",
                }));
            auto classifications =
                std::make_shared<DataBlock>(output_descriptor(
                    *module,
                    "classifications",
                    "Click classifications",
                    kClickClassificationDataType,
                    input_rate,
                    input_channels,
                    {"annotations"}));
            prepared.nodes.push_back(
                std::make_unique<ClickClassifierNode>(
                    module->id,
                    std::move(config),
                    input,
                    ClickClassifierNodeOutputs{
                        accepted,
                        classifications,
                    }));
            prepared.blocks.emplace(
                accepted->descriptor().id,
                accepted);
            prepared.blocks.emplace(
                classifications->descriptor().id,
                classifications);
        }
        else if (
            module->type_id ==
            "pamguard.matched-template-classifier") {
            MatchedTemplateNodeConfig config;
            const auto decoded =
                matched_template_settings_from_json(
                    settings.dump(),
                    1);
            config.classifier.enabled = true;
            config.classifier.normalisation_type =
                decoded.normalisation_type;
            config.classifier.peak_search =
                decoded.peak_search;
            config.classifier.peak_smoothing =
                decoded.peak_smoothing;
            config.classifier.length_db =
                decoded.length_db;
            config.classifier.restricted_bins =
                decoded.restricted_bins;
            config.classifier.channel_classification =
                decoded.channel_classification;
            config.classifier.classifiers =
                decoded.classifiers;
            config.click_type = decoded.click_type;
            auto accepted = std::make_shared<DataBlock>(output_descriptor(
                *module,
                "accepted",
                "Classified clicks",
                kClickDataType,
                input_rate,
                input_channels,
                {
                    "detections",
                    "waveform",
                    "overlay",
                    "classified",
                }));
            auto classifications =
                std::make_shared<DataBlock>(output_descriptor(
                    *module,
                    "classifications",
                    "Matched-template classifications",
                    kMatchedTemplateClassificationDataType,
                    input_rate,
                    input_channels,
                    {"annotations", "classified"}));
            prepared.nodes.push_back(
                std::make_unique<MatchedTemplateNode>(
                    module->id,
                    input_rate,
                    std::move(config),
                    input,
                    ClickClassifierNodeOutputs{
                        accepted,
                        classifications,
                    }));
            prepared.blocks.emplace(
                accepted->descriptor().id,
                accepted);
            prepared.blocks.emplace(
                classifications->descriptor().id,
                classifications);
        }
        else if (module->type_id == "pamguard.noise-band-monitor") {
            NoiseBandNodeConfig config;
            config.monitor.enabled = true;
            config.channel_bitmap = intersect_selected_channels(
                settings.value("channelBitmap", input_channels),
                input_channels,
                module->id);
            config.monitor.band_type = noise_band_type_from_json(
                settings.value(
                    "bandType",
                    std::string("thirdOctave")));
            config.monitor.min_frequency_hz = settings.value(
                "minimumFrequencyHz",
                config.monitor.min_frequency_hz);
            config.monitor.max_frequency_hz = settings.value(
                "maximumFrequencyHz",
                config.monitor.max_frequency_hz);
            config.monitor.reference_frequency_hz = settings.value(
                "referenceFrequencyHz",
                config.monitor.reference_frequency_hz);
            config.monitor.iir_order =
                settings.value("iirOrder", config.monitor.iir_order);
            config.monitor.filter_type = filter_type(
                settings.value(
                    "filterType",
                    std::string("butterworth")));
            config.monitor.fir_order = settings.value(
                "firOrder",
                config.monitor.fir_order);
            config.monitor.fir_gamma = settings.value(
                "firGamma",
                config.monitor.fir_gamma);
            config.monitor.output_interval_seconds = settings.value(
                "outputIntervalSeconds",
                config.monitor.output_interval_seconds);
            config.calibration_db_offset_by_channel = settings.value(
                "calibrationDbOffsetByChannel",
                std::vector<double>{});
            auto output = std::make_shared<DataBlock>(output_descriptor(
                *module,
                "measurements",
                "Band noise measurements",
                kNoiseBandDataType,
                input_rate,
                input_channels & config.channel_bitmap,
                {"measurements"}));
            prepared.nodes.push_back(std::make_unique<NoiseBandNode>(
                module->id,
                input_rate,
                std::move(config),
                input,
                output));
            prepared.blocks.emplace(output->descriptor().id, output);
        }
        else if (module->type_id == "pamguard.fft-noise-monitor") {
            detectors::FftNoiseConfig config;
            config.enabled = true;
            config.channels =
                settings.at("channels").get<std::vector<std::size_t>>();
            const auto selected_channels = selected_channel_bitmap(
                config.channels,
                input_channels,
                module->id);
            config.measurement_interval_seconds = settings.value(
                "measurementIntervalSeconds",
                config.measurement_interval_seconds);
            config.n_measures =
                settings.value("nMeasures", config.n_measures);
            config.use_all = settings.value("useAll", config.use_all);
            for (const auto& band : settings.at("bands")) {
                config.bands.push_back({
                    band.value("name", std::string{}),
                    band.at("lowFrequencyHz").get<double>(),
                    band.at("highFrequencyHz").get<double>(),
                });
            }
            const auto fft_length = settings.contains("fftLength")
                ? settings.at("fftLength").get<std::size_t>()
                : input->descriptor().fft_length.value_or(0);
            const auto fft_hop = settings.contains("fftHop")
                ? settings.at("fftHop").get<std::size_t>()
                : input->descriptor().fft_hop.value_or(0);
            if (fft_length == 0 || fft_hop == 0) {
                throw std::invalid_argument(
                    "Module " + module->id +
                    " requires FFT length/hop metadata from its source");
            }
            const double resolution_hz =
                input_rate / static_cast<double>(fft_length);
            for (const auto& band : config.bands) {
                if (band.low_frequency_hz < 0.0 ||
                    !(band.high_frequency_hz >
                      band.low_frequency_hz) ||
                    band.high_frequency_hz > input_rate / 2.0 ||
                    band.high_frequency_hz -
                            band.low_frequency_hz <
                        resolution_hz) {
                    throw std::invalid_argument(
                        "Module " + module->id +
                        " has a measurement band outside the bound "
                        "FFT resolution/Nyquist constraints");
                }
            }
            auto output = std::make_shared<DataBlock>(output_descriptor(
                *module,
                "measurements",
                "Noise measurements",
                kFftNoiseDataType,
                input_rate,
                selected_channels,
                {"measurements"}));
            prepared.nodes.push_back(std::make_unique<FftNoiseNode>(
                module->id,
                input_rate,
                fft_length,
                fft_hop,
                std::move(config),
                input,
                output));
            prepared.blocks.emplace(output->descriptor().id, output);
        }
        else if (module->type_id == "pamguard.ltsa") {
            detectors::LtsaConfig config;
            config.enabled = true;
            config.interval_seconds =
                settings.value("intervalSeconds", config.interval_seconds);
            config.channel_bitmap = intersect_selected_channels(
                settings.value("channelBitmap", input_channels),
                input_channels,
                module->id);
            auto output = std::make_shared<DataBlock>(output_descriptor(
                *module,
                "ltsa",
                "LTSA periods",
                kLtsaDataType,
                input_rate,
                input_channels & config.channel_bitmap,
                {"frequency-domain", "measurements"}));
            prepared.nodes.push_back(std::make_unique<LtsaNode>(
                module->id,
                std::move(config),
                input,
                output));
            prepared.blocks.emplace(output->descriptor().id, output);
        }
        else if (module->type_id == "pamguard.ishmael-energy-sum") {
            if (!input->descriptor().fft_length ||
                !input->descriptor().fft_hop) {
                throw std::invalid_argument(
                    "Ishmael Energy Sum requires FFT geometry from its selected source");
            }
            const auto fft_length =
                *input->descriptor().fft_length;
            const auto fft_hop =
                *input->descriptor().fft_hop;
            if ((settings.contains("fftLength") &&
                 settings.at("fftLength").get<std::size_t>() !=
                     fft_length) ||
                (settings.contains("fftHop") &&
                 settings.at("fftHop").get<std::size_t>() !=
                     fft_hop)) {
                throw std::invalid_argument(
                    "Ishmael Energy Sum projected FFT geometry differs from its selected source");
            }
            detectors::IshmaelEnergySumConfig config;
            config.enabled = true;
            config.f0 = settings.value("f0Hz", config.f0);
            config.f1 = settings.value("f1Hz", config.f1);
            config.ratio_f0 =
                settings.value("ratioF0Hz", config.ratio_f0);
            config.ratio_f1 =
                settings.value("ratioF1Hz", config.ratio_f1);
            config.use_ratio =
                settings.value("useRatio", config.use_ratio);
            config.use_log = settings.value("useLog", config.use_log);
            config.adaptive_threshold = settings.value(
                "adaptiveThreshold",
                config.adaptive_threshold);
            config.long_filter =
                settings.value("longFilter", config.long_filter);
            config.spike_decay =
                settings.value("spikeDecay", config.spike_decay);
            config.output_smoothing = settings.value(
                "outputSmoothing",
                config.output_smoothing);
            config.short_filter =
                settings.value("shortFilter", config.short_filter);
            config.thresh =
                settings.value("threshold", config.thresh);
            config.min_time_s =
                settings.value("minTimeSeconds", config.min_time_s);
            config.max_time_s =
                settings.value("maxTimeSeconds", config.max_time_s);
            config.refractory_time_s = settings.value(
                "refractoryTimeSeconds",
                config.refractory_time_s);
            const auto function_channels =
                intersect_selected_channels(
                    settings.at("channelBitmap")
                        .get<std::uint32_t>(),
                    input_channels,
                    module->id);
            const auto detection_channels =
                intersect_selected_channels(
                    settings.at("activeChannelBitmap")
                        .get<std::uint32_t>(),
                    function_channels,
                    module->id);
            auto function = std::make_shared<DataBlock>(output_descriptor(
                *module,
                "function",
                "Detection function",
                kIshmaelFunctionDataType,
                input_rate,
                function_channels,
                {"timeseries"}));
            auto detections =
                std::make_shared<DataBlock>(output_descriptor(
                    *module,
                    "detections",
                    "Ishmael detections",
                    kIshmaelDetectionDataType,
                    input_rate,
                    detection_channels,
                    {"detections", "overlay"}));
            prepared.nodes.push_back(std::make_unique<IshmaelNode>(
                module->id,
                input_rate,
                fft_hop,
                std::move(config),
                input,
                IshmaelNodeOutputs{
                    function,
                    detections,
                }));
            prepared.blocks.emplace(
                function->descriptor().id,
                function);
            prepared.blocks.emplace(
                detections->descriptor().id,
                detections);
        }
        else if (module->type_id == "pamguard.spectrogram-noise") {
            const auto config =
                spectrogram_noise_from_json(settings);
            auto descriptor = output_descriptor(
                *module,
                "output",
                "Noise-reduced FFT",
                kFftDataType,
                input_rate,
                input_channels,
                {"frequency-domain"});
            descriptor.minimum_frequency_hz =
                input->descriptor().minimum_frequency_hz;
            descriptor.maximum_frequency_hz =
                input->descriptor().maximum_frequency_hz;
            descriptor.fft_length =
                input->descriptor().fft_length;
            descriptor.fft_hop =
                input->descriptor().fft_hop;
            auto output =
                std::make_shared<DataBlock>(std::move(descriptor));
            prepared.nodes.push_back(
                std::make_unique<SpectrogramNoiseNode>(
                    module->id,
                    config,
                    input,
                    output));
            prepared.blocks.emplace(
                output->descriptor().id,
                output);
        }
        else if (module->type_id == "pamguard.whistles-moans") {
            const auto decoded =
                whistle_moan_contour_runtime_settings_from_json(
                    settings.dump(),
                    1);
            if (!std::isfinite(input_rate) ||
                !(input_rate > 0.0) ||
                input_rate >
                    static_cast<double>(
                        std::numeric_limits<
                            std::uint32_t>::max())) {
                throw std::invalid_argument(
                    "Whistles & Moans requires a positive integral FFT sample rate");
            }
            const auto rounded_input_rate =
                std::llround(input_rate);
            if (rounded_input_rate <= 0 ||
                std::abs(
                    input_rate -
                    static_cast<double>(
                        rounded_input_rate)) > 1e-9) {
                throw std::invalid_argument(
                    "Whistles & Moans requires a positive integral FFT sample rate");
            }
            WhistleMoanNodeConfig config;
            config.channel_bitmap =
                intersect_selected_channels(
                    decoded.channel_bitmap,
                    input_channels,
                    module->id);
            if (decoded.grouping_type ==
                WhistleSourceGrouping::Singles) {
                config.grouping = WhistleGroupingType::Singles;
            }
            else if (decoded.grouping_type ==
                     WhistleSourceGrouping::All) {
                config.grouping = WhistleGroupingType::All;
            }
            else {
                config.grouping = WhistleGroupingType::User;
            }
            config.channel_groups =
                decoded.channel_groups;
            config.contours.sample_rate_hz =
                static_cast<std::uint32_t>(
                    rounded_input_rate);
            config.contours.min_pixels =
                decoded.min_pixels;
            config.contours.min_length =
                decoded.min_length;
            config.contours.connect_type =
                decoded.connect_type;
            config.contours.min_frequency_hz =
                decoded.min_frequency_hz;
            config.contours.max_frequency_hz =
                decoded.max_frequency_hz;
            config.contours.keep_shape_stubs =
                decoded.keep_shape_stubs;
            config.contours.fragmentation_method =
                decoded.fragmentation_method;
            config.contours.max_cross_length =
                decoded.max_cross_length;
            auto contour_output =
                std::make_shared<DataBlock>(output_descriptor(
                    *module,
                    "contours",
                    "Whistle/moan contours",
                    kWhistleContourDataType,
                    input_rate,
                    config.channel_bitmap,
                    {"detections", "overlay"}));
            prepared.nodes.push_back(
                std::make_unique<WhistleMoanNode>(
                    module->id,
                    std::move(config),
                    input,
                    WhistleMoanNodeOutputs{
                        contour_output,
                    }));
            prepared.blocks.emplace(
                contour_output->descriptor().id,
                contour_output);
        }
        else if (module->type_id == "pamguard.ishmael-sgram-corr") {
            if (!input->descriptor().fft_length ||
                !input->descriptor().fft_hop) {
                throw std::invalid_argument(
                    "Ishmael Spectrogram Correlation requires FFT geometry from its selected source");
            }
            const auto fft_length =
                *input->descriptor().fft_length;
            const auto fft_hop =
                *input->descriptor().fft_hop;
            if ((settings.contains("fftLength") &&
                 settings.at("fftLength").get<std::size_t>() !=
                     fft_length) ||
                (settings.contains("fftHop") &&
                 settings.at("fftHop").get<std::size_t>() !=
                     fft_hop)) {
                throw std::invalid_argument(
                    "Ishmael Spectrogram Correlation projected FFT geometry differs from its selected source");
            }
            detectors::SgramCorrConfig config;
            config.enabled = true;
            for (const auto& segment : settings.at("segments")) {
                if (!segment.is_array() || segment.size() != 4) {
                    throw std::invalid_argument(
                        "Spectrogram-correlation segments must have four values");
                }
                config.segments.push_back({
                    segment.at(0).get<double>(),
                    segment.at(1).get<double>(),
                    segment.at(2).get<double>(),
                    segment.at(3).get<double>(),
                });
            }
            config.spread = settings.value("spreadHz", config.spread);
            config.use_log = settings.value("useLog", config.use_log);
            config.thresh = settings.value("threshold", config.thresh);
            config.min_time_s =
                settings.value("minTimeSeconds", config.min_time_s);
            config.max_time_s =
                settings.value("maxTimeSeconds", config.max_time_s);
            config.refractory_time_s = settings.value(
                "refractoryTimeSeconds",
                config.refractory_time_s);
            const auto function_channels =
                intersect_selected_channels(
                    settings.at("channelBitmap")
                        .get<std::uint32_t>(),
                    input_channels,
                    module->id);
            const auto detection_channels =
                intersect_selected_channels(
                    settings.at("activeChannelBitmap")
                        .get<std::uint32_t>(),
                    function_channels,
                    module->id);
            auto function = std::make_shared<DataBlock>(output_descriptor(
                *module,
                "function",
                "Detection function",
                kIshmaelFunctionDataType,
                input_rate,
                function_channels,
                {"timeseries"}));
            auto detections =
                std::make_shared<DataBlock>(output_descriptor(
                    *module,
                    "detections",
                    "Ishmael detections",
                    kIshmaelDetectionDataType,
                    input_rate,
                    detection_channels,
                    {"detections", "overlay"}));
            prepared.nodes.push_back(std::make_unique<SgramCorrNode>(
                module->id,
                input_rate,
                fft_length,
                fft_hop,
                std::move(config),
                input,
                IshmaelNodeOutputs{function, detections}));
            prepared.blocks.emplace(
                function->descriptor().id,
                function);
            prepared.blocks.emplace(
                detections->descriptor().id,
                detections);
        }
        else if (module->type_id == "pamguard.ishmael-match-filter") {
            detectors::MatchFiltConfig config;
            config.enabled = true;
            config.kernel =
                settings.at("kernel").get<std::vector<double>>();
            config.channels =
                settings.value(
                    "channels",
                    std::vector<std::size_t>{0});
            const auto active_channels =
                selected_channel_bitmap(
                    config.channels,
                    input_channels,
                    module->id);
            config.thresh = settings.value("threshold", config.thresh);
            config.min_time_s =
                settings.value("minTimeSeconds", config.min_time_s);
            config.max_time_s =
                settings.value("maxTimeSeconds", config.max_time_s);
            config.refractory_time_s = settings.value(
                "refractoryTimeSeconds",
                config.refractory_time_s);
            auto function = std::make_shared<DataBlock>(output_descriptor(
                *module,
                "function",
                "Detection function",
                kIshmaelFunctionDataType,
                input_rate,
                active_channels,
                {"timeseries"}));
            auto detections =
                std::make_shared<DataBlock>(output_descriptor(
                    *module,
                    "detections",
                    "Ishmael detections",
                    kIshmaelDetectionDataType,
                    input_rate,
                    active_channels,
                    {"detections", "overlay"}));
            prepared.nodes.push_back(std::make_unique<MatchFiltNode>(
                module->id,
                input_rate,
                std::move(config),
                input,
                IshmaelNodeOutputs{function, detections}));
            prepared.blocks.emplace(
                function->descriptor().id,
                function);
            prepared.blocks.emplace(
                detections->descriptor().id,
                detections);
        }
        else if (module->type_id == "pamguard.click-detector") {
            ClickDetectorNodeConfig node_config;
            auto& config = node_config.detector;
            config.channel_bitmap =
                settings.value("channelBitmap", std::uint32_t{3});
            config.trigger_bitmap =
                settings.value("triggerBitmap", config.trigger_bitmap);
            config.channel_bitmap = intersect_selected_channels(
                config.channel_bitmap,
                input_channels,
                module->id);
            config.trigger_bitmap = intersect_selected_channels(
                config.trigger_bitmap,
                config.channel_bitmap,
                module->id);
            config.min_trigger_channels =
                settings.value(
                    "minTriggerChannels",
                    config.min_trigger_channels);
            config.threshold_db =
                settings.value("thresholdDb", config.threshold_db);
            config.long_filter =
                settings.value("longFilter", config.long_filter);
            config.long_filter_2 =
                settings.value("longFilter2", config.long_filter_2);
            config.short_filter =
                settings.value("shortFilter", config.short_filter);
            config.pre_sample =
                settings.value("preSample", config.pre_sample);
            config.post_sample =
                settings.value("postSample", config.post_sample);
            config.min_sep =
                settings.value("minSep", config.min_sep);
            config.max_length =
                settings.value("maxLength", config.max_length);
            config.sample_noise =
                settings.value("sampleNoise", config.sample_noise);
            config.noise_sample_interval_seconds =
                settings.value(
                    "noiseSampleIntervalSeconds",
                    config.noise_sample_interval_seconds);
            config.store_background =
                settings.value("storeBackground", config.store_background);
            config.background_interval_milliseconds =
                settings.value(
                    "backgroundIntervalMilliseconds",
                    config.background_interval_milliseconds);
            config.publish_trigger_function =
                settings.value(
                    "publishTriggerFunction",
                    config.publish_trigger_function);
            if (settings.contains("preFilter")) {
                config.pre_filter = filter_params_from_json(
                    settings.at("preFilter"),
                    config.pre_filter);
            }
            if (settings.contains("triggerFilter")) {
                config.trigger_filter = filter_params_from_json(
                    settings.at("triggerFilter"),
                    config.trigger_filter);
            }

            const auto grouping_type =
                settings.value("groupingType", std::string{"all"});
            if (grouping_type == "all") {
                node_config.channel_group_bitmaps.push_back(
                    config.channel_bitmap);
            }
            else if (grouping_type == "singles") {
                for (std::size_t channel = 0; channel < 32; ++channel) {
                    const auto channel_bit =
                        std::uint32_t{1} << channel;
                    if ((config.channel_bitmap & channel_bit) != 0) {
                        node_config.channel_group_bitmaps.push_back(
                            channel_bit);
                    }
                }
            }
            else if (grouping_type == "user") {
                const auto channel_groups =
                    settings.value(
                        "channelGroups",
                        std::vector<int>{});
                for (const auto group : channel_groups) {
                    if (group < 0 || group >= 32) {
                        throw std::invalid_argument(
                            "Click detector channel group numbers "
                            "must be between 0 and 31");
                    }
                }
                std::map<int, std::uint32_t> group_bitmaps;
                for (std::size_t channel = 0; channel < 32; ++channel) {
                    const auto channel_bit =
                        std::uint32_t{1} << channel;
                    if ((config.channel_bitmap & channel_bit) == 0) {
                        continue;
                    }
                    if (channel >= channel_groups.size()) {
                        throw std::invalid_argument(
                            "Click detector user grouping must assign "
                            "every selected channel");
                    }
                    group_bitmaps[channel_groups[channel]] |=
                        channel_bit;
                }
                for (const auto& [_, bitmap] : group_bitmaps) {
                    node_config.channel_group_bitmaps.push_back(
                        bitmap);
                }
            }
            else {
                throw std::invalid_argument(
                    "Click detector groupingType must be all, "
                    "singles, or user");
            }

            std::uint32_t grouped_channels = 0;
            for (const auto bitmap :
                 node_config.channel_group_bitmaps) {
                if (bitmap == 0 ||
                    (bitmap & ~config.channel_bitmap) != 0 ||
                    (bitmap & grouped_channels) != 0) {
                    throw std::invalid_argument(
                        "Click detector channel groups must be "
                        "non-empty, disjoint subsets of the selected "
                        "channels");
                }
                grouped_channels |= bitmap;
            }
            if (grouped_channels != config.channel_bitmap) {
                throw std::invalid_argument(
                    "Click detector channel groups must cover every "
                    "selected channel exactly once");
            }

            const auto echo =
                settings.value("echo", Json::object());
            if (!echo.is_object()) {
                throw std::invalid_argument(
                    "Click detector echo settings must be an object");
            }
            node_config.run_echo_online =
                echo.value("runOnline", false);
            node_config.discard_echoes =
                echo.value("discardEchoes", false);
            node_config.echo_max_interval_seconds =
                echo.value("maxIntervalSeconds", 0.1);
            if (!std::isfinite(
                    node_config.echo_max_interval_seconds) ||
                node_config.echo_max_interval_seconds < 0.0) {
                throw std::invalid_argument(
                    "Click detector echo maxIntervalSeconds must be "
                    "finite and non-negative");
            }

            auto clicks = std::make_shared<DataBlock>(output_descriptor(
                *module,
                "clicks",
                "Detected clicks",
                kClickDataType,
                input_rate,
                config.channel_bitmap,
                {"detections", "waveform", "overlay"}));
            auto noise = std::make_shared<DataBlock>(output_descriptor(
                *module,
                "noise",
                "Noise samples",
                kClickNoiseDataType,
                input_rate,
                config.channel_bitmap,
                {"waveform"}));
            auto background = std::make_shared<DataBlock>(output_descriptor(
                *module,
                "background",
                "Trigger background",
                kClickTriggerBackgroundDataType,
                input_rate,
                config.channel_bitmap,
                {"monitoring"}));
            auto trigger = std::make_shared<DataBlock>(output_descriptor(
                *module,
                "trigger",
                "Trigger function",
                kClickTriggerFunctionDataType,
                input_rate,
                config.channel_bitmap,
                {"monitoring"}));
            prepared.nodes.push_back(std::make_unique<ClickDetectorNode>(
                module->id,
                std::move(node_config),
                input,
                ClickDetectorNodeOutputs{
                    clicks,
                    noise,
                    background,
                    trigger,
                }));
            prepared.blocks.emplace(clicks->descriptor().id, clicks);
            prepared.blocks.emplace(noise->descriptor().id, noise);
            prepared.blocks.emplace(background->descriptor().id, background);
            prepared.blocks.emplace(trigger->descriptor().id, trigger);
        }
        else if (module->type_id == "pamguard.amplifier") {
            const auto portable_settings =
                signal_amplifier_settings_from_json(
                    settings.dump(),
                    1);
            AmplifierNodeConfig config;
            config.channel_gains =
                portable_settings.signed_linear_gains();
            auto descriptor = output_descriptor(
                *module,
                "output",
                "Amplified audio",
                kRawAudioDataType,
                input_rate,
                input_channels,
                {"sampled"});
            const auto& input_calibration =
                input->descriptor().calibration_db_offset_by_channel;
            if (!input_calibration.empty()) {
                descriptor.calibration_db_offset_by_channel =
                    input_calibration;
                for (std::size_t channel = 0;
                     channel < descriptor
                         .calibration_db_offset_by_channel.size();
                     ++channel) {
                    const double gain = config.channel_gains.empty()
                        ? 1.0
                        : config.channel_gains.at(channel);
                    if (!std::isfinite(gain) || gain == 0.0) {
                        throw std::invalid_argument(
                            "A calibrated amplifier requires finite, "
                            "non-zero channel gains");
                    }
                    descriptor.calibration_db_offset_by_channel[channel] -=
                        20.0 * std::log10(std::abs(gain));
                }
            }
            auto output =
                std::make_shared<DataBlock>(std::move(descriptor));
            prepared.nodes.push_back(std::make_unique<AmplifierNode>(
                module->id,
                std::move(config),
                input,
                output));
            prepared.blocks.emplace(output->descriptor().id, output);
        }
        else if (module->type_id == "pamguard.patch-panel") {
            const auto portable_settings =
                patch_panel_settings_from_json(
                    settings.dump(),
                    1);
            PatchPanelNodeConfig config;
            config.patches =
                portable_settings.coefficient_matrix();
            std::uint32_t output_bitmap = 0;
            for (std::size_t output_channel = 0;
                 output_channel < kPamguardSignalChannelLimit;
                 ++output_channel) {
                for (std::size_t input_channel = 0;
                     input_channel < kPamguardSignalChannelLimit;
                     ++input_channel) {
                    const auto input_bit =
                        std::uint32_t{1} << input_channel;
                    if ((input_channels & input_bit) != 0 &&
                        config.patches[input_channel][output_channel] !=
                            0.0) {
                        output_bitmap |= std::uint32_t{1} << output_channel;
                        break;
                    }
                }
            }
            auto output = std::make_shared<DataBlock>(output_descriptor(
                *module,
                "output",
                "Patched audio",
                kRawAudioDataType,
                input_rate,
                output_bitmap,
                {"sampled"}));
            const auto& input_calibration =
                input->descriptor().calibration_db_offset_by_channel;
            const auto output_channels =
                channel_count_from_bitmap(output_bitmap);
            if (!input_calibration.empty() &&
                output_channels != 0) {
                std::vector<double> output_calibration(
                    output_channels,
                    0.0);
                bool all_active_outputs_calibrated = true;
                for (std::size_t output_channel = 0;
                     output_channel < output_channels;
                     ++output_channel) {
                    const auto output_bit =
                        std::uint32_t{1} << output_channel;
                    if ((output_bitmap & output_bit) == 0) {
                        continue;
                    }
                    bool found_first_input = false;
                    for (std::size_t input_channel = 0;
                         input_channel < kPamguardSignalChannelLimit;
                         ++input_channel) {
                        const auto input_bit =
                            std::uint32_t{1} << input_channel;
                        if ((input_channels & input_bit) == 0) {
                            continue;
                        }
                        const double gain =
                            config.patches[input_channel][output_channel];
                        if (gain == 0.0) {
                            continue;
                        }
                        if (input_channel >= input_calibration.size() ||
                            !std::isfinite(gain)) {
                            all_active_outputs_calibrated = false;
                            break;
                        }
                        output_calibration[output_channel] =
                            input_calibration[input_channel] -
                            20.0 * std::log10(std::abs(gain));
                        found_first_input = true;
                        break;
                    }
                    if (!all_active_outputs_calibrated ||
                        !found_first_input) {
                        all_active_outputs_calibrated = false;
                        break;
                    }
                }
                if (all_active_outputs_calibrated) {
                    output->configure_calibration(
                        std::move(output_calibration));
                }
            }
            prepared.nodes.push_back(std::make_unique<PatchPanelNode>(
                module->id,
                std::move(config),
                input,
                output));
            prepared.blocks.emplace(output->descriptor().id, output);
        }
        else if (module->type_id == "pamguard.filter") {
            const auto decoded =
                standalone_filter_settings_from_json(
                    settings.dump(),
                    1);
            FilterNodeConfig config;
            config.channel_bitmap = decoded.channel_bitmap;
            config.channel_bitmap = intersect_selected_channels(
                config.channel_bitmap,
                input_channels,
                module->id);
            config.filter = decoded.filter;
            auto output = std::make_shared<DataBlock>(output_descriptor(
                *module,
                "output",
                "Filtered audio",
                kRawAudioDataType,
                input_rate,
                input_channels & config.channel_bitmap,
                {"sampled"}));
            prepared.nodes.push_back(std::make_unique<FilterNode>(
                module->id,
                std::move(config),
                input,
                output));
            prepared.blocks.emplace(output->descriptor().id, output);
        }
        else if (module->type_id == "pamguard.decimator") {
            const auto decoded =
                decimator_settings_from_json(
                    settings.dump(),
                    1);
            DecimatorNodeConfig config;
            config.output_sample_rate_hz =
                decoded.output_sample_rate_hz;
            config.filter = decoded.filter;
            config.interpolation = decoded.interpolation;
            config.channel_bitmap = decoded.channel_bitmap;
            config.channel_bitmap = intersect_selected_channels(
                config.channel_bitmap,
                input_channels,
                module->id);
            auto output = std::make_shared<DataBlock>(output_descriptor(
                *module,
                "output",
                "Decimated audio",
                kRawAudioDataType,
                config.output_sample_rate_hz,
                input_channels & config.channel_bitmap,
                {"sampled"}));
            prepared.nodes.push_back(std::make_unique<DecimatorNode>(
                module->id,
                std::move(config),
                input,
                output));
            prepared.blocks.emplace(output->descriptor().id, output);
        }
        else if (module->type_id == "pamguard.fft") {
            FftConfig config;
            config.fft_length = settings.at("fftLength").get<std::size_t>();
            config.fft_hop = settings.at("fftHop").get<std::size_t>();
            config.channels =
                settings.value("channels", std::vector<std::size_t>{0});
            const auto selected_channels = selected_channel_bitmap(
                config.channels,
                input_channels,
                module->id);
            config.window_type = window_type_from_json(
                settings.value("windowType", Json("Hann")));
            config.click_removal =
                settings.value("clickRemoval", config.click_removal);
            config.click_threshold =
                settings.value("clickThreshold", config.click_threshold);
            config.click_power =
                settings.value("clickPower", config.click_power);
            auto descriptor = output_descriptor(
                *module,
                "fft",
                "FFT frames",
                kFftDataType,
                input_rate,
                selected_channels,
                {"frequency-domain"});
            descriptor.minimum_frequency_hz = 0.0;
            descriptor.maximum_frequency_hz = input_rate / 2.0;
            descriptor.fft_length = config.fft_length;
            descriptor.fft_hop = config.fft_hop;
            auto output =
                std::make_shared<DataBlock>(std::move(descriptor));
            prepared.nodes.push_back(std::make_unique<FftNode>(
                module->id,
                std::move(config),
                input,
                output));
            prepared.blocks.emplace(output->descriptor().id, output);
        }
        else {
            throw std::invalid_argument(
                "No runtime factory is registered for module type " + module->type_id);
        }
    }
    // Propagate and validate clock/sample domains after every output block
    // exists. A multi-input process may only combine streams from one clock
    // and one declared sample rate (inputs without either declaration are
    // ignored for that dimension).
    for (const auto* module : topological_modules(document)) {
        if (!module->enabled) {
            continue;
        }
        std::set<std::string> clock_domains;
        std::optional<double> sample_rate;
        std::optional<std::vector<double>> calibration;
        bool calibration_conflict = false;
        std::optional<double> volts_peak_to_peak;
        bool voltage_scale_conflict = false;
        for (const auto& connection : document.connections) {
            if (connection.target.module_id != module->id) {
                continue;
            }
            const auto found = prepared.blocks.find(block_id(
                connection.source.module_id,
                connection.source.port_id));
            if (found == prepared.blocks.end()) {
                continue;
            }
            const auto& source = found->second->descriptor();
            if (!source.clock_domain_id.empty()) {
                clock_domains.insert(source.clock_domain_id);
            }
            if (source.sample_rate_hz > 0.0) {
                if (sample_rate &&
                    std::abs(*sample_rate - source.sample_rate_hz) > 1e-9) {
                    throw std::invalid_argument(
                        "Module " + module->id +
                        " combines inputs with incompatible sample rates");
                }
                sample_rate = source.sample_rate_hz;
            }
            if (!source.calibration_db_offset_by_channel.empty()) {
                if (calibration &&
                    *calibration !=
                        source.calibration_db_offset_by_channel) {
                    calibration_conflict = true;
                }
                else {
                    calibration =
                        source.calibration_db_offset_by_channel;
                }
            }
            if (source.volts_peak_to_peak) {
                if (volts_peak_to_peak &&
                    std::abs(
                        *volts_peak_to_peak -
                        *source.volts_peak_to_peak) > 1e-12) {
                    voltage_scale_conflict = true;
                }
                else {
                    volts_peak_to_peak =
                        source.volts_peak_to_peak;
                }
            }
        }
        if (clock_domains.size() > 1) {
            throw std::invalid_argument(
                "Module " + module->id +
                " combines inputs from incompatible clock domains");
        }
        if (clock_domains.empty()) {
            continue;
        }
        for (const auto& [_, block] : prepared.blocks) {
            if (block->descriptor().producer_module_id != module->id) {
                continue;
            }
            if (block->descriptor().clock_domain_id.empty()) {
                block->configure_clock_domain(
                    *clock_domains.begin());
            }
            if (calibration && !calibration_conflict &&
                module->type_id != "pamguard.amplifier" &&
                module->type_id != "pamguard.patch-panel" &&
                block->descriptor()
                    .calibration_db_offset_by_channel.empty()) {
                block->configure_calibration(*calibration);
            }
            if (volts_peak_to_peak &&
                !voltage_scale_conflict &&
                !block->descriptor().volts_peak_to_peak) {
                block->configure_voltage_scale(
                    *volts_peak_to_peak);
            }
        }
    }
    for (const auto& node : prepared.nodes) {
        node->prepare();
    }
    return prepared;
}

void ModuleRuntime::configure(const ModuleGraphDocument& document) {
    auto prepared = build(document);
    std::unique_lock lock(mutex_);
    if (running_) {
        throw std::logic_error("Module runtime must be stopped before reconfiguration");
    }
    runtime_ = std::move(prepared);
}

void ModuleRuntime::swap_stopped(ModuleRuntime& other) {
    if (this == &other) {
        return;
    }
    std::scoped_lock lock(mutex_, other.mutex_);
    if (running_ || other.running_) {
        throw std::logic_error(
            "Both module runtimes must be stopped before exchange");
    }
    using std::swap;
    swap(runtime_, other.runtime_);
}

void ModuleRuntime::start() {
    std::unique_lock lock(mutex_);
    if (running_) {
        return;
    }
    std::size_t started = 0;
    try {
        for (const auto& node : runtime_.nodes) {
            node->start();
            ++started;
        }
        running_ = true;
    }
    catch (...) {
        while (started > 0) {
            --started;
            try {
                runtime_.nodes[started]->stop();
            }
            catch (...) {
            }
        }
        running_ = false;
        throw;
    }
}

void ModuleRuntime::stop() {
    std::unique_lock lock(mutex_);
    if (!running_) {
        return;
    }

    std::vector<std::string> errors;
    const auto attempt =
        [&](const char* phase, ModuleNode& node, const auto& operation) {
            try {
                operation();
            }
            catch (const std::exception& error) {
                errors.push_back(
                    std::string(phase) + " module '" +
                    node.instance_id() + "': " + error.what());
            }
            catch (...) {
                errors.push_back(
                    std::string(phase) + " module '" +
                    node.instance_id() + "': unknown error");
            }
        };

    // Refuse new acquisition input before any buffered processor publishes its
    // final units. The service quiesces external capture processes before
    // entering this runtime lifecycle operation.
    std::unordered_set<ModuleNode*> acquisition_nodes;
    acquisition_nodes.reserve(runtime_.sources.size());
    for (const auto& [_, source] : runtime_.sources) {
        acquisition_nodes.insert(source);
    }
    for (const auto& node : runtime_.nodes) {
        if (acquisition_nodes.contains(node.get())) {
            attempt("quiesce", *node, [&] { node->stop(); });
        }
    }

    // Nodes are stored in dependency order. Flushing in that same order keeps
    // downstream subscriptions alive while upstream final units (trains,
    // contours, clips, recordings, and interval summaries) propagate.
    for (const auto& node : runtime_.nodes) {
        attempt("flush", *node, [&] { node->flush(); });
    }

    // Once final units have propagated, tear subscriptions down from sinks
    // towards sources.
    for (auto node = runtime_.nodes.rbegin();
         node != runtime_.nodes.rend();
         ++node) {
        attempt("stop", **node, [&] { (*node)->stop(); });
    }

    if (errors.empty()) {
        running_ = false;
        return;
    }

    // A partial lifecycle failure must not be advertised as clean idle. A
    // subsequent explicit stop retries every phase and may recover.
    std::string message =
        "Module runtime stop incomplete; runtime remains non-idle";
    for (const auto& error : errors) {
        message += "; " + error;
    }
    throw std::runtime_error(message);
}

void ModuleRuntime::flush() {
    std::unique_lock lock(mutex_);
    std::vector<std::string> errors;
    for (const auto& node : runtime_.nodes) {
        try {
            node->flush();
        }
        catch (const std::exception& error) {
            errors.push_back(
                "flush module '" + node->instance_id() +
                "': " + error.what());
        }
        catch (...) {
            errors.push_back(
                "flush module '" + node->instance_id() +
                "': unknown error");
        }
    }
    if (!errors.empty()) {
        std::string message = "Module runtime flush incomplete";
        for (const auto& error : errors) {
            message += "; " + error;
        }
        throw std::runtime_error(message);
    }
}

void ModuleRuntime::reset() {
    std::unique_lock lock(mutex_);
    if (running_) {
        throw std::logic_error("Module runtime must be stopped before reset");
    }
    for (const auto& node : runtime_.nodes) {
        node->reset();
        node->prepare();
    }
}

bool ModuleRuntime::running() const {
    std::shared_lock lock(mutex_);
    return running_;
}

std::uint64_t ModuleRuntime::revision() const {
    std::shared_lock lock(mutex_);
    return runtime_.revision;
}

std::vector<DataBlockDescriptor> ModuleRuntime::data_blocks() const {
    std::shared_lock lock(mutex_);
    std::vector<DataBlockDescriptor> result;
    result.reserve(runtime_.blocks.size());
    for (const auto& [_, block] : runtime_.blocks) {
        result.push_back(block->descriptor());
    }
    std::sort(
        result.begin(),
        result.end(),
        [](const auto& left, const auto& right) { return left.id < right.id; });
    return result;
}

std::vector<ModuleRuntimeStatus> ModuleRuntime::module_statuses() const {
    std::shared_lock lock(mutex_);
    std::vector<ModuleRuntimeStatus> result;
    result.reserve(runtime_.nodes.size());
    for (const auto& node : runtime_.nodes) {
        result.push_back({
            node->instance_id(),
            node->state(),
        });
    }
    std::sort(
        result.begin(),
        result.end(),
        [](const auto& left, const auto& right) {
            return left.instance_id < right.instance_id;
        });
    return result;
}

std::shared_ptr<DataBlock> ModuleRuntime::find_block(const DataBlockId& id) const {
    std::shared_lock lock(mutex_);
    const auto found = runtime_.blocks.find(id);
    return found == runtime_.blocks.end() ? nullptr : found->second;
}

void ModuleRuntime::ingest(
    const ModuleInstanceId& acquisition_module_id,
    AudioChunk chunk) {
    std::shared_lock lock(mutex_);
    if (!running_) {
        throw std::logic_error("Module runtime is not running");
    }
    const auto found = runtime_.sources.find(acquisition_module_id);
    if (found == runtime_.sources.end()) {
        throw std::invalid_argument(
            "Unknown acquisition module instance: " + acquisition_module_id);
    }
    found->second->ingest(std::move(chunk));
}

void ModuleRuntime::publish_operator_event(
    const ModuleInstanceId& module_id,
    GraphOperatorEvent event,
    std::int64_t time_unix_ms,
    std::int64_t start_sample) {
    std::shared_lock lock(mutex_);
    if (!running_) {
        throw std::logic_error("Module runtime is not running");
    }
    const auto found = runtime_.operator_inputs.find(module_id);
    if (found == runtime_.operator_inputs.end()) {
        throw std::invalid_argument(
            "Unknown operator-input module instance: " +
            module_id);
    }
    found->second->publish(
        std::move(event),
        time_unix_ms,
        start_sample);
}

SoundRecorderCommandResult
ModuleRuntime::set_sound_recorder_transport(
    const ModuleInstanceId& module_id,
    SoundRecorderTransportState state) {
    std::shared_lock lock(mutex_);
    const auto found =
        runtime_.sound_recorders.find(module_id);
    if (found == runtime_.sound_recorders.end()) {
        throw std::invalid_argument(
            "Unknown sound-recorder module instance: " +
            module_id);
    }
    return found->second->set_transport_state(state);
}

SoundRecorderNodeStatus
ModuleRuntime::sound_recorder_status(
    const ModuleInstanceId& module_id) const {
    std::shared_lock lock(mutex_);
    const auto found =
        runtime_.sound_recorders.find(module_id);
    if (found == runtime_.sound_recorders.end()) {
        throw std::invalid_argument(
            "Unknown sound-recorder module instance: " +
            module_id);
    }
    return found->second->recorder_status();
}

} // namespace pamguard::core
