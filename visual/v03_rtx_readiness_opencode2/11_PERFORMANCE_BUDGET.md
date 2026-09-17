# 11 — PERFORMANCE BUDGET

## Frame Time and Resource Budgets

---

### 1. Target Hardware Context

| Parameter | Value |
|---|---|
| Resolution | 3440×1440 (Ultrawide) |
| Refresh rate | 85 Hz |
| Frame budget | 11.76ms (1000/85) |
| GPU target | RTX 3070 / RX 6800 or better |
| CPU target | Modern 6+ core |
| API | DirectX 11 |

---

### 2. Baseline Measurements (CLASSIC Profile)

| Metric | Estimated Current | Measurement Method |
|---|---|---|
| CPU frame time | ~2-4ms | Profile existing SoH |
| GPU frame time | ~3-6ms | GPU profiler |
| Draw calls per frame | ~500-1500 | Frame capture |
| Texture switches per frame | ~200-500 | Frame capture |
| Triangles per frame | ~50k-200k | Frame capture |
| VRAM usage | ~500MB-1GB | Task manager |
| Shader compilations | One-time at startup | D3D11 debug layer |

**Budget remaining at 85 Hz**: 11.76ms - (4ms CPU + 6ms GPU) = **1.76ms headroom**

---

### 3. CLASSIC Profile Budget

| Stage | Budget | Notes |
|---|---|---|
| Fast3D command processing | 1.5ms | Display list interpretation |
| Vertex transform | 0.5ms | MVP + lighting |
| VBO batching | 0.3ms | CPU-side vertex assembly |
| D3D11 state management | 0.2ms | State change overhead |
| Texture binding | 0.3ms | PSSetShaderResources |
| Draw calls | 2.0ms | GPU-side triangle submission |
| Framebuffer ops | 0.2ms | Clear, copy |
| Present | 0.1ms | DXGI swap |
| **TOTAL** | **5.1ms** | Well within budget |

---

### 4. ENHANCED Profile Budget Additions

| Stage | Added Cost | Cumulative | Notes |
|---|---|---|---|
| MaterialRegistry classify | +0.05ms | +0.05ms | Integer comparisons per draw |
| Material property lookup | +0.02ms | +0.07ms | Map lookup per draw |
| Constant buffer update | +0.03ms | +0.10ms | Per-draw CB update |
| PBR lighting (per pixel) | +0.15ms | +0.25ms | GGX approximation |
| ACES tonemapping | +0.10ms | +0.35ms | Fullscreen post-process |
| Atmospheric fog | +0.08ms | +0.43ms | Depth-based post-process |
| **TOTAL ADDED** | **+0.43ms** | | ~4% of frame budget |

**Enhanced profile estimated total: 5.53ms** — well within 11.76ms budget.

---

### 5. RTX Experimental Budget (Future)

| Stage | Added Cost | Cumulative | Notes |
|---|---|---|---|
| All ENHANCED features | +0.43ms | +0.43ms | Baseline enhanced |
| BLAS construction | +0.5ms | +0.93ms | One-time per scene |
| TLAS update | +0.1ms | +1.03ms | Per-frame instance update |
| RT shadows | +1.0ms | +2.03ms | 1 ray per pixel |
| RT reflections | +1.5ms | +3.53ms | 1 ray per pixel |
| Denoising | +0.5ms | +4.03ms | Temporal denoiser |
| Motion vectors | +0.1ms | +4.13ms | Per-vertex velocity |
| **TOTAL ADDED** | **+4.13ms** | | ~35% of frame budget |

**RTX experimental estimated total: 9.23ms** — tight but feasible at 85 Hz.

---

### 6. Expensive Stages (Ordered by Cost)

| Rank | Stage | Cost | Optimization |
|---|---|---|---|
| 1 | RT reflections | 1.5ms | Half-res, limited bounces |
| 2 | RT shadows | 1.0ms | Adaptive ray count |
| 3 | PBR lighting | 0.15ms | Per-pixel (unavoidable) |
| 4 | ACES tonemapping | 0.10ms | Already optimal |
| 5 | Atmospheric fog | 0.08ms | Half-res depth sample |
| 6 | Material classification | 0.05ms | Already optimal |

---

### 7. Metrics to Capture

| Metric | Tool | Target |
|---|---|---|
| CPU frame time | DXGI debug, CPU profiler | <4ms |
| GPU frame time | D3D11 GPU profiler | <8ms (Enhanced), <10ms (RTX) |
| Draw calls per frame | RenderDoc / PIX | <2000 |
| Texture switches per frame | RenderDoc / PIX | <600 |
| Triangles per frame | RenderDoc / PIX | <200k |
| VRAM usage | D3D11 debug, Task Manager | <2GB |
| Shader compilation time | D3D11 debug | <5s total |
| 1% low frame time | Custom timer | >85% of average |
| Frame time variance | Custom timer | <1ms standard deviation |

---

### 8. 1% Low Analysis

**Definition**: 1% worst frame times must still meet budget.

| Scenario | Risk | Mitigation |
|---|---|---|
| Shader compilation stall | One-time, visible as hitch | Pre-compile at startup |
| Scene transition | New materials, new classifications | Cache classifications |
| Large draw call spike | Many small triangles | Batch optimization |
| VRAM pressure | Texture streaming | LOD system (future) |

**Target**: 1% low > 8ms at 85 Hz (within 2ms of average).

---

### 9. Scaling Strategy

| Quality Level | Features | GPU Cost |
|---|---|---|
| CLASSIC | N64 pipeline only | Baseline |
| ENHANCED-LOW | PBR + Tonemap only | +0.25ms |
| ENHANCED-MED | PBR + Tonemap + Fog | +0.43ms |
| ENHANCED-HIGH | PBR + Tonemap + Fog + SSAO | +0.70ms |
| RTX-EXPERIMENTAL | All + RT shadows | +2.0ms |
| RTX-FULL | All + RT reflections | +4.0ms |

---

### 10. VRAM Budget

| Category | CLASSIC | ENHANCED | RTX |
|---|---|---|---|
| Textures | 500MB | 500MB | 500MB |
| Render targets | 50MB | 100MB | 200MB |
| Depth buffers | 30MB | 30MB | 30MB |
| BLAS/TLAS | 0 | 0 | 200MB |
| Denoiser history | 0 | 0 | 100MB |
| Constant buffers | 1MB | 2MB | 5MB |
| **TOTAL** | **~581MB** | **~632MB** | **~1,035MB** |

RTX stays within 1GB VRAM overhead — acceptable for modern GPUs.
