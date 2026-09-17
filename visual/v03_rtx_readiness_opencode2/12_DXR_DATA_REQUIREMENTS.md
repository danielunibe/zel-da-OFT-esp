# 12 — DXR DATA REQUIREMENTS

## Ray Tracing Acceleration Structure Requirements

---

### 1. Current Geometry Representation

**N64 Fast3D pipeline output:**
- Triangles batched into VBOs per draw call
- No persistent geometry buffer — VBOs rebuilt every frame
- No acceleration structure — rasterization only
- Vertex format: position(4) + UV(4) + fog(4) + grayscale(4) + inputs(4) = 16 floats per vertex
- No per-vertex normal data in VBO (normals computed in shader from lighting)

---

### 2. BLAS (Bottom-Level Acceleration Structure) Requirements

#### Static World Geometry

| Requirement | Detail |
|---|---|
| Persistent vertex buffer | Must accumulate geometry across frames |
| Index buffer | Required for DXR BLAS build |
| Normal data | Must be computed or stored for shading |
| Material IDs | Per-triangle material classification for RT shading |
| Rebuild trigger | Scene change only (not per-frame) |
| Update frequency | Never (static) or rare (moving platforms) |

**Challenge**: Current pipeline rebuilds VBOs every frame. DXR requires persistent geometry.

**Solution**: Accumulate unique geometry per scene into a static VB/IB pair. On scene load, build BLAS. Cache across frames.

#### Dynamic Actors (Enemies, NPCs, Link)

| Requirement | Detail |
|---|---|
| Per-instance transforms | Matrix per dynamic actor |
| Update frequency | Every frame (60 Hz) |
| Geometry complexity | Low poly count per actor |
| Animation | Skeletal or vertex animation |
| rebuild | TLAS instance update, not BLAS rebuild |

**Challenge**: N64 actors use vertex animation (not skeletal). Each frame, vertices are transformed by game code.

**Solution**: For V03 RTX experimental, limit RT to static geometry only. Dynamic actors use rasterized shadows/reflections.

#### Alpha-Tested Geometry

| Requirement | Detail |
|---|---|
| Foliage, grass, fences | Many alpha-tested draw calls |
| DXR support | Alpha-to-coverage or alpha test in ray shaders |
| Performance impact | High — alpha test per ray hit |
| Recommendation | Exclude from RT initially, rasterize with alpha test |

#### Transparent Geometry

| Requirement | Detail |
|---|---|
| Water, glass, magic | Transparent materials |
| DXR support | Any-hit shader or transparency classification |
| Recommendation | Exclude from BLAS, handle via screen-space techniques |

---

### 3. TLAS (Top-Level Acceleration Structure) Requirements

| Component | Purpose | Update Rate |
|---|---|---|
| Static world instance | BLAS reference + identity transform | Scene load only |
| Dynamic actor instances | BLAS reference + per-frame transform | Per-frame |
| Light instances | Point/spot light positions for RT lighting | Per-frame |
| Camera instance | View/projection for ray generation | Per-frame |

---

### 4. Instance Transform Requirements

| Object Type | Transform Source | Frequency |
|---|---|---|
| Static world | Identity (already in world space) | Never |
| Moving platforms | Game-provided matrix | Per-frame |
| Doors/drawbridges | Animation state | Per-frame |
| Enemies | Game actor transform | Per-frame |
| Link | Player transform | Per-frame |
| Projectiles | Physics state | Per-frame |

**Challenge**: N64 transform matrices are in `mtx_replacements` map passed to Interpreter::Run(). These are available but not exposed to DXR.

**Solution**: Capture transform matrices during Interpreter::Run() and pass to DXR TLAS update.

---

### 5. Texture/Material Mapping for RT

| Data Needed | Source | Available? |
|---|---|---|
| Albedo color | Color combiner output / texture | YES |
| Roughness | MaterialRegistry classification | AFTER V03 activation |
| Metallic | MaterialRegistry classification | AFTER V03 activation |
| Emissive | MaterialRegistry classification | AFTER V03 activation |
| Normal | NOT AVAILABLE | Would need normal maps |
| Opacity | Alpha channel | YES |

---

### 6. Lighting Data for RT

| Light Type | N64 Source | RT Usage |
|---|---|---|
| Directional (sun) | GBI light commands | Primary shadow caster |
| Point lights | GBI light commands | Local shadows + GI |
| Ambient | GBI ambient light | Base illumination |
| Fog color | GBI fog color | Atmospheric scattering |
| Emissive materials | MaterialRegistry | Area lights |

**Challenge**: N64 supports 8 lights max. RT needs to handle all lights per ray.

**Solution**: Capture light state per frame, upload to GPU buffer for RT shaders.

---

### 7. Data Upload Strategy

```
Per frame:
  1. Interpreter::Run() processes display list
  2. Capture: transforms, lights, camera, material IDs
  3. Upload to GPU constant buffer
  4. DXR dispatch reads buffer for RT queries
```

**Data flow:**
```
Game state → Interpreter → Capture → GPU Buffer → DXR Shader → Output
```

---

### 8. Memory Estimates

| Component | Size | Notes |
|---|---|---|
| Static BLAS | 10-50MB | Depends on scene complexity |
| Dynamic BLAS (per actor) | 0.1-1MB | Low-poly N64 actors |
| TLAS | 0.1MB | Instance references |
| Transform buffer | 0.01MB | 64 actors x 64 bytes |
| Light buffer | 0.001MB | 8 lights x 32 bytes |
| Material buffer | 0.01MB | Per-triangle material IDs |
| **Total overhead** | **~50-100MB** | Within VRAM budget |

---

### 9. Implementation Prerequisite

DXR requires D3D12 or DXR-enabled D3D11 extension. Current D3D11 backend does NOT support DXR natively. Architecture upgrade required before any of this data can be used.
