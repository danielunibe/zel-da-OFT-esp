# 13 — RTX ROADMAP

## Staged Ray Tracing Implementation Plan

---

### RTX-01: Ray-Traced Shadows

**Dependencies**: D3D12 backend OR D3D11on12, BLAS for static geometry, TLAS, light buffer

| Aspect | Detail |
|---|---|
| Feature | Replace blob/projected shadows with ray-traced shadows |
| Complexity | MEDIUM |
| GPU cost | +1.0ms |
| Visual impact | HIGH — eliminates shadow acne, peter-panning, blob artifacts |
| Risk | Medium — shadow bias tuning, soft shadow filtering |
| Fallback | Rasterized shadow maps or original blob shadows |
| Alpha-tested geometry | Exclude from shadow rays, use alpha-to-coverage |

**Implementation notes:**
- Primary directional light casts shadow rays
- Point lights cast shadow rays for local shadows
- Soft shadows via ray jittering or PCF filter
- Temporal accumulation for noise reduction

---

### RTX-02: Ray-Traced Reflections

**Dependencies**: RTX-01 complete, motion vectors, depth buffer, material roughness

| Aspect | Detail |
|---|---|
| Feature | Screen-space reflections replaced with RT reflections |
| Complexity | HIGH |
| GPU cost | +1.5ms |
| Visual impact | HIGH — accurate reflections on water, metal, glass |
| Risk | High — transparent geometry, reflection resolution, denoising |
| Fallback | Screen-space reflections (SSR) or cubemap approximations |
| Transparent geometry | Exclude from RT, use screen-space fallback |

**Implementation notes:**
- Reflection rays from camera through screen pixels
- Roughness controls ray spread (rough = blurry reflections)
- Water/metal/glass get priority reflections
- Half-resolution for performance, temporal upscale

---

### RTX-03: Limited Indirect Illumination

**Dependencies**: RTX-02 complete, material albedo/roughness/metallic from V03 PBR

| Aspect | Detail |
|---|---|
| Feature | One-bounce indirect lighting for indoor scenes |
| Complexity | VERY HIGH |
| GPU cost | +1.5ms |
| Visual impact | MEDIUM — light bounces illuminate dark corners |
| Risk | Very high — noise, color bleeding, performance |
| Fallback | Ambient occlusion + ambient light |
| Scope | Indoor scenes only initially |

**Implementation notes:**
- One bounce indirect from sun/point lights
- Clamp indirect energy to prevent fireflies
- Temporal denoiser mandatory
- Indoor-only to limit ray distance

---

### RTX-04: Global Illumination

**Dependencies**: RTX-03 complete, multi-bounce support, advanced denoising

| Aspect | Detail |
|---|---|
| Feature | Full multi-bounce GI for outdoor scenes |
| Complexity | EXTREME |
| GPU cost | +2.0ms |
| Visual impact | VERY HIGH — complete lighting transformation |
| Risk | Extreme — noise, performance, artifact management |
| Fallback | RTX-03 limited GI |
| Scope | Outdoor scenes with distant visibility |

**Implementation notes:**
- Multi-bounce path tracing with Russian roulette
- ReSTIR or similar for light sampling
- Very high denoising requirements
- May need dynamic resolution scaling

---

### RTX-05: Experimental Path Tracing

**Dependencies**: RTX-04 complete, full material system, advanced denoising, DLSS

| Aspect | Detail |
|---|---|
| Feature | Full path tracing for selected scenes |
| Complexity | EXTREME |
| GPU cost | +4.0ms |
| Visual impact | MAXIMUM — photorealistic lighting |
| Risk | Extreme — may not achieve target frame rate |
| Fallback | RTX-04 GI |
| Scope | Temple of Time only, experimental |

**Implementation notes:**
- Full material system (roughness, metallic, emissive, subsurface)
- Multi-bounce diffuse + specular
- Volumetric scattering for fog/atmosphere
- DLSS mandatory for performance
- May require 30 Hz target for full quality

---

### Feature Dependency Graph

```
RTX-01: RT Shadows
    ↓
RTX-02: RT Reflections
    ↓
RTX-03: Limited Indirect Illumination
    ↓
RTX-04: Global Illumination
    ↓
RTX-05: Experimental Path Tracing
```

Each feature requires the previous to be stable.

---

### Risk Summary

| Feature | Risk Level | Primary Risk |
|---|---|---|
| RTX-01 | MEDIUM | Shadow bias, soft shadow quality |
| RTX-02 | HIGH | Transparent geometry, reflection noise |
| RTX-03 | VERY HIGH | Noise, performance, color bleeding |
| RTX-04 | EXTREME | Performance, denoising quality |
| RTX-05 | EXTREME | May not hit 85 Hz target |

---

### Stop Rules

| Rule | Trigger |
|---|---|
| Frame time exceeds budget | >11.76ms at 85 Hz for 3 consecutive frames |
| Visual artifacts | More than 3 noticeable artifacts per minute |
| Memory overflow | VRAM exceeds 8GB |
| Crash rate | More than 1 crash per hour |
| User comfort | Any motion sickness from temporal instability |
