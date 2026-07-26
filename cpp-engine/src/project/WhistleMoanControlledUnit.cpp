#include "pamguard/project/WhistleMoanControlledUnit.h"

#include "pamguard/core/WhistleMoanSettings.h"

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

PublicDataRoleDescriptor fft_input() {
    return {
        "fft",
        "FFT data source",
        DataRoleDirection::Input,
        "pamguard.fft",
        RoleCardinality::ExactlyOne,
        {"frequency-domain"},
        "fftManager.FFTDataUnit",
        "pamguard.fft",
    };
}

PublicDataRoleDescriptor fft_output() {
    return {
        "noiseReducedFft",
        "Noise free FFT data",
        DataRoleDirection::Output,
        "pamguard.fft",
        RoleCardinality::ExactlyOne,
        {"frequency-domain"},
        {},
        std::nullopt,
    };
}

PublicDataRoleDescriptor contour_output() {
    return {
        "contours",
        "Contours",
        DataRoleDirection::Output,
        "pamguard.whistle-contour",
        RoleCardinality::ExactlyOne,
        {"detections", "overlay", "clip-trigger"},
        {},
        std::nullopt,
    };
}

std::vector<SettingDefaultDescriptor>
default_evidence() {
    return {
        {
            "/channelBitmap",
            "channelBitmap",
            "0",
            {},
            "before the operator selects FFT channels or sequences",
            "PamView.GroupedSourceParameters#channelBitmap",
        },
        {
            "/groupingType",
            "groupingType",
            R"("all")",
            {},
            "Java GROUP_ALL integer 1 is represented by the stable portable name",
            "PamView.GroupedSourceParameters#groupingType",
        },
        {
            "/channelGroups",
            "channelGroups",
            "[]",
            {},
            "Java null normalized to a portable empty array",
            "PamView.GroupedSourceParameters#channelGroups",
        },
        {
            "/minFrequencyHz",
            "minFrequency",
            "0",
            {},
            {},
            "whistlesAndMoans.WhistleToneParameters#minFrequency",
        },
        {
            "/maxFrequencyHz",
            "maxFrequency",
            "0",
            {},
            "zero is the constructor sentinel resolved to source Nyquist by getMaxFrequency",
            "whistlesAndMoans.WhistleToneParameters#maxFrequency",
        },
        {
            "/connectType",
            "connectType",
            "8",
            {},
            {},
            "whistlesAndMoans.WhistleToneParameters#connectType",
        },
        {
            "/minLength",
            "minLength",
            "10",
            {},
            {},
            "whistlesAndMoans.WhistleToneParameters#minLength",
        },
        {
            "/minPixels",
            "minPixels",
            "20",
            {},
            {},
            "whistlesAndMoans.WhistleToneParameters#minPixels",
        },
        {
            "/keepShapeStubs",
            "keepShapeStubs",
            "false",
            {},
            {},
            "whistlesAndMoans.WhistleToneParameters#keepShapeStubs",
        },
        {
            "/fragmentationMethod",
            "fragmentationMethod",
            "3",
            {},
            "Java FRAGMENT_RELINK",
            "whistlesAndMoans.WhistleToneParameters#fragmentationMethod",
        },
        {
            "/maxCrossLength",
            "maxCrossLength",
            "5",
            {},
            {},
            "whistlesAndMoans.WhistleToneParameters#maxCrossLength",
        },
        {
            "/noiseReduction/medianFilter",
            "specNoiseSettings.runMethod[0]",
            "false",
            {},
            "bare SpectrogramNoiseSettings has a null runMethod array",
            "spectrogramNoiseReduction.SpectrogramNoiseSettings#isRunMethod",
        },
        {
            "/noiseReduction/medianFilterLength",
            "specNoiseSettings.methodSettings[0].filterLength",
            "61",
            {},
            "methodSettings is initially empty; the owned process supplies its method default",
            "spectrogramNoiseReduction.medianFilter.MedianFilterParams#filterLength",
        },
        {
            "/noiseReduction/averageSubtraction",
            "specNoiseSettings.runMethod[1]",
            "false",
            {},
            "bare SpectrogramNoiseSettings has a null runMethod array",
            "spectrogramNoiseReduction.SpectrogramNoiseSettings#isRunMethod",
        },
        {
            "/noiseReduction/updateConstant",
            "specNoiseSettings.methodSettings[1].updateConstant",
            "0.02",
            {},
            "methodSettings is initially empty; the owned process supplies its method default",
            "spectrogramNoiseReduction.averageSubtraction.AverageSubtractionParameters#updateConstant",
        },
        {
            "/noiseReduction/kernelSmoothing",
            "specNoiseSettings.runMethod[2]",
            "false",
            {},
            "bare SpectrogramNoiseSettings has a null runMethod array",
            "spectrogramNoiseReduction.SpectrogramNoiseSettings#isRunMethod",
        },
        {
            "/noiseReduction/threshold",
            "specNoiseSettings.runMethod[3]",
            "false",
            {},
            "bare SpectrogramNoiseSettings has a null runMethod array",
            "spectrogramNoiseReduction.SpectrogramNoiseSettings#isRunMethod",
        },
        {
            "/noiseReduction/thresholdDb",
            "specNoiseSettings.methodSettings[3].thresholdDB",
            "8",
            {},
            "methodSettings is initially empty; the owned process supplies its method default",
            "spectrogramNoiseReduction.threshold.ThresholdParams#thresholdDB",
        },
        {
            "/noiseReduction/finalOutput",
            "specNoiseSettings.methodSettings[3].finalOutput",
            "2",
            {},
            "Java SpectrogramThreshold.OUTPUT_RAW",
            "spectrogramNoiseReduction.threshold.ThresholdParams#finalOutput",
        },
    };
}

} // namespace

