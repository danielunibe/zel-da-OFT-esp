# 18 — V03 IMPLEMENTATION PLAN

## Precise Implementation Sequence for V03 Visual Pipeline Activation

---

### Step 1: Wire MaterialRegistry into Rendering Pipeline

| Aspect | Detail |
|---|---|
| **Priority** | CRITICAL — foundation for all V03 features |
| **Files** | `interpreter.cpp` |
| **Functions** | `GfxSpTri1()`, `GfxDpTextureRectangle()`, `EndFrame()` |
| **Dependencies** | RC1.1 stable |
| **Risk** | LOW — purely additive metadata |
| **Visual result** | None — metadata collection only |
| **Performance impact** | <0.1ms per frame |
| **Classic fallback** | Always active — metadata only, no visual change |
| **Test method** | Log output from RecordDrawCall(), verify frame time |

**Exact changes:**
1. Add `#include "MaterialRegistry.h"` to interpreter.cpp
2. After VBO build in GfxSpTri1(), populate DrawCallInfo and call RecordDrawCall()
3. In GfxDpTextureRectangle(), mark is2D=true and call RecordDrawCall()
4. In EndFrame(), call FlushFrame()
5. Verify with logging output

---

### Step 2: VisualProfile Gate

| Aspect | Detail |
|---|---|
| **Priority** | HIGH — enables profile switching |
| **Files** | `interpreter.cpp` |
| **Functions** | `GfxSpTri1()`, `EndFrame()` |
| **Dependencies** | Step 1 |
| **Risk** | LOW — CVar read + conditional |
| **Visual result** | None — gate only |
| **Performance impact** | ZERO when Classic |
| **Classic fallback** | Gate ensures Classic = zero overhead |
| **Test method** | Toggle CVar, verify Enhanced processing only when enabled |

**Exact changes:**
1. Read CVar: `CVarGetInteger("gEnhancements.Graphics.VisualProfile", 0)`
2. Wrap RecordDrawCall() calls in `if (profile == 1)` check
3. Wrap FlushFrame() call in same check
4. Classic mode = zero material processing overhead

---

### Step 3: ACES Tonemapping Activation

| Aspect | Detail |
|---|---|
| **Priority** | HIGH — first visible enhancement |
| **Files** | `gfx_direct3d11.cpp`, `interpreter.cpp` |
| **Functions** | `RunTonemappingPass()` (exists), `EndFrame()` (new call) |
| **Dependencies** | Step 2, sRGB render target |
| **Risk** | MEDIUM — color pipeline change |
| **Visual result** | Filmic tonemapping on entire scene |
| **Performance impact** | +0.10ms |
| **Classic fallback** | Skip RunTonemappingPass() when Classic |
| **Test method** | Visual inspection, compare before/after screenshots |

**Exact changes:**
1. Create sRGB render target view in gfx_direct3d11.cpp Init()
2. Add RunTonemappingPass() call in interpreter.cpp EndFrame()
3. Gate behind ENHANCED profile
4. Verify no double-gamma (compare with/without sRGB view)

---

### Step 4: Material Constant Buffer

| Aspect | Detail |
|---|---|
| **Priority** | HIGH — enables PBR material response |
| **Files** | `MaterialRegistry.cpp`, `gfx_direct3d11.cpp`, `default.shader.hlsl` |
| **Functions** | New: `GetMaterialProperties()`, Updated: DrawTriangles() |
| **Dependencies** | Steps 1, 2 |
| **Risk** | MEDIUM — new constant buffer + shader changes |
| **Visual result** | None until shader modified |
| **Performance impact** | +0.05ms |
| **Classic fallback** | Default material params when Classic |
| **Test method** | Verify constant buffer updates, log material types |

**Exact changes:**
1. Add MaterialProperties struct to MaterialRegistry.h
2. Add GetMaterialProperties(type) method
3. Add constant buffer slot b2 in gfx_direct3d11.cpp
4. Update DrawTriangles() to populate b2 from MaterialRegistry
5. Gate behind ENHANCED profile

---

### Step 5: PBR-Lite Shader Integration

| Aspect | Detail |
|---|---|
| **Priority** | HIGH — core visual enhancement |
| **Files** | `default.shader.hlsl` |
| **Functions** | Pixel shader PBR block |
| **Dependencies** | Steps 3, 4 |
| **Risk** | MEDIUM — shader modification |
| **Visual result** | Material-dependent specular/emissive response |
| **Performance impact** | +0.15ms |
| **Classic fallback** | `@if(PBR_ENABLED)` = 0 disables PBR block |
| **Test method** | Visual inspection per material type |

**Exact changes:**
1. Add `PBR_ENABLED` option to shader template
2. Add GGX specular calculation in pixel shader
3. Add emissive material path
4. Add water Fresnel special path
5. Gate with `@if(PBR_ENABLED)`
6. Verify Classic = no PBR

---

### Step 6: Atmospheric Depth Fog

| Aspect | Detail |
|---|---|
| **Priority** | MEDIUM — distance enhancement |
| **Files** | New shader pass, `gfx_direct3d11.cpp`, `interpreter.cpp` |
| **Functions** | New post-process pass, depth buffer readback |
| **Dependencies** | Steps 3, 5 |
| **Risk** | LOW — independent post-process |
| **Visual result** | Depth-based atmospheric haze |
| **Performance impact** | +0.08ms |
| **Classic fallback** | Skip atmospheric pass when Classic |
| **Test method** | Visual inspection at distance, verify no gameplay visibility change |

**Exact changes:**
1. Create atmospheric fog pixel shader
2. Read depth buffer SRV (already exists)
3. Linearize depth, compute fog factor
4. Blend with scene color
5. Gate behind ENHANCED profile
6. Per-scene density configuration

---

### Step 7: UI Protection Verification

| Aspect | Detail |
|---|---|
| **Priority** | HIGH — safety requirement |
| **Files** | Testing only — no code changes |
| **Functions** | Manual verification |
| **Dependencies** | Steps 3, 5, 6 |
| **Risk** | NONE — verification step |
| **Visual result** | Verify UI unchanged |
| **Performance impact** | N/A |
| **Classic fallback** | N/A |
| **Test method** | Test all UI elements in ENHANCED mode |

**Verification checklist:**
1. HUD hearts — no color shift
2. Rupee counter — readable
3. Pause menu — icons original color
4. Navi hints — legible
5. Message boxes — unchanged
6. Screen transitions — smooth

---

### Summary

| Step | Time Estimate | Risk | Visual Change |
|---|---|---|---|
| 1. Wire Registry | 0.5 day | LOW | None |
| 2. VisualProfile Gate | 0.25 day | LOW | None |
| 3. ACES Tonemapping | 1 day | MEDIUM | YES — tonemapping |
| 4. Material CB | 1 day | MEDIUM | None until Step 5 |
| 5. PBR-Lite Shader | 2 days | MEDIUM | YES — material response |
| 6. Atmospheric Fog | 1 day | LOW | YES — distance haze |
| 7. UI Verification | 0.5 day | NONE | Verification |
| **TOTAL** | **~6.25 days** | | |

**After Step 7**: V03 ENHANCED profile complete and verified.
