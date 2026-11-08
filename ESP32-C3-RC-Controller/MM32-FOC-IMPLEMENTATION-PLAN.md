# MM32 FOC implementation path — engineering specification

Status: design specification from source inspection. This is not a firmware patch and must not be flashed.

Upstream source inspected at commit:
`AILIFE-4798/Hoverboard-Firmware-Hack-Gen2.x-MM32`
`3354fcbe8b621d493576b19de77f1da6d9b2388b`

Target: MM32SPIN05PF splitboard motor controller. Keep the original COM/SINE modes working while FOC is brought up behind an explicit experimental build/config gate.

## 1. Why FOC modes are not ready in this source revision

### 1.1 The mode selector exposes modes that the UART config cannot select

`bldc.h` declares:
- 0 COM_VOLT
- 1 COM_SPEED
- 2 SINE_VOLT
- 3 SINE_SPEED
- 4 FOC_VOLT
- 5 FOC_SPEED
- 6 FOC_TORQUE

But `remoteUartBus.c` accepts a config mode only when `iDriveMode <= SINE_SPEED`. Modes 4–6 are therefore rejected over the documented config packet.

### 1.2 The outer control loop never generates FOC references

`speedupdate()` only calculates `pwm` for modes 0–3. It then sets `abspwm = abs(pwm)`. For modes 4–6, `pwm` can retain an old value or remain zero. There is no FOC mode-specific assignment to:
- `Speed.qInRef`
- `Speed.qInMeas`
- `Speed.qOut`

### 1.3 The ADC ISR consumes a speed PI output that is never updated

`interrupt.c` assigns `CurIQ.qInRef = Speed.qOut`, then executes the q-axis current PI. Search of the inspected source finds no writer to `Speed.qOut` except initialization. Consequently the FOC q-axis reference remains zero in the current source path.

### 1.4 Torque current PI is one-sided

`InitPI()` configures `CurIQ.qOutMin = 0` and `qOutMax = 30000`. That may be intentional for a unidirectional torque convention, but it is not sufficient evidence for signed forward/reverse torque control. Do not simply change the minimum to -30000: first establish how `CMDDIR`, Hall angle progression, phase-current polarity and q-axis sign interact.

### 1.5 FOC_VOLT and FOC_TORQUE semantics are undefined

The code treats all FOC modes identically in the current-control ISR, but does not define a distinct outer reference path for voltage, speed, or torque. Mode names alone are not a control specification.

## 2. Required control architecture

Use a cascaded structure with explicit units and rates:

1. **Command layer (UART / RC):** signed normalized request, validated range, timeout and disable state.
2. **Command conditioning (1 kHz):** clamp, slew/ramp limit, direction/reversal policy, zero-speed reset.
3. **Speed feedback:** signed RPM derived from Hall timing, with validity/timeout and bounded filtering.
4. **Outer speed PI (1 kHz):** target RPM minus measured RPM -> signed q-axis current reference in a documented current unit.
5. **Inner d/q current PI (ADC interrupt):** d-axis reference normally zero; q-axis reference from the outer loop; measured d/q currents -> voltage vector.
6. **Inverse Park + SVPWM:** angle, saturation, duty update and hardware break protection.

The ADC interrupt must not call the 1 kHz outer speed controller. It should consume a stable, atomically published reference. The outer loop must not modify current PI state.

## 3. Resolve sign conventions before enabling reverse

Create a bench-only sign verification procedure:
- Lift wheel clear and use a current-limited supply.
- With gate drive disabled, inspect Hall sequence for manually rotated wheel in both directions.
- Verify phase-current sensor polarity and Clarke/Park sign using low-energy controlled tests.
- Verify Hall electrical angle table and direction offset.
- Apply a very small positive q reference and record wheel direction/current.
- Repeat only after complete stop for negative q reference.
- Confirm sign consistency across both physical splitboards.

Do not infer q-axis sign from `dir` alone. The code sets `HALL1.CMDDIR = -dir` in the ADC interrupt, while Hall speed direction is separately inferred from the Hall transition table.

## 4. Speed feedback requirements

Current Hall code computes speed from the sum of six Hall intervals and uses `SPEEDX / HallTimeSum`. Before using it as FOC feedback:
- verify timer clock/prescaler and units for both supported target builds;
- guard zero/overflow/invalid HallTimeSum;
- reject illegal Hall states and impossible transitions;
- preserve sign consistently;
- set speed to zero on a defined Hall timeout;
- avoid a moving average that adds excessive delay to the speed loop.

