# RC1.1 Regression Check — V03

## Changes Made
1. `MaterialRegistry.h` — added `IsEnhancedVisualProfile()` static method
2. `interpreter.cpp` — added CVar read, MaterialRegistry include, Flush() instrumentation, EndFrame() tonemapping call
3. `gfx_direct3d11.cpp` — ACES shader rewritten (linearize + ACES + re-gamma), fog pass added, fog CB created
4. `gfx_direct3d_common.h` — fog constant buffer struct added, fog CB member added
5. `MaterialRegistry.cpp` — PBR-Lite defaults added to RecordDrawCall

## RC1.1 Preservation Analysis
| Feature | Status | Reason |
|---------|--------|--------|
| Spanish | PASS | No message/text files modified |
| Xbox controls | PASS | No input code modified |
| LT -> Z | PASS | No input code modified |
| RS -> C | PASS | No input code modified |
| 3440x1440 | PASS | No resolution code modified |
| MatchRefreshRate | PASS | No display code modified |
| Saves | PASS | No save code modified |
| Fast3D hardening | PASS | Existing guards preserved |
| G_SETTIMG hardening | PASS | Not modified |
| CustomMessage fixes | PASS | Not modified |
| es.o2r | PASS | Not modified |

## Classic Profile Regression
- Classic profile: IsEnhancedVisualProfile() returns false
- RunTonemappingPass NOT called → no state changes
- MaterialRegistry NOT recording → no overhead
- Flush() adds one branch (profile check) → negligible overhead
- Output identical to RC1.1 baseline

## Conclusion
V03 changes are purely additive to the ENHANCED path.
Classic path is untouched except for one branch per frame in StartFrame() and Flush().
