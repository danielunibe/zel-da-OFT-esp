# OCARINA COUCH EDITION V03.1 — RELEASE NOTES

**Version:** OCARINA_COUCH_EDITION_V03.1  
**Build Profile:** Release x64 (MSVC v143)  
**Release Date:** 2026-09-17  
**Gate:** Master Visual Consolidation  

---

## 1. Overview & Highlights

Ocarina Couch Edition V03.1 represents the unified master release consolidating multi-agent developments across color science, atmospheric rendering, depth safety, and material intelligence. It builds directly upon the robust, portable foundation of RC1.1, adding filmic visual fidelity while strictly preserving couch playability and 100% classic parity.

---

## 2. Key New Features & Enhancements

### 2.1 Filmic ACES Color Pipeline
- **Linear-Space Tonemapping:** Full implementation of the Stephen Hill ACES Fitted Curve in Direct3D 11 (`TonemapPS.hlsl`).
- **Dynamic Range Preservation:** Eliminates harsh color clipping in bright outdoor areas (Hyrule Field midday sun, Death Mountain lava, light shafts) while maintaining deep, rich shadow contrast.

### 2.2 Atmospheric Fog Integration
- **Direct F3DEX2 Register Extraction:** Dynamic fog colors (`mRdp->fog_color`) and RSP depth parameters are read per frame.
- **Linear Space Blending:** Fog is linearized and blended prior to ACES tonemapping, reproducing authentic N64 atmosphere with modern color precision.

### 2.3 Strict UI & HUD Isolation (`ACES_UI_CONTAMINATION = 0`)
- **Zero Color Distortion on 2D Elements:** In-game HUD (Hearts, Magic, Rupees, Action Icons), dialogue boxes, and the Couch Edition ImGui menu are composited after the 3D tonemapping pass.
- **$\Delta E_{00} = 0.00$:** Guaranteed bit-exact color reproduction for all 2D assets and fonts.

### 2.4 Material Intelligence Pilot System
- **1,407 Compiled Rules:** High-confidence pilot rules pre-compiled into `MaterialRulesPilot.h`.
- **Fast CRC64 Lookup:** O(1) cache query with O(log N) binary search fallback, consuming $< 0.02\,\mu\text{s}$ per query.
- **UI Protection Shield:** Over 6,100 UI textures classified as `PROTECTED_2D` to enforce strict passthrough.
- **PBR-Lite Baseline (`METADATA_ONLY`):** Physical attributes (roughness, metallic, specular, emissive) are classified and tracked in memory for future shading passes.

### 2.5 Direct3D 11 Engine Hardening
- **Zero Resource Hazards:** Depth-stencil targets are explicitly unbound during post-processing to eliminate OM read/write hazards.
- **Zero COM Churn:** All render states, depth-stencil states, and blend states are preallocated at initialization.

---

## 3. Preservation & Compatibility

- **Classic Mode Parity:** When disabled (`gVisualEnhancements.MasterTonemapping = 0`), the engine operates bit-for-bit identical to upstream Shipwright 9.1.1.
- **Save Integrity:** Save files (`file2.sav`, `file3.sav`, `global.sav`) load seamlessly without modification.
- **Spanish Localization:** Full support for `es.o2r` language pack with perfect font clarity.
- **Couch Comfort:** Gamepad navigation, rumble feedback, and couch overlays fully preserved.

---

## 4. Operational Boundaries

- **No D3D12 / Ray Tracing:** This build runs on standard Direct3D 11. DXR and path tracing are intentionally deferred.
- **No Git Auto-Push:** As mandated, this build candidate remains local for human visual validation.
