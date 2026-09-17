# PBR-LITE REALITY CHECK & CAPABILITY AUDIT

**Release Candidate:** Ocarina Couch Edition V03.1  
**Author:** Gemini 3.8 Master Integration Owner  
**Date:** 2026-09-17  
**Status:** AUDITED & CERTIFIED (METADATA_ONLY)  

---

## 1. Explicit Status Declaration

| Capability Claim | Actual Implementation in V03.1 | Status |
|---|---|---|
| **PBR Material Classification** | Static & runtime tagging of roughness, metallic, emissive, category | **ACTIVE (METADATA)** |
| **Material Hash Registry** | CRC64 lookup in `MaterialRegistry` with pilot table | **ACTIVE (RUNTIME)** |
| **ACES Tonemapping & Color Grading** | D3D11 linear-space ACES Fitted Curve on 3D geometry | **ACTIVE (SHADER)** |
| **Dynamic Atmospheric Fog** | F3DEX2 scene fog linearization & post-process blending | **ACTIVE (SHADER)** |
| **Cook-Torrance / Microfacet BRDF** | Full specular BRDF ($D \cdot F \cdot G / (4(\vec{n}\cdot\vec{l})(\vec{n}\cdot\vec{v}))$) | **NOT IMPLEMENTED** |
| **Roughness / Metallic Texture Maps** | Dedicated auxiliary roughness/normal maps | **NOT IMPLEMENTED** |
| **Ray-Traced / Screen-Space Reflections** | DXR / Screen-space raymarching on metallic/water | **NOT IMPLEMENTED** |
| **PBR-Lite Overall Classification** | Accurate, verified material tagging and foundation | **`METADATA_ONLY`** |

---

## 2. Detailed Technical Audit

### 2.1 What Is Implemented
1. **Material Intelligence Engine:**
   - Every texture loaded by the engine is classified into semantic categories (`STONE`, `WOOD`, `METAL`, `WATER`, `FOLIAGE`, `UI_2D`, `UNKNOWN`).
   - Each material entry carries normalized physical parameters (`roughness`, `metallic`, `specular`, `emissive`).
   - UI elements (over 6,100 resources) are strictly shielded from 3D post-processing.
2. **Color Science Pipeline:**
   - Full linear-space ACES color grading and filmic curve mapping in D3D11 fullscreen quad shader (`TonemapPS.hlsl`).
   - Native N64 fog integration in linear color space.

### 2.2 What Is Deliberately Deferred to Future Versions
1. **BRDF Shader Pipeline:**
   - LibUltraShip's current rasterizer translates N64 display lists into standard D3D11 vertex/pixel shaders that replicate the N64 CC (Color Combiner).
   - Evaluating a true PBR BRDF requires normal maps, view vectors, roughness sampling, and multi-light accumulation passes that are outside the scope of V03.1.
2. **Raytracing / DXR:**
   - No D3D12/DXR runtime code exists or is loaded in V03.1.
   - The engine operates reliably on standard D3D11 hardware.

---

## 3. Engineering Conclusion

V03.1 Master establishes the foundational material intelligence architecture. All 1,407 pilot material rules are active, tracked, and ready in memory. However, claims of full physically-based lighting must remain strictly bounded: **V03.1 PBR-Lite is certified as `METADATA_ONLY`**.
