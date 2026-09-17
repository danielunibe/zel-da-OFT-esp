# Task 03.6 — Display Target Validation

## Read-only source investigation

The existing DXGI path already supports monitor selection through stored window coordinates; no new selector or source change was required.

- `libultraship/src/fast/backends/gfx_dxgi.cpp`: `GetMonitorList()` enumerates Windows monitors by bounds and primary flag.
- `GetMonitorAtCoords(...)`: selects the monitor containing the configured window coordinates, with a primary-monitor fallback.
- `Window.PositionX` / `Window.PositionY`: configuration keys read during window initialization and fullscreen transitions.
- `ToggleBorderlessWindowFullScreen(true)`: uses the selected monitor's bounds and sets the borderless fullscreen rectangle to that monitor.
- `WM_MOVE` / `WM_SIZE`: refresh the selected monitor and detected refresh period when the window moves or changes size.

## Build-test change

Before the change, `runtime/build-test/shipofharkinian.json` used `Window.PositionX=100`, `Window.PositionY=100`, which selected the primary 2560×1440 display. A byte-for-byte backup was created at:

`runtime/build-test/shipofharkinian.TASK03_6_BEFORE_DISPLAY_FIX.json`

The build-test-only change set `Window.PositionX=2560` and `Window.PositionY=0`, based on dynamically detected bounds for `\\.\DISPLAY5` (`X=2560`, `Y=0`, `3440×1440`).

## Observed result

The isolated `runtime/build-test/soh.exe` opened responsively on `\\.\DISPLAY5` with window rectangle `(2560,0)-(6000,1440)`. The effective fullscreen mode was 3440×1440, aspect ratio 2.3889:1. Windows reported the target LG ultrawide at 85 Hz during the validation.

The primary display was not changed globally. The live runtime was not launched or modified.

`MatchRefreshRate` remained enabled. The display-selection path updates its detected monitor and refresh period from the window's selected monitor; no simulation setting was changed and `InterpolationFPS=170` remains stored as fallback configuration.
