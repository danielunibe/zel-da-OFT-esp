# 15 — LOW-POLY LIGHTING STRATEGY

## N64 Geometry Limitations and Modern Lighting Solutions

---

### 1. N64 Geometry Characteristics

| Characteristic | Detail | Impact on Modern Lighting |
|---|---|---|
| Low polygon count | 50-500 triangles per actor | Faceted lighting, no smooth shading |
| Hard normals | Flat shading per triangle | Sharp lighting transitions |
| No tangent space | Only vertex normals | Cannot do normal mapping |
| Simple UV mapping | Often planar or box projection | Texture stretching on sides |
| Vertex colors | Pre-lit or combiner-generated | No per-pixel lighting data |
| Limited vertex count | 160 vertices max per display list | Very coarse geometry |

---

### 2. Missing Data for Modern Lighting

| Data | Available? | Workaround |
|---|---|---|
| Per-vertex normals | PARTIAL — computed in lighting step | Extract from GBI lighting |
| Tangent vectors | NO | Cannot do tangent-space normal maps |
| Smooth normals | NO — hard edges everywhere | Compute from geometry (edge averaging) |
| World-space position | YES — in VBO | Available for distance-based effects |
| Screen-space position | YES — computed | Available for screen-space effects |
| Depth | YES — DSV | Available for depth-based effects |

---

### 3. Lighting Strategy: Preserve Original, Enhance Response

**Principle: Do NOT try to make low-poly geometry look high-poly. Instead, make the existing geometry respond to light in a more physically convincing way.**

| Approach | Do | Do Not |
|---|---|---|
| PBR response | Roughness/metallic on existing surfaces | Subdivide geometry for smooth normals |
| Shadows | Ray-traced from existing silhouette | Add shadow-casting proxy geometry |
| Reflections | Screen-space or RT on existing surfaces | Create mirror-polished high-poly versions |
| AO | Depth-based SSAO | Add cavity geometry |
| Fog | Depth-based atmospheric | Replace with volumetric fog |
| Emissive | Material-based glow | Add emissive proxy meshes |

---

### 4. Flat Surface Lighting Solution

**Problem**: Flat triangles show uniform lighting across their surface.

**Solution**: Screen-space derivatives for per-pixel lighting variation.

```hlsl
// Compute screen-space derivatives for lighting variation
float3 dPdx = ddx(input.worldPos);
float3 dPdy = ddy(input.worldPos);
float3 flatNormal = normalize(cross(dPdx, dPdy));

// Use flatNormal for lighting instead of vertex normal
float lighting = max(dot(flatNormal, lightDir), 0.0);
```

This gives per-pixel lighting variation even on flat triangles without adding geometry.

---

### 5. Faceted Geometry Enhancement

**Problem**: Low-poly models show obvious flat faces.

**Solutions (ranked by cost):**

| Solution | Cost | Quality | Notes |
|---|---|---|---|
| Screen-space derivatives | LOW | MEDIUM | Per-pixel normal from depth |
| Phong interpolation | LOW | MEDIUM | Fake smooth normals in shader |
| Geometry shader subdivision | HIGH | HIGH | Too expensive for N64-scale models |
| Do nothing | ZERO | LOW | Original N64 appearance preserved |

**Recommendation**: Use screen-space derivatives for lighting. Do NOT subdivide geometry.

---

### 6. Normal Map Alternative

Since tangent-space normal maps cannot be used (no tangent vectors), consider:

| Alternative | Feasibility | Notes |
|---|---|---|
| Screen-space normal perturbation | MEDIUM | Use noise textures to add surface detail |
| Depth-based parallax | LOW | Only for flat surfaces, limited effect |
| Material roughness variation | HIGH | Already in V03 plan |
| Do nothing | ZERO | Preserve original appearance |

**Recommendation**: Focus on roughness/metallic response rather than normal perturbation.

---

### 7. Shadow Quality on Low-Poly

**Problem**: Low-poly shadows are blocky and aliased.

**Solutions:**
| Solution | Cost | Quality |
|---|---|---|
| PCF filtering | LOW | MEDIUM |
| PCSS (soft shadows) | MEDIUM | HIGH |
| RT shadows | HIGH | VERY HIGH |
| Shadow maps at higher resolution | MEDIUM | MEDIUM |

**Recommendation**: PCSS for ENHANCED, RT shadows for RTX.

---

### 8. What NOT to Do

| Anti-Pattern | Why |
|---|---|
| Subdivide all models | Changes game silhouette, performance cost |
| Add tangent vectors to VBO | Changes vertex format, massive pipeline change |
| Replace textures with PBR maps | Requires asset pipeline, not metadata-based |
| Add proxy high-poly meshes | Memory cost, no gameplay benefit |
| Compute smooth normals | May create visual artifacts on sharp edges |

---

### 9. Performance Impact

| Enhancement | GPU Cost | Visual Benefit |
|---|---|---|
| Screen-space derivatives | +0.02ms | Per-pixel lighting variation |
| Roughness-based specular | +0.05ms | Material distinction |
| PCSS shadows | +0.3ms | Soft, realistic shadows |
| SSAO | +0.2ms | Ambient occlusion depth |
| Atmospheric fog | +0.08ms | Distance haze |
| **Total** | **+0.65ms** | Significant improvement |

All within ENHANCED budget at 85 Hz.
