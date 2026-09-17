# COLOR PIPELINE REPORT — V03.1 MASTER CONSOLIDATION
**Date:** 2026-09-17  
**Project:** Ocarina of Time PC — Couch Edition  
**Owner:** Gemini 3.8 High / High Flash  
**Result:** `DOUBLE_GAMMA = NO`  

---

## 1. Color Pipeline Architecture

The graphics backend targets standard dynamic range displays via D3D11 on swapchains formatted as `DXGI_FORMAT_R8G8B8A8_UNORM`. In classic N64 rendering, textures and vertex colors are blended in gamma-encoded space without explicit sRGB linearization.

Applying ACES filmic tonemapping directly to gamma-encoded values would severely crush midtones and oversaturate shadows. Conversely, applying sRGB conversions multiple times would result in washed out or double-darkened output.

---

## 2. Final Color Transformations in ENHANCED Profile

In the Enhanced profile, the color pipeline follows a strict single-conversion model:

```
Fast3D Combiner Output (Gamma Space, R8G8B8A8_UNORM)
       │
       ▼ [HLSL SRGBToLinear: pow(max(c, 0.0), 2.2)]
Linear Radiance Space
       │
       ▼ [HLSL ACESFilm curve evaluation]
Tonemapped Linear Color
       │
       ▼ [Atmospheric Fog Blending with Linearized Fog Color]
Fog-Blended Linear Color
       │
       ▼ [HLSL LinearToSRGB: pow(max(c, 0.0), 1.0 / 2.2)]
Display Encoding (Gamma Space)
       │
       ▼ [Direct Composition]
Backbuffer Present
```

---

## 3. Audit of Pipeline Invariants

| Color Stage | Implementation | Verified Property |
|---|---|---|
| Gamma to Linear Conversion | `SRGBToLinear(color)` | Exactly **1** conversion per pixel |
| Tonemapping Curve | `ACESFilm(linear_color)` | Evaluated strictly in linear space |
| Supplemental Fog Blending | `lerp(color, linearFog, fogFactor * fogColor.a)` | Both color and fog are in linear space |
| Linear to Display Encoding | `LinearToSRGB(linear_color)` | Exactly **1** conversion per pixel |
| Double Gamma Artifacts | None | **DOUBLE_GAMMA = NO** |
| Midtone Crushing | None | Verified smooth gradient distribution |
| UI Contamination | Bypassed | UI elements render directly in native display space |

---

## 4. CLASSIC Profile Invariant

When `gEnhancements.Graphics.VisualProfile == 0` (CLASSIC):
- The tonemapping pass is completely bypassed.
- Color output is 100% identical to Shipwright / RC1.1 baseline.
- Zero color transformations are applied.
