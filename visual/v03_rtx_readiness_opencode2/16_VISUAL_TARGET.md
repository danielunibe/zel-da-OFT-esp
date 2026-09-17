# 16 — VISUAL TARGET

## Design Philosophy and Constraints

---

### 1. Core Vision

**"Original Ocarina geometry and silhouette with modern physically convincing lighting/material response."**

This is NOT a remake. This is NOT a remaster. This is a **material response enhancement** applied to the original N64 geometry.

---

### 2. Comparable Reference

| Reference | What It Shows | Relevance |
|---|---|---|
| Quake 2 RTX | Simple geometry + RT lighting | Closest analog to V03 vision |
| Minecraft RTX | Blocky geometry + PBR + RT | Proves low-poly + modern lighting works |
| Doom 3 BFG | Simple meshes + modern shading | Material-driven visual quality |

**Key insight**: Modern lighting/material response can dramatically improve visuals even on simple geometry.

---

### 3. Visual Identity Rules

| Rule | Description |
|---|---|
| 1. Geometry preservation | Original N64 polygon count and silhouette preserved |
| 2. Texture preservation | Original textures used, not replaced |
| 3. Artistic intent preservation | N64 art direction maintained |
| 4. Material response enhancement | How surfaces respond to light is modernized |
| 5. No photorealism goal | Stylized N64 look with better lighting |
| 6. No visibility advantage | Enhanced mode does not reveal hidden details |

---

### 4. What Changes vs What Stays

| Changes (Enhanced) | Stays (Both Profiles) |
|---|---|
| Material roughness/metallic response | Original polygon count |
| Tonemapping (ACES) | Original textures |
| Atmospheric depth fog | Original fog behavior |
| Shadow quality | Original shadow placement |
| Specular highlights on metals | Original camera behavior |
| Emissive material glow | Original gameplay mechanics |

---

### 5. Visual Target Per Material

| Material | Visual Target |
|---|---|
| STONE | Rough, matte surface with subtle light response |
| WOOD | Warm, matte, organic appearance |
| METAL | Distinctive metallic sheen, sharp specular |
| WATER | Fresnel-based reflectivity, depth-based color |
| LAVA | Glowing, molten heat with emissive response |
| FOLIAGE | Wax-like subsurface hint on leaves |
| SKIN | Warm subsurface scatter approximation |
| MAGIC | Glowing, mystical particle effects |

---

### 6. Design Constraints

| Constraint | Reason |
|---|---|
| No asset replacement | Preserves original game, no legal issues |
| No gameplay changes | Purely visual enhancement |
| No performance regression >2ms | Must maintain 85 Hz |
| Classic profile identical to SoH | Fallback safety |
| No new geometry | Preserves original art silhouette |
| No new textures | Metadata overlay only |
| No new UVs | Uses existing texture coordinates |

---

### 7. Quality Bar

**Enhanced mode must look:**
- Better than N64 original (obviously)
- Better than SoH default renderer
- Worse than a full remake (by design)
- Comparable to "what if N64 had PBR materials"

**NOT:**
- Photorealistic
- Modern AAA quality
- High-poly remaster
- RTX showcase demo
