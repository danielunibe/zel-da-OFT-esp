# 19 — RTX IMPLEMENTATION PLAN

## Future RTX Implementation Sequence

---

### Prerequisites

| Prerequisite | Status | Notes |
|---|---|---|
| V03 ENHANCED complete | Required | Steps 1-7 from V03 plan |
| D3D12 backend | Required | Current D3D11 does not support DXR |
| NVIDIA RTX hardware | Required | RT cores needed for performance |
| DXR SDK | Required | DirectX Raytracing API |

---

### Step 8: D3D12 Backend Foundation

| Aspect | Detail |
|---|---|
| **Priority** | CRITICAL — RT foundation |
| **Files** | New: `gfx_direct3d12.cpp`, Updated: build system |
| **Dependencies** | V03 complete |
| **Risk** | VERY HIGH — full backend rewrite |
| **Visual result** | None — backend swap |
| **Performance impact** | +0.1ms (D3D12 overhead) |
| **Classic fallback** | Keep D3D11 as fallback backend |
| **Test method** | Visual parity with D3D11, frame time comparison |

**Key tasks:**
1. D3D12 device + command queue + command allocator
2. Resource management (heap allocation, barriers)
3. Root signature + PSO creation
4. Swap chain integration
5. Shader compilation (DXIL)
6. Vertex buffer / constant buffer management
7. Texture upload + descriptor management

**Estimated time**: 5-10 days

---

### Step 9: BLAS/TLAS Construction

| Aspect | Detail |
|---|---|
| **Priority** | HIGH — required for any RT feature |
| **Files** | New: `rtx_acceleration.cpp`, `rtx_acceleration.h` |
| **Dependencies** | Step 8 |
| **Risk** | HIGH — geometry accumulation, scene-change handling |
| **Visual result** | None — infrastructure only |
| **Performance impact** | +0.5ms on scene load, +0.1ms per-frame |
| **Classic fallback** | N/A — RTX profile only |
| **Test method** | RenderDoc capture of acceleration structures |

**Key tasks:**
1. Accumulate static geometry into persistent VB/IB
2. Build BLAS for static world
3. Collect per-frame actor transforms
4. Update TLAS instances
5. Handle scene transitions (rebuild BLAS)
6. Material ID per-triangle for RT shading

**Estimated time**: 3-5 days

---

### Step 10: Motion Vector Generation

| Aspect | Detail |
|---|---|
| **Priority** | HIGH — required for denoising and TAA |
| **Files** | New: `motion_vectors.cpp`, shader additions |
| **Dependencies** | Step 8 |
| **Risk** | MEDIUM — depth reprojection |
| **Visual result** | None — data only |
| **Performance impact** | +0.1ms |
| **Classic fallback** | N/A |
| **Test method** | Visualize motion vectors as color image |

**Key tasks:**
1. Generate per-pixel velocity from depth + camera
2. Handle object vs camera motion
3. Upload as RG16_FLOAT texture
4. Edge case handling (disocclusion, teleport)

**Estimated time**: 2-3 days

---

### Step 11: RT Shadows (RTX-01)

| Aspect | Detail |
|---|---|
| **Priority** | HIGH — first RT feature |
| **Files** | New: `rtx_shadows.cpp`, shadow ray shader |
| **Dependencies** | Steps 9, 10 |
| **Risk** | MEDIUM — shadow bias, soft shadows |
| **Visual result** | Accurate per-pixel shadows |
| **Performance impact** | +1.0ms |
| **Classic fallback** | PCSS rasterized shadows |
| **Test method** | Visual comparison, shadow acne check |

**Key tasks:**
1. Shadow ray generation from light
2. Any-hit shader for alpha-tested geometry
3. Shadow denoiser (temporal)
4. Soft shadow filtering
5. Bias tuning to prevent acne/ peter-panning
6. Fallback to rasterized shadows

**Estimated time**: 3-5 days

---

### Step 12: Denoising Pipeline

