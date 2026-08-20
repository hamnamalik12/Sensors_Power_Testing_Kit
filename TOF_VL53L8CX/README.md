# VL53L8CX ToF Sensor — ESP32-S3 Test Firmware

This repository contains a set of test builds for driving the ST **VL53L8CX**
Time-of-Flight sensor from a **Seeed XIAO ESP32-S3**, sweeping through
different resolutions, ranging frequencies, and integration times. Each
variant was flashed individually and its current draw was captured on a
**Nordic Semiconductor Power Profiler Kit II (PPK2)** in ampere-meter mode,
powering the board directly from the PPK2 while ranging.

## Hardware / Wiring

| Signal      | XIAO ESP32-S3 Pin | Notes                          |
|-------------|--------------------|---------------------------------|
| I2C SDA     | GPIO5              | 400 kHz, internal pull-ups on   |
| I2C SCL     | GPIO6              | 400 kHz, internal pull-ups on   |
| LPn         | GPIO4 (D3)         | Held low at boot, driven high to enable the sensor |
| I2C Address | 0x29               | Default VL53L8CX address        |

All variants follow the same bring-up sequence: configure the LPn GPIO →
hold the sensor in reset → initialize the I2C bus → drive LPn high to wake
the sensor → check `vl53l8cx_is_alive` → `vl53l8cx_init` (firmware upload,
~1–2 s) → configure resolution/frequency/integration time → start ranging.

## Power Measurement Method

- Instrument: **Nordic PPK2**, source/ammeter mode, board powered directly
  from the PPK2 supply rail.
- Measurement taken during **steady-state continuous ranging** (after
  firmware upload/init settles), not during boot or firmware upload.
- Values below are taken from the in-code comments left by whoever ran each
  test — treat them as bench measurements from that specific run, not
  datasheet guarantees.

## Test File Summary

| File               | Resolution        | Ranging Frequency | Integration Time | Behavior                                                        | Measured Current (PPK2) |
|---------------------|-------------------|--------------------|-------------------|-------------------------------------------------------------------|--------------------------|
| `TEST1.c`          | Real 8×8, software-downsampled to 1×1…8×8 | 15 Hz (max for 8×8) | Default (~3000 cm max distance mode) | Sensor always ranges at true 8×8; firmware block-averages the 64 zones down to virtual grids from 1×1 to 8×8, running each virtual grid size for 20 s before powering down via LPn | **75 mA** (native 8×8 ranging; downsampling is done in software after the fact, so current matches full 8×8 mode) |
| `TEST2.c`          | 4×4 → 8×8 (sequential, one pass) | 15 Hz for both phases | Default | Runs 4×4 for 20 s, then switches to 8×8 for 20 s, then stops ranging and powers the sensor down via LPn permanently | **25 mA** during 4×4 phase, **75 mA** during 8×8 phase |
| `TEST3.c`          | 4×4 → 8×8 → power-down (repeating cycle) | 15 Hz for both phases | Default | Same 4×4/8×8 sequence as TEST2, but loops forever: after the 8×8 phase it drives LPn low to fully power down the sensor for 20 s, then wakes and re-initializes (firmware re-upload) for the next cycle | **25 mA** (4×4), **75 mA** (8×8), **325 µA** during the LPn-low power-down phase |
| `TEST4.c`          | 4×4                | 5 Hz               | 5 ms              | Continuous 4×4 ranging (16 zones/frame) printed indefinitely over serial | Not recorded in comments |
| `TEST5.c`          | 8×8                | 5 Hz               | 5 ms              | Continuous 8×8 ranging (64 zones/frame) printed indefinitely over serial | Not recorded in comments |
| `TEST6.c`          | 8×8                | 15 Hz (max for 8×8) | Default           | Continuous 8×8 ranging (64 zones/frame) at the maximum frequency the sensor supports in this mode | Not recorded in comments |
| `tof_VL53L8CX.c`   | 8×8                | 5 Hz               | 5 ms              | Continuous 8×8 ranging (64 zones/frame), same config as TEST5 — appears to be the reference/baseline build | **17 mA** |

## Key Takeaways from the PPK2 Measurements

- **Resolution dominates current draw**: 4×4 mode measured around **25 mA**
  vs. **75 mA** for 8×8 mode at the same 15 Hz ranging frequency — roughly a
  3× increase for 4× the zones.
- **Reducing integration time and frequency matters a lot**: dropping to a
  5 ms integration time and 5 Hz ranging frequency in 8×8 mode
  (`tof_VL53L8CX.c`) brought current down to **17 mA**, well below the
  **75 mA** measured for 8×8 mode at 15 Hz / default integration time
  (`TEST1`–`TEST3`). This is the same underlying trade-off used in
  `TEST4.c`/`TEST5.c`, which were not measured on the PPK2 but should sit in
  a similar band since they use the same reduced timing.
- **LPn-controlled shutdown is very effective**: driving LPn low between
  duty cycles (`TEST3.c`) drops consumption to **325 µA**, at the cost of a
  full firmware re-upload (~1–2 s) on every wake.
- **8×8 "software downsample" (`TEST1.c`) does not save power**: since the
  sensor is always physically ranging at native 8×8, the current stays at
  **75 mA** regardless of the virtual grid size being reported — the
  downsampling only reduces the amount of data printed, not the sensor's
  power draw.

## Suggested Use

- Use `tof_VL53L8CX.c` (or `TEST4.c`/`TEST5.c`) as the low-power continuous
  baseline when battery life matters and 5 Hz update rate is acceptable.
- Use `TEST6.c` when the full 15 Hz / 8×8 update rate is needed and power is
  not a constraint.
- Use `TEST3.c`'s duty-cycled pattern (range → range → power down) for
  battery-powered deployments that only need periodic snapshots.
- Use TEST.c files in main to run all these codes...