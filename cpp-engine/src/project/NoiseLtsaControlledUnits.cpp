#include "pamguard/project/NoiseLtsaControlledUnits.h"

#include "pamguard/core/NoiseLtsaSettings.h"

#include <optional>
#include <utility>

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

PublicDataRoleDescriptor fft_input(
    std::string java_data_class =
        "fftManager.FFTDataUnit") {
    return {
        "fft",
        "FFT data source",
        DataRoleDirection::Input,
        "pamguard.fft",
        RoleCardinality::ExactlyOne,
        {"frequency-domain"},
        std::move(java_data_class),
        "pamguard.fft",
    };
}

PublicDataRoleDescriptor raw_audio_input() {
    return {
        "rawAudio",
        "Raw audio source",
        DataRoleDirection::Input,
        "pamguard.raw-audio",
        RoleCardinality::ExactlyOne,
        {"sampled"},
        "PamDetection.RawDataUnit",
        "pamguard.acquisition",
    };
}

PublicDataRoleDescriptor measurement_output(
    std::string id,
    std::string name,
    std::string data_type,
    std::vector<std::string> capabilities) {
    return {
        std::move(id),
        std::move(name),
        DataRoleDirection::Output,
        std::move(data_type),
        RoleCardinality::ExactlyOne,
        std::move(capabilities),
        {},
        std::nullopt,
    };
}

RuntimeExpansionRecipeDescriptor single_child_recipe(
    std::string recipe_id,
    std::string child_role,
    std::string runtime_type_id,
    std::string adapter_id,
    std::string public_input_role,
    std::string runtime_input_port,
    std::string public_output_role,
    std::string runtime_output_port) {
    const auto role = child_role;
    return {
        1,
        {
            {
                std::move(child_role),
                std::move(runtime_type_id),
                {
                    "",
                    std::move(adapter_id),
                },
                true,
                AvailabilityStatus::Available,
                "java-fixture-validated",
            },
        },
        {
            {
                std::move(public_input_role),
                {
                    role,
                    std::move(runtime_input_port),
                },
            },
            {
                std::move(public_output_role),
                {
                    role,
                    std::move(runtime_output_port),
                },
            },
        },
        {},
        {},
        std::move(recipe_id),
    };
}

} // namespace

ControlledUnitDescriptor
make_fft_noise_monitor_controlled_unit_descriptor() {
    return {
        "pamguard.fft-noise-monitor",
        1,
        {
            "Noise Monitor",
            "Sound Processing",
            "noiseMonitor.NoiseControl",
            "direct",
            "Measures noise in predefined frequency bands (e.g. third octave) using FFT data",
            "sound_processing.NoiseBands.Docs.NoiseBandsFFT",
            {
                "src/PamModel/PamModel.java",
                "src/noiseMonitor/NoiseControl.java",
                "src/noiseMonitor/NoiseSettings.java",
                "src/noiseMonitor/NoiseMeasurementBand.java",
                "src/noiseBandMonitor/BandType.java",
                "src/noiseMonitor/NoiseDialog.java",
                "src/noiseMonitor/UserBandDialog.java",
                "src/noiseMonitor/NoiseProcess.java",
                "src/noiseMonitor/NoiseDataBlock.java",
            },
        },
        unlimited_in_all_modes(),
        {
            fft_input(),
            measurement_output(
                "noiseMeasurements",
                "FFT-band noise measurements",
                "pamguard.fft-noise",
                {"measurements"}),
        },
        {
            1,
            {
                "noiseMonitor.NoiseSettings",
                "noiseMonitor.NoiseMeasurementBand",
                "noiseBandMonitor.BandType",
            },
            {
                "src/noiseMonitor/NoiseControl.java",
                "src/noiseMonitor/NoiseSettings.java",
                "src/noiseMonitor/NoiseMeasurementBand.java",
                "src/noiseBandMonitor/BandType.java",
                "src/noiseMonitor/NoiseDialog.java",
                "src/noiseMonitor/UserBandDialog.java",
                "src/noiseMonitor/NoiseProcess.java",
            },
            core::fft_noise_monitor_default_settings_json(),
            {
                {
                    "settings.dialog",
                    {
                        "FFT Data Source",
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
                        "Edit ...",
                    },
                },
                {
                    "settings.custom-band-dialog",
                    {
                        "Frequency Resolution",
                        "Name",
                        "Range",
                    },
                },
            },
            {
                {
                    "/channelBitmap",
                    "channelBitmap",
                    "1",
                    {},
                    {},
                    "noiseMonitor.NoiseSettings#channelBitmap",
                },
                {
                    "/measurementIntervalSeconds",
                    "measurementIntervalSeconds",
                    "60",
                    {},
                    {},
                    "noiseMonitor.NoiseSettings#measurementIntervalSeconds",
                },
                {
                    "/nMeasures",
                    "nMeasures",
                    "100",
                    {},
                    "ignored while useAll is true",
                    "noiseMonitor.NoiseSettings#nMeasures",
                },
                {
                    "/useAll",
                    "useAll",
                    "true",
                    {},
                    {},
                    "noiseMonitor.NoiseSettings#useAll",
                },
                {
                    "/bands",
                    "measurementBands",
                    "[]",
                    {},
                    "the Java constructor creates an empty list; at least one band is required before runtime",
                    "noiseMonitor.NoiseSettings#NoiseSettings",
                },
            },
            SettingsChangePolicy::StopRequired,
            "not-claimed",
            std::string(
                core::fft_noise_monitor_settings_schema_json()),
        },
        single_child_recipe(
            "pamguard.fft-noise-monitor.runtime",
            "noise-process",
            "pamguard.fft-noise-monitor",
            "pamguard.fft-noise-settings.v1",
            "fft",
            "input",
            "noiseMeasurements",
            "measurements"),
        AvailabilityStatus::Available,
        "partial",
    };
}

