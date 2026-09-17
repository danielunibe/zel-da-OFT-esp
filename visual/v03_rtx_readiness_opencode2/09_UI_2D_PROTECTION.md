# 09 — UI / 2D PROTECTION

## Protected Rendering Categories

---

### 1. Categories That Must NEVER Receive Enhancements

| Category | Examples | Why Protected |
|---|---|---|
| **HUD** | hearts, magic meter, rupee counter, minimap | Must be crisp, readable, unaltered |
| **Pause menu** | item select, map, quest status | Must remain functionally identical |
| **Text/Dialogue** | message boxes, NPC text, system messages | Must be legible, no fog/color shift |
| **Icons** | item icons, button prompts, status icons | Must match original art intent |
| **Sprites** | particle effects, Deku scrub projectiles | Gameplay-critical visibility |
| **Title cards** | area names, boss names | Artistic text presentation |
| **Message boxes** | Navi hints, tutorial prompts | Must be readable |
| **Screen transitions** | fade in/out, warp effects | Must not be tonemapped oddly |
| **Cutscene bars** | letterbox bars | Must remain solid black |
| **Save/load UI** | file select screen | Must remain unchanged |
| **Debug/development overlays** | any dev-only UI | Must not be enhanced |

---

### 2. Detection Methods

#### Method A: is2D Flag (Primary)

**Source**: `interpreter.cpp` — `GfxDpTextureRectangle()` sets `is2D = true`

This is the most reliable signal. Rectangle draws in N64 are used exclusively for 2D UI elements.

**Implementation:**
```cpp
// In DrawCallInfo population:
info.is2D = (current_draw_type == DRAW_TYPE_RECTANGLE);
```

#### Method B: PROTECTED_2D Material Type

**Source**: `MaterialRegistry.h` — `MaterialType::PROTECTED_2D`

Any draw call classified as PROTECTED_2D is excluded from all enhancement passes.

#### Method C: Texture Asset Inventory

**Source**: `visual_workspace/inventories/PROTECTED_2D_ASSETS.csv`

30 catalogued assets with known UI/2D usage. Can be used for additional validation.

#### Method D: Draw Call Context

| Signal | Indicates 2D |
|---|---|
| Rectangle draw (not triangle) | YES |
| Depth test disabled | Likely 2D |
| Z-mode decal | Likely UI overlay |
| Alpha test with full opacity | Likely UI |
| Small texture (≤64×64) | Likely icon/sprite |

---

### 3. Protection Matrix

| Enhancement | PROTECTED_2D Excluded? | How |
|---|---|---|
| PBR material response | YES | Material constant buffer = default, no PBR |
| ACES tonemapping | DEBATABLE | Tonemapping on UI may desaturate icons |
| Atmospheric fog | YES | Post-process must not affect 2D renders |
| SSAO | YES | SSAO must not affect 2D renders |
| Shadow enhancement | YES | N/A for 2D |
| Normal mapping | YES | N/A for 2D |
| Reflection | YES | N/A for 2D |

---

### 4. ACES Tonemapping Decision

**Question: Should ACES tonemapping affect UI elements?**

| Option | Pros | Cons |
|---|---|---|
| Skip UI entirely | UI matches original art | UI may appear too bright vs scene |
| Apply to UI | Consistent final image | Icons may shift color |
| Apply with reduced strength | Compromise | Adds complexity |

**Recommendation**: Apply ACES to the entire framebuffer. UI elements in OoT use the same color space as the scene. Since ACES is a global operator, separating it per-draw would add significant complexity for minimal gain. The original UI colors are already within SDR range and will survive ACES mapping.

**However**: If UI corruption is observed during testing, implement a separate render target for UI that bypasses ACES.

---

### 5. Implementation of PROTECTED_2D Exclusion

**In MaterialRegistry:**
```cpp
MaterialType MaterialRegistry::ClassifyFromState(const DrawCallInfo& info) const {
    if (info.is2D) {
        return MaterialType::PROTECTED_2D;
    }
    // ... rest of classification
}
```

**In rendering pipeline (V03):**
```cpp
// When populating material constant buffer:
if (materialType == MaterialType::PROTECTED_2D) {
    // Use default material params — no PBR, no enhancement
    materialRoughness = 0.7f;
    materialMetallic = 0.0f;
    materialEmissive = 0.0f;
    pbrEnabled = false;
}
```

---

### 6. Edge Cases

| Case | Handling |
|---|---|
| 2D sprite in 3D world (e.g., billboard) | Classified by geometry mode, not draw type |
| Transparent UI overlay on 3D scene | is2D flag catches rectangle draws |
| Full-screen post-process on UI | ACES applied globally; UI survives |
| Animated UI elements | is2D persists across frames |
| Cutscene UI | is2D + scene context |

---

### 7. Verification Checklist

| Test | Expected Result |
|---|---|
| Hearts display correctly | No color shift, no fog |
| Rupee counter readable | Text sharp, no tonemap artifacts |
| Pause menu functional | All icons original colors |
| Navi hints legible | Text unaffected by atmospheric fog |
| Item screen icons | No PBR metallic response on icons |
| Screen fade transitions | Smooth, no banding from tonemap |
| Minimap clarity | No blur from post-process |
| Controller button prompts | No visual change |