ControlledUnitDescriptor
make_whistle_moan_controlled_unit_descriptor() {
    return {
        "pamguard.whistles-moans",
        1,
        {
            "Whistle and Moan Detector",
            "Detectors",
            "whistlesAndMoans.WhistleMoanControl",
            "direct",
            "Searches for tonal noises. Measures bearings and locations of source. Replaces older Whistle Detector",
            "detectors/whistleMoanHelp/docs/whistleMoan_Overview.html",
            {
                "src/PamModel/PamModel.java",
                "src/whistlesAndMoans/WhistleMoanControl.java",
                "src/whistlesAndMoans/WhistleToneParameters.java",
                "src/whistlesAndMoans/WhistleToneDialog.java",
                "src/whistlesAndMoans/layoutFX/WhistleMoanSettingsPaneFX.java",
                "src/spectrogramNoiseReduction/SpectrogramNoiseProcess.java",
                "src/spectrogramNoiseReduction/SpectrogramNoiseSettings.java",
                "src/whistlesAndMoans/WhistleToneConnectProcess.java",
                "src/whistlesAndMoans/RejoiningFragmenter.java",
                "src/whistlesAndMoans/StubRemover.java",
            },
        },
        unlimited_in_all_modes(),
        {
            fft_input(),
            fft_output(),
            contour_output(),
        },
        {
            1,
            {
                "whistlesAndMoans.WhistleToneParameters",
                "PamView.GroupedSourceParameters",
                "spectrogramNoiseReduction.SpectrogramNoiseSettings",
                "spectrogramNoiseReduction.medianFilter.MedianFilterParams",
                "spectrogramNoiseReduction.averageSubtraction.AverageSubtractionParameters",
                "spectrogramNoiseReduction.threshold.ThresholdParams",
            },
            {
                "src/whistlesAndMoans/WhistleMoanControl.java",
                "src/whistlesAndMoans/WhistleToneParameters.java",
                "src/whistlesAndMoans/WhistleToneDialog.java",
                "src/whistlesAndMoans/layoutFX/WhistleMoanSettingsPaneFX.java",
                "src/PamView/GroupedSourceParameters.java",
                "src/PamView/dialog/GroupedSourcePanel.java",
                "src/spectrogramNoiseReduction/SpectrogramNoiseSettings.java",
                "src/spectrogramNoiseReduction/SpectrogramNoiseProcess.java",
                "src/spectrogramNoiseReduction/SpectrogramNoiseDialogPanel.java",
                "src/whistlesAndMoans/WhistleToneConnectProcess.java",
            },
            core::whistle_moan_default_settings_json(),
            {
                {
                    "settings.tabs",
                    {
                        "Detection",
                        "Noise and Thresholding",
                    },
                },
                {
                    "settings.detection",
                    {
                        "Source of FFT data",
                        "Channel/Sequence list and grouping",
                        "Connections",
                        "Min Frequency",
                        "Max Frequency",
                        "Connection Type",
                        "Minimum length",
                        "Minimum total size",
                        "Shape 'stubs'",
                        "Crossing and Joining",
                        "Max Cross length",
                    },
                },
                {
                    "settings.noise-methods",
                    {
                        "Median Filter",
                        "Average Subtraction",
                        "Gaussian Kernel Smoothing",
                        "Thresholding",
                    },
                },
            },
            default_evidence(),
            SettingsChangePolicy::StopRequired,
            "not-claimed",
            std::string(
                core::whistle_moan_settings_schema_json()),
        },
        {
            1,
            {
                {
                    "noise-reduction",
                    "pamguard.spectrogram-noise",
                    {
                        "",
                        "pamguard.whistle-noise-settings.v1",
                    },
                    true,
                    AvailabilityStatus::Available,
                    "java-fixture-validated",
                },
                {
                    "contour-connect",
                    "pamguard.whistles-moans",
                    {
                        "",
                        "pamguard.whistle-contour-settings.v1",
                    },
                    true,
                    AvailabilityStatus::Available,
                    "java-fixture-validated",
                },
            },
            {
                {
                    "fft",
                    {
                        "noise-reduction",
                        "input",
                    },
                },
                {
                    "noiseReducedFft",
                    {
                        "noise-reduction",
                        "output",
                    },
                },
                {
                    "contours",
                    {
                        "contour-connect",
                        "contours",
                    },
                },
            },
            {
                {
                    "noise-to-contours",
                    {
                        "noise-reduction",
                        "output",
                    },
                    {
                        "contour-connect",
                        "input",
                    },
                },
            },
            {},
            "pamguard.whistles-moans.runtime",
        },
        AvailabilityStatus::Available,
        "partial",
    };
}

} // namespace pamguard::project
