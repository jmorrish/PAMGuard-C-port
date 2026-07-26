#include "pamguard/project/MhtClickTrainControlledUnit.h"

#include "pamguard/core/MhtClickTrainSettings.h"

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

std::vector<SettingDefaultDescriptor> default_evidence() {
    return {
        {
            "/algorithm",
            "ClickTrainParams.ctDetectorType",
            R"("mht")",
            {},
            "Java integer zero is represented by the only algorithm "
            "instantiated by ClickTrainControl",
            "clickTrainDetector.ClickTrainParams#ctDetectorType",
        },
        {
            "/channelGroups",
            "ClickTrainParams.channelGroups",
            "[1]",
            {},
            {},
            "clickTrainDetector.ClickTrainParams#channelGroups",
        },
        {
            "/kernel/nHold",
            "MHTKernelParams.nHold",
            "20",
            {},
            {},
            "clickTrainDetector.clickTrainAlgorithms.mht."
            "MHTKernelParams#nHold",
        },
        {
            "/kernel/nPruneback",
            "MHTKernelParams.nPruneback",
            "4",
            {},
            {},
            "clickTrainDetector.clickTrainAlgorithms.mht."
            "MHTKernelParams#nPruneback",
        },
        {
            "/kernel/nPrunebackStart",
            "MHTKernelParams.nPrunebackStart",
            "5",
            {},
            {},
            "clickTrainDetector.clickTrainAlgorithms.mht."
            "MHTKernelParams#nPrunebackStart",
        },
        {
            "/kernel/maxCoast",
            "MHTKernelParams.maxCoast",
            "3",
            {},
            {},
            "clickTrainDetector.clickTrainAlgorithms.mht."
            "MHTKernelParams#maxCoast",
        },
        {
            "/chi2/maximumIciSeconds",
            "StandardMHTChi2Params.maxICI",
            "0.4",
            {},
            {},
            "clickTrainDetector.clickTrainAlgorithms.mht."
            "StandardMHTChi2Params#maxICI",
        },
        {
            "/chi2/coastPenalty",
            "StandardMHTChi2Params.coastPenalty",
            "10",
            {},
            {},
            "clickTrainDetector.clickTrainAlgorithms.mht."
            "StandardMHTChi2Params#coastPenalty",
        },
        {
            "/chi2/newTrackPenalty",
            "StandardMHTChi2Params.newTrackPenalty",
            "50",
            {},
            {},
            "clickTrainDetector.clickTrainAlgorithms.mht."
            "StandardMHTChi2Params#newTrackPenalty",
        },
        {
            "/chi2/newTrackClicks",
            "StandardMHTChi2Params.newTrackN",
            "3",
            {},
            {},
            "clickTrainDetector.clickTrainAlgorithms.mht."
            "StandardMHTChi2Params#newTrackN",
        },
        {
            "/classifier/runClassifier",
            "ClickTrainParams.runClassifier",
            "false",
            {},
            {},
            "clickTrainDetector.ClickTrainParams#runClassifier",
        },
        {
            "/classifier/preClassifier/chi2Threshold",
            "Chi2ThresholdParams.chi2Threshold",
            "1500",
            {},
            {},
            "clickTrainDetector.classification.simplechi2classifier."
            "Chi2ThresholdParams#chi2Threshold",
        },
        {
            "/classifier/preClassifier/minimumClicks",
            "Chi2ThresholdParams.minClicks",
            "5",
            {},
            {},
            "clickTrainDetector.classification.simplechi2classifier."
            "Chi2ThresholdParams#minClicks",
        },
        {
            "/localisation/enabled",
            "CTLocParams.shouldloc",
            "false",
            {},
            "enabled=true remains explicitly unsupported in the portable "
            "runtime",
            "clickTrainDetector.localisation.CTLocParams#shouldloc",
        },
    };
}

} // namespace

