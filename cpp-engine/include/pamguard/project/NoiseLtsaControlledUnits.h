#pragma once

#include "pamguard/project/ControlledUnitRegistry.h"

namespace pamguard::project {

/**
 * Java-authoritative noiseMonitor.NoiseControl. The Java dataSource member is
 * represented by the public FFT binding.
 */
[[nodiscard]] ControlledUnitDescriptor
make_fft_noise_monitor_controlled_unit_descriptor();

/**
 * Java-authoritative noiseBandMonitor.NoiseBandControl. Plot preferences are
 * intentionally separate from its portable scientific settings.
 */
[[nodiscard]] ControlledUnitDescriptor
make_noise_band_monitor_controlled_unit_descriptor();

/**
 * Java-authoritative ltsa.LtsaControl. Although PamModel declares a
 * RawDataUnit dependency, the executable dialog/process consume FFT data, so
 * the public role follows the actual Java process.
 */
[[nodiscard]] ControlledUnitDescriptor
make_ltsa_controlled_unit_descriptor();

} // namespace pamguard::project