ControlledUnitDescriptor
make_noise_band_monitor_controlled_unit_descriptor() {
    return {
        "pamguard.noise-band-monitor",
        1,
        {
            "Noise Band Monitor",
            "Sound Processing",
            "noiseBandMonitor.NoiseBandControl",
            "direct",
            "Measure noise in octave, third octave, decade bands, etc. using filter banks",
            "sound_processing/NoiseBands/Docs/NoiseBands.html",
            {
                "src/PamModel/PamModel.java",
                "src/noiseBandMonitor/NoiseBandControl.java",
                "src/noiseBandMonitor/NoiseBandSettings.java",
                "src/noiseBandMonitor/NoiseBandDialog.java",
                "src/noiseBandMonitor/NoiseBandProcess.java",
                "src/noiseBandMonitor/BandType.java",
                "src/noiseBandMonitor/BandData.java",
                "src/Filters/FilterType.java",
            },
        },
        unlimited_in_all_modes(),
        {
            raw_audio_input(),
            measurement_output(
                "noiseBandMeasurements",
                "Filter-bank noise measurements",
                "pamguard.noise-band",
                {"measurements"}),
        },
        {
            1,
            {
                "noiseBandMonitor.NoiseBandSettings",
                "noiseBandMonitor.BandType",
                "Filters.FilterType",
            },
            {
                "src/noiseBandMonitor/NoiseBandControl.java",
                "src/noiseBandMonitor/NoiseBandSettings.java",
                "src/noiseBandMonitor/NoiseBandDialog.java",
                "src/noiseBandMonitor/NoiseBandProcess.java",
                "src/noiseBandMonitor/BandType.java",
                "src/noiseBandMonitor/BandData.java",
                "src/Filters/FilterType.java",
            },
            core::noise_band_monitor_default_settings_json(),
            {
                {
                    "settings.dialog",
                    {
                        "Raw Data Source",
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
                        "Filter Gamma",
                    },
                },
                {
                    "display.preferences",
                    {
                        "Log Scale",
                        "Show Grid",
                        "Show Decimators",
                        "Show ANSI standards",
                        "Class 0",
                        "Class 1",
                        "Class 2",
                    },
                },
            },
            {
                {
                    "/channelBitmap",
                    "channelMap",
                    "1",
                    {},
                    {},
                    "noiseBandMonitor.NoiseBandSettings#channelMap",
                },
                {
                    "/bandType",
                    "bandType",
                    R"("thirdOctave")",
                    {},
                    {},
                    "noiseBandMonitor.NoiseBandSettings#bandType",
                },
                {
                    "/filterType",
                    "filterType",
                    R"("butterworth")",
                    {},
                    {},
                    "noiseBandMonitor.NoiseBandSettings#filterType",
                },
                {
                    "/iirOrder",
                    "iirOrder",
                    "6",
                    {},
                    {},
                    "noiseBandMonitor.NoiseBandSettings#iirOrder",
                },
                {
                    "/firOrder",
                    "firOrder",
                    "7",
                    {},
                    {},
                    "noiseBandMonitor.NoiseBandSettings#firOrder",
                },
                {
                    "/firGamma",
                    "firGamma",
                    "2.5",
                    {},
                    {},
                    "noiseBandMonitor.NoiseBandSettings#firGamma",
                },
                {
                    "/outputIntervalSeconds",
                    "outputIntervalSeconds",
                    "10",
                    {},
                    {},
                    "noiseBandMonitor.NoiseBandSettings#outputIntervalSeconds",
                },
                {
                    "/minimumFrequencyHz",
                    "minFrequency",
                    "1.7925856629456591",
                    {},
                    "Java lazily derives this from endDecimation/startDecimation, bandType, and getMaxFrequency",
                    "noiseBandMonitor.NoiseBandSettings#getMinFrequency",
                },
                {
                    "/maximumFrequencyHz",
                    "maxFrequency",
                    "1133.6866687924667",
                    {},
                    "Java lazily derives this from highBandNumber 30 and bandType",
                    "noiseBandMonitor.NoiseBandSettings#getMaxFrequency",
                },
                {
                    "/referenceFrequencyHz",
                    "referenceFrequency",
                    "1000",
                    {},
                    {},
                    "noiseBandMonitor.NoiseBandSettings#getReferenceFrequency",
                },
            },
            SettingsChangePolicy::StopRequired,
            "not-claimed",
            std::string(
                core::noise_band_monitor_settings_schema_json()),
        },
        single_child_recipe(
            "pamguard.noise-band-monitor.runtime",
            "noise-band-process",
            "pamguard.noise-band-monitor",
            "pamguard.noise-band-settings.v1",
            "rawAudio",
            "input",
            "noiseBandMeasurements",
            "measurements"),
        AvailabilityStatus::Available,
        "partial",
    };
}

