// Dev-mode soil recalibration over the serial console.
//
// The buttonless cheap/budget node can't run the OLED+button calibration
// flow, so this offers a serial-command path for bench recalibration and for
// deriving the CARL_SOIL_FACTORY_*_ADC defaults. Commands (one per line):
//   CAL DRY    — capture the current averaged reading as the dry endpoint
//   CAL WET    — capture it as the wet endpoint
//   CAL SAVE   — persist the captured dry/wet to NVS (overrides factory cal)
//   CAL SHOW   — print live ADC + pending + stored calibration
//
// Entirely compiled out unless CONFIG_CARL_DEV_CAL=y — start() is then a no-op.

#pragma once

namespace carl::dev_cal {

// Spawn the console listener thread. No-op unless CONFIG_CARL_DEV_CAL.
void start();

}  // namespace carl::dev_cal
