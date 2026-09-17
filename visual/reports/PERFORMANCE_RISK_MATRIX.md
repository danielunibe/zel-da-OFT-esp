# Performance risk matrix

The target is smooth frame pacing at 3440x1440/high refresh; no FPS promise is made here.

| Profile | Expected cost | Main risks | Gate |
|---|---|---|---|
| CLASSIC | Baseline | Existing resolution/MSAA/interpolation choices | frame-time and visual baseline |
| ENHANCED | Low to medium incremental | extra material constants, shader variants, texture bindings | per-scene GPU capture; classic comparison |
| RTX EXPERIMENTAL | High/unknown | extra passes, temporal history, DX12/DXR, memory and synchronization | opt-in hardware matrix and capture/replay |

Likely cost order: path tracing/DXR > frame generation/DLSS integration > shadow maps/reflections > normal maps/water passes > bloom/AO > constants. CPU risks are shader/material lookup, draw classification, resource lifetime and acceleration-structure updates. Memory risks are duplicate maps, mip chains, history buffers and shadow atlases.

Budget method: measure classic scene captures first; enforce a per-feature GPU/CPU delta budget rather than inventing a universal FPS number. Default every new feature off and preserve a runtime fallback.
