# Task 03.5 — Hardware / Display Validation Report

## Scope

Verify-only validation of `runtime/build-test`. The live project was not launched or modified. No source, gameplay, glyph, localization, renderer, haptics, or simulation implementation work was performed.

## Controller detection

Windows detected `Xbox Wireless Controller` over Bluetooth with an XINPUT HID interface. SDL 2.32.10 detected one GameController:

- Name: `Xbox Series X Controller`
- GUID: `030000005e040000130b000023057200`
- SDL mapping: available
- Axes: 6
- Buttons: 16
- Hats: 1
- Rumble capability: available

The complete probe metadata is in `SDL_CONTROLLER_EVENTS.csv`. No physical button or axis event was observed during the unattended probe window, so control-level validation remains `UNVERIFIED`.

## Mapping discrepancy and isolated correction

The prior build-test profile used SDL button indices 9, 10, and 6 for LB, RB, and Menu/Start. Shipwright consumes `SDL_GameControllerGetButton`, and this device's SDL mapping identifies those controls as LB 4, RB 5, and Start 7. The prior values therefore represented right-stick click, guide, and back rather than the requested physical controls.

A byte-for-byte backup was taken at `runtime/build-test/shipofharkinian.TASK03_5_BEFORE_MAPPING_FIX.json`. Only build-test configuration was corrected to 4, 5, and 7. The live runtime and source checkout were not touched. The detailed result is in `INPUT_MAPPING_RESULTS.csv`.

## Display and refresh

Windows exposes a 2560×1440/240 Hz primary display and a 3440×1440/85 Hz LG ultrawide secondary display. The isolated fullscreen Shipwright process opened on the primary display at 2560×1440, with a logical 1707×960 window rectangle caused by display scaling. Therefore the effective run was 16:9, not ultrawide. `MatchRefreshRate=1` remained enabled; no simulation rate was changed. See `DISPLAY_MODE_REPORT.md`.

## Saves and live safety

The three build-test save files matched their pre-test copies after the runtime launch. The live executable, resources, configuration, launcher files, and saves remain unchanged according to the Task 03 hash baseline. No live saves were replaced.

## Result

The controller and SDL capability gate is technically detected, but physical control events, stick drift/range, right-stick quadrants, in-game actions, and user-perceived rumble were not observed. The task therefore remains `PARTIAL`.
