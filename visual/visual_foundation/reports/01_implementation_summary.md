# V02 Visual Foundation — Implementation Summary

## Date: 2026-09-16

## Objective
Implement V02 Visual Foundation: modernize OoT's visual presentation via MaterialRegistry, draw-call instrumentation, CLASSIC/ENHANCED profile system, tonemapping/post-processing, and atmospheric depth improvements.

## What Was Implemented

### 1. MaterialRegistry (Singleton)
- **Header**: `libultraship/include/fast/MaterialRegistry.h`
- **Implementation**: `libultraship/src/fast/MaterialRegistry.cpp`
- 18 material types: LAVA, WATER, MAGIC, EMISSIVE, PROTECTED_2D, STONE, WOOD, METAL, GLASS, FABRIC, EARTH, GRASS, FOLIAGE, SAND, ICE, SKY, CHARACTER, CLASSIC
- Heuristic classification from RDP state (texture hash, combine mode, geometry mode, prim/env colors)
- Draw-call recording with texture hash, combine mode, geometry mode, prim/env colors, fog, rect/2D flags
- CSV inventory output for material analysis
- Profile switching: CLASSIC (0) / ENHANCED (1)

### 2. Draw-Call Instrumentation
- **File**: `libultraship/src/fast/interpreter.cpp`
- `GfxSpTri1()` — records 3D draw calls with full material data
- `GfxDpTextureRectangle()` — records 2D draw calls with is2D=true
- `EndFrame()` — flushes MaterialRegistry and reads `gEnhancements.Graphics.VisualProfile` CVar

### 3. Visual Profile Menu
- **File**: `soh/soh/SohGui/SohMenuSettings.cpp`
- Settings > Graphics > Visual Profile: "Classic" (0) / "Enhanced" (1)
- Uses `CVarGetInteger("gEnhancements.Graphics.VisualProfile", 0)`

### 4. ACES Filmic Tonemapping (Enhanced Mode Only)
- **Files**: `gfx_direct3d_common.h`, `gfx_direct3d11.cpp`
- Inline HLSL shaders: fullscreen triangle VS + ACES filmic PS
- Runtime compilation via `mD3dCompile` (same pattern as existing compute shader)
- Lazy texture creation with automatic resize on window resize
- Full DX11 state save/restore for safe post-process pass
- Activated only when `VisualProfile == 1` (Enhanced mode)

### 5. Atmospheric Fog Depth Curve (Enhanced Mode Only)
- **File**: `interpreter.cpp` — `GfxSpVertex()`
- Applies power curve (`pow(normalized, 0.85)`) to fog factor
- Creates smoother, more gradual atmospheric depth transition
- Only active in Enhanced mode

## Build Status
- ✅ Compiles with zero errors (MSVC 19.44, C++17)
- ✅ `soh.exe` generated (33 MB, Release)
- ⚠️ No runtime smoke test (no OTR assets available)

## Files Modified
| File | Change |
|------|--------|
| `libultraship/include/fast/MaterialRegistry.h` | NEW: Material classification singleton |
| `libultraship/src/fast/MaterialRegistry.cpp` | NEW: Implementation |
| `libultraship/src/fast/interpreter.cpp` | MODIFIED: Draw-call recording, fog enhancement, tonemapping trigger |
| `libultraship/include/fast/backends/gfx_rendering_api.h` | MODIFIED: Added `RunTonemappingPass()` virtual |
| `libultraship/include/fast/backends/gfx_direct3d_common.h` | MODIFIED: Added tonemapping members |
| `libultraship/src/fast/backends/gfx_direct3d11.cpp` | MODIFIED: Tonemapping shader + pass |
| `soh/soh/SohGui/SohMenuSettings.cpp` | MODIFIED: Visual Profile combobox |
