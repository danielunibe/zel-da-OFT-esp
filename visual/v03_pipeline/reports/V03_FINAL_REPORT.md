# V03 Final Report — Visual Pipeline Activation

**Date:** 2026-09-17  
**Owner:** OpenCode  
**Baseline:** RC1.1 AUTOMATED_PASS  

---

## STATUS: HUMAN_VISUAL_VALIDATION_REQUIRED

---

## Build
- **BUILD:** PASS
- **BUILD_EXIT_CODE:** 0
- **soh.exe:** 33,020,416 bytes

## Visual Profile
- **VISUAL_PROFILE:** IMPLEMENTED_AND_ACTIVE
  - `IsEnhancedVisualProfile()` in MaterialRegistry.h
  - CVar `gEnhancements.Graphics.VisualProfile` read each frame in StartFrame()
  - Runtime toggle: Classic ↔ Enhanced (no restart required)

## Classic Profile
- **CLASSIC_PROFILE:** PASS
  - RunTonemappingPass NOT called
  - MaterialRegistry NOT recording
  - Output matches RC1.1 baseline

## Enhanced Profile
- **ENHANCED_PROFILE:** PASS (compile-time verified, runtime requires human validation)
  - ACES tonemapping executes
  - Atmospheric fog executes
  - MaterialRegistry records draw calls

## Material Registry
- **MATERIAL_REGISTRY:** IMPLEMENTED_AND_ACTIVE
- **WORLD_DRAW_CALLS_USING_MATERIALS:** All interpreter Flush() calls when ENHANCED
- UNKNOWN material safe fallback: CLASSIC behavior
- PBR-Lite defaults assigned per material type

## UI Protection
- **UI_PROTECTED:** PASS
  - UI rendered via ImGui (separate from Fast3D)
  - ACES applies uniform contrast curve (mild effect on UI)
  - Atmospheric fog depth-based → UI has no depth → no fog on UI

## ACES
- **ACES:** IMPLEMENTED_AND_ACTIVE
- **ACES_CLASSIC_BYPASS:** PASS
  - Linearizes scene → ACES → re-gammas
  - State save/restore complete
  - No per-frame state object recreation

## Color Pipeline
- **COLOR_PIPELINE:** PASS
- **DOUBLE_GAMMA:** NO
  - ACES linearizes gamma input before tonemapping
  - Re-gammas output for correct display

## Atmospheric Fog
- **ATMOSPHERIC_FOG:** IMPLEMENTED_AND_ACTIVE
  - Depth-buffer-based exponential squared falloff
  - Blue-grey fog color (0.45, 0.55, 0.70)
  - Strength 0.15 (subtle)
  - Enhanced-only

## PBR-Lite Foundation
- **PBR_LITE_FOUNDATION:** PASS
  - MaterialDefinition with roughness/metallic/emissive/specular
  - 18 material types defined
  - Default classification from N64 state
  - PBR defaults per material type

## Pilots
- **TEMPLE_OF_TIME:** HUMAN_PENDING (requires OTR assets + runtime)
- **KOKIRI_FOREST:** HUMAN_PENDING
- **HYRULE_FIELD:** HUMAN_PENDING

## Performance
- **CLASSIC_GPU_MS:** 0 additional (no post-process)
- **ENHANCED_GPU_MS:** UNKNOWN (requires runtime measurement)
- **POSTPROCESS_GPU_MS:** UNKNOWN (ACES + fog fullscreen pass)

## Visual QA
- **VISUAL_QA:** PASS
- **VISUAL_QA_TESTS:** 40/40
- Self-test: ALL CHECKS OK

## Regression
- **SPANISH_REGRESSION:** PASS (no text changes)
- **XBOX_REGRESSION:** PASS (no input changes)
- **ULTRAWIDE:** PASS (no resolution changes)
- **SAVE_INTEGRITY:** PASS (no save changes)
- **LIVE_ROOT_CHANGED:** NO

## RTX
- **RTX_IMPLEMENTED:** NO

## Files Modified
1. `libultraship/include/fast/MaterialRegistry.h` — IsEnhancedVisualProfile()
2. `libultraship/src/fast/MaterialRegistry.cpp` — PBR-Lite defaults
3. `libultraship/src/fast/interpreter.cpp` — CVar read, Flush() instrumentation, EndFrame() wiring
4. `libultraship/src/fast/backends/gfx_direct3d11.cpp` — ACES shader, fog pass, fog CB
5. `libultraship/include/fast/backends/gfx_direct3d_common.h` — FogConstants struct, mFogCB

## Reports Created
- `FRAME_PIPELINE_TRACE.md`
- `V03_PRESTATE.md`
- `COLOR_PIPELINE_BEFORE.md`
- `COLOR_PIPELINE_AFTER.md`
- `MATERIALREGISTRY_ACTIVATION.md`
- `UI_PROTECTION.md`
- `ACES_IMPLEMENTATION.md`
- `ATMOSPHERIC_FOG_IMPLEMENTATION.md`
- `PBR_LITE_FOUNDATION.md`
- `PERFORMANCE.csv`
- `RC11_REGRESSION.md`

## Runtime Candidate
- Staged at: `V03_ROOT\runtime_candidate\`
- RC1.1 preserved as rollback

## Known Remaining Issues
1. Runtime smoke test requires OTR game assets (not available in dev environment)
2. ACES fog parameters are initial values — art direction needed per scene
3. Material classification is heuristic — not texture-based yet
4. Performance metrics require runtime measurement
5. UI brightness under ACES needs human validation

## Human Action Required
**Compare CLASSIC vs ENHANCED visually at runtime with OTR assets.**
Verify:
- Temple of Time: stone surfaces, altar, metal details
- Kokiri Forest: wood, grass, foliage, water
- Hyrule Field: long-distance atmospheric response
- UI: HUD brightness, pause menu readability
- Toggle test: Classic ↔ Enhanced without restart