Use the existing signed `realspeed` only after confirming its RPM scale from the timer configuration and a tachometer. Keep filtered speed for display/telemetry separate from the feedback signal used by the controller.

## 5. PI controller design

Do not reuse the legacy `PID()` gains for FOC.

Implement a dedicated speed PI with:
- explicit fixed-point scale and documented units;
- output clamp to a measured safe q-current reference;
- conditional integration / anti-windup when output saturates;
- reset on disarm, command timeout, mode change, direction change and fault;
- bounded command ramp;
- deterministic 1 kHz update period (or measured actual dt);
- no floating point in the ADC interrupt.

The inner d/q PI should also have saturation-aware anti-windup. The current source uses fixed-point coefficients; preserve the arithmetic format and prove bounds for intermediate products and shifts before changing gains.

Initial PI gains should be conservative and derived from bench response, not guessed from the current constants. Expose gains as build-time or EEPROM parameters only after range checks and versioned configuration.

## 6. Mode definitions to implement

Keep legacy modes 0–3 unchanged during the first FOC work.

Recommended first FOC release:
- FOC_VOLT: explicitly defined open-loop q-axis voltage request with current limiting. This is a commissioning mode only, not a riding mode.
- FOC_TORQUE: signed q-axis current request with d-axis reference fixed at zero, current limits and verified direction sign.
- FOC_SPEED: outer speed PI -> signed q-axis current reference -> inner current PI.

Do not enable FOC_SPEED until FOC_TORQUE has passed sign, current scaling and low-speed tests. Do not expose FOC_VOLT as a normal user/riding mode.

If source maintainers intended different semantics, document that and adapt the names rather than silently changing behavior.

## 7. Safety gates

FOC may be selected only if:
- target MCU/build is confirmed as MM32SPIN05PF;
- current-sensor offset and gain are calibrated;
- ADC channels and phase mapping are confirmed;
- Hall sequence and angle table are validated;
- hardware break / ADC watchdog has been bench-tested;
- DC current and phase current are independently measured;
- temperature monitoring or conservative timed test limits are in place;
- command timeout stops torque and clears PI integrators;
- invalid Hall state, overcurrent, undervoltage, overtemperature (if available), watchdog or UART loss enters a latched fault requiring deliberate re-arm.

Do not increase `SOFT_ILIMIT`, `AWDG`, PWM duty, or speed ceiling as part of FOC bring-up.

## 8. Required regression and hardware tests

### Host/unit tests
- signed speed PI: positive and negative error;
- output and integrator saturation;
- anti-windup recovery;
- zero target resets/decays output according to defined policy;
- timeout and fault reset;
- command ramp and reversal interlock;
- Hall RPM conversion including zero, maximum, timeout and overflow;
- q-current sign mapping.

### Firmware integration tests
- modes 0–3 behavior unchanged;
- config rejects unsupported modes and accepts only compiled-in FOC modes;
- no stale `pwm` dependency in FOC path;
- outer PI executes at fixed rate and is not called in ADC ISR;
- q-current reference is updated only by the outer loop;
- all faults force safe PWM state and reset references.

### Hardware test sequence
1. Gate-driver disabled: inspect ADC offsets, Hall state, UART and timer rates.
2. Motor unloaded: verify phase order and Hall angle at very low command.
3. FOC_VOLT commissioning at minimal amplitude with current limit.
4. FOC_TORQUE positive then negative at very low current.
5. Speed PI at low target with wheel unloaded.
6. Thermal/current tests with short runs and cool-down.
7. Only after all previous steps: controlled low-speed ridden test in a protected area.

Record firmware hash, EEPROM dump, supply voltage, current, temperature, requested/actual RPM and fault flags for every test.

## 9. Patch order

1. Add compile-time FOC feature gate and explicit mode availability.
2. Fix Hall feedback validity/sign and unit tests.
3. Add fixed-rate speed PI reference update and atomic handoff to ADC ISR.
4. Add signed torque-current reference with proven sign convention.
5. Add anti-windup and reset/fault handling.
6. Add validated UART config support for modes 4–6 only when compiled in.
7. Run host and target builds; attach build logs and test reports.
8. Hardware validation before recommending any tuning values.

## 10. Explicit non-goals for first patch

- No increase to current limits or PWM limits.
- No automatic EEPROM rewrite of either board.
- No changing the legacy COM/SINE defaults.
- No assumption that both splitboards have identical sensor calibration.
- No claim of safe riding operation based on compilation or host tests alone.
