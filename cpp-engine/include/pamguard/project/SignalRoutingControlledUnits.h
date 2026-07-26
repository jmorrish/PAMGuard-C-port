#pragma once

#include "pamguard/project/ControlledUnitRegistry.h"

namespace pamguard::project {

/**
 * Java-authoritative amplifier.AmpControl descriptor. The selected
 * RawDataUnit source is represented by the public rawAudio binding.
 */
[[nodiscard]] ControlledUnitDescriptor
make_signal_amplifier_controlled_unit_descriptor();

/**
 * Java-authoritative patchPanel.PatchPanelControl descriptor. Java's
 * dataSource is represented by the public rawAudio binding and the Swing-only
 * "Apply immediately" preference is deliberately not persisted.
 */
[[nodiscard]] ControlledUnitDescriptor
make_patch_panel_controlled_unit_descriptor();

} // namespace pamguard::project
