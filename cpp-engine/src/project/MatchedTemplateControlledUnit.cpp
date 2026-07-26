#include "pamguard/project/MatchedTemplateControlledUnit.h"

#include "pamguard/core/MatchedTemplateSettings.h"

#include <optional>

namespace pamguard::project {

namespace {

InstanceRulesDescriptor unlimited_in_all_modes() {
    return {
        0,
        std::nullopt,
        {
            RunMode::Normal,
            RunMode::Mixed,
            RunMode::Viewer,
        },
        {},
    };
}

PublicDataRoleDescriptor clicks_input() {
    return {
        "clicks",
        "Click Data Source",
        DataRoleDirection::Input,
        "pamguard.click",
        RoleCardinality::ExactlyOne,
        {"detections", "waveform"},
        "clickDetector.ClickDetection",
        "pamguard.click-detector",
    };
}

PublicDataRoleDescriptor annotated_clicks_output() {
    return {
        "annotatedClicks",
        "Classified clicks",
        DataRoleDirection::Output,
        "pamguard.click",
        RoleCardinality::ExactlyOne,
        {
            "detections",
            "waveform",
            "overlay",
            "classified",
        },
        {},
        std::nullopt,
    };
}

PublicDataRoleDescriptor classifications_output() {
    return {
        "classifications",
        "Matched-template classifications",
        DataRoleDirection::Output,
        "pamguard.matched-template-classification",
        RoleCardinality::ExactlyOne,
        {"annotations", "classified"},
        {},
        std::nullopt,
    };
}

std::vector<SettingDefaultDescriptor>
default_evidence() {
    return {
        {
            "/clickType",
            "type",
            "101",
            {},
            "unsigned view of the Java byte",
            "matchedTemplateClassifer.MatchedTemplateParams#type",
        },
        {
            "/normalisationType",
            "normalisationType",
            "1",
            {},
            "Java NORMALIZATION_RMS",
            "matchedTemplateClassifer.MatchedTemplateParams#normalisationType",
        },
        {
            "/peakSearch",
            "peakSearch",
            "true",
            {},
            {},
            "matchedTemplateClassifer.MatchedTemplateParams#peakSearch",
        },
        {
            "/peakSmoothing",
            "peakSmoothing",
            "5",
            {},
            {},
            "matchedTemplateClassifer.MatchedTemplateParams#peakSmoothing",
        },
        {
            "/lengthDb",
            "lengthdB",
            "6",
            {},
            {},
            "matchedTemplateClassifer.MatchedTemplateParams#lengthdB",
        },
        {
            "/restrictedBins",
            "restrictedBins",
            "2048",
            {},
            {},
            "matchedTemplateClassifer.MatchedTemplateParams#restrictedBins",
        },
        {
            "/channelClassification",
            "channelClassification",
            "0",
            {},
            "Java CHANNELS_REQUIRE_ALL; the dialog exposes only all or one",
            "matchedTemplateClassifer.MatchedTemplateParams#channelClassification",
        },
        {
            "/classifiers/0/thresholdToAccept",
            "classifiers[0].thresholdToAccept",
            "0.01",
            {},
            {},
            "matchedTemplateClassifer.MTClassifier#thresholdToAccept",
        },
        {
            "/classifiers/0/normalisation",
            "classifiers[0].normalisation",
            "0",
            {},
            "bare MTClassifier uses peak while the global default is RMS; accepting the dialog synchronises it to the global selection",
            "matchedTemplateClassifer.MTClassifier#normalisation",
        },
        {
            "/classifiers/0/matchTemplate/name",
            "classifiers[0].waveformMatch.name",
            R"("Beaked Whale")",
            {},
            {},
            "matchedTemplateClassifer.MTClassifier#waveformMatch",
        },
        {
            "/classifiers/0/matchTemplate/sampleRateHz",
            "classifiers[0].waveformMatch.sR",
            "192000",
            {},
            {},
            "matchedTemplateClassifer.MTClassifier#waveformMatch",
        },
        {
            "/classifiers/0/matchTemplate/waveform",
            "classifiers[0].waveformMatch.waveform",
            std::nullopt,
            "matchedTemplateClassifer.DefaultTemplates.beakedWhale1",
            {},
            "matchedTemplateClassifer.MTClassifier#waveformMatch",
        },
        {
            "/classifiers/0/rejectTemplate/name",
            "classifiers[0].waveformReject.name",
            R"("Dolphin")",
            {},
            {},
            "matchedTemplateClassifer.MTClassifier#waveformReject",
        },
        {
            "/classifiers/0/rejectTemplate/sampleRateHz",
            "classifiers[0].waveformReject.sR",
            "192000",
            {},
            {},
            "matchedTemplateClassifer.MTClassifier#waveformReject",
        },
        {
            "/classifiers/0/rejectTemplate/waveform",
            "classifiers[0].waveformReject.waveform",
            std::nullopt,
            "matchedTemplateClassifer.DefaultTemplates.dolphin1",
            {},
            "matchedTemplateClassifer.MTClassifier#waveformReject",
        },
    };
}

} // namespace

ControlledUnitDescriptor
make_matched_template_controlled_unit_descriptor() {
    return {
        "pamguard.matched-template-classifier",
        1,
        {
            // Preserve the misspelling registered by PamModel.
            "Matched Template Click Classifer",
            "Classifiers",
            "matchedTemplateClassifer.MTClassifierControl",
            "direct",
            "Classifies clicks by correlation against match and reject waveform templates",
            "classifiers.matchedtemplate.mathchedtemplate",
            {
                "src/PamModel/PamModel.java",
                "src/matchedTemplateClassifer/MTClassifierControl.java",
                "src/matchedTemplateClassifer/MatchedTemplateParams.java",
                "src/matchedTemplateClassifer/MTClassifier.java",
                "src/matchedTemplateClassifer/MatchTemplate.java",
                "src/matchedTemplateClassifer/DefaultTemplates.java",
                "src/matchedTemplateClassifer/MTProcess.java",
                "src/matchedTemplateClassifer/layoutFX/MTSettingsPane.java",
                "src/matchedTemplateClassifer/layoutFX/MTClassifierPane.java",
                "src/matchedTemplateClassifer/bespokeClassification/BeskopeClassifierManager.java",
            },
        },
        unlimited_in_all_modes(),
        {
            clicks_input(),
            annotated_clicks_output(),
            classifications_output(),
        },
        {
            1,
            {
                "matchedTemplateClassifer.MatchedTemplateParams",
                "matchedTemplateClassifer.MTClassifier",
                "matchedTemplateClassifer.MatchTemplate",
            },
            {
                "src/matchedTemplateClassifer/MTClassifierControl.java",
                "src/matchedTemplateClassifer/MatchedTemplateParams.java",
                "src/matchedTemplateClassifer/MTClassifier.java",
                "src/matchedTemplateClassifer/MatchTemplate.java",
                "src/matchedTemplateClassifer/DefaultTemplates.java",
                "src/matchedTemplateClassifer/MTProcess.java",
                "src/matchedTemplateClassifer/layoutFX/MTSettingsPane.java",
                "src/matchedTemplateClassifer/layoutFX/MTClassifierPane.java",
            },
            core::matched_template_default_settings_json(),
            {
                {
                    "settings.dialog",
                    {
                        "Click Data Source",
                        "General Classifier Settings",
                        "Channel Options",
                        "Click Type",
                        "Click Waveform",
                        "Amplitude Normalisation",
                        "Template 0",
                    },
                },
                {
                    "settings.click-waveform",
                    {
                        "Restrict parameter extraction to",
                        "samples",
                        "Peak threshold",
                        "dB",
                        "Smoothing",
                        "bins",
                    },
                },
                {
                    "settings.template-tab",
                    {
                        "Click Template Settings",
                        "Match threshold",
                        "Click Templates",
                        "Match Template",
                        "Reject Template",
                        "Import",
                    },
                },
                {
                    "settings.template-library",
                    {
                        "Beaked Whale Click",
                        "Dolphin Click",
                        "Harbour Porpoise",
                        "Sperm Whale (P0 P1 P2)",
                        "None",
                    },
                },
            },
            default_evidence(),
            SettingsChangePolicy::StopRequired,
            "java-fixture-validated",
            std::string(
                core::matched_template_settings_schema_json()),
        },
        {
            1,
            {
                {
                    "classifier",
                    "pamguard.matched-template-classifier",
                    {
                        "",
                        "pamguard.matched-template-settings.v1",
                    },
                    true,
                    AvailabilityStatus::Available,
                    "java-fixture-validated",
                },
            },
            {
                {
                    "clicks",
                    {
                        "classifier",
                        "clicks",
                    },
                },
                {
                    "annotatedClicks",
                    {
                        "classifier",
                        "accepted",
                    },
                },
                {
                    "classifications",
                    {
                        "classifier",
                        "classifications",
                    },
                },
            },
            {},
            {},
            "pamguard.matched-template-classifier.runtime",
        },
        AvailabilityStatus::Available,
        "partial",
    };
}

} // namespace pamguard::project
