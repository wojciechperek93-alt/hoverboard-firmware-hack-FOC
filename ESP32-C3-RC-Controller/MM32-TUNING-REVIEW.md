# MM32SPIN05PF Gen2 firmware — tuning review

This review is based on the public source repository:
`AILIFE-4798/Hoverboard-Firmware-Hack-Gen2.x-MM32`

It is analysis/documentation only. No MM32 motor-control firmware has been modified or flashed.

## 1. Baseline configuration found in source

The sample `pinstorage[64]` initializer in `HoverBoardMindMotion/Src/main.c` contains:

| Setting | Index | Sample value | Meaning |
|---|---:|---:|---|
| UART baud | 36 | 19200 | UART link rate |
| PWM_RES | 37 | 8192 | PWM timer half-period / resolution setting |
| SLAVE_ID | 38 | 1 | addressed controller ID |
| WINDINGS | 39 | 30 | used in hall-time-to-speed conversion |
| INVERT_LOWSIDE | 40 | 0 | low-side output polarity option |
| SOFT_ILIMIT | 41 | 10 | software current trip threshold in firmware's scaled units |
| AWDG | 42 | 300 | ADC watchdog threshold offset/setting |
| DRIVEMODE | 44 | 0 | COM_VOLT |
| BAT_FULL | 45 | 42000 | battery full threshold in mV-like scaled units |
| BAT_EMPTY | 46 | 32000 | battery empty threshold |
| SERIAL_TIMEOUT | 47 | 1000 | command timeout in ms |

These are source defaults, not a verified dump from either of the user's physical splitboards. Do not copy them over EEPROM blindly.

## 2. Drive modes — what the source actually does

Defined in `Inc/bldc.h`:
- 0 `COM_VOLT`
- 1 `COM_SPEED`
- 2 `SINE_VOLT`
- 3 `SINE_SPEED`
- 4 `FOC_VOLT`
- 5 `FOC_SPEED`
- 6 `FOC_TORQUE`

The UART config handler accepts modes only up to `SINE_SPEED` (`iDriveMode <= SINE_SPEED`). Therefore modes 4–6 cannot be selected through that config packet in this source version.

The code path for modes 0–3 is present in `bldc.c`. The FOC current-control path is in `interrupt.c`. However, this source needs additional validation before any FOC mode is recommended:
- `Speed.qOut` is consumed as the q-axis current reference (`CurIQ.qInRef = Speed.qOut`) in the ADC interrupt.
- The inspected `speedupdate()` implementation does not update `Speed.qOut` for `FOC_SPEED`.
- The UART config handler rejects mode values above `SINE_SPEED`.
- The FOC speed/torque command semantics are therefore incomplete or unreachable through the inspected control/config path. Do not select modes 4–6 as a tuning experiment without fixing and testing the full control path.

## 3. PID and scaling observations

Legacy outer speed controller in `Src/calculation.c`:
- `COM_SPEED`: Kp=50, Ki=100, Kd=200; output/integrator limits ±4000.
- `SINE_SPEED`: same gains; output/integrator limits ±40000.
- The outer PID is called from `speedupdate()`, which the main loop schedules at approximately 1 kHz.
- In speed modes, the result is passed through `PID2PWM()`; the scaling differs between commutation and sine paths.

FOC current controllers in `Src/PID.c`:
- d-axis: Kp=2048, Ki=50, output ±30000.
- q-axis: Kp=2048, Ki=50, output 0…30000.
- speed PI object: Kp=8000, Ki=50, output ±30000.
- These are fixed-point/Q15-style gains and must not be treated as ordinary floating-point PID gains.

The current code uses both a legacy outer PID and a separate FOC current PI structure. They are not interchangeable tuning knobs.

## 4. Safety-relevant findings

1. `SOFT_ILIMIT` trips PWM when measured DC current exceeds the configured threshold; the ADC watchdog can also generate a timer break. The exact physical current corresponding to these values depends on the board's shunt/amplifier/divider configuration. Measure and calculate before changing either limit.
2. `SERIAL_TIMEOUT` defaults to 1000 ms. On timeout, `RemoteUpdate()` sets the speed command to zero. For a ridden drift trike, 1 s is too long to treat as a dependable RC failsafe. The upstream controller should stop both boards much sooner, and a separate hardware kill path is recommended.
3. Each splitboard must have a unique `SLAVE_ID` if both share one addressed UART command stream. The sample initializer sets ID=1. The runtime config code has the slave-ID write disabled (`&& 0`), so do not assume an ordinary config frame can change it.
4. The parser in `serialit()` has no explicit `default` branch for unknown packet types, and `iReceivePos` is statically initialized to zero rather than the documented idle sentinel -1. This deserves a parser hardening patch and regression tests before relying on noisy/boot-time UART conditions.
5. The firmware transmits `hello World` at startup on the same UART. The ESP32 receiver must ignore text until it finds a complete telemetry frame with a valid CRC.
6. The command's `iSteer` member is parsed in packet type 1 but is not used in the shown receive handler; differential left/right speed mixing belongs in the ESP32 controller.

## 5. Tuning plan — do not change multiple variables at once

### Stage A — identify both boards
- Read and record firmware revision, MCU, board pinout/autodetect result, EEPROM settings and current sensor scaling separately for each board.
- Confirm each board's slave ID and UART routing electrically.
- Confirm battery voltage range and motor phase/current sensor calibration.

### Stage B — safe baseline
- Begin with wheels lifted, current-limited supply/battery protection, low command limit and a physical kill switch.
- Verify zero command, direction, hall sequence, telemetry, timeout and restart behavior.
- Verify measured DC current against an external meter before changing current limits.

### Stage C — select a mode
- Do not assume FOC modes 4–6 are usable in this source revision.
- Compare only documented/implemented modes 0–3 after verifying the actual compiled firmware and board support.
- For a first controlled test, prefer a conservative voltage/command limit; do not raise PWM_RES, current limits or speed limits together.

### Stage D — tune response
- Log requested speed, measured speed, PWM/current, battery voltage and fault state.
- Change one gain at a time, in small increments, with unloaded tests first.
- Check startup, reversal, zero-speed hold, braking/coasting, low battery and thermal behavior.
- Tune both boards independently before testing synchronized differential drive.

## 6. Next code work

Recommended next patch set in the MM32 source (as a fork/patch, not an unreviewed flash):
1. Add packet parser bounds/type validation and a deterministic idle/start state.
2. Add host-side tests for all 8/10/15-byte frames, CRC rejection, noise, partial frames and resynchronization.
3. Add an explicit, safe method to provision unique slave IDs.
4. Decide whether modes 4–6 are intentionally experimental; either complete the FOC speed/torque command path and config support or mark them unavailable.
5. Make RC-loss stop behavior independent of a 1000 ms default and validate coordinated stop of both boards.

No numerical gain or current-limit increase is recommended until board-specific measurements and the exact compiled firmware revision are known.
