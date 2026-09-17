# 04 — MATERIAL REGISTRY ACTIVATION PLAN

## Wiring MaterialRegistry into the Rendering Pipeline

---

### 1. Current State

| Component | Location | Status |
|---|---|---|
| `MaterialRegistry` singleton | `MaterialRegistry.cpp:Instance()` | Built |
| `RecordDrawCall()` | `MaterialRegistry.cpp` | Built, never called |
| `ClassifyFromState()` | `MaterialRegistry.cpp` | Built, never called |
| `FlushFrame()` | `MaterialRegistry.cpp` | Built, never called |
| `DrawCallInfo` struct | `MaterialRegistry.h` | Built |
| `GfxSpTri1()` | `interpreter.cpp` | Main triangle submission — NO registry call |
| `GfxDpTextureRectangle()` | `interpreter.cpp` | Rectangle draws — NO registry call |

---

### 2. Activation Path — Minimal Safe Wiring

#### Step 1: Instrument `GfxSpTri1()` — Primary Hook

**File**: `interpreter.cpp`
**Function**: `GfxSpTri1()` (approximate line ~1450-1530)
**Location**: After the VBO is built and `mBufVboNumTris++`, before flush

**What to add:**
```cpp
// After mBufVboNumTris++ and before returning from GfxSpTri1:
DrawCallInfo info;
info.shaderId0 = current_shader_id0;  // CC mode
info.shaderId1 = current_shader_id1;  // options
info.geometryMode = mRsp->geometry_mode;
info.otherModeH = mRdp->other_mode_h;
info.otherModeL = mRdp->other_mode_l;
info.primColor = mRdp->prim_color;
info.envColor = mRdp->env_color;
info.fogColor = mRdp->fog_color;
info.is2D = is_rectangle_draw;
info.textureId = mTexSelected[0];
MaterialRegistry::Instance().RecordDrawCall(info);
```

**Risk**: LOW — purely additive, no existing behavior changed.

---

#### Step 2: Instrument `GfxDpTextureRectangle()` — Rectangle Hook

**File**: `interpreter.cpp`
**Function**: `GfxDpTextureRectangle()` (handles HUD elements)

**What to add:**
```cpp
DrawCallInfo info;
// ... populate from current state ...
info.is2D = true;
MaterialRegistry::Instance().RecordDrawCall(info);
```

**Risk**: LOW — marking 2D draws for exclusion.

---

#### Step 3: Call `FlushFrame()` at Frame End

**File**: `interpreter.cpp`
**Function**: `EndFrame()` or `Fast3dWindow::DrawAndRunGraphicsCommands()`

**What to add:**
```cpp
MaterialRegistry::Instance().FlushFrame();
```

**Location**: After all triangles are drawn, before present.

**Risk**: LOW — aggregation step, no rendering impact.

---

### 3. What Must NOT Change

| Constraint | Reason |
|---|---|
| No modification to `GfxSpTri1()` draw behavior | Preserve N64 rendering fidelity |
| No modification to shader selection | No PBR injection yet in V03 RC |
| No modification to texture binding | Original textures stay |
| No modification to VBO layout | Existing vertex format preserved |
| No new D3D11 API calls in activation step | Metadata only |

---

### 4. Data Flow After Activation

```
GfxSpTri1()
  ├─ [existing] Build VBO, lookup shader, increment tri count
  └─ [NEW] MaterialRegistry::Instance().RecordDrawCall(info)
       ├─ ClassifyFromState(info) → MaterialType
       ├─ Store in frame buffer
       └─ Update statistics

EndFrame()
  └─ [NEW] MaterialRegistry::Instance().FlushFrame()
       ├─ Aggregate frame statistics
       └─ Clear frame buffer
```

---

### 5. What LoadRegistry/SaveRegistry Need

**Current**: Empty TODO stubs.

**Future requirement** (not V03):
- JSON file mapping texture IDs → material overrides
- Allow user to manually correct misclassified materials
- Persist across sessions
- Path: `!mods/material_overrides.json` or similar

**This is NOT required for V03 activation.** Classification runs purely from runtime state.

---

### 6. VisualProfile Integration

**File**: `SohMenuSettings.cpp:414` — CVar `gEnhancements.Graphics.VisualProfile`

**Activation path:**
1. In rendering code, read: `CVarGetInteger("gEnhancements.Graphics.VisualProfile", 0)`
2. If value is 0 (Classic): skip all MaterialRegistry processing
3. If value is 1 (Enhanced): enable RecordDrawCall + material property application

**This is Step 2 of activation** — after registry is wired and verified.

---

### 7. Verification Without Rendering Changes

To confirm activation works without modifying visuals:

1. Add logging to `RecordDrawCall()` — print material type per draw call
2. Add logging to `FlushFrame()` — print frame summary
3. Run game, verify log output shows classifications
4. Verify frame time impact < 0.1ms
5. Verify no rendering changes (shader/texture/blend unchanged)

---

### 8. Risk Assessment

| Risk | Mitigation |
|---|---|
| Performance regression from per-draw-call classification | Heuristic is lightweight (integer comparisons only) |
| Incorrect classification → wrong PBR properties later | UNKNOWN fallback + classification confidence levels |
| Thread safety | MaterialRegistry is singleton called from single render thread |
| Memory growth from frame buffer | FlushFrame() clears after each frame |

---

### 9. Files Likely Modified

| File | Change | Risk |
|---|---|---|
| `interpreter.cpp` | Add RecordDrawCall() call in GfxSpTri1() | LOW |
| `interpreter.cpp` | Add RecordDrawCall() call in GfxDpTextureRectangle() | LOW |
| `interpreter.cpp` | Add FlushFrame() call in EndFrame() | LOW |
| `SohMenuSettings.cpp` | No change needed (CVar already exists) | NONE |

**Total**: 3 insertion points in 1 file.

---

### 10. Activation Sequence

```
Phase 1: Wire registry (interpreter.cpp edits)
  → Verify: log output shows classifications
  → Verify: no rendering change
  → Verify: frame time < +0.1ms

Phase 2: Add VisualProfile gate
  → Read CVar, skip processing when Classic
  → Verify: Classic mode = zero overhead

Phase 3: Material property application (V03 actual)
  → Read material properties from registry
  → Pass to shader via constant buffer
  → Adjust roughness/metallic/emissive per material

Phase 4: PBR-Lite shader integration
  → Modify HLSL template to accept material parameters
  → Add simple PBR lighting model
  → Gate behind ENHANCED profile
```

Phase 1-2 are safe metadata operations. Phase 3-4 change visuals and require the V03 implementation plan.
