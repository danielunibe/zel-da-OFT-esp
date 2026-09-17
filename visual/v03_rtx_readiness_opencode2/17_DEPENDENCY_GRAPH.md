# 17 — IMPLEMENTATION DEPENDENCY GRAPH

## From Current State to RTX

---

### 1. Current State Assessment

```
RC1.1 (STABLE) ← Currently here
    │
    ├── VisualProfile: UI stub only, NOT connected
    ├── MaterialRegistry: Built but NOT wired
    ├── ACES Tonemapping: Dead code, never called
    ├── Atmospheric Fog: N64 emulation only
    └── Draw-Call Instrumentation: NOT implemented
```

---

### 2. Full Dependency Graph

```
RC1.1 STABLE
    │
    ├─[1] Wire MaterialRegistry
    │       └─ RecordDrawCall() in GfxSpTri1()
    │       └─ FlushFrame() in EndFrame()
    │       └─ VisualProfile CVar gate
    │
    ├─[2] Activate ACES Tonemapping
    │       └─ sRGB render target
    │       └─ RunTonemappingPass() call
    │       └─ Profile gate
    │
    ├─[3] Material Property Application
    │       └─ MaterialRegistry → constant buffer
    │       └─ PBR shader parameters
    │       └─ Profile gate
    │
    ├─[4] PBR-Lite Shader Integration
    │       └─ HLSL template modification
    │       └─ GGX specular approximation
    │       └─ Material-specific response
    │
    ├─[5] Linear/HDR Color Pipeline
    │       └─ sRGB render targets
    │       └─ Linear intermediate
    │       └─ HDR headroom (optional)
    │
    ├─[6] Atmospheric Depth Fog
    │       └─ Depth buffer readback
    │       └─ Post-process fog shader
    │       └─ Per-scene configuration
    │
    ├─[7] SSAO (Optional)
    │       └─ Depth/normal G-buffer
    │       └─ Screen-space AO
    │       └─ Profile gate
    │
    ═══════════════════════════════════
    │  ENHANCED PROFILE COMPLETE  │
    ═══════════════════════════════════
    │
    ├─[8] D3D12 Backend Foundation
    │       └─ D3D12 device + command queue
    │       └─ Resource management
    │       └─ Shader compilation
    │
    ├─[9] BLAS/TLAS Construction
    │       └─ Static geometry accumulation
    │       └─ Dynamic instance management
    │       └─ Scene-change rebuild
    │
    ├─[10] Motion Vectors
    │       └─ Per-pixel velocity generation
    │       └─ History buffer management
    │
    ├─[11] RT Shadows (RTX-01)
    │       └─ Ray generation shader
    │       └─ Shadow denoiser
    │       └─ PCF fallback
    │
    ├─[12] RT Reflections (RTX-02)
    │       └─ Reflection ray generation
    │       └─ Roughness-based ray spread
    │       └─ SSR fallback
    │
    ├─[13] Denoising Pipeline
    │       └─ Temporal accumulator
    │       └─ Edge-aware spatial filter
    │       └─ History reset logic
    │
    ├─[14] TAA/DLAA
    │       └─ Jittered projection
    │       └─ Temporal accumulation
    │       └─ Sharpness pass
    │
    ├─[15] Limited GI (RTX-03)
    │       └─ One-bounce path tracing
    │       └─ Indoor scene detection
    │       └─ Indirect denoiser
    │
    ├─[16] Global Illumination (RTX-04)
    │       └─ Multi-bounce path tracing
    │       └─ Outdoor scene support
    │       └─ Advanced denoiser
    │
    ├─[17] DLSS Integration
    │       └─ NVIDIA SDK
    │       └─ Dynamic resolution
    │       └─ Temporal upscaling
    │
    └─[18] Path Tracing (RTX-05)
            └─ Full material system
            └─ Volumetric scattering
            └─ Maximum quality
```

---

### 3. Critical Path

```
[1] Wire Registry → [3] Material Props → [4] PBR Shader → [5] Color Pipeline → [8] D3D12 → [9] BLAS/TLAS → [11] RT Shadows
```

**Critical path length**: 7 steps

**Estimated time per step**: 1-3 days for architecture, 3-7 days for implementation

---

### 4. Parallel Work Streams

| Stream | Steps | Can Run In Parallel |
|---|---|---|
| Material System | [1] → [3] → [4] | Yes — independent of color pipeline |
| Color Pipeline | [2] → [5] | Yes — independent of material system |
| Atmospheric | [6] | Yes — independent of both |
| Post-Process | [7] | Yes — independent of RT |
| D3D12 Foundation | [8] → [9] | Depends on Enhanced complete |
| RT Features | [10] → [11] → [12] | Depends on D3D12 |
| Denoising | [13] → [14] | Depends on RT |
| Advanced RT | [15] → [16] → [17] → [18] | Sequential from RT-01 |

---

### 5. Milestone Markers

| Milestone | Steps Complete | Profile |
|---|---|---|
| M0: Registry Wired | [1] | Metadata only |
| M1: Tonemap Active | [1] + [2] | Visual change |
| M2: PBR-Lite Active | [1] + [2] + [3] + [4] | Material response |
| M3: Color Pipeline | All through [6] | ENHANCED complete |
| M4: D3D12 Ready | [8] + [9] | RT foundation |
| M5: RT Shadows | [10] + [11] | First RT feature |
| M6: RT Reflections | [12] | Second RT feature |
| M7: Full RTX | All through [18] | Maximum quality |
