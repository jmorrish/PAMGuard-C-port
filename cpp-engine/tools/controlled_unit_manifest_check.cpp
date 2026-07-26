#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <json.hpp>

#include "pamguard/core/BuiltInModules.h"
#include "pamguard/core/ModuleGraph.h"

namespace {

using json = nlohmann::json;

struct Bundle {
    std::string kind;
    std::string parity_label;
    std::string java_relationship;
    std::set<std::string> runtime_type_ids;
};

struct InstanceLimit {
    std::size_t minimum = 0;
    std::optional<std::size_t> maximum;

    bool operator==(const InstanceLimit&) const = default;
};

struct Dependency {
    std::string required_data_class;
    std::string default_provider_class;

    bool operator==(const Dependency&) const = default;
};

struct JavaAuthorityTuple {
    std::string registered_name;
    std::string menu_group;
    std::string class_name;
    std::string relationship;

    bool operator==(const JavaAuthorityTuple&) const = default;
};

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string required_string(
    const json& value,
    const char* key,
    const std::string& context) {
    require(
        value.contains(key) && value.at(key).is_string() &&
            !value.at(key).get_ref<const std::string&>().empty(),
        context + " must contain a non-empty string '" + key + "'");
    return value.at(key).get<std::string>();
}

std::vector<std::string> required_string_array(
    const json& value,
    const char* key,
    const std::string& context) {
    require(
        value.contains(key) && value.at(key).is_array() &&
            !value.at(key).empty(),
        context + " must contain a non-empty array '" + key + "'");
    std::vector<std::string> result;
    for (const auto& item : value.at(key)) {
        require(
            item.is_string() && !item.get_ref<const std::string&>().empty(),
            context + "." + key + " must contain non-empty strings");
        result.push_back(item.get<std::string>());
    }
    require(
        std::set<std::string>(result.begin(), result.end()).size() ==
            result.size(),
        context + "." + key + " must not contain duplicates");
    return result;
}

std::vector<std::string> string_array(
    const json& value,
    const char* key,
    const std::string& context,
    bool allow_empty) {
    require(
        value.contains(key) && value.at(key).is_array() &&
            (allow_empty || !value.at(key).empty()),
        context + " must contain " +
            (allow_empty ? "an array '" : "a non-empty array '") +
            key + "'");
    std::vector<std::string> result;
    for (const auto& item : value.at(key)) {
        require(
            item.is_string() && !item.get_ref<const std::string&>().empty(),
            context + "." + key + " must contain non-empty strings");
        result.push_back(item.get<std::string>());
    }
    require(
        std::set<std::string>(result.begin(), result.end()).size() ==
            result.size(),
        context + "." + key + " must not contain duplicates");
    return result;
}

std::size_t required_nonnegative_integer(
    const json& value,
    const char* key,
    const std::string& context) {
    require(
        value.contains(key) && value.at(key).is_number_integer(),
        context + " must contain an integer '" + key + "'");
    const auto number = value.at(key).get<std::int64_t>();
    require(number >= 0, context + "." + key + " must be non-negative");
    return static_cast<std::size_t>(number);
}

std::optional<std::size_t> required_optional_limit(
    const json& value,
    const char* key,
    const std::string& context) {
    require(value.contains(key), context + " must contain '" + key + "'");
    if (value.at(key).is_null()) {
        return {};
    }
    return required_nonnegative_integer(value, key, context);
}

void require_java_source(
    const std::filesystem::path& java_root,
    const std::string& relative_path,
    const std::string& context) {
    const std::filesystem::path path(relative_path);
    require(
        path.is_relative() &&
            std::none_of(
                path.begin(),
                path.end(),
                [](const auto& part) { return part == ".."; }),
        context + " must be a safe Java-root-relative path");
    require(
        std::filesystem::is_regular_file(java_root / path),
        context + " does not exist in the pinned Java tree: " + relative_path);
}

std::string trim_ascii_whitespace(std::string value) {
    const auto not_space = [](unsigned char character) {
        return !std::isspace(character);
    };
    value.erase(
        value.begin(),
        std::find_if(value.begin(), value.end(), not_space));
    value.erase(
        std::find_if(value.rbegin(), value.rend(), not_space).base(),
        value.end());
    return value;
}

std::string first_line(
    const std::filesystem::path& path,
    const std::string& context) {
    std::ifstream input(path);
    require(input.is_open(), "Could not open " + context);
    std::string line;
    require(
        static_cast<bool>(std::getline(input, line)),
        context + " is empty");
    return trim_ascii_whitespace(std::move(line));
}

bool is_git_object_id(const std::string& value) {
    return value.size() == 40 &&
        std::all_of(
            value.begin(),
            value.end(),
            [](unsigned char character) {
                return std::isxdigit(character);
            });
}

std::filesystem::path git_directory(
    const std::filesystem::path& worktree_root) {
    const auto dot_git = worktree_root / ".git";
    if (std::filesystem::is_directory(dot_git)) {
        return dot_git;
    }
    require(
        std::filesystem::is_regular_file(dot_git),
        "Pinned Java authority root must be a Git checkout");
    constexpr std::string_view prefix = "gitdir:";
    const auto pointer = first_line(dot_git, "Java .git pointer");
    require(
        pointer.starts_with(prefix),
        "Java .git pointer has an unsupported format");
    auto path = std::filesystem::path(
        trim_ascii_whitespace(pointer.substr(prefix.size())));
    if (path.is_relative()) {
        path = worktree_root / path;
    }
    require(
        std::filesystem::is_directory(path),
        "Java .git pointer does not resolve to a directory");
    return path;
}

std::string git_head_commit(
    const std::filesystem::path& worktree_root) {
    const auto git = git_directory(worktree_root);
    const auto head = first_line(git / "HEAD", "Java Git HEAD");
    if (is_git_object_id(head)) {
        return head;
    }

    constexpr std::string_view prefix = "ref:";
    require(
        head.starts_with(prefix),
        "Java Git HEAD is neither a commit nor a symbolic ref");
    const auto ref_name =
        trim_ascii_whitespace(head.substr(prefix.size()));
    const std::filesystem::path ref_path(ref_name);
    require(
        ref_path.is_relative() &&
            std::none_of(
                ref_path.begin(),
                ref_path.end(),
                [](const auto& part) { return part == ".."; }),
        "Java Git HEAD contains an unsafe symbolic ref");
    if (std::filesystem::is_regular_file(git / ref_path)) {
        const auto commit =
            first_line(git / ref_path, "Java Git symbolic ref");
        require(
            is_git_object_id(commit),
            "Java Git symbolic ref does not contain a commit");
        return commit;
    }

    std::ifstream packed_refs(git / "packed-refs");
    require(
        packed_refs.is_open(),
        "Java Git symbolic ref is unresolved");
    std::string line;
    while (std::getline(packed_refs, line)) {
        line = trim_ascii_whitespace(std::move(line));
        if (line.empty() || line.front() == '#' ||
            line.front() == '^') {
            continue;
        }
        const auto separator = line.find(' ');
        if (separator == std::string::npos ||
            line.substr(separator + 1) != ref_name) {
            continue;
        }
        const auto commit = line.substr(0, separator);
        require(
            is_git_object_id(commit),
            "Java Git packed ref does not contain a commit");
        return commit;
    }
    throw std::runtime_error(
        "Java Git symbolic ref is absent from packed-refs");
}

std::map<std::string, std::vector<std::string>> ordered_surfaces(
    const json& contract,
    const char* key,
    const std::string& context) {
    require(
        contract.contains(key) && contract.at(key).is_array() &&
            !contract.at(key).empty(),
        context + " must contain a non-empty '" + key + "' array");
    std::map<std::string, std::vector<std::string>> result;
    for (const auto& surface_entry : contract.at(key)) {
        const auto surface = required_string(
            surface_entry,
            "surface",
            context + "." + key);
        const bool has_labels = surface_entry.contains("labels");
        const bool has_items = surface_entry.contains("items");
        require(
            has_labels != has_items,
            context + "." + key + " surface '" + surface +
                "' must contain exactly one of labels or items");
        std::vector<std::string> labels;
        if (has_labels) {
            labels = required_string_array(
                surface_entry,
                "labels",
                context + "." + key + "." + surface);
        }
        else {
            require(
                surface_entry.at("items").is_array() &&
                    !surface_entry.at("items").empty(),
                context + "." + key + "." + surface +
                    ".items must be a non-empty array");
            for (const auto& item : surface_entry.at("items")) {
                labels.push_back(required_string(
                    item,
                    "label",
                    context + "." + key + "." + surface + ".item"));
            }
        }
        require(
            result.emplace(surface, std::move(labels)).second,
            context + "." + key + " contains duplicate surface '" +
                surface + "'");
    }
    return result;
}

json read_document(const std::string& path) {
    std::ifstream input(path);
    require(input.is_open(), "Could not open parity manifest: " + path);
    json document;
    input >> document;
    return document;
}

} // namespace