| Aspect | Detail |
|---|---|
| **Priority** | HIGH — required for all RT features |
| **Files** | New: `denoiser.cpp`, denoiser shaders |
| **Dependencies** | Step 10 |
| **Risk** | HIGH — temporal stability, ghosting |
| **Visual result** | Clean RT output |
| **Performance impact** | +0.5ms |
| **Classic fallback** | N/A |
| **Test method** | Temporal stability test, ghosting check |

**Key tasks:**
1. Temporal accumulation with motion vector reprojection
2. Edge-aware spatial bilateral filter
3. History confidence weighting
4. Reset logic for disocclusion
5. Integration with RT shadows output

**Estimated time**: 3-5 days

---

### Step 13: RT Reflections (RTX-02)

| Aspect | Detail |
|---|---|
| **Priority** | MEDIUM — second RT feature |
| **Files** | New: `rtx_reflections.cpp`, reflection ray shader |
| **Dependencies** | Steps 11, 12 |
| **Risk** | HIGH — transparent geometry, noise |
| **Visual result** | Accurate reflections on water, metal |
| **Performance impact** | +1.5ms |
| **Classic fallback** | Screen-space reflections |
| **Test method** | Reflection accuracy, noise level |

**Key tasks:**
1. Reflection ray generation from camera
2. Roughness-based ray spread
3. Material-aware reflection color
4. Half-resolution rendering + temporal upscale
5. Transparent geometry exclusion
6. Integration with denoiser

**Estimated time**: 3-5 days

---

### Step 14: TAA Integration

| Aspect | Detail |
|---|---|
| **Priority** | MEDIUM — anti-aliasing improvement |
| **Files** | Updated: shader pipeline, new TAA shader |
| **Dependencies** | Step 10 |
| **Risk** | LOW — standard technique |
| **Visual result** | Smooth edges, reduced aliasing |
| **Performance impact** | +0.1ms |
| **Classic fallback** | Original AA method |
| **Test method** | Edge quality, shimmer test |

**Key tasks:**
1. Jittered projection matrix generation
2. Temporal accumulation with motion vector
3. Sharpness/reconstruction filter
4. Integration with existing rendering

**Estimated time**: 2-3 days

---

### Step 15: Limited GI (RTX-03)

| Aspect | Detail |
|---|---|
| **Priority** | LOW — advanced lighting |
| **Files** | New: `rtx_gi.cpp`, GI ray shader |
| **Dependencies** | Steps 12, 13 |
| **Risk** | VERY HIGH — noise, performance |
| **Visual result** | Indirect lighting in indoor scenes |
| **Performance impact** | +1.5ms |
| **Classic fallback** | Ambient + SSAO |
| **Test method** | Indoor scene lighting quality |

**Key tasks:**
1. One-bounce indirect illumination
2. Indoor scene detection
3. Indirect denoiser
4. Energy clamping
5. Color bleeding management

**Estimated time**: 5-7 days

---

### Summary Table

| Step | Feature | Time | Risk | GPU Cost |
|---|---|---|---|---|
| 8 | D3D12 Backend | 5-10 days | VERY HIGH | +0.1ms |
| 9 | BLAS/TLAS | 3-5 days | HIGH | +0.1ms |
| 10 | Motion Vectors | 2-3 days | MEDIUM | +0.1ms |
| 11 | RT Shadows | 3-5 days | MEDIUM | +1.0ms |
| 12 | Denoiser | 3-5 days | HIGH | +0.5ms |
| 13 | RT Reflections | 3-5 days | HIGH | +1.5ms |
| 14 | TAA | 2-3 days | LOW | +0.1ms |
| 15 | Limited GI | 5-7 days | VERY HIGH | +1.5ms |
| **TOTAL** | | **26-43 days** | | **+4.9ms** |

RTX budget (including Enhanced baseline): 5.53ms + 4.9ms = **10.43ms** — within 11.76ms frame budget at 85 Hz.
