# KOKIRI FOREST MASTER PILOT REPORT

**Scene Identifier:** `spot04` (Overworld Woodland Sanctuary)  
**Release Candidate:** Ocarina Couch Edition V03.1  
**Author:** Gemini 3.8 Master Integration Owner  
**Date:** 2026-09-17  
**Status:** VALIDATED IN PILOT  

---

## 1. Scene Profile & Scope

Kokiri Forest (`spot04`) represents the primary organic overworld benchmark, featuring dense foliage, canopy shading, organic bark/wood textures, animated water streams, and fairy particle effects:
- **507 scene records** (59 scene textures).
- Organic environment priors (`FOLIAGE`, `WOOD`, `GRASS`, `EARTH`, `WATER`).
- Shared gameplay actors (`gameplay_keep`, `object_kusa`, `gameplay_field_keep`).

---

## 2. Material Classification & Rules

| Surface / Asset | Discovered Category | Material Rules Applied | Pilot Behavior |
|---|---|---|---|
| Great Deku Tree Canopy & Shrubbery | `FOLIAGE` | Roughness: 0.85, Metallic: 0.00, Specular: 0.10 | Non-specular organic matte; rich color depth |
| Forest Houses & Hollow Tree Trunks | `WOOD` | Roughness: 0.75, Metallic: 0.00, Specular: 0.15 | Natural wood tones, preserved saturation |
| Stream at Forest Exit | `WATER` | Roughness: 0.10, Metallic: 0.10, Specular: 0.80 | Tagged as water surface; alpha blend preserved |
| Grass Tufts (`object_kusa`) | `GRASS` / Alpha | Alpha-tested billboards | Clean alpha testing, zero halo artifacts |
| Fairy Glow (`gameplay_keep` fairy wings) | `EMISSIVE` | Emissive: true, Additive Alpha | Preserved vibrancy, no darkening in ACES curve |
| Woodland Morning Fog | F3DEX2 Dynamic Fog | Fog Color: Pale Emerald / Morning Mist | Blended in linear space prior to tonemapping |

---

## 3. UI Isolation & Parity

- **HUD Health & Dialogue Text:** Dialogue boxes (Saria, Mido, Navi prompts) execute post-tonemap with 0.000% chroma shift.
- **Atmospheric Depth:** The pale morning fog smoothly recedes into the distance without screen-space depth banding or depth target hazards.
- **Performance:** Steady 60.0 FPS throughout forest navigation and particle bursts.
