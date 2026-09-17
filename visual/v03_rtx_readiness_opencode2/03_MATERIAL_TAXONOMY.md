# 03 — MATERIAL TAXONOMY

## V03 Material Classification System

---

### 1. Classification Sources

Materials must be classified from data already available in the rendering pipeline without modifying gameplay systems.

**Available classification signals:**

| Signal | Source | Reliability |
|---|---|---|
| N64 Color Combiner mode | `shader_id0` / CC bits | HIGH — encodes texture blending strategy |
| Geometry mode bits | `G_FOG`, `G_LIGHTING`, `G_TEXTURE`, etc. | HIGH — per-draw-call |
| Other mode bits | Alpha compare, Z-mode, blend mode | HIGH — per-draw-call |
| Primitive/Env/PrimColor | `mRdp->prim_color`, `env_color` | MEDIUM — game-chosen constants |
| Texture content | Actual texture RGBA data | LOW — expensive at runtime |
| Draw call position in scene | Display list position | LOW — indirect |
| Scene identity | Current scene ID | MEDIUM — contextual |

---

### 2. Proposed V03 Taxonomy

| Category | Classification Heuristic | Expected Prevalence |
|---|---|---|
| **STONE** | Heuristic: non-blended, opaque, specific CC modes + scene context | ~15% |
| **WOOD** | Similar to STONE but different texture/scene context | ~10% |
| **METAL** | Heuristic: specular-like CC response, bright highlights | ~5% |
| **EARTH** | Non-blended, opaque, terrain-like texture patterns | ~10% |
| **GRASS** | Semi-transparent alpha, nature scenes | ~8% |
| **FOLIAGE** | Alpha-blended, nature scenes | ~7% |
| **CLOTH** | Specific CC modes, character/cutscene contexts | ~5% |
| **SKIN** | Character rendering CC modes | ~3% |
| **WATER** | Blue env_color + blend mode + transparency | ~4% |
| **LAVA** | Red env_color + blend mode + transparency | ~2% |
| **GLASS** | High transparency + specific blend mode | ~1% |
| **MAGIC** | Noise mode or 2-cycle mode | ~3% |
| **EMISSIVE** | Bright prim/env colors, additive blend | ~2% |
| **UI_2D** | `is2D` flag, rectangle draws | ~10% |
| **SPRITE** | Billboarded geometry, alpha test | ~5% |
| **UNKNOWN** | Unclassified — default fallback | ~10% |

---

### 3. Classification Decision Tree

```
Is is2D flag set?
  ├─ YES → UI_2D
  └─ NO ↓

Is draw call a rectangle (GfxDpTextureRectangle)?
  ├─ YES → UI_2D
  └─ NO ↓

Is noise mode active (other_mode_h & G_MDMODE cycle)?
  ├─ YES → MAGIC
  └─ NO ↓

Is 2-cycle mode with specific env blend?
  ├─ YES → Check env_color:
  │   ├─ High red → LAVA
  │   ├─ High blue → WATER
  │   └─ Other → MAGIC
  └─ NO ↓

Is transparency active (alpha compare/blend)?
  ├─ YES → Check blend type:
  │   ├─ Additive → EMISSIVE
  │   ├─ Alpha-blended → Check texture context:
  │   │   ├─ Nature scene → FOLIAGE / GRASS
  │   │   ├─ Character → CLOTH / SKIN
  │   │   └─ Other → UNKNOWN
  │   └─ Other → UNKNOWN
  └─ NO ↓

Is prim/env color bright (>0.8)?
  ├─ YES → EMISSIVE
  └─ NO ↓

Is scene known?
  ├─ Temple of Time → STONE / METAL
  ├─ Kokiri Forest → WOOD / FOLIAGE
  ├─ Hyrule Field → EARTH / GRASS
  └─ Other → UNKNOWN

Fallback: UNKNOWN
```

---

### 4. Material Properties Per Category

| Category | Roughness | Metallic | Emissive | Specular | Alpha |
|---|---|---|---|---|---|
| STONE | 0.8 | 0.0 | 0.0 | 0.3 | 1.0 |
| WOOD | 0.85 | 0.0 | 0.0 | 0.2 | 1.0 |
| METAL | 0.35 | 1.0 | 0.0 | 0.9 | 1.0 |
| EARTH | 0.9 | 0.0 | 0.0 | 0.1 | 1.0 |
| GRASS | 0.7 | 0.0 | 0.0 | 0.2 | 0.9 |
| FOLIAGE | 0.7 | 0.0 | 0.0 | 0.2 | 0.8 |
| CLOTH | 0.9 | 0.0 | 0.0 | 0.1 | 1.0 |
| SKIN | 0.7 | 0.0 | 0.0 | 0.4 | 1.0 |
| WATER | 0.1 | 0.0 | 0.0 | 0.9 | 0.7 |
| LAVA | 0.3 | 0.0 | 0.8 | 0.5 | 0.8 |
| GLASS | 0.05 | 0.0 | 0.0 | 1.0 | 0.5 |
| MAGIC | 0.5 | 0.0 | 0.3 | 0.5 | 0.8 |
| EMISSIVE | 0.5 | 0.0 | 1.0 | 0.5 | 1.0 |
| UI_2D | N/A | N/A | N/A | N/A | N/A |
| SPRITE | 0.5 | 0.0 | 0.0 | 0.3 | 0.9 |
| UNKNOWN | 0.7 | 0.0 | 0.0 | 0.3 | 1.0 |

---

### 5. Classification Confidence Levels

| Level | Meaning | Action |
|---|---|---|
| **HIGH** | Multiple signals agree | Apply material properties directly |
| **MEDIUM** | Single strong signal | Apply with conservative defaults |
| **LOW** | Ambiguous or missing signals | Use UNKNOWN fallback |
| **EXCLUDED** | UI/2D detection positive | Skip all PBR processing |

---

### 6. Key Constraints

1. **No gameplay changes** — material classification must not alter game logic
2. **No texture replacement** — metadata overlay only, original textures preserved
3. **Per-draw-call, not per-vertex** — each triangle batch gets one material
4. **Heuristic-based** — true PBR texture maps would require asset pipeline (future phase)
5. **Scene-context dependent** — same CC mode may mean different materials in different scenes
6. **UI must be excluded** — PROTECTED_2D category prevents any enhancement on HUD/menus
