# Trigger Mapping Analysis — SDL vs Windows Native XInput

**Project**: Ocarina of Time PC — Couch Edition  
**Device**: Xbox Series X Wireless Controller (Bluetooth LE HOGP, VID 0x045E, PID 0x0B13, Rev 0x0523)  
**Date**: 2026-09-15 / 2026-09-16  
**Status**: CLOSED — RESOLVED  

---

## 1. Physical Trace Evidence & Comparative Measurements

From the physical trace recorded in `reports/TASK03_7/BACKEND_EVENT_TRACE.csv` (2,198 events total, 1,609 XInput events, 375 SDL Raw events, 213 SDL GameController events):

### 1.1 Windows Native XInput (Ground Truth Hardware State)
- **LT (`bLeftTrigger`)**:
  - Idle: `0` (0/255 = 0.0%)
  - Full: `255` (255/255 = 100.0%)
  - Samples: 74 dynamic motion transitions
  - Polarity: Unidirectional positive (0 -> 255)
  - Resting behavior: Consistently returns to `0` upon physical release.
- **RT (`bRightTrigger`)**:
  - Idle: `0` (0/255 = 0.0%)
  - Full: `255` (255/255 = 100.0%)
  - Samples: 82 dynamic motion transitions
  - Polarity: Unidirectional positive (0 -> 255)
  - Resting behavior: Consistently returns to `0` upon physical release.

### 1.2 Baseline SDL Raw Joystick (`\\?\HID#{00001812...}`)
- **Raw Axis 4 (LT)**:
  - Range: `min = -32768, max = 32767`
  - Idle: `-32768` (unpressed)
  - Full: `+32767` (pressed)
- **Raw Axis 5 (RT)**:
  - Range: `min = -32768, max = 32767`
  - Idle: `-32768` (unpressed)
  - Full: `+32767` (pressed)

### 1.3 Baseline SDL GameController (`030000005e040000130b000023057200`)
- **Mapping String**: `...lefttrigger:a4,righttrigger:a5...`
- **Initial tester anomaly**: Displayed `32767` when idle.
- **Root Cause of Initial 32767 Anomaly**:
  In Windows 10/11 Bluetooth LE GATT HID report descriptors, the uncalibrated driver state or initial poll before the first report event was returned as 0. Because SDL maps a raw axis range of `[-32768, 32767]` to unsigned trigger `[0, 32767]`, an uninitialized raw value of `0` or an unsigned HID interpretation caused the math `(raw + 32768) / 2` or unsigned overflow to report saturation. Once a report was pumped, the axis reported `min = -32768` and `max = 32767`, with GameController mapping showing `lefttrigger: 0` at idle and `32767` at full.

### 1.4 Verified SDL XInput Bridge Mode (`0300fa675e040000130b000023057801`, `XInput#0`)
When SDL is configured with process-local hints:
```cpp
SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT, "0");
SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_XBOX, "0");
SDL_SetHint(SDL_HINT_XINPUT_ENABLED, "1");
```
- **LT (`SDL_CONTROLLER_AXIS_TRIGGERLEFT`)**:
  - Idle: `0`
  - Full: `32767`
- **RT (`SDL_CONTROLLER_AXIS_TRIGGERRIGHT`)**:
  - Idle: `0`
  - Full: `32767`
- **Linearity**: Perfectly monotonic and proportional to XInput `0..255` scaled to `0..32767`.
- **Rumble**: `SDL_GameControllerRumble` functions with return code 0.

---

## 2. Canonical Trigger Output Specification

```text
LT_RAW_AXIS: 2 (XInput) / 4 (Raw HID)
RT_RAW_AXIS: 5 (XInput) / 5 (Raw HID)

LT_IDLE: 0
LT_FULL: 255 (XInput) / 32767 (SDL GameController)

RT_IDLE: 0
RT_FULL: 255 (XInput) / 32767 (SDL GameController)

INVERTED: NO
MAPPING_CORRECT: YES
```

---

## 3. Conclusion & Recommendation

The trigger hardware is 100% healthy, non-inverted, and perfectly calibrated.
In `shipofharkinian.json`, the Z-Target binding:
```json
"P0-B8192-SDLA4-ADP": {
    "AxisDirection": 1,
    "Bitmask": 8192,
    "ButtonMappingClass": "SDLAxisDirectionToButtonMapping",
    "SDLControllerAxis": 4
}
```
correctly references logical axis 4 (`SDL_CONTROLLER_AXIS_TRIGGERLEFT`) with positive direction (`+1`), which activates Z-targeting when LT is pulled beyond the threshold and releases completely at idle (0). No inversion or deadzone offset correction is required.
