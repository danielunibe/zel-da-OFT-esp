# HYRULE FIELD MASTER PILOT REPORT

**Scene Identifier:** `spot00` (Vast Open Overworld)  
**Release Candidate:** Ocarina Couch Edition V03.1  
**Author:** Gemini 3.8 Master Integration Owner  
**Date:** 2026-09-17  
**Status:** VALIDATED IN PILOT  

---

## 1. Scene Profile & Scope

Hyrule Field (`spot00`) serves as the ultimate macro-scale benchmark for dynamic day/night cycles, rotating skyboxes, horizon distance fog, and wide-area terrain blending:
- **505 scene records** (51 scene textures).
- Hyrule Castle drawbridge, moat architecture, barbed wire fencing.
- Dynamic atmosphere: Day, sunset, night, sunrise, and overcast weather transitions.

---

## 2. Material Classification & Rules

| Surface / Asset | Discovered Category | Material Rules Applied | Pilot Behavior |
|---|---|---|---|
| Rolling Plains & Path Dirt | `GRASS` / `EARTH` | Roughness: 0.80, Metallic: 0.00, Specular: 0.05 | Rich contrast, no overexposure under bright sun |
| Drawbridge Chains & Iron Bolts | `METAL` | Roughness: 0.40, Metallic: 0.85, Specular: 0.70 | Clear metallic distinction against wooden bridge |
| Castle Drawbridge Timbers | `WOOD` | Roughness: 0.70, Metallic: 0.00, Specular: 0.15 | Weathered wood finish |
| Castle Moat Water | `WATER` | Roughness: 0.15, Metallic: 0.05, Specular: 0.85 | Fluid alpha layer |
| Dynamic Skybox (`textures/vr_cloud*`) | `SKY` | Emissive / Sky pass | Smooth highlight roll-off during sunset/sunrise |
| Horizon Distance Fog | F3DEX2 Fog | Variable per time of day (Amber, Blue, Purple) | Linearized dynamic blending via ACES constant buffer |

---

## 3. Dynamic Day/Night Cycle Verification

- **Sunrise / Sunset:** Highlight transitions on the horizon maintain filmic compression without channel clipping.
- **Nighttime:** Low-light scenes remain readable and atmospheric; UI elements (A button, clock, B button) remain crisp and bright.
- **Toggle Stress:** Toggling between Enhanced and Classic across midday sun and midnight fog produces seamless instantaneous transitions without shader re-compilation hitches.
