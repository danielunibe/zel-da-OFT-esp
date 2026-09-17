# 02 — V02 REALITY CHECK

## Independent Source Verification

---

### Methodology

All claims verified against current source files in `C:\Users\danie\Desktop\Ocarina_CouchEdition_DEV\source\shipwright\`. No assumptions from prior reports accepted without source evidence.

---

### 1. VisualProfile

| Aspect | Finding |
|---|---|
| **CVar existence** | `gEnhancements.Graphics.VisualProfile` exists in `SohMenuSettings.cpp:414` |
| **UI options** | Classic (0), Enhanced (1) |
| **Tooltip text** | "Modern material response, tonemapping, and atmospheric depth. (Experimental - requires rebuild)" |
| **Rendering code read** | NO rendering file reads this CVar |
| **Branching logic** | NONE — no if/switch on VisualProfile in any rendering path |
| **Integration** | Menu-only stub |
| **Status** | **NOT_IMPLEMENTED** |

The VisualProfile UI element is a placeholder. It does not affect rendering behavior in any way.

---

### 2. MaterialRegistry

| Aspect | Finding |
|---|---|
| **File exists** | `MaterialRegistry.h` (92 lines), `MaterialRegistry.cpp` (152 lines) |
| **Singleton** | `MaterialRegistry::Instance()` — implemented |
| **Material types** | 18 types defined: CLASSIC, STONE, WOOD, METAL, WATER, GLASS, FABRIC, EARTH, GRASS, FOLIAGE, SAND, LAVA, ICE, MAGIC, EMISSIVE, SKY, CHARACTER, PROTECTED_2D, UNKNOWN |
| **ClassifyFromState()** | Implemented with heuristic rules (noise→MAGIC, red env→LAVA, blue env→WATER, etc.) |
| **RecordDrawCall()** | Implemented in MaterialRegistry.cpp |
| **FlushFrame()** | Implemented in MaterialRegistry.cpp |
| **LoadRegistry()** | **TODO STUB — empty function body** |
| **SaveRegistry()** | **TODO STUB — empty function body** |
| **Called from rendering?** | **NO** — `MaterialRegistry::Instance()` is never invoked outside its own .cpp |
| **Hooked into GfxSpTri1?** | **NO** — draw call instrumentation does NOT call RecordDrawCall() |
| **Status** | **IMPLEMENTED_BUT_DISABLED** |

Infrastructure built. Completely disconnected from the rendering pipeline. No draw call ever reaches the registry.

---

### 3. ACES Tonemapping

| Aspect | Finding |
|---|---|
| **Shader compilation** | `gfx_direct3d11.cpp:279-349` — ACES filmic VS/PS compiled during `Init()` |
| **ACES curve** | Standard filmic: `saturate((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14))` |
| **RunTonemappingPass()** | **FULLY IMPLEMENTED** (gfx_direct3d11.cpp:801-921) — saves/restores all D3D11 state |
| **Base class virtual** | `GfxRenderingAPI::RunTonemappingPass()` has empty default impl |
| **Called from anywhere?** | **NO** — not called from Interpreter, Fast3dWindow, or any other code |
| **OpenGL/Metal backend** | No tonemapping pass in either |
| **Status** | **IMPLEMENTED_BUT_DISABLED** |

Complete D3D11 ACES implementation exists as dead code. Never invoked during rendering.

---

### 4. Atmospheric Fog

| Aspect | Finding |
|---|---|
| **N64 fog pipeline** | Fully working across all backends |
| **Vertex fog factor** | `z * (1/w) * fog_mul + fog_offset` — calculated in `interpreter.cpp` |
| **Fog color** | Set by game via `GfxDpSetFogColor()` → stored in `mRdp->fog_color` |
| **Fog parameters** | Set by `GfxSpMovewordF3dex2(G_MW_FOG, ...)` → `mRsp->fog_mul`, `mRsp->fog_offset` |
| **Shader blending** | HLSL: `lerp(texel.rgb, input.fog.rgb, input.fog.a)` — standard alpha fog |
| **Geometry mode** | `G_FOG` bit enables fog per draw call |
| **Enhanced fog?** | **NO** — this is pure N64 emulation, not a modern atmospheric system |
| **Status** | **IMPLEMENTED_AND_ACTIVE** (as N64 emulation) |

Fog works as it always has on N64. It is NOT a modern atmospheric depth system. The "atmospheric depth" mentioned in the VisualProfile tooltip does not exist.

---

### 5. Draw-Call Instrumentation

| Aspect | Finding |
|---|---|
| **GfxSpTri1() call to registry** | **NO** — the critical triangle submission function does NOT call MaterialRegistry |
| **DrawCallInfo struct** | Defined in MaterialRegistry.h |
| **Instrumentation hooks** | **NOT present** in interpreter.cpp |
| **Status** | **NOT_IMPLEMENTED** |

No code path connects a draw call to the MaterialRegistry.

---

### Summary Table

| System | Status | Evidence |
|---|---|---|
| VisualProfile | **NOT_IMPLEMENTED** | CVar in UI only; no rendering code reads it |
| MaterialRegistry | **IMPLEMENTED_BUT_DISABLED** | Singleton exists; RecordDrawCall() never invoked |
| ACES Tonemapping | **IMPLEMENTED_BUT_DISABLED** | D3D11 code complete; RunTonemappingPass() never called |
| Atmospheric Fog | **IMPLEMENTED_AND_ACTIVE** | N64 fog pipeline fully functional (not modern atmospheric) |
| Draw-Call Instrumentation | **NOT_IMPLEMENTED** | No hooks in GfxSpTri1 or anywhere else |

---

### Critical Finding

V02 as described in visual foundation reports contains **scaffolding code only**. The systems exist as isolated implementation units but are not wired into the rendering pipeline. No visual enhancement is active at runtime.

The only "enhanced" visual behavior currently functional is the N64 fog, which is part of the original game emulation — not a new feature.