int main(int argc, char** argv) {
    try {
        require(
            argc == 3,
            "Usage: controlled_unit_manifest_check <manifest.json> <java-source-root>");
        const std::filesystem::path java_root(argv[2]);
        require(
            std::filesystem::is_directory(java_root / "src"),
            "Java source root must contain src/");
        const auto document = read_document(argv[1]);
        require(document.is_object(), "Parity manifest root must be an object");
        require(
            document.value("schemaVersion", 0) == 2,
            "Parity manifest schemaVersion must be 2");
        require(
            document.value("manifestId", std::string{}) ==
                "pamguard-controlled-unit-parity",
            "Parity manifestId is not authoritative");

        const auto& authority = document.at("authority");
        require(
            required_string(authority, "application", "authority") == "PAMGuard",
            "Authority application must be PAMGuard");
        require(
            required_string(authority, "version", "authority") == "2.02.18e",
            "Authority version must remain pinned to PAMGuard 2.02.18e");
        const std::string pinned_java_commit =
            "dca55c81ef6f1498a8a3b926c69e7182afb915ee";
        require(
            required_string(authority, "commit", "authority") ==
                pinned_java_commit,
            "Authority commit does not match the pinned Java source");
        required_string(authority, "repository", "authority");
        const auto registration_source = required_string(
            authority,
            "registrationSource",
            "authority");
        require(
            registration_source == "src/PamModel/PamModel.java",
            "Authority registration source must identify PamModel.java");
        require_java_source(
            java_root,
            registration_source,
            "authority.registrationSource");
        require(
            git_head_commit(java_root) == pinned_java_commit,
            "Java authority checkout HEAD does not match the pinned commit");

        const std::set<std::string> allowed_parity_labels{
            "partial",
            "experimental",
            "internal-foundation",
            "display-foundation",
            "web-extension",
        };
        const auto declared_labels = required_string_array(
            document,
            "parityLabels",
            "manifest");
        require(
            std::set<std::string>(
                declared_labels.begin(),
                declared_labels.end()) == allowed_parity_labels,
            "Manifest parityLabels changed without a schema-version update");

        const std::map<std::string, JavaAuthorityTuple>
            expected_java_authorities{
                {"pamguard.acquisition",
                 {"Sound Acquisition",
                  "Sound Processing",
                  "Acquisition.AcquisitionControl",
                  "direct"}},
                {"pamguard.amplifier",
                 {"Signal Amplifier",
                  "Sound Processing",
                  "amplifier.AmpControl",
                  "direct"}},
                {"pamguard.patch-panel",
                 {"Patch Panel",
                  "Sound Processing",
                  "patchPanel.PatchPanelControl",
                  "direct"}},
                {"pamguard.filter",
                 {"Filters (IIR and FIR)",
                  "Sound Processing",
                  "Filters.FilterControl",
                  "direct"}},
                {"pamguard.decimator",
                 {"Decimator",
                  "Sound Processing",
                  "decimator.DecimatorControl",
                  "direct"}},
                {"pamguard.fft",
                 {"FFT (Spectrogram) Engine",
                  "Sound Processing",
                  "fftManager.PamFFTControl",
                  "direct"}},
                {"pamguard.click-detector",
                 {"Click Detector",
                  "Detectors",
                  "clickDetector.ClickControl",
                  "direct"}},
                {"pamguard.mht-click-train",
                 {"Click Train Detector",
                  "Detectors",
                  "clickTrainDetector.ClickTrainControl",
                  "direct"}},
                {"pamguard.whistles-moans",
                 {"Whistle and Moan Detector",
                  "Detectors",
                  "whistlesAndMoans.WhistleMoanControl",
                  "direct"}},
                {"pamguard.ishmael-energy-sum",
                 {"Ishmael energy sum",
                  "Detectors",
                  "IshmaelDetector.EnergySumControl",
                  "direct"}},
                {"pamguard.ishmael-sgram-corr",
                 {"Ishmael spectrogram correlation",
                  "Detectors",
                  "IshmaelDetector.SgramCorrControl",
                  "direct"}},
                {"pamguard.ishmael-match-filter",
                 {"Ishmael matched filtering",
                  "Detectors",
                  "IshmaelDetector.MatchFiltControl",
                  "direct"}},
                {"pamguard.matched-template-classifier",
                 {"Matched Template Click Classifer",
                  "Classifiers",
                  "matchedTemplateClassifer.MTClassifierControl",
                  "direct"}},
                {"pamguard.fft-noise-monitor",
                 {"Noise Monitor",
                  "Sound Processing",
                  "noiseMonitor.NoiseControl",
                  "direct"}},
                {"pamguard.noise-band-monitor",
                 {"Noise Band Monitor",
                  "Sound Processing",
                  "noiseBandMonitor.NoiseBandControl",
                  "direct"}},
                {"pamguard.ltsa",
                 {"Long Term Spectral Average",
                  "Sound Processing",
                  "ltsa.LtsaControl",
                  "direct"}},
                {"pamguard.sound-recorder",
                 {"Sound recorder",
                  "Sound Processing",
                  "SoundRecorder.RecorderControl",
                  "direct"}},
                {"pamguard.clip-generator",
                 {"Clip generator",
                  "Sound Processing",
                  "clipgenerator.ClipControl",
                  "direct"}},
                {"pamguard.sound-output",
                 {"Sound Output",
                  "Sound Processing",
                  "soundPlayback.PlaybackControl",
                  "direct"}},
                {"pamguard.level-meter",
                 {"Level Meter",
                  "Displays",
                  "levelMeter.LevelMeterControl",
                  "direct"}},
                {"pamguard.alarm-event-counter",
                 {"Alarm",
                  "Utilities",
                  "alarm.AlarmControl",
                  "direct"}},
                {"pamguard.effort-monitor",
                 {"Scroll Effort",
                  "Utilities",
                  "effortmonitor.EffortControl",
                  "direct"}},
                {"pamguard.aural-listening",
                 {"Aural Listening Form",
                  "Utilities",
                  "listening.ListeningControl",
                  "direct"}},
                {"pamguard.user-input",
                 {"User input",
                  "Utilities",
                  "UserInput.UserInputController",
                  "direct"}},
                {"pamguard.user-display",
                 {"User Display",
                  "Displays",
                  "userDisplay.UserDisplayControl",
                  "provider-owner"}},
                {"pamguard.system-diagnostics",
                 {"Backup Manager",
                  "Utilities",
                  "backupmanager.BackupManager",
                  "comparison-target"}},
            };

        require(
            document.contains("bundles") && document.at("bundles").is_array(),
            "Manifest must contain a bundles array");
        std::unordered_map<std::string, Bundle> bundles;
        for (const auto& entry : document.at("bundles")) {
            const auto id = required_string(entry, "bundleId", "bundle");
            const auto context = "bundle '" + id + "'";
            const auto kind = required_string(entry, "kind", context);
            require(
                kind == "controlled-unit" || kind == "system-extension",
                context + " has an invalid kind");
            const auto parity = required_string(entry, "parityLabel", context);
            require(
                allowed_parity_labels.contains(parity),
                context + " has an unknown parity label");

            require(
                entry.contains("javaAuthority") &&
                    entry.at("javaAuthority").is_object(),
                context + " must contain javaAuthority");
            const auto& java = entry.at("javaAuthority");
            const JavaAuthorityTuple java_authority{
                required_string(
                    java,
                    "registeredName",
                    context + ".javaAuthority"),
                required_string(
                    java,
                    "menuGroup",
                    context + ".javaAuthority"),
                required_string(
                    java,
                    "className",
                    context + ".javaAuthority"),
                required_string(
                    java,
                    "relationship",
                    context + ".javaAuthority"),
            };
            const auto expected_authority =
                expected_java_authorities.find(id);
            require(
                expected_authority != expected_java_authorities.end(),
                context +
                    " has no pinned Java authority tuple");
            require(
                java_authority == expected_authority->second,
                context +
                    " Java name/group/class/relationship differs from the pinned tuple");
            const auto relationship = required_string(
                java,
                "relationship",
                context + ".javaAuthority");
            require(
                relationship == "direct" ||
                    relationship == "provider-owner" ||
                    relationship == "comparison-target",
                context + " has an invalid Java authority relationship");

            require(
                entry.contains("runtimeRecipe") &&
                    entry.at("runtimeRecipe").is_object(),
                context + " must contain runtimeRecipe");
            const auto& recipe = entry.at("runtimeRecipe");
            require(
                recipe.value("version", 0) == 1,
                context + " runtime recipe version must be 1");
            const auto runtime_ids = required_string_array(
                recipe,
                "runtimeTypeIds",
                context + ".runtimeRecipe");
            require(
                bundles.emplace(
                    id,
                    Bundle{
                        kind,
                        parity,
                        relationship,
                        {runtime_ids.begin(), runtime_ids.end()},
                    }).second,
                "Duplicate bundleId: " + id);
        }
        require(
            bundles.size() == 26,
            "Expected 26 mapped controlled-unit/extension bundle owners");

        const std::vector<std::string> all_run_modes{
            "normal",
            "mixed",
            "viewer",
        };
        const std::map<std::string, std::vector<Dependency>>
            expected_dependencies{
                {"pamguard.amplifier",
                 {{"PamDetection.RawDataUnit",
                   "Acquisition.AcquisitionControl"}}},
                {"pamguard.patch-panel",
                 {{"PamDetection.RawDataUnit",
                   "Acquisition.AcquisitionControl"}}},
                {"pamguard.filter",
                 {{"PamDetection.RawDataUnit",
                   "Acquisition.AcquisitionControl"}}},
                {"pamguard.decimator",
                 {{"PamDetection.RawDataUnit",
                   "Acquisition.AcquisitionControl"}}},
                {"pamguard.fft",
                 {{"PamDetection.RawDataUnit",
                   "Acquisition.AcquisitionControl"}}},
                {"pamguard.click-detector",
                 {{"PamDetection.RawDataUnit",
                   "Acquisition.AcquisitionControl"}}},
                {"pamguard.mht-click-train",
                 {{"PamDetection.RawDataUnit",
                   "clickDetector.ClickControl"}}},
                {"pamguard.whistles-moans",
                 {{"fftManager.FFTDataUnit",
                   "fftManager.PamFFTControl"}}},
                {"pamguard.ishmael-energy-sum",
                 {{"fftManager.FFTDataUnit",
                   "fftManager.PamFFTControl"}}},
                {"pamguard.ishmael-sgram-corr",
                 {{"fftManager.FFTDataUnit",
                   "fftManager.PamFFTControl"}}},
                {"pamguard.ishmael-match-filter",
                 {{"PamDetection.RawDataUnit",
                   "Acquisition.AcquisitionControl"}}},
                {"pamguard.matched-template-classifier",
                 {{"clickDetector.ClickDetection",
                   "clickDetector.ClickControl"}}},
                {"pamguard.fft-noise-monitor",
                 {{"fftManager.FFTDataUnit",
                   "fftManager.PamFFTControl"}}},
                {"pamguard.noise-band-monitor",
                 {{"PamDetection.RawDataUnit",
                   "Acquisition.AcquisitionControl"}}},
                {"pamguard.ltsa",
                 {{"PamDetection.RawDataUnit",
                   "fftManager.PamFFTControl"}}},
                {"pamguard.sound-recorder",
                 {{"PamDetection.RawDataUnit",
                   "Acquisition.AcquisitionControl"}}},
                {"pamguard.clip-generator",
                 {{"PamDetection.RawDataUnit",
                   "Acquisition.AcquisitionControl"}}},
                {"pamguard.level-meter",
                 {{"PamDetection.RawDataUnit",
                   "Acquisition.AcquisitionControl"}}},
            };
        const std::map<std::string, std::vector<std::string>>
            expected_settings_classes{
                {"pamguard.acquisition",
                 {"Acquisition.AcquisitionParameters"}},
                {"pamguard.amplifier", {"amplifier.AmpParameters"}},
                {"pamguard.patch-panel",
                 {"patchPanel.PatchPanelParameters"}},
                {"pamguard.filter",
                 {"Filters.FilterParameters_2",
                  "Filters.FilterParams"}},
                {"pamguard.decimator",
                 {"decimator.DecimatorParams",
                  "Filters.FilterParams"}},
                {"pamguard.fft",
                 {"fftManager.FFTParameters",
                  "spectrogramNoiseReduction.SpectrogramNoiseSettings"}},
                {"pamguard.click-detector",
                 {"clickDetector.ClickParameters",
                  "clickDetector.ClickClassifiers.ClickClassifierManager",
                  "clickDetector.BasicClickIdParameters",
                  "clickDetector.ClickTypeParams",
                  "clickDetector.ClickClassifiers.ClickTypeCommonParams",
                  "clickDetector.ClickClassifiers.basicSweep.SweepClassifierParameters",
                  "clickDetector.ClickClassifiers.basicSweep.SweepClassifierSet",
                  "clickDetector.clicktrains.ClickTrainIdParams",
                  "clickDetector.localisation.ClickLocParams",
                  "Localiser.DelayMeasurementParams",
                  "angleVetoes.AngleVetoParameters",
                  "clickDetector.echoDetection.SimpleEchoParams",
                  "clickDetector.BTDisplayParameters"}},
                {"pamguard.mht-click-train",
                 {"clickTrainDetector.ClickTrainParams",
                  "clickTrainDetector.clickTrainAlgorithms.mht.MHTParams",
                  "clickTrainDetector.clickTrainAlgorithms.mht.MHTKernelParams",
                  "clickTrainDetector.clickTrainAlgorithms.mht.MHTChi2Params",
                  "clickTrainDetector.clickTrainAlgorithms.mht.StandardMHTChi2Params",
                  "clickTrainDetector.clickTrainAlgorithms.mht.electricalNoiseFilter.SimpleElectricalNoiseParams",
                  "clickTrainDetector.clickTrainAlgorithms.mht.mhtvar.SimpleChi2VarParams",
                  "clickTrainDetector.clickTrainAlgorithms.mht.mhtvar.IDIChi2Params",
                  "clickTrainDetector.clickTrainAlgorithms.mht.mhtvar.AmplitudeChi2Params",
                  "clickTrainDetector.clickTrainAlgorithms.mht.mhtvar.BearingChi2VarParams",
                  "clickTrainDetector.clickTrainAlgorithms.mht.mhtvar.CorrelationChi2Params",
                  "clickTrainDetector.clickTrainAlgorithms.mht.mhtvar.TimeDelayParams",
                  "clickTrainDetector.classification.simplechi2classifier.Chi2ThresholdParams",
                  "clickTrainDetector.classification.idiClassifier.IDIClassifierParams",
                  "clickTrainDetector.classification.bearingClassifier.BearingClassifierParams",
                  "clickTrainDetector.classification.templateClassifier.TemplateClassifierParams",
                  "clickTrainDetector.classification.templateClassifier.SpectrumTemplateParams",
                  "clickTrainDetector.localisation.CTLocParams",
                  "clickDetector.alarm.ClickAlarmParameters"}},
                {"pamguard.whistles-moans",
                 {"whistlesAndMoans.WhistleToneParameters",
                  "PamView.GroupedSourceParameters",
                  "spectrogramNoiseReduction.SpectrogramNoiseSettings",
                  "spectrogramNoiseReduction.medianFilter.MedianFilterParams",
                  "spectrogramNoiseReduction.averageSubtraction.AverageSubtractionParameters",
                  "spectrogramNoiseReduction.threshold.ThresholdParams"}},
                {"pamguard.ishmael-energy-sum",
                 {"IshmaelDetector.EnergySumParams",
                  "IshmaelDetector.IshDetParams",
                  "PamView.GroupedSourceParameters"}},
                {"pamguard.ishmael-sgram-corr",
                 {"IshmaelDetector.SgramCorrParams",
                  "IshmaelDetector.IshDetParams",
                  "PamView.GroupedSourceParameters"}},
                {"pamguard.ishmael-match-filter",
                 {"IshmaelDetector.MatchFiltParams",
                  "IshmaelDetector.IshDetParams",
                  "PamView.GroupedSourceParameters"}},
                {"pamguard.matched-template-classifier",
                 {"matchedTemplateClassifer.MatchedTemplateParams",
                  "matchedTemplateClassifer.MTClassifier",
                  "matchedTemplateClassifer.MatchTemplate"}},
                {"pamguard.fft-noise-monitor",
                 {"noiseMonitor.NoiseSettings",
                  "noiseMonitor.NoiseMeasurementBand",
                  "noiseBandMonitor.BandType"}},
                {"pamguard.noise-band-monitor",
                 {"noiseBandMonitor.NoiseBandSettings",
                  "noiseBandMonitor.BandType",
                  "Filters.FilterType"}},
                {"pamguard.ltsa", {"ltsa.LtsaParameters"}},
                {"pamguard.sound-recorder",
                 {"SoundRecorder.RecorderSettings",
                  "SoundRecorder.trigger.RecorderTriggerData"}},
                {"pamguard.clip-generator",
                 {"clipgenerator.ClipSettings",
                  "clipgenerator.ClipGenSetting"}},
                {"pamguard.sound-output",
                 {"soundPlayback.PlaybackParameters"}},
                {"pamguard.level-meter",
                 {"levelMeter.LevelMeterParams",
                  "levelMeter.LevelMeterSidePanel"}},
                {"pamguard.alarm-event-counter",
                 {"alarm.AlarmParameters"}},
                {"pamguard.effort-monitor",
                 {"effortmonitor.EffortParams"}},
                {"pamguard.aural-listening",
                 {"listening.ListeningParameters",
                  "listening.SpeciesItem"}},
                {"pamguard.user-input",
                 {"UserInput.UserInputController"}},
                {"pamguard.user-display",
                 {"userDisplay.UserDisplayParameters",
                  "Spectrogram.SpectrogramParameters"}},
            };
        const std::set<std::string>
            fixture_validated_settings_contracts{
                "pamguard.ishmael-energy-sum",
                "pamguard.ishmael-sgram-corr",
                "pamguard.ishmael-match-filter",
                "pamguard.matched-template-classifier",
                "pamguard.sound-recorder",
                "pamguard.clip-generator",
            };
        const std::map<std::string, std::string>
            source_validated_settings_contracts{
                {
                    "pamguard.aural-listening",
                    "java-source-validated-operational-settings",
                },
                {
                    "pamguard.user-input",
                    "java-source-validated-no-settings",
                },
            };

        require(
            document.contains("controlledUnitContracts") &&
                document.at("controlledUnitContracts").is_array(),
            "Manifest must contain a controlledUnitContracts array");
        std::set<std::string> controlled_bundle_ids;
        for (const auto& [bundle_id, bundle] : bundles) {
            if (bundle.kind == "controlled-unit") {
                controlled_bundle_ids.insert(bundle_id);
            }
        }
        std::set<std::string> contracted_bundle_ids;
        std::unordered_map<
            std::string,
            std::map<std::string, InstanceLimit>>
            limits_by_bundle;
        for (const auto& entry : document.at("controlledUnitContracts")) {
            const auto bundle_id = required_string(
                entry,
                "bundleId",
                "controlled-unit contract");
            const auto context =
                "controlled-unit contract '" + bundle_id + "'";
            require(
                controlled_bundle_ids.contains(bundle_id),
                context + " does not refer to an operator controlled unit");
            require(
                contracted_bundle_ids.insert(bundle_id).second,
                "Duplicate " + context);

            require(
                entry.contains("instanceRules") &&
                    entry.at("instanceRules").is_object(),
                context + " must contain instanceRules");
            const auto& rules = entry.at("instanceRules");
            require(
                required_string_array(
                    rules,
                    "allowedModes",
                    context + ".instanceRules") == all_run_modes,
                context +
                    " must explicitly cover normal, mixed, and viewer modes");
            const InstanceLimit base_limit{
                required_nonnegative_integer(
                    rules,
                    "minimumInstances",
                    context + ".instanceRules"),
                required_optional_limit(
                    rules,
                    "maximumInstances",
                    context + ".instanceRules"),
            };
            require(
                !base_limit.maximum ||
                    base_limit.minimum <= *base_limit.maximum,
                context + " has a minimum above its maximum");
            const auto authority_sources = required_string_array(
                rules,
                "authoritySources",
                context + ".instanceRules");
            for (const auto& source : authority_sources) {
                require_java_source(
                    java_root,
                    source,
                    context + ".instanceRules.authoritySources");
            }
            require(
                rules.contains("modeOverrides") &&
                    rules.at("modeOverrides").is_array(),
                context + ".instanceRules must contain modeOverrides");
            std::map<std::string, InstanceLimit> resolved_limits;
            for (const auto& mode : all_run_modes) {
                resolved_limits.emplace(mode, base_limit);
            }
            std::set<std::string> overridden_modes;
            for (const auto& override_value :
                 rules.at("modeOverrides")) {
                const auto mode = required_string(
                    override_value,
                    "mode",
                    context + ".instanceRules.modeOverride");
                require(
                    resolved_limits.contains(mode) &&
                        overridden_modes.insert(mode).second,
                    context + " has an invalid or duplicate mode override");
                const InstanceLimit override_limit{
                    required_nonnegative_integer(
                        override_value,
                        "minimumInstances",
                        context + ".instanceRules.modeOverride"),
                    required_optional_limit(
                        override_value,
                        "maximumInstances",
                        context + ".instanceRules.modeOverride"),
                };
                require(
                    !override_limit.maximum ||
                        override_limit.minimum <= *override_limit.maximum,
                    context + " mode override has minimum above maximum");
                resolved_limits.at(mode) = override_limit;
            }
            limits_by_bundle.emplace(bundle_id, resolved_limits);

            require(
                entry.contains("dependencies") &&
                    entry.at("dependencies").is_array(),
                context + " must contain a dependencies array");
            std::vector<Dependency> dependencies;
            for (const auto& dependency_value :
                 entry.at("dependencies")) {
                dependencies.push_back({
                    required_string(
                        dependency_value,
                        "requiredDataClass",
                        context + ".dependency"),
                    required_string(
                        dependency_value,
                        "defaultProviderClass",
                        context + ".dependency"),
                });
                const auto source = required_string(
                    dependency_value,
                    "authoritySource",
                    context + ".dependency");
                require_java_source(
                    java_root,
                    source,
                    context + ".dependency.authoritySource");
            }
            const auto expected_dependency =
                expected_dependencies.find(bundle_id);
            const std::vector<Dependency> no_dependencies;
            require(
                dependencies ==
                    (expected_dependency == expected_dependencies.end()
                         ? no_dependencies
                         : expected_dependency->second),
                context +
                    " dependencies differ from pinned PamModel registration");

            require(
                entry.contains("configurationAuthority") &&
                    entry.at("configurationAuthority").is_object(),
                context + " must contain configurationAuthority");
            const auto& configuration =
                entry.at("configurationAuthority");
            const auto status = required_string(
                configuration,
                "status",
                context + ".configurationAuthority");
            require(
                status == "discovered" ||
                    status == "operator-form-only",
                context + " has an invalid configuration authority status");
            const auto source_validated =
                source_validated_settings_contracts.find(
                    bundle_id);
            const auto expected_settings_claim =
                fixture_validated_settings_contracts.contains(
                    bundle_id)
                ? std::string("java-fixture-validated")
                : source_validated !=
                        source_validated_settings_contracts.end()
                    ? source_validated->second
                    : std::string("not-claimed");
            require(
                required_string(
                    configuration,
                    "cppParityClaim",
                    context + ".configurationAuthority") ==
                    expected_settings_claim,
                context +
                    " settings parity claim differs from its pinned "
                    "fixture-evidence status");
            const auto settings_classes = string_array(
                configuration,
                "settingsClasses",
                context + ".configurationAuthority",
                true);
            require(
                settings_classes ==
                    expected_settings_classes.at(bundle_id),
                context +
                    " settings classes differ from the pinned Java mapping");
            require(
                (status == "operator-form-only") ==
                    settings_classes.empty(),
                context +
                    " operator-form-only status and settings classes disagree");
            const auto sources = required_string_array(
                configuration,
                "sources",
                context + ".configurationAuthority");
            for (const auto& source : sources) {
                require_java_source(
                    java_root,
                    source,
                    context + ".configurationAuthority.sources");
            }
        }
        require(
            contracted_bundle_ids == controlled_bundle_ids &&
                contracted_bundle_ids.size() == 25,
            "Every operator controlled unit must have exactly one contract");

        for (const auto& bundle_id : controlled_bundle_ids) {
            for (const auto& mode : all_run_modes) {
                InstanceLimit expected_limit{0, {}};
                if (bundle_id == "pamguard.effort-monitor" ||
                    bundle_id == "pamguard.user-input") {
                    expected_limit.maximum = 1;
                }
                if (bundle_id == "pamguard.sound-output" &&
                    mode == "viewer") {
                    expected_limit = {1, 1};
                }
                require(
                    limits_by_bundle.at(bundle_id).at(mode) ==
                        expected_limit,
                    "Instance limit differs from pinned PamModel for " +
                        bundle_id + " in " + mode + " mode");
            }
        }

        using OrderedSurfaces =
            std::map<std::string, std::vector<std::string>>;
        using Defaults = std::map<std::string, json>;
        const std::map<std::string, OrderedSurfaces>
            expected_core_sections{
                {"pamguard.acquisition",
                 {{"settings.normal",
                   {"Data Source Type",
                    "Device-specific settings",
                    "Sampling",
                    "Channel mapping",
                    "Calibration"}},
                  {"settings.viewer-tabs",
                   {"Offline Files", "DAQ Settings"}},
                  {"settings.smru-tabs",
                   {"DAQ Settings", "GPS Timing"}}}},
                {"pamguard.amplifier",
                 {{"settings.dialog",
                   {"Raw Data input",
                    "Channel Gains",
                    "Gain (dB)",
                    "invert"}}}},
                {"pamguard.patch-panel",
                 {{"settings.dialog",
                   {"Data Source",
                    "Channel Connections",
                    "Inputs",
                    "Outputs"}},
                   {"settings.advanced",
                    {"Gain matrix (C++ extension)"}}}},
                {"pamguard.filter",
                 {{"settings.dialog",
                   {"Data input",
                    "Filter Type",
                    "Filter Response",
                    "Filter shape",
                    "Filter parameters",
                    "Pole/impulse response",
                    "Bode Plot",
                    "Log Scale",
                    "Linear Scale"}}}},
                {"pamguard.decimator",
                 {{"settings.dialog",
                   {"Input Data Source",
                    "Decimator settings",
                    "Source sample rate",
                    "Output sample rate",
                    "Anti-aliasing filter",
                    "Interpolation"}},
                  {"settings.viewer-tabs",
                   {"Offline Files", "Runtime Settings"}},
                  {"filter-settings.dialog",
                   {"Filter Type",
                    "Filter Response",
                    "Filter shape",
                    "Filter parameters",
                    "Pole/impulse response",
                    "Bode Plot",
                    "Log Scale",
                    "Linear Scale"}}}},
                {"pamguard.fft",
                 {{"settings.tabs",
                   {"FFT", "Click Removal", "Spectral Noise Removal"}}}},
                {"pamguard.fft-noise-monitor",
                 {{"settings.dialog",
                   {"FFT Data Source",
                    "FFT Resolution",
                    "Measurements",
                    "Interval between measurements",
                    "Number of measures in interval",
                    "Use all FFT data",
                    "Third Octave",
                    "Deci Decade",
                    "Octave",
                    "Decade",
                    "Add Other bands",
                    "Remove",
                    "Edit ..."}},
                  {"settings.custom-band-dialog",
                   {"Frequency Resolution", "Name", "Range"}}}},
                {"pamguard.noise-band-monitor",
                 {{"settings.dialog",
                   {"Raw Data Source",
                    "Output",
                    "Output Interval",
                    "Measurement Bands",
                    "Band Type",
                    "Reference Frequency",
                    "Default",
                    "Maximum Frequency",
                    "Max",
                    "Minimum Frequency",
                    "Filters",
                    "Filter Type",
                    "Filter Order",
                    "Filter Gamma"}},
                  {"display.preferences",
                   {"Log Scale",
                    "Show Grid",
                    "Show Decimators",
                    "Show ANSI standards",
                    "Class 0",
                    "Class 1",
                    "Class 2"}}}},
                {"pamguard.ltsa",
                 {{"settings.dialog",
                   {"FFT Data source",
                    "Measurement",
                    "Measurement interval"}},
                  {"settings.advanced",
                   {"Longer average factor (persisted, dormant)"}}}},
                {"pamguard.whistles-moans",
                 {{"settings.tabs",
                   {"Detection", "Noise and Thresholding"}},
                  {"settings.detection",
                   {"Source of FFT data",
                    "Channel/Sequence list and grouping",
                    "Connections",
                    "Min Frequency",
                    "Max Frequency",
                    "Connection Type",
                    "Minimum length",
                    "Minimum total size",
                    "Shape 'stubs'",
                    "Crossing and Joining",
                    "Max Cross length"}},
                  {"settings.noise-methods",
                   {"Median Filter",
                    "Average Subtraction",
                    "Gaussian Kernel Smoothing",
                    "Thresholding"}}}},
                {"pamguard.ishmael-energy-sum",
                 {{"settings.source",
                   {"FFT Data Source",
                    "Channel/Sequence list and grouping"}},
                  {"settings.energy-sum",
                   {"Lower Frequency Bound",
                    "Upper Frequency Bound",
                    "Use Energy Ratio",
                    "Lower Ratio Bound",
                    "Upper Ratio Bound",
                    "Use Adaptive Threshold",
                    "Long filter",
                    "Spike Threshold",
                    "Use Detector Smoothing",
                    "Short filter",
                    "Use log scale"}},
                  {"settings.peak-detection",
                   {"Threshold",
                    "Min time over threshold",
                    "Max time over threshold",
                    "Min IDI"}}}},
                {"pamguard.ishmael-sgram-corr",
                 {{"settings.source",
                   {"FFT Data Source",
                    "Channel/Sequence list and grouping"}},
                  {"settings.spectrogram-correlation",
                   {"Segments (t0, f0, t1, f1)",
                    "Add Row",
                    "Remove Selected Row",
                    "Paste from Clipboard",
                    "Kernel Width, Hz",
                    "Use log-scaled spectrogram",
                    "Time-Frequency Contour"}},
                  {"settings.peak-detection",
                   {"Threshold",
                    "Min time over threshold",
                    "Max time over threshold",
                    "Min IDI"}}}},
                {"pamguard.ishmael-match-filter",
                 {{"settings.source",
                   {"Data Source",
                    "Channel list and grouping"}},
                  {"settings.matched-filter",
                   {"Kernel sound file",
                    "Select another file..."}},
                  {"settings.peak-picking",
                   {"Threshold",
                    "Min time over threshold",
                    "Min time before next detection"}}}},
                {"pamguard.click-detector",
                 {{"detection-parameters.tabs",
                   {"Source",
                    "Trigger",
                    "Click Length",
                    "Delays",
                    "Echoes",
                    "Noise"}},
                  {"display.operational",
                   {"Channel groups",
                    "Time window",
                    "Bearing axis",
                    "Amplitude axis",
                    "ICI axis",
                    "Echo visibility"}}}},
                {"pamguard.user-display",
                 {{"spectrogram-settings.tabs",
                   {"Data Source", "Scales", "Plug ins", "Mark Observers"}},
                  {"spectrogram-scales.sections",
                   {"Frequency Range",
                    "Amplitude Range",
                    "Time Range",
                    "Scrolling"}}}},
                {"pamguard.sound-output",
                 {{"settings.tabs", {"Playback", "Side Bar"}},
                  {"playback.sections",
                   {"Data source", "Source-specific playback options"}},
                  {"runtime-controls",
                   {"High-pass filter",
                    "Envelope mix",
                    "Playback speed",
                    "Gain"}}}},
                {"pamguard.level-meter",
                 {{"settings.dialog",
                   {"Raw Data Source",
                    "Scale selection",
                    "Peak",
                    "RMS",
                    "Scale range"}},
                  {"scale-reference.order",
                   {"Relative to full scale",
                    "Volts",
                    "Micropascal"}}}},
            };
        const std::map<std::string, OrderedSurfaces>
            expected_core_actions{
                {"pamguard.acquisition",
                 {{"settings-menu", {"<unitName> ..."}}}},
                {"pamguard.amplifier",
                 {{"settings-menu", {"<unitName> ..."}}}},
                {"pamguard.patch-panel",
                 {{"settings-menu", {"<unitName> ..."}}}},
                {"pamguard.filter",
                 {{"settings-menu", {"<unitName> Settings..."}},
                  {"settings-dialog", {"Import", "Plot"}}}},
                {"pamguard.decimator",
                 {{"settings-menu", {"<unitName>..."}},
                  {"settings-dialog",
                   {"Filter settings",
                    "Default Filter",
                    "Set Defaults"}}}},
                {"pamguard.fft",
                 {{"settings-menu", {"<unitName> settings ..."}}}},
                {"pamguard.fft-noise-monitor",
                 {{"settings-menu", {"<unitName> settings ..."}}}},
                {"pamguard.noise-band-monitor",
                 {{"settings-menu", {"<unitName> Settings..."}}}},
                {"pamguard.ltsa",
                 {{"settings-menu", {"<unitName> settings..."}}}},
                {"pamguard.whistles-moans",
                 {{"detection-menu", {"<unitName>"}},
                  {"display-menu", {"<unitName>"}},
                  {"settings-dialog", {"Set Defaults"}}}},
                {"pamguard.ishmael-energy-sum",
                 {{"detection-menu", {"<unitName>"}}}},
                {"pamguard.ishmael-sgram-corr",
                 {{"detection-menu", {"<unitName>"}}}},
                {"pamguard.ishmael-match-filter",
                 {{"detection-menu", {"<unitName>"}}}},
                {"pamguard.click-detector",
                 {{"settings-menu.normal",
                   {"Detection Parameters ...",
                    "Mark observer options",
                    "Digital pre filter ...",
                    "Digital trigger filter ...",
                    "Angle Vetoes ...",
                    "Click Classification ...",
                    "Click Train Identification ...",
                    "Click Train Localisation ...",
                    "Audible Alarm ..."}}}},
                {"pamguard.user-display",
                 {{"display-menu",
                   {"New Spectrogram Display",
                    "New Radar Display",
                    "New Time base data display",
                    "New Time base data display fx",
                    "Arrange Windows ...",
                    "Scroller Coupling"}},
                  {"spectrogram-context-menu", {"Settings ..."}}}},
                {"pamguard.sound-output",
                 {{"settings-menu", {"<unitName> ..."}}}},
                {"pamguard.level-meter",
                 {{"settings-menu", {"<unitName> ..."}}}},
            };
        const std::map<std::string, Defaults> expected_core_defaults{
            {"pamguard.acquisition",
             {{"daqSystemType", "Sound Card"},
              {"sampleRate", 48000},
              {"nChannels", 2},
              {"voltsPeak2Peak", 5},
              {"channelList", json::array({0, 1})},
              {"hydrophoneList", json::array({0, 1})},
              {"preamplifier",
               {
                   {"gainDb", 0},
                   {"bandwidthHz", json::array({0, 20000})},
               }},
              {"subtractDC", true},
              {"dcTimeConstant", 1}}},
            {"pamguard.amplifier",
             {{"gain[].magnitudeDb", 0},
              {"gain[].invert", false}}},
            {"pamguard.patch-panel",
             {{"patches.diagonal", true},
               {"patches.offDiagonal", false}}},
            {"pamguard.filter",
             {{"channelBitmap", 0},
              {"filterParams.filterType", "butterworth"},
              {"filterParams.filterBand", "bandPass"},
              {"filterParams.filterOrder", 4},
              {"filterParams.lowPassFreq", 20000},
              {"filterParams.highPassFreq", 2000},
              {"filterParams.passBandRipple", 2},
              {"filterParams.stopBandRipple", 2},
              {"filterParams.chebyGamma", 3},
              {"filterParams.arbFreqs", json::array()},
              {"filterParams.arbGains", json::array()}}},
            {"pamguard.decimator",
             {{"newSampleRate", 2000},
              {"channelMap", 0},
              {"interpolation", 0},
              {"filterParams.filterType", "butterworth"},
              {"filterParams.filterBand", "lowPass"},
              {"filterParams.filterOrder", 6},
              {"filterParams.lowPassFreq", 1000},
              {"filterParams.highPassFreq", 2000},
              {"filterParams.passBandRipple", 2},
              {"filterParams.stopBandRipple", 2},
              {"filterParams.chebyGamma", 3},
              {"filterParams.arbFreqs", json::array()},
              {"filterParams.arbGains", json::array()}}},
            {"pamguard.fft",
             {{"fftLength", 1024},
              {"fftHop", 512},
              {"channelMap", 3},
              {"windowFunction", 2},
              {"clickRemoval", false},
              {"clickThreshold", 5},
              {"clickPower", 6}}},
            {"pamguard.fft-noise-monitor",
             {{"channelBitmap", 1},
              {"measurementIntervalSeconds", 60},
              {"nMeasures", 100},
              {"useAll", true},
              {"measurementBands", json::array()}}},
            {"pamguard.noise-band-monitor",
             {{"channelMap", 1},
              {"bandType", "thirdOctave"},
              {"filterType", "butterworth"},
              {"iirOrder", 6},
              {"firOrder", 7},
              {"firGamma", 2.5},
              {"outputIntervalSeconds", 10},
              {"minFrequency", 1.7925856629456591},
              {"maxFrequency", 1133.6866687924667},
              {"referenceFrequency", 1000}}},
            {"pamguard.ltsa",
             {{"channelMap", 0},
              {"intervalSeconds", 60},
              {"longerFactor", 10}}},
            {"pamguard.whistles-moans",
             {{"channelBitmap", 0},
              {"groupingType", "all"},
              {"channelGroups", json::array()},
              {"minFrequency", 0},
              {"maxFrequency", 0},
              {"connectType", 8},
              {"minLength", 10},
              {"minPixels", 20},
              {"keepShapeStubs", false},
              {"fragmentationMethod", 3},
              {"maxCrossLength", 5},
              {"specNoiseSettings.runMethod[0]", false},
              {"specNoiseSettings.methodSettings[0].filterLength", 61},
              {"specNoiseSettings.runMethod[1]", false},
              {"specNoiseSettings.methodSettings[1].updateConstant", 0.02},
              {"specNoiseSettings.runMethod[2]", false},
              {"specNoiseSettings.runMethod[3]", false},
              {"specNoiseSettings.methodSettings[3].thresholdDB", 8},
              {"specNoiseSettings.methodSettings[3].finalOutput", 2}}},
            {"pamguard.ishmael-energy-sum",
             {{"groupedSourceParmas.channelBitmap", 0},
              {"groupedSourceParmas.groupingType", "all"},
              {"groupedSourceParmas.channelGroups", json::array()},
              {"thresh", 1},
              {"minTime", 0},
              {"maxTime", 99999},
              {"refractoryTime", 0},
              {"f0", 0},
              {"f1", 1000},
              {"ratiof0", 1000},
              {"ratiof1", 2000},
              {"useRatio", false},
              {"adaptiveThreshold", false},
              {"longFilter", 0.0001},
              {"useLog", false},
              {"spikeDecay", 100},
              {"outPutSmoothing", false},
              {"shortFilter", 0.1}}},
            {"pamguard.ishmael-sgram-corr",
             {{"groupedSourceParmas.channelBitmap", 0},
              {"groupedSourceParmas.groupingType", "all"},
              {"groupedSourceParmas.channelGroups", json::array()},
              {"thresh", 1},
              {"minTime", 0},
              {"maxTime", 99999},
              {"refractoryTime", 0},
              {"segment", json::array()},
              {"spread", 100},
              {"useLog", false}}},
            {"pamguard.ishmael-match-filter",
             {{"groupedSourceParmas.channelBitmap", 0},
              {"groupedSourceParmas.groupingType", "all"},
              {"groupedSourceParmas.channelGroups", json::array()},
              {"thresh", 1},
              {"minTime", 0},
              {"maxTime", 99999},
              {"refractoryTime", 0},
              {"kernelFilenameList", json::array()},
              {"MatchFiltProcess2.kernel", json::array()}}},
            {"pamguard.click-detector",
             {{"groupedSourceParameters.channelBitmap", 3},
              {"groupedSourceParameters.groupingType", "all"},
              {"triggerBitmap", std::uint64_t{4294967295}},
              {"minTriggerChannels", 1},
              {"dbThreshold", 10},
              {"longFilter", 0.00001},
              {"longFilter2", 0.000001},
              {"shortFilter", 0.1},
              {"preSample", 40},
              {"postSample", 40},
              {"minSep", 100},
              {"maxLength", 1024},
              {"preFilter.filterType", "BUTTERWORTH"},
              {"preFilter.filterBand", "HIGHPASS"},
              {"preFilter.filterOrder", 4},
              {"preFilter.highPassFreq", 500},
              {"triggerFilter.filterType", "BUTTERWORTH"},
              {"triggerFilter.filterBand", "HIGHPASS"},
              {"triggerFilter.filterOrder", 2},
              {"triggerFilter.highPassFreq", 2000},
              {"sampleNoise", true},
              {"noiseSampleInterval", 5},
              {"storeBackground", true},
              {"backgroundIntervalMillis", 5000},
              {"clickClassifierType", "sweep"},
              {"runEchoOnline", false},
              {"discardEchoes", false},
              {"publishTriggerFunction", false},
              {"classifyOnline", false},
              {"runClickTrainId", false},
              {"minTrainClicks", 6}}},
            {"pamguard.user-display",
             {{"channelList", json::array({0})},
               {"frequencyLimits", json::array({0, 0})},
               {"amplitudeLimits", json::array({50, 120})},
               {"colourMap", "GREY"},
               {"wrapDisplay", true},
               {"timeScaleFixed", false},
               {"displayLength", 20},
               {"pixelsPerSlics", 1},
               {"showScale", true}}},
            {"pamguard.sound-output",
             {{"dataSource", 0},
              {"channelBitmap", 0},
              {"deviceNumber", 0},
              {"deviceType", 0},
              {"defaultSampleRate", true},
              {"playbackRate", 48000},
              {"logPlaybackSpeed", 0},
              {"playbackSpeed", 1},
              {"playbackGain", 0},
              {"hpFilter", 0}}},
            {"pamguard.level-meter",
             {{"minLevel", -80},
              {"scaleReference", 0},
              {"scaleType", 0}}},
        };
        const std::map<std::string, std::map<std::string, std::string>>
            expected_dynamic_defaults{
                {"pamguard.user-display",
                 {{"nPanels",
                   "Array.ArrayManager current hydrophone count"}}},
            };
        const std::map<std::string, std::string>
            expected_click_action_scopes{
                {"Detection Parameters ...", "required"},
                {"Mark observer options", "requires-display-marking"},
                {"Digital pre filter ...", "required"},
                {"Digital trigger filter ...", "required"},
                {"Angle Vetoes ...", "required"},
                {"Click Classification ...", "required"},
                {"Click Train Identification ...", "required"},
                {"Click Train Localisation ...", "required"},
                {"Audible Alarm ...", "agreed-exclusion"},
            };
        const std::map<std::string, std::string>
            expected_display_action_conditions{
                {"New Spectrogram Display", "always"},
                {"New Radar Display", "always"},
                {"New Time base data display", "SMRU enabled"},
                {"New Time base data display fx",
                 "Java compliance >= 1.8"},
                {"Arrange Windows ...", "always"},
                {"Scroller Coupling",
                 "viewer mode and coupling supplies an options item"},
            };

        require(
            document.contains("coreConfigurationContracts") &&
                document.at("coreConfigurationContracts").is_array(),
            "Manifest must contain a coreConfigurationContracts array");
        std::set<std::string> core_contract_ids;
        for (const auto& contract :
             document.at("coreConfigurationContracts")) {
            const auto bundle_id = required_string(
                contract,
                "bundleId",
                "core configuration contract");
            const auto context =
                "core configuration contract '" + bundle_id + "'";
            require(
                expected_core_sections.contains(bundle_id),
                context +
                    " is not one of the approved Phase 3/4 configuration units");
            require(
                core_contract_ids.insert(bundle_id).second,
                "Duplicate " + context);
            const auto expected_core_claim =
                fixture_validated_settings_contracts.contains(
                    bundle_id)
                ? "java-fixture-validated"
                : "not-claimed";
            require(
                required_string(contract, "cppParityClaim", context) ==
                    expected_core_claim,
                context +
                    " parity claim differs from its pinned fixture "
                    "evidence");
            require(
                ordered_surfaces(contract, "sectionOrder", context) ==
                    expected_core_sections.at(bundle_id),
                context +
                    " section order differs from the pinned Java UI");
            require(
                ordered_surfaces(contract, "actionOrder", context) ==
                    expected_core_actions.at(bundle_id),
                context +
                    " action order differs from the pinned Java UI");

            for (const auto& surface :
                 contract.at("actionOrder")) {
                if (!surface.contains("items")) {
                    continue;
                }
                const auto surface_name = required_string(
                    surface,
                    "surface",
                    context + ".actionOrder");
                for (const auto& item : surface.at("items")) {
                    const auto label = required_string(
                        item,
                        "label",
                        context + ".actionOrder." + surface_name);
                    if (bundle_id == "pamguard.click-detector") {
                        require(
                            surface_name == "settings-menu.normal" &&
                                required_string(
                                    item,
                                    "implementationScope",
                                    context + ".actionOrder." +
                                        surface_name + "." + label) ==
                                    expected_click_action_scopes.at(label),
                            context +
                                " action implementation scope changed for " +
                                label);
                    }
                    else if (bundle_id == "pamguard.user-display" &&
                             surface_name == "display-menu") {
                        require(
                            required_string(
                                item,
                                "condition",
                                context + ".actionOrder." + surface_name +
                                    "." + label) ==
                                expected_display_action_conditions.at(label),
                            context + " action condition changed for " +
                                label);
                    }
                    else {
                        require(
                            false,
                            context +
                                " uses unexpected object-form action items");
                    }
                }
            }

            require(
                contract.contains("priorityDefaults") &&
                    contract.at("priorityDefaults").is_array() &&
                    !contract.at("priorityDefaults").empty(),
                context +
                    " must contain non-empty priorityDefaults");
            Defaults static_defaults;
            std::map<std::string, std::string> dynamic_defaults;
            for (const auto& default_value :
                 contract.at("priorityDefaults")) {
                const auto path = required_string(
                    default_value,
                    "path",
                    context + ".priorityDefault");
                required_string(
                    default_value,
                    "authority",
                    context + ".priorityDefault." + path);
                const bool has_value = default_value.contains("value");
                const bool has_value_source =
                    default_value.contains("valueSource");
                require(
                    has_value != has_value_source,
                    context + ".priorityDefault." + path +
                        " must contain exactly one of value or valueSource");
                if (has_value) {
                    require(
                        static_defaults
                            .emplace(path, default_value.at("value"))
                            .second,
                        context +
                            " contains duplicate priority default path " +
                            path);
                }
                else {
                    require(
                        dynamic_defaults
                            .emplace(
                                path,
                                required_string(
                                    default_value,
                                    "valueSource",
                                    context + ".priorityDefault." + path))
                            .second,
                        context +
                            " contains duplicate priority default path " +
                            path);
                }
                if (default_value.contains("condition")) {
                    required_string(
                        default_value,
                        "condition",
                        context + ".priorityDefault." + path);
                }
            }
            require(
                static_defaults == expected_core_defaults.at(bundle_id),
                context +
                    " priority defaults differ from the pinned Java values");
            const auto expected_dynamic =
                expected_dynamic_defaults.find(bundle_id);
            const std::map<std::string, std::string> no_dynamic_defaults;
            require(
                dynamic_defaults ==
                    (expected_dynamic == expected_dynamic_defaults.end()
                         ? no_dynamic_defaults
                         : expected_dynamic->second),
                context +
                    " dynamic priority defaults differ from pinned Java");
            if (bundle_id == "pamguard.user-display") {
                const auto amplitude = std::find_if(
                    contract.at("priorityDefaults").begin(),
                    contract.at("priorityDefaults").end(),
                    [](const auto& value) {
                        return value.value("path", std::string{}) ==
                            "amplitudeLimits";
                    });
                require(
                    amplitude != contract.at("priorityDefaults").end() &&
                        required_string(
                            *amplitude,
                            "condition",
                            context +
                                ".priorityDefault.amplitudeLimits") ==
                            "default underwater scale before the global-medium air override",
                    context +
                        " must preserve the medium-dependent amplitude default");
            }
        }
        require(
            core_contract_ids.size() == 17 &&
                core_contract_ids ==
                    std::set<std::string>{
                        "pamguard.acquisition",
                        "pamguard.amplifier",
                        "pamguard.patch-panel",
                        "pamguard.filter",
                        "pamguard.decimator",
                        "pamguard.fft",
                        "pamguard.fft-noise-monitor",
                        "pamguard.noise-band-monitor",
                        "pamguard.ltsa",
                        "pamguard.whistles-moans",
                        "pamguard.ishmael-energy-sum",
                        "pamguard.ishmael-sgram-corr",
                        "pamguard.ishmael-match-filter",
                        "pamguard.click-detector",
                        "pamguard.user-display",
                        "pamguard.sound-output",
                        "pamguard.level-meter",
                    },
            "The seventeen approved Phase 3/4 configuration contracts must "
            "be present exactly once");

        require(
            document.contains("runtimeTypes") &&
                document.at("runtimeTypes").is_array(),
            "Manifest must contain a runtimeTypes array");
        std::unordered_map<std::string, std::string> runtime_names;
        std::unordered_map<std::string, std::string> dispositions;
        std::unordered_map<std::string, std::vector<std::string>> owners_by_runtime;
        std::map<std::string, std::size_t> disposition_counts;
        for (const auto& entry : document.at("runtimeTypes")) {
            const auto id = required_string(entry, "runtimeTypeId", "runtime type");
            const auto context = "runtime type '" + id + "'";
            const auto runtime_name =
                required_string(entry, "runtimeName", context);
            const auto disposition =
                required_string(entry, "operatorDisposition", context);
            require(
                disposition == "controlled-unit" ||
                    disposition == "hidden-adapter" ||
                    disposition == "display-provider" ||
                    disposition == "extension",
                context + " has an invalid operator disposition");
            const auto parity = required_string(entry, "parityLabel", context);
            require(
                allowed_parity_labels.contains(parity),
                context + " has an unknown parity label");
            const auto owners =
                required_string_array(entry, "recipeOwners", context);

            for (const auto& owner : owners) {
                const auto found = bundles.find(owner);
                require(
                    found != bundles.end(),
                    context + " refers to unknown recipe owner '" + owner + "'");
                require(
                    found->second.runtime_type_ids.contains(id),
                    context + " is absent from owner '" + owner +
                        "' runtime recipe");
            }
            if (disposition == "controlled-unit") {
                require(
                    owners.size() == 1 && owners.front() == id,
                    context +
                        " must be directly owned by its controlled-unit bundle");
                const auto& owner = bundles.at(owners.front());
                require(
                    owner.kind == "controlled-unit" &&
                        owner.java_relationship == "direct" &&
                        parity == owner.parity_label,
                    context +
                        " must inherit its direct Java controlled-unit parity");
            }
            else if (disposition == "hidden-adapter") {
                require(
                    parity == "internal-foundation",
                    context + " must be labelled internal-foundation");
                for (const auto& owner : owners) {
                    require(
                        bundles.at(owner).kind == "controlled-unit",
                        context + " must be owned by controlled-unit recipes");
                }
            }
            else if (disposition == "display-provider") {
                const bool user_spectrogram =
                    id == "pamguard.spectrogram-display" &&
                    owners == std::vector<std::string>{
                        "pamguard.user-display"};
                const bool click_display =
                    id == "pamguard.click-display" &&
                    owners == std::vector<std::string>{
                        "pamguard.click-detector"};
                const bool level_meter_display =
                    id == "pamguard.level-meter-display" &&
                    owners == std::vector<std::string>{
                        "pamguard.level-meter"};
                require(
                    (user_spectrogram || click_display ||
                     level_meter_display) &&
                        parity == "display-foundation",
                    context +
                        " must be an authoritative owner-contributed "
                        "display foundation");
                required_string(entry, "javaImplementationClass", context);
            }
            else {
                require(
                    owners == std::vector<std::string>{
                        "pamguard.system-diagnostics"} &&
                        parity == "web-extension",
                    context + " must remain an explicitly labelled web extension");
            }

            require(
                runtime_names.emplace(id, runtime_name).second,
                "Duplicate runtimeTypeId: " + id);
            dispositions.emplace(id, disposition);
            owners_by_runtime.emplace(id, owners);
            ++disposition_counts[disposition];
        }

        require(
            runtime_names.size() == 33,
            "Parity manifest must account for exactly 33 current runtime types");
        // Java registers the matched-template unit with the historical
        // "Classifer" spelling. The current C++ runtime label is the explicit
        // corrected alias; lock both sides so neither can silently overwrite
        // the authority spelling or masquerade as the other.
        require(
            expected_java_authorities
                    .at("pamguard.matched-template-classifier")
                    .registered_name ==
                "Matched Template Click Classifer" &&
                runtime_names.at(
                    "pamguard.matched-template-classifier") ==
                    "Matched Template Classifier",
            "Matched-template Java name/corrected runtime alias changed");
        require(
            disposition_counts["controlled-unit"] == 24 &&
                disposition_counts["hidden-adapter"] == 5 &&
                disposition_counts["display-provider"] == 3 &&
                disposition_counts["extension"] == 1,
            "Runtime disposition totals do not match the approved mapping");

        const std::map<std::string, std::vector<std::string>> expected_hidden{
            {"pamguard.spectrogram-noise",
             {"pamguard.fft", "pamguard.whistles-moans"}},
            {"pamguard.click-features", {"pamguard.click-detector"}},
            {"pamguard.click-localiser", {"pamguard.click-detector"}},
            {"pamguard.click-classifier", {"pamguard.click-detector"}},
            {"pamguard.click-train", {"pamguard.click-detector"}},
        };
        for (const auto& [id, expected_owners] : expected_hidden) {
            require(
                dispositions.at(id) == "hidden-adapter" &&
                    owners_by_runtime.at(id) == expected_owners,
                "Hidden adapter ownership changed for " + id);
        }

        for (const auto& [bundle_id, bundle] : bundles) {
            for (const auto& runtime_id : bundle.runtime_type_ids) {
                require(
                    runtime_names.contains(runtime_id),
                    "Bundle '" + bundle_id +
                        "' recipe refers to an unregistered manifest runtime type");
                const auto& owners = owners_by_runtime.at(runtime_id);
                require(
                    std::find(owners.begin(), owners.end(), bundle_id) != owners.end(),
                    "Bundle/runtime recipe ownership is not bidirectional for " +
                        bundle_id + " -> " + runtime_id);
            }
        }

        pamguard::core::ModuleRegistry registry;
        pamguard::core::register_builtin_module_types(registry);
        const auto registered = registry.list();
        require(
            registered.size() == 33,
            "Built-in registry no longer contains the expected 33 runtime types");
        std::set<std::string> registered_ids;
        for (const auto& descriptor : registered) {
            require(
                registered_ids.insert(descriptor.id).second,
                "Built-in registry contains a duplicate runtime type");
            const auto found = runtime_names.find(descriptor.id);
            require(
                found != runtime_names.end(),
                "Built-in runtime type is absent from parity manifest: " +
                    descriptor.id);
            require(
                found->second == descriptor.name,
                "Runtime name differs from parity manifest for " + descriptor.id);
        }
        for (const auto& [runtime_id, unused_name] : runtime_names) {
            require(
                registered_ids.contains(runtime_id),
                "Parity manifest contains a stale runtime type: " + runtime_id);
        }

        std::cout
            << "Controlled-unit parity manifest covers "
            << runtime_names.size() << " runtime types across "
            << bundles.size() << " recipe owners\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
