#pragma once
#include "Types.h"

// Hall sensor: TI DRV5053, analog bipolar, both the OA and PA variants were
// ordered (2026-11-10); the one-square prototype decides which one stays.
// The variant in use is chosen once, in lib/hw/Pins.h (pins::HALL_SENSOR).
enum class Drv5053 : uint8_t { OA, PA };

namespace hallcal {

// DRV5053 values from the TI datasheet (SLIS153D, Feb 2023), typical:
//  - output at B = 0 (VQ): 1.02 V (0.9 to 1.15 V); output range 0.2 V to ~1.8 V.
//  - sensitivity is negative: the output falls for a south pole and rises for
//    a north pole (with the sensor's top face towards the magnet; a sensor
//    mounted the other way round flips this, check with the rig's `hall`).
//  - OA: -11 mV/mT (-17.5 to -5), saturates at 73 mT, noise 5 mVpp.
//  - PA: -23 mV/mT (-35 to -10), saturates at 35 mT, noise 11 mVpp.
constexpr float VQ_MV = 1020.0f;
constexpr float OA_MV_PER_MT = -11.0f;
constexpr float PA_MV_PER_MT = -23.0f;

// PROVISIONAL detection field (not measured): a piece is detected at 6 mT and
// kept until the field drops below 3 mT. Replace with values from the
// one-square prototype (hardware/test-log.md).
constexpr float ENTER_MT = 6.0f;
constexpr float EXIT_MT = 3.0f;

// ESP32 ADC1, 12-bit, 11 dB attenuation (HallScanner::begin). Assumed linear
// model: mV = ADC_OFFSET_MV + counts * ADC_MV_PER_COUNT, i.e. the ESP-IDF v4.4
// esp_adc_cal (ESP32) characterization with the default Vref of 1100 mV
// (scale 196602/65536 * 1100/4096 = ~0.8056 mV per count, offset 142 mV).
// Each chip differs (eFuse Vref / two-point calibration, sensor VQ spread
// +-0.13 V), so HallScanner::calibrate() replaces `center` per square.
constexpr float ADC_OFFSET_MV = 142.0f;
constexpr float ADC_MV_PER_COUNT = 3300.0f / 4096.0f;

constexpr int roundToInt(float x) { return static_cast<int>(x < 0.0f ? x - 0.5f : x + 0.5f); }
// ADC reading for an input of mv millivolts (under the model above).
constexpr int countsAtMv(float mv) { return roundToInt((mv - ADC_OFFSET_MV) / ADC_MV_PER_COUNT); }
// ADC counts spanned by a change of dmv millivolts.
constexpr int countsForMv(float dmv) { return roundToInt(dmv / ADC_MV_PER_COUNT); }
// Sensitivity of a variant in mV/mT (negative).
constexpr float sensitivity(Drv5053 v) { return v == Drv5053::PA ? PA_MV_PER_MT : OA_MV_PER_MT; }
// Estimated field in mT from an output of mv millivolts, against the output
// zeroMv read with no magnet. Positive: S pole (machine), negative: N (human).
constexpr float fieldMt(float mv, float zeroMv, Drv5053 v) { return (mv - zeroMv) / sensitivity(v); }

}  // namespace hallcal

// Hall-sensor thresholds for one square, in raw ADC counts.
// The defaults are the DRV5053 OA preset (hallPreset(Drv5053::OA)), all
// PROVISIONAL: center from VQ typ, enter/exit from the provisional detection
// field. Measure on the one-square prototype and replace.
struct HallThresholds {
  int center = hallcal::countsAtMv(hallcal::VQ_MV);  // reading with no magnet (1090)
  // |raw - center| at or above this: a piece is present (82 for OA).
  int enter = hallcal::countsForMv(-hallcal::ENTER_MT * hallcal::OA_MV_PER_MT);
  // A detected piece is kept until |raw - center| drops below this (41 for OA).
  int exit = hallcal::countsForMv(-hallcal::EXIT_MT * hallcal::OA_MV_PER_MT);
  // true: S (machine) reads above center, N (human) below. DRV5053: false.
  bool southHigh = false;
};

// Provisional thresholds for a DRV5053 variant: center from VQ typ (1090),
// enter/exit from ENTER_MT/EXIT_MT times the variant's sensitivity
// (OA: 82/41 counts, PA: 171/86 counts), S below center.
constexpr HallThresholds hallPreset(Drv5053 v) {
  return HallThresholds{hallcal::countsAtMv(hallcal::VQ_MV),
                        hallcal::countsForMv(-hallcal::ENTER_MT * hallcal::sensitivity(v)),
                        hallcal::countsForMv(-hallcal::EXIT_MT * hallcal::sensitivity(v)), false};
}

// Classify one reading as EMPTY, HUMAN (N pole) or MACHINE (S pole), with
// hysteresis: a piece already detected in prev is kept while the reading
// stays on the same side of center and at least `exit` away from it.
Cell classify(int raw, Cell prev, const HallThresholds& t);
