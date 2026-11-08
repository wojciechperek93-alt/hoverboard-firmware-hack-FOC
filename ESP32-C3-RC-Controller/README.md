# ESP32-C3 RC Controller for two MM32SPIN05PF splitboards

This is a separate PlatformIO application. It does not modify the existing STM32F103/EFeru firmware.

## Current implementation stage

- MM32 Gen2 addressed UART command type 0 encoder (8-byte frame).
- MM32 telemetry stream parser (15-byte frame, sync 0xABCD).
- CRC-16/CCITT implementation (polynomial 0x1021, initial value 0).
- Native regression tests for CRC, command layout, bounds checking and telemetry decoding.

## Verified protocol facts

The implementation is based on `AILIFE-4798/Hoverboard-Firmware-Hack-Gen2.x-MM32`, especially `HoverBoardMindMotion/Src/remoteUartBus.c`.

- UART: 8 data bits, no parity, 1 stop bit; firmware template default baud is 19200.
- Command type 0: 8 bytes: `'/'`, type, destination slave ID, signed speed (little-endian), state byte, CRC (little-endian).
- Telemetry: 15 bytes: start word `0xABCD` (wire bytes `CD AB`), source slave ID, speed, voltage, current, odometer, CRC.
- CRC-16/CCITT: polynomial `0x1021`, initial value `0x0000`.
- Firmware telemetry fields are raw/scaled values; conversion to display units must be checked against the exact target build.

## Important limitations — not yet hardware validated

- This repository is an EFeru STM32F103 firmware fork; it is not the MM32 Gen2 firmware repository. The ESP32 controller is intentionally isolated in this subdirectory.
- Do not connect the TX outputs of two splitboards together until the actual electrical bus topology is confirmed. The final design may need separate RX paths or a bus selector.
- GPIO assignment, UART topology, board-specific pin restrictions, receiver wiring and voltage levels are not finalized. ESP32-C3 GPIOs are not 5 V tolerant.
- No motor command is sent by the current firmware scaffold. RC decoding, arming/failsafe, dual-controller scheduling and physical emergency stop are still to be implemented.
- A valid CRC/parser test is not proof of electrical compatibility or safe motor operation.

## Build and test

Install PlatformIO Core, then run from this directory:

```sh
pio test -e native
pio run -e esp32c3
```

Tests must be run and their output recorded before claiming they pass. Physical validation requires the actual ESP32-C3 SuperMini and both MM32 splitboards with the motors safely unloaded.

## Planned modules

1. Protocol codec and per-controller telemetry.
2. RC receiver backends: PWM, PPM, iBUS and SBUS with explicit pin/configuration and validated frame detection.
3. Channel mapping, deadband, calibration and differential mixing.
4. Safety state machine: STARTUP, DISARMED, ARMING, ARMED, FAILSAFE, ERROR.
5. Independent communication timeout for each MM32; loss of either controller stops both and requires deliberate re-arm.
6. Non-blocking control loop, watchdog, diagnostics and configuration.
7. Wiring guide and staged bench tests.
