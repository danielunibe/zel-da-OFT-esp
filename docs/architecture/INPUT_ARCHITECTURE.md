# Input architecture — Shipwright 9.1.1

## Current path

The current Windows runtime is SDL-based through libultraship. The controller path is:

```text
SDL gamepad
  -> ConnectedPhysicalDeviceManager::RefreshConnectedSDLGamepads
  -> Controller / ControllerStick / ControllerButton
  -> configured mapping objects
  -> N64 pad state consumed by SoH
```

The relevant source lives in `libultraship/src/ship/controller` and `libultraship/include/ship/controller`. `Controller.cpp` owns the controller device and creates the two sticks, buttons and rumble mapping. `ConnectedPhysicalDeviceManager.cpp` owns SDL device discovery, instance identity and connect/disconnect handling.

## Stick processing

`ControllerStick.cpp` is the authoritative place for stick normalization:

- `ControllerStick::Process` applies the configured deadzone and sensitivity.
- `ControllerStick::GetAxisDirectionValue` evaluates directional mapping values.
- `ControllerStick::UpdatePad` writes the resulting N64 stick values.
- `SetDeadzone`, `SetSensitivity` and `SetNotchSnapAngle` expose the current tuning surface.
- Defaults are 20% deadzone and 100% sensitivity; the implementation uses a circular scaled deadzone. There is no release hysteresis in this path.

The persistent configuration is under `gSettings.Controllers`, including `Port%d.LeftStick` and `Port%d.RightStick`, with directional mapping IDs, `DeadzonePercentage`, `SensitivityPercentage` and `NotchSnapAngle`.

`SDLAxisDirectionToAxisDirectionMapping.cpp` reads SDL axis values and normalizes the signed SDL range. `SDLAxisDirectionToButtonMapping.cpp` turns an axis direction into a digital button using the configured threshold. Its default threshold is 25%; it also uses separate stick and trigger threshold settings. Neither mapping currently provides a distinct press/release hysteresis band.

## Couch contract

The proposed contract is compatible with the existing mapping model:

| Physical control | N64/control target | Existing-config-only? |
|---|---|---|
| Left stick | Move / N64 stick | Yes |
| Right stick up/down/left/right | C-Up/C-Down/C-Left/C-Right | Yes, via `ButtonMappings` with `SDLAxisDirectionToButtonMapping` |
| A | A | Yes |
| B | B | Yes |
| LT | Z | Yes |
| LB | L | Yes |
| RB | R | Yes |
| Menu | Start | Yes |
| X, Y, RT, View, L3, R3, D-pad | Reserved | Yes, by leaving unmapped |

This is a mapping/configuration task, not a new input backend. The current runtime JSON already contains right-stick axis mappings and button mappings, so the first implementation experiment should be a versioned configuration fixture, not source modification.

## Risk and boundary

The current code has no hysteresis, no radial response curve beyond deadzone/sensitivity, and no guarantee that every SDL controller exposes the same axis ordering. A couch profile must therefore identify the controller by SDL mapping/name, keep a reset path, and test reconnects. Do not treat a saved JSON mapping as portable across unrelated gamepads without verification.

## Relevant files

- `libultraship/src/ship/controller/controldevice/controller/Controller.cpp`
- `libultraship/src/ship/controller/controldevice/controller/ControllerStick.cpp`
- `libultraship/src/ship/controller/controldevice/controller/mapping/sdl/SDLAxisDirectionToAxisDirectionMapping.cpp`
- `libultraship/src/ship/controller/controldevice/controller/mapping/sdl/SDLAxisDirectionToButtonMapping.cpp`
- `libultraship/src/ship/controller/controldevice/controller/mapping/factories/AxisDirectionMappingFactory.cpp`
- `libultraship/src/ship/controller/physicaldevice/ConnectedPhysicalDeviceManager.cpp`
- `soh/soh/Enhancements/controls/SohInputEditorWindow.cpp`

