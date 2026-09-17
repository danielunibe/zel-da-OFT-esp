# MaterialRegistry Activation — V03

## Changes
1. **IsEnhancedVisualProfile()** added to `MaterialRegistry.h`
   - Static helper: `Instance().GetProfile() == 1`
   - Single canonical query point for all Enhanced-only systems

2. **CVar wired in StartFrame()** — `interpreter.cpp:4289`
   - Reads `gEnhancements.Graphics.VisualProfile` each frame
   - Calls `MaterialRegistry::Instance().SetProfile()`

3. **Draw call recording in Flush()** — `interpreter.cpp:132`
   - Records DrawCallInfo with N64 state (combine mode, geometry mode, other modes, prim/env/fog colors)
   - Only records when ENHANCED profile active (no overhead in CLASSIC)
   - Uses `MaterialRegistry::Instance().RecordDrawCall()`

4. **FlushFrame() called in EndFrame()** — `interpreter.cpp:4412`
   - Clears per-frame draw call data after tonemapping pass

## Classification
- `ClassifyFromState()` uses N64 rendering state to classify materials
- Initial categories: LAVA (red env), WATER (blue env), EMISSIVE (bright prim+env), MAGIC (noise/2cycle), PROTECTED_2D (no Z-buffer), CLASSIC (default)
- Classification is advisory — UNKNOWN renders identically to CLASSIC

## PBR-Lite Defaults
Per-type roughness/metallic/specular assigned on first encounter:
- STONE: roughness 0.85, metallic 0, specular 0.1
- WOOD: roughness 0.75, metallic 0, specular 0.05
- METAL: roughness 0.4, metallic 0.8, specular 0.6
- WATER: roughness 0.1, metallic 0, specular 0.4
- LAVA: roughness 0.3, emissive 0.8
- EMISSIVE: roughness 0.5, emissive 0.6
- GRASS: roughness 0.9, metallic 0
- DEFAULT: roughness 0.5, metallic 0

## Draw Calls Using Materials
- All world draw calls go through `Flush()` → `RecordDrawCall()`
- UI draw calls (HUD, menus) do NOT go through the interpreter flush path
- UI protection is inherent — UI is rendered via ImGui, not N64 display lists
