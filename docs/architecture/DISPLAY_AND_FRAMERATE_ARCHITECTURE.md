# Display and framerate architecture — Shipwright 9.1.1

## Current model

The original game simulation remains tied to its native timing model. Shipwright adds frame interpolation and a display/window layer. `OTRGlobals::GetInterpolationFPS` chooses the interpolation target:

1. If `gSettings.MatchRefreshRate` is enabled, use `Window->GetCurrentRefreshRate()`.
2. Otherwise, when VSync is enabled or cannot be disabled, clamp the configured interpolation FPS to the current refresh rate.
3. Otherwise, use `gSettings.InterpolationFPS` directly.

The UI for these settings is in `soh/soh/SohGui/SohMenuSettings.cpp`. VSync, Match Refresh Rate and Interpolation FPS are separate controls. `CVAR_MSAA_VALUE` is passed to `Window->SetMsaaLevel` and has a UI range of 1–8.

```text
window backend -> active monitor refresh rate
              -> OTRGlobals::GetInterpolationFPS
              -> frame interpolation target
              -> rendered presentation
```

`Fast3dWindow.cpp` delegates refresh-rate, VSync capability and MSAA operations to the active backend. The Windows path uses the DirectX 11 backend by default according to the project README/build configuration; SDL2 remains part of the window/input integration.

## Couch display implications

`MatchRefreshRate` is the right existing setting for a TV/monitor couch setup, but it is not a simulation-rate switch. A 120 Hz display does not turn the game simulation into a 120 Hz simulation; it changes the interpolation target. The active refresh rate is queried through the window backend, so monitor changes and fullscreen transitions should be part of later acceptance testing.

MSAA is a renderer setting, not a texture or message setting. VSync controls presentation pacing. These must be tested separately because a successful build cannot certify frame pacing or visual quality.

## Relevant files

- `soh/soh/OTRGlobals.cpp`
- `soh/soh/SohGui/SohMenuSettings.cpp`
- `soh/soh/frame_interpolation.cpp`
- `soh/soh/config/ConfigMigrators.h`
- `libultraship/include/ship/window/Window.h`
- `libultraship/src/fast/Fast3dWindow.cpp`
- `libultraship/src/fast/backends/`