ControlledUnitDescriptor
make_mht_click_train_controlled_unit_descriptor() {
    return {
        "pamguard.mht-click-train",
        1,
        {
            "Click Train Detector",
            "Detectors",
            "clickTrainDetector.ClickTrainControl",
            "direct",
            "Searches for click trains in detected clicks.",
            "detectors/ClickTrainDetector/docs/ClickTrainDetector.html",
            {
                "src/PamModel/PamModel.java",
                "src/clickTrainDetector/ClickTrainControl.java",
                "src/clickTrainDetector/ClickTrainParams.java",
                "src/clickTrainDetector/ClickTrainProcess.java",
                "src/clickTrainDetector/clickTrainAlgorithms/mht/"
                "MHTClickTrainAlgorithm.java",
                "src/clickTrainDetector/layout/ClickTrainAlgorithmPaneFX.java",
            },
        },
        unlimited_in_all_modes(),
        {
            {
                "clicks",
                "Detected click source",
                DataRoleDirection::Input,
                "pamguard.click",
                RoleCardinality::ExactlyOne,
                {},
                // This is the dependency declared in PamModel.java. The
                // settings pane narrows the actual source to ClickDetection.
                "PamDetection.RawDataUnit",
                "pamguard.click-detector",
            },
            {
                "features",
                "Click features",
                DataRoleDirection::Input,
                "pamguard.click-feature",
                RoleCardinality::ZeroOrOne,
                {},
                "clickDetector.ClickDetection",
                std::nullopt,
            },
            {
                "localisations",
                "Click delay localisations",
                DataRoleDirection::Input,
                "pamguard.click-localisation",
                RoleCardinality::ZeroOrOne,
                {},
                "clickDetector.ClickDetection",
                std::nullopt,
            },
            {
                "bearings",
                "Click bearings",
                DataRoleDirection::Input,
                "pamguard.click-bearing",
                RoleCardinality::ZeroOrOne,
                {},
                "clickDetector.ClickDetection",
                std::nullopt,
            },
            {
                "trains",
                "MHT click trains",
                DataRoleDirection::Output,
                "pamguard.mht-click-train",
                RoleCardinality::ExactlyOne,
                {"clip-trigger"},
                {},
                std::nullopt,
            },
            {
                "classifications",
                "Click-train classifications",
                DataRoleDirection::Output,
                "pamguard.click-train-classification",
                RoleCardinality::ExactlyOne,
                {"clip-trigger"},
                {},
                std::nullopt,
            },
        },
        {
            1,
            {
                "clickTrainDetector.ClickTrainParams",
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
                "clickDetector.alarm.ClickAlarmParameters",
            },
            {
                "src/clickTrainDetector/ClickTrainParams.java",
                "src/clickTrainDetector/clickTrainAlgorithms/mht/MHTParams.java",
                "src/clickTrainDetector/clickTrainAlgorithms/mht/MHTKernelParams.java",
                "src/clickTrainDetector/clickTrainAlgorithms/mht/StandardMHTChi2Params.java",
                "src/clickTrainDetector/clickTrainAlgorithms/mht/mhtvar/SimpleChi2VarParams.java",
                "src/clickTrainDetector/clickTrainAlgorithms/mht/mhtvar/IDIChi2Params.java",
                "src/clickTrainDetector/clickTrainAlgorithms/mht/mhtvar/AmplitudeChi2Params.java",
                "src/clickTrainDetector/clickTrainAlgorithms/mht/mhtvar/BearingChi2VarParams.java",
                "src/clickTrainDetector/clickTrainAlgorithms/mht/mhtvar/CorrelationChi2Params.java",
                "src/clickTrainDetector/classification/simplechi2classifier/Chi2ThresholdParams.java",
                "src/clickTrainDetector/classification/idiClassifier/IDIClassifierParams.java",
                "src/clickTrainDetector/classification/bearingClassifier/BearingClassifierParams.java",
                "src/clickTrainDetector/classification/templateClassifier/TemplateClassifierParams.java",
                "src/clickTrainDetector/localisation/CTLocParams.java",
                "src/clickDetector/alarm/ClickAlarmParameters.java",
                "src/clickTrainDetector/layout/ClickTrainAlgorithmPaneFX.java",
            },
            core::mht_click_train_default_settings_json(),
            {
                {
                    "settings.tabs",
                    {
                        "Detector",
                        "Pre Classifier",
                        "Species Classifiers",
                    },
                },
            },
            default_evidence(),
            SettingsChangePolicy::StopRequired,
            "not-claimed",
            std::string(
                core::mht_click_train_settings_schema_json()),
        },
        {
            1,
            {
                {
                    "detector",
                    "pamguard.mht-click-train",
                    {
                        "",
                        "pamguard.mht-click-train-settings.v1",
                    },
                    true,
                    AvailabilityStatus::Available,
                    "partial",
                },
            },
            {
                {"clicks", {"detector", "clicks"}},
                {"features", {"detector", "features"}},
                {"localisations", {"detector", "localisations"}},
                {"bearings", {"detector", "bearings"}},
                {"trains", {"detector", "trains"}},
                {
                    "classifications",
                    {"detector", "classifications"},
                },
            },
            {},
            {},
            "pamguard.mht-click-train.runtime",
        },
        AvailabilityStatus::Available,
        "partial",
    };
}

} // namespace pamguard::project
