# 06 — COLOR PIPELINE / ACES ANALYSIS

## D3D11 Color Space and Tonemapping Audit

---

### 1. Current Backbuffer Configuration

| Property | Value | Source |
|---|---|---|
| Swap chain format | `DXGI_FORMAT_R8G8B8A8_UNORM` | `gfx_dxgi.cpp` — standard SoH |
| sRGB encoding | **NOT explicitly set** — UNORM, not `_SRGB` | No sRGB view created |
| Render target format | `DXGI_FORMAT_R8G8B8A8_UNORM` | `gfx_direct3d11.cpp` framebuffer creation |
| Depth buffer | `DXGI_FORMAT_D32_FLOAT` | `gfx_direct3d11.cpp` |
| Shader math | Implicitly gamma-space | No linearization in shaders |

**Current assumption: All rendering happens in gamma space (sRGB stored as UNORM, read as linear by hardware).**

This is technically incorrect for physically-based operations but matches the N64's original behavior where color math was done in the same space as the framebuffer.

---

### 2. Where Tonemapping Belongs (Mathematically)

Correct linear-workflow tonemapping requires:

```
1. Render in linear space
2. Apply lighting in linear space
3. Apply PBR response in linear space
4. Tonemap (compress HDR → SDR)
5. Convert back to sRGB for display
```

**Current N64 pipeline:**
```
1. Vertex colors computed (gamma or linear? N64 was integer math)
2. Color combiner blends textures + vertex colors
3. Result written directly to framebuffer
4. Display shows framebuffer
```

There is no linear intermediate step. The N64 did all color math in its fixed-point integer pipeline, which is effectively gamma space for 8-bit colors.

---

### 3. ACES Implementation Analysis

**File**: `gfx_direct3d11.cpp:279-349` (init), `801-921` (pass)

**Shader code (simplified):**
```hlsl
float3 ACESFilm(float3 x) {
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x*(a*x+b))/(x*(c*x+d)+e));
}
```

**This is the standard ACES filmic curve by Stephen Hill.**

---

### 4. Double-Gamma Risk Assessment

**If applied to current pipeline (gamma-space input):**

| Step | Space | Issue |
|---|---|---|
| N64 rendering | Gamma (8-bit integers) | Original behavior |
| Backbuffer read | Linear (hardware) | Hardware interprets sRGB bytes as linear |
| ACES applied | Linear | Curve expects linear input |
| Output to backbuffer | Linear → displayed as gamma | **Double compression** |

**Result: Dark, crushed midtones. Colors appear too dark and desaturated.**

This is the classic "double-gamma" error. ACES expects linear input but receives gamma-encoded data.

---

### 5. Corrected Color Pipeline (V03 Target)

**Option A: sRGB render targets (recommended)**

```
1. Create SRV views with DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
2. Hardware automatically linearizes on texture read
3. Render in linear space (shader math is now correct)
4. Write to UNORM_SRGB target (hardware re-encodes to sRGB)
5. ACES tonemap reads from UNORM_SRGB → linear
6. Output to backbuffer
```

**Option B: Manual linearization in shader**

```hlsl
// In pixel shader, before ACES:
float3 color = pow(backbufferSample, 2.2);  // gamma → linear
color = ACESFilm(color);
// No explicit re-encoding needed if output is UNORM
```

Option A is preferred because it's hardware-accelerated and consistent.

---

### 6. ACES Validity Check

**The ACES implementation itself is mathematically correct:**
- Standard Hill filmic curve
- Proper `saturate()` clamp
- Applied as fullscreen post-process
- State save/restore is thorough (all D3D11 state saved and restored)

**Issues with current dead code:**
1. Reads backbuffer as linear (UNORM) but data is gamma → double-gamma
2. No linear intermediate render target
3. No sRGB view creation
4. No HDR intermediate (16-bit float) for proper HDR headroom

---

### 7. Required V03 Color Pipeline Changes

| Change | File | Purpose |
|---|---|---|
| Create sRGB render target view | `gfx_direct3d11.cpp` | Linearize rendering math |
| Linearize backbuffer read for tonemap | `gfx_direct3d11.cpp` | Correct ACES input |
| Add `RunTonemappingPass()` call | `interpreter.cpp` EndFrame() | Activate dead code |
| Gate behind ENHANCED profile | `interpreter.cpp` | Classic fallback |
| Consider 16-bit float intermediate | `gfx_direct3d11.cpp` | HDR headroom (future) |

---

### 8. Graphics State to Save/Restore for Tonemapping

The current `RunTonemappingPass()` saves/restores:

| State | Saved? | Restored? |
|---|---|---|
| Render targets | YES | YES |
| Viewports | YES | YES |
| Scissors | YES | YES |
| Blend state | YES | YES |
| Depth/stencil state | YES | YES |
| Rasterizer state | YES | YES |
| Vertex shader | YES | YES |
| Pixel shader | YES | YES |
| Input layout | YES | YES |
| Shader resources | YES | YES |
| Samplers | YES | YES |
| Constant buffers | YES | YES |

**State save/restore is thorough and correct.** No issues found.

---

### 9. Post-Process Execution Point

**Recommended insertion:**
```
Interpreter::EndFrame()
  ├─ mRapi->EndFrame()           // Flush remaining triangles
  ├─ [NEW] mRapi->RunTonemappingPass()  // ACES post-process
  ├─ mWapi->SwapBuffersBegin()   // Present
  ├─ mRapi->FinishRender()
  └─ mWapi->SwapBuffersEnd()
```

This ensures all geometry is flushed before tonemapping reads the backbuffer.

---

### 10. Summary

| Aspect | Current State | V03 Requirement |
|---|---|---|
| Backbuffer format | UNORM (gamma) | UNORM_SRGB (linear rendering) |
| ACES shader | Built but dead | Activate via EndFrame() |
| ACES input | Gamma (wrong) | Linear via sRGB view |
| Linear intermediate | NONE | sRGB render target |
| HDR intermediate | NONE | Optional, future |
| Profile gate | NONE | ENHANCED only |
| State save/restore | Complete | No changes needed |
