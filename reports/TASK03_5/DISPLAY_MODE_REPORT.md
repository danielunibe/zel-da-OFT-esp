# Task 03.5 — Display Mode Report

Windows display enumeration at validation time reported:

| Display | Bounds | Current mode | Refresh |
|---|---:|---:|---:|
| `\\.\DISPLAY1` primary | logical 1707×960 | 2560×1440 | 240 Hz |
| `\\.\DISPLAY5` LG ULTRAWIDE(HDMI) | 3440×1440 | 3440×1440 | 85 Hz |

The isolated Shipwright process opened fullscreen on the primary display. Its window rectangle was logical `(0,0)-(1707,960)`, corresponding to the primary display's 2560×1440 mode at the active DPI scale. The runtime did not use the ultrawide display during this test.

Results:

- `WINDOWS_DESKTOP_RESOLUTION`: 2560×1440 on the primary display; ultrawide secondary is 3440×1440.
- `WINDOWS_ACTIVE_REFRESH_HZ`: 240 Hz on the primary display; 85 Hz on the ultrawide secondary.
- `SHIPWRIGHT_EFFECTIVE_RESOLUTION`: 2560×1440 fullscreen on primary.
- `SHIPWRIGHT_EFFECTIVE_REFRESH_HZ`: 240 Hz, inherited from the primary display mode.
- `ULTRAWIDE_EFFECTIVE`: NO for this run.
- `EFFECTIVE_ASPECT_RATIO`: 16:9 (1.7778).
- `CONFIG_RESOLUTION_FIELDS_AUTHORITATIVE`: PARTIAL. Fullscreen selected the primary display mode; windowed dimensions were not authoritative for this run.

No resolution or display configuration was changed.
