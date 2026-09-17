# 14 — TEMPORAL / DENOISING PLAN

## Motion Vectors, History, and Denoiser Requirements

---

### 1. Why Temporal Processing is Required

Ray tracing produces noisy output (1 spp). Without denoising:
- RT shadows: grainy, flickering
- RT reflections: sparkling, unstable
- Indirect illumination: extremely noisy

Temporal denoising accumulates data across frames to produce clean output.

---

### 2. Motion Vectors

**Purpose**:告诉去噪器每个像素在前后帧之间的运动方向。

| Requirement | Detail |
|---|---|
| Per-pixel velocity | Compute from depth buffer + camera motion |
| Object motion | Per-vertex velocity from game transforms |
| Update rate | Every frame |
| Precision | 16-bit float sufficient |
| Format | RG16_FLOAT |

**Current pipeline**: No motion vectors exist. Must be generated.

**Generation method:**
```
1. Render scene to G-buffer (depth + velocity)
2. Velocity = reprojected position difference between frames
3. Upload to denoiser as input
```

---

### 3. History Buffer

**Purpose**:存储前几帧的降噪结果用于时间累积。

| Buffer | Contents | Lifetime |
|---|---|---|
| History color | Previous frame denoised output | 1 frame |
| History depth | Previous frame depth | 1 frame |
| History velocity | Previous frame motion vectors | 1 frame |
| History normal | Previous frame normals | 1 frame |
| Accumulation counter | Frames since last reset | Persistent |

**Reset triggers:**
- Camera teleport (large velocity discontinuity)
- Scene transition
- Fast camera rotation (>45 deg/frame)
- Menu open/close

---

### 4. Denoiser Architecture

**Recommended: Custom temporal denoiser (not NVIDIA NRD initially)**

```
Per frame:
  1. Generate motion vectors
  2. Reproject previous frame to current camera
  3. Compute confidence weight (based on velocity, depth discontinuity)
  4. Blend current noisy sample with reprojected history
  5. Edge-aware spatial filter (bilateral)
  6. Output denoised result
```

**Parameters:**
| Parameter | Range | Default | Purpose |
|---|---|---|---|
| Temporal blend factor | 0.0-1.0 | 0.9 | History weight |
| Spatial filter radius | 1-5 pixels | 2 | Edge-aware blur |
| Depth threshold | 0.01-0.1 | 0.05 | Discontinuity detection |
| Normal threshold | 0.1-0.9 | 0.5 | Edge detection |
| Max history frames | 1-8 | 4 | Accumulation limit |

---

### 5. TAA / DLAA Compatibility

**TAA (Temporal Anti-Aliasing):**
- Jittered projection (sub-pixel jitter each frame)
- Temporal accumulation of aliased samples
- Requires motion vectors
- Can be combined with denoiser

**DLAA (Deep Learning Anti-Aliasing):**
- NVIDIA AI-based AA
- Requires motion vectors + depth
- Better quality than TAA
- Requires SDK integration

**Recommendation**: Start with basic temporal denoiser. Add TAA later. DLAA requires NVIDIA SDK.

---

### 6. DLSS Feasibility

| Requirement | Status | Notes |
|---|---|---|
| D3D12 backend | Required | DLSS needs D3D12 or Vulkan |
| Motion vectors | Required | Must be generated |
| Depth buffer | Available | DSV exists |
| Jittered projection | Required | Sub-pixel jitter per frame |
| Exposure | Required | From ACES tonemapping |
| DLSS SDK | Not integrated | NVIDIA SDK needed |

**DLSS provides:**
- Temporal upscaling (render at lower res, upscale to target)
- Built-in denoising
- Better quality than manual TAA
- Performance gain: 1.5-2x at same quality

**Feasibility**: DLSS is feasible but requires D3D12 backend + SDK integration. Not available in current D3D11 pipeline.

---

### 7. Dynamic Resolution

**Purpose**: When GPU cannot maintain target frame time, reduce internal resolution.

| Parameter | Range | Default | Notes |
|---|---|---|---|
| Min resolution scale | 0.5 | 0.66 | 66% of target resolution |
| Max resolution scale | 1.0 | 1.0 | Full resolution |
| Target frame time | 11.76ms | 11.76ms | 85 Hz |
| Scale adjustment rate | 0.1/frame | 0.05 | How fast to adjust |
| Hysteresis | 0.5ms | 0.5ms | Prevent oscillation |

**Algorithm:**
```
if (gpu_frame_time > target + hysteresis):
    resolution_scale -= scale_adjustment
elif (gpu_frame_time < target - hysteresis):
    resolution_scale += scale_adjustment
resolution_scale = clamp(resolution_scale, min, max)
```

---

### 8. Implementation Phases

| Phase | Feature | Dependencies |
|---|---|---|
| Phase 1 | Motion vector generation | Depth buffer, camera matrices |
| Phase 2 | Basic temporal denoiser | Motion vectors |
| Phase 3 | Edge-aware spatial filter | Phase 2 |
| Phase 4 | TAA integration | Phase 2 + jittered projection |
| Phase 5 | DLSS integration | D3D12 + SDK |
| Phase 6 | Dynamic resolution | Phase 5 + frame time measurement |

---

### 9. Memory Cost

| Buffer | Resolution | Format | Size |
|---|---|---|---|
| Motion vectors | 3440x1440 | RG16_FLOAT | 19MB |
| History color | 3440x1440 | RGBA16_FLOAT | 38MB |
| History depth | 3440x1440 | R32_FLOAT | 19MB |
| History normal | 3440x1440 | RGBA16_FLOAT | 38MB |
| Accumulation | 3440x1440 | R8_UINT | 5MB |
| **Total** | | | **~119MB** |

Within VRAM budget for RTX profile.