ControlledUnitDescriptor
make_ltsa_controlled_unit_descriptor() {
    return {
        "pamguard.ltsa",
        1,
        {
            "Long Term Spectral Average",
            "Sound Processing",
            "ltsa.LtsaControl",
            "direct",
            "Make Long Term Spectral Average Measurements",
            "sound_processing/LTSA/Docs/LTSA.html",
            {
                "src/PamModel/PamModel.java",
                "src/ltsa/LtsaControl.java",
                "src/ltsa/LtsaParameters.java",
                "src/ltsa/LtsaDialog.java",
                "src/ltsa/LtsaProcess.java",
                "src/ltsa/LtsaDataBlock.java",
                "src/ltsa/LtsaBinaryDataSource.java",
            },
        },
        unlimited_in_all_modes(),
        {
            // Preserve PamModel's declared dependency class as authority
            // metadata while following LtsaProcess's real FFT data type.
            fft_input("PamDetection.RawDataUnit"),
            measurement_output(
                "ltsa",
                "Long-term spectral averages",
                "pamguard.ltsa",
                {"frequency-domain", "measurements"}),
        },
        {
            1,
            {
                "ltsa.LtsaParameters",
            },
            {
                "src/ltsa/LtsaControl.java",
                "src/ltsa/LtsaParameters.java",
                "src/ltsa/LtsaDialog.java",
                "src/ltsa/LtsaProcess.java",
            },
            core::ltsa_default_settings_json(),
            {
                {
                    "settings.dialog",
                    {
                        "FFT Data source",
                        "Measurement",
                        "Measurement interval",
                    },
                },
                {
                    "settings.advanced",
                    {
                        "Longer average factor (persisted, dormant)",
                    },
                },
            },
            {
                {
                    "/channelBitmap",
                    "channelMap",
                    "0",
                    {},
                    "bare Java default before the operator selects channels",
                    "ltsa.LtsaParameters#channelMap",
                },
                {
                    "/intervalSeconds",
                    "intervalSeconds",
                    "60",
                    {},
                    {},
                    "ltsa.LtsaParameters#intervalSeconds",
                },
                {
                    "/longerFactor",
                    "longerFactor",
                    "10",
                    {},
                    "persisted by Java but its longer-average process/output is commented out",
                    "ltsa.LtsaParameters#longerFactor",
                },
            },
            SettingsChangePolicy::StopRequired,
            "not-claimed",
            std::string(core::ltsa_settings_schema_json()),
        },
        single_child_recipe(
            "pamguard.ltsa.runtime",
            "ltsa-process",
            "pamguard.ltsa",
            "pamguard.ltsa-settings.v1",
            "fft",
            "input",
            "ltsa",
            "ltsa"),
        AvailabilityStatus::Available,
        "partial",
    };
}

} // namespace pamguard::project
